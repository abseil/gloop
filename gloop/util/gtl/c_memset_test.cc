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

#include "gloop/util/gtl/c_memset.h"

#include <array>
#include <string>
#include <vector>

#include "absl/base/macros.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::testing::ElementsAre;

TEST(CMemsetTest, WorksForContainersOfMultiByteType) {
  std::vector<int> v = {1, 2, 3, 4, 5};
  gtl::c_memset(v, 0);
  EXPECT_THAT(v, ElementsAre(0, 0, 0, 0, 0));

  std::vector<int> v2 = {1, 2, 3};
  // 0x01010101 is 16843009
  gtl::c_memset(v2, 0x01);
  EXPECT_THAT(v2, ElementsAre(16843009, 16843009, 16843009));
}

TEST(CMemsetTest, WorksForRvalueSpans) {
  std::vector<int> v = {1, 2, 3, 4, 5};
  gtl::c_memset(absl::MakeSpan(v).subspan(1, 3), 0);
  EXPECT_THAT(v, ElementsAre(1, 0, 0, 0, 5));

  std::string s = "abcde";
  gtl::c_memset(absl::MakeSpan(s).subspan(1, 3), 'x');
  EXPECT_EQ(s, "axxxe");
}

struct TrivialInts {
  int value1;
  int value2;
  union {
    int value3;
    char value4;
  };

  bool operator==(const TrivialInts& other) const {
    return value1 == other.value1 && value2 == other.value2;
  }
};

// TODO: b/528015871 - Add negative tests so that the template doesn't
// instantiate for types that may trigger undefined behavior.
TEST(CMemsetTest, WorksForCustomTrivialType) {
  std::vector<TrivialInts> v = {{1, 2, {.value3 = 3}}, {3, 4, {.value4 = 4}}};
  gtl::c_memset(v, 0x0f);
  EXPECT_THAT(
      v,
      ElementsAre(TrivialInts{0x0f0f0f0f, 0x0f0f0f0f, {.value3 = 0x0f0f0f0f}},
                  TrivialInts{0x0f0f0f0f, 0x0f0f0f0f, {.value4 = 0x0f}}));
}

TEST(CMemsetTest, WorksForContainersOfSingleByteType) {
  std::array<char, 4> a = {'1', '2', '3', '4'};
  gtl::c_memset(a, '9');
  EXPECT_THAT(a, ElementsAre('9', '9', '9', '9'));
}

TEST(CMemsetNTest, WorksForContainersOfMultiByteType) {
  std::vector<int> v = {0x11111111, 0x22222222, 0x33333333};
  // Fills only the first 4 bytes (the first int element)
  gtl::c_memset_n(v, 0, 4);
  EXPECT_THAT(v, ElementsAre(0, 0x22222222, 0x33333333));

  // Fills the first 8 bytes (first two int elements)
  std::vector<int> v2 = {0x11111111, 0x22222222, 0x33333333};
  gtl::c_memset_n(v2, 0, 8);
  EXPECT_THAT(v2, ElementsAre(0, 0, 0x33333333));

  // Fills 0 bytes (no changes)
  std::vector<int> v3 = {0x11111111, 0x22222222, 0x33333333};
  gtl::c_memset_n(v3, 0, 0);
  EXPECT_THAT(v3, ElementsAre(0x11111111, 0x22222222, 0x33333333));
}

TEST(CMemsetNTest, WorksForContainersOfSingleByteType) {
  std::vector<char> v = {'a', 'b', 'c'};
  // Fills front 2 elements:
  gtl::c_memset_n(v, 'x', 2);
  EXPECT_THAT(v, ElementsAre('x', 'x', 'c'));

  // Fills 0 elements:
  std::vector<char> v2 = {'a', 'b', 'c'};
  gtl::c_memset_n(v2, 'x', 0);
  EXPECT_THAT(v2, ElementsAre('a', 'b', 'c'));
}

TEST(CMemsetNTest, WorksForRvalueSpans) {
  std::vector<int> v = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
  gtl::c_memset_n(absl::MakeSpan(v).subspan(1, 3), 0, 4);
  EXPECT_THAT(v, ElementsAre(0x11111111, 0, 0x33333333, 0x44444444));

  std::vector<char> v2 = {'a', 'b', 'c', 'd'};
  gtl::c_memset_n(absl::MakeSpan(v2).subspan(1, 3), 'x', 2);
  EXPECT_THAT(v2, ElementsAre('a', 'x', 'x', 'd'));
}

bool IsHardened() {
  bool hardened = false;
  ABSL_HARDENING_ASSERT([&hardened]() {
    hardened = true;
    return true;
  }());
  return hardened;
}

TEST(CMemsetNDeathTest, CrashesOnOutOfBoundsWrite) {
  // For non-single-byte types, the limit is size in bytes:
  // std::vector<int> size 3 has 3 * sizeof(int) = 12 bytes capacity.
  std::vector<int> vec_int = {1, 2, 3};
  (void)vec_int;

#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    EXPECT_DEATH(gtl::c_memset_n(vec_int, 0, 13), "");
  }
#endif

  // For single-byte types, the limit is element count:
  // std::vector<char> size 3 has limit 3 elements.
  std::vector<char> vec_char = {'a', 'b', 'c'};
  (void)vec_char;

#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    EXPECT_DEATH(gtl::c_memset_n(vec_char, 'x', 4), "");
  }
#endif
}

}  // namespace
