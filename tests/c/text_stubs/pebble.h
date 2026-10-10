// Minimal graphics surface for sanitizer tests of the real formatted-text code.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int16_t x, y; } GPoint;
typedef struct { int16_t w, h; } GSize;
typedef struct { GPoint origin; GSize size; } GRect;
#define GPoint(x, y) ((GPoint){(x), (y)})
#define GSize(w, h) ((GSize){(w), (h)})
#define GRect(x, y, w, h) ((GRect){GPoint(x, y), GSize(w, h)})
typedef struct Layer Layer;
typedef void GContext;
typedef const char *GFont;
typedef int GColor;
typedef int GTextAlignment;
#define GTextAlignmentLeft 0
#define GTextAlignmentCenter 1
#define GColorBlack 0
#define GTextOverflowModeWordWrap 0
#define FONT_KEY_GOTHIC_28_BOLD "title"
#define FONT_KEY_GOTHIC_24_BOLD "subtitle"
#define FONT_KEY_GOTHIC_24 "body"
// Opaque SDK types mentioned by memory/sdk.h; not used by this test.
typedef void Window;
typedef void ActionBarLayer;
typedef void TextLayer;
typedef void MenuLayer;
typedef void SimpleMenuLayer;
typedef void SimpleMenuSection;
typedef void BitmapLayer;
typedef void ActionMenuLevel;
typedef void ScrollLayer;
typedef void StatusBarLayer;
typedef void GBitmap;
typedef void GDrawCommandImage;
typedef void GDrawCommandSequence;
void *layer_get_data(Layer *layer);
GRect layer_get_bounds(Layer *layer);
void layer_destroy(Layer *layer);
void layer_set_update_proc(Layer *layer, void (*update)(Layer *, GContext *));
void layer_mark_dirty(Layer *layer);
GPoint layer_convert_point_to_screen(Layer *layer, GPoint point);
GFont fonts_get_system_font(const char *key);
void graphics_context_set_text_color(GContext *ctx, GColor color);
void graphics_draw_text(GContext *ctx, const char *text, GFont font, GRect box,
                        int overflow, GTextAlignment alignment, void *attributes);
GSize graphics_text_layout_get_content_size(const char *text, GFont font, GRect box,
                                            int overflow, GTextAlignment alignment);
