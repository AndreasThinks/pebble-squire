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

#include "consent.h"
#include "../util/persist_keys.h"
#include "../util/style.h"
#include "../util/result_window.h"
#include "../util/tuple_util.h"
#include "../util/logging.h"
#include "../util/memory/malloc.h"
#include "../util/memory/sdk.h"
#include "../version/version.h"
#include "../root_window.h"

#include <pebble.h>
#include <pebble-events/pebble-events.h>


#define STAGE_LLM_WARNING 0
#define STAGE_AGENT_CONSENT 1
#define STAGE_LOCATION_CONSENT 2

#define STAGE_COUNT 3

// Onboarding is three short pages. Each opens with an azure band carrying the
// page title and progress seals, and ends with a footer naming what SELECT
// does, so it's obvious the page has to be read to the end.
#define TEXT_PADDING (SQUIRE_SPACE_M + SQUIRE_ROUND_INSET)
#ifdef PBL_ROUND
#define TEXT_ALIGNMENT GTextAlignmentCenter
#define HEADER_TOP SQUIRE_SPACE_L
#else
#define TEXT_ALIGNMENT GTextAlignmentLeft
#define HEADER_TOP 0
#endif
#if defined(PBL_PLATFORM_GABBRO)
#define SCROLL_BOTTOM_PADDING 60
#elif defined(PBL_ROUND)
#define SCROLL_BOTTOM_PADDING 40
#else
#define SCROLL_BOTTOM_PADDING 0
#endif
#define BODY_FONT SQUIRE_FONT_BODY
#define HEADER_HEIGHT (HEADER_TOP + 50)
#define BODY_TOP (HEADER_HEIGHT + SQUIRE_SPACE_XS)
#define FOOTER_HEIGHT 30
// How long the frozen location menu waits for the phone to confirm.
#define LOCATION_REPLY_TIMEOUT_MS 15000

typedef struct {
  ScrollLayer *scroll_layer;
  Layer *header_layer;
  Layer *footer_layer;
  TextLayer *text_layer;
  Layer *content_indicator_layer;
  char* current_text;
  const char* title_text;
  GBitmap* select_indicator_bitmap;
  BitmapLayer* select_indicator_layer;
  ActionMenu* action_menu;
  int stage;
  int expected_app_response;
  // The location choice sent to the phone, awaiting its confirmation.
  bool pending_choice;
  AppTimer *response_timer;
  EventHandle app_message_handle;
} ConsentWindowData;

static void prv_set_stage(Window* window, int stage);
static void prv_window_load(Window *window);
static void prv_window_unload(Window *window);
static void prv_window_appear(Window *window);
static void prv_click_config_provider(void *context);
static void prv_select_click_handler(ClickRecognizerRef recognizer, void *context);
static bool prv_did_scroll_to_bottom(Window* window);
static void prv_present_consent_menu(Window* window);
static void prv_consent_menu_select_callback(ActionMenu *action_menu, const ActionMenuItem *action, void *context);
static void prv_action_menu_close(ActionMenu* action_menu, const ActionMenuItem* item, void* context);
static void prv_app_message_handler(DictionaryIterator *iter, void *context);
static void prv_mark_consents_complete();

static void prv_header_update(Layer *layer, GContext *ctx) {
  Window *window = *(Window **)layer_get_data(layer);
  ConsentWindowData *data = window_get_user_data(window);
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, SQUIRE_COLOR_AZURE);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  // Progress seals: done and current pages filled gold, later pages hollow.
  const int16_t r = 3;
  const int16_t gap = 12;
  const int16_t seals_w = gap * (STAGE_COUNT - 1);
  const int16_t y = HEADER_TOP + 10;
  for (int i = 0; i < STAGE_COUNT; ++i) {
    GPoint c = GPoint(bounds.size.w / 2 - seals_w / 2 + gap * i, y);
    if (i <= data->stage) {
      graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(SQUIRE_COLOR_GOLD, GColorWhite));
      graphics_fill_circle(ctx, c, r);
    } else {
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_draw_circle(ctx, c, r);
    }
  }
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, data->title_text, fonts_get_system_font(SQUIRE_FONT_TITLE),
                     GRect(TEXT_PADDING, HEADER_TOP + 14, bounds.size.w - TEXT_PADDING * 2, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void prv_footer_update(Layer *layer, GContext *ctx) {
  Window *window = *(Window **)layer_get_data(layer);
  ConsentWindowData *data = window_get_user_data(window);
  GRect bounds = layer_get_bounds(layer);
  const char *label = data->stage == STAGE_LOCATION_CONSENT ? "Choose" : "Continue";
  GFont font = fonts_get_system_font(SQUIRE_FONT_LABEL);
  GSize text = graphics_text_layout_get_content_size(label, font, bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  const int16_t glyph_w = 12 + SQUIRE_SPACE_S;
  const int16_t x = (bounds.size.w - glyph_w - text.w) / 2;
  graphics_context_set_stroke_color(ctx, SQUIRE_COLOR_MUTED);
  graphics_draw_line(ctx, GPoint(TEXT_PADDING, 0), GPoint(bounds.size.w - TEXT_PADDING - 1, 0));
  squire_draw_select_glyph(ctx, GPoint(x + 6, bounds.size.h / 2 + 1), SQUIRE_COLOR_AZURE_INK);
  graphics_context_set_text_color(ctx, SQUIRE_COLOR_AZURE_INK);
  graphics_draw_text(ctx, label, font, GRect(x + glyph_w, bounds.size.h / 2 - 11, text.w + 2, 24),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

void consent_window_push() {
  Window* window = bwindow_create();
  ConsentWindowData* data = bmalloc(sizeof(ConsentWindowData));
  memset(data, 0, sizeof(ConsentWindowData));
  window_set_user_data(window, data);
  window_set_window_handlers(window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
    .appear = prv_window_appear,
  });
  window_stack_push(window, true);
}

bool must_present_consent() {
  return persist_read_int(PERSIST_KEY_CONSENTS_COMPLETED) < 1;
}

void consent_migrate() {
  if (version_is_updated() && !version_is_first_launch()) {
    // If we're updating from version 1.1 or older, consent agreement was implied by LOCATION_ENABLED being set
    // (either true or false).
    if (version_info_compare(version_get_last_launch(), (VersionInfo) {1, 1}) <= 0) {
      SQUIRE_LOG(APP_LOG_LEVEL_INFO, "Performing consent migration from version 1.1.");
      // If the location enabled state is set, that's equivalent to consent agreement version 1.
      if (persist_exists(PERSIST_KEY_LOCATION_ENABLED)) {
        SQUIRE_LOG(APP_LOG_LEVEL_INFO, "Marking consent as 1.");;
        status_t status = persist_write_int(PERSIST_KEY_CONSENTS_COMPLETED, 1);
        if (status < 0) {
          SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Failed to mark consent as complete: %d", status);
        }
      } else {
        SQUIRE_LOG(APP_LOG_LEVEL_INFO, "Not marking consent.");;
      }
    }
  }
}

static void prv_window_load(Window *window) {
  ConsentWindowData *data = window_get_user_data(window);
  Layer *root_layer = window_get_root_layer(window);
  GRect window_bounds = layer_get_frame(root_layer);
  window_set_background_color(window, GColorWhite);
  data->scroll_layer = bscroll_layer_create(window_bounds);
  scroll_layer_set_click_config_onto_window(data->scroll_layer, window);
  // Decoration only: if memory is short the pages still work without them.
  data->header_layer = blayer_create_with_data(GRect(0, 0, window_bounds.size.w, HEADER_HEIGHT), sizeof(Window *));
  if (data->header_layer) {
    *(Window **)layer_get_data(data->header_layer) = window;
    layer_set_update_proc(data->header_layer, prv_header_update);
  }
  data->footer_layer = blayer_create_with_data(GRect(0, 0, window_bounds.size.w, FOOTER_HEIGHT), sizeof(Window *));
  if (data->footer_layer) {
    *(Window **)layer_get_data(data->footer_layer) = window;
    layer_set_update_proc(data->footer_layer, prv_footer_update);
  }
  data->text_layer = btext_layer_create(GRect(TEXT_PADDING, BODY_TOP, window_bounds.size.w - TEXT_PADDING * 2, window_bounds.size.h - BODY_TOP));
  text_layer_set_font(data->text_layer, fonts_get_system_font(BODY_FONT));
  text_layer_set_text_alignment(data->text_layer, TEXT_ALIGNMENT);
  data->select_indicator_bitmap = bgbitmap_create_with_resource(RESOURCE_ID_BUTTON_INDICATOR);
  data->select_indicator_layer = bbitmap_layer_create(
    GRect(window_bounds.size.w - 5, window_bounds.size.h / 2 - 10, 5, 20));
  bitmap_layer_set_bitmap(data->select_indicator_layer, data->select_indicator_bitmap);
  bitmap_layer_set_compositing_mode(data->select_indicator_layer, GCompOpSet);
  data->content_indicator_layer = blayer_create(
    GRect(0, window_bounds.size.h - STATUS_BAR_LAYER_HEIGHT, window_bounds.size.w, STATUS_BAR_LAYER_HEIGHT));
  scroll_layer_set_shadow_hidden(data->scroll_layer, true);
  ContentIndicator *indicator = scroll_layer_get_content_indicator(data->scroll_layer);
  const ContentIndicatorConfig content_indicator_config = (ContentIndicatorConfig) {
    .layer = data->content_indicator_layer,
    .alignment = GAlignCenter,
    .colors = {
      .background = GColorWhite,
      .foreground = GColorBlack,
    }
  };
  content_indicator_configure_direction(indicator, ContentIndicatorDirectionDown, &content_indicator_config);
  layer_add_child(root_layer, scroll_layer_get_layer(data->scroll_layer));
  layer_add_child(root_layer, (Layer *) data->select_indicator_layer);
  if (data->header_layer) {
    scroll_layer_add_child(data->scroll_layer, data->header_layer);
  }
  scroll_layer_add_child(data->scroll_layer, (Layer *) data->text_layer);
  if (data->footer_layer) {
    scroll_layer_add_child(data->scroll_layer, data->footer_layer);
  }
  layer_add_child(root_layer, data->content_indicator_layer);
  scroll_layer_set_callbacks(data->scroll_layer, (ScrollLayerCallbacks) {
    .click_config_provider = prv_click_config_provider,
  });
  scroll_layer_set_context(data->scroll_layer, window);
  data->app_message_handle = events_app_message_register_inbox_received(prv_app_message_handler, window);
}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click_handler);
}


static void prv_window_appear(Window *window) {
  prv_set_stage(window, STAGE_LLM_WARNING);
}

static void prv_window_unload(Window *window) {
  ConsentWindowData* data = window_get_user_data(window);
  if (data->response_timer) {
    app_timer_cancel(data->response_timer);
    data->response_timer = NULL;
  }
  if (data->app_message_handle) {
    events_app_message_unsubscribe(data->app_message_handle);
    data->app_message_handle = NULL;
  }
  scroll_layer_destroy(data->scroll_layer);
  if (data->header_layer) {
    layer_destroy(data->header_layer);
  }
  if (data->footer_layer) {
    layer_destroy(data->footer_layer);
  }
  text_layer_destroy(data->text_layer);
  gbitmap_destroy(data->select_indicator_bitmap);
  bitmap_layer_destroy(data->select_indicator_layer);
  layer_destroy(data->content_indicator_layer);
  free(data->current_text);
  free(data);
}

static void prv_set_stage(Window* window, int stage) {
  ConsentWindowData *data = window_get_user_data(window);
  if (data->current_text) {
    free(data->current_text);
    data->current_text = NULL;
  }
  ResHandle res_handle = NULL;
  switch (stage) {
  case STAGE_LLM_WARNING:
    res_handle = resource_get_handle(RESOURCE_ID_LLM_WARNING_TEXT);
    data->title_text = "Before you start";
    break;
  case STAGE_AGENT_CONSENT:
    res_handle = resource_get_handle(RESOURCE_ID_AGENT_CONSENT_TEXT);
    data->title_text = "Your privacy";
    break;
  case STAGE_LOCATION_CONSENT:
    res_handle = resource_get_handle(RESOURCE_ID_LOCATION_CONSENT_TEXT);
    data->title_text = "Location";
    break;
  default:
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Unknown consent stage: %d", stage);
    return;
  }
  if (res_handle != NULL) {
    size_t res_size = resource_size(res_handle);
    data->current_text = bmalloc(res_size + 1);
    resource_load(res_handle, (uint8_t*)data->current_text, res_size);
    data->current_text[res_size] = '\0';
  }
  data->stage = stage;
  if (data->header_layer) {
    layer_mark_dirty(data->header_layer);
  }
  text_layer_set_text(data->text_layer, data->current_text);
  GSize window_size = layer_get_frame(window_get_root_layer(window)).size;
  GSize text_size = graphics_text_layout_get_content_size(
    data->current_text,
    fonts_get_system_font(BODY_FONT),
    GRect(TEXT_PADDING, BODY_TOP, window_size.w - TEXT_PADDING * 2, 2000),
    GTextOverflowModeWordWrap,
    TEXT_ALIGNMENT);
  text_size.h += 5;
  text_size.w = window_size.w - TEXT_PADDING * 2;
  text_layer_set_size(data->text_layer, text_size);
  const int16_t footer_y = BODY_TOP + text_size.h + SQUIRE_SPACE_S;
  if (data->footer_layer) {
    layer_set_frame(data->footer_layer, GRect(0, footer_y, window_size.w, FOOTER_HEIGHT));
    layer_mark_dirty(data->footer_layer);
  }
  scroll_layer_set_content_size(data->scroll_layer, GSize(window_size.w, footer_y + FOOTER_HEIGHT + SCROLL_BOTTOM_PADDING));
  scroll_layer_set_content_offset(data->scroll_layer, GPoint(0, 0), false);
}

static bool prv_did_scroll_to_bottom(Window* window) {
  ConsentWindowData *data = window_get_user_data(window);
  ScrollLayer *scroll_layer = data->scroll_layer;
  int16_t offset = -scroll_layer_get_content_offset(data->scroll_layer).y;
  return (offset + layer_get_frame((Layer *)scroll_layer).size.h >= scroll_layer_get_content_size(scroll_layer).h - 10);
}

static void prv_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  Window* window = context;
  ConsentWindowData *data = window_get_user_data(window);
  if (!prv_did_scroll_to_bottom(window)) {
    // Not at the end yet: SELECT reads on by most of a page, rather than
    // silently doing nothing.
    GPoint offset = scroll_layer_get_content_offset(data->scroll_layer);
    int16_t page = layer_get_frame(scroll_layer_get_layer(data->scroll_layer)).size.h - 40;
    scroll_layer_set_content_offset(data->scroll_layer, GPoint(0, offset.y - page), true);
    return;
  }
  switch (data->stage) {
    case STAGE_LLM_WARNING:
      prv_set_stage(window, STAGE_AGENT_CONSENT);
      break;
    case STAGE_AGENT_CONSENT:
      prv_set_stage(window, STAGE_LOCATION_CONSENT);
      break;
    case STAGE_LOCATION_CONSENT:
      prv_present_consent_menu(window);
      break;
  }
}

static void prv_present_consent_menu(Window* window) {
  ConsentWindowData* data = window_get_user_data(window);
  ActionMenuLevel* root_level = baction_menu_level_create(2);
  action_menu_level_add_action(root_level, "Allow location", prv_consent_menu_select_callback, (void *)true);
  action_menu_level_add_action(root_level, "Don't allow", prv_consent_menu_select_callback, (void *)false);
  ActionMenuConfig config = (ActionMenuConfig) {
    .root_level = root_level,
    .align = ActionMenuAlignCenter,
    .context = window,
    .did_close = prv_action_menu_close,
  };
  squire_action_menu_colors(&config);
  data->action_menu = action_menu_open(&config);
}

// The phone never confirmed: unfreeze the user rather than leave the menu
// stuck. A reply that turns up later is ignored (expected_app_response is
// cleared) and the user simply chooses again.
static void prv_location_reply_timed_out(void *context) {
  Window *window = context;
  ConsentWindowData *data = window_get_user_data(window);
  data->response_timer = NULL;
  data->expected_app_response = 0;
  SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Phone didn't confirm the location choice.");
  if (data->action_menu) {
    // Frozen menus take no input, so this is still the open menu.
    action_menu_close(data->action_menu, true);
    data->action_menu = NULL;
  }
  result_window_push(ResultToneError, "No answer from phone",
      "Squire Plus couldn't confirm your location choice with the Pebble app on your phone. Check Bluetooth, then press SELECT to choose again.",
      RESULT_WINDOW_PERSISTENT);
}

static void prv_consent_menu_select_callback(ActionMenu *action_menu, const ActionMenuItem *action, void *context) {
  Window *window = context;
  ConsentWindowData *data = window_get_user_data(window);
  bool choice = (int)action_menu_item_get_action_data(action);
  // We need to inform the phone of the user's choice; onboarding finishes
  // when it confirms.
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK || !iter ||
      dict_write_int16(iter, MESSAGE_KEY_LOCATION_ENABLED, choice) != DICT_OK ||
      app_message_outbox_send() != APP_MSG_OK) {
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Couldn't send location choice to the phone.");
    // The menu closes; SELECT on the last page reopens it.
    result_window_push_phone_unreachable();
    return;
  }
  data->expected_app_response = STAGE_LOCATION_CONSENT;
  data->pending_choice = choice;
  if (data->response_timer) {
    app_timer_cancel(data->response_timer);
  }
  data->response_timer = app_timer_register(LOCATION_REPLY_TIMEOUT_MS, prv_location_reply_timed_out, window);
  action_menu_freeze(action_menu);
}

static void prv_app_message_handler(DictionaryIterator *iter, void *context) {
  Window* window = context;
  ConsentWindowData* data = window_get_user_data(window);
  if (data->expected_app_response != STAGE_LOCATION_CONSENT) {
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Ignoring unexpected location consent response.");
    return;
  }
  Tuple *tuple = dict_find(iter, MESSAGE_KEY_LOCATION_ENABLED);
  if (tuple == NULL) {
    return;
  }
  bool location_enabled = tuple_get_int32(tuple) != 0;
  if (location_enabled != data->pending_choice) {
    // A late confirmation of an earlier, timed-out choice.
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Ignoring a stale location consent response.");
    return;
  }
  data->expected_app_response = 0;
  if (data->response_timer) {
    app_timer_cancel(data->response_timer);
    data->response_timer = NULL;
  }
  SQUIRE_LOG(APP_LOG_LEVEL_INFO, "Got location enabled reply, dismissing dialog.");
  events_app_message_unsubscribe(data->app_message_handle);
  data->app_message_handle = NULL;
  status_t status = persist_write_bool(PERSIST_KEY_LOCATION_ENABLED, location_enabled);
  if (status < 0) {
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Failed to write location enabled: %d", status);
  }
  prv_mark_consents_complete();
  RootWindow *root_window = root_window_create();
  action_menu_set_result_window(data->action_menu, root_window_get_window(root_window));
  action_menu_close(data->action_menu, true);
  window_stack_remove(window, false);
}

static void prv_action_menu_close(ActionMenu* action_menu, const ActionMenuItem* item, void* context) {
  action_menu_hierarchy_destroy(action_menu_get_root_level(action_menu), NULL, NULL);
}

static void prv_mark_consents_complete() {
  status_t status = persist_write_int(PERSIST_KEY_CONSENTS_COMPLETED, 1);
  if (status < 0) {
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Failed to mark consents as complete: %d", status);
  }
}
