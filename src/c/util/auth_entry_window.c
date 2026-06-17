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

#include "auth_entry_window.h"
#include "memory/malloc.h"
#include "memory/sdk.h"
#include "style.h"
#include "logging.h"

#define MAX_ENTRY_LENGTH 32

static const char* const PRV_CHARSET = "0123456789";
#define PRV_CHARSET_SIZE 10

typedef struct {
  Window *window;
  TextLayer *title_layer;
  TextLayer *value_layer;
  TextLayer *digit_layer;
  TextLayer *hint_layer;
  TextLayer *hint2_layer;
  char title_text[48];
  char value[MAX_ENTRY_LENGTH + 1];
  char confirm_label[MAX_ENTRY_LENGTH + 16];
  int value_length;
  int selected_digit;
  int max_length;
  AuthEntryCallback callback;
  AuthEntryCancelCallback cancel_callback;
  ActionMenu* action_menu;
  bool confirm_choice;
  bool select_long_pressed;
  bool show_plus_prefix;
} AuthEntryWindowData;

static void prv_window_load(Window* window);
static void prv_window_unload(Window* window);
static void prv_update_display(AuthEntryWindowData* data);
static void prv_select_clicked(ClickRecognizerRef recognizer, void* context);
static void prv_select_long_click_handler(ClickRecognizerRef recognizer, void* context);
static void prv_select_long_click_release_handler(ClickRecognizerRef recognizer, void* context);
static void prv_up_clicked(ClickRecognizerRef recognizer, void* context);
static void prv_down_clicked(ClickRecognizerRef recognizer, void* context);
static void prv_back_clicked(ClickRecognizerRef recognizer, void* context);
static void prv_click_config_provider(void* context);
static void prv_present_confirm_menu(AuthEntryWindowData* data);
static void prv_confirm_menu_selected(ActionMenu *action_menu, const ActionMenuItem *action, void *context);
static void prv_action_menu_closed(ActionMenu *action_menu, const ActionMenuItem *performed_action, void *context);

void auth_entry_window_push_with_prefix(const char* title, int max_length, bool show_plus_prefix, AuthEntryCallback callback, AuthEntryCancelCallback cancel_callback) {
  Window *window = bwindow_create();
  AuthEntryWindowData *data = bmalloc(sizeof(AuthEntryWindowData));
  memset(data, 0, sizeof(AuthEntryWindowData));
  data->window = window;
  data->max_length = max_length > 0 && max_length < MAX_ENTRY_LENGTH ? max_length : MAX_ENTRY_LENGTH;
  data->callback = callback;
  data->cancel_callback = cancel_callback;
  data->selected_digit = 1; // Default to '1' to avoid leading zero
  data->confirm_choice = false;
  data->show_plus_prefix = show_plus_prefix;
  snprintf(data->title_text, sizeof(data->title_text), "%s", title);
  window_set_background_color(window, GColorWhite);
  window_set_user_data(window, data);
  window_set_window_handlers(window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(window, true);
}

void auth_entry_window_push(const char* title, int max_length, AuthEntryCallback callback, AuthEntryCancelCallback cancel_callback) {
  auth_entry_window_push_with_prefix(title, max_length, true, callback, cancel_callback);
}

static void prv_window_load(Window* window) {
  AuthEntryWindowData *data = window_get_user_data(window);
  Layer *root_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root_layer);

  data->title_layer = btext_layer_create(GRect(5, 5, bounds.size.w - 10, 30));
  text_layer_set_background_color(data->title_layer, GColorClear);
  text_layer_set_text_color(data->title_layer, GColorBlack);
  text_layer_set_font(data->title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text(data->title_layer, data->title_text);
  text_layer_set_text_alignment(data->title_layer, GTextAlignmentCenter);
  layer_add_child(root_layer, text_layer_get_layer(data->title_layer));

  data->value_layer = btext_layer_create(GRect(10, 38, bounds.size.w - 20, 28));
  text_layer_set_background_color(data->value_layer, GColorClear);
  text_layer_set_text_color(data->value_layer, GColorBlack);
  text_layer_set_font(data->value_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(data->value_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(data->value_layer, GTextOverflowModeTrailingEllipsis);
  layer_add_child(root_layer, text_layer_get_layer(data->value_layer));

  data->digit_layer = btext_layer_create(GRect(bounds.size.w / 2 - 30, 70, 60, 60));
  text_layer_set_background_color(data->digit_layer, GColorBlack);
  text_layer_set_text_color(data->digit_layer, GColorWhite);
  text_layer_set_font(data->digit_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_alignment(data->digit_layer, GTextAlignmentCenter);
  layer_add_child(root_layer, text_layer_get_layer(data->digit_layer));

  data->hint_layer = btext_layer_create(GRect(5, bounds.size.h - 45, bounds.size.w - 10, 20));
  text_layer_set_background_color(data->hint_layer, GColorClear);
  text_layer_set_text_color(data->hint_layer, GColorBlack);
  text_layer_set_font(data->hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text(data->hint_layer, "UP/DN: digit | SEL: add");
  text_layer_set_text_alignment(data->hint_layer, GTextAlignmentCenter);
  layer_add_child(root_layer, text_layer_get_layer(data->hint_layer));

  data->hint2_layer = btext_layer_create(GRect(5, bounds.size.h - 25, bounds.size.w - 10, 20));
  text_layer_set_background_color(data->hint2_layer, GColorClear);
  text_layer_set_text_color(data->hint2_layer, GColorBlack);
  text_layer_set_font(data->hint2_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text(data->hint2_layer, "hold SEL: done");
  text_layer_set_text_alignment(data->hint2_layer, GTextAlignmentCenter);
  layer_add_child(root_layer, text_layer_get_layer(data->hint2_layer));

  prv_update_display(data);

  window_set_click_config_provider(window, prv_click_config_provider);
}

static void prv_window_unload(Window* window) {
  AuthEntryWindowData *data = window_get_user_data(window);
  if (data->action_menu) {
    action_menu_close(data->action_menu, false);
    data->action_menu = NULL;
  }
  text_layer_destroy(data->title_layer);
  text_layer_destroy(data->value_layer);
  text_layer_destroy(data->digit_layer);
  text_layer_destroy(data->hint_layer);
  text_layer_destroy(data->hint2_layer);
  free(data);
  window_destroy(window);
}

static void prv_update_display(AuthEntryWindowData* data) {
  static char digit_text[2];
  static char display_value[MAX_ENTRY_LENGTH + 4];
  digit_text[0] = PRV_CHARSET[data->selected_digit];
  digit_text[1] = '\0';
  text_layer_set_text(data->digit_layer, digit_text);
  text_layer_set_text(data->title_layer, data->title_text);

  if (data->show_plus_prefix) {
    snprintf(display_value, sizeof(display_value), "+%s", data->value);
  } else {
    snprintf(display_value, sizeof(display_value), "%s", data->value);
  }
  text_layer_set_text(data->value_layer, display_value);
}

static void prv_submit(AuthEntryWindowData *data) {
  if (data->value_length == 0) return;
  if (data->callback) {
    data->callback(data->value);
  }
  window_stack_pop(true);
}

static void prv_present_confirm_menu(AuthEntryWindowData* data) {
  if (data->show_plus_prefix) {
    snprintf(data->confirm_label, sizeof(data->confirm_label), "Confirm +%s", data->value);
  } else {
    snprintf(data->confirm_label, sizeof(data->confirm_label), "Confirm %s", data->value);
  }
  ActionMenuLevel* root_level = baction_menu_level_create(2);
  action_menu_level_add_action(root_level, data->confirm_label, prv_confirm_menu_selected, (void *)true);
  action_menu_level_add_action(root_level, "Edit", prv_confirm_menu_selected, (void *)false);
  ActionMenuConfig config = (ActionMenuConfig) {
    .root_level = root_level,
    .colors = {
      .background = BRANDED_BACKGROUND_COLOUR,
      .foreground = gcolor_legible_over(BRANDED_BACKGROUND_COLOUR),
    },
    .align = ActionMenuAlignCenter,
    .context = data,
    .did_close = prv_action_menu_closed,
  };
  data->action_menu = action_menu_open(&config);
}

static void prv_confirm_menu_selected(ActionMenu *action_menu, const ActionMenuItem *action, void *context) {
  AuthEntryWindowData *data = context;
  data->confirm_choice = (int)action_menu_item_get_action_data(action);
}

static void prv_action_menu_closed(ActionMenu *action_menu, const ActionMenuItem *performed_action, void *context) {
  AuthEntryWindowData *data = context;
  action_menu_hierarchy_destroy(action_menu_get_root_level(action_menu), NULL, NULL);
  data->action_menu = NULL;
  if (data->confirm_choice) {
    data->confirm_choice = false;
    prv_submit(data);
  }
}

static void prv_select_clicked(ClickRecognizerRef recognizer, void* context) {
  AuthEntryWindowData *data = window_get_user_data((Window*)context);
  if (data->select_long_pressed) {
    data->select_long_pressed = false;
    return;
  }
  if (data->value_length >= data->max_length) {
    prv_present_confirm_menu(data);
    return;
  }

  data->value[data->value_length] = PRV_CHARSET[data->selected_digit];
  data->value_length++;
  data->value[data->value_length] = '\0';

  if (data->value_length >= data->max_length) {
    prv_present_confirm_menu(data);
  }

  prv_update_display(data);
}

static void prv_select_long_click_handler(ClickRecognizerRef recognizer, void* context) {
  AuthEntryWindowData *data = window_get_user_data((Window*)context);
  data->select_long_pressed = true;
  if (data->value_length > 0) {
    prv_present_confirm_menu(data);
  }
}

static void prv_select_long_click_release_handler(ClickRecognizerRef recognizer, void* context) {
  AuthEntryWindowData *data = window_get_user_data((Window*)context);
  data->select_long_pressed = true;
}

static void prv_up_clicked(ClickRecognizerRef recognizer, void* context) {
  AuthEntryWindowData *data = window_get_user_data((Window*)context);
  data->selected_digit = (data->selected_digit + 1) % PRV_CHARSET_SIZE;
  prv_update_display(data);
}

static void prv_down_clicked(ClickRecognizerRef recognizer, void* context) {
  AuthEntryWindowData *data = window_get_user_data((Window*)context);
  data->selected_digit = (data->selected_digit + PRV_CHARSET_SIZE - 1) % PRV_CHARSET_SIZE;
  prv_update_display(data);
}

static void prv_back_clicked(ClickRecognizerRef recognizer, void* context) {
  AuthEntryWindowData *data = window_get_user_data((Window*)context);
  if (data->value_length > 0) {
    data->value_length--;
    data->value[data->value_length] = '\0';
    prv_update_display(data);
  } else {
    if (data->cancel_callback) {
      data->cancel_callback();
    }
    window_stack_pop(true);
  }
}

static void prv_click_config_provider(void* context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_clicked);
  window_long_click_subscribe(BUTTON_ID_SELECT, 700, prv_select_long_click_handler, prv_select_long_click_release_handler);
  window_single_click_subscribe(BUTTON_ID_UP, prv_up_clicked);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_down_clicked);
  window_single_click_subscribe(BUTTON_ID_BACK, prv_back_clicked);
}
