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

#include "segment_layer.h"
#include "../conversation.h"
#include "info_layer.h"
#include "message_layer.h"
#include "../../util/memory/sdk.h"
#include "../../util/style.h"
#include "../../util/logging.h"
#include "../../features.h"

#include <pebble.h>



// The speaker label above the first reply after a prompt: a small gold seal
// and "Squire" in azure ink, so the start of each answer is easy to find when
// scrolling back.
#define NAME_HEIGHT 22
#define NAME_TEXT "Squire"
#define NAME_INSET (SQUIRE_SPACE_M + SQUIRE_ROUND_INSET)
#define SEAL_RADIUS 4

typedef enum {
  SegmentTypeNone,
  SegmentTypeMessage,
  SegmentTypeInfo,
} SegmentType;

typedef struct {
  ConversationEntry* entry;
  bool assistant_label;
  SegmentType type;
  union {
    // fun fact: every member of this union is actually a Layer*.
    // given this, we can get away with using a generic layer for layer-accepting functions.
    // also as a result, there is absolutely no warning if you call the wrong methods on these, sigh.
    Layer* layer;
    InfoLayer* info_layer;
    MessageLayer* message_layer;
  };
} SegmentLayerData;

static SegmentType prv_get_segment_type(ConversationEntry* entry);

static void prv_draw_label(Layer *layer, GContext *ctx) {
  SegmentLayerData* data = layer_get_data(layer);
  if (!data->assistant_label) {
    return;
  }
  GRect bounds = layer_get_bounds(layer);
  GFont font = fonts_get_system_font(SQUIRE_FONT_CAPTION);
  const int16_t seal_w = SEAL_RADIUS * 2 + SQUIRE_SPACE_S;
#ifdef PBL_ROUND
  GSize text = graphics_text_layout_get_content_size(NAME_TEXT, font, GRect(0, 0, bounds.size.w, NAME_HEIGHT),
                                                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  int16_t x = (bounds.size.w - seal_w - text.w) / 2;
#else
  int16_t x = NAME_INSET;
#endif
  const int16_t mid = SQUIRE_SPACE_S + 9;
  graphics_context_set_fill_color(ctx, SQUIRE_COLOR_GOLD);
  graphics_fill_circle(ctx, GPoint(x + SEAL_RADIUS, mid), SEAL_RADIUS);
#ifndef PBL_COLOR
  // Gold falls back to white; ring it so the seal still shows.
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_circle(ctx, GPoint(x + SEAL_RADIUS, mid), SEAL_RADIUS);
#endif
  graphics_context_set_text_color(ctx, SQUIRE_COLOR_AZURE_INK);
  graphics_draw_text(ctx, NAME_TEXT, font, GRect(x + seal_w, SQUIRE_SPACE_S, bounds.size.w - x - seal_w, NAME_HEIGHT),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

SegmentLayer* segment_layer_create(GRect rect, ConversationEntry* entry, bool assistant_label) {
  Layer* layer = blayer_create_with_data(rect, sizeof(SegmentLayerData));
  if (!layer) {
    return NULL;
  }
  SegmentLayerData* data = layer_get_data(layer);
  data->entry = entry;
  data->type = prv_get_segment_type(entry);
  data->layer = NULL;
  GRect child_frame = GRect(0, 0, rect.size.w, rect.size.h);
  data->assistant_label = assistant_label;
  if (assistant_label) {
    child_frame = GRect(0, NAME_HEIGHT, rect.size.w, rect.size.h - NAME_HEIGHT);
    layer_set_update_proc(layer, prv_draw_label);
  }
  switch (data->type) {
    case SegmentTypeNone:
      break;
    case SegmentTypeMessage:
      data->message_layer = message_layer_create(child_frame, entry);
      break;
    case SegmentTypeInfo:
      data->info_layer = info_layer_create(child_frame, entry);
      break;
  }
  if (!data->layer) {
    // Out of memory (or an entry type with nothing to show).
    layer_destroy(layer);
    return NULL;
  }
  layer_add_child(layer, data->layer);
  GSize child_size = layer_get_frame(data->layer).size;
  GRect final_size = GRect(rect.origin.x, rect.origin.y, child_size.w, child_size.h);
  if (data->assistant_label) {
    final_size.size.h += NAME_HEIGHT;
  }
  layer_set_frame(layer, final_size);
  return layer;
}

void segment_layer_destroy(SegmentLayer* layer) {
  SQUIRE_LOG(APP_LOG_LEVEL_INFO, "destroying SegmentLayer %p.", layer);
  SegmentLayerData* data = layer_get_data(layer);
  switch (data->type) {
    case SegmentTypeNone:
      break;
    case SegmentTypeMessage:
      message_layer_destroy(data->message_layer);
      break;
    case SegmentTypeInfo:
      info_layer_destroy(data->info_layer);
      break;
  }
  layer_destroy(layer);
}

ConversationEntry* segment_layer_get_entry(SegmentLayer* layer) {
  SegmentLayerData* data = layer_get_data(layer);
  return data->entry;
}

void segment_layer_update(SegmentLayer* layer) {
  SegmentLayerData* data = layer_get_data(layer);
  switch (data->type) {
    case SegmentTypeNone:
      break;
    case SegmentTypeMessage:
      message_layer_update(data->message_layer);
      break;
    case SegmentTypeInfo:
      info_layer_update(data->info_layer);
      break;
  }
  GSize child_size = layer_get_frame(data->layer).size;
  GPoint origin = layer_get_frame(layer).origin;
  GRect final_frame = GRect(origin.x, origin.y, child_size.w, child_size.h);
  if (data->assistant_label) {
    final_frame.size.h += NAME_HEIGHT;
  }
  layer_set_frame(layer, final_frame);
}

static SegmentType prv_get_segment_type(ConversationEntry* entry) {
  switch (conversation_entry_get_type(entry)) {
    case EntryTypeDeleted:
      return SegmentTypeNone;
    case EntryTypePrompt:
    case EntryTypeResponse:
      return SegmentTypeMessage;
    case EntryTypeThought:
    case EntryTypeError:
    case EntryTypeAction:
      return SegmentTypeInfo;
  }
  SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Unknown entry type %d.", conversation_entry_get_type(entry));
  return SegmentTypeNone;
}
