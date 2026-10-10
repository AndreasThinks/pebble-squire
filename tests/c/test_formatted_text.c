// The real renderer runs with ASan/UBSan; SDK calls only supply geometry/fonts.
#include <assert.h>
#include <stdio.h>
#include <pebble.h>
#include "util/formatted_text_layer.h"

struct Layer {
  GRect frame;
  void *data;
  void (*update)(Layer *, GContext *);
};
static int s_allocations_before_failure = -1;
static int s_draws;
static char s_last_draw[1024];
static bool s_fail_layer;

void *bmalloc(size_t size) {
  if (s_allocations_before_failure == 0) return NULL;
  if (s_allocations_before_failure > 0) s_allocations_before_failure--;
  return malloc(size);
}
Layer *blayer_create_with_data(GRect frame, size_t size) {
  if (s_fail_layer) return NULL;
  Layer *layer = calloc(1, sizeof(*layer));
  assert(layer);
  layer->data = calloc(1, size);
  assert(layer->data);
  layer->frame = frame;
  return layer;
}
void *layer_get_data(Layer *layer) { return layer->data; }
GRect layer_get_bounds(Layer *layer) { return layer->frame; }
void layer_destroy(Layer *layer) { free(layer->data); free(layer); }
void layer_set_update_proc(Layer *layer, void (*update)(Layer *, GContext *)) { layer->update = update; }
void layer_mark_dirty(Layer *layer) { (void)layer; }
GPoint layer_convert_point_to_screen(Layer *layer, GPoint point) { return point; }
GFont fonts_get_system_font(const char *key) { return key; }
void graphics_context_set_text_color(GContext *ctx, GColor color) {}
void graphics_draw_text(GContext *ctx, const char *text, GFont font, GRect box,
                        int overflow, GTextAlignment alignment, void *attributes) {
  s_draws++;
  snprintf(s_last_draw, sizeof(s_last_draw), "%s", text);
}
GSize graphics_text_layout_get_content_size(const char *text, GFont font, GRect box,
                                            int overflow, GTextAlignment alignment) {
  return GSize(box.size.w, strlen(text) ? 24 : 0);
}

int main(void) {
  Layer *layer = formatted_text_layer_create(GRect(0, 0, 144, 1000));
  assert(layer);
  // A heading larger than all body fragments must fit the scratch buffer.
  formatted_text_layer_set_text(layer, "# A long heading with no body text");
  layer->update(layer, NULL);
  assert(strcmp(s_last_draw, "A long heading with no body text") == 0);
  assert(formatted_text_layer_get_content_size(layer).h == 24);
  // Replacing text and changing fonts/alignment must rebuild from fragment 0.
  for (int i = 0; i < 10; i++) {
    formatted_text_layer_set_text(layer, "# Title\nbody\n## Subtitle\nend");
    formatted_text_layer_set_body_font(layer, "alternate");
    formatted_text_layer_set_text_alignment(layer, GTextAlignmentCenter);
    layer->update(layer, NULL);
    assert(strcmp(s_last_draw, "end") == 0);
    assert(formatted_text_layer_get_content_size(layer).h == 96);
  }
  // Rendering must not drop content below the old 168-pixel cutoff.
  formatted_text_layer_set_text(layer, "# 1\n# 2\n# 3\n# 4\n# 5\n# 6\n# 7\n# 8\n# 9\nlast");
  layer->update(layer, NULL);
  assert(strcmp(s_last_draw, "last") == 0);
  // Fragment allocation, layout scratch allocation and render allocation failures.
  s_allocations_before_failure = 0;
  formatted_text_layer_set_text(layer, "replacement");
  assert(formatted_text_layer_get_content_size(layer).h == 0);
  s_allocations_before_failure = 1;
  formatted_text_layer_set_text(layer, "replacement");
  assert(formatted_text_layer_get_content_size(layer).h == 0);
  int before = s_draws;
  s_allocations_before_failure = 0;
  layer->update(layer, NULL);
  assert(s_draws == before);
  s_allocations_before_failure = -1;
  formatted_text_layer_set_text(layer, "recovered");
  layer->update(layer, NULL);
  assert(strcmp(s_last_draw, "recovered") == 0);
  formatted_text_layer_destroy(layer);
  s_fail_layer = true;
  assert(formatted_text_layer_create(GRect(0, 0, 144, 168)) == NULL);
  puts("ok - formatted text headings, repeated layout, tall displays and allocation failures");
  return 0;
}
