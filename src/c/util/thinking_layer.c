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

#include "thinking_layer.h"

#include <pebble.h>

#include "memory/sdk.h"
#include "style.h"

#define STEP_MS 400

typedef struct {
  AppTimer *timer;
  uint8_t step;
  GColor color;
  const char *hint;
} ThinkingLayerData;

static void prv_layer_render(Layer* layer, GContext* ctx);
static void prv_step(void *context);

ThinkingLayer* thinking_layer_create(GRect rect) {
  ThinkingLayer* layer = blayer_create_with_data(rect, sizeof(ThinkingLayerData));
  if (!layer) {
    return NULL;
  }
  ThinkingLayerData* data = layer_get_data(layer);
  data->step = 0;
  data->color = GColorBlack;
  data->hint = NULL;
  data->timer = app_timer_register(STEP_MS, prv_step, layer);
  layer_set_update_proc(layer, prv_layer_render);
  return layer;
}

void thinking_layer_destroy(ThinkingLayer *layer) {
  ThinkingLayerData* data = layer_get_data(layer);
  if (data->timer) {
    app_timer_cancel(data->timer);
    data->timer = NULL;
  }
  layer_destroy(layer);
}

void thinking_layer_set_color(ThinkingLayer *layer, GColor color) {
  ThinkingLayerData* data = layer_get_data(layer);
  data->color = color;
  layer_mark_dirty(layer);
}

void thinking_layer_set_hint(ThinkingLayer *layer, const char *hint) {
  ThinkingLayerData* data = layer_get_data(layer);
  if (data->hint == hint) {
    return;
  }
  data->hint = hint;
  if (hint && data->timer) {
    app_timer_cancel(data->timer);
    data->timer = NULL;
  } else if (!hint && !data->timer) {
    data->step = 0;
    data->timer = app_timer_register(STEP_MS, prv_step, layer);
  }
  layer_mark_dirty(layer);
}

static void prv_step(void *context) {
  ThinkingLayer* layer = context;
  ThinkingLayerData* data = layer_get_data(layer);
  data->step = (data->step + 1) % 3;
  data->timer = app_timer_register(STEP_MS, prv_step, layer);
  layer_mark_dirty(layer);
}

static void prv_render_hint(ThinkingLayerData *data, GRect bounds, GContext *ctx) {
  GFont font = fonts_get_system_font(SQUIRE_FONT_CAPTION);
  GSize text = graphics_text_layout_get_content_size(data->hint, font, GRect(0, 0, bounds.size.w, bounds.size.h),
                                                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  const int16_t glyph_w = 12 + SQUIRE_SPACE_S;
  int16_t x = (bounds.size.w - glyph_w - text.w) / 2;
  if (x < 0) {
    x = 0;
  }
  const int16_t mid = bounds.size.h / 2;
  squire_draw_select_glyph(ctx, GPoint(x + 6, mid), data->color);
  graphics_context_set_text_color(ctx, data->color);
  graphics_draw_text(ctx, data->hint, font, GRect(x + glyph_w, mid - 10, bounds.size.w - x - glyph_w, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void prv_layer_render(Layer* layer, GContext* ctx) {
  ThinkingLayerData* data = layer_get_data(layer);
  GRect full = layer_get_bounds(layer);
  if (data->hint) {
    prv_render_hint(data, full, ctx);
    return;
  }
  // Centre the dots in whatever space we were given.
  GRect bounds = full;
  if (bounds.size.w > THINKING_LAYER_WIDTH) {
    bounds.origin.x = (bounds.size.w - THINKING_LAYER_WIDTH) / 2;
    bounds.size.w = THINKING_LAYER_WIDTH;
  }
  if (bounds.size.h > THINKING_LAYER_HEIGHT) {
    bounds.origin.y = (bounds.size.h - THINKING_LAYER_HEIGHT) / 2;
    bounds.size.h = THINKING_LAYER_HEIGHT;
  }
  int max_radius = bounds.size.h / 2;
  int min_radius = max_radius * 2 / 3;
  graphics_context_set_fill_color(ctx, data->color);
  graphics_context_set_stroke_color(ctx, data->color);
  graphics_context_set_stroke_width(ctx, 1);
  int span = bounds.size.w - max_radius * 2 - 1;
  for (int i = 0; i < 3; ++i) {
    GPoint center = GPoint(bounds.origin.x + max_radius + span * i / 2, bounds.origin.y + max_radius);
    if (i == data->step) {
      graphics_fill_circle(ctx, center, max_radius);
    } else {
      // Resting dots are hollow, so the active one reads even in one colour.
      graphics_draw_circle(ctx, center, min_radius);
    }
  }
}
