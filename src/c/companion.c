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

#include "companion.h"
#include "util/style.h"

// All proportions are fractions of the head width `w`, in integer maths so
// tools/identity/squire_plus_identity.py can reproduce them exactly. Widths
// are odd so every part centres on the same pixel column as the head.

static int prv_max(int a, int b) {
  return a > b ? a : b;
}

static int prv_min(int a, int b) {
  return a < b ? a : b;
}

static int prv_stroke(int w) {
  return prv_max(2, w / 16);
}

static int prv_crest_arm(int w) {
  // Odd, so the "+" has a true centre pixel.
  return (w * 5 / 16) | 1;
}

static int prv_crest_bar(int w) {
  return prv_max(3, (w / 10) | 1);
}

static int prv_crest_stem(int w) {
  return w / 10;
}

static int prv_crest_outline(int w) {
  return prv_max(2, prv_stroke(w) - 1);
}

int companion_crest_height(int head_w) {
  return prv_crest_stem(head_w) + prv_crest_arm(head_w) + prv_crest_outline(head_w);
}

int companion_head_height(int head_w) {
  return head_w * 7 / 8;
}

int companion_neck_height(int head_w) {
  return head_w / 16;
}

// graphics_fill_rect caps corner radii at 8 px; the head needs far rounder
// corners, so build them from a cross of rectangles and four discs.
static void prv_fill_round_rect(GContext *ctx, GRect r, int radius, GColor color) {
  // A disc of radius r is 2r + 1 px wide, so this keeps the corners inside.
  radius = prv_min(radius, (prv_min(r.size.w, r.size.h) - 1) / 2);
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, GRect(r.origin.x + radius, r.origin.y, r.size.w - 2 * radius, r.size.h), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(r.origin.x, r.origin.y + radius, r.size.w, r.size.h - 2 * radius), 0, GCornerNone);
  if (radius <= 0) {
    return;
  }
  const int left = r.origin.x + radius;
  const int right = r.origin.x + r.size.w - 1 - radius;
  const int top = r.origin.y + radius;
  const int bottom = r.origin.y + r.size.h - 1 - radius;
  graphics_fill_circle(ctx, GPoint(left, top), radius);
  graphics_fill_circle(ctx, GPoint(right, top), radius);
  graphics_fill_circle(ctx, GPoint(left, bottom), radius);
  graphics_fill_circle(ctx, GPoint(right, bottom), radius);
}

// A filled shape with an ink outline: the outer shape in ink, then the inner
// one inset by the stroke width. Crisper than stroking at these sizes.
static void prv_outlined_round_rect(GContext *ctx, GRect r, int radius, int stroke, GColor fill) {
  prv_fill_round_rect(ctx, r, radius, SQUIRE_COLOR_COMPANION_INK);
  prv_fill_round_rect(ctx, grect_inset(r, GEdgeInsets(stroke)), radius - stroke, fill);
}

static void prv_draw_plus(GContext *ctx, GPoint c, int arm, int bar, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, GRect(c.x - arm / 2, c.y - bar / 2, arm, bar), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(c.x - bar / 2, c.y - arm / 2, bar, arm), 0, GCornerNone);
}

void companion_draw(GContext *ctx, GPoint head_top, int w, int bottom) {
  const int s = prv_stroke(w);
  const int h = companion_head_height(w);
  const int cx = head_top.x;
  const int top = head_top.y;
  const int left = cx - w / 2;

  // Hard pixel edges throughout: the mark is drawn as pixel art.
  graphics_context_set_antialiased(ctx, false);

  // Crest: a gold "+" on a short stem, the plume of the old squire's helm.
  const int arm = prv_crest_arm(w);
  const int bar = prv_crest_bar(w);
  const int stem = prv_crest_stem(w);
  const int outline = prv_crest_outline(w);
  const GPoint plus = GPoint(cx, top - stem - arm / 2);
  graphics_context_set_fill_color(ctx, SQUIRE_COLOR_COMPANION_INK);
  graphics_fill_rect(ctx, GRect(cx - s / 2, top - stem - 1, s | 1, stem + 2), 0, GCornerNone);
  prv_draw_plus(ctx, plus, arm + 2 * outline, bar + 2 * outline, SQUIRE_COLOR_COMPANION_INK);
  prv_draw_plus(ctx, plus, arm, bar, SQUIRE_COLOR_COMPANION_CREST);

  // Shoulders, running off the bottom edge.
  const int shoulders_w = (w * 9 / 8) | 1;
  const int shoulders_top = top + h + companion_neck_height(w);
  const int shoulders_h = prv_max(bottom - shoulders_top, 0) + w;
  prv_outlined_round_rect(ctx, GRect(cx - shoulders_w / 2, shoulders_top, shoulders_w, shoulders_h),
                          w * 3 / 8, s, SQUIRE_COLOR_COMPANION_BODY);

  // Helm hinges at the temples, half hidden behind the head.
  const int stud_r = prv_max(3, w * 5 / 32);
  const int stud_y = top + h / 2;
  for (int i = 0; i < 2; i++) {
    const GPoint stud = GPoint(i ? left + w - 1 - s / 2 : left + s / 2, stud_y);
    graphics_context_set_fill_color(ctx, SQUIRE_COLOR_COMPANION_INK);
    graphics_fill_circle(ctx, stud, stud_r);
    graphics_context_set_fill_color(ctx, SQUIRE_COLOR_COMPANION_VISOR);
    graphics_fill_circle(ctx, stud, stud_r - s);
  }

  // The head: a soft pebble.
  prv_outlined_round_rect(ctx, GRect(left, top, w, h), w * 3 / 8, s, SQUIRE_COLOR_COMPANION_FACE);

  const int visor_w = (w * 13 / 16) | 1;
  const int visor_h = w * 11 / 32;
  const int visor_y = top + h * 5 / 16;

  // An open smile: the lower half of a disc, mid-greeting.
  const int mouth_r = w / 10;
  const int mouth_y = visor_y + visor_h + prv_max(2, w / 24);
  graphics_context_set_fill_color(ctx, SQUIRE_COLOR_COMPANION_INK);
  graphics_fill_circle(ctx, GPoint(cx, mouth_y), mouth_r);
  // Hide the disc's top half. The visor, drawn next, covers any overlap.
  graphics_context_set_fill_color(ctx, SQUIRE_COLOR_COMPANION_FACE);
  graphics_fill_rect(ctx, GRect(cx - mouth_r, mouth_y - mouth_r, 2 * mouth_r + 1, mouth_r), 0, GCornerNone);

  // Visor, with two tall rounded eyes.
  prv_fill_round_rect(ctx, GRect(cx - visor_w / 2, visor_y, visor_w, visor_h), visor_h / 2,
                      SQUIRE_COLOR_COMPANION_VISOR);
  const int eye_w = prv_max(5, (w / 8) | 1);
  const int eye_h = visor_h * 3 / 4;
  const int eye_dx = w * 3 / 16;
  for (int i = 0; i < 2; i++) {
    const int eye_cx = i ? cx + eye_dx : cx - eye_dx;
    prv_fill_round_rect(ctx, GRect(eye_cx - eye_w / 2, visor_y + (visor_h - eye_h) / 2, eye_w, eye_h),
                        eye_w / 2, SQUIRE_COLOR_COMPANION_EYE);
  }

  graphics_context_set_antialiased(ctx, true);
}
