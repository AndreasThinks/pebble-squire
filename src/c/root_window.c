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

#include <pebble.h>
#include <pebble-events/pebble-events.h>

#include "root_window.h"
#include "talking_squire_layer.h"
#include "converse/session_window.h"
#include "converse/history.h"
#include "menus/about_window.h"
#include "menus/sample_prompts_menu.h"
#include "util/logging.h"
#include "util/style.h"
#include "util/thinking_layer.h"
#include "util/memory/malloc.h"
#include "util/memory/sdk.h"
#include "util/result_window.h"
#include "util/action_menu_crimes.h"
#include "settings/settings.h"
#include "vibes/haptic_feedback.h"

static const char* const PRV_GREETINGS[] = {
  "Hey!",
  "Yo!",
  "Sup?",
  "Hiya!",
  "Hello!",
  "Hej!",
  "Hola!",
  "Heyo!",
  "Howdy!",
  "Oi!",
};
#define PRV_GREETINGS_COUNT 10

struct RootWindow {
  Window* window;
  ActionBarLayer* action_bar;
  SessionWindow* session_window;
  GBitmap* question_icon;
  GBitmap* dictation_icon;
  GBitmap* more_icon;
  TalkingSquireLayer* talking_squire_layer;
  EventHandle app_message_handle;
  bool talking_squire_overridden;
  ActionMenu* disconnect_confirm_menu;
  bool disconnect_confirmed;
};

static void prv_window_load(Window* window);
static void prv_window_appear(Window* window);
static void prv_window_disappear(Window* window);
static void prv_click_config_provider(void *context);
static void prv_prompt_clicked(ClickRecognizerRef recognizer, void *context);
static void prv_select_long_pressed(ClickRecognizerRef recognizer, void *context);
static void prv_more_clicked(ClickRecognizerRef recognizer, void* context);
static void prv_up_clicked(ClickRecognizerRef recognizer, void *context);
static void prv_action_menu_closed(ActionMenu *action_menu, const ActionMenuItem *performed_action, void *context);
static void prv_menu_about(ActionMenu *action_menu, const ActionMenuItem *action, void *context);
static void prv_menu_disconnect(ActionMenu *action_menu, const ActionMenuItem *action, void *context);
static void prv_disconnect_confirm_selected(ActionMenu *action_menu, const ActionMenuItem *action, void *context);
static void prv_disconnect_confirm_menu_closed(ActionMenu *action_menu, const ActionMenuItem *performed_action, void *context);
static void prv_app_message_handler(DictionaryIterator *iter, void *context);

RootWindow* root_window_create() {
  RootWindow* rw = bmalloc(sizeof(RootWindow));
  memset(rw, 0, sizeof(RootWindow));
  rw->window = bwindow_create();
  window_set_window_handlers(rw->window, (WindowHandlers) {
    .load = prv_window_load,
    .appear = prv_window_appear,
    .disappear = prv_window_disappear,
  });
  window_set_user_data(rw->window, rw);
  return rw;
}

void root_window_push(RootWindow* window) {
  window_stack_push(window->window, true);
}

void root_window_destroy(RootWindow* window) {
  window_destroy(window->window);
  free(window);
}

Window* root_window_get_window(RootWindow* window) {
  return window->window;
}

static void prv_window_load(Window *window) {
  // RootWindow* root_window = (RootWindow*)window_get_user_data(window);
}

static void prv_window_appear(Window* window) {
  size_t heap_size = heap_bytes_free();
  RootWindow* rw = window_get_user_data(window);
  GRect bounds = layer_get_bounds(window_get_root_layer(rw->window));
  window_set_background_color(rw->window, COLOR_FALLBACK(ACCENT_COLOUR, GColorWhite));
  rw->question_icon = bgbitmap_create_with_resource(RESOURCE_ID_QUESTION_ICON);
  rw->dictation_icon = bgbitmap_create_with_resource(RESOURCE_ID_DICTATION_ICON);
  rw->more_icon = bgbitmap_create_with_resource(RESOURCE_ID_MORE_ICON);
  rw->action_bar = baction_bar_layer_create();
  action_bar_layer_set_context(rw->action_bar, rw);
  action_bar_layer_set_icon(rw->action_bar, BUTTON_ID_UP, rw->question_icon);
  action_bar_layer_set_icon(rw->action_bar, BUTTON_ID_SELECT, rw->dictation_icon);
  action_bar_layer_set_icon(rw->action_bar, BUTTON_ID_DOWN, rw->more_icon);
  action_bar_layer_add_to_window(rw->action_bar, window);
  action_bar_layer_set_click_config_provider(rw->action_bar, prv_click_config_provider);
#ifdef PBL_ROUND
  rw->talking_squire_layer = talking_squire_layer_create(GRect(0, 0, bounds.size.w, bounds.size.h));
#else
  rw->talking_squire_layer = talking_squire_layer_create(GRect(0, 0, bounds.size.w - ACTION_BAR_WIDTH, bounds.size.h));
#endif
  layer_add_child(window_get_root_layer(rw->window), (Layer *)rw->talking_squire_layer);
  rw->talking_squire_overridden = false;
  const char* greeting = PRV_GREETINGS[rand() % PRV_GREETINGS_COUNT];
  talking_squire_layer_set_text(rw->talking_squire_layer, greeting);

  if (!rw->app_message_handle) {
    rw->app_message_handle = events_app_message_register_inbox_received(prv_app_message_handler, rw);
  }
  SQUIRE_LOG(APP_LOG_LEVEL_DEBUG, "Window appeared. Heap usage increased %d bytes", heap_size - heap_bytes_free());
}

static void prv_window_disappear(Window* window) {
  size_t heap_size = heap_bytes_free();
  RootWindow* rw = window_get_user_data(window);
  if (rw->app_message_handle) {
    events_app_message_unsubscribe(rw->app_message_handle);
    rw->app_message_handle = NULL;
  }
  action_bar_layer_destroy(rw->action_bar);
  gbitmap_destroy(rw->question_icon);
  gbitmap_destroy(rw->dictation_icon);
  gbitmap_destroy(rw->more_icon);
  talking_squire_layer_destroy(rw->talking_squire_layer);
  SQUIRE_LOG(APP_LOG_LEVEL_DEBUG, "Window disappeared. Heap usage decreased %d bytes", heap_bytes_free() - heap_size);
}

static void prv_app_message_handler(DictionaryIterator *iter, void *context) {
  RootWindow* rw = context;
  Tuple *tuple = dict_find(iter, MESSAGE_KEY_COBBLE_WARNING);
  if (!tuple) {
    return;
  }
  if (tuple->value->int32 == 1) {
    rw->talking_squire_overridden = true;
    talking_squire_layer_set_text(rw->talking_squire_layer, "Uh oh!");
    window_set_background_color(rw->window, COLOR_FALLBACK(GColorVeryLightBlue, GColorDarkGray));
    vibe_haptic_feedback();
  }
}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, prv_up_clicked);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_prompt_clicked);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, prv_select_long_pressed, NULL);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_more_clicked);
}

static Window *s_loading_window = NULL;
static ThinkingLayer *s_loading_thinking_layer = NULL;

static void prv_loading_window_unload(Window *window) {
  if (s_loading_thinking_layer) {
    thinking_layer_destroy(s_loading_thinking_layer);
    s_loading_thinking_layer = NULL;
  }
  window_destroy(s_loading_window);
  s_loading_window = NULL;
}

static void prv_history_loaded_callback(void) {
  if (s_loading_window) {
    window_stack_remove(s_loading_window, true);
    // Unload handler will destroy resources
  }
  session_window_push_with_history(0, NULL, history_get_thread_id());
}

static void prv_push_loading_window(void) {
  if (s_loading_window) return;
  Window *window = bwindow_create();
  s_loading_window = window;
  window_set_window_handlers(window, (WindowHandlers) {
    .unload = prv_loading_window_unload,
  });
  window_set_background_color(window, BRANDED_BACKGROUND_COLOUR);
  Layer *root_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root_layer);
  s_loading_thinking_layer = thinking_layer_create(GRect(
    (bounds.size.w - THINKING_LAYER_WIDTH) / 2,
    (bounds.size.h - THINKING_LAYER_HEIGHT) / 2,
    THINKING_LAYER_WIDTH,
    THINKING_LAYER_HEIGHT));
  layer_add_child(root_layer, s_loading_thinking_layer);
  window_stack_push(window, true);
}

static void prv_up_clicked(ClickRecognizerRef recognizer, void *context) {
  sample_prompts_menu_push();
}

static void prv_select_long_pressed(ClickRecognizerRef recognizer, void *context) {
  if (history_is_available()) {
    session_window_push_with_history(0, NULL, history_get_thread_id());
  } else if (history_is_loading()) {
    // Wait for history to load before deciding whether to show history or start fresh
    history_set_done_callback(prv_history_loaded_callback);
    prv_push_loading_window();
  } else {
    session_window_push(0, NULL);
  }
}

static void prv_action_menu_closed(ActionMenu *action_menu, const ActionMenuItem *performed_action, void *context) {
  action_menu_hierarchy_destroy(action_menu_get_root_level(action_menu), NULL, NULL);
}

static void prv_prompt_clicked(ClickRecognizerRef recognizer, void *context) {
  // SELECT always starts a fresh voice conversation, regardless of history state.
  session_window_push(0, NULL);
}

static void prv_more_clicked(ClickRecognizerRef recognizer, void* context) {
  RootWindow* rw = context;
  // About + Disconnect (with separator before Disconnect)
  ActionMenuLevel *level = baction_menu_level_create(2);
  action_menu_level_add_action(level, "About", prv_menu_about, NULL);
  action_menu_level_set_separator_index(level, 1);
  action_menu_level_add_action(level, "Disconnect", prv_menu_disconnect, rw);
  ActionMenuConfig config = (ActionMenuConfig) {
    .root_level = level,
    .colors = {
      .background = BRANDED_BACKGROUND_COLOUR,
      .foreground = gcolor_legible_over(BRANDED_BACKGROUND_COLOUR),
    },
    .align = ActionMenuAlignTop,
    .context = rw,
    .did_close = prv_action_menu_closed,
  };
  action_menu_open(&config);
}

static void prv_menu_about(ActionMenu *action_menu, const ActionMenuItem *action, void *context) {
  about_window_push();
}

static void prv_menu_disconnect(ActionMenu *action_menu, const ActionMenuItem *action, void *context) {
  RootWindow* rw = context;
  if (!settings_is_telegram_connected()) {
    result_window_push("Not Connected", "Telegram is not currently connected.", NULL, GColorWhite);
    return;
  }
  ActionMenuLevel *confirm_level = baction_menu_level_create(2);
  action_menu_level_add_action(confirm_level, "Disconnect", prv_disconnect_confirm_selected, (void*)true);
  action_menu_level_add_action(confirm_level, "Cancel", prv_disconnect_confirm_selected, (void*)false);
  ActionMenuConfig config = (ActionMenuConfig) {
    .root_level = confirm_level,
    .colors = {
      .background = BRANDED_BACKGROUND_COLOUR,
      .foreground = gcolor_legible_over(BRANDED_BACKGROUND_COLOUR),
    },
    .align = ActionMenuAlignCenter,
    .context = rw,
    .did_close = prv_disconnect_confirm_menu_closed,
  };
  rw->disconnect_confirmed = false;
  rw->disconnect_confirm_menu = action_menu_open(&config);
}

static void prv_disconnect_confirm_selected(ActionMenu *action_menu, const ActionMenuItem *action, void *context) {
  RootWindow* rw = context;
  rw->disconnect_confirmed = (bool)action_menu_item_get_action_data(action);
}

static void prv_disconnect_confirm_menu_closed(ActionMenu *action_menu, const ActionMenuItem *performed_action, void *context) {
  RootWindow* rw = context;
  action_menu_hierarchy_destroy(action_menu_get_root_level(action_menu), NULL, NULL);
  rw->disconnect_confirm_menu = NULL;
  if (rw->disconnect_confirmed) {
    rw->disconnect_confirmed = false;
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
      dict_write_int8(iter, MESSAGE_KEY_TELEGRAM_DISCONNECT, 1);
      app_message_outbox_send();
      result_window_push("Disconnecting", "Signing out of Telegram...", NULL, GColorWhite);
    } else {
      result_window_push("Error", "Could not reach phone. Try again.", NULL, GColorWhite);
    }
  }
}
