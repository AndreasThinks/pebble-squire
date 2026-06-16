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

#ifndef AUTH_ENTRY_WINDOW_H
#define AUTH_ENTRY_WINDOW_H

#include <pebble.h>

typedef void (*AuthEntryCallback)(const char* value);

void auth_entry_window_push(const char* title, int max_length, AuthEntryCallback callback);
void auth_entry_window_push_with_prefix(const char* title, int max_length, bool show_plus_prefix, AuthEntryCallback callback);

#endif // AUTH_ENTRY_WINDOW_H
