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

#include "conversation.h"
#include "../util/memory/malloc.h"
#include "../util/logging.h"
#include "../features.h"

#include <string.h>

#define CONVERSATION_INITIAL_CAPACITY 16
#define CONVERSATION_CAPACITY_STEP 8

struct ConversationEntry {
  EntryType type;
  union {
    ConversationAction *action;
    ConversationPrompt *prompt;
    ConversationResponse *response;
    ConversationThought *thought;
    ConversationError *error;
  } content;
};

// entries is a table of individually allocated entries, so ConversationEntry
// pointers handed out to the UI stay valid when the table grows. Slots below
// deleted_entries have been freed (NULL). Discarded thoughts stay in the
// table as EntryTypeDeleted until they reach the front.
struct Conversation {
  ConversationEntry** entries;
  int deleted_entries;
  int nulled_entries;
  int entry_count;
  int entry_allocated;
  char thread_id[37];
};

static void prv_destroy_entry_content(ConversationEntry *entry);
static bool prv_append_entry(Conversation* conversation, ConversationEntry* entry);
static ConversationEntry* prv_new_entry(EntryType type);
static char* prv_strdup(const char* text);
const char* prv_type_to_string(EntryType type);

Conversation* conversation_create() {
  Conversation *conversation = bmalloc(sizeof(Conversation));
  if (!conversation) {
    return NULL;
  }
  memset(conversation, 0, sizeof(Conversation));
  conversation->entries = bmalloc(sizeof(ConversationEntry*) * CONVERSATION_INITIAL_CAPACITY);
  if (!conversation->entries) {
    free(conversation);
    return NULL;
  }
  conversation->entry_allocated = CONVERSATION_INITIAL_CAPACITY;
  return conversation;
}

void conversation_destroy(Conversation* conversation) {
  if (!conversation) {
    return;
  }
  for (int i = conversation->deleted_entries; i < conversation->entry_count; ++i) {
    if (conversation->entries[i]) {
      prv_destroy_entry_content(conversation->entries[i]);
      free(conversation->entries[i]);
    }
  }
  free(conversation->entries);
  free(conversation);
}

static void prv_destroy_entry_content(ConversationEntry *entry) {
  switch (entry->type) {
    case EntryTypeDeleted:
      // Nothing to do here.
      break;
    case EntryTypePrompt:
      free(entry->content.prompt->prompt);
      free(entry->content.prompt);
      break;
    case EntryTypeResponse:
      free(entry->content.response->response);
      free(entry->content.response);
      break;
    case EntryTypeThought:
      free(entry->content.thought->thought);
      free(entry->content.thought);
      break;
    case EntryTypeError:
      free(entry->content.error->message);
      free(entry->content.error);
      break;
    case EntryTypeAction:
      switch (entry->content.action->type) {
        case ConversationActionTypeGenericSentence:
          free(entry->content.action->action.generic_sentence.sentence);
          break;
        case ConversationActionTypeUpdateChecklist:
          break;
      }
      free(entry->content.action);
      break;
  }
  entry->type = EntryTypeDeleted;
}

static char* prv_strdup(const char* text) {
  size_t len = strlen(text);
  char* copy = bmalloc(len + 1);
  if (copy) {
    memcpy(copy, text, len + 1);
  }
  return copy;
}

static ConversationEntry* prv_new_entry(EntryType type) {
  ConversationEntry* entry = bmalloc(sizeof(ConversationEntry));
  if (entry) {
    memset(entry, 0, sizeof(ConversationEntry));
    entry->type = type;
  }
  return entry;
}

// Links a fully built entry into the table, growing the table if needed.
// Content is always allocated before calling this, so a memory pressure
// callback that runs during the growth allocation (and deletes entries from
// the front) never sees a half-built entry.
static bool prv_append_entry(Conversation* conversation, ConversationEntry* entry) {
  if (conversation->entry_count >= conversation->entry_allocated && conversation->deleted_entries > 0) {
    // Reclaim slots freed at the front before reaching for the heap. Entry
    // pointers are unaffected; only indices shift.
    int shift = conversation->deleted_entries;
    memmove(conversation->entries, conversation->entries + shift,
            sizeof(ConversationEntry*) * (conversation->entry_count - shift));
    conversation->entry_count -= shift;
    conversation->deleted_entries = 0;
  }
  if (conversation->entry_count >= conversation->entry_allocated) {
    int new_allocated = conversation->entry_allocated + CONVERSATION_CAPACITY_STEP;
    ConversationEntry **new_entries = bmalloc(sizeof(ConversationEntry*) * new_allocated);
    if (!new_entries) {
      SQUIRE_LOG(APP_LOG_LEVEL_ERROR, "Couldn't grow conversation to %d entries.", new_allocated);
      return false;
    }
    // Read the table only now: bmalloc may have run memory pressure handlers
    // that deleted (NULLed) entries at the front.
    memcpy(new_entries, conversation->entries, sizeof(ConversationEntry*) * conversation->entry_count);
    free(conversation->entries);
    conversation->entries = new_entries;
    conversation->entry_allocated = new_allocated;
  }
  conversation->entries[conversation->entry_count++] = entry;
  return true;
}

bool conversation_add_prompt(Conversation* conversation, const char* prompt_text) {
  ConversationEntry *entry = prv_new_entry(EntryTypePrompt);
  ConversationPrompt *prompt = bmalloc(sizeof(ConversationPrompt));
  char *text = prv_strdup(prompt_text);
  if (!entry || !prompt || !text) {
    goto fail;
  }
  prompt->prompt = text;
  entry->content.prompt = prompt;
  if (!prv_append_entry(conversation, entry)) {
    goto fail;
  }
  return true;
fail:
  free(text);
  free(prompt);
  free(entry);
  return false;
}

static bool prv_add_response(Conversation* conversation, const char* response_text, bool complete) {
  ConversationEntry *entry = prv_new_entry(EntryTypeResponse);
  ConversationResponse *response = bmalloc(sizeof(ConversationResponse));
  char *text = prv_strdup(response_text);
  if (!entry || !response || !text) {
    goto fail;
  }
  response->complete = complete;
  response->len = strlen(text);
  response->allocated = response->len + 1;
  response->response = text;
  entry->content.response = response;
  if (!prv_append_entry(conversation, entry)) {
    goto fail;
  }
  return true;
fail:
  free(text);
  free(response);
  free(entry);
  return false;
}

bool conversation_add_response(Conversation* conversation, const char* response_text) {
  return prv_add_response(conversation, response_text, true);
}

bool conversation_start_response(Conversation *conversation) {
  return prv_add_response(conversation, "", false);
}

// Appends fragment to response. On failure the response keeps its previous
// buffer and text untouched.
static bool prv_append_to_response(ConversationResponse *response, const char* fragment) {
  size_t len = strlen(fragment);
  if (len == 0) {
    return true;
  }
  size_t needed = response->len + len + 1;
  if (needed > response->allocated) {
    // Grow to exactly what's needed plus a little slack: fragments are large
    // (whole Telegram messages), and doubling would waste scarce heap.
    size_t new_allocated = needed + 32;
    char* new_resp = bmalloc(new_allocated);
    if (!new_resp) {
      SQUIRE_LOG(APP_LOG_LEVEL_ERROR, "Couldn't expand response buffer to %d bytes.", (int)new_allocated);
      return false;
    }
    memcpy(new_resp, response->response, response->len + 1);
    free(response->response);
    response->response = new_resp;
    response->allocated = new_allocated;
  }
  memcpy(response->response + response->len, fragment, len + 1);
  response->len += len;
  return true;
}

static ConversationResponse* prv_find_last_open_response(Conversation* conversation) {
  for (int i = conversation->entry_count - 1; i >= conversation->deleted_entries; --i) {
    ConversationEntry* entry = conversation->entries[i];
    if (!entry || entry->type != EntryTypeResponse) {
      continue;
    }
    if (!entry->content.response->complete) {
      return entry->content.response;
    }
  }
  return NULL;
}

ConversationAppendResult conversation_add_response_fragment(Conversation* conversation, const char* fragment) {
  ConversationResponse* response = prv_find_last_open_response(conversation);
  ConversationAppendResult result = ConversationAppendOk;
  if (response == NULL) {
    if (!conversation_start_response(conversation)) {
      return ConversationAppendFailed;
    }
    result = ConversationAppendNewEntry;
    response = prv_find_last_open_response(conversation);
  }
  if (!prv_append_to_response(response, fragment)) {
    // If we just created the entry it stays (empty and open); the caller
    // reports the failure and completes it.
    return ConversationAppendFailed;
  }
  return result;
}

bool conversation_reset_response(Conversation *conversation) {
  ConversationResponse* response = prv_find_last_open_response(conversation);
  if (response == NULL) {
    return false;
  }
  response->len = 0;
  response->response[0] = '\0';
  return true;
}

void conversation_complete_response(Conversation *conversation) {
  ConversationResponse* response = prv_find_last_open_response(conversation);
  if (response == NULL) {
    SQUIRE_LOG(APP_LOG_LEVEL_DEBUG, "Trying to complete a response, but couldn't find any.");
    return;
  }
  response->complete = true;
}

bool conversation_add_thought(Conversation* conversation, char* thought_text) {
  ConversationEntry* entry = prv_new_entry(EntryTypeThought);
  ConversationThought* thought = bmalloc(sizeof(ConversationThought));
  char* text = prv_strdup(thought_text);
  if (!entry || !thought || !text) {
    goto fail;
  }
  thought->thought = text;
  entry->content.thought = thought;
  if (!prv_append_entry(conversation, entry)) {
    goto fail;
  }
  return true;
fail:
  free(text);
  free(thought);
  free(entry);
  return false;
}

bool conversation_add_action(Conversation* conversation, ConversationAction* action) {
  ConversationEntry* entry = prv_new_entry(EntryTypeAction);
  ConversationAction* new_action = bmalloc(sizeof(ConversationAction));
  if (!entry || !new_action) {
    goto fail;
  }
  memcpy(new_action, action, sizeof(ConversationAction));
  entry->content.action = new_action;
  if (!prv_append_entry(conversation, entry)) {
    goto fail;
  }
  return true;
fail:
  free(new_action);
  free(entry);
  return false;
}

bool conversation_add_error(Conversation* conversation, const char* error_text) {
  ConversationEntry* entry = prv_new_entry(EntryTypeError);
  ConversationError* error = bmalloc(sizeof(ConversationError));
  char* text = prv_strdup(error_text);
  if (!entry || !error || !text) {
    goto fail;
  }
  error->message = text;
  entry->content.error = error;
  if (!prv_append_entry(conversation, entry)) {
    goto fail;
  }
  return true;
fail:
  free(text);
  free(error);
  free(entry);
  return false;
}

// Drops discarded (EntryTypeDeleted) entries sitting at the front.
static void prv_skip_deleted_front(Conversation* conversation) {
  while (conversation->deleted_entries < conversation->entry_count) {
    ConversationEntry* entry = conversation->entries[conversation->deleted_entries];
    if (entry && entry->type != EntryTypeDeleted) {
      return;
    }
    if (entry) {
      free(entry);
      conversation->nulled_entries--;
    }
    conversation->entries[conversation->deleted_entries] = NULL;
    conversation->deleted_entries++;
  }
}

ConversationEntry* conversation_peek_first(Conversation* conversation) {
  prv_skip_deleted_front(conversation);
  if (conversation->deleted_entries >= conversation->entry_count) {
    return NULL;
  }
  return conversation->entries[conversation->deleted_entries];
}

bool conversation_delete_first_entry(Conversation* conversation) {
  ConversationEntry* entry = conversation_peek_first(conversation);
  if (!entry) {
    return false;
  }
  if (entry->type == EntryTypeResponse && !entry->content.response->complete) {
    // Still being written to; freeing it would lose the reply in progress.
    return false;
  }
  prv_destroy_entry_content(entry);
  free(entry);
  conversation->entries[conversation->deleted_entries] = NULL;
  conversation->deleted_entries++;
  return true;
}

void conversation_delete_last_thought(Conversation* conversation) {
  SQUIRE_LOG(APP_LOG_LEVEL_DEBUG, "Deleting last thought");
  for (int i = conversation->entry_count - 2; i >= conversation->deleted_entries; --i) {
    ConversationEntry* entry = conversation->entries[i];
    if (entry && entry->type == EntryTypeThought) {
      SQUIRE_LOG(APP_LOG_LEVEL_DEBUG, "Deleting thought %d", i);
      prv_destroy_entry_content(entry);
      conversation->nulled_entries++;
      return;
    }
  }
}

int conversation_first_index(Conversation* conversation) {
  return conversation->deleted_entries;
}

int conversation_end_index(Conversation* conversation) {
  return conversation->entry_count;
}

ConversationEntry* conversation_entry_at_index(Conversation* conversation, int index) {
  if (index < conversation->deleted_entries || index >= conversation->entry_count) {
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Caller asked for entry %d, but only %d-%d exist.", index, conversation->deleted_entries, conversation->entry_count);
    return NULL;
  }
  return conversation->entries[index];
}

ConversationEntry* conversation_peek(Conversation* conversation) {
  if (conversation->entry_count == conversation->deleted_entries) {
    SQUIRE_LOG(APP_LOG_LEVEL_DEBUG, "Tried to peek at conversation, but no entries yet.");
    return NULL;
  }
  return conversation->entries[conversation->entry_count-1];
}

ConversationEntry* conversation_get_last_of_type(Conversation* conversation, EntryType type) {
  for (int i = conversation->entry_count - 1; i >= conversation->deleted_entries; --i) {
    ConversationEntry* entry = conversation->entries[i];
    if (entry && entry->type == type) {
      return entry;
    }
  }
  return NULL;
}

const char* prv_type_to_string(EntryType type) {
  switch(type) {
    case EntryTypeDeleted:
      return "EntryTypeDeleted";
    case EntryTypePrompt:
      return "EntryTypePrompt";
    case EntryTypeResponse:
      return "EntryTypeResponse";  
    case EntryTypeThought:
      return "EntryTypeThought";
    case EntryTypeAction:
      return "EntryTypeAction";
    case EntryTypeError:
      return "EntryTypeError";
  }
  return "Unknown";
}

ConversationPrompt* conversation_entry_get_prompt(ConversationEntry* entry) {
  if (entry->type != EntryTypePrompt) {
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Asked for prompt %p, but it's actually a %s.", entry, prv_type_to_string(entry->type));
    return NULL;
  }
  return entry->content.prompt;
}

ConversationResponse* conversation_entry_get_response(ConversationEntry* entry) {
  if (entry->type != EntryTypeResponse) {
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Asked for response %p, but it's actually a %s.", entry, prv_type_to_string(entry->type));
    return NULL;
  }
  return entry->content.response;
}

ConversationThought* conversation_entry_get_thought(ConversationEntry* entry) {
  if (entry->type != EntryTypeThought) {
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Asked for thought %p, but it's actually a %s.", entry, prv_type_to_string(entry->type));
    return NULL;
  }
  return entry->content.thought;
}

ConversationError* conversation_entry_get_error(ConversationEntry* entry) {
  if (entry->type != EntryTypeError) {
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Asked for error %p, but it's actually a %s.", entry, prv_type_to_string(entry->type));
    return NULL;
  }
  return entry->content.error;
}

ConversationAction* conversation_entry_get_action(ConversationEntry* action) {
  if (action->type != EntryTypeAction) {
    SQUIRE_LOG(APP_LOG_LEVEL_WARNING, "Asked for action %p, but it's actually a %s.", action, prv_type_to_string(action->type));
    return NULL;
  }
  return action->content.action;
}

EntryType conversation_entry_get_type(ConversationEntry* entry) {
  return entry->type;
}

void conversation_set_thread_id(Conversation* conversation, const char* thread_id) {
  SQUIRE_LOG(APP_LOG_LEVEL_INFO, "Thread ID updated: %s", thread_id);
  strncpy(conversation->thread_id, thread_id, 36);
  conversation->thread_id[36] = '\0';  // Ensure null termination
}

const char* conversation_get_thread_id(Conversation* conversation) {
  return conversation->thread_id;
}

int conversation_length(Conversation* conversation) {
  return conversation->entry_count - conversation->deleted_entries - conversation->nulled_entries;
}

bool conversation_is_idle(Conversation* conversation) {
  ConversationEntry* entry = conversation_peek(conversation);
  if (entry == NULL) {
    return true;
  }
  if (entry->type == EntryTypeError) {
    return true;
  }
  if (entry->type == EntryTypeResponse) {
    return entry->content.response->complete;
  }
  return false;
}

static bool prv_entry_type_is_assistant(ConversationEntry* entry) {
  switch (entry->type) {
    case EntryTypeDeleted:
    case EntryTypePrompt:
    case EntryTypeError:
    case EntryTypeThought:
    case EntryTypeAction:
      return false;
    case EntryTypeResponse:
      return true;
  }
  return false;
}

bool conversation_assistant_just_started(Conversation* conversation) {
  ConversationEntry *entry = conversation_peek(conversation);
  if (entry == NULL) {
    return false;
  }
  if (!prv_entry_type_is_assistant(entry)) {
    return false;
  }
  if (conversation->entry_count - 1 <= conversation->deleted_entries) {
    return true;
  }
  ConversationEntry *previous = conversation->entries[conversation->entry_count - 2];
  return !previous || !prv_entry_type_is_assistant(previous);
}
