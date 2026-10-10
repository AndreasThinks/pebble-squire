# Host-side regression tests for watch C code (no Pebble SDK required).
#   make -C tests/c        build and run
CC ?= cc
SRC := ../../src/c
CFLAGS := -std=gnu11 -g -O0 -Wall -Wextra -Wno-unused-parameter -Wno-zero-length-bounds \
          -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined \
          -Istubs -iquote $(SRC)
SOURCES := test_watch.c stubs/pebble_stubs.c \
           $(SRC)/converse/conversation.c \
           $(SRC)/converse/conversation_manager.c \
           $(SRC)/converse/history.c \
           $(SRC)/util/memory/malloc.c \
           $(SRC)/util/memory/pressure.c \
           $(SRC)/util/strings.c \
           $(SRC)/util/tuple_util.c

BUILD := build

.PHONY: test clean
test: $(BUILD)/test_watch
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 ./$(BUILD)/test_watch

$(BUILD)/test_watch: $(SOURCES) stubs/pebble.h stubs/test_support.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(SOURCES)

clean:
	rm -rf $(BUILD)
