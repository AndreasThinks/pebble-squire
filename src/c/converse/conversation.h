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

#ifndef CONVERSATION_H
#define CONVERSATION_H

#include <pebble.h>
#include "../features.h"

typedef struct Conversation Conversation;
typedef struct ConversationEntry ConversationEntry;

typedef enum {
  ConversationActionTypeUpdateChecklist,
  ConversationActionTypeGenericSentence,
} ConversationActionType;

typedef struct {
} ConversationActionPokeHomeAssistant;

typedef struct {
  char *sentence;
} ConversationActionGenericSentence;

typedef struct {
  ConversationActionType type;
  union {
    ConversationActionPokeHomeAssistant poke_hass;
    ConversationActionGenericSentence generic_sentence;
  } action;
} ConversationAction;

typedef struct {
  char *prompt;
} ConversationPrompt;

typedef struct {
  char *response;
  size_t len;
  size_t allocated;
  bool complete;
} ConversationResponse;

typedef struct {
  char *thought;
} ConversationThought;

typedef struct {
  char *message;
} ConversationError;

typedef enum {
  EntryTypeDeleted,
  EntryTypePrompt,
  EntryTypeResponse,
  EntryTypeThought,
  EntryTypeAction,
  EntryTypeError,
} EntryType;

typedef enum {
  // The fragment could not be stored (out of memory). The response, if one
  // exists, keeps whatever text it had before; nothing was half-appended.
  ConversationAppendFailed = 0,
  // The fragment was appended to an existing open response.
  ConversationAppendOk,
  // A new open response entry was created to hold the fragment.
  ConversationAppendNewEntry,
} ConversationAppendResult;

// All constructors and mutators below allocate with bmalloc and may fail when
// the heap is exhausted. They never leave a half-built entry behind: on
// failure they return NULL/false and the conversation is unchanged.
//
// ConversationEntry pointers are stable for the lifetime of the entry (entries
// are allocated individually; only the pointer table is ever reallocated), so
// UI layers may keep them until the entry is deleted.
Conversation* conversation_create();
void conversation_destroy(Conversation* conversation);
bool conversation_add_prompt(Conversation* conversation, const char* prompt);
bool conversation_add_response(Conversation* conversation, const char* response);
bool conversation_start_response(Conversation *conversation);
ConversationAppendResult conversation_add_response_fragment(Conversation *conversation, const char* fragment);
// Clears the text of the last open (incomplete) response without freeing its
// buffer. Returns false if there is no open response.
bool conversation_reset_response(Conversation *conversation);
void conversation_complete_response(Conversation *conversation);
bool conversation_add_thought(Conversation* conversation, char* thought);
// On success the conversation takes ownership of any heap pointers inside
// *action; on failure ownership stays with the caller.
bool conversation_add_action(Conversation* conversation, ConversationAction* action);
bool conversation_add_error(Conversation* conversation, const char* error_text);
void conversation_set_thread_id(Conversation* conversation, const char* thread_id);
const char* conversation_get_thread_id(Conversation* conversation);
int conversation_length(Conversation* conversation);
bool conversation_is_idle(Conversation* conversation);
bool conversation_assistant_just_started(Conversation* conversation);
// Index range of entries that have not been deleted from the front:
// [conversation_first_index, conversation_end_index). Entries in that range
// may still be EntryTypeDeleted (discarded thoughts).
int conversation_first_index(Conversation* conversation);
int conversation_end_index(Conversation* conversation);
ConversationEntry* conversation_entry_at_index(Conversation* conversation, int index);
ConversationEntry* conversation_peek(Conversation* conversation);
// The entry conversation_delete_first_entry would delete next, or NULL.
ConversationEntry* conversation_peek_first(Conversation* conversation);
ConversationEntry* conversation_get_last_of_type(Conversation* conversation, EntryType type);
EntryType conversation_entry_get_type(ConversationEntry* entry);
// Deletes the oldest live entry. Returns false (deleting nothing) if there is
// no such entry or it is a response that is still being received.
bool conversation_delete_first_entry(Conversation* conversation);
void conversation_delete_last_thought(Conversation* conversation);

ConversationPrompt* conversation_entry_get_prompt(ConversationEntry* entry);
ConversationResponse* conversation_entry_get_response(ConversationEntry* response);
ConversationThought* conversation_entry_get_thought(ConversationEntry* thought);
ConversationError* conversation_entry_get_error(ConversationEntry* response);
ConversationAction* conversation_entry_get_action(ConversationEntry* action);

#endif
