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

#include "tuple_util.h"

int32_t tuple_get_int32(const Tuple* tuple) {
  if (!tuple) {
    return 0;
  }
  if (tuple->type == TUPLE_INT) {
    switch (tuple->length) {
      case 1: return tuple->value->int8;
      case 2: return tuple->value->int16;
      case 4: return tuple->value->int32;
    }
  } else if (tuple->type == TUPLE_UINT) {
    switch (tuple->length) {
      case 1: return tuple->value->uint8;
      case 2: return tuple->value->uint16;
      case 4: return (int32_t)tuple->value->uint32;
    }
  }
  return 0;
}
