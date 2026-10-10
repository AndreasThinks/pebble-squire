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

#include "auth_flow.h"
#include "auth_entry_window.h"
#include "logging.h"
#include "memory/malloc.h"
#include "memory/sdk.h"
#include "result_window.h"

#include <pebble.h>

static AuthFlowCompleteCallback s_complete_callback = NULL;
static bool s_waiting_for_code = false;
static int s_code_length = 5;
static char s_agent_username[64];

#define DEFAULT_CODE_LENGTH 5

static const char* prv_error_message(int32_t reason) {
  switch (reason) {
    case AuthErrorInvalidPhone:
      return "Telegram didn't accept that phone number. Check it and try again.";
    case AuthErrorInvalidCode:
      return "That code wasn't right. Try entering it again.";
    case AuthErrorCodeExpired:
      return "That code has expired. Start again to get a new one.";
    case AuthErrorFloodWait:
      return "Too many attempts. Telegram asks you to wait before trying again.";
    case AuthErrorBadPassword:
      return "Incorrect two-step verification password. Re-enter it in Squire's settings on your phone.";
    case AuthErrorNoPendingSignIn:
      return "The sign-in attempt expired. Start again.";
    case AuthErrorAppNotConfigured:
      return "This build of Squire has no Telegram API credentials. Rebuild it with TELEGRAM_APP_ID and TELEGRAM_APP_HASH set.";
    case AuthErrorNetwork:
      return "Couldn't reach Telegram. Check your phone's connection and try again.";
    case AuthErrorNoAccount:
      return "There's no Telegram account for that number. Sign up in the Telegram app first.";
    default:
      return "Could not sign in to Telegram. Please try again.";
  }
}

static void prv_cancel(void) {
  s_complete_callback = NULL;
  s_waiting_for_code = false;
  s_agent_username[0] = '\0';
}

static void prv_phone_entered(const char* value) {
  if (!value || strlen(value) == 0) return;
  char normalized[20];
  snprintf(normalized, sizeof(normalized), "+%s", value);

  DictionaryIterator *iter;
  AppMessageResult result = app_message_outbox_begin(&iter);
  if (result != APP_MSG_OK) {
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Failed to prepare auth outbox: %d", result);
    result_window_push_persistent("Error", "Could not send to phone. Try again.", NULL, GColorWhite);
    return;
  }
  dict_write_cstring(iter, MESSAGE_KEY_TELEGRAM_START_AUTH, normalized);
  result = app_message_outbox_send();
  if (result != APP_MSG_OK) {
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Failed to send auth message: %d", result);
    result_window_push_persistent("Error", "Could not send to phone. Try again.", NULL, GColorWhite);
    return;
  }

  s_waiting_for_code = true;
  // Don't block the UI; the code entry screen will be pushed when TELEGRAM_CODE_SENT arrives.
}

static void prv_code_entered(const char* value) {
  if (!value || strlen(value) == 0) return;

  DictionaryIterator *iter;
  AppMessageResult result = app_message_outbox_begin(&iter);
  if (result != APP_MSG_OK) {
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Failed to prepare auth outbox: %d", result);
    result_window_push_persistent("Error", "Could not send to phone. Try again.", NULL, GColorWhite);
    return;
  }
  dict_write_cstring(iter, MESSAGE_KEY_TELEGRAM_PROVIDE_CODE, value);
  result = app_message_outbox_send();
  if (result != APP_MSG_OK) {
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Failed to send auth message: %d", result);
    result_window_push_persistent("Error", "Could not send to phone. Try again.", NULL, GColorWhite);
    return;
  }

  result_window_push("Verifying...", "Checking your code with Telegram.", NULL, GColorWhite);
}

void auth_flow_start(AuthFlowCompleteCallback callback) {
  s_complete_callback = callback;
  s_waiting_for_code = false;
  s_agent_username[0] = '\0';
  auth_entry_window_push("Phone Number", 15, prv_phone_entered, prv_cancel);
}

void auth_flow_handle_message(uint32_t key, const char* value, int32_t number) {
  if (key == MESSAGE_KEY_TELEGRAM_CODE_SENT) {
    if (s_waiting_for_code) {
      // The phone reports the code length Telegram announced (older phone
      // code sent 1); fall back to the usual five digits.
      s_code_length = (number >= 4 && number <= 8) ? number : DEFAULT_CODE_LENGTH;
      auth_entry_window_push_with_prefix("Enter Code", s_code_length, false, prv_code_entered, prv_cancel);
    }
  } else if (key == MESSAGE_KEY_TELEGRAM_CONNECTED) {
    if (value) {
      snprintf(s_agent_username, sizeof(s_agent_username), "%s", value);
    } else {
      s_agent_username[0] = '\0';
    }
    if (s_complete_callback) {
      AuthFlowCompleteCallback cb = s_complete_callback;
      s_complete_callback = NULL;
      s_waiting_for_code = false;
      cb(true, s_agent_username[0] ? s_agent_username : NULL);
      s_agent_username[0] = '\0';
    }
  } else if (key == MESSAGE_KEY_TELEGRAM_PASSWORD_NEEDED) {
    // Two-step verification: the password can't be entered on the watch.
    // Keep the flow pending — once the password is entered on the phone's
    // settings page, TELEGRAM_CONNECTED will arrive and complete it.
    result_window_push_persistent("Two-Step Verification",
        "Your account has a Telegram cloud password. Open Squire's settings in the Pebble phone app and enter it there to finish signing in.",
        NULL, GColorWhite);
    s_waiting_for_code = false;
  } else if (key == MESSAGE_KEY_TELEGRAM_AUTH_ERROR) {
    if (number == AuthErrorInvalidCode && s_complete_callback) {
      // The phone keeps the pending sign-in; let the user re-enter the code
      // once they've dismissed the error.
      auth_entry_window_push_with_prefix("Enter Code", s_code_length, false, prv_code_entered, prv_cancel);
      result_window_push_persistent("Sign-In Failed", prv_error_message(number), NULL, GColorWhite);
      return;
    }
    result_window_push_persistent("Sign-In Failed", prv_error_message(number), NULL, GColorWhite);
    if (s_complete_callback) {
      AuthFlowCompleteCallback cb = s_complete_callback;
      s_complete_callback = NULL;
      s_waiting_for_code = false;
      cb(false, NULL);
    }
  }
}

void auth_flow_cancel(void) {
  prv_cancel();
}
