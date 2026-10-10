//
// Created by Katharine Berry on 3/5/25.
//

#include <pebble.h>
#include "style.h"

void squire_status_bar_config(StatusBarLayer *status_bar) {
  // Conversation chrome: a slim azure band carrying the clock.
  status_bar_layer_set_colors(status_bar, COLOR_FALLBACK(SQUIRE_COLOR_AZURE, GColorWhite), COLOR_FALLBACK(GColorWhite, GColorBlack));
  status_bar_layer_set_separator_mode(status_bar, PBL_IF_COLOR_ELSE(StatusBarLayerSeparatorModeNone, StatusBarLayerSeparatorModeDotted));
}

void squire_status_bar_result_pane_config(StatusBarLayer *status_bar) {
  status_bar_layer_set_colors(status_bar, GColorWhite, GColorBlack);
  status_bar_layer_set_separator_mode(status_bar, StatusBarLayerSeparatorModeDotted);
}

void squire_action_menu_colors(ActionMenuConfig *config) {
  config->colors.background = SQUIRE_COLOR_AZURE;
  config->colors.foreground = GColorWhite;
}

void squire_draw_select_glyph(GContext *ctx, GPoint center, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_fill_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, center, 5);
  graphics_fill_circle(ctx, center, 2);
}
