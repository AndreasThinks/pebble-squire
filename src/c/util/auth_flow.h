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

#ifndef AUTH_FLOW_H
#define AUTH_FLOW_H

#include <pebble.h>

typedef void (*AuthFlowCompleteCallback)(bool success, const char* agent_username);

// Reasons sent by the phone as the value of TELEGRAM_AUTH_ERROR. Keep in sync
// with AUTH_ERROR_REASONS in src/pkjs/telegram/auth.js.
typedef enum {
  AuthErrorGeneric = 1,
  AuthErrorInvalidPhone = 2,
  AuthErrorInvalidCode = 3,
  AuthErrorCodeExpired = 4,
  AuthErrorFloodWait = 5,
  AuthErrorBadPassword = 6,
  AuthErrorNoPendingSignIn = 7,
  AuthErrorAppNotConfigured = 8,
  AuthErrorNetwork = 9,
  AuthErrorNoAccount = 10,
} AuthErrorReason;

void auth_flow_start(AuthFlowCompleteCallback callback);
// value is the string payload (agent username for TELEGRAM_CONNECTED),
// number the integer payload (code length, error reason, ...).
void auth_flow_handle_message(uint32_t key, const char* value, int32_t number);
void auth_flow_cancel(void);

#endif // AUTH_FLOW_H
