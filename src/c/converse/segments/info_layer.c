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

#include "info_layer.h"
#include "../../util/memory/malloc.h"
#include "../../util/memory/sdk.h"
#include "../../util/style.h"
#include <pebble.h>

// Thoughts, actions and errors are asides rather than conversation, so they
// sit in a tinted card with their icon: gold for a thought, sky for an
// action, rose for an error. Monochrome screens get an outlined white card.
// Rectangular screens put the icon on the left; round screens stack it above
// the centred text.

#define CONTENT_FONT SQUIRE_FONT_BODY
#define ICON_SIZE 18
#define CARD_MARGIN_X (SQUIRE_SPACE_M + SQUIRE_ROUND_INSET)
#define CARD_MARGIN_Y SQUIRE_SPACE_S
#define CARD_PAD SQUIRE_SPACE_S
#define GLYPH_TOP_SLACK 4
#ifdef PBL_ROUND
#define TEXT_LEFT CARD_PAD
#define TEXT_TOP (CARD_PAD + ICON_SIZE + SQUIRE_SPACE_XS)
#define TEXT_ALIGNMENT GTextAlignmentCenter
#else
#define TEXT_LEFT (CARD_PAD + ICON_SIZE + SQUIRE_SPACE_S)
#define TEXT_TOP CARD_PAD
#define TEXT_ALIGNMENT GTextAlignmentLeft
#endif

typedef struct {
  ConversationEntry* entry;
  GDrawCommandImage* icon;
  uint32_t icon_resource;
  TextLayer* content_layer;
  uint16_t content_height;
  char* content_text; // For ConversationEntries without their own text, we need somewhere to stash what we generate.
} InfoLayerData;

static char *prv_get_content_text(InfoLayer *layer);
static int prv_get_content_height(InfoLayer* layer);
static void prv_layer_render(Layer* layer, GContext* ctx);
static char* prv_generate_action_text(ConversationAction* action);
static uint32_t prv_get_icon_resource(ConversationEntry* entry);
static GColor prv_get_tint(ConversationEntry* entry);

static int16_t prv_card_width(int16_t layer_w) {
  return layer_w - CARD_MARGIN_X * 2;
}

static int16_t prv_text_width(int16_t layer_w) {
  return prv_card_width(layer_w) - TEXT_LEFT - CARD_PAD;
}

InfoLayer* info_layer_create(GRect rect, ConversationEntry* entry) {
  Layer* layer = blayer_create_with_data(rect, sizeof(InfoLayerData));
  if (!layer) {
    return NULL;
  }
  InfoLayerData* data = layer_get_data(layer);
  data->entry = entry;
  data->icon = NULL;
  data->icon_resource = 0;
  data->content_text = NULL;
  data->content_height = prv_get_content_height(layer);
  data->content_layer = btext_layer_create(GRect(CARD_MARGIN_X + TEXT_LEFT, CARD_MARGIN_Y + TEXT_TOP - GLYPH_TOP_SLACK,
                                                 prv_text_width(rect.size.w), data->content_height));
  if (!data->content_layer) {
    if (data->content_text) {
      free(data->content_text);
    }
    layer_destroy(layer);
    return NULL;
  }
  text_layer_set_text_alignment(data->content_layer, TEXT_ALIGNMENT);
  text_layer_set_text(data->content_layer, prv_get_content_text(layer));
  text_layer_set_font(data->content_layer, fonts_get_system_font(CONTENT_FONT));
  text_layer_set_background_color(data->content_layer, GColorClear);
  text_layer_set_text_color(data->content_layer, GColorBlack);
  layer_add_child(layer, (Layer *)data->content_layer);
  info_layer_update(layer);
  layer_set_update_proc(layer, prv_layer_render);
  return layer;
}

void info_layer_destroy(InfoLayer* layer) {
  InfoLayerData* data = layer_get_data(layer);
  text_layer_destroy(data->content_layer);
  if (data->content_text) {
    free(data->content_text);
  }
  if (data->icon) {
    gdraw_command_image_destroy(data->icon);
  }
  layer_destroy(layer);
}

ConversationEntry* info_layer_get_entry(InfoLayer* layer) {
  InfoLayerData* data = layer_get_data(layer);
  return data->entry;
}

void info_layer_update(InfoLayer* layer) {
  InfoLayerData* data = layer_get_data(layer);
  // The text pointer can change out underneath us.
  text_layer_set_text(data->content_layer, prv_get_content_text(layer));
  data->content_height = prv_get_content_height(layer);
  GRect frame = layer_get_frame(layer);
  text_layer_set_size(data->content_layer, GSize(prv_text_width(frame.size.w), data->content_height + GLYPH_TOP_SLACK + 2));
  int16_t card_h = TEXT_TOP + data->content_height + CARD_PAD + SQUIRE_SPACE_XS;
#ifndef PBL_ROUND
  if (card_h < ICON_SIZE + CARD_PAD * 2) {
    card_h = ICON_SIZE + CARD_PAD * 2;
  }
#endif
  frame.size.h = card_h + CARD_MARGIN_Y * 2;
  layer_set_frame(layer, frame);
  uint32_t new_resource = prv_get_icon_resource(data->entry);
  if (new_resource != data->icon_resource) {
    if (data->icon) {
      gdraw_command_image_destroy(data->icon);
    }
    data->icon = bgdraw_command_image_create_with_resource(new_resource);
    data->icon_resource = new_resource;
  }
}

static char *prv_get_content_text(InfoLayer *layer) {
  InfoLayerData* data = layer_get_data(layer);
  ConversationEntry* entry = data->entry;
  switch(conversation_entry_get_type(entry)) {
    case EntryTypeThought:
      return conversation_entry_get_thought(entry)->thought;
    case EntryTypeError:
      return conversation_entry_get_error(entry)->message;
    case EntryTypeAction:
      // Assumption: the action text doesn't ever change.
      if (!data->content_text) {
        data->content_text = prv_generate_action_text(conversation_entry_get_action(entry));
      }
      return data->content_text ? data->content_text : "";
    default:
      return "";
  }
}

static int prv_get_content_height(InfoLayer* layer) {
  char* text = prv_get_content_text(layer);
  const GFont font = fonts_get_system_font(CONTENT_FONT);
  const GRect rect = GRect(0, 0, prv_text_width(layer_get_frame(layer).size.w), 10000);
  return graphics_text_layout_get_content_size(text, font, rect, GTextOverflowModeTrailingEllipsis, TEXT_ALIGNMENT).h;
}

static void prv_layer_render(Layer* layer, GContext* ctx) {
  InfoLayerData* data = layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);
  GRect card = GRect(CARD_MARGIN_X, CARD_MARGIN_Y, prv_card_width(bounds.size.w), bounds.size.h - CARD_MARGIN_Y * 2);
  graphics_context_set_fill_color(ctx, prv_get_tint(data->entry));
  graphics_fill_rect(ctx, card, SQUIRE_CORNER, GCornersAll);
#ifndef PBL_COLOR
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_round_rect(ctx, card, SQUIRE_CORNER);
#endif
  if (data->icon) {
#ifdef PBL_ROUND
    GPoint icon_origin = GPoint(card.origin.x + (card.size.w - ICON_SIZE) / 2, card.origin.y + CARD_PAD);
#else
    GPoint icon_origin = GPoint(card.origin.x + CARD_PAD, card.origin.y + CARD_PAD);
#endif
    gdraw_command_image_draw(ctx, data->icon, icon_origin);
  }
}

static char* prv_generate_action_text(ConversationAction* action) {
  const char* text = "";
  switch (action->type) {
    case ConversationActionTypeUpdateChecklist:
      text = "Checklist updated.";
      break;
    case ConversationActionTypeGenericSentence:
      text = action->action.generic_sentence.sentence;
      break;
  }
  char* buffer = bmalloc(strlen(text) + 1);
  if (buffer) {
    strcpy(buffer, text);
  }
  return buffer;
}

static uint32_t prv_get_icon_resource(ConversationEntry* entry) {
  switch(conversation_entry_get_type(entry)) {
    case EntryTypeThought:
      return RESOURCE_ID_LIGHTBULB_ICON;
    case EntryTypeError:
      return RESOURCE_ID_SKULL_ICON;
    case EntryTypeAction: {
      ConversationAction* action = conversation_entry_get_action(entry);
      switch (action->type) {
        default:
          return RESOURCE_ID_COG_ICON;
      }
    }
    default:
      return RESOURCE_ID_COG_ICON;
  }
}

static GColor prv_get_tint(ConversationEntry* entry) {
  switch (conversation_entry_get_type(entry)) {
    case EntryTypeThought:
      return SQUIRE_COLOR_THOUGHT_TINT;
    case EntryTypeError:
      return SQUIRE_COLOR_ERROR_TINT;
    case EntryTypeAction:
    default:
      return SQUIRE_COLOR_ACTION_TINT;
  }
}
