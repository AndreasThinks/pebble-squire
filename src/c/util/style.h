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

#ifndef COLOURS_H
#define COLOURS_H

#include <pebble.h>

// Squire's design tokens. See docs/DESIGN.md for the rationale.
//
// The palette is heraldic: azure (brand surfaces, the user's own words) and
// gold (attention, selection), on white paper for reading. Every colour has a
// black-and-white fallback chosen for contrast rather than hue, so diorite
// keeps the same hierarchy.

// Brand surfaces: home field, headers, the user's prompt bubbles.
#define SQUIRE_COLOR_AZURE COLOR_FALLBACK(GColorCobaltBlue, GColorBlack)
// Darker azure for outlines and small text on white.
#define SQUIRE_COLOR_AZURE_INK COLOR_FALLBACK(GColorDukeBlue, GColorBlack)
// Gold: selection highlights and status chips. Always paired with black text.
#define SQUIRE_COLOR_GOLD COLOR_FALLBACK(GColorChromeYellow, GColorWhite)
// Secondary text (captions, hints).
#define SQUIRE_COLOR_MUTED COLOR_FALLBACK(GColorDarkGray, GColorBlack)
// Card tints for non-message conversation entries.
#define SQUIRE_COLOR_ERROR_TINT COLOR_FALLBACK(GColorMelon, GColorWhite)
#define SQUIRE_COLOR_THOUGHT_TINT COLOR_FALLBACK(GColorPastelYellow, GColorWhite)
#define SQUIRE_COLOR_ACTION_TINT COLOR_FALLBACK(GColorCeleste, GColorWhite)
// Status tones (status cards).
#define SQUIRE_COLOR_SUCCESS COLOR_FALLBACK(GColorIslamicGreen, GColorBlack)
#define SQUIRE_COLOR_ERROR COLOR_FALLBACK(GColorFolly, GColorBlack)
#define SQUIRE_COLOR_WARNING COLOR_FALLBACK(GColorChromeYellow, GColorBlack)

// Home screen field. On black-and-white the squire reads best on paper.
#define SQUIRE_COLOR_FIELD COLOR_FALLBACK(GColorCobaltBlue, GColorWhite)
#define SQUIRE_COLOR_FIELD_OFFLINE COLOR_FALLBACK(GColorDarkGray, GColorWhite)

// The Squire Plus companion (src/c/companion.c). Ink outlines and a dark visor
// keep the face legible on the azure field and on white paper alike.
#define SQUIRE_COLOR_COMPANION_INK GColorBlack
#define SQUIRE_COLOR_COMPANION_FACE GColorWhite
#define SQUIRE_COLOR_COMPANION_VISOR COLOR_FALLBACK(GColorOxfordBlue, GColorBlack)
#define SQUIRE_COLOR_COMPANION_EYE GColorWhite
#define SQUIRE_COLOR_COMPANION_CREST SQUIRE_COLOR_GOLD
#define SQUIRE_COLOR_COMPANION_BODY COLOR_FALLBACK(GColorPictonBlue, GColorWhite)

// Legacy names kept for code that predates the token set.
#define ACCENT_COLOUR SQUIRE_COLOR_AZURE
#define BRANDED_BACKGROUND_COLOUR SQUIRE_COLOR_AZURE
#define SELECTION_HIGHLIGHT_COLOUR COLOR_FALLBACK(GColorChromeYellow, GColorBlack)

// Type scale. Large screens (emery, gabbro) get one step up for body copy.
#define SQUIRE_FONT_DISPLAY FONT_KEY_GOTHIC_28_BOLD
#define SQUIRE_FONT_TITLE FONT_KEY_GOTHIC_24_BOLD
#define SQUIRE_FONT_MESSAGE FONT_KEY_GOTHIC_24_BOLD
#define SQUIRE_FONT_CAPTION FONT_KEY_GOTHIC_14_BOLD
#if PBL_DISPLAY_WIDTH >= 200
#define SQUIRE_FONT_BODY FONT_KEY_GOTHIC_24
#define SQUIRE_FONT_PROMPT FONT_KEY_GOTHIC_24_BOLD
#define SQUIRE_FONT_LABEL FONT_KEY_GOTHIC_18_BOLD
#else
#define SQUIRE_FONT_BODY FONT_KEY_GOTHIC_18
#define SQUIRE_FONT_PROMPT FONT_KEY_GOTHIC_18_BOLD
#define SQUIRE_FONT_LABEL FONT_KEY_GOTHIC_14_BOLD
#endif

// Spacing: a 4px grid. Round screens need extra horizontal inset because the
// corners of the content area are outside the display.
#define SQUIRE_SPACE_XS 2
#define SQUIRE_SPACE_S 4
#define SQUIRE_SPACE_M 8
#define SQUIRE_SPACE_L 12
#define SQUIRE_CORNER 6
#if defined(PBL_PLATFORM_GABBRO)
#define SQUIRE_ROUND_INSET 26
#elif defined(PBL_ROUND)
#define SQUIRE_ROUND_INSET 18
#else
#define SQUIRE_ROUND_INSET 0
#endif

void squire_status_bar_config(StatusBarLayer *status_bar);
void squire_status_bar_result_pane_config(StatusBarLayer *status_bar);
// Brand colours for every ActionMenu, so menus feel like one family.
void squire_action_menu_colors(ActionMenuConfig *config);
// Draws the "press SELECT" glyph used in on-screen hints: a filled dot inside
// a ring, centred on `center`.
void squire_draw_select_glyph(GContext *ctx, GPoint center, GColor color);

#endif //COLOURS_H
