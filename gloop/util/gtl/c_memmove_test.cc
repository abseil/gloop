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

#include "gloop/util/gtl/c_memmove.h"

#include <array>
#include <vector>

#include "absl/base/macros.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::testing::ElementsAre;

TEST(CMemmoveTest, WorksForContainersOfMultiByteType) {
  std::vector<int> src = {1, 2, 3};
  std::vector<int> dest = {0, 0, 0};
  gtl::c_memmove(dest, src, 3 * sizeof(int));
  EXPECT_THAT(dest, ElementsAre(1, 2, 3));

  // Copies only the first 4 bytes (the first int element).
  std::vector<int> dest2 = {0, 0, 0};
  gtl::c_memmove(dest2, src, sizeof(int));
  EXPECT_THAT(dest2, ElementsAre(1, 0, 0));

  // Copies 0 bytes (no changes).
  std::vector<int> dest3 = {7, 8, 9};
  gtl::c_memmove(dest3, src, 0);
  EXPECT_THAT(dest3, ElementsAre(7, 8, 9));
}

TEST(CMemmoveTest, WorksForArrays) {
  int src[3] = {1, 2, 3};
  int dest[3] = {0, 0, 0};
  gtl::c_memmove(dest, src, 3 * sizeof(int));
  EXPECT_THAT(dest, ElementsAre(1, 2, 3));

  // Copies only the first 4 bytes (the first int element).
  int dest2[3][2] = {{0}};
  gtl::c_memmove(dest2, src, sizeof(src));
  EXPECT_THAT(dest2, ElementsAre(ElementsAre(1, 2), ElementsAre(3, 0),
                                 ElementsAre(0, 0)));
}

TEST(CMemmoveTest, WorksForContainersOfSingleByteType) {
  std::vector<char> src = {'a', 'b', 'c'};
  std::array<char, 3> dest = {'1', '2', '3'};
  gtl::c_memmove(dest, src, 2);
  EXPECT_THAT(dest, ElementsAre('a', 'b', '3'));
}

TEST(CMemmoveTest, WorksForContainersOfDifferentElementTypes) {
  std::vector<char> src = {'a', 'b', 'c', 'd'};
  std::vector<int> dest = {0};
  gtl::c_memmove(dest, src, 4);
  EXPECT_THAT(dest, ElementsAre('a' | ('b' << 8) | ('c' << 16) | ('d' << 24)));
}

TEST(CMemmoveTest, WorksWhenLengthIsElided) {
  std::vector<int> src = {1, 2, 3};
  std::vector<int> dest = {0, 0, 0};
  gtl::c_memmove(dest, src);
  EXPECT_THAT(dest, ElementsAre(1, 2, 3));

  std::vector<char> src2 = {'a', 'b', 'c', 'd'};
  std::vector<int> dest2 = {0};
  gtl::c_memmove(dest2, src2);
  EXPECT_THAT(dest2, ElementsAre('a' | ('b' << 8) | ('c' << 16) | ('d' << 24)));

  std::vector<char> src3 = {'a', 'b', 'c'};
  std::array<char, 3> dest3 = {'1', '2', '3'};
  gtl::c_memmove(dest3, src3);
  EXPECT_THAT(dest3, ElementsAre('a', 'b', 'c'));
}

struct TrivialInts {
  int value1;
  int value2;

  bool operator==(const TrivialInts& other) const {
    return value1 == other.value1 && value2 == other.value2;
  }
};

// TODO: b/528015871 - Add negative tests so that the template doesn't
// instantiate for types that may trigger undefined behavior.
TEST(CMemmoveTest, WorksForCustomTrivialType) {
  std::vector<TrivialInts> src = {{1, 2}, {3, 4}};
  std::vector<TrivialInts> dest = {{0, 0}, {0, 0}};
  gtl::c_memmove(dest, src, 2 * sizeof(TrivialInts));
  EXPECT_THAT(dest, ElementsAre(TrivialInts{1, 2}, TrivialInts{3, 4}));
}

TEST(CMemmoveTest, WorksForRvalueSpans) {
  std::vector<int> v = {1, 2, 3, 4};
  gtl::c_memmove(absl::MakeSpan(v).subspan(2, 2),
                 absl::MakeConstSpan(v).subspan(0, 2), 2 * sizeof(int));
  EXPECT_THAT(v, ElementsAre(1, 2, 1, 2));
}

TEST(CMemmoveTest, WorksForOverlappingRanges) {
  std::vector<int> v = {1, 2, 3, 4};
  // Shifts the last three elements one position to the front.
  gtl::c_memmove(absl::MakeSpan(v), absl::MakeConstSpan(v).subspan(1),
                 3 * sizeof(int));
  EXPECT_THAT(v, ElementsAre(2, 3, 4, 4));

  // Shifts the first three elements one position to the back.
  std::vector<int> v2 = {1, 2, 3, 4};
  gtl::c_memmove(absl::MakeSpan(v2).subspan(1), absl::MakeConstSpan(v2),
                 3 * sizeof(int));
  EXPECT_THAT(v2, ElementsAre(1, 1, 2, 3));
}

bool IsHardened() {
  bool hardened = false;
  ABSL_HARDENING_ASSERT([&hardened]() {
    hardened = true;
    return true;
  }());
  return hardened;
}

TEST(CMemmoveDeathTest, CrashesOnOutOfBoundsWrite) {
  // std::vector<int> of size 3 has 3 * sizeof(int) = 12 bytes capacity.
  std::vector<int> src = {1, 2, 3, 4};
  std::vector<int> dest = {0, 0, 0};
  (void)src;
  (void)dest;

#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    EXPECT_DEATH(gtl::c_memmove(dest, src, 4 * sizeof(int)), "");
  }
#endif
}

TEST(CMemmoveDeathTest, CrashesOnOutOfBoundsRead) {
  std::vector<char> src = {'a', 'b', 'c'};
  std::vector<char> dest = {'1', '2', '3', '4'};
  (void)src;
  (void)dest;

#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    EXPECT_DEATH(gtl::c_memmove(dest, src, 4), "");
  }
#endif
}

TEST(CMemmoveDeathTest, RejectsPartialElementsInDebugBuilds) {
  // Permitted, as with memmove(), but DCHECK-ed as a likely unit mix-up.
  const std::vector<char> src = {1, 2, 3, 4, 5, 6};
  std::vector<int> dest = {0, 0};
  (void)src;
  (void)dest;

#if GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(gtl::c_memmove(dest, src, 2), "not a multiple");
  EXPECT_DEBUG_DEATH(gtl::c_memmove(dest, src), "not a multiple");
#endif
}

}  // namespace
