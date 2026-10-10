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
#include "util/style.h"
#include <pebble.h>

#include "util/memory/sdk.h"

#define GREETING_FONT FONT_KEY_GOTHIC_28_BOLD
#define CAPTION_FONT FONT_KEY_GOTHIC_14_BOLD
// The caption tucks up into the empty bottom of the greeting's line box, so a
// second line costs the squire as little height as possible.
#define CAPTION_TUCK 4
// The artwork's intrinsic size.
#define SQUIRE_W 171
#define SQUIRE_H 177
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
#define SQUIRE_CROP 0
#else
// Small screens let the legs crop off the bottom edge.
#define SQUIRE_CROP 60
#endif

typedef struct {
  GPerimeter perimeter;
  Layer *layer;
  const char* text;
  const char* caption;
  GDrawCommandImage* squire;
  GSize text_size;
  GSize caption_size;
  GTextAttributes *text_attributes;
} TalkingSquireLayerData;

static void prv_update_layer(Layer *layer, GContext *ctx);
static GTextAttributes *prv_create_text_attributes(TalkingSquireLayer *layer);
static GRangeHorizontal prv_perimeter_callback(const GPerimeter *perimeter, const GSize *ctx_size, GRangeVertical vertical_range, uint16_t inset);

// Where the squire is drawn. The caption line pushes him down (cropping a
// little more of him) so the speech bubble never leaves the top of the screen.
static int prv_caption_height(const TalkingSquireLayerData *data) {
  return data->caption ? data->caption_size.h + SQUIRE_SPACE_XS - CAPTION_TUCK : 0;
}

static GPoint prv_squire_origin(const TalkingSquireLayerData *data, GSize size) {
  const int caption_h = prv_caption_height(data);
#if defined(PBL_PLATFORM_EMERY)
  const int x = 0;
#else
  const int x = (size.w - SQUIRE_W) / 2;
#endif
  return GPoint(x, size.h - SQUIRE_H + SQUIRE_CROP + caption_h);
}

TalkingSquireLayer *talking_squire_layer_create(GRect frame) {
  Layer *layer = blayer_create_with_data(frame, sizeof(TalkingSquireLayerData));
  if (!layer) {
    return NULL;
  }
  TalkingSquireLayerData *data = layer_get_data(layer);
  data->perimeter = (GPerimeter) { .callback = prv_perimeter_callback };
  data->layer = layer;
  data->text = NULL;
  data->caption = NULL;
  data->text_size = GSizeZero;
  data->caption_size = GSizeZero;
  data->squire = bgdraw_command_image_create_with_resource(RESOURCE_ID_ROOT_SCREEN_SQUIRE);
  data->text_attributes = prv_create_text_attributes(layer);
  layer_set_update_proc(layer, prv_update_layer);
  return layer;
}

void talking_squire_layer_destroy(TalkingSquireLayer *layer) {
  TalkingSquireLayerData *data = layer_get_data(layer);
  if (data->squire) {
    gdraw_command_image_destroy(data->squire);
  }
  if (data->text_attributes) {
    graphics_text_attributes_destroy(data->text_attributes);
  }
  layer_destroy(layer);
}

void talking_squire_layer_set_text(TalkingSquireLayer *layer, const char *text) {
  TalkingSquireLayerData *data = layer_get_data(layer);
  data->text = text;
  GRect bounds = layer_get_bounds(layer);
  data->text_size = graphics_text_layout_get_content_size_with_attributes(text, fonts_get_system_font(GREETING_FONT), GRect(0, 1, bounds.size.w - 23, bounds.size.h - 15), GTextOverflowModeWordWrap, GTextAlignmentLeft, data->text_attributes);
  layer_mark_dirty(layer);
}

void talking_squire_layer_set_caption(TalkingSquireLayer *layer, const char *caption) {
  TalkingSquireLayerData *data = layer_get_data(layer);
  data->caption = caption;
  if (caption) {
    GRect bounds = layer_get_bounds(layer);
    data->caption_size = graphics_text_layout_get_content_size(caption, fonts_get_system_font(CAPTION_FONT), GRect(0, 0, bounds.size.w - 23, 40), GTextOverflowModeWordWrap, GTextAlignmentLeft);
  } else {
    data->caption_size = GSizeZero;
  }
  layer_mark_dirty(layer);
}

static void prv_update_layer(Layer *layer, GContext *ctx) {
  TalkingSquireLayerData *data = layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);
  GSize size = bounds.size;

  const int caption_h = prv_caption_height(data);
  const int content_w = data->text_size.w > data->caption_size.w ? data->text_size.w : data->caption_size.w;
  const int text_height = data->text_size.h + 5 + caption_h;
  const int available_space = bounds.size.w - 18 - content_w - 10;
  const int bubble_width = size.w - 16 - available_space;
  const int corner_offset = 6;
  const int padding = 4;

  const GPoint squire = prv_squire_origin(data, size);

#ifdef PBL_ROUND
  const int bubble_x = size.w - bubble_width - 45;
#else
  const int bubble_x = 8 + available_space;
#endif

  // Position bubble just above the squire on all platforms
  const int bubble_overlap = 2;
  const int bubble_y_offset = 9;
  const int speech_bubble_top = squire.y - text_height + bubble_overlap - bubble_y_offset;

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
  const int text_x = bubble_x - padding + corner_offset + 2;
  const int text_y = speech_bubble_top - padding + corner_offset - 5;
  GRect text_bounds = GRect(text_x, text_y, data->text_size.w, data->text_size.h);
  graphics_draw_text(ctx, data->text, fonts_get_system_font(GREETING_FONT), text_bounds, GTextOverflowModeWordWrap, GTextAlignmentLeft, data->text_attributes);
  if (data->caption) {
    // What SELECT will do next, in the squire's own voice.
    graphics_context_set_text_color(ctx, SQUIRE_COLOR_AZURE_INK);
    graphics_draw_text(ctx, data->caption, fonts_get_system_font(CAPTION_FONT),
                       GRect(text_x, text_y + data->text_size.h + 3 - CAPTION_TUCK, data->caption_size.w, data->caption_size.h),
                       GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
  }
  if (data->squire) {
    gdraw_command_image_draw(ctx, data->squire, squire);
  }
}

static GTextAttributes* prv_create_text_attributes(TalkingSquireLayer *layer) {
  TalkingSquireLayerData *data = layer_get_data(layer);
  GTextAttributes *attributes = graphics_text_attributes_create();
  if (!attributes) {
    return NULL;
  }
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
  // Must match prv_update_layer.
  GPoint squire = prv_squire_origin(data, bounds.size);
  GPoint wrap_point = layer_convert_point_to_screen(layer, GPoint(squire.x + SQUIRE_H, squire.y));
  // We know the squire is at the bottom of our layer, so we don't bother worrying about text being rendered past it.
  if (vertical_range.origin_y + vertical_range.size_h < wrap_point.y) {
    // nothing to do here - implement the inset while we're here, though.
    return (GRangeHorizontal) { .origin_x = inset, .size_w = ctx_size->w - inset * 2 };
  } else {
    // The squire is in the way, so we need to indent the text on the left.
    return (GRangeHorizontal) { .origin_x = wrap_point.x + inset, .size_w = ctx_size->w - wrap_point.x - inset * 2 };
  }
}
