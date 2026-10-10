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

// Layout: an azure band with the step and title; the entered value (slots for
// a login code, the tail of the number for a phone number); a spinner-style
// digit picker with chevrons for UP/DOWN; and a footer naming the hold
// gesture, which is otherwise undiscoverable.
#ifdef PBL_ROUND
#define BAND_TOP SQUIRE_SPACE_L
#else
#define BAND_TOP 0
#endif
#if PBL_DISPLAY_WIDTH >= 200
#define BAND_HEIGHT (BAND_TOP + 46)
#define TILE_W 48
#define TILE_H 50
#define GAP SQUIRE_SPACE_M
#else
#define BAND_HEIGHT (BAND_TOP + 40)
#define TILE_W 40
#define TILE_H 42
#define GAP SQUIRE_SPACE_S
#endif
#define VALUE_HEIGHT 30
#define CHEVRON 6
#define SIDE_INSET (SQUIRE_SPACE_M + SQUIRE_ROUND_INSET)

typedef struct {
  Window *window;
  Layer *canvas;
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
  if (!window || !data) {
    SQUIRE_LOG(APP_LOG_LEVEL_ERROR, "Not enough memory to show sign-in entry.");
    free(data);
    if (window) {
      window_destroy(window);
    }
    if (cancel_callback) {
      cancel_callback();
    }
    return;
  }
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

static void prv_draw_chevron(GContext *ctx, GPoint tip, bool up, GColor color) {
  GPathInfo info = {
    .num_points = 3,
    .points = (GPoint[]) {
      { tip.x, tip.y },
      { tip.x - CHEVRON, tip.y + (up ? CHEVRON : -CHEVRON) },
      { tip.x + CHEVRON, tip.y + (up ? CHEVRON : -CHEVRON) },
    },
  };
  GPath path = { .num_points = info.num_points, .points = info.points };
  graphics_context_set_fill_color(ctx, color);
  gpath_draw_filled(ctx, &path);
}

// One underlined slot per digit of the login code; the next empty slot is
// marked in gold so it's clear how many digits remain.
static void prv_draw_code_slots(GContext *ctx, AuthEntryWindowData *data, GRect box) {
  const int n = data->max_length;
  int slot_w = (box.size.w + SQUIRE_SPACE_S) / n - SQUIRE_SPACE_S;
  if (slot_w > 22) {
    slot_w = 22;
  }
  const int total = n * slot_w + (n - 1) * SQUIRE_SPACE_S;
  int x = box.origin.x + (box.size.w - total) / 2;
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  char digit[2] = {0, 0};
  for (int i = 0; i < n; ++i, x += slot_w + SQUIRE_SPACE_S) {
    const bool next = i == data->value_length;
    graphics_context_set_fill_color(ctx, next ? PBL_IF_COLOR_ELSE(SQUIRE_COLOR_GOLD, GColorBlack) : SQUIRE_COLOR_MUTED);
    graphics_fill_rect(ctx, GRect(x, box.origin.y + box.size.h - (next ? 4 : 2), slot_w, next ? 4 : 2), 0, GCornerNone);
    if (i < data->value_length) {
      digit[0] = data->value[i];
      graphics_context_set_text_color(ctx, GColorBlack);
      graphics_draw_text(ctx, digit, font, GRect(x - 2, box.origin.y - 2, slot_w + 4, 28),
                         GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    }
  }
}

// The phone number grows from the right; when it no longer fits, its start
// is elided so the digits just entered stay visible.
static void prv_draw_phone_value(GContext *ctx, AuthEntryWindowData *data, GRect box) {
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  if (data->value_length == 0) {
    graphics_context_set_text_color(ctx, SQUIRE_COLOR_MUTED);
    graphics_draw_text(ctx, "Country code first", fonts_get_system_font(SQUIRE_FONT_LABEL), GRect(box.origin.x, box.origin.y + 4, box.size.w, box.size.h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    return;
  }
  char shown[MAX_ENTRY_LENGTH + 8];
  const char *digits = data->value;
  for (;;) {
    snprintf(shown, sizeof(shown), "%s%s", digits == data->value ? (data->show_plus_prefix ? "+" : "") : "\xE2\x80\xA6", digits);
    GSize size = graphics_text_layout_get_content_size(shown, font, GRect(0, 0, 1000, 40), GTextOverflowModeFill, GTextAlignmentLeft);
    if (size.w <= box.size.w || digits[1] == '\0') {
      break;
    }
    digits++;
  }
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, shown, font, box, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void prv_canvas_update(Layer *layer, GContext *ctx) {
  AuthEntryWindowData *data = *(AuthEntryWindowData **)layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);
  const int16_t w = bounds.size.w;

  // Band: step and title.
  graphics_context_set_fill_color(ctx, SQUIRE_COLOR_AZURE);
  graphics_fill_rect(ctx, GRect(0, 0, w, BAND_HEIGHT), 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, data->show_plus_prefix ? "Sign in \xC2\xB7 1 of 2" : "Sign in \xC2\xB7 2 of 2",
                     fonts_get_system_font(SQUIRE_FONT_CAPTION), GRect(SIDE_INSET, BAND_TOP, w - SIDE_INSET * 2, 16),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, data->title_text, fonts_get_system_font(SQUIRE_FONT_TITLE),
                     GRect(SIDE_INSET, BAND_TOP + 10, w - SIDE_INSET * 2, 28),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // Value.
  GRect value_box = GRect(SIDE_INSET, BAND_HEIGHT + GAP - 2, w - SIDE_INSET * 2, VALUE_HEIGHT);
  if (data->show_plus_prefix) {
    prv_draw_phone_value(ctx, data, value_box);
  } else {
    prv_draw_code_slots(ctx, data, value_box);
  }

  // Picker: chevrons for UP and DOWN around the digit SELECT will add.
  const int16_t cx = w / 2;
  const int16_t up_tip = value_box.origin.y + VALUE_HEIGHT + GAP;
  const int16_t tile_y = up_tip + CHEVRON + SQUIRE_SPACE_XS;
  const bool full = data->value_length >= data->max_length;
  const GColor ink = full ? SQUIRE_COLOR_MUTED : SQUIRE_COLOR_AZURE;
  prv_draw_chevron(ctx, GPoint(cx, up_tip), true, ink);
  GRect tile = GRect(cx - TILE_W / 2, tile_y, TILE_W, TILE_H);
  graphics_context_set_fill_color(ctx, ink);
  graphics_fill_rect(ctx, tile, SQUIRE_CORNER, GCornersAll);
  char digit_text[2] = { PRV_CHARSET[data->selected_digit], '\0' };
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, digit_text, fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS),
                     GRect(tile.origin.x, tile.origin.y + (TILE_H - 32) / 2 - 2, TILE_W, 36),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  prv_draw_chevron(ctx, GPoint(cx, tile_y + TILE_H + SQUIRE_SPACE_XS + CHEVRON), false, ink);

  // Footer: the gestures that aren't obvious from the picker.
  const int16_t footer_y = tile_y + TILE_H + SQUIRE_SPACE_XS + CHEVRON + GAP - 2;
  const char *hint;
#if PBL_DISPLAY_WIDTH >= 200
  if (full) {
    hint = "SELECT to confirm";
  } else if (data->value_length > 0) {
    hint = "Hold SELECT when done";
  } else {
    hint = "SELECT adds the digit";
  }
#else
  if (full) {
    hint = "SELECT: confirm";
  } else if (data->value_length > 0) {
    hint = "Hold SELECT: done";
  } else {
    hint = "SELECT: add digit";
  }
#endif
  graphics_context_set_text_color(ctx, SQUIRE_COLOR_MUTED);
  graphics_draw_text(ctx, hint, fonts_get_system_font(SQUIRE_FONT_CAPTION),
                     GRect(SIDE_INSET, footer_y, w - SIDE_INSET * 2, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
#if PBL_DISPLAY_HEIGHT >= 220
  graphics_draw_text(ctx, data->value_length > 0 ? "BACK deletes a digit" : "BACK cancels",
                     fonts_get_system_font(SQUIRE_FONT_CAPTION),
                     GRect(SIDE_INSET, footer_y + 16, w - SIDE_INSET * 2, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
#endif
}

static void prv_window_load(Window* window) {
  AuthEntryWindowData *data = window_get_user_data(window);
  Layer *root_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root_layer);

  data->canvas = blayer_create_with_data(bounds, sizeof(AuthEntryWindowData *));
  if (data->canvas) {
    *(AuthEntryWindowData **)layer_get_data(data->canvas) = data;
    layer_set_update_proc(data->canvas, prv_canvas_update);
    layer_add_child(root_layer, data->canvas);
  }
  window_set_click_config_provider(window, prv_click_config_provider);
}

static void prv_window_unload(Window* window) {
  AuthEntryWindowData *data = window_get_user_data(window);
  if (data->action_menu) {
    action_menu_close(data->action_menu, false);
    data->action_menu = NULL;
  }
  if (data->canvas) {
    layer_destroy(data->canvas);
  }
  free(data);
  window_destroy(window);
}

static void prv_update_display(AuthEntryWindowData* data) {
  if (data->canvas) {
    layer_mark_dirty(data->canvas);
  }
}

static void prv_submit(AuthEntryWindowData *data) {
  if (data->value_length == 0) return;
  AuthEntryCallback cb = data->callback;
  char value[MAX_ENTRY_LENGTH + 1];
  memcpy(value, data->value, data->value_length + 1);
  window_stack_pop(true);
  if (cb) {
    cb(value);
  }
}

static void prv_present_confirm_menu(AuthEntryWindowData* data) {
  if (data->show_plus_prefix) {
    snprintf(data->confirm_label, sizeof(data->confirm_label), "Send +%s", data->value);
  } else {
    snprintf(data->confirm_label, sizeof(data->confirm_label), "Send code %s", data->value);
  }
  ActionMenuLevel* root_level = baction_menu_level_create(2);
  action_menu_level_add_action(root_level, data->confirm_label, prv_confirm_menu_selected, (void *)true);
  action_menu_level_add_action(root_level, "Keep editing", prv_confirm_menu_selected, (void *)false);
  ActionMenuConfig config = (ActionMenuConfig) {
    .root_level = root_level,
    .align = ActionMenuAlignCenter,
    .context = data,
    .did_close = prv_action_menu_closed,
  };
  squire_action_menu_colors(&config);
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
