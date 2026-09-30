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

#include "gloop/util/gtl/c_memcmp.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gloop/util/gtl/internal/is_hardened.h"
#include "gtest/gtest.h"

namespace gtl {
namespace {

using ::gtl::internal::IsHardened;
TEST(CMemcmp, WorksForContainersOfMultiByteType) {
  std::vector<int> lhs = {1, 2, 3};
  std::vector<int> rhs = {1, 2, 3};
  EXPECT_EQ(c_memcmp(lhs, rhs, 3 * sizeof(int)), 0);
  EXPECT_EQ(c_memcmp_n(lhs, rhs, 3 * sizeof(int)), 0);

  std::vector<int> greater = {1, 2, 4};
  EXPECT_LT(c_memcmp(lhs, greater, 3 * sizeof(int)), 0);
  EXPECT_GT(c_memcmp(greater, lhs, 3 * sizeof(int)), 0);
  EXPECT_LT(c_memcmp_n(lhs, greater, 3 * sizeof(int)), 0);
  EXPECT_GT(c_memcmp_n(greater, lhs, 3 * sizeof(int)), 0);
}

TEST(CMemcmp, ComparesPrefixOfMultiByteContainer) {
  std::vector<int> lhs = {1, 2, 3};
  std::vector<int> rhs = {1, 9, 9};
  EXPECT_EQ(c_memcmp(lhs, rhs, sizeof(int)), 0);
  EXPECT_LT(c_memcmp(lhs, rhs, 2 * sizeof(int)), 0);
  EXPECT_EQ(c_memcmp_n(lhs, rhs, sizeof(int)), 0);
  EXPECT_LT(c_memcmp_n(lhs, rhs, 2 * sizeof(int)), 0);
}

TEST(CMemcmp, ComparesZeroBytesFromNonEmptyContainer) {
  std::vector<int> lhs = {1, 2, 3};
  std::vector<int> rhs = {7, 8, 9};
  EXPECT_EQ(c_memcmp(lhs, rhs, 0), 0);
  EXPECT_EQ(c_memcmp_n(lhs, rhs, 0), 0);
}

TEST(CMemcmp, ComparesPartialElements) {
  const std::vector<int> lhs = {1 | (2 << 8)};
  const std::vector<char> rhs = {1, 2, 3, 4};
  EXPECT_EQ(c_memcmp(lhs, rhs, 2), 0);
  EXPECT_LT(c_memcmp(lhs, rhs, 3), 0);
  EXPECT_EQ(c_memcmp_n(lhs, rhs, 2), 0);
  EXPECT_LT(c_memcmp_n(lhs, rhs, 3), 0);
}

TEST(CMemcmp, ComparesBytesAsUnsigned) {
  std::vector<char> lhs = {'\x01'};
  std::vector<char> rhs = {'\xff'};
  EXPECT_LT(c_memcmp(lhs, rhs, 1), 0);
  EXPECT_GT(c_memcmp(rhs, lhs, 1), 0);
  EXPECT_LT(c_memcmp_n(lhs, rhs, 1), 0);
  EXPECT_GT(c_memcmp_n(rhs, lhs, 1), 0);
}

TEST(CMemcmp, WorksForEmptyStdArrayWithExplicitZeroLength) {
  // Empty std::array has non-null data(), so the memcmp path is well-defined.
  std::array<int, 0> lhs = {};
  std::array<int, 0> rhs = {};
  EXPECT_EQ(c_memcmp(lhs, rhs, 0), 0);
  EXPECT_EQ(c_memcmp_n(lhs, rhs, 0), 0);
}

TEST(CMemcmp, WorksForEmptyStdArrayWhenLengthIsElided) {
  std::vector<uint32_t> lhs = {1, 2, 3};
  std::array<int, 0> rhs = {};
  EXPECT_EQ(c_memcmp(lhs, rhs), 0);
}

TEST(CMemcmp, WorksForCArrayWithExplicitLength) {
  int lhs[3] = {1, 2, 3};
  int rhs[3] = {1, 2, 3};
  EXPECT_EQ(c_memcmp(lhs, rhs, 3 * sizeof(int)), 0);
  EXPECT_EQ(c_memcmp_n(lhs, rhs, 3 * sizeof(int)), 0);
}

TEST(CMemcmp, WorksForCArrayWhenLengthIsElided) {
  int lhs[3] = {1, 2, 3};
  int rhs[3] = {1, 2, 4};
  EXPECT_LT(c_memcmp(lhs, rhs), 0);
}

TEST(CMemcmp, WorksForCArrayAndStdArray) {
  int lhs[3] = {1, 2, 3};
  std::array<int, 3> rhs = {1, 2, 3};
  EXPECT_EQ(c_memcmp(lhs, rhs), 0);
}

TEST(CMemcmp, WorksForMultidimensionalArrayLhsWithExplicitLength) {
  int lhs[3][2] = {{1, 2}, {3, 0}, {0, 0}};
  int rhs[3] = {1, 2, 3};
  EXPECT_EQ(c_memcmp(lhs, rhs, sizeof(rhs)), 0);
  EXPECT_EQ(c_memcmp_n(lhs, rhs, sizeof(rhs)), 0);
}

TEST(CMemcmp, WorksForMultidimensionalArrayLhsWhenLengthIsElided) {
  int lhs[3][2] = {{1, 2}, {3, 0}, {0, 0}};
  int rhs[3] = {1, 2, 3};
  EXPECT_EQ(c_memcmp(lhs, rhs), 0);
}

TEST(CMemcmp, WorksForMultidimensionalArrayRhs) {
  int lhs[4] = {1, 2, 3, 4};
  const int rhs[2][2] = {{1, 2}, {3, 4}};
  EXPECT_EQ(c_memcmp(lhs, rhs), 0);
}

TEST(CMemcmp, WorksForContainersOfIdenticalSingleByteType) {
  std::array<char, 3> lhs = {'a', 'b', '3'};
  std::vector<char> rhs = {'a', 'b', 'c'};
  EXPECT_EQ(c_memcmp(lhs, rhs, 2), 0);
  EXPECT_LT(c_memcmp(lhs, rhs, 3), 0);
  EXPECT_EQ(c_memcmp_n(lhs, rhs, 2), 0);
  EXPECT_LT(c_memcmp_n(lhs, rhs, 3), 0);
}

TEST(CMemcmp, WorksForContainersOfDifferentSingleByteTypes) {
  std::array<char, 3> lhs = {'x', 'y', '3'};
  std::vector<uint8_t> rhs = {'x', 'y', 'z'};
  EXPECT_EQ(c_memcmp(lhs, rhs, 2), 0);
  EXPECT_LT(c_memcmp(lhs, rhs, 3), 0);
  EXPECT_EQ(c_memcmp_n(lhs, rhs, 2), 0);
  EXPECT_LT(c_memcmp_n(lhs, rhs, 3), 0);
}

TEST(CMemcmp, WorksForStringAndStringView) {
  std::string lhs = "ab3";
  absl::string_view rhs = "ab";
  EXPECT_EQ(c_memcmp(lhs, rhs, 2), 0);
  EXPECT_EQ(c_memcmp_n(lhs, rhs, 2), 0);
  EXPECT_EQ(c_memcmp(lhs, rhs), 0);
}

TEST(CMemcmp, WorksForContainersOfDifferentElementTypes) {
  std::vector<int> lhs = {'a' | ('b' << 8) | ('c' << 16) | ('d' << 24)};
  std::vector<char> rhs = {'a', 'b', 'c', 'd'};
  EXPECT_EQ(c_memcmp(lhs, rhs, 4), 0);
  EXPECT_EQ(c_memcmp_n(lhs, rhs, 4), 0);
}

TEST(CMemcmp, WorksWhenLengthIsElidedForMultiByteType) {
  std::vector<int> lhs = {1, 2, 3};
  std::vector<int> rhs = {1, 2, 3};
  EXPECT_EQ(c_memcmp(lhs, rhs), 0);
}

TEST(CMemcmp, WorksWhenLengthIsElidedForDifferentElementTypes) {
  std::vector<int> lhs = {'a' | ('b' << 8) | ('c' << 16) | ('d' << 24)};
  std::vector<char> rhs = {'a', 'b', 'c', 'd'};
  EXPECT_EQ(c_memcmp(lhs, rhs), 0);
}

TEST(CMemcmp, WorksWhenLengthIsElidedForSingleByteType) {
  std::array<char, 3> lhs = {'a', 'b', 'c'};
  std::vector<char> rhs = {'a', 'b', 'c'};
  EXPECT_EQ(c_memcmp(lhs, rhs), 0);
}

struct TrivialInts {
  int value1;
  int value2;
};

TEST(CMemcmp, WorksForCustomTrivialTypeWithExplicitLength) {
  std::vector<TrivialInts> lhs = {{1, 2}, {3, 4}};
  std::vector<TrivialInts> rhs = {{1, 2}, {3, 4}};
  EXPECT_EQ(c_memcmp(lhs, rhs, 2 * sizeof(TrivialInts)), 0);
  EXPECT_EQ(c_memcmp_n(lhs, rhs, 2 * sizeof(TrivialInts)), 0);
}

TEST(CMemcmp, WorksForCustomTrivialTypeWhenLengthIsElided) {
  std::vector<TrivialInts> lhs = {{1, 2}, {3, 4}};
  std::vector<TrivialInts> rhs = {{1, 2}, {3, 4}};
  EXPECT_EQ(c_memcmp(lhs, rhs), 0);
}

struct TriviallyCopyableNonTrivial {
  int value;
  explicit TriviallyCopyableNonTrivial(int v) : value(v) {}
};
static_assert(!std::is_trivial_v<TriviallyCopyableNonTrivial>);
static_assert(std::is_trivially_copyable_v<TriviallyCopyableNonTrivial>);

TEST(CMemcmp, WorksForTriviallyCopyableNonTrivialType) {
  std::vector<TriviallyCopyableNonTrivial> lhs = {
      TriviallyCopyableNonTrivial(1), TriviallyCopyableNonTrivial(2)};
  std::vector<TriviallyCopyableNonTrivial> rhs = {
      TriviallyCopyableNonTrivial(1), TriviallyCopyableNonTrivial(2)};
  EXPECT_EQ(c_memcmp(lhs, rhs), 0);
  EXPECT_EQ(c_memcmp_n(lhs, rhs, 2 * sizeof(TriviallyCopyableNonTrivial)), 0);
}

TEST(CMemcmp, WorksForSpansWithExplicitLength) {
  std::vector<int> v = {1, 2, 1, 2};
  EXPECT_EQ(c_memcmp(absl::MakeConstSpan(v).subspan(2, 2),
                     absl::MakeConstSpan(v).subspan(0, 2), 2 * sizeof(int)),
            0);
  EXPECT_EQ(c_memcmp_n(absl::MakeConstSpan(v).subspan(2, 2),
                       absl::MakeConstSpan(v).subspan(0, 2), 2 * sizeof(int)),
            0);
}

TEST(CMemcmp, WorksForSpansWhenLengthIsElided) {
  std::vector<int> v = {1, 2, 1, 2};
  EXPECT_EQ(c_memcmp(absl::MakeConstSpan(v).subspan(2, 2),
                     absl::MakeConstSpan(v).subspan(0, 2)),
            0);
}

TEST(CMemcmp, WorksForSingleByteSpansWithExplicitLength) {
  std::vector<char> v = {'a', 'b', 'a', 'b'};
  EXPECT_EQ(c_memcmp(absl::MakeConstSpan(v).subspan(2, 2),
                     absl::MakeConstSpan(v).subspan(0, 2), 2),
            0);
  EXPECT_EQ(c_memcmp_n(absl::MakeConstSpan(v).subspan(2, 2),
                       absl::MakeConstSpan(v).subspan(0, 2), 2),
            0);
}

TEST(CMemcmp, WorksForOverlappingRanges) {
  std::vector<int> v = {1, 1, 1, 2};
  EXPECT_EQ(c_memcmp(absl::MakeConstSpan(v).subspan(0, 2),
                     absl::MakeConstSpan(v).subspan(1, 2), 2 * sizeof(int)),
            0);
  EXPECT_LT(c_memcmp(absl::MakeConstSpan(v).subspan(0, 3),
                     absl::MakeConstSpan(v).subspan(1, 3), 3 * sizeof(int)),
            0);
  EXPECT_EQ(c_memcmp_n(absl::MakeConstSpan(v).subspan(0, 2),
                       absl::MakeConstSpan(v).subspan(1, 2), 2 * sizeof(int)),
            0);
  EXPECT_LT(c_memcmp_n(absl::MakeConstSpan(v).subspan(0, 3),
                       absl::MakeConstSpan(v).subspan(1, 3), 3 * sizeof(int)),
            0);
}

template <typename L, typename R, typename = void>
struct CanCMemcmp : std::false_type {};

template <typename L, typename R>
struct CanCMemcmp<
    L, R, std::void_t<decltype(c_memcmp(std::declval<L>(), std::declval<R>()))>>
    : std::true_type {};

template <typename L, typename R, typename = void>
struct CanCMemcmp3 : std::false_type {};

template <typename L, typename R>
struct CanCMemcmp3<L, R,
                   std::void_t<decltype(c_memcmp(
                       std::declval<L>(), std::declval<R>(), size_t{0}))>>
    : std::true_type {};

template <typename L, typename R, typename = void>
struct CanCMemcmpN : std::false_type {};

template <typename L, typename R>
struct CanCMemcmpN<L, R,
                   std::void_t<decltype(c_memcmp_n(
                       std::declval<L>(), std::declval<R>(), size_t{0}))>>
    : std::true_type {};

// Validates that both const and non-const lvalue/rvalue containers and spans
// are accepted since c_memcmp and c_memcmp_n only read from both inputs.
TEST(CMemcmp, AcceptsConstAndRvalueContainers) {
  static_assert(
      CanCMemcmp<const std::vector<int>&, const std::vector<int>&>::value);
  static_assert(CanCMemcmp<std::vector<int>&, std::vector<int>&>::value);
  static_assert(CanCMemcmp<std::vector<int>, std::vector<int>>::value);
  static_assert(CanCMemcmp<std::array<int, 3>, std::array<int, 3>>::value);
  static_assert(CanCMemcmp<std::string, absl::string_view>::value);
  static_assert(
      CanCMemcmp<absl::Span<const int>, absl::Span<const int>>::value);
  static_assert(CanCMemcmp<absl::Span<int>, absl::Span<int>>::value);
  static_assert(CanCMemcmp<const int (&)[3], const int (&)[3]>::value);

  static_assert(
      CanCMemcmp3<const std::vector<int>&, const std::vector<int>&>::value);
  static_assert(CanCMemcmp3<std::vector<int>, std::vector<int>>::value);
  static_assert(CanCMemcmp3<std::string, absl::string_view>::value);
  static_assert(
      CanCMemcmp3<absl::Span<const int>, absl::Span<const int>>::value);
  static_assert(CanCMemcmp3<const int (&)[3], const int (&)[3]>::value);

  static_assert(
      CanCMemcmpN<const std::vector<int>&, const std::vector<int>&>::value);
  static_assert(CanCMemcmpN<std::vector<int>, std::vector<int>>::value);
  static_assert(CanCMemcmpN<std::string, absl::string_view>::value);
  static_assert(
      CanCMemcmpN<absl::Span<const int>, absl::Span<const int>>::value);
  static_assert(CanCMemcmpN<const int (&)[3], const int (&)[3]>::value);
}

TEST(CMemcmpDeathTest, CrashesOnOutOfBoundsReadLhs) {
#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    std::vector<int> lhs = {1, 2, 3};
    std::vector<int> rhs = {1, 2, 3, 4};
    EXPECT_DEATH(c_memcmp(lhs, rhs, 4 * sizeof(int)), "");
    EXPECT_DEATH(c_memcmp_n(lhs, rhs, 4 * sizeof(int)), "");
    EXPECT_DEATH(c_memcmp(lhs, rhs), "");
  } else {
    GTEST_SKIP() << "hardening is disabled";
  }
#else
  GTEST_SKIP() << "death tests are not supported";
#endif
}

TEST(CMemcmpDeathTest, CrashesOnOutOfBoundsReadLhsForDifferentElementTypes) {
#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    std::vector<uint32_t> lhs = {1, 2, 3};
    std::vector<int> rhs = {1, 2, 3, 4};
    EXPECT_DEATH(c_memcmp(lhs, rhs), "");
  } else {
    GTEST_SKIP() << "hardening is disabled";
  }
#else
  GTEST_SKIP() << "death tests are not supported";
#endif
}

TEST(CMemcmpDeathTest, CrashesOnOutOfBoundsReadLhsForSingleByteTypes) {
#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    std::vector<char> lhs = {'1', '2', '3'};
    std::vector<char> rhs = {'a', 'b', 'c', 'd'};
    EXPECT_DEATH(c_memcmp(lhs, rhs, 4), "");
    EXPECT_DEATH(c_memcmp_n(lhs, rhs, 4), "");
    EXPECT_DEATH(c_memcmp(lhs, rhs), "");
  } else {
    GTEST_SKIP() << "hardening is disabled";
  }
#else
  GTEST_SKIP() << "death tests are not supported";
#endif
}

TEST(CMemcmpDeathTest, CrashesOnOutOfBoundsReadRhsForMultiByteTypes) {
#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    std::vector<int> lhs = {1, 2, 3, 4};
    std::vector<int> rhs = {1, 2, 3};
    EXPECT_DEATH(c_memcmp(lhs, rhs, 4 * sizeof(int)), "");
    EXPECT_DEATH(c_memcmp_n(lhs, rhs, 4 * sizeof(int)), "");
  } else {
    GTEST_SKIP() << "hardening is disabled";
  }
#else
  GTEST_SKIP() << "death tests are not supported";
#endif
}

TEST(CMemcmpDeathTest, CrashesOnOutOfBoundsReadRhsForSingleByteTypes) {
#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    std::vector<char> lhs = {'1', '2', '3', '4'};
    std::vector<char> rhs = {'a', 'b', 'c'};
    EXPECT_DEATH(c_memcmp(lhs, rhs, 4), "");
    EXPECT_DEATH(c_memcmp_n(lhs, rhs, 4), "");
  } else {
    GTEST_SKIP() << "hardening is disabled";
  }
#else
  GTEST_SKIP() << "death tests are not supported";
#endif
}

}  // namespace
}  // namespace gtl
