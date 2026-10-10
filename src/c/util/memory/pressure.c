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

#include "pressure.h"
#include "../logging.h"
#include <pebble.h>

typedef struct {
  MemoryPressureHandler handler;
  int priority;
  void *context;
} MemoryPressureCallbackEntry;

static MemoryPressureCallbackEntry s_callbacks[MEMORY_PRESSURE_MAX_CALLBACKS];
static int s_callback_count = 0;
static bool s_freeing = false;

void memory_pressure_init() {
  s_callback_count = 0;
  s_freeing = false;
}

void memory_pressure_deinit() {
  s_callback_count = 0;
}

bool memory_pressure_register_callback(MemoryPressureHandler handler, int priority, void *context) {
  for (int i = 0; i < s_callback_count; ++i) {
    if (s_callbacks[i].handler == handler && s_callbacks[i].context == context) {
      return false;
    }
  }
  if (s_callback_count >= MEMORY_PRESSURE_MAX_CALLBACKS) {
    SQUIRE_LOG(APP_LOG_LEVEL_ERROR, "memory_pressure_register_callback: table full");
    return false;
  }
  s_callbacks[s_callback_count++] = (MemoryPressureCallbackEntry) {
    .handler = handler,
    .priority = priority,
    .context = context,
  };
  SQUIRE_LOG(APP_LOG_LEVEL_DEBUG, "memory_pressure_register_callback: %p, priority %d", handler, priority);
  return true;
}

void memory_pressure_unregister_callback(MemoryPressureHandler handler, void *context) {
  for (int i = 0; i < s_callback_count; ++i) {
    if (s_callbacks[i].handler == handler && s_callbacks[i].context == context) {
      for (int j = i; j < s_callback_count - 1; ++j) {
        s_callbacks[j] = s_callbacks[j + 1];
      }
      --s_callback_count;
      return;
    }
  }
}

int memory_pressure_callback_count() {
  return s_callback_count;
}

bool memory_pressure_try_free() {
  if (s_freeing) {
    // A handler is already running and has itself run out of memory. Don't
    // recurse into the handlers (they're mid-mutation); let the inner
    // allocation fail instead.
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Memory emergency while already freeing memory; not recursing.");
    return false;
  }
  SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Memory emergency! Trying to free memory.");
  if (s_callback_count == 0) {
    SQUIRE_LOG(APP_LOG_LEVEL_DEBUG, "No memory freeing callbacks registered");
    return false;
  }
  int max_priority = 0;
  for (int i = 0; i < s_callback_count; ++i) {
    if (s_callbacks[i].priority > max_priority) {
      max_priority = s_callbacks[i].priority;
    }
  }
  s_freeing = true;
  bool freed = false;
  for (int p = 0; p <= max_priority && !freed; ++p) {
    SQUIRE_LOG(APP_LOG_LEVEL_DEBUG, "Trying priority level %d", p);
    // Handlers may unregister themselves (or others) while running, so copy
    // the entry and re-check bounds on every step rather than caching them.
    for (int i = 0; i < s_callback_count; ++i) {
      MemoryPressureCallbackEntry entry = s_callbacks[i];
      if (entry.priority != p) {
        continue;
      }
      SQUIRE_LOG(APP_LOG_LEVEL_DEBUG, "Calling memory pressure callback %p with priority %d", entry.handler, entry.priority);
      if (entry.handler(entry.context)) {
        SQUIRE_LOG(APP_LOG_LEVEL_DEBUG, "Freed some memory!");
        freed = true;
        break;
      }
      SQUIRE_LOG(APP_LOG_LEVEL_DEBUG, "No joy.");
    }
  }
  s_freeing = false;
  return freed;
}
