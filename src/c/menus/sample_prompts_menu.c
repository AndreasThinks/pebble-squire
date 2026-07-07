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

#include "sample_prompts_menu.h"

#include "../converse/session_window.h"
#include "../converse/history.h"
#include "../util/style.h"
#include "../util/memory/malloc.h"
#include "../util/memory/sdk.h"
#include "../util/logging.h"
#include <pebble.h>

typedef struct {
  MenuLayer *menu_layer;
  StatusBarLayer *status_bar;
  char *buffer;
  char **prompts;
  int count;
  bool using_history;
  bool waiting_for_history;
} SamplePromptsMenuData;

// The window currently showing, so the history-done callback can refresh it.
static Window *s_window;

static void prv_window_load(Window* window);
static void prv_window_unload(Window* window);
static uint16_t prv_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *context);
static void prv_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *context);
static void prv_select_click(MenuLayer *menu_layer, MenuIndex *cell_index, void *context);

static int prv_load_prompts(char *buffer, size_t size, char ***prompts) {
  int count = 1;
  for (size_t i = 0; i < size; ++i) {
    if (buffer[i] == '\n') {
      ++count;
    }
  }
  *prompts = bmalloc(sizeof(char*) * count);
  char *p = buffer;
  for (int i = 0; i < count; ++i) {
    (*prompts)[i] = p;
    p = strchr(p, '\n');
    if (p) {
      *p = '\0';
      ++p;
    }
  }
  return count;
}

void sample_prompts_menu_push() {
  Window *window = bwindow_create();
  SamplePromptsMenuData *data = bmalloc(sizeof(SamplePromptsMenuData));
  memset(data, 0, sizeof(SamplePromptsMenuData));
  window_set_user_data(window, data);
  window_set_window_handlers(window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(window, true);
}

static void prv_free_prompts(SamplePromptsMenuData *data) {
  if (data->prompts) {
    free(data->prompts);
    data->prompts = NULL;
  }
  if (data->buffer) {
    free(data->buffer);
    data->buffer = NULL;
  }
  data->count = 0;
}

// Pick the prompt source. History wins when it's ready; while the phone is
// still fetching it we show a loading row (and refresh when it lands) rather
// than silently committing to the canned fallback.
static void prv_load_source(SamplePromptsMenuData *data) {
  prv_free_prompts(data);
  data->waiting_for_history = false;

  if (history_is_available()) {
    int hist_count = history_get_count();
    int prompt_count = 0;
    for (int i = 0; i < hist_count; i++) {
      const HistoryEntry *entry = history_get_entry(i);
      if (entry && entry->type == HistoryEntryTypePrompt) {
        prompt_count++;
      }
    }
    data->count = prompt_count;
    data->prompts = bmalloc(sizeof(char*) * (prompt_count > 0 ? prompt_count : 1));
    int idx = 0;
    for (int i = 0; i < hist_count && idx < prompt_count; i++) {
      const HistoryEntry *entry = history_get_entry(i);
      if (entry && entry->type == HistoryEntryTypePrompt) {
        data->prompts[idx] = (char*)entry->text;
        idx++;
      }
    }
    data->using_history = true;
  } else if (history_is_loading()) {
    data->using_history = true;
    data->waiting_for_history = true;
  } else {
    ResHandle handle = resource_get_handle(RESOURCE_ID_SAMPLE_PROMPTS);
    size_t size = resource_size(handle);
    // The resource is raw text with no terminator; add one so the last
    // prompt doesn't read past the end of the buffer.
    data->buffer = bmalloc(size + 1);
    resource_load(handle, (uint8_t*)data->buffer, size);
    data->buffer[size] = '\0';
    data->count = prv_load_prompts(data->buffer, size, &data->prompts);
    data->using_history = false;
  }
}

static void prv_history_done(void) {
  if (!s_window) {
    return;
  }
  SamplePromptsMenuData *data = window_get_user_data(s_window);
  prv_load_source(data);
  menu_layer_reload_data(data->menu_layer);
}

static void prv_window_load(Window* window) {
  SamplePromptsMenuData *data = window_get_user_data(window);
  Layer *root_layer = window_get_root_layer(window);
  GRect window_bounds = layer_get_frame(root_layer);

  s_window = window;
  prv_load_source(data);
  if (data->waiting_for_history) {
    history_set_done_callback(prv_history_done);
  }

  window_set_background_color(window, GColorWhite);

  data->status_bar = bstatus_bar_layer_create();
  squire_status_bar_config(data->status_bar);
  layer_add_child(root_layer, status_bar_layer_get_layer(data->status_bar));

  GRect menu_frame = GRect(0, STATUS_BAR_LAYER_HEIGHT, window_bounds.size.w, window_bounds.size.h - STATUS_BAR_LAYER_HEIGHT);
  data->menu_layer = bmenu_layer_create(menu_frame);
  menu_layer_set_callbacks(data->menu_layer, data, (MenuLayerCallbacks) {
    .get_num_rows = prv_get_num_rows,
    .draw_row = prv_draw_row,
    .select_click = prv_select_click,
  });
  menu_layer_set_highlight_colors(data->menu_layer, SELECTION_HIGHLIGHT_COLOUR, gcolor_legible_over(SELECTION_HIGHLIGHT_COLOUR));
#ifdef PBL_ROUND
  menu_layer_set_center_focused(data->menu_layer, true);
#endif
  layer_add_child(root_layer, menu_layer_get_layer(data->menu_layer));
  menu_layer_set_click_config_onto_window(data->menu_layer, window);
}

static void prv_window_unload(Window* window) {
  SamplePromptsMenuData *data = window_get_user_data(window);
  if (data->waiting_for_history) {
    history_set_done_callback(NULL);
  }
  s_window = NULL;
  menu_layer_destroy(data->menu_layer);
  status_bar_layer_destroy(data->status_bar);
  prv_free_prompts(data);
  free(data);
  window_destroy(window);
}

static uint16_t prv_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *context) {
  SamplePromptsMenuData *data = context;
  if (data->count == 0) return 1;
  return data->count;
}

static void prv_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *context) {
  SamplePromptsMenuData *data = context;

  if (data->count == 0) {
    const char *empty = data->waiting_for_history ? "Loading history..." : "No history yet";
#ifdef PBL_ROUND
    GRect bounds = layer_get_bounds(cell_layer);
    graphics_draw_text(ctx, empty,
      fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
      GRect(10, 5, bounds.size.w - 20, bounds.size.h - 10),
      GTextOverflowModeWordWrap,
      GTextAlignmentCenter,
      NULL);
#else
    menu_cell_title_draw(ctx, cell_layer, empty);
#endif
    return;
  }

  const char *title = data->prompts[cell_index->row];
#ifdef PBL_ROUND
  GRect bounds = layer_get_bounds(cell_layer);
  graphics_draw_text(ctx, title,
    fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
    GRect(10, 5, bounds.size.w - 20, bounds.size.h - 10),
    GTextOverflowModeWordWrap,
    GTextAlignmentCenter,
    NULL);
#else
  menu_cell_title_draw(ctx, cell_layer, title);
#endif
}

static void prv_select_click(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  SamplePromptsMenuData *data = context;
  if (data->count == 0) return;

  const char *prompt = data->prompts[cell_index->row];

  if (history_is_available() && history_get_thread_id() && history_get_thread_id()[0]) {
    // Resume existing conversation thread with history context
    session_window_push_with_history(0, (char*)prompt, history_get_thread_id());
  } else {
    session_window_push(0, (char*)prompt);
  }
}
