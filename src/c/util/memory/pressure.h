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

#pragma once

#include <pebble.h>

typedef bool (*MemoryPressureHandler)(void *context);

// Maximum number of simultaneously registered callbacks. Registration uses a
// fixed table so that it can never fail because the heap is exhausted.
#define MEMORY_PRESSURE_MAX_CALLBACKS 8

void memory_pressure_init();
void memory_pressure_deinit();
// Registers handler/context. Returns false (and registers nothing) if the
// table is full or the pair is already registered.
bool memory_pressure_register_callback(MemoryPressureHandler handler, int priority, void *context);
// Unregisters the exact handler/context pair. The same handler may be
// registered with several contexts (e.g. one per ConversationManager), so
// both must match. Safe to call for a pair that isn't registered.
void memory_pressure_unregister_callback(MemoryPressureHandler handler, void *context);
// Asks registered handlers, lowest priority first, to free memory. Returns
// true if one of them freed something. Re-entrant calls (a handler that
// itself allocates) return false immediately rather than recursing.
bool memory_pressure_try_free();
int memory_pressure_callback_count();
