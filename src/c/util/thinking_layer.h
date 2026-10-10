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

#ifndef THINKING_LAYER_H
#define THINKING_LAYER_H

#include <pebble.h>

// The dots' size. A larger layer centres them.
#define THINKING_LAYER_WIDTH 48
#define THINKING_LAYER_HEIGHT 15

typedef Layer ThinkingLayer;

// Three dots; one at a time steps forward a few times a second. A stepped
// timer rather than a 30fps animation keeps long agent waits cheap on battery.
ThinkingLayer* thinking_layer_create(GRect rect);
void thinking_layer_destroy(ThinkingLayer *layer);
void thinking_layer_set_color(ThinkingLayer *layer, GColor color);
// Swaps the dots for a still "press SELECT" glyph and hint text (a string
// literal or otherwise long-lived), stopping the timer. NULL brings the dots
// back. Lets one layer mark the end of a conversation in both states.
void thinking_layer_set_hint(ThinkingLayer *layer, const char *hint);

#endif
