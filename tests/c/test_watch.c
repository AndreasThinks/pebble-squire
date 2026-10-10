// Host regression tests for the watch-side conversation, memory pressure and
// history code. Built with AddressSanitizer/UBSan (see Makefile) so
// use-after-free and NULL dereferences fail loudly.
#include <pebble.h>
#include "test_support.h"

#include "converse/conversation.h"
#include "converse/conversation_manager.h"
#include "converse/history.h"
#include "util/memory/malloc.h"
#include "util/memory/pressure.h"

static int s_failures = 0;
static int s_checks = 0;
static const char *s_current = "";

#define CHECK(cond) do { \
  ++s_checks; \
  if (!(cond)) { \
    ++s_failures; \
    fprintf(stderr, "FAIL %s:%d [%s]: %s\n", __FILE__, __LINE__, s_current, #cond); \
  } \
} while (0)

#define RUN(fn) do { \
  s_current = #fn; \
  test_alloc_reset(); \
  memory_pressure_init(); \
  history_init(); \
  test_set_connected(true); \
  test_set_outbox_begin_result(APP_MSG_OK); \
  long before = test_live_allocations(); \
  fn(); \
  CHECK(test_live_allocations() == before); \
  CHECK(test_active_timers() == 0); \
  CHECK(test_active_subscriptions() == 0); \
  printf("ok - %s\n", #fn); \
} while (0)

// ---- helpers ---------------------------------------------------------------

typedef struct {
  int updates;
  int added;
  ConversationEntry *deleted[8];
  int deleted_count;
} Observer;

static void prv_update(bool entry_added, void *context) {
  Observer *o = context;
  o->updates++;
  o->added += entry_added;
}

static void prv_deleted(ConversationEntry *entry, void *context) {
  Observer *o = context;
  if (o->deleted_count < 8) {
    o->deleted[o->deleted_count] = entry;
  }
  o->deleted_count++;
}

static int32_t prv_sent_request_id(void) {
  Tuple *t = dict_find(test_outbox(), MESSAGE_KEY_REQUEST_ID);
  return t ? t->value->int32 : 0;
}

static void prv_deliver(int32_t request_id, uint32_t key, const char *text) {
  DictionaryIterator iter = {0};
  if (request_id) {
    dict_write_int32(&iter, MESSAGE_KEY_REQUEST_ID, request_id);
  }
  if (text) {
    dict_write_cstring(&iter, key, text);
  } else {
    dict_write_int32(&iter, key, 1);
  }
  test_deliver_inbox(&iter);
  test_dict_clear(&iter);
}

static void prv_deliver_seq(int32_t seq, int32_t request_id, uint32_t key, const char *text) {
  DictionaryIterator iter = {0};
  dict_write_int32(&iter, MESSAGE_KEY_SEQ, seq);
  dict_write_int32(&iter, MESSAGE_KEY_REQUEST_ID, request_id);
  if (text) {
    dict_write_cstring(&iter, key, text);
  } else {
    dict_write_int32(&iter, key, 1);
  }
  test_deliver_inbox(&iter);
  test_dict_clear(&iter);
}

static const char *prv_last_text(Conversation *conv) {
  ConversationEntry *e = conversation_peek(conv);
  if (!e) return NULL;
  switch (conversation_entry_get_type(e)) {
    case EntryTypeResponse: return conversation_entry_get_response(e)->response;
    case EntryTypePrompt: return conversation_entry_get_prompt(e)->prompt;
    case EntryTypeError: return conversation_entry_get_error(e)->message;
    default: return NULL;
  }
}

// ---- memory pressure lifetime ---------------------------------------------

static void test_destroy_unregisters_memory_pressure_callback(void) {
  ConversationManager *m = conversation_manager_create();
  CHECK(m != NULL);
  CHECK(memory_pressure_callback_count() == 1);
  conversation_manager_destroy(m);
  CHECK(memory_pressure_callback_count() == 0);
  // With the callback still registered this would read the freed manager.
  CHECK(memory_pressure_try_free() == false);
}

static void test_unregister_matches_context(void) {
  ConversationManager *a = conversation_manager_create();
  ConversationManager *b = conversation_manager_create();
  Observer ob = {0};
  conversation_manager_set_handler(b, prv_update, &ob);
  conversation_manager_set_deletion_handler(b, prv_deleted);
  Conversation *conv_b = conversation_manager_get_conversation(b);
  conversation_add_prompt(conv_b, "one");
  conversation_add_response(conv_b, "two");
  conversation_add_prompt(conv_b, "three");
  // Destroying the first manager must remove its own registration only.
  conversation_manager_destroy(a);
  CHECK(memory_pressure_callback_count() == 1);
  CHECK(memory_pressure_try_free() == true);
  CHECK(ob.deleted_count == 1);
  CHECK(conversation_length(conv_b) == 2);
  conversation_manager_destroy(b);
}

static bool prv_noop_handler(void *context) { return false; }

static void test_pressure_registration_never_allocates(void) {
  test_fail_allocations_after(0);
  int dummy;
  CHECK(memory_pressure_register_callback(prv_noop_handler, 0, &dummy));
  test_allow_allocations();
  memory_pressure_unregister_callback(prv_noop_handler, &dummy);
  CHECK(memory_pressure_callback_count() == 0);
}

static int s_reentry_depth = 0;
static bool prv_allocating_handler(void *context) {
  (void)context;
  s_reentry_depth++;
  // The handler itself runs out of memory; this must not recurse forever.
  void *p = bmalloc(16);
  free(p);
  return false;
}

static void test_try_free_is_not_reentrant(void) {
  int ctx;
  memory_pressure_register_callback(prv_allocating_handler, 0, &ctx);
  s_reentry_depth = 0;
  test_fail_allocations_after(0);
  CHECK(bmalloc(32) == NULL);
  test_allow_allocations();
  CHECK(s_reentry_depth == 1);
  memory_pressure_unregister_callback(prv_allocating_handler, &ctx);
}

static void test_bmalloc_fails_fast_without_sleeping(void) {
  test_fail_allocations_after(0);
  CHECK(bmalloc(100) == NULL);
  test_allow_allocations();
  CHECK(test_sleep_calls() == 0);
}

// ---- conversation OOM safety ----------------------------------------------

static void test_conversation_create_survives_every_allocation_failure(void) {
  for (int n = 0; n < 3; ++n) {
    test_fail_allocations_after(n);
    Conversation *c = conversation_create();
    test_allow_allocations();
    if (c) {
      conversation_destroy(c);
    }
  }
  for (int n = 0; n < 8; ++n) {
    test_fail_allocations_after(n);
    ConversationManager *m = conversation_manager_create();
    test_allow_allocations();
    conversation_manager_destroy(m);
    CHECK(memory_pressure_callback_count() == 0);
  }
}

static void test_add_functions_fail_cleanly(void) {
  Conversation *c = conversation_create();
  conversation_add_prompt(c, "hello");
  int before = conversation_length(c);
  for (int n = 0; n < 3; ++n) {
    test_fail_allocations_after(n);
    CHECK(!conversation_add_prompt(c, "p"));
    CHECK(!conversation_add_response(c, "r"));
    CHECK(!conversation_add_error(c, "e"));
    CHECK(!conversation_add_thought(c, "t"));
    test_allow_allocations();
  }
  CHECK(conversation_length(c) == before);
  CHECK(strcmp(prv_last_text(c), "hello") == 0);
  conversation_destroy(c);
}

static void test_fragment_failure_keeps_existing_text(void) {
  Conversation *c = conversation_create();
  CHECK(conversation_add_response_fragment(c, "Hello") == ConversationAppendNewEntry);
  test_fail_allocations_after(0);
  char big[200];
  memset(big, 'x', sizeof(big) - 1);
  big[sizeof(big) - 1] = 0;
  CHECK(conversation_add_response_fragment(c, big) == ConversationAppendFailed);
  test_allow_allocations();
  CHECK(strcmp(prv_last_text(c), "Hello") == 0);
  CHECK(conversation_add_response_fragment(c, " world") == ConversationAppendOk);
  CHECK(strcmp(prv_last_text(c), "Hello world") == 0);
  conversation_destroy(c);
}

static void test_fragment_failure_with_no_open_response(void) {
  Conversation *c = conversation_create();
  test_fail_allocations_after(0);
  CHECK(conversation_add_response_fragment(c, "abc") == ConversationAppendFailed);
  test_allow_allocations();
  CHECK(conversation_peek(c) == NULL);
  conversation_destroy(c);
}

static void test_entry_pointers_survive_table_growth(void) {
  Conversation *c = conversation_create();
  conversation_add_prompt(c, "first");
  ConversationEntry *first = conversation_peek(c);
  for (int i = 0; i < 100; ++i) {
    CHECK(conversation_add_response(c, "filler"));
  }
  // The UI keeps entry pointers in its segment layers. With the old
  // array-of-structs table these pointed into freed memory after growth.
  CHECK(conversation_entry_get_type(first) == EntryTypePrompt);
  CHECK(strcmp(conversation_entry_get_prompt(first)->prompt, "first") == 0);
  conversation_destroy(c);
}

static void test_table_growth_failure_keeps_old_entries(void) {
  Conversation *c = conversation_create();
  int added = 0;
  while (added < 16) {
    conversation_add_prompt(c, "x");
    added++;
  }
  // Next add needs a bigger table; let the entry allocations succeed but
  // the table allocation fail.
  test_fail_allocations_after(3);
  CHECK(!conversation_add_prompt(c, "overflow"));
  test_allow_allocations();
  CHECK(conversation_length(c) == 16);
  CHECK(conversation_add_prompt(c, "fits now"));
  CHECK(strcmp(prv_last_text(c), "fits now") == 0);
  conversation_destroy(c);
}

static void test_delete_first_entry_never_frees_open_response(void) {
  Conversation *c = conversation_create();
  conversation_add_response_fragment(c, "streaming");
  conversation_add_prompt(c, "a");
  conversation_add_prompt(c, "b");
  CHECK(!conversation_delete_first_entry(c));
  CHECK(conversation_length(c) == 3);
  conversation_destroy(c);
}

static void test_memory_pressure_during_append_relieves_and_reports(void) {
  ConversationManager *m = conversation_manager_create();
  Observer ob = {0};
  conversation_manager_set_handler(m, prv_update, &ob);
  conversation_manager_set_deletion_handler(m, prv_deleted);
  Conversation *c = conversation_manager_get_conversation(m);
  conversation_add_prompt(c, "old prompt");
  ConversationEntry *oldest = conversation_peek(c);
  conversation_add_response(c, "old reply");
  conversation_add_prompt(c, "new prompt");
  conversation_add_response_fragment(c, "partial");
  // The next allocation fails once; bmalloc asks the pressure handlers for
  // memory, which deletes the oldest entry, then retries successfully.
  const char *more = " and a good deal more text, enough to need a bigger buffer";
  test_fail_next_allocations(1);
  CHECK(conversation_add_response_fragment(c, more) == ConversationAppendOk);
  CHECK(ob.deleted_count == 1);
  CHECK(ob.deleted[0] == oldest);
  char expected[128];
  snprintf(expected, sizeof(expected), "partial%s", more);
  CHECK(strcmp(prv_last_text(c), expected) == 0);
  conversation_manager_destroy(m);
}

// ---- request correlation and transport --------------------------------------

static void test_replies_are_correlated_with_request(void) {
  ConversationManager *m = conversation_manager_create();
  Observer ob = {0};
  conversation_manager_set_handler(m, prv_update, &ob);
  Conversation *c = conversation_manager_get_conversation(m);

  // Nothing is accepted before a request is sent.
  prv_deliver(0, MESSAGE_KEY_CHAT, "unsolicited");
  CHECK(conversation_peek(c) == NULL);

  conversation_manager_add_input(m, "What's up?");
  int32_t id = prv_sent_request_id();
  CHECK(id != 0);
  CHECK(dict_find(test_outbox(), MESSAGE_KEY_PROMPT) != NULL);
  CHECK(conversation_manager_is_request_pending(m));

  prv_deliver(id + 1, MESSAGE_KEY_CHAT, "reply to someone else");
  CHECK(strcmp(prv_last_text(c), "What's up?") == 0);

  prv_deliver(id, MESSAGE_KEY_CHAT, "Not much.");
  CHECK(strcmp(prv_last_text(c), "Not much.") == 0);
  prv_deliver(id, MESSAGE_KEY_CHAT_RESET, NULL);
  prv_deliver(id, MESSAGE_KEY_CHAT, "Edited reply.");
  CHECK(strcmp(prv_last_text(c), "Edited reply.") == 0);
  prv_deliver(id, MESSAGE_KEY_CHAT_DONE, NULL);
  CHECK(!conversation_manager_is_request_pending(m));
  CHECK(conversation_is_idle(c));

  // Late traffic for the finished request is ignored.
  prv_deliver(id, MESSAGE_KEY_CHAT, " late");
  CHECK(strcmp(prv_last_text(c), "Edited reply.") == 0);
  CHECK(test_active_timers() == 0);
  conversation_manager_destroy(m);
}

static void test_resent_fragment_is_not_appended_twice(void) {
  // The phone's ack for "Hello " was lost, so it resends the same SEQ.
  ConversationManager *m = conversation_manager_create();
  Conversation *c = conversation_manager_get_conversation(m);
  conversation_manager_add_input(m, "hi");
  int32_t id = prv_sent_request_id();
  prv_deliver_seq(700, id, MESSAGE_KEY_CHAT, "Hello ");
  prv_deliver_seq(700, id, MESSAGE_KEY_CHAT, "Hello ");
  prv_deliver_seq(701, id, MESSAGE_KEY_CHAT, "world");
  CHECK(prv_last_text(c) && strcmp(prv_last_text(c), "Hello world") == 0);
  // A repeated message that isn't adjacent is new traffic, not a resend.
  prv_deliver_seq(702, id, MESSAGE_KEY_CHAT, "!");
  prv_deliver_seq(701, id, MESSAGE_KEY_CHAT, "?");
  CHECK(prv_last_text(c) && strcmp(prv_last_text(c), "Hello world!?") == 0);
  // Messages without SEQ (older phone code) are never dropped.
  prv_deliver(id, MESSAGE_KEY_CHAT, ".");
  prv_deliver(id, MESSAGE_KEY_CHAT, ".");
  CHECK(prv_last_text(c) && strcmp(prv_last_text(c), "Hello world!?..") == 0);
  prv_deliver_seq(703, id, MESSAGE_KEY_CHAT_DONE, NULL);
  CHECK(!conversation_manager_is_request_pending(m));
  conversation_manager_destroy(m);
}

static void test_second_window_ignores_first_windows_reply(void) {
  ConversationManager *first = conversation_manager_create();
  conversation_manager_add_input(first, "first question");
  int32_t first_id = prv_sent_request_id();
  conversation_manager_destroy(first);

  ConversationManager *second = conversation_manager_create();
  conversation_manager_add_input(second, "second question");
  int32_t second_id = prv_sent_request_id();
  CHECK(second_id != first_id);
  Conversation *c = conversation_manager_get_conversation(second);
  prv_deliver(first_id, MESSAGE_KEY_CHAT, "answer to the first question");
  prv_deliver(first_id, MESSAGE_KEY_CHAT_DONE, NULL);
  CHECK(strcmp(prv_last_text(c), "second question") == 0);
  CHECK(conversation_manager_is_request_pending(second));
  conversation_manager_destroy(second);
}

static void test_out_of_memory_reply_is_reported_not_silently_truncated(void) {
  ConversationManager *m = conversation_manager_create();
  Conversation *c = conversation_manager_get_conversation(m);
  conversation_manager_add_input(m, "q");
  int32_t id = prv_sent_request_id();
  prv_deliver(id, MESSAGE_KEY_CHAT, "part one");
  // Fail both the first attempt and bmalloc's last-ditch retry.
  test_fail_next_allocations(2);
  prv_deliver(id, MESSAGE_KEY_CHAT, " part two, which is long enough that it needs a bigger buffer");
  CHECK(conversation_entry_get_type(conversation_peek(c)) == EntryTypeError);
  // Further fragments are dropped instead of being glued on after a hole.
  prv_deliver(id, MESSAGE_KEY_CHAT, " part three");
  CHECK(conversation_entry_get_type(conversation_peek(c)) == EntryTypeError);
  ConversationEntry *reply = conversation_get_last_of_type(c, EntryTypeResponse);
  CHECK(strcmp(conversation_entry_get_response(reply)->response, "part one") == 0);
  CHECK(conversation_entry_get_response(reply)->complete);
  prv_deliver(id, MESSAGE_KEY_CHAT_DONE, NULL);
  CHECK(!conversation_manager_is_request_pending(m));
  conversation_manager_destroy(m);
}

static void test_prompt_not_sent_when_out_of_memory(void) {
  ConversationManager *m = conversation_manager_create();
  Conversation *c = conversation_manager_get_conversation(m);
  int sends = test_outbox_sends();
  test_fail_allocations_after(0);
  conversation_manager_add_input(m, "hello");
  test_allow_allocations();
  CHECK(test_outbox_sends() == sends);
  CHECK(!conversation_manager_is_request_pending(m));
  (void)c;
  conversation_manager_destroy(m);
}

static void test_dropped_inbox_handler_is_registered(void) {
  ConversationManager *m = conversation_manager_create();
  CHECK(test_has_dropped_handler());
  conversation_manager_add_input(m, "q");
  test_deliver_dropped(APP_MSG_BUFFER_OVERFLOW);
  // A drop alone isn't fatal: the phone retries.
  CHECK(conversation_manager_is_request_pending(m));
  conversation_manager_destroy(m);
}

static void test_watchdog_reports_lost_phone(void) {
  ConversationManager *m = conversation_manager_create();
  Conversation *c = conversation_manager_get_conversation(m);
  conversation_manager_add_input(m, "q");
  CHECK(test_active_timers() == 1);
  test_fire_timers();  // still connected and recent: re-arms
  CHECK(conversation_manager_is_request_pending(m));
  CHECK(test_active_timers() == 1);
  test_set_connected(false);
  test_fire_timers();
  CHECK(!conversation_manager_is_request_pending(m));
  CHECK(conversation_entry_get_type(conversation_peek(c)) == EntryTypeError);
  CHECK(conversation_is_idle(c));
  CHECK(test_active_timers() == 0);
  conversation_manager_destroy(m);
}

static void test_outbox_failure_only_reported_by_sender(void) {
  ConversationManager *a = conversation_manager_create();
  ConversationManager *b = conversation_manager_create();
  conversation_manager_add_input(a, "from a");
  DictionaryIterator failed = {0};
  dict_write_cstring(&failed, MESSAGE_KEY_PROMPT, "from a");
  dict_write_int32(&failed, MESSAGE_KEY_REQUEST_ID, prv_sent_request_id());
  test_deliver_outbox_failed(&failed, APP_MSG_SEND_TIMEOUT);
  test_dict_clear(&failed);
  CHECK(conversation_entry_get_type(conversation_peek(conversation_manager_get_conversation(a))) == EntryTypeError);
  CHECK(!conversation_manager_is_request_pending(a));
  CHECK(conversation_peek(conversation_manager_get_conversation(b)) == NULL);
  conversation_manager_destroy(a);
  conversation_manager_destroy(b);
}

// ---- history ----------------------------------------------------------------

static void prv_history_message(uint32_t key, const char *text, int32_t number) {
  // Mirrors prv_history_message_handler in src/c/assistant.c.
  if (key == MESSAGE_KEY_HISTORY_BEGIN) history_begin();
  else if (key == MESSAGE_KEY_HISTORY_PROMPT) history_add_prompt(text);
  else if (key == MESSAGE_KEY_HISTORY_RESPONSE) history_add_response(text);
  else if (key == MESSAGE_KEY_HISTORY_DONE) history_set_done(number);
}

static int s_history_done_calls = 0;
static void prv_history_done(void) { s_history_done_calls++; }

static void test_history_loading_until_done(void) {
  s_history_done_calls = 0;
  history_set_done_callback(prv_history_done);
  CHECK(history_is_loading());
  prv_history_message(MESSAGE_KEY_HISTORY_BEGIN, NULL, 1);
  prv_history_message(MESSAGE_KEY_HISTORY_PROMPT, "hi", 0);
  // The first entry must not end "loading": otherwise a long-press here
  // opens a fresh conversation in the middle of the transfer.
  CHECK(history_is_loading());
  CHECK(!history_is_available());
  prv_history_message(MESSAGE_KEY_HISTORY_RESPONSE, "hello", 0);
  prv_history_message(MESSAGE_KEY_HISTORY_DONE, NULL, HISTORY_STATUS_OK);
  CHECK(!history_is_loading());
  CHECK(history_is_available());
  CHECK(history_get_count() == 2);
  CHECK(s_history_done_calls == 1);
}

static void test_history_error_discards_partial_batch(void) {
  prv_history_message(MESSAGE_KEY_HISTORY_BEGIN, NULL, 1);
  prv_history_message(MESSAGE_KEY_HISTORY_PROMPT, "hi", 0);
  prv_history_message(MESSAGE_KEY_HISTORY_DONE, NULL, HISTORY_STATUS_ERROR);
  CHECK(!history_is_loading());
  CHECK(!history_is_available());
}

static void test_history_begin_replaces_previous_batch(void) {
  prv_history_message(MESSAGE_KEY_HISTORY_BEGIN, NULL, 1);
  prv_history_message(MESSAGE_KEY_HISTORY_PROMPT, "one", 0);
  prv_history_message(MESSAGE_KEY_HISTORY_DONE, NULL, HISTORY_STATUS_OK);
  prv_history_message(MESSAGE_KEY_HISTORY_BEGIN, NULL, 2);
  prv_history_message(MESSAGE_KEY_HISTORY_PROMPT, "two", 0);
  prv_history_message(MESSAGE_KEY_HISTORY_DONE, NULL, HISTORY_STATUS_OK);
  CHECK(history_get_count() == 1);
  CHECK(strcmp(history_get_entry(0)->text, "two") == 0);
}

static void test_history_truncation_keeps_utf8_valid(void) {
  char text[600];
  // 'é' is two bytes; fill so the 511-byte cut would land mid-character.
  size_t pos = 0;
  text[pos++] = 'a';
  while (pos + 2 < sizeof(text) - 1) {
    text[pos++] = (char)0xC3;
    text[pos++] = (char)0xA9;
  }
  text[pos] = 0;
  history_begin();
  history_add_response(text);
  const char *stored = history_get_entry(0)->text;
  size_t len = strlen(stored);
  CHECK(len < 512);
  // Must end on a complete two-byte sequence.
  CHECK(((unsigned char)stored[len - 1] & 0xC0) == 0x80);
  CHECK((len - 1) % 2 == 0);
  history_set_done(HISTORY_STATUS_OK);
}

int main(void) {
  conversation_manager_init();
  RUN(test_destroy_unregisters_memory_pressure_callback);
  RUN(test_unregister_matches_context);
  RUN(test_pressure_registration_never_allocates);
  RUN(test_try_free_is_not_reentrant);
  RUN(test_bmalloc_fails_fast_without_sleeping);
  RUN(test_conversation_create_survives_every_allocation_failure);
  RUN(test_add_functions_fail_cleanly);
  RUN(test_fragment_failure_keeps_existing_text);
  RUN(test_fragment_failure_with_no_open_response);
  RUN(test_entry_pointers_survive_table_growth);
  RUN(test_table_growth_failure_keeps_old_entries);
  RUN(test_delete_first_entry_never_frees_open_response);
  RUN(test_memory_pressure_during_append_relieves_and_reports);
  RUN(test_replies_are_correlated_with_request);
  RUN(test_resent_fragment_is_not_appended_twice);
  RUN(test_second_window_ignores_first_windows_reply);
  RUN(test_out_of_memory_reply_is_reported_not_silently_truncated);
  RUN(test_prompt_not_sent_when_out_of_memory);
  RUN(test_dropped_inbox_handler_is_registered);
  RUN(test_watchdog_reports_lost_phone);
  RUN(test_outbox_failure_only_reported_by_sender);
  RUN(test_history_loading_until_done);
  RUN(test_history_error_discards_partial_batch);
  RUN(test_history_begin_replaces_previous_batch);
  RUN(test_history_truncation_keeps_utf8_valid);
  test_dict_clear(test_outbox());
  printf("%d checks, %d failures\n", s_checks, s_failures);
  return s_failures ? 1 : 0;
}
