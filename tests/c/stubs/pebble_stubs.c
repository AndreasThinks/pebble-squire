// Host implementations of the Pebble SDK stubs declared in stubs/pebble.h,
// plus test controls (allocation failure injection, timers, app messages).
#include <pebble.h>
#include <pebble-events/pebble-events.h>
#include "test_support.h"

#undef malloc
#undef free

// ---- Allocator ------------------------------------------------------------

static long s_live_allocations = 0;
static long s_allocation_count = 0;
static long s_fail_after = -1;     // fail every allocation once this many succeeded
static long s_fail_next = 0;       // fail this many upcoming allocations
static int s_heap_free = 100000;
static int s_sleep_calls = 0;

void *test_malloc(size_t size) {
  if (s_fail_next > 0) {
    --s_fail_next;
    return NULL;
  }
  if (s_fail_after >= 0 && s_allocation_count >= s_fail_after) {
    return NULL;
  }
  void *ptr = malloc(size ? size : 1);
  if (ptr) {
    ++s_allocation_count;
    ++s_live_allocations;
  }
  return ptr;
}

void test_free(void *ptr) {
  if (ptr) {
    --s_live_allocations;
    free(ptr);
  }
}

void test_alloc_reset(void) {
  s_fail_after = -1;
  s_fail_next = 0;
  s_allocation_count = 0;
  s_heap_free = 100000;
  s_sleep_calls = 0;
}
long test_live_allocations(void) { return s_live_allocations; }
void test_fail_allocations_after(long successes) { s_allocation_count = 0; s_fail_after = successes; }
void test_fail_next_allocations(long count) { s_fail_next = count; }
void test_allow_allocations(void) { s_fail_after = -1; s_fail_next = 0; }
void test_set_heap_free(int bytes) { s_heap_free = bytes; }
int test_sleep_calls(void) { return s_sleep_calls; }

int heap_bytes_free(void) { return s_heap_free; }
void psleep(int millis) { (void)millis; ++s_sleep_calls; }
void vibes_short_pulse(void) {}

// ---- Dictionaries ------------------------------------------------------------

Tuple *dict_find(const DictionaryIterator *iter, uint32_t key) {
  for (int i = 0; i < iter->count; ++i) {
    if (iter->tuples[i]->key == key) {
      return iter->tuples[i];
    }
  }
  return NULL;
}

Tuple *dict_read_first(DictionaryIterator *iter) {
  iter->cursor = 0;
  return iter->count > 0 ? iter->tuples[iter->cursor++] : NULL;
}

Tuple *dict_read_next(DictionaryIterator *iter) {
  return iter->cursor < iter->count ? iter->tuples[iter->cursor++] : NULL;
}

static Tuple *prv_add_tuple(DictionaryIterator *iter, uint32_t key, TupleType type, const void *data, uint16_t length) {
  if (iter->count >= TEST_DICT_MAX_TUPLES) {
    abort();
  }
  Tuple *tuple = malloc(sizeof(Tuple) + length + 4);
  memset(tuple, 0, sizeof(Tuple) + length + 4);
  tuple->key = key;
  tuple->type = type;
  tuple->length = length;
  memcpy(tuple->value->data, data, length);
  iter->tuples[iter->count++] = tuple;
  return tuple;
}

int dict_write_cstring(DictionaryIterator *iter, uint32_t key, const char *value) {
  prv_add_tuple(iter, key, TUPLE_CSTRING, value, (uint16_t)(strlen(value) + 1));
  return 0;
}

int dict_write_int32(DictionaryIterator *iter, uint32_t key, int32_t value) {
  prv_add_tuple(iter, key, TUPLE_INT, &value, 4);
  return 0;
}

int dict_write_uint8(DictionaryIterator *iter, uint32_t key, uint8_t value) {
  prv_add_tuple(iter, key, TUPLE_UINT, &value, 1);
  return 0;
}

void test_dict_clear(DictionaryIterator *iter) {
  for (int i = 0; i < iter->count; ++i) {
    free(iter->tuples[i]);
  }
  memset(iter, 0, sizeof(*iter));
}

// ---- Outbox ------------------------------------------------------------------

static DictionaryIterator s_outbox;
static bool s_outbox_open = false;
static AppMessageResult s_outbox_begin_result = APP_MSG_OK;
static int s_outbox_sends = 0;

AppMessageResult app_message_outbox_begin(DictionaryIterator **iter) {
  if (s_outbox_begin_result != APP_MSG_OK) {
    return s_outbox_begin_result;
  }
  test_dict_clear(&s_outbox);
  s_outbox_open = true;
  *iter = &s_outbox;
  return APP_MSG_OK;
}

AppMessageResult app_message_outbox_send(void) {
  if (!s_outbox_open) {
    return APP_MSG_BUSY;
  }
  s_outbox_open = false;
  ++s_outbox_sends;
  return APP_MSG_OK;
}

DictionaryIterator *test_outbox(void) { return &s_outbox; }
int test_outbox_sends(void) { return s_outbox_sends; }
void test_set_outbox_begin_result(AppMessageResult result) { s_outbox_begin_result = result; }

// ---- Timers ------------------------------------------------------------------

struct AppTimer {
  AppTimerCallback callback;
  void *data;
  bool active;
};

#define MAX_TIMERS 64
static struct AppTimer s_timers[MAX_TIMERS];

AppTimer *app_timer_register(uint32_t timeout_ms, AppTimerCallback callback, void *callback_data) {
  (void)timeout_ms;
  for (int i = 0; i < MAX_TIMERS; ++i) {
    if (!s_timers[i].active) {
      s_timers[i] = (struct AppTimer){ callback, callback_data, true };
      return &s_timers[i];
    }
  }
  abort();
}

void app_timer_cancel(AppTimer *timer) {
  if (!timer->active) {
    // Cancelling a dead timer is a bug the real SDK punishes.
    fprintf(stderr, "app_timer_cancel on inactive timer\n");
    abort();
  }
  timer->active = false;
}

int test_active_timers(void) {
  int n = 0;
  for (int i = 0; i < MAX_TIMERS; ++i) {
    n += s_timers[i].active;
  }
  return n;
}

// Fires every active timer once (in registration order).
void test_fire_timers(void) {
  for (int i = 0; i < MAX_TIMERS; ++i) {
    if (s_timers[i].active) {
      s_timers[i].active = false;
      s_timers[i].callback(s_timers[i].data);
    }
  }
}

static bool s_connected = true;
bool connection_service_peek_pebble_app_connection(void) { return s_connected; }
void test_set_connected(bool connected) { s_connected = connected; }

// ---- pebble-events -----------------------------------------------------------

typedef struct {
  bool active;
  EventAppMessageHandlers handlers;
  void *context;
} Subscription;

#define MAX_SUBSCRIPTIONS 8
static Subscription s_subscriptions[MAX_SUBSCRIPTIONS];

void events_app_message_request_inbox_size(uint32_t size) { (void)size; }
void events_app_message_request_outbox_size(uint32_t size) { (void)size; }

EventHandle events_app_message_subscribe_handlers(EventAppMessageHandlers handlers, void *context) {
  for (int i = 0; i < MAX_SUBSCRIPTIONS; ++i) {
    if (!s_subscriptions[i].active) {
      s_subscriptions[i] = (Subscription){ true, handlers, context };
      return &s_subscriptions[i];
    }
  }
  return NULL;
}

void events_app_message_unsubscribe(EventHandle handle) {
  ((Subscription *)handle)->active = false;
}

int test_active_subscriptions(void) {
  int n = 0;
  for (int i = 0; i < MAX_SUBSCRIPTIONS; ++i) {
    n += s_subscriptions[i].active;
  }
  return n;
}

bool test_has_dropped_handler(void) {
  for (int i = 0; i < MAX_SUBSCRIPTIONS; ++i) {
    if (s_subscriptions[i].active && s_subscriptions[i].handlers.dropped) {
      return true;
    }
  }
  return false;
}

void test_deliver_inbox(DictionaryIterator *iter) {
  for (int i = 0; i < MAX_SUBSCRIPTIONS; ++i) {
    if (s_subscriptions[i].active && s_subscriptions[i].handlers.received) {
      s_subscriptions[i].handlers.received(iter, s_subscriptions[i].context);
    }
  }
}

void test_deliver_dropped(AppMessageResult reason) {
  for (int i = 0; i < MAX_SUBSCRIPTIONS; ++i) {
    if (s_subscriptions[i].active && s_subscriptions[i].handlers.dropped) {
      s_subscriptions[i].handlers.dropped(reason, s_subscriptions[i].context);
    }
  }
}

void test_deliver_outbox_failed(DictionaryIterator *iter, AppMessageResult reason) {
  for (int i = 0; i < MAX_SUBSCRIPTIONS; ++i) {
    if (s_subscriptions[i].active && s_subscriptions[i].handlers.failed) {
      s_subscriptions[i].handlers.failed(iter, reason, s_subscriptions[i].context);
    }
  }
}

void test_deliver_outbox_sent(DictionaryIterator *iter) {
  for (int i = 0; i < MAX_SUBSCRIPTIONS; ++i) {
    if (s_subscriptions[i].active && s_subscriptions[i].handlers.sent) {
      s_subscriptions[i].handlers.sent(iter, s_subscriptions[i].context);
    }
  }
}
