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

#include "gloop/util/gtl/c_memcpy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gloop/util/gtl/c_mem_internal.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace gtl {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

// The C++ Inliner (<link>) rewrites direct calls to the deprecated
// overloads into absl::c_copy() / absl::c_copy_n(), which would silently drop
// their coverage. It skips type-dependent calls, so the tests reach those
// overloads through these templates. The static_asserts pin which overload a
// call selects; direct c_memcpy() calls below take the memcpy path.
template <typename D, typename S>
void CMemcpyViaCCopy(D&& dest, const S& src) {
  static_assert(c_mem_internal::kCanUseCCopy<D, S>);
  c_memcpy(std::forward<D>(dest), src);
}

template <typename D, typename S>
void CMemcpyViaCCopyN(D&& dest, const S& src, size_t num_bytes) {
  static_assert(c_mem_internal::kCanUseCCopyN<D, S>);
  c_memcpy(std::forward<D>(dest), src, num_bytes);
}

static_assert(
    !c_mem_internal::kCanUseCCopyN<std::vector<int>&, std::vector<int>>);
static_assert(
    !c_mem_internal::kCanUseCCopy<std::vector<uint8_t>&, std::vector<char>>);

TEST(CMemcpy, WorksForContainersOfMultiByteType) {
  std::vector<int> src = {1, 2, 3};
  std::vector<int> dest = {0, 0, 0};
  c_memcpy(dest, src, 3 * sizeof(int));
  EXPECT_THAT(dest, ElementsAre(1, 2, 3));
}

TEST(CMemcpy, CopiesPrefixOfMultiByteContainer) {
  std::vector<int> src = {1, 2, 3};
  std::vector<int> dest = {0, 0, 0};
  c_memcpy(dest, src, sizeof(int));
  EXPECT_THAT(dest, ElementsAre(1, 0, 0));
}

TEST(CMemcpy, CopiesZeroBytesFromNonEmptyContainer) {
  std::vector<int> src = {1, 2, 3};
  std::vector<int> dest = {7, 8, 9};
  c_memcpy(dest, src, 0);
  EXPECT_THAT(dest, ElementsAre(7, 8, 9));
}

TEST(CMemcpy, CopiesPartialElements) {
  const std::vector<char> src = {1, 2, 3, 4};
  std::vector<int> dest = {0};
  c_memcpy(dest, src, 2);
  EXPECT_EQ(dest[0], 1 | (2 << 8));
}

TEST(CMemcpy, WorksForEmptyStdArrayWithExplicitZeroLength) {
  // Empty std::array has non-null data(), so the memcpy path is well-defined.
  std::array<int, 0> src = {};
  std::array<int, 0> dest = {};
  c_memcpy(dest, src, 0);
  EXPECT_THAT(dest, IsEmpty());
}

TEST(CMemcpy, WorksForEmptyStdArrayWhenLengthIsElided) {
  std::array<int, 0> src = {};
  std::vector<uint32_t> dest = {1, 2, 3};
  c_memcpy(dest, src);
  EXPECT_THAT(dest, ElementsAre(1, 2, 3));
}

TEST(CMemcpy, WorksForEmptyVectorsWhenLengthIsElided) {
  // Identical element types delegate to absl::c_copy, which accepts an empty
  // std::vector.
  std::vector<int> src;
  std::vector<int> dest;
  CMemcpyViaCCopy(dest, src);
  EXPECT_THAT(dest, IsEmpty());
}

TEST(CMemcpy, WorksForEmptySingleByteVectorsWithExplicitZeroLength) {
  // Identical 1-byte element types delegate to absl::c_copy_n, which accepts an
  // empty std::vector.
  std::vector<char> src;
  std::vector<char> dest;
  CMemcpyViaCCopyN(dest, src, 0);
  EXPECT_THAT(dest, IsEmpty());
}

TEST(CMemcpy, WorksForCArrayWithExplicitLength) {
  int src[3] = {1, 2, 3};
  int dest[3] = {0, 0, 0};
  c_memcpy(dest, src, 3 * sizeof(int));
  EXPECT_THAT(dest, ElementsAre(1, 2, 3));
}

TEST(CMemcpy, WorksForCArrayWhenLengthIsElided) {
  int src[3] = {1, 2, 3};
  int dest[3] = {0, 0, 0};
  CMemcpyViaCCopy(dest, src);
  EXPECT_THAT(dest, ElementsAre(1, 2, 3));
}

TEST(CMemcpy, WorksForCArrayToStdArray) {
  int src[3] = {1, 2, 3};
  std::array<int, 3> dest = {0, 0, 0};
  CMemcpyViaCCopy(dest, src);
  EXPECT_THAT(dest, ElementsAre(1, 2, 3));
}

TEST(CMemcpy, WorksForMultidimensionalArrayDestinationWithExplicitLength) {
  int src[3] = {1, 2, 3};
  int dest[3][2] = {{0}};
  c_memcpy(dest, src, sizeof(src));
  EXPECT_THAT(dest, ElementsAre(ElementsAre(1, 2), ElementsAre(3, 0),
                                ElementsAre(0, 0)));
}

TEST(CMemcpy, WorksForMultidimensionalArrayDestinationWhenLengthIsElided) {
  int src[3] = {1, 2, 3};
  int dest[3][2] = {{0}};
  c_memcpy(dest, src);
  EXPECT_THAT(dest, ElementsAre(ElementsAre(1, 2), ElementsAre(3, 0),
                                ElementsAre(0, 0)));
}

TEST(CMemcpy, WorksForMultidimensionalArraySource) {
  const int src[2][2] = {{1, 2}, {3, 4}};
  int dest[4] = {0, 0, 0, 0};
  c_memcpy(dest, src);
  EXPECT_THAT(dest, ElementsAre(1, 2, 3, 4));
}

TEST(CMemcpy, WorksForContainersOfIdenticalSingleByteType) {
  // Identical 1-byte element types (delegates to absl::c_copy_n).
  std::vector<char> src = {'a', 'b', 'c'};
  std::array<char, 3> dest = {'1', '2', '3'};
  CMemcpyViaCCopyN(dest, src, 2);
  EXPECT_THAT(dest, ElementsAre('a', 'b', '3'));
}

TEST(CMemcpy, WorksForContainersOfDifferentSingleByteTypes) {
  // Different 1-byte element types (uses memcpy).
  std::vector<uint8_t> src = {'x', 'y', 'z'};
  std::array<char, 3> dest = {'1', '2', '3'};
  c_memcpy(dest, src, 2);
  EXPECT_THAT(dest, ElementsAre('x', 'y', '3'));
}

TEST(CMemcpy, WorksForStringAndStringView) {
  absl::string_view src = "ab";
  std::string dest = "123";
  CMemcpyViaCCopyN(dest, src, 2);
  EXPECT_EQ(dest, "ab3");
}

TEST(CMemcpy, WorksForContainersOfDifferentElementTypes) {
  std::vector<char> src = {'a', 'b', 'c', 'd'};
  std::vector<int> dest = {0};
  c_memcpy(dest, src, 4);
  EXPECT_THAT(dest, ElementsAre('a' | ('b' << 8) | ('c' << 16) | ('d' << 24)));
}

TEST(CMemcpy, WorksWhenLengthIsElidedForMultiByteType) {
  // Identical multi-byte element types (delegates to absl::c_copy).
  std::vector<int> src = {1, 2, 3};
  std::vector<int> dest = {0, 0, 0};
  CMemcpyViaCCopy(dest, src);
  EXPECT_THAT(dest, ElementsAre(1, 2, 3));
}

TEST(CMemcpy, WorksWhenLengthIsElidedForDifferentElementTypes) {
  // Different element types (uses memcpy).
  std::vector<char> src = {'a', 'b', 'c', 'd'};
  std::vector<int> dest = {0};
  c_memcpy(dest, src);
  EXPECT_THAT(dest, ElementsAre('a' | ('b' << 8) | ('c' << 16) | ('d' << 24)));
}

TEST(CMemcpy, WorksWhenLengthIsElidedForSingleByteType) {
  // Identical single-byte element types (delegates to absl::c_copy).
  std::vector<char> src = {'a', 'b', 'c'};
  std::array<char, 3> dest = {'1', '2', '3'};
  CMemcpyViaCCopy(dest, src);
  EXPECT_THAT(dest, ElementsAre('a', 'b', 'c'));
}

struct TrivialInts {
  int value1;
  int value2;

  bool operator==(const TrivialInts& other) const {
    return value1 == other.value1 && value2 == other.value2;
  }
};

TEST(CMemcpy, WorksForCustomTrivialTypeWithExplicitLength) {
  std::vector<TrivialInts> src = {{1, 2}, {3, 4}};
  std::vector<TrivialInts> dest = {{0, 0}, {0, 0}};
  c_memcpy(dest, src, 2 * sizeof(TrivialInts));
  EXPECT_THAT(dest, ElementsAre(TrivialInts{1, 2}, TrivialInts{3, 4}));
}

TEST(CMemcpy, WorksForCustomTrivialTypeWhenLengthIsElided) {
  std::vector<TrivialInts> src = {{1, 2}, {3, 4}};
  std::vector<TrivialInts> dest = {{0, 0}, {0, 0}};
  CMemcpyViaCCopy(dest, src);
  EXPECT_THAT(dest, ElementsAre(TrivialInts{1, 2}, TrivialInts{3, 4}));
}

TEST(CMemcpy, WorksForSpansWithExplicitLength) {
  std::vector<int> v = {1, 2, 3, 4};
  c_memcpy(absl::MakeSpan(v).subspan(2, 2),
           absl::MakeConstSpan(v).subspan(0, 2), 2 * sizeof(int));
  EXPECT_THAT(v, ElementsAre(1, 2, 1, 2));
}

TEST(CMemcpy, WorksForSpansWhenLengthIsElided) {
  std::vector<int> v = {1, 2, 3, 4};
  CMemcpyViaCCopy(absl::MakeSpan(v).subspan(2, 2),
                  absl::MakeConstSpan(v).subspan(0, 2));
  EXPECT_THAT(v, ElementsAre(1, 2, 1, 2));
}

TEST(CMemcpy, WorksForSingleByteSpansWithExplicitLength) {
  std::vector<char> v = {'a', 'b', 'c', 'd'};
  CMemcpyViaCCopyN(absl::MakeSpan(v).subspan(2, 2),
                   absl::MakeConstSpan(v).subspan(0, 2), 2);
  EXPECT_THAT(v, ElementsAre('a', 'b', 'a', 'b'));
}

TEST(CMemcpy, WorksForLvalueSpanDestination) {
  std::vector<int> v = {1, 2, 3, 4};
  absl::Span<int> dest = absl::MakeSpan(v).subspan(2, 2);
  CMemcpyViaCCopy(dest, absl::MakeConstSpan(v).subspan(0, 2));
  EXPECT_THAT(v, ElementsAre(1, 2, 1, 2));
}

template <typename D, typename S, typename = void>
struct CanCMemcpy : std::false_type {};

template <typename D, typename S>
struct CanCMemcpy<
    D, S, std::void_t<decltype(c_memcpy(std::declval<D>(), std::declval<S>()))>>
    : std::true_type {};

template <typename D, typename S, typename = void>
struct CanCMemcpyN : std::false_type {};

template <typename D, typename S>
struct CanCMemcpyN<D, S,
                   std::void_t<decltype(c_memcpy(
                       std::declval<D>(), std::declval<S>(), size_t{0}))>>
    : std::true_type {};

// Validates that destination is a reference or a mutable non-owning type
// (Span).  Copies are not suitable targets since that makes the memcpy a no-op.
TEST(CMemcpy, EnforcesDestinationValueCategory) {
  static_assert(CanCMemcpy<std::vector<int>&, const std::vector<int>&>::value);
  static_assert(CanCMemcpy<std::string&, absl::string_view>::value);
  static_assert(CanCMemcpy<absl::Span<int>, absl::Span<const int>>::value);
  static_assert(CanCMemcpy<absl::Span<int>&, absl::Span<const int>>::value);
  static_assert(
      CanCMemcpy<const absl::Span<int>, absl::Span<const int>>::value);

  // Rvalue non-span destinations are rejected.
  static_assert(!CanCMemcpy<std::vector<int>, const std::vector<int>&>::value);
  static_assert(
      !CanCMemcpy<std::array<int, 3>, const std::vector<int>&>::value);
  static_assert(!CanCMemcpy<std::string, absl::string_view>::value);
  static_assert(!CanCMemcpy<int[3], const std::vector<int>&>::value);

  static_assert(CanCMemcpyN<std::vector<int>&, const std::vector<int>&>::value);
  static_assert(CanCMemcpyN<std::string&, absl::string_view>::value);
  static_assert(CanCMemcpyN<absl::Span<int>, absl::Span<const int>>::value);
  static_assert(!CanCMemcpyN<std::vector<int>, const std::vector<int>&>::value);
  static_assert(!CanCMemcpyN<std::string, absl::string_view>::value);
  static_assert(!CanCMemcpyN<int[3], const std::vector<int>&>::value);
}

bool IsHardened() {
  bool hardened = false;
  ABSL_HARDENING_ASSERT([&hardened]() {
    hardened = true;
    return true;
  }());
  return hardened;
}

TEST(CMemcpyDeathTest, CrashesOnOutOfBoundsWrite) {
#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    std::vector<int> src = {1, 2, 3, 4};
    std::vector<int> dest = {0, 0, 0};
    EXPECT_DEATH(c_memcpy(dest, src, 4 * sizeof(int)), "");
    EXPECT_DEATH(CMemcpyViaCCopy(dest, src), "");
  } else {
    GTEST_SKIP() << "hardening is disabled";
  }
#else
  GTEST_SKIP() << "death tests are not supported";
#endif
}

TEST(CMemcpyDeathTest, CrashesOnOutOfBoundsWriteForDifferentElementTypes) {
#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    std::vector<int> src = {1, 2, 3, 4};
    std::vector<uint32_t> dest = {0, 0, 0};
    EXPECT_DEATH(c_memcpy(dest, src), "");
  } else {
    GTEST_SKIP() << "hardening is disabled";
  }
#else
  GTEST_SKIP() << "death tests are not supported";
#endif
}

TEST(CMemcpyDeathTest, CrashesOnOutOfBoundsWriteForIdenticalSingleByteTypes) {
#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    std::vector<char> src = {'a', 'b', 'c', 'd'};
    std::vector<char> dest = {'1', '2', '3'};
    EXPECT_DEATH(CMemcpyViaCCopyN(dest, src, 4), "");
    EXPECT_DEATH(CMemcpyViaCCopy(dest, src), "");
  } else {
    GTEST_SKIP() << "hardening is disabled";
  }
#else
  GTEST_SKIP() << "death tests are not supported";
#endif
}

TEST(CMemcpyDeathTest, CrashesOnOutOfBoundsWriteForDifferentSingleByteTypes) {
#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    std::vector<char> src = {'a', 'b', 'c', 'd'};
    std::vector<uint8_t> dest = {'1', '2', '3'};
    EXPECT_DEATH(c_memcpy(dest, src, 4), "");
    EXPECT_DEATH(c_memcpy(dest, src), "");
  } else {
    GTEST_SKIP() << "hardening is disabled";
  }
#else
  GTEST_SKIP() << "death tests are not supported";
#endif
}

TEST(CMemcpyDeathTest, CrashesOnOutOfBoundsReadForIdenticalSingleByteTypes) {
#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    std::vector<char> src = {'a', 'b', 'c'};
    std::vector<char> dest = {'1', '2', '3', '4'};
    EXPECT_DEATH(CMemcpyViaCCopyN(dest, src, 4), "");
  } else {
    GTEST_SKIP() << "hardening is disabled";
  }
#else
  GTEST_SKIP() << "death tests are not supported";
#endif
}

TEST(CMemcpyDeathTest, CrashesOnOutOfBoundsReadForDifferentSingleByteTypes) {
#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    std::vector<char> src = {'a', 'b', 'c'};
    std::vector<uint8_t> dest = {'1', '2', '3', '4'};
    EXPECT_DEATH(c_memcpy(dest, src, 4), "");
  } else {
    GTEST_SKIP() << "hardening is disabled";
  }
#else
  GTEST_SKIP() << "death tests are not supported";
#endif
}

TEST(CMemcpyDeathTest, CrashesOnOutOfBoundsReadForMultiByteTypes) {
#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    std::vector<int> src = {1, 2, 3};
    std::vector<int> dest = {0, 0, 0, 0};
    EXPECT_DEATH(c_memcpy(dest, src, 4 * sizeof(int)), "");
  } else {
    GTEST_SKIP() << "hardening is disabled";
  }
#else
  GTEST_SKIP() << "death tests are not supported";
#endif
}

}  // namespace
}  // namespace gtl
