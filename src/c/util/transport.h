/*
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

#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <pebble.h>

// The phone stamps every message with a SEQ number and reuses it when it
// resends a message whose acknowledgement it never saw. Its queue is strictly
// ordered, so a resend can only ever repeat the message just before it.
// Each inbox handler that does non-idempotent work (appending text, pushing
// windows) keeps its own guard, because every handler sees every message.
typedef struct {
  bool valid;
  int32_t last_seq;
} TransportReplayGuard;

// True if iter repeats the message this guard last accepted; otherwise records
// it. Messages without SEQ are never treated as replays.
bool transport_is_replay(TransportReplayGuard *guard, DictionaryIterator *iter);

#endif
