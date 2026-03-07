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

// Removing the following header is prohibited as it can introduce undefined
// behavior.
// clang-format off
#include "gloop/enforce_gloop_support.h"
// clang-format on

// Copyright 2012 Google Inc. All Rights Reserved.

//
// This is a simple program which you can disassemble to convince yourself that
// the compiler is smart enough to use StrongInt properly.

#include <time.h>

#include "gloop/util/intops/strong_int.h"
#include "gloop/util/intops/strong_vector.h"

DEFINE_STRONG_INT_TYPE(Strong, time_t);

int Test1() {
  Strong x(93);
  Strong y(time(nullptr) % 10);
  Strong z = x + y;
  return z.value();
}

int Test2() {
  int x(93);
  int y(time(nullptr) % 10);
  int z = x + y;
  return z;
}

time_t Direct(int s) {
  util_intops::StrongVector<Strong, time_t> v(s, time(nullptr));
  time_t result = 0;
  for (const auto f : v) {
    result += f;
  }
  return result;
}

time_t ExplicitIndex(int s) {
  util_intops::StrongVector<Strong, time_t> v(s, time(nullptr));
  time_t result = 0;
  for (Strong idx(0); idx < v.end_index(); ++idx) {
    result += v[idx];
  }
  return result;
}

time_t IndexRange(int s) {
  util_intops::StrongVector<Strong, time_t> v(s, time(nullptr));
  time_t result = 0;
  for (const Strong idx : v.index_range()) {
    result += v[idx];
  }
  return result;
}

int main() {
  int x = Test1();
  int y = Test2();
  return x ^ y ^ (Direct(x) + ExplicitIndex(x) == IndexRange(x));
}
