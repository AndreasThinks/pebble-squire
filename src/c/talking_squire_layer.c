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

#include "talking_squire_layer.h"
#include "companion.h"
#include "util/style.h"
#include <pebble.h>

#include "util/memory/sdk.h"

#define GREETING_FONT FONT_KEY_GOTHIC_28_BOLD
#define CAPTION_FONT FONT_KEY_GOTHIC_14_BOLD

// Speech bubble geometry. Gothic's line boxes carry empty space above the
// glyphs, so the text sits a little higher than the bubble's padding implies.
#define BUBBLE_STROKE 2
#define BUBBLE_RADIUS 6
#define BUBBLE_PAD_X 8
#define BUBBLE_TEXT_LIFT 1
#define BUBBLE_PAD_BOTTOM 8
// Clearance between the greeting's descenders and the caption.
#define CAPTION_GAP 1
#define TAIL_H 7
// Space between the tail and the companion's crest.
#define CREST_GAP 3

// The companion's largest head width per screen (odd, for symmetry); it
// shrinks in steps of 2 if a tall bubble needs the room.
#if defined(PBL_PLATFORM_GABBRO)
#define COMPANION_MAX_W 107
#elif defined(PBL_PLATFORM_EMERY)
#define COMPANION_MAX_W 95
#elif defined(PBL_ROUND)
#define COMPANION_MAX_W 67
#else
#define COMPANION_MAX_W 63
#endif
#define COMPANION_MIN_W 39

typedef struct {
  const char* text;
  const char* caption;
  GSize text_size;
  GSize caption_size;
} TalkingSquireLayerData;

typedef struct {
  GRect bubble;
  int tail_x;
  GPoint head_top;
  int head_w;
} Layout;

static void prv_update_layer(Layer *layer, GContext *ctx);

TalkingSquireLayer *talking_squire_layer_create(GRect frame) {
  Layer *layer = blayer_create_with_data(frame, sizeof(TalkingSquireLayerData));
  if (!layer) {
    return NULL;
  }
  TalkingSquireLayerData *data = layer_get_data(layer);
  data->text = NULL;
  data->caption = NULL;
  data->text_size = GSizeZero;
  data->caption_size = GSizeZero;
  layer_set_update_proc(layer, prv_update_layer);
  return layer;
}

void talking_squire_layer_destroy(TalkingSquireLayer *layer) {
  layer_destroy(layer);
}

// The widest the bubble's text may be: the content area left of the action
// bar (round screens draw under it), less the bubble's own padding.
static int prv_max_text_width(GSize size) {
#ifdef PBL_ROUND
  const int area = size.w - ACTION_BAR_WIDTH - 2 * SQUIRE_ROUND_INSET;
#else
  const int area = size.w - 2 * SQUIRE_SPACE_S;
#endif
  return area - 2 * (BUBBLE_PAD_X + BUBBLE_STROKE);
}

void talking_squire_layer_set_text(TalkingSquireLayer *layer, const char *text) {
  TalkingSquireLayerData *data = layer_get_data(layer);
  data->text = text;
  GRect bounds = layer_get_bounds(layer);
  data->text_size = graphics_text_layout_get_content_size(text, fonts_get_system_font(GREETING_FONT),
      GRect(0, 0, prv_max_text_width(bounds.size), bounds.size.h / 2), GTextOverflowModeWordWrap, GTextAlignmentLeft);
  layer_mark_dirty(layer);
}

void talking_squire_layer_set_caption(TalkingSquireLayer *layer, const char *caption) {
  TalkingSquireLayerData *data = layer_get_data(layer);
  data->caption = caption;
  if (caption) {
    GRect bounds = layer_get_bounds(layer);
    data->caption_size = graphics_text_layout_get_content_size(caption, fonts_get_system_font(CAPTION_FONT),
        GRect(0, 0, prv_max_text_width(bounds.size), 40), GTextOverflowModeWordWrap, GTextAlignmentLeft);
  } else {
    data->caption_size = GSizeZero;
  }
  layer_mark_dirty(layer);
}

static int prv_caption_y_offset(const TalkingSquireLayerData *data) {
  return data->text_size.h + CAPTION_GAP;
}

#ifdef PBL_ROUND
static bool prv_inside_display(GSize size, int x, int y) {
  const int r = size.w / 2 - SQUIRE_SPACE_XS;
  const int dx = x - size.w / 2;
  const int dy = y - size.h / 2;
  return dx * dx + dy * dy <= r * r;
}
#endif

static Layout prv_layout(const TalkingSquireLayerData *data, GSize size) {
  Layout l;
  // The companion stands in the middle of the content area. On round screens
  // that's nudged right of centre-of-the-free-area, toward the display's own
  // centre, because the bottom of the circle narrows.
#ifdef PBL_ROUND
  const int cx = (size.w - ACTION_BAR_WIDTH) / 2 + ACTION_BAR_WIDTH / 4;
#else
  const int cx = size.w / 2;
#endif

  const int content_w = data->text_size.w > data->caption_size.w ? data->text_size.w : data->caption_size.w;
  const int bubble_w = content_w + 2 * (BUBBLE_PAD_X + BUBBLE_STROKE);
  int bubble_h = data->text_size.h + BUBBLE_PAD_BOTTOM - BUBBLE_TEXT_LIFT;
  if (data->caption) {
    bubble_h += data->caption_size.h + CAPTION_GAP;
  }
  int bubble_x = cx - bubble_w / 2;
#ifdef PBL_ROUND
  const int bubble_max_right = size.w - ACTION_BAR_WIDTH - SQUIRE_SPACE_S;
#else
  const int bubble_max_right = size.w - SQUIRE_SPACE_S;
#endif
  if (bubble_x + bubble_w > bubble_max_right) {
    bubble_x = bubble_max_right - bubble_w;
  }
  if (bubble_x < SQUIRE_SPACE_S) {
    bubble_x = SQUIRE_SPACE_S;
  }
  int bubble_y = SQUIRE_SPACE_M;
#ifdef PBL_ROUND
  // Drop the bubble until its top corners clear the bezel.
  const int c = BUBBLE_RADIUS / 2;
  while (bubble_y < size.h / 3 &&
         !(prv_inside_display(size, bubble_x + c, bubble_y + c) &&
           prv_inside_display(size, bubble_x + bubble_w - 1 - c, bubble_y + c))) {
    bubble_y++;
  }
#endif
  l.bubble = GRect(bubble_x, bubble_y, bubble_w, bubble_h);

  // Shoulders show for a quarter of the head width (more on round screens,
  // where the bezel eats the bottom corners).
  const int free_top = bubble_y + bubble_h + TAIL_H + CREST_GAP;
  int w = COMPANION_MAX_W;
  int shoulders = 0;
  for (; w > COMPANION_MIN_W; w -= 2) {
    shoulders = PBL_IF_ROUND_ELSE(w / 2, w / 4);
    const int need = companion_crest_height(w) + companion_head_height(w) + companion_neck_height(w) + shoulders;
    if (free_top + need <= size.h) {
      break;
    }
  }
  shoulders = PBL_IF_ROUND_ELSE(w / 2, w / 4);
  l.head_w = w;
  l.head_top = GPoint(cx, size.h - shoulders - companion_neck_height(w) - companion_head_height(w));

  // The tail points down-left, at the companion's head.
  int tail_x = cx + w / 4;
  const int tail_min = bubble_x + BUBBLE_RADIUS + BUBBLE_STROKE;
  const int tail_max = bubble_x + bubble_w - BUBBLE_RADIUS - BUBBLE_STROKE - TAIL_H;
  if (tail_x > tail_max) {
    tail_x = tail_max;
  }
  if (tail_x < tail_min) {
    tail_x = tail_min;
  }
  l.tail_x = tail_x;
  return l;
}

static void prv_draw_bubble(GContext *ctx, GRect bubble, int tail_x) {
  // Ink first, then paper inset by the stroke: crisp 2 px outlines.
  graphics_context_set_fill_color(ctx, SQUIRE_COLOR_COMPANION_INK);
  graphics_fill_rect(ctx, bubble, BUBBLE_RADIUS, GCornersAll);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, grect_inset(bubble, GEdgeInsets(BUBBLE_STROKE)), BUBBLE_RADIUS - BUBBLE_STROKE, GCornersAll);

  // A stepped right-angled tail: a straight left edge and a staircase
  // hypotenuse, drawn row by row so every pixel is deliberate.
  const int base = bubble.origin.y + bubble.size.h;
  for (int i = 0; i < TAIL_H; i++) {
    const int run = TAIL_H - i;
    graphics_context_set_fill_color(ctx, SQUIRE_COLOR_COMPANION_INK);
    graphics_fill_rect(ctx, GRect(tail_x, base + i, run + BUBBLE_STROKE, 1), 0, GCornerNone);
    if (run > BUBBLE_STROKE) {
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_rect(ctx, GRect(tail_x + BUBBLE_STROKE, base + i, run - BUBBLE_STROKE, 1), 0, GCornerNone);
    }
  }
  // Open the bubble's bottom outline into the tail.
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(tail_x + BUBBLE_STROKE, base - BUBBLE_STROKE, TAIL_H - BUBBLE_STROKE, BUBBLE_STROKE),
                     0, GCornerNone);
}

static void prv_update_layer(Layer *layer, GContext *ctx) {
  TalkingSquireLayerData *data = layer_get_data(layer);
  const GSize size = layer_get_bounds(layer).size;
  const Layout l = prv_layout(data, size);

  companion_draw(ctx, l.head_top, l.head_w, size.h);
  prv_draw_bubble(ctx, l.bubble, l.tail_x);

  const int text_x = l.bubble.origin.x + BUBBLE_STROKE + BUBBLE_PAD_X;
  const int text_y = l.bubble.origin.y - BUBBLE_TEXT_LIFT;
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, data->text, fonts_get_system_font(GREETING_FONT),
                     GRect(text_x, text_y, data->text_size.w, data->text_size.h),
                     GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
  if (data->caption) {
    // What SELECT will do next, in the companion's own voice.
    graphics_context_set_text_color(ctx, SQUIRE_COLOR_AZURE_INK);
    graphics_draw_text(ctx, data->caption, fonts_get_system_font(CAPTION_FONT),
                       GRect(text_x, text_y + prv_caption_y_offset(data), data->caption_size.w, data->caption_size.h),
                       GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
  }
}
