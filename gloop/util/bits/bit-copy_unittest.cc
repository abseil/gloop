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

// Testing bit-copy functionality

#include "gloop/util/bits/bit-copy.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include "absl/log/log_streamer.h"
#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "gloop/gloop_test.h"

TEST(TestBitCopy, Test) {
  absl::BitGen random;
  // Initialization
  const int n = 12345;
  const int numbits = n * 8;
  char a0[n];
  char a[n];
  char b[n];

  // a[] and a0[] should be the same at the beginning and end of each
  // testing phase
  for (int i = 0; i < n; ++i) {
    a[i] = absl::Uniform<uint8_t>(random);
    a0[i] = a[i];
  }
  for (int i = 0; i < n; ++i) {
    b[i] = absl::Uniform<uint8_t>(random);
  }
  EXPECT_EQ(memcmp(a, a0, n), 0);
  absl::PrintF("Now testing 100000 times BitCopy()\n");

  // Each testing phase consists of the following:
  // Copy some bits from a[] to b[]
  // Copy the same bits in the first step from b[] back to a[]
  // Check to make sure a[] and a0[] are still the same
  for (int i = 0; i < 100000; ++i) {
    int j1 = absl::Uniform<int>(random, 0, numbits);
    int j2 = absl::Uniform<int>(random, 0, numbits);
    int len =
        absl::Uniform<int>(random, 0, std::min(numbits - j1, numbits - j2));
    len %= 1 << (absl::Uniform<int>(random, 0, 30));

    // check BitCopy
    BitCopy(b, j2, a, j1, len);
    BitCopy(a, j1, b, j2, len);
    EXPECT_EQ(memcmp(a, a0, n), 0);
  }
}
