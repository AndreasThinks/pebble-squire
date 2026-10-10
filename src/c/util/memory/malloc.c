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

#include "malloc.h"
#include "pressure.h"
#include "../logging.h"

#include <pebble.h>

// Below this much free heap we ask the memory pressure handlers to make room
// before allocating, so the system keeps some headroom for itself.
#define BMALLOC_LOW_HEAP_THRESHOLD 750

// Allocates size bytes, asking the memory pressure handlers to free memory
// (e.g. old conversation entries) when the heap is low or malloc fails.
// Returns NULL if the allocation still can't be satisfied once nothing more
// can be freed: every caller must handle that. There is deliberately no
// sleep-and-retry loop: the app is single-threaded, so nothing can free
// memory while we wait.
void *bmalloc(size_t size) {
#if defined(__arm__)
  register uintptr_t lr __asm("lr");
  const uintptr_t saved_lr = lr;
#else
  const uintptr_t saved_lr = 0;
#endif
  (void)saved_lr;
  int heap_size = heap_bytes_free();
  SQUIRE_LOG(APP_LOG_LEVEL_DEBUG, "malloc request: %d; free: %d", (int)size, heap_size);
  while (true) {
    heap_size = heap_bytes_free();
    if (heap_size > BMALLOC_LOW_HEAP_THRESHOLD) {
      void *ptr = malloc(size);
      if (ptr) {
        SQUIRE_LOG(APP_LOG_LEVEL_DEBUG_VERBOSE, "malloc returned %p for caller %p", ptr, (void *)saved_lr);
        return ptr;
      }
      SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Out of memory! Need to allocate %d bytes; %d bytes free.", (int)size, heap_size);
    } else {
      SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Low memory (%d byte free); trying to free some before allocating %d bytes.", heap_size, (int)size);
    }
    if (!memory_pressure_try_free()) {
      // Nothing more to free: make one last attempt, even below the
      // threshold, and report failure to the caller if it doesn't work.
      void *tried = malloc(size);
      if (tried) {
        SQUIRE_LOG(APP_LOG_LEVEL_DEBUG, "malloc returned %p for caller %p", tried, (void *)saved_lr);
        return tried;
      }
      SQUIRE_LOG(APP_LOG_LEVEL_ERROR, "Critical: failed to allocate %d bytes for caller %p.", (int)size, (void *)saved_lr);
      return NULL;
    }
    int new_heap_size = heap_bytes_free();
    SQUIRE_LOG(APP_LOG_LEVEL_INFO, "Freed %d bytes, heap size is now %d. Retrying allocation of %d bytes.", new_heap_size - heap_size, new_heap_size, (int)size);
  }
}
