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

#include "transport.h"
#include "tuple_util.h"

bool transport_is_replay(TransportReplayGuard *guard, DictionaryIterator *iter) {
  Tuple *tuple = dict_find(iter, MESSAGE_KEY_SEQ);
  if (!tuple) {
    return false;
  }
  int32_t seq = tuple_get_int32(tuple);
  if (guard->valid && guard->last_seq == seq) {
    return true;
  }
  guard->valid = true;
  guard->last_seq = seq;
  return false;
}
