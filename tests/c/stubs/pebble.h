// Minimal host-side stand-in for the Pebble SDK, covering only what the
// conversation/memory/history code under test uses. Allocation goes through
// test_malloc/test_free so tests can inject failures and check for leaks.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void *test_malloc(size_t size);
void test_free(void *ptr);
#define malloc(size) test_malloc(size)
#define free(ptr) test_free(ptr)

int heap_bytes_free(void);
void psleep(int millis);
void vibes_short_pulse(void);

typedef enum {
  APP_LOG_LEVEL_ERROR = 1,
  APP_LOG_LEVEL_WARNING = 50,
  APP_LOG_LEVEL_INFO = 100,
  APP_LOG_LEVEL_DEBUG = 200,
  APP_LOG_LEVEL_DEBUG_VERBOSE = 255,
} AppLogLevel;
#define APP_LOG(level, fmt, ...) do { if (0) printf(fmt, ##__VA_ARGS__); } while (0)

typedef enum {
  TUPLE_BYTE_ARRAY = 0,
  TUPLE_CSTRING = 1,
  TUPLE_UINT = 2,
  TUPLE_INT = 3,
} TupleType;

typedef struct {
  uint32_t key;
  TupleType type;
  uint16_t length;
  union {
    uint8_t data[0];
    char cstring[0];
    uint8_t uint8;
    uint16_t uint16;
    uint32_t uint32;
    int8_t int8;
    int16_t int16;
    int32_t int32;
  } value[];
} Tuple;

#define TEST_DICT_MAX_TUPLES 16
typedef struct {
  int count;
  int cursor;
  Tuple *tuples[TEST_DICT_MAX_TUPLES];
} DictionaryIterator;

Tuple *dict_find(const DictionaryIterator *iter, uint32_t key);
Tuple *dict_read_first(DictionaryIterator *iter);
Tuple *dict_read_next(DictionaryIterator *iter);
int dict_write_cstring(DictionaryIterator *iter, uint32_t key, const char *value);
int dict_write_int32(DictionaryIterator *iter, uint32_t key, int32_t value);
int dict_write_uint8(DictionaryIterator *iter, uint32_t key, uint8_t value);

typedef enum {
  APP_MSG_OK = 0,
  APP_MSG_SEND_TIMEOUT = 2,
  APP_MSG_SEND_REJECTED = 4,
  APP_MSG_NOT_CONNECTED = 8,
  APP_MSG_BUSY = 64,
  APP_MSG_BUFFER_OVERFLOW = 128,
  APP_MSG_OUT_OF_MEMORY = 1024,
} AppMessageResult;

AppMessageResult app_message_outbox_begin(DictionaryIterator **iter);
AppMessageResult app_message_outbox_send(void);

typedef void (*AppMessageInboxReceived)(DictionaryIterator *iterator, void *context);
typedef void (*AppMessageInboxDropped)(AppMessageResult reason, void *context);
typedef void (*AppMessageOutboxSent)(DictionaryIterator *iterator, void *context);
typedef void (*AppMessageOutboxFailed)(DictionaryIterator *iterator, AppMessageResult reason, void *context);

typedef struct AppTimer AppTimer;
typedef void (*AppTimerCallback)(void *data);
AppTimer *app_timer_register(uint32_t timeout_ms, AppTimerCallback callback, void *callback_data);
void app_timer_cancel(AppTimer *timer);

bool connection_service_peek_pebble_app_connection(void);

// Message keys (values are arbitrary but distinct).
enum {
  MESSAGE_KEY_CHAT = 1,
  MESSAGE_KEY_FUNCTION,
  MESSAGE_KEY_PROMPT,
  MESSAGE_KEY_CLOSE_CODE,
  MESSAGE_KEY_CLOSE_REASON,
  MESSAGE_KEY_CLOSE_WAS_CLEAN,
  MESSAGE_KEY_CHAT_DONE,
  MESSAGE_KEY_THREAD_ID,
  MESSAGE_KEY_ACTION_SETTINGS_UPDATED,
  MESSAGE_KEY_TYPING,
  MESSAGE_KEY_WARNING,
  MESSAGE_KEY_REQUEST_ID,
  MESSAGE_KEY_CHAT_RESET,
  MESSAGE_KEY_HISTORY_BEGIN,
  MESSAGE_KEY_HISTORY_PROMPT,
  MESSAGE_KEY_HISTORY_RESPONSE,
  MESSAGE_KEY_HISTORY_THREAD_ID,
  MESSAGE_KEY_HISTORY_DONE,
  MESSAGE_KEY_SEQ,
};
