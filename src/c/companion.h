/*
 * Copyright 2026 Andreas Varotsis
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

#ifndef COMPANION_H
#define COMPANION_H

#include <pebble.h>

// The Squire Plus companion: a pebble-shaped head with a helm visor, two
// bright eyes, an open smile and a gold "+" where a helm's plume would be,
// above a pair of shoulders that run off the bottom of the screen.
//
// Everything is drawn from primitives scaled from one number, the head width,
// so it stays crisp at every size and takes its colours from style.h (with
// black-and-white fallbacks). See docs/IDENTITY-REFRESH.md; the preview and
// icon generator in tools/identity/ mirrors this geometry.

// Height of the "+" crest and its stem above the head's top edge.
int companion_crest_height(int head_w);
// Height of the head itself.
int companion_head_height(int head_w);
// Gap between the head and the shoulders.
int companion_neck_height(int head_w);

// Draws the companion with the head's top edge centred on `head_top`. Pass an
// odd `head_w` so the face is symmetric about that column. The
// shoulders run down past `bottom` (the bottom edge of the drawing area).
void companion_draw(GContext *ctx, GPoint head_top, int head_w, int bottom);

#endif // COMPANION_H
