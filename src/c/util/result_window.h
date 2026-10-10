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

#ifndef RESULT_WINDOW_H
#define RESULT_WINDOW_H

#include <pebble.h>

// Status cards: a full-screen message with a tone-coloured header and glyph,
// a title and scrollable body. SELECT or BACK dismisses; UP/DOWN scroll.
typedef enum {
  ResultToneInfo,
  ResultToneSuccess,
  ResultToneWarning,
  ResultToneError,
  // Shows stepping dots instead of a glyph, for "working on it" states. Only
  // one progress card exists at a time: pushing another replaces it, and
  // result_window_dismiss_progress() removes it when the work finishes.
  ResultToneProgress,
} ResultTone;

// Pass as timeout_ms to keep the card until the user dismisses it.
#define RESULT_WINDOW_PERSISTENT 0
#define RESULT_WINDOW_SHORT_MS 4000

typedef void (*ResultWindowClosedHandler)(Window *window);

// Returns the card's window, or NULL if there wasn't memory to show it.
Window* result_window_push(ResultTone tone, const char* title, const char* text, uint32_t timeout_ms);
// Called as the card unloads, however it was closed. Lets owners that keep the
// window pointer (to remove it later) forget it in time.
void result_window_set_closed_handler(Window *window, ResultWindowClosedHandler handler);
// Removes the current progress card, if any. Safe to call when there is none.
void result_window_dismiss_progress(void);
// The shared "couldn't send to the phone" error card.
void result_window_push_phone_unreachable(void);

#endif //RESULT_WINDOW_H
