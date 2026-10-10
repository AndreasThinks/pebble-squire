// Reproduces the watch-side memory bugs against the ORIGINAL sources
// (commit 00143c9), using the old API. Built and run by repro_baseline.sh;
// each scenario is expected to crash under AddressSanitizer on the old code.
#include <pebble.h>
#include "test_support.h"
#include "converse/conversation.h"
#include "converse/conversation_manager.h"
#include "util/memory/malloc.h"
#include "util/memory/pressure.h"

int main(int argc, char **argv) {
  const char *scenario = argc > 1 ? argv[1] : "";
  memory_pressure_init();
  if (strcmp(scenario, "pressure-after-destroy") == 0) {
    ConversationManager *m = conversation_manager_create();
    Conversation *c = conversation_manager_get_conversation(m);
    conversation_add_prompt(c, "a");
    conversation_add_prompt(c, "b");
    conversation_add_prompt(c, "c");
    conversation_manager_destroy(m);
    // The callback registered with the freed manager is still in the list.
    memory_pressure_try_free();
  } else if (strcmp(scenario, "create-oom") == 0) {
    test_fail_allocations_after(0);
    conversation_create();
  } else if (strcmp(scenario, "dangling-entry") == 0) {
    Conversation *c = conversation_create();
    conversation_add_prompt(c, "first");
    ConversationEntry *first = conversation_peek(c);
    for (int i = 0; i < 40; ++i) {
      conversation_add_prompt(c, "more");
    }
    // Segment layers hold pointers like this one.
    printf("type %d\n", conversation_entry_get_type(first));
  } else if (strcmp(scenario, "bmalloc-sleeps") == 0) {
    test_fail_allocations_after(0);
    void *p = bmalloc(10);
    printf("bmalloc returned %p after %d psleep(100) calls\n", p, test_sleep_calls());
    return test_sleep_calls() > 0 ? 1 : 0;
  } else {
    fprintf(stderr, "unknown scenario\n");
    return 2;
  }
  return 0;
}
