// Copyright 2026 Google LLC.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef THIRD_PARTY_GLOOP_STRINGS_NUMBERS_TEST_COMMON_H_
#define THIRD_PARTY_GLOOP_STRINGS_NUMBERS_TEST_COMMON_H_

#include <string>

namespace strings {

template <typename IntType>
inline bool Itoa(IntType value, int base, std::string* destination) {
  destination->clear();
  if (base <= 1 || base > 36) {
    return false;
  }

  if (value == 0) {
    destination->push_back('0');
    return true;
  }

  bool negative = value < 0;
  while (value != 0) {
    const IntType next_value = value / base;
    // Can't use std::abs here because of problems when IntType is unsigned.
    int remainder = value > next_value * base ? value - next_value * base
                                              : next_value * base - value;
    char c = remainder < 10 ? '0' + remainder : 'A' + remainder - 10;
    destination->insert(0, 1, c);
    value = next_value;
  }

  if (negative) {
    destination->insert(0, 1, '-');
  }
  return true;
}

}  // namespace strings

#endif  // THIRD_PARTY_GLOOP_STRINGS_NUMBERS_TEST_COMMON_H_
