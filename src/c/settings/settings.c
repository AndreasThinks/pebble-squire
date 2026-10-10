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

#include "settings.h"
#include <pebble.h>
#include <pebble-events/pebble-events.h>

#include "../util/persist_keys.h"
#include "../util/logging.h"
#include "../util/auth_flow.h"
#include "../util/tuple_util.h"
#include "../util/result_window.h"
#include "../util/transport.h"

static EventHandle s_event_handle;

static void prv_app_message_handler(DictionaryIterator *iter, void *context);

void settings_init() {
  s_event_handle = events_app_message_register_inbox_received(prv_app_message_handler, NULL);
}

void settings_deinit() {
  events_app_message_unsubscribe(s_event_handle);
}

QuickLaunchBehaviour settings_get_quick_launch_behaviour() {
  int result = persist_read_int(PERSIST_KEY_QUICK_LAUNCH_BEHAVIOUR);
  if (result == 0) {
    return QuickLaunchBehaviourConverseWithTimeout;
  }
  return result;
}

bool settings_get_should_confirm_transcripts() {
  return persist_read_bool(PERSIST_KEY_CONFIRM_TRANSCRIPTS);
}

bool settings_is_telegram_connected() {
  return persist_read_bool(PERSIST_KEY_TELEGRAM_CONNECTED);
}

void settings_set_telegram_connected(bool connected) {
  status_t status = persist_write_bool(PERSIST_KEY_TELEGRAM_CONNECTED, connected);
  if (status < 0) {
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Failed to write telegram connected: %d", status);
  }
}


static void prv_app_message_handler(DictionaryIterator *iter, void *context) {
  // Auth messages push windows; a resend must not push them twice.
  static TransportReplayGuard s_guard;
  if (transport_is_replay(&s_guard, iter)) {
    return;
  }
  for (Tuple *tuple = dict_read_first(iter); tuple; tuple = dict_read_next(iter)) {
    if (tuple->key == MESSAGE_KEY_QUICK_LAUNCH_BEHAVIOUR) {
      int value = atoi(tuple->value->cstring);
      status_t status = persist_write_int(PERSIST_KEY_QUICK_LAUNCH_BEHAVIOUR, value);
      if (status < 0) {
        SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Failed to write quick launch behaviour: %d", status);
      }
    } else if (tuple->key == MESSAGE_KEY_CONFIRM_TRANSCRIPTS) {
      status_t status = persist_write_bool(PERSIST_KEY_CONFIRM_TRANSCRIPTS, tuple->value->int8);
      if (status < 0) {
        SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Failed to write confirm transcripts setting: %d", status);
      }
    } else if (tuple->key == MESSAGE_KEY_TELEGRAM_CONNECTED) {
      bool connected = tuple_get_int32(tuple) != 0;
      status_t status = persist_write_bool(PERSIST_KEY_TELEGRAM_CONNECTED, connected);
      if (status < 0) {
        SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Failed to write telegram connected: %d", status);
      }
      SQUIRE_LOG(APP_LOG_LEVEL_INFO, "Telegram connected: %s", connected ? "true" : "false");
      // Only a positive status is an auth success; a disconnect notification
      // (e.g. the phone reporting an expired session mid-auth) must not
      // complete a pending auth flow as signed in.
      if (connected) {
        const char* username = NULL;
        Tuple* username_tuple = dict_find(iter, MESSAGE_KEY_AGENT_TELEGRAM_USERNAME);
        if (username_tuple && username_tuple->type == TUPLE_CSTRING && username_tuple->length > 1) {
          username = username_tuple->value->cstring;
        }
        auth_flow_handle_message(tuple->key, username, 1);
      }
    } else if (tuple->key == MESSAGE_KEY_TELEGRAM_CODE_SENT ||
               tuple->key == MESSAGE_KEY_TELEGRAM_PASSWORD_NEEDED ||
               tuple->key == MESSAGE_KEY_TELEGRAM_AUTH_ERROR) {
      auth_flow_handle_message(tuple->key, NULL, tuple_get_int32(tuple));
    } else if (tuple->key == MESSAGE_KEY_TELEGRAM_LOGOUT_RESULT) {
      // Replaces the "Signing out" card pushed from the home screen.
      result_window_dismiss_progress();
      if (tuple_get_int32(tuple) == 1) {
        result_window_push(ResultToneSuccess, "Signed out", "This watch's Telegram session was ended on Telegram's servers.", RESULT_WINDOW_SHORT_MS);
      } else {
        result_window_push(ResultToneWarning, "Signed out here only",
            "Squire Plus forgot its Telegram session, but couldn't reach Telegram to end it. Remove \"Squire Plus\" under Telegram > Settings > Devices.",
            RESULT_WINDOW_PERSISTENT);
      }
    }
  }
}