#include "history.h"
#include "../util/memory/malloc.h"
#include "../util/logging.h"
#include <string.h>

static HistoryEntry s_entries[HISTORY_MAX_ENTRIES];
static int s_count = 0;
static char s_thread_id[37] = "";
static bool s_done = false;
static bool s_loading = false;
static void (*s_done_callback)(void) = NULL;

void history_init(void) {
  s_count = 0;
  s_done = false;
  s_loading = true;
  s_done_callback = NULL;
  s_thread_id[0] = '\0';
}

void history_set_loading(bool loading) {
  s_loading = loading;
}

bool history_is_loading(void) {
  return s_loading;
}

void history_set_done_callback(void (*callback)(void)) {
  s_done_callback = callback;
}

void history_add_prompt(const char* text) {
  if (s_count >= HISTORY_MAX_ENTRIES) return;
  s_entries[s_count].type = HistoryEntryTypePrompt;
  strncpy(s_entries[s_count].text, text, sizeof(s_entries[s_count].text) - 1);
  s_entries[s_count].text[sizeof(s_entries[s_count].text) - 1] = '\0';
  s_count++;
  SQUIRE_LOG(APP_LOG_LEVEL_INFO, "History prompt %d: %.50s", s_count, text);
}

void history_add_response(const char* text) {
  if (s_count >= HISTORY_MAX_ENTRIES) return;
  s_entries[s_count].type = HistoryEntryTypeResponse;
  strncpy(s_entries[s_count].text, text, sizeof(s_entries[s_count].text) - 1);
  s_entries[s_count].text[sizeof(s_entries[s_count].text) - 1] = '\0';
  s_count++;
  SQUIRE_LOG(APP_LOG_LEVEL_INFO, "History response %d: %.50s", s_count, text);
}

void history_set_thread_id(const char* thread_id) {
  strncpy(s_thread_id, thread_id, sizeof(s_thread_id) - 1);
  s_thread_id[sizeof(s_thread_id) - 1] = '\0';
  SQUIRE_LOG(APP_LOG_LEVEL_INFO, "History thread ID: %s", thread_id);
}

static void prv_shift_if_full(void) {
  if (s_count < HISTORY_MAX_ENTRIES) return;
  for (int i = 0; i < HISTORY_MAX_ENTRIES - 1; i++) {
    s_entries[i] = s_entries[i + 1];
  }
  s_count = HISTORY_MAX_ENTRIES - 1;
}

void history_push_prompt(const char* text) {
  prv_shift_if_full();
  s_entries[s_count].type = HistoryEntryTypePrompt;
  strncpy(s_entries[s_count].text, text, sizeof(s_entries[s_count].text) - 1);
  s_entries[s_count].text[sizeof(s_entries[s_count].text) - 1] = '\0';
  s_count++;
  SQUIRE_LOG(APP_LOG_LEVEL_INFO, "History push prompt %d: %.50s", s_count, text);
}

void history_push_response(const char* text) {
  prv_shift_if_full();
  s_entries[s_count].type = HistoryEntryTypeResponse;
  strncpy(s_entries[s_count].text, text, sizeof(s_entries[s_count].text) - 1);
  s_entries[s_count].text[sizeof(s_entries[s_count].text) - 1] = '\0';
  s_count++;
  SQUIRE_LOG(APP_LOG_LEVEL_INFO, "History push response %d: %.50s", s_count, text);
}

void history_push_thread_id(const char* thread_id) {
  history_set_thread_id(thread_id);
}

void history_set_done(void) {
  s_done = true;
  s_loading = false;
  SQUIRE_LOG(APP_LOG_LEVEL_INFO, "History done. %d entries.", s_count);
  if (s_done_callback) {
    s_done_callback();
    s_done_callback = NULL;
  }
}

bool history_is_available(void) {
  return s_done && s_count > 0;
}

const char* history_get_thread_id(void) {
  return s_thread_id;
}

int history_get_count(void) {
  return s_count;
}

const HistoryEntry* history_get_entry(int index) {
  if (index < 0 || index >= s_count) return NULL;
  return &s_entries[index];
}

void history_free(void) {
  s_count = 0;
  s_done = false;
  s_thread_id[0] = '\0';
}