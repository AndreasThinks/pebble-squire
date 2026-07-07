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

#include "talking_squire_layer.h"
#include "util/perimeter.h"
#include <pebble.h>

#include "util/memory/sdk.h"

typedef struct {
  GPerimeter perimeter;
  Layer *layer;
  const char* text;
  GDrawCommandImage* squire;
  GSize text_size;
  GTextAttributes *text_attributes;
} TalkingSquireLayerData;

static void prv_update_layer(Layer *layer, GContext *ctx);
static GTextAttributes *prv_create_text_attributes(TalkingSquireLayer *layer);
static GRangeHorizontal prv_perimeter_callback(const GPerimeter *perimeter, const GSize *ctx_size, GRangeVertical vertical_range, uint16_t inset);


TalkingSquireLayer *talking_squire_layer_create(GRect frame) {
  Layer *layer = blayer_create_with_data(frame, sizeof(TalkingSquireLayerData));
  TalkingSquireLayerData *data = layer_get_data(layer);
  data->perimeter = (GPerimeter) { .callback = prv_perimeter_callback };
  data->layer = layer;
  data->text = NULL;
  data->text_size = GSizeZero;
  data->squire = bgdraw_command_image_create_with_resource(RESOURCE_ID_ROOT_SCREEN_SQUIRE);
  data->text_attributes = prv_create_text_attributes(layer);
  layer_set_update_proc(layer, prv_update_layer);
  return layer;
}

void talking_squire_layer_destroy(TalkingSquireLayer *layer) {
  TalkingSquireLayerData *data = layer_get_data(layer);
  gdraw_command_image_destroy(data->squire);
  graphics_text_attributes_destroy(data->text_attributes);
  layer_destroy(layer);
}

void talking_squire_layer_set_text(TalkingSquireLayer *layer, const char *text) {
  TalkingSquireLayerData *data = layer_get_data(layer);
  data->text = text;
  GRect bounds = layer_get_bounds(layer);
  data->text_size = graphics_text_layout_get_content_size_with_attributes(text, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD), GRect(0, 1, bounds.size.w - 23, bounds.size.h - 15), GTextOverflowModeWordWrap, GTextAlignmentLeft, data->text_attributes);
  layer_mark_dirty(layer);
}

static void prv_update_layer(Layer *layer, GContext *ctx) {
  TalkingSquireLayerData *data = layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);
  GSize size = bounds.size;

  const int text_height = data->text_size.h + 5;
  const int available_space = bounds.size.w - 18 - data->text_size.w - 10;
  const int bubble_width = size.w - 16 - available_space;
  const int corner_offset = 6;
  const int padding = 4;

  // Squire position. The artwork is 171x177, drawn at its intrinsic size, so
  // smaller screens centre it and let the legs crop off the bottom edge.
#if defined(PBL_PLATFORM_EMERY)
  const int squire_height = 177;
  const int squire_width = 171;
  const int squire_x = 0;
  const int squire_y = size.h - squire_height;
#elif defined(PBL_PLATFORM_GABBRO)
  const int squire_height = 177;
  const int squire_width = 171;
  const int squire_x = (size.w - squire_width) / 2;
  const int squire_y = size.h - squire_height;
#else
  const int squire_height = 177;
  const int squire_width = 171;
  const int squire_crop = 60;  // legs drawn past the bottom edge
  const int squire_x = (size.w - squire_width) / 2;
  const int squire_y = size.h - squire_height + squire_crop;
#endif

#ifdef PBL_ROUND
  const int bubble_x = size.w - bubble_width - 45;
#else
  const int bubble_x = 8 + available_space;
#endif

  // Position bubble just above the squire on all platforms
  const int bubble_overlap = 2;
  const int bubble_y_offset = 9;
  const int speech_bubble_top = squire_y - text_height + bubble_overlap - bubble_y_offset;

  GPath bubble_path = {
    .num_points = 8,
    .offset = GPoint(bubble_x - padding, speech_bubble_top - padding),
    .rotation = 0,
    .points = (GPoint[]) {
      // top left rounded
      {0, corner_offset},
      {corner_offset, 0},
      // top right rounded
      {bubble_width + padding * 2 - corner_offset, 0},
      {bubble_width + padding * 2, corner_offset},
      // bottom right rounded
      {bubble_width + padding * 2, text_height + padding * 2},
      {bubble_width + padding * 2 - corner_offset, text_height + padding * 2 + corner_offset},
      // bottom left rounded
      {corner_offset, text_height + padding * 2 + corner_offset},
      {0, text_height + padding * 2},
    }
  };
  graphics_context_set_fill_color(ctx, GColorWhite);
  gpath_draw_filled(ctx, &bubble_path);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 4);
  gpath_draw_outline(ctx, &bubble_path);

  graphics_context_set_text_color(ctx, GColorBlack);
  GRect text_bounds = GRect(bubble_x - padding + corner_offset + 2, speech_bubble_top - padding + corner_offset - 5, data->text_size.w, data->text_size.h);
  graphics_draw_text(ctx, data->text, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD), text_bounds, GTextOverflowModeWordWrap, GTextAlignmentLeft, data->text_attributes);
  gdraw_command_image_draw(ctx, data->squire, GPoint(squire_x, squire_y));
}

static GTextAttributes* prv_create_text_attributes(TalkingSquireLayer *layer) {
  TalkingSquireLayerData *data = layer_get_data(layer);
  GTextAttributes *attributes = graphics_text_attributes_create();
  attributes->flow_data.perimeter.impl = &data->perimeter;
  attributes->flow_data.perimeter.inset = 0;
  return attributes;
}


static GRangeHorizontal prv_perimeter_callback(const GPerimeter *perimeter, const GSize *ctx_size, GRangeVertical vertical_range, uint16_t inset) {
  // We don't get a reference to the original layer, but we do get this perimeter pointer. By putting the perimeter at
  // the top of the struct, we can make this cast and get away with it.
  TalkingSquireLayerData *data = (TalkingSquireLayerData*)perimeter;
  Layer *layer = data->layer;
  GRect bounds = layer_get_bounds(layer);
  // the squire is drawn at the bottom-left of the layer
#if defined(PBL_PLATFORM_EMERY)
  const int16_t squire_size = 177;
  const int16_t squire_y_offset = 0;
  const int16_t squire_x = 0;
#elif defined(PBL_PLATFORM_GABBRO)
  const int16_t squire_size = 177;
  const int16_t squire_y_offset = 0;
  const int16_t squire_x = (bounds.size.w - squire_size) / 2;
#else
  // Must match prv_update_layer: centred, with the legs cropped below the
  // bottom edge.
  const int16_t squire_size = 177;
  const int16_t squire_y_offset = 60;
  const int16_t squire_x = (bounds.size.w - 171) / 2;
#endif
  GPoint wrap_point = layer_convert_point_to_screen(layer, GPoint(squire_x + squire_size, bounds.size.h - squire_size + squire_y_offset));
  // We know the squire is at the bottom of our layer, so we don't bother worrying about text being rendered past it.
  if (vertical_range.origin_y + vertical_range.size_h < wrap_point.y) {
    // nothing to do here - implement the inset while we're here, though.
    return (GRangeHorizontal) { .origin_x = inset, .size_w = ctx_size->w - inset * 2 };
  } else {
    // The squire is in the way, so we need to indent the text on the left.
    return (GRangeHorizontal) { .origin_x = wrap_point.x + inset, .size_w = ctx_size->w - wrap_point.x - inset * 2 };
  }
}