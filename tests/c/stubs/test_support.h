#pragma once
#include <pebble.h>

void test_alloc_reset(void);
long test_live_allocations(void);
void test_fail_allocations_after(long successes);
void test_fail_next_allocations(long count);
void test_allow_allocations(void);
void test_set_heap_free(int bytes);
int test_sleep_calls(void);

void test_dict_clear(DictionaryIterator *iter);
DictionaryIterator *test_outbox(void);
int test_outbox_sends(void);
void test_set_outbox_begin_result(AppMessageResult result);

int test_active_timers(void);
void test_fire_timers(void);
void test_set_connected(bool connected);

int test_active_subscriptions(void);
bool test_has_dropped_handler(void);
void test_deliver_inbox(DictionaryIterator *iter);
void test_deliver_dropped(AppMessageResult reason);
void test_deliver_outbox_failed(DictionaryIterator *iter, AppMessageResult reason);
void test_deliver_outbox_sent(DictionaryIterator *iter);
