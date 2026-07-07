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

#include <pebble-events/pebble-events.h>
#include <pebble.h>


struct ConversationManager {
  Conversation* conversation;
  EventHandle app_message_handle;
  void* context;
  ConversationManagerUpdateHandler handler;
  ConversationManagerEntryDeletedHandler deletion_handler;
};

static void prv_conversation_updated(ConversationManager* manager, bool new_entry);
static void prv_handle_app_message_outbox_sent(DictionaryIterator *iterator, void *context);
static void prv_handle_app_message_outbox_failed(DictionaryIterator *iterator, AppMessageResult reason, void *context);
static void prv_handle_app_message_inbox_received(DictionaryIterator *iterator, void *context);
static void prv_handle_app_message_inbox_dropped(AppMessageResult result, void *context);
static bool prv_handle_memory_pressure(void *context);

static ConversationManager* s_conversation_manager;

// Pebble's per-message hard limit is ~8 KB. We request 5000 bytes so that a
// single app message (one dictionary tuple) can carry a chunk with headroom,
// while the pkjs side splits agent responses into 4000-byte chunks to stay
// below this budget (see src/pkjs/lib/message_queue.js). Keeping this modest
// preserves watch heap — see prv_handle_memory_pressure for why that matters.
#define SQUIRE_APP_MESSAGE_BUFFER_SIZE 5000

void conversation_manager_init() {
  events_app_message_request_outbox_size(SQUIRE_APP_MESSAGE_BUFFER_SIZE);
  events_app_message_request_inbox_size(SQUIRE_APP_MESSAGE_BUFFER_SIZE);
}

ConversationManager* conversation_manager_create() {
  ConversationManager* manager = bmalloc(sizeof(ConversationManager));
  manager->conversation = conversation_create();
  manager->handler = NULL;
  manager->deletion_handler = NULL;
  manager->context = NULL;
  manager->app_message_handle = events_app_message_subscribe_handlers((EventAppMessageHandlers){
      .sent = prv_handle_app_message_outbox_sent,
      .failed = prv_handle_app_message_outbox_failed,
      .received = prv_handle_app_message_inbox_received,
      // We don't handle this elegantly enough for it to make sense here.
      // .dropped = prv_handle_app_message_inbox_dropped,
  }, manager);
  s_conversation_manager = manager;
  memory_pressure_register_callback(prv_handle_memory_pressure, 1, manager);
  return manager;
}

void conversation_manager_destroy(ConversationManager* manager) {
  conversation_destroy(manager->conversation);
  events_app_message_unsubscribe(manager->app_message_handle);
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

void conversation_manager_set_handler(ConversationManager* manager, ConversationManagerUpdateHandler handler, void* context) {
  manager->handler = handler;
  manager->context = context;
}

void conversation_manager_set_deletion_handler(ConversationManager* manager, ConversationManagerEntryDeletedHandler handler) {
  manager->deletion_handler = handler;
}

void conversation_manager_add_input(ConversationManager* manager, const char* input) {
  DictionaryIterator *iter;
  AppMessageResult result = app_message_outbox_begin(&iter);
  conversation_add_prompt(manager->conversation, input);
  prv_conversation_updated(manager, true);
  if (result != APP_MSG_OK) {
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Preparing outbox failed: %d.", result);
    conversation_add_error(manager->conversation, "Sending to service failed.");
    prv_conversation_updated(manager, true);
    return;
  }

  // The Android Pebble app has a fun bug where any double-quotes in a
  // message will cause it to be dropped, this is a bodge workaround.
  char* bridge_bodge = bmalloc(strlen(input) + 1);
  strcpy(bridge_bodge, input);
  strings_fix_android_bridge_bodge(bridge_bodge);
  dict_write_cstring(iter, MESSAGE_KEY_PROMPT, bridge_bodge);
  free(bridge_bodge);

  const char* thread_id = conversation_get_thread_id(manager->conversation);
  if (thread_id[0] != 0) {
    SQUIRE_LOG(APP_LOG_LEVEL_INFO, "Continuing previous conversation %s.", thread_id);
    dict_write_cstring(iter, MESSAGE_KEY_THREAD_ID, thread_id);
  }
  result = app_message_outbox_send();
  if (result != APP_MSG_OK) {
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Sending message failed: %d.", result);
    conversation_add_error(manager->conversation, "Sending to service failed.");
    prv_conversation_updated(manager, true);
    return;
  }
}

void conversation_manager_add_action(ConversationManager* manager, ConversationAction* action) {
  SQUIRE_LOG(APP_LOG_LEVEL_DEBUG, "Adding action to conversation.");
  conversation_add_action(manager->conversation, action);
  prv_conversation_updated(manager, true);
}

static void prv_handle_app_message_outbox_sent(DictionaryIterator *iterator, void *context) {
  SQUIRE_LOG(APP_LOG_LEVEL_INFO, "Sent message successfully.");
}

static void prv_handle_app_message_outbox_failed(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Sending message failed: %d", reason);
  ConversationManager* manager = context;
  conversation_add_error(manager->conversation, "Sending to service failed.");
  prv_conversation_updated(manager, true);
}

static void prv_handle_app_message_inbox_received(DictionaryIterator *iter, void *context) {
  ConversationManager* manager = context;
  for (Tuple *tuple = dict_read_first(iter); tuple; tuple = dict_read_next(iter)) {
    if (tuple->key == MESSAGE_KEY_CHAT) {
      bool added_entry = conversation_add_response_fragment(manager->conversation, tuple->value->cstring);
      prv_conversation_updated(manager, added_entry);
    } else if (tuple->key == MESSAGE_KEY_FUNCTION) {
      SQUIRE_LOG(APP_LOG_LEVEL_INFO, "Received function: \"%s\".", tuple->value->cstring);
      conversation_complete_response(manager->conversation);
      prv_conversation_updated(manager, false);
      conversation_add_thought(manager->conversation, tuple->value->cstring);
      prv_conversation_updated(manager, true);
    } else if (tuple->key == MESSAGE_KEY_CHAT_DONE) {
      conversation_complete_response(manager->conversation);
      prv_conversation_updated(manager, false);
    } else if (tuple->key == MESSAGE_KEY_THREAD_ID) {
      conversation_set_thread_id(manager->conversation, tuple->value->cstring);
      history_push_thread_id(tuple->value->cstring);
    } else if (tuple->key == MESSAGE_KEY_CLOSE_WAS_CLEAN) {
      if (!tuple->value->int16) {
        conversation_complete_response(manager->conversation);
        conversation_add_error(manager->conversation, "Lost connection to server.");
        prv_conversation_updated(manager, true);
      }
    } else if (tuple->key == MESSAGE_KEY_CLOSE_REASON) {
      if (tuple->value->cstring[0] != 0) {
        conversation_complete_response(manager->conversation);
        conversation_add_error(manager->conversation, tuple->value->cstring);
        prv_conversation_updated(manager, true);
      }
    } else if (tuple->key == MESSAGE_KEY_ACTION_SETTINGS_UPDATED) {
      char *sentence = bmalloc(strlen(tuple->value->cstring) + 1);
      strcpy(sentence, tuple->value->cstring);
      ConversationAction action = {
        .type = ConversationActionTypeGenericSentence,
        .action = {
          .generic_sentence = {
            .sentence = sentence,
          }
        },
      };
      conversation_manager_add_action(manager, &action);
    } else if (tuple->key == MESSAGE_KEY_TYPING) {
      bool added_entry = conversation_add_response_fragment(manager->conversation, "");
      prv_conversation_updated(manager, added_entry);
    } else if (tuple->key == MESSAGE_KEY_WARNING) {
      conversation_complete_response(manager->conversation);
      prv_conversation_updated(manager, false);
      conversation_add_error(manager->conversation, tuple->value->cstring);
      prv_conversation_updated(manager, true);
    }
  }
}

static void prv_handle_app_message_inbox_dropped(AppMessageResult reason, void *context) {
  SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Received message dropped: %d", reason);
  ConversationManager* manager = context;
  conversation_add_error(manager->conversation, "Response from service lost.");
  prv_conversation_updated(manager, true);
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
  SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Deleting oldest entry from conversation.");
  if (manager->deletion_handler) {
    manager->deletion_handler(0, manager->context);
  }
  conversation_delete_first_entry(manager->conversation);
  return true;
}