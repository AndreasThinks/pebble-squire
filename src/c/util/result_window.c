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

#include "result_window.h"
#include "style.h"
#include "thinking_layer.h"
#include "logging.h"
#include "../util/memory/malloc.h"
#include <pebble.h>

#include "memory/sdk.h"

#if PBL_DISPLAY_WIDTH >= 200
#define BAND_HEIGHT 58
#define GLYPH_RADIUS 18
#define CARD_TITLE_FONT FONT_KEY_GOTHIC_28_BOLD
#else
#define BAND_HEIGHT 46
#define GLYPH_RADIUS 14
#define CARD_TITLE_FONT FONT_KEY_GOTHIC_24_BOLD
#endif
#define TEXT_INSET (SQUIRE_SPACE_M + SQUIRE_ROUND_INSET)
#define BOTTOM_PADDING (SQUIRE_SPACE_L + (SQUIRE_ROUND_INSET ? 30 : 0))

typedef struct {
  StatusBarLayer *status_bar;
  ScrollLayer *scroll_layer;
  Layer *content_layer;
  ThinkingLayer *thinking_layer;
  ResultTone tone;
  char *title_text;
  char *body_text;
  int16_t title_height;
  int16_t body_height;
  AppTimer *timer;
  uint32_t timeout_ms;
  ResultWindowClosedHandler closed_handler;
} ResultWindowData;

// The single live progress card, if any.
static Window *s_progress_window = NULL;

static void prv_window_load(Window* window);
static void prv_window_unload(Window* window);
static void prv_window_appear(Window* window);
static void prv_timer_expired(void* context);

static char *prv_strdup(const char *text) {
  size_t len = strlen(text);
  char *copy = bmalloc(len + 1);
  if (copy) {
    memcpy(copy, text, len + 1);
  }
  return copy;
}

static GColor prv_tone_color(ResultTone tone) {
  switch (tone) {
    case ResultToneSuccess: return SQUIRE_COLOR_SUCCESS;
    case ResultToneWarning: return SQUIRE_COLOR_WARNING;
    case ResultToneError: return SQUIRE_COLOR_ERROR;
    case ResultToneInfo:
    case ResultToneProgress:
    default: return SQUIRE_COLOR_AZURE;
  }
}

Window* result_window_push(ResultTone tone, const char* title, const char* text, uint32_t timeout_ms) {
  if (tone == ResultToneProgress) {
    result_window_dismiss_progress();
  }
  Window* window = bwindow_create();
  ResultWindowData* data = bmalloc(sizeof(ResultWindowData));
  if (!window || !data) {
    goto oom;
  }
  memset(data, 0, sizeof(ResultWindowData));
  data->tone = tone;
  data->timeout_ms = timeout_ms;
  data->title_text = prv_strdup(title);
  data->body_text = prv_strdup(text);
  if (!data->title_text || !data->body_text) {
    free(data->title_text);
    free(data->body_text);
    goto oom;
  }
  window_set_background_color(window, GColorWhite);
  window_set_user_data(window, data);
  window_set_window_handlers(window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
    .appear = prv_window_appear,
  });
  if (tone == ResultToneProgress) {
    s_progress_window = window;
  }
  window_stack_push(window, true);
  return window;
oom:
  SQUIRE_LOG(APP_LOG_LEVEL_ERROR, "Not enough memory to show a status card.");
  free(data);
  if (window) {
    window_destroy(window);
  }
  return NULL;
}

void result_window_push_phone_unreachable(void) {
  result_window_push(ResultToneError, "Phone unreachable",
      "Squire Plus couldn't reach the Pebble app on your phone. Check Bluetooth, then try again.",
      RESULT_WINDOW_PERSISTENT);
}

void result_window_dismiss_progress(void) {
  if (s_progress_window) {
    // Unload clears s_progress_window.
    window_stack_remove(s_progress_window, false);
  }
}

void result_window_set_closed_handler(Window *window, ResultWindowClosedHandler handler) {
  ResultWindowData* data = window_get_user_data(window);
  data->closed_handler = handler;
}

static void prv_draw_glyph(GContext *ctx, ResultTone tone, GPoint c, GColor ink) {
  const int r = GLYPH_RADIUS;
  graphics_context_set_stroke_color(ctx, ink);
  graphics_context_set_fill_color(ctx, ink);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_circle(ctx, c, r);
  const int k = r / 2;
  switch (tone) {
    case ResultToneSuccess:
      graphics_draw_line(ctx, GPoint(c.x - k, c.y), GPoint(c.x - k / 3, c.y + k * 2 / 3));
      graphics_draw_line(ctx, GPoint(c.x - k / 3, c.y + k * 2 / 3), GPoint(c.x + k, c.y - k * 2 / 3));
      break;
    case ResultToneError:
      graphics_draw_line(ctx, GPoint(c.x - k * 2 / 3, c.y - k * 2 / 3), GPoint(c.x + k * 2 / 3, c.y + k * 2 / 3));
      graphics_draw_line(ctx, GPoint(c.x + k * 2 / 3, c.y - k * 2 / 3), GPoint(c.x - k * 2 / 3, c.y + k * 2 / 3));
      break;
    case ResultToneWarning:
      graphics_draw_line(ctx, GPoint(c.x, c.y - k), GPoint(c.x, c.y + k / 4));
      graphics_fill_circle(ctx, GPoint(c.x, c.y + k - 1), 2);
      break;
    case ResultToneInfo:
    default:
      graphics_fill_circle(ctx, GPoint(c.x, c.y - k + 1), 2);
      graphics_draw_line(ctx, GPoint(c.x, c.y - k / 4), GPoint(c.x, c.y + k));
      break;
  }
}

static void prv_content_update(Layer *layer, GContext *ctx) {
  Window *window = *(Window **)layer_get_data(layer);
  ResultWindowData *data = window_get_user_data(window);
  GRect bounds = layer_get_bounds(layer);
  GColor band = prv_tone_color(data->tone);
#ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, band);
  graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, BAND_HEIGHT), 0, GCornerNone);
  GColor ink = gcolor_legible_over(band);
#else
  GColor ink = GColorBlack;
  (void)band;
#endif
  if (data->tone != ResultToneProgress) {
    prv_draw_glyph(ctx, data->tone, GPoint(bounds.size.w / 2, BAND_HEIGHT / 2), ink);
  }
  int16_t y = BAND_HEIGHT + SQUIRE_SPACE_S;
  const int16_t w = bounds.size.w - TEXT_INSET * 2;
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, data->title_text, fonts_get_system_font(CARD_TITLE_FONT),
                     GRect(TEXT_INSET, y, w, data->title_height), GTextOverflowModeWordWrap,
                     GTextAlignmentCenter, NULL);
  y += data->title_height + SQUIRE_SPACE_S;
  graphics_draw_text(ctx, data->body_text, fonts_get_system_font(SQUIRE_FONT_BODY),
                     GRect(TEXT_INSET, y, w, data->body_height), GTextOverflowModeWordWrap,
                     GTextAlignmentCenter, NULL);
}

static void prv_select_clicked(ClickRecognizerRef recognizer, void *context) {
  window_stack_remove((Window *)context, true);
}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_clicked);
}

static void prv_window_load(Window* window) {
  ResultWindowData* data = window_get_user_data(window);
  Layer* root_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root_layer);

  data->status_bar = bstatus_bar_layer_create();
  if (data->status_bar) {
#ifdef PBL_COLOR
    GColor band = prv_tone_color(data->tone);
    status_bar_layer_set_colors(data->status_bar, band, gcolor_legible_over(band));
    status_bar_layer_set_separator_mode(data->status_bar, StatusBarLayerSeparatorModeNone);
#else
    squire_status_bar_result_pane_config(data->status_bar);
#endif
    layer_add_child(root_layer, status_bar_layer_get_layer(data->status_bar));
  }

  const int16_t w = bounds.size.w - TEXT_INSET * 2;
  data->title_height = graphics_text_layout_get_content_size(
      data->title_text, fonts_get_system_font(CARD_TITLE_FONT), GRect(0, 0, w, 1000),
      GTextOverflowModeWordWrap, GTextAlignmentCenter).h + SQUIRE_SPACE_S;
  data->body_height = graphics_text_layout_get_content_size(
      data->body_text, fonts_get_system_font(SQUIRE_FONT_BODY), GRect(0, 0, w, 2000),
      GTextOverflowModeWordWrap, GTextAlignmentCenter).h + SQUIRE_SPACE_S;
  const int16_t content_h = BAND_HEIGHT + SQUIRE_SPACE_S * 2 + data->title_height +
                            data->body_height + BOTTOM_PADDING;

  GRect scroll_frame = GRect(0, STATUS_BAR_LAYER_HEIGHT, bounds.size.w, bounds.size.h - STATUS_BAR_LAYER_HEIGHT);
  data->scroll_layer = bscroll_layer_create(scroll_frame);
  data->content_layer = blayer_create_with_data(GRect(0, 0, bounds.size.w, content_h), sizeof(Window *));
  if (!data->scroll_layer || !data->content_layer) {
    // Out of memory: an empty card can still be dismissed with BACK.
    return;
  }
  *(Window **)layer_get_data(data->content_layer) = window;
  layer_set_update_proc(data->content_layer, prv_content_update);
  scroll_layer_set_shadow_hidden(data->scroll_layer, true);
  scroll_layer_set_content_size(data->scroll_layer, GSize(bounds.size.w, content_h));
  scroll_layer_set_context(data->scroll_layer, window);
  scroll_layer_set_callbacks(data->scroll_layer, (ScrollLayerCallbacks) {
    .click_config_provider = prv_click_config_provider,
  });
  scroll_layer_set_click_config_onto_window(data->scroll_layer, window);
  scroll_layer_add_child(data->scroll_layer, data->content_layer);
#ifdef PBL_ROUND
  scroll_layer_set_paging(data->scroll_layer, true);
#endif
  layer_add_child(root_layer, scroll_layer_get_layer(data->scroll_layer));

  if (data->tone == ResultToneProgress) {
    data->thinking_layer = thinking_layer_create(GRect((bounds.size.w - THINKING_LAYER_WIDTH) / 2,
        (BAND_HEIGHT - THINKING_LAYER_HEIGHT) / 2, THINKING_LAYER_WIDTH, THINKING_LAYER_HEIGHT));
    if (data->thinking_layer) {
      thinking_layer_set_color(data->thinking_layer, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
      layer_add_child(data->content_layer, data->thinking_layer);
    }
  }
}

static void prv_window_unload(Window* window) {
  ResultWindowData* data = window_get_user_data(window);
  if (s_progress_window == window) {
    s_progress_window = NULL;
  }
  if (data->closed_handler) {
    data->closed_handler(window);
  }
  if (data->timer) {
    app_timer_cancel(data->timer);
    data->timer = NULL;
  }
  if (data->thinking_layer) {
    thinking_layer_destroy(data->thinking_layer);
  }
  if (data->content_layer) {
    layer_destroy(data->content_layer);
  }
  if (data->scroll_layer) {
    scroll_layer_destroy(data->scroll_layer);
  }
  if (data->status_bar) {
    status_bar_layer_destroy(data->status_bar);
  }
  free(data->title_text);
  free(data->body_text);
  free(data);
  window_destroy(window);
}

static void prv_window_appear(Window* window) {
  ResultWindowData* data = window_get_user_data(window);
  if (data->timer) {
    app_timer_cancel(data->timer);
    data->timer = NULL;
  }
  if (data->timeout_ms != RESULT_WINDOW_PERSISTENT) {
    data->timer = app_timer_register(data->timeout_ms, prv_timer_expired, window);
  }
}

static void prv_timer_expired(void* context) {
  Window *window = context;
  ResultWindowData *data = window_get_user_data(window);
  data->timer = NULL;
  window_stack_remove(window, true);
}
