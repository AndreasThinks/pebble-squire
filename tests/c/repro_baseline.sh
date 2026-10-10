#!/usr/bin/env bash
# Demonstrates, against the original watch sources, the bugs that the host
# tests in test_watch.c guard against. Read-only with respect to the
# checkout: baseline files are extracted with `git show` into a temp dir.
#   tests/c/repro_baseline.sh [baseline-commit]
set -u
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
rev="${1:-00143c9cef6c5b793664d63411736880445f6685}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/src/c/converse" "$tmp/src/c/util/memory"
for f in converse/conversation.c converse/conversation.h converse/conversation_manager.c \
         converse/conversation_manager.h converse/history.c converse/history.h \
         util/memory/malloc.c util/memory/malloc.h util/memory/pressure.c util/memory/pressure.h \
         util/strings.c util/strings.h util/logging.h util/debug_state.h features.h; do
  git -C "$root" show "$rev:src/c/$f" > "$tmp/src/c/$f"
done
# The old bmalloc reads the ARM link register; neutralise that on the host.
echo '#define __asm(x)' > "$tmp/host_compat.h"
cc -std=gnu11 -g -O0 -w -fsanitize=address,undefined -fno-omit-frame-pointer \
   -include "$tmp/host_compat.h" -I"$here/stubs" -iquote "$tmp/src/c" \
   -I"$root/node_modules/@rebble/linked-list/dist/include" \
   -o "$tmp/repro" "$here/repro_baseline.c" "$here/stubs/pebble_stubs.c" \
   "$tmp"/src/c/converse/*.c "$tmp"/src/c/util/memory/*.c "$tmp/src/c/util/strings.c" \
   "$root/node_modules/@rebble/linked-list/src/c/linked-list.c" -iquote "$root/node_modules/@rebble/linked-list/include" \
   || { echo "could not build baseline repro"; exit 2; }
status=0
for scenario in pressure-after-destroy create-oom dangling-entry bmalloc-sleeps; do
  out="$(ASAN_OPTIONS=detect_leaks=0 "$tmp/repro" "$scenario" 2>&1)"
  code=$?
  summary="$(printf '%s\n' "$out" | grep -m1 -E 'ERROR: AddressSanitizer: [a-zA-Z-]+|runtime error|bmalloc returned' | sed -E 's/ on address.*//; s/0x[0-9a-f]+/<addr>/g')"
  if [ $code -ne 0 ]; then
    echo "REPRODUCED $scenario (exit $code): $summary"
  else
    echo "not reproduced: $scenario"
    status=1
  fi
done
exit $status
