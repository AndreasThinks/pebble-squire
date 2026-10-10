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
#include <pebble.h>

#define CONTENT_FONT FONT_KEY_GOTHIC_24
#define STRIPE_WIDTH 24
#define TEXT_PADDING_LEFT 2
#define TEXT_PADDING_RIGHT 5
#define UNAVAILABLE_WIDTH (STRIPE_WIDTH + TEXT_PADDING_LEFT + TEXT_PADDING_RIGHT)

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
static GColor prv_get_stripe_color(ConversationEntry* entry);

InfoLayer* info_layer_create(GRect rect, ConversationEntry* entry) {
    Layer* layer = blayer_create_with_data(rect, sizeof(InfoLayerData));
    if (!layer) {
      return NULL;
    }
    InfoLayerData* data = layer_get_data(layer);
    data->entry = entry;
    data->icon = NULL;
    data->content_text = NULL;
    data->content_height = 24;
  data->content_height = prv_get_content_height(layer);
#ifdef PBL_PLATFORM_GABBRO
  data->content_layer = btext_layer_create(GRect(0, 1, rect.size.w, data->content_height));
#else
  data->content_layer = btext_layer_create(GRect(STRIPE_WIDTH + TEXT_PADDING_LEFT, 1, rect.size.w - UNAVAILABLE_WIDTH, data->content_height));
#endif
  if (!data->content_layer) {
    if (data->content_text) {
      free(data->content_text);
    }
    layer_destroy(layer);
    return NULL;
  }
#ifdef PBL_PLATFORM_GABBRO
  text_layer_set_text_alignment(data->content_layer, GTextAlignmentCenter);
#else
  text_layer_set_text_alignment(data->content_layer, GTextAlignmentLeft);
#endif
  text_layer_set_text(data->content_layer, prv_get_content_text(layer));
  text_layer_set_font(data->content_layer, fonts_get_system_font(CONTENT_FONT));
  text_layer_set_background_color(data->content_layer, GColorClear);
    data->content_height = text_layer_get_content_size(data->content_layer).h;
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
  int width = layer_get_bounds((Layer *)data->content_layer).size.w;
  GRect frame = layer_get_frame(layer);
  frame.size.h = data->content_height + 11;
  text_layer_set_size(data->content_layer, GSize(width, data->content_height + 5));
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
      return "(Bobby bug)";
  }
}

static int prv_get_content_height(InfoLayer* layer) {
  char* text = prv_get_content_text(layer);
  const GFont font = fonts_get_system_font(CONTENT_FONT);
#ifdef PBL_PLATFORM_GABBRO
  const GRect rect = GRect(0, 0, layer_get_frame(layer).size.w, 10000);
  GTextAlignment alignment = GTextAlignmentCenter;
#else
  const GRect rect = GRect(0, 0, layer_get_frame(layer).size.w - UNAVAILABLE_WIDTH, 10000);
  GTextAlignment alignment = GTextAlignmentLeft;
#endif
  return graphics_text_layout_get_content_size(text, font, rect, GTextOverflowModeTrailingEllipsis, alignment).h;
}

static void prv_layer_render(Layer* layer, GContext* ctx) {
  InfoLayerData* data = layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);
#ifndef PBL_PLATFORM_GABBRO
  GColor stripe_color = prv_get_stripe_color(data->entry);
  graphics_context_set_fill_color(ctx, stripe_color);
  graphics_fill_rect(ctx, GRect(0, 0, STRIPE_WIDTH, bounds.size.h), 0, GCornerNone);
#endif
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_line(ctx, GPoint(0, 0), GPoint(bounds.size.w, 0));
  graphics_draw_line(ctx, GPoint(0, bounds.size.h - 1), GPoint(bounds.size.w, bounds.size.h - 1));
  if (data->icon) {
    gdraw_command_image_draw(ctx, data->icon, GPoint(3, 10));
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

static GColor prv_get_stripe_color(ConversationEntry* entry) {
#if defined(PBL_COLOR)
  switch (conversation_entry_get_type(entry)) {
    case EntryTypeThought:
      return GColorYellow;
    case EntryTypeError:
      return GColorVeryLightBlue;
    case EntryTypeAction:
      return GColorShockingPink;
    default:
      return GColorOrange;
  }
#else
  return GColorDarkGray;
#endif
}