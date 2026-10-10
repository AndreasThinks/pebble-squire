/*
 * Copyright 2025 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "message_layer.h"
#include "../../util/memory/malloc.h"
#include "../../util/memory/sdk.h"
#include "../../util/logging.h"
#include "../../util/style.h"

#include <pebble.h>

// The user's words sit in an azure bubble (black on monochrome), pushed to
// the right like a sent message; the agent's reply is the hero, set large on
// plain paper. On round screens both are centred inside the safe inset.

// Gothic draws its glyphs a few pixels below the top of the text box.
#define GLYPH_TOP_SLACK 4
#define BUBBLE_PAD_X SQUIRE_SPACE_M
#define BUBBLE_PAD_TOP 1
#define BUBBLE_PAD_BOTTOM 7
#define BUBBLE_MARGIN_TOP SQUIRE_SPACE_M
#define BUBBLE_MARGIN_BOTTOM SQUIRE_SPACE_S
// Room left of a prompt bubble on rectangular screens, so it reads as "mine".
#define PROMPT_GUTTER 24
#define REPLY_INSET (SQUIRE_SPACE_M + SQUIRE_ROUND_INSET)

typedef struct {
  ConversationEntry* entry;
  TextLayer* content_layer;
  uint16_t content_height;
  size_t last_newline_offset;
  bool is_prompt;
  GRect bubble;
} MessageLayerData;

static char *prv_get_content_text(MessageLayer *layer);
static int prv_get_content_height(MessageLayer* layer);
static void prv_layer_render(Layer* layer, GContext* ctx);

static GFont prv_font(const MessageLayerData *data) {
  return fonts_get_system_font(data->is_prompt ? SQUIRE_FONT_PROMPT : SQUIRE_FONT_MESSAGE);
}

static GTextAlignment prv_alignment(const MessageLayerData *data) {
  return PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft);
}

// The widest the text may be, given the layer width.
static int16_t prv_text_width(const MessageLayerData *data, int16_t layer_w) {
  if (data->is_prompt) {
    return layer_w - BUBBLE_PAD_X * 2 - SQUIRE_SPACE_M - PBL_IF_ROUND_ELSE(SQUIRE_ROUND_INSET * 2, PROMPT_GUTTER);
  }
  return layer_w - REPLY_INSET * 2;
}

// Positions the text (and, for prompts, the bubble around it) for the current
// content height, and resizes the layer to fit.
static void prv_layout(MessageLayer *layer) {
  MessageLayerData* data = layer_get_data(layer);
  GRect frame = layer_get_frame(layer);
  const int16_t w = frame.size.w;
  const int16_t max_text_w = prv_text_width(data, w);
  if (data->is_prompt) {
    // Prompts never stream, so measuring the real width is cheap and lets
    // short prompts get snug bubbles.
    GSize size = graphics_text_layout_get_content_size(prv_get_content_text(layer), prv_font(data),
        GRect(0, 0, max_text_w, 10000), GTextOverflowModeWordWrap, prv_alignment(data));
    int16_t text_w = size.w + 2 < max_text_w ? size.w + 2 : max_text_w;
    int16_t bubble_w = text_w + BUBBLE_PAD_X * 2;
    int16_t bubble_h = data->content_height + BUBBLE_PAD_TOP + BUBBLE_PAD_BOTTOM;
    int16_t bubble_x = PBL_IF_ROUND_ELSE((w - bubble_w) / 2, w - SQUIRE_SPACE_M - bubble_w);
    data->bubble = GRect(bubble_x, BUBBLE_MARGIN_TOP, bubble_w, bubble_h);
    layer_set_frame(text_layer_get_layer(data->content_layer),
        GRect(bubble_x + BUBBLE_PAD_X, BUBBLE_MARGIN_TOP + BUBBLE_PAD_TOP - GLYPH_TOP_SLACK, text_w, data->content_height + GLYPH_TOP_SLACK + 2));
    frame.size.h = BUBBLE_MARGIN_TOP + bubble_h + BUBBLE_MARGIN_BOTTOM;
  } else {
    layer_set_frame(text_layer_get_layer(data->content_layer),
        GRect(REPLY_INSET, -GLYPH_TOP_SLACK, max_text_w, data->content_height + 5));
    frame.size.h = data->content_height + 5;
  }
  layer_set_frame(layer, frame);
}

MessageLayer* message_layer_create(GRect rect, ConversationEntry* entry) {
  Layer* layer = blayer_create_with_data(rect, sizeof(MessageLayerData));
  if (!layer) {
    return NULL;
  }
  MessageLayerData* data = layer_get_data(layer);
  data->entry = entry;
  data->is_prompt = conversation_entry_get_type(entry) == EntryTypePrompt;
  data->last_newline_offset = 0;
  data->content_height = 24;
  data->content_height = prv_get_content_height(layer);
  data->content_layer = btext_layer_create(GRect(0, 0, prv_text_width(data, rect.size.w), data->content_height));
  if (!data->content_layer) {
    layer_destroy(layer);
    return NULL;
  }
  text_layer_set_text(data->content_layer, prv_get_content_text(layer));
  text_layer_set_font(data->content_layer, prv_font(data));
  text_layer_set_text_alignment(data->content_layer, prv_alignment(data));
  text_layer_set_background_color(data->content_layer, GColorClear);
  text_layer_set_text_color(data->content_layer, data->is_prompt ? GColorWhite : GColorBlack);
  layer_add_child(layer, (Layer *)data->content_layer);
  if (data->is_prompt) {
    layer_set_update_proc(layer, prv_layer_render);
  }
  message_layer_update(layer);
  return layer;
}

void message_layer_destroy(MessageLayer* layer) {
  MessageLayerData* data = layer_get_data(layer);
  text_layer_destroy(data->content_layer);
  layer_destroy(layer);
}

void message_layer_update(MessageLayer* layer) {
  MessageLayerData* data = layer_get_data(layer);
  // The text pointer can change out underneath us.
  text_layer_set_text(data->content_layer, prv_get_content_text(layer));
  data->content_height = prv_get_content_height(layer);
  prv_layout(layer);
}

static void prv_layer_render(Layer* layer, GContext* ctx) {
  MessageLayerData* data = layer_get_data(layer);
  graphics_context_set_fill_color(ctx, SQUIRE_COLOR_AZURE);
  graphics_fill_rect(ctx, data->bubble, SQUIRE_CORNER * 2, GCornersAll);
}

static char *prv_get_content_text(MessageLayer *layer) {
  MessageLayerData* data = layer_get_data(layer);
  ConversationEntry* entry = data->entry;
  switch(conversation_entry_get_type(entry)) {
    case EntryTypePrompt:
      return conversation_entry_get_prompt(entry)->prompt;
    case EntryTypeResponse:
      return conversation_entry_get_response(entry)->response;
    default:
      return "";
  }
}

static int prv_get_content_height(MessageLayer* layer) {
  MessageLayerData* data = layer_get_data(layer);
  // Measuring the whole field every time we add text is way too expensive.
  // Instead, just try to figure out where we are line breaking. We don't get enough information
  // from the text layout engine to do this in any particularly clever way, so instead we try
  // to figure out what's on the current line, and when that spills over to the next line.
  // Then we figure out (guess) exactly what spilled over and repeat from there.
  size_t offset = data->last_newline_offset;
  char* text = prv_get_content_text(layer);
  const GFont font = prv_font(data);
  const GRect rect = GRect(0, 0, prv_text_width(data, layer_get_frame(layer).size.w), 10000);
  // This algorithm is somewhat buggy (it can't cope with words getting broken; it assumes only one break per
  // fragment), so for content where speed is less important just actually measure the thing.
  const GTextAlignment alignment = prv_alignment(data);
  if (conversation_entry_get_type(data->entry) != EntryTypeResponse || conversation_entry_get_response(data->entry)->complete) {
    return graphics_text_layout_get_content_size(text, font, rect, GTextOverflowModeTrailingEllipsis, alignment).h;
  }
  int height = graphics_text_layout_get_content_size(text + offset, font, rect, GTextOverflowModeTrailingEllipsis, alignment).h;
  int content_height = data->content_height;

  if (height > 35) {
    int h2 = 0;
    // we broke to a new line. see if we can figure out where.
    size_t len = strlen(text + offset);
    // Measure prefixes against a scratch copy rather than mutating the live buffer in place:
    // `text` may point into a streaming ConversationResponse->response, and writing NUL into
    // the middle of it (even transiently) violates the buffer's invariant. We also start at
    // len-1 so we never touch the terminator at index len.
    char *scratch = bmalloc(len + 1);
    if (!scratch) {
      // Not enough memory to find the line break; measure the whole thing.
      return graphics_text_layout_get_content_size(text, font, rect, GTextOverflowModeTrailingEllipsis, alignment).h;
    }
    memcpy(scratch, text + offset, len + 1);
    for (int i = (int)len - 1; i >= 0; --i) {
      scratch[i] = 0;
      h2 = graphics_text_layout_get_content_size(scratch, font, rect, GTextOverflowModeTrailingEllipsis, alignment).h;
      if (h2 < height) {
        // now try to backtrack to where in the text we caused this.
        // I can't think of any way to infer this from the sizing, so just go back to a word break.
        for (int j = i - 1; j >= 0; --j) {
          if (scratch[j] == ' ' || scratch[j] == '-' || scratch[j] == '\n') {
            i = j+1;
//            SQUIRE_LOG(APP_LOG_LEVEL_DEBUG, "New line starts \"%s\".", text+offset+i);
            break;
          }
        }
        offset += i;
        break;
      }
    }
    free(scratch);
    data->last_newline_offset = offset;
    content_height += height - h2;
  }

  return content_height;
}
