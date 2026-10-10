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

#include "conversation_manager.h"

#include "conversation.h"
#include "history.h"
#include "../util/memory/malloc.h"
#include "../util/memory/pressure.h"
#include "../util/logging.h"
#include "../util/strings.h"
#include "../util/tuple_util.h"
#include "../util/transport.h"

#include <pebble-events/pebble-events.h>
#include <pebble.h>

// How often the watchdog checks on a pending request, and how long a request
// may go without hearing anything from the phone before we give up. The
// phone gives up on the agent after 5 minutes (see src/pkjs/session.js) and
// then always sends a terminal message, so this only fires if that message
// can't reach us.
#define REQUEST_WATCHDOG_INTERVAL_MS 30000
#define REQUEST_SILENCE_LIMIT_S 330

struct ConversationManager {
  Conversation* conversation;
  // Per manager: every subscribed handler sees each message once.
  TransportReplayGuard replay_guard;
  EventHandle app_message_handle;
  void* context;
  ConversationManagerUpdateHandler handler;
  ConversationManagerEntryDeletedHandler deletion_handler;
  // Correlates replies with the prompt that caused them. Messages tagged with
  // any other REQUEST_ID (e.g. a reply to a prompt sent from a window that has
  // since been closed) are ignored.
  int32_t request_id;
  bool request_pending;
  bool awaiting_outbox_ack;
  // A fragment of the current reply couldn't be stored; the rest of the
  // reply is dropped rather than shown with a hole in it.
  bool reply_failed;
  bool saw_dropped_message;
  time_t last_activity;
  AppTimer* watchdog;
};

static void prv_conversation_updated(ConversationManager* manager, bool new_entry);
static void prv_handle_app_message_outbox_sent(DictionaryIterator *iterator, void *context);
static void prv_handle_app_message_outbox_failed(DictionaryIterator *iterator, AppMessageResult reason, void *context);
static void prv_handle_app_message_inbox_received(DictionaryIterator *iterator, void *context);
static void prv_handle_app_message_inbox_dropped(AppMessageResult result, void *context);
static bool prv_handle_memory_pressure(void *context);
static void prv_add_error(ConversationManager* manager, const char* message);
static void prv_finish_request(ConversationManager* manager);
static void prv_schedule_watchdog(ConversationManager* manager);
static void prv_watchdog_fired(void* context);

static ConversationManager* s_conversation_manager;
static int32_t s_next_request_id = 1;

// Pebble's per-message hard limit is ~8 KB. We request 5000 bytes so that a
// single app message (one dictionary tuple) can carry a chunk with headroom,
// while the pkjs side keeps every message (including tuple headers) within
// this budget (see src/pkjs/lib/message_queue.js). Keeping this modest
// preserves watch heap — see prv_handle_memory_pressure for why that matters.
#define SQUIRE_APP_MESSAGE_BUFFER_SIZE 5000

void conversation_manager_init() {
  events_app_message_request_outbox_size(SQUIRE_APP_MESSAGE_BUFFER_SIZE);
  events_app_message_request_inbox_size(SQUIRE_APP_MESSAGE_BUFFER_SIZE);
  // Make request IDs differ between launches, so the phone can't confuse a
  // reply from a previous run with one for this run.
  s_next_request_id = (int32_t)((((uint32_t)time(NULL) << 8) ^ (uint32_t)rand()) & 0x7FFFFFFF);
  if (s_next_request_id == 0) {
    s_next_request_id = 1;
  }
}

ConversationManager* conversation_manager_create() {
  ConversationManager* manager = bmalloc(sizeof(ConversationManager));
  if (!manager) {
    return NULL;
  }
  memset(manager, 0, sizeof(ConversationManager));
  manager->conversation = conversation_create();
  if (!manager->conversation) {
    free(manager);
    return NULL;
  }
  manager->app_message_handle = events_app_message_subscribe_handlers((EventAppMessageHandlers){
      .sent = prv_handle_app_message_outbox_sent,
      .failed = prv_handle_app_message_outbox_failed,
      .received = prv_handle_app_message_inbox_received,
      .dropped = prv_handle_app_message_inbox_dropped,
  }, manager);
  if (!manager->app_message_handle) {
    conversation_destroy(manager->conversation);
    free(manager);
    return NULL;
  }
  s_conversation_manager = manager;
  // Losing this only means we can't shed old entries under memory pressure.
  if (!memory_pressure_register_callback(prv_handle_memory_pressure, 1, manager)) {
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Couldn't register memory pressure callback.");
  }
  return manager;
}

void conversation_manager_destroy(ConversationManager* manager) {
  if (!manager) {
    return;
  }
  // Unregister first: the callback's context is about to be freed.
  memory_pressure_unregister_callback(prv_handle_memory_pressure, manager);
  events_app_message_unsubscribe(manager->app_message_handle);
  if (manager->watchdog) {
    app_timer_cancel(manager->watchdog);
    manager->watchdog = NULL;
  }
  conversation_destroy(manager->conversation);
  if (s_conversation_manager == manager) {
    s_conversation_manager = NULL;
  }
  free(manager);
}

ConversationManager* conversation_manager_get_current() {
  return s_conversation_manager;
}

Conversation* conversation_manager_get_conversation(ConversationManager* manager) {
  return manager->conversation;
}

bool conversation_manager_is_request_pending(ConversationManager* manager) {
  return manager->request_pending;
}

void conversation_manager_set_handler(ConversationManager* manager, ConversationManagerUpdateHandler handler, void* context) {
  manager->handler = handler;
  manager->context = context;
}

void conversation_manager_set_deletion_handler(ConversationManager* manager, ConversationManagerEntryDeletedHandler handler) {
  manager->deletion_handler = handler;
}

static void prv_add_error(ConversationManager* manager, const char* message) {
  conversation_complete_response(manager->conversation);
  prv_conversation_updated(manager, false);
  if (conversation_add_error(manager->conversation, message)) {
    prv_conversation_updated(manager, true);
  } else {
    SQUIRE_LOG(APP_LOG_LEVEL_ERROR, "Couldn't store error message: out of memory.");
  }
}

static void prv_finish_request(ConversationManager* manager) {
  manager->request_pending = false;
  manager->awaiting_outbox_ack = false;
  if (manager->watchdog) {
    app_timer_cancel(manager->watchdog);
    manager->watchdog = NULL;
  }
}

void conversation_manager_add_input(ConversationManager* manager, const char* input) {
  // Allocate everything we need before touching the outbox: once
  // app_message_outbox_begin succeeds we must send.
  //
  // The Android Pebble app has a fun bug where any double-quotes in a
  // message will cause it to be dropped, this is a bodge workaround.
  size_t input_len = strlen(input);
  char* bridge_bodge = bmalloc(input_len + 1);
  if (!bridge_bodge) {
    prv_add_error(manager, "Not enough memory to send that.");
    return;
  }
  memcpy(bridge_bodge, input, input_len + 1);
  strings_fix_android_bridge_bodge(bridge_bodge);

  if (!conversation_add_prompt(manager->conversation, input)) {
    free(bridge_bodge);
    prv_add_error(manager, "Not enough memory to send that.");
    return;
  }
  prv_conversation_updated(manager, true);

  // Any earlier request is abandoned: its late replies carry the old ID and
  // will be ignored, and the phone cancels it when this prompt arrives.
  prv_finish_request(manager);
  manager->reply_failed = false;
  manager->saw_dropped_message = false;
  manager->request_id = s_next_request_id;
  s_next_request_id = (s_next_request_id >= 0x7FFFFFFF) ? 1 : s_next_request_id + 1;

  DictionaryIterator *iter;
  AppMessageResult result = app_message_outbox_begin(&iter);
  if (result != APP_MSG_OK) {
    free(bridge_bodge);
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Preparing outbox failed: %d.", result);
    prv_add_error(manager, "Sending to phone failed. Try again.");
    return;
  }
  dict_write_cstring(iter, MESSAGE_KEY_PROMPT, bridge_bodge);
  free(bridge_bodge);
  dict_write_int32(iter, MESSAGE_KEY_REQUEST_ID, manager->request_id);

  const char* thread_id = conversation_get_thread_id(manager->conversation);
  if (thread_id[0] != 0) {
    SQUIRE_LOG(APP_LOG_LEVEL_INFO, "Continuing previous conversation %s.", thread_id);
    dict_write_cstring(iter, MESSAGE_KEY_THREAD_ID, thread_id);
  }
  result = app_message_outbox_send();
  if (result != APP_MSG_OK) {
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Sending message failed: %d.", result);
    prv_add_error(manager, "Sending to phone failed. Try again.");
    return;
  }
  manager->request_pending = true;
  manager->awaiting_outbox_ack = true;
  manager->last_activity = time(NULL);
  prv_schedule_watchdog(manager);
}

void conversation_manager_add_action(ConversationManager* manager, ConversationAction* action) {
  SQUIRE_LOG(APP_LOG_LEVEL_DEBUG, "Adding action to conversation.");
  if (conversation_add_action(manager->conversation, action)) {
    prv_conversation_updated(manager, true);
  }
}

static bool prv_is_our_outbox_message(ConversationManager* manager, DictionaryIterator *iterator) {
  if (!manager->awaiting_outbox_ack || !iterator) {
    return false;
  }
  Tuple* request = dict_find(iterator, MESSAGE_KEY_REQUEST_ID);
  return request && tuple_get_int32(request) == manager->request_id;
}

static void prv_handle_app_message_outbox_sent(DictionaryIterator *iterator, void *context) {
  ConversationManager* manager = context;
  if (prv_is_our_outbox_message(manager, iterator)) {
    SQUIRE_LOG(APP_LOG_LEVEL_INFO, "Sent prompt successfully.");
    manager->awaiting_outbox_ack = false;
  }
}

static void prv_handle_app_message_outbox_failed(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  ConversationManager* manager = context;
  // Every subscriber hears about every outbox failure; only react to ours.
  if (!prv_is_our_outbox_message(manager, iterator)) {
    return;
  }
  SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Sending prompt failed: %d", reason);
  prv_finish_request(manager);
  prv_add_error(manager, "Sending to phone failed. Try again.");
}

static void prv_handle_app_message_inbox_received(DictionaryIterator *iter, void *context) {
  ConversationManager* manager = context;
  if (transport_is_replay(&manager->replay_guard, iter)) {
    // The phone resent something we already applied (its ack was lost).
    return;
  }
  Tuple* request = dict_find(iter, MESSAGE_KEY_REQUEST_ID);
  if (request) {
    if (!manager->request_pending || tuple_get_int32(request) != manager->request_id) {
      // A reply to some other (abandoned or finished) request.
      return;
    }
  } else if (!manager->request_pending) {
    // Untagged conversation traffic is only meaningful mid-request.
    return;
  }

  // Process in a fixed order, regardless of how the phone ordered the tuples.
  Tuple *tuple;
  bool activity = false;
  if ((tuple = dict_find(iter, MESSAGE_KEY_CHAT_RESET))) {
    activity = true;
    if (!manager->reply_failed && conversation_reset_response(manager->conversation)) {
      prv_conversation_updated(manager, false);
    }
  }
  if ((tuple = dict_find(iter, MESSAGE_KEY_CHAT))) {
    activity = true;
    if (!manager->reply_failed) {
      ConversationAppendResult result = conversation_add_response_fragment(manager->conversation, tuple->value->cstring);
      if (result == ConversationAppendFailed) {
        manager->reply_failed = true;
        prv_add_error(manager, "Out of memory: the reply was cut short.");
      } else {
        prv_conversation_updated(manager, result == ConversationAppendNewEntry);
      }
    }
  }
  if ((tuple = dict_find(iter, MESSAGE_KEY_TYPING))) {
    activity = true;
    if (!manager->reply_failed) {
      ConversationAppendResult result = conversation_add_response_fragment(manager->conversation, "");
      if (result != ConversationAppendFailed) {
        prv_conversation_updated(manager, result == ConversationAppendNewEntry);
      }
    }
  }
  if ((tuple = dict_find(iter, MESSAGE_KEY_FUNCTION))) {
    activity = true;
    SQUIRE_LOG(APP_LOG_LEVEL_INFO, "Received function.");
    conversation_complete_response(manager->conversation);
    prv_conversation_updated(manager, false);
    if (conversation_add_thought(manager->conversation, tuple->value->cstring)) {
      prv_conversation_updated(manager, true);
    }
  }
  if ((tuple = dict_find(iter, MESSAGE_KEY_THREAD_ID))) {
    conversation_set_thread_id(manager->conversation, tuple->value->cstring);
    history_push_thread_id(tuple->value->cstring);
  }
  if ((tuple = dict_find(iter, MESSAGE_KEY_ACTION_SETTINGS_UPDATED))) {
    char *sentence = bmalloc(strlen(tuple->value->cstring) + 1);
    if (sentence) {
      strcpy(sentence, tuple->value->cstring);
      ConversationAction action = {
        .type = ConversationActionTypeGenericSentence,
        .action = {
          .generic_sentence = {
            .sentence = sentence,
          }
        },
      };
      if (conversation_add_action(manager->conversation, &action)) {
        prv_conversation_updated(manager, true);
      } else {
        free(sentence);
      }
    }
  }
  if ((tuple = dict_find(iter, MESSAGE_KEY_WARNING))) {
    activity = true;
    prv_add_error(manager, tuple->value->cstring);
  }
  if ((tuple = dict_find(iter, MESSAGE_KEY_CLOSE_WAS_CLEAN)) && !tuple_get_int32(tuple)) {
    prv_finish_request(manager);
    prv_add_error(manager, "Lost connection to server.");
  }
  if ((tuple = dict_find(iter, MESSAGE_KEY_CLOSE_REASON)) && tuple->value->cstring[0] != 0) {
    prv_finish_request(manager);
    prv_add_error(manager, tuple->value->cstring);
  }
  if ((tuple = dict_find(iter, MESSAGE_KEY_CHAT_DONE))) {
    conversation_complete_response(manager->conversation);
    prv_finish_request(manager);
    prv_conversation_updated(manager, false);
    return;
  }
  if (activity && manager->request_pending) {
    manager->last_activity = time(NULL);
  }
}

static void prv_handle_app_message_inbox_dropped(AppMessageResult reason, void *context) {
  ConversationManager* manager = context;
  SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Received message dropped: %d", reason);
  if (!manager->request_pending) {
    return;
  }
  // The phone retries dropped messages, and reports a terminal error itself
  // if it has to give up, so don't declare failure here. Remember it so the
  // watchdog can say the reply may be incomplete if nothing else arrives.
  manager->saw_dropped_message = true;
}

static void prv_schedule_watchdog(ConversationManager* manager) {
  if (manager->watchdog) {
    app_timer_cancel(manager->watchdog);
  }
  manager->watchdog = app_timer_register(REQUEST_WATCHDOG_INTERVAL_MS, prv_watchdog_fired, manager);
}

static void prv_watchdog_fired(void* context) {
  ConversationManager* manager = context;
  manager->watchdog = NULL;
  if (!manager->request_pending) {
    return;
  }
  bool connected = connection_service_peek_pebble_app_connection();
  bool silent_too_long = time(NULL) - manager->last_activity >= REQUEST_SILENCE_LIMIT_S;
  if (connected && !silent_too_long) {
    prv_schedule_watchdog(manager);
    return;
  }
  SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Giving up on request (connected: %d).", connected);
  ConversationEntry* last = conversation_peek(manager->conversation);
  bool had_reply = last && conversation_entry_get_type(last) == EntryTypeResponse &&
                   conversation_entry_get_response(last)->len > 0;
  prv_finish_request(manager);
  if (!connected) {
    prv_add_error(manager, had_reply ? "Lost connection to phone. The reply may be incomplete."
                                     : "Lost connection to phone.");
  } else {
    prv_add_error(manager, (had_reply || manager->saw_dropped_message)
                           ? "Phone stopped responding. The reply may be incomplete."
                           : "No response from phone.");
  }
}

static void prv_conversation_updated(ConversationManager* manager, bool new_entry) {
  if (manager->handler) {
    manager->handler(new_entry, manager->context);
  }
}

static bool prv_handle_memory_pressure(void *context) {
  SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Memory pressure detected.");
  ConversationManager* manager = context;
  if (!manager->conversation) {
    return false;
  }
  if (conversation_length(manager->conversation) <= 2) {
    return false;
  }
  ConversationEntry* victim = conversation_peek_first(manager->conversation);
  if (!victim) {
    return false;
  }
  if (conversation_entry_get_type(victim) == EntryTypeResponse &&
      !conversation_entry_get_response(victim)->complete) {
    return false;
  }
  SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Deleting oldest entry from conversation.");
  // Let the UI drop its reference to the entry before it's freed.
  if (manager->deletion_handler) {
    manager->deletion_handler(victim, manager->context);
  }
  return conversation_delete_first_entry(manager->conversation);
}
