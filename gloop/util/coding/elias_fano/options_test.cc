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

#include "gloop/util/coding/elias_fano/options.h"

#include <cstdint>
#include <limits>
#include <string>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

namespace elias_fano {
namespace {

TEST(_, Empty) {
  Options o(0, 0);
  EXPECT_EQ(0, o.width());
  EXPECT_EQ(0, o.one_bits());
  EXPECT_EQ(0, o.zero_bits());
}

TEST(_, Max) {
  Options o(std::numeric_limits<uint32_t>::max(),
            std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(0, o.width());
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(), o.one_bits());
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(), o.zero_bits());
}

TEST(_, WhenZerosWouldBeMoreThan2_32) {
  Options o(std::numeric_limits<uint32_t>::max(),
            uint64_t{std::numeric_limits<uint32_t>::max()} + 1);
  EXPECT_EQ(1, o.width());
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(), o.one_bits());
  EXPECT_EQ(std::numeric_limits<uint32_t>::max() / 2 + 1, o.zero_bits());
}

TEST(_, Override) {
  Options o(10, 128);
  o.override_width(1);
  EXPECT_EQ(1, o.width());
  EXPECT_EQ(10, o.one_bits());
  EXPECT_EQ(64, o.zero_bits());
}

void Garbage(std::string input) {
  Options::Decode(input).IgnoreError();  // just test that it doesn't crash
}
FUZZ_TEST(OptionsTest, Garbage);

}  // namespace
}  // namespace elias_fano
