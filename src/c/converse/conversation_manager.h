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

#ifndef CONVERSATION_MANAGER_H
#define CONVERSATION_MANAGER_H

#include <pebble.h>
#include "conversation.h"

typedef struct ConversationManager ConversationManager;
typedef void (*ConversationManagerUpdateHandler)(bool entry_added, void* context);
// Called just before `entry` is deleted to relieve memory pressure, so the UI
// can drop anything that references it.
typedef void (*ConversationManagerEntryDeletedHandler)(ConversationEntry* entry, void* context);

void conversation_manager_init();
// Returns NULL if there isn't enough memory to create the manager.
ConversationManager* conversation_manager_create();
ConversationManager* conversation_manager_get_current();
// Unsubscribes from app messages and memory pressure, cancels timers and frees
// the manager and its conversation. Safe to call with NULL.
void conversation_manager_destroy(ConversationManager* manager);
void conversation_manager_set_handler(ConversationManager* manager, ConversationManagerUpdateHandler handler, void* context);
void conversation_manager_set_deletion_handler(ConversationManager* manager, ConversationManagerEntryDeletedHandler handler);
void conversation_manager_add_input(ConversationManager* manager, const char* input);
void conversation_manager_add_action(ConversationManager* manager, ConversationAction* action);
Conversation* conversation_manager_get_conversation(ConversationManager* manager);
// True while a prompt has been sent and its reply hasn't finished (CHAT_DONE,
// a terminal error, or the watchdog).
bool conversation_manager_is_request_pending(ConversationManager* manager);

#endif
