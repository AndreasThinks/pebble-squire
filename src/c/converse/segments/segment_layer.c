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
#include "../../util/logging.h"
#include "../../features.h"

#include <pebble.h>



#define CONTENT_FONT FONT_KEY_GOTHIC_24_BOLD
#define NAME_HEIGHT 20

typedef enum {
  SegmentTypeNone,
  SegmentTypeMessage,
  SegmentTypeInfo,
} SegmentType;

typedef struct {
  ConversationEntry* entry;
  TextLayer* assistant_label_layer;
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

SegmentLayer* segment_layer_create(GRect rect, ConversationEntry* entry, bool assistant_label) {
  Layer* layer = blayer_create_with_data(rect, sizeof(SegmentLayerData));
  SegmentLayerData* data = layer_get_data(layer);
  data->entry = entry;
  data->type = prv_get_segment_type(entry);
  GRect child_frame = GRect(0, 0, rect.size.w, rect.size.h);
  if (assistant_label) {
    data->assistant_label_layer = btext_layer_create(GRect(5, 0, rect.size.w, NAME_HEIGHT));
    layer_add_child(layer, text_layer_get_layer(data->assistant_label_layer));
    text_layer_set_text(data->assistant_label_layer, "Squire");
#ifdef PBL_PLATFORM_GABBRO
    text_layer_set_text_alignment(data->assistant_label_layer, GTextAlignmentCenter);
#endif
    child_frame = GRect(0, NAME_HEIGHT, rect.size.w, rect.size.h - NAME_HEIGHT);
  } else {
    data->assistant_label_layer = NULL;
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
  layer_add_child(layer, data->layer);
  GSize child_size = layer_get_frame(data->layer).size;
  GRect final_size = GRect(rect.origin.x, rect.origin.y, child_size.w, child_size.h);
  if (data->assistant_label_layer) {
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
  if (data->assistant_label_layer) {
    text_layer_destroy(data->assistant_label_layer);
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
  if (data->assistant_label_layer) {
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
