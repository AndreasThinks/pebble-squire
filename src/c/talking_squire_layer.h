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

#ifndef TALKING_SQUIRE_LAYER_H
#define TALKING_SQUIRE_LAYER_H

#include <pebble.h>

typedef Layer TalkingSquireLayer;

TalkingSquireLayer *talking_squire_layer_create(GRect frame);
void talking_squire_layer_destroy(TalkingSquireLayer *layer);
void talking_squire_layer_set_text(TalkingSquireLayer *layer, const char *text);
// A smaller second line in the bubble telling the user what SELECT does now.
// The string must outlive the layer (pass a literal). NULL hides it.
void talking_squire_layer_set_caption(TalkingSquireLayer *layer, const char *caption);

#endif //TALKING_SQUIRE_LAYER_H
