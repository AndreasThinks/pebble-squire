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

#include "root_window.h"
#include "release_notes.h"
#include "consent/consent.h"
#include "converse/session_window.h"
#include "converse/conversation_manager.h"
#include "converse/history.h"
#include "version/version.h"
#include "settings/settings.h"

#include <pebble.h>
#include <pebble-events/pebble-events.h>

#include "util/logging.h"
#include "util/memory/pressure.h"


#define QUICK_LAUNCH_TIMEOUT_MS 60000

static RootWindow* s_root_window = NULL;
static EventHandle s_history_handle;

static void prv_history_message_handler(DictionaryIterator *iter, void *context) {
  bool saw_history_key = false;
  for (Tuple *tuple = dict_read_first(iter); tuple; tuple = dict_read_next(iter)) {
    if (tuple->key == MESSAGE_KEY_HISTORY_PROMPT) {
      saw_history_key = true;
      history_add_prompt(tuple->value->cstring);
    } else if (tuple->key == MESSAGE_KEY_HISTORY_RESPONSE) {
      saw_history_key = true;
      history_add_response(tuple->value->cstring);
    } else if (tuple->key == MESSAGE_KEY_HISTORY_THREAD_ID) {
      saw_history_key = true;
      history_set_thread_id(tuple->value->cstring);
    } else if (tuple->key == MESSAGE_KEY_HISTORY_DONE) {
      saw_history_key = true;
      history_set_done();
    }
  }
  if (saw_history_key) {
    history_set_loading(false);
  }
}

static void prv_init(void) {
  memory_pressure_init();
  version_init();
  consent_migrate();
  settings_init();
  history_init();
  conversation_manager_init();
  s_history_handle = events_app_message_register_inbox_received(prv_history_message_handler, NULL);
  events_app_message_open();
}

static void prv_deinit(void) {
  if (s_root_window) {
    root_window_destroy(s_root_window);
  }
  events_app_message_unsubscribe(s_history_handle);
}

int main(void) {
  VersionInfo version_info = version_get_current();
  SQUIRE_LOG(APP_LOG_LEVEL_INFO, "Squire %d.%d", version_info.major, version_info.minor);
  prv_init();
  
  if (must_present_consent()) {
    consent_window_push();
  } else {
    if (launch_reason() == APP_LAUNCH_QUICK_LAUNCH) {
      QuickLaunchBehaviour quick_launch_behaviour = settings_get_quick_launch_behaviour();
      if (quick_launch_behaviour != QuickLaunchBehaviourHomeScreen) {
        session_window_push(quick_launch_behaviour == QuickLaunchBehaviourConverseWithTimeout ? QUICK_LAUNCH_TIMEOUT_MS : 0, NULL);
      } else {
        s_root_window = root_window_create();
        root_window_push(s_root_window);
      }
    } else {
      s_root_window = root_window_create();
      root_window_push(s_root_window);
    }
    release_notes_maybe_push();
  }

  app_event_loop();
  prv_deinit();
}
