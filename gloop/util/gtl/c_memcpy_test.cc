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

namespace {

using ::gtl::c_memcpy;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

// The C++ Inliner (<link>) rewrites direct calls to the deprecated
// overloads into absl::c_copy() / absl::c_copy_n(), which would silently drop
// their coverage. It skips type-dependent calls, so the tests reach those
// overloads through these templates. The static_asserts pin which overload a
// call selects; direct c_memcpy() calls below take the memcpy path.
template <typename D, typename S>
void CMemcpyViaCCopy(D&& dest, const S& src) {
  static_assert(gtl::c_mem_internal::kCanUseCCopy<D, S>);
  c_memcpy(std::forward<D>(dest), src);
}

template <typename D, typename S>
void CMemcpyViaCCopyN(D&& dest, const S& src, size_t num_bytes) {
  static_assert(gtl::c_mem_internal::kCanUseCCopyN<D, S>);
  c_memcpy(std::forward<D>(dest), src, num_bytes);
}

static_assert(
    !gtl::c_mem_internal::kCanUseCCopyN<std::vector<int>&, std::vector<int>>);
static_assert(!gtl::c_mem_internal::kCanUseCCopy<std::vector<uint8_t>&,
                                                 std::vector<char>>);

TEST(CMemcpyTest, WorksForContainersOfMultiByteType) {
  std::vector<int> src = {1, 2, 3};
  std::vector<int> dest = {0, 0, 0};
  c_memcpy(dest, src, 3 * sizeof(int));
  EXPECT_THAT(dest, ElementsAre(1, 2, 3));

  // Copies only the first 4 bytes (the first int element).
  std::vector<int> dest2 = {0, 0, 0};
  c_memcpy(dest2, src, sizeof(int));
  EXPECT_THAT(dest2, ElementsAre(1, 0, 0));

  // Copies 0 bytes (no changes).
  std::vector<int> dest3 = {7, 8, 9};
  c_memcpy(dest3, src, 0);
  EXPECT_THAT(dest3, ElementsAre(7, 8, 9));
}

TEST(CMemcpyTest, WorksForZeroLengthCopies) {
  // Empty std::array has non-null data(), so the memcpy path is well-defined.
  std::array<int, 0> src = {};
  std::array<int, 0> dest = {};
  c_memcpy(dest, src, 0);
  EXPECT_THAT(dest, IsEmpty());

  std::vector<uint32_t> dest2 = {1, 2, 3};
  c_memcpy(dest2, src);
  EXPECT_THAT(dest2, ElementsAre(1, 2, 3));

  // Identical element types delegate to absl::c_copy/c_copy_n, which accept an
  // empty std::vector.
  std::vector<int> int_src;
  std::vector<int> int_dest;
  CMemcpyViaCCopy(int_dest, int_src);
  EXPECT_THAT(int_dest, IsEmpty());

  std::vector<char> char_src;
  std::vector<char> char_dest;
  CMemcpyViaCCopyN(char_dest, char_src, 0);
  EXPECT_THAT(char_dest, IsEmpty());
}

TEST(CMemcpyTest, WorksForArrays) {
  int src[3] = {1, 2, 3};
  int dest[3] = {0, 0, 0};
  c_memcpy(dest, src, 3 * sizeof(int));
  EXPECT_THAT(dest, ElementsAre(1, 2, 3));

  int dest_full[3] = {0, 0, 0};
  CMemcpyViaCCopy(dest_full, src);
  EXPECT_THAT(dest_full, ElementsAre(1, 2, 3));

  std::array<int, 3> std_dest = {0, 0, 0};
  CMemcpyViaCCopy(std_dest, src);
  EXPECT_THAT(std_dest, ElementsAre(1, 2, 3));

  // Copies 3 ints into a 3x2 multidimensional array.
  int dest2[3][2] = {{0}};
  c_memcpy(dest2, src, sizeof(src));
  EXPECT_THAT(dest2, ElementsAre(ElementsAre(1, 2), ElementsAre(3, 0),
                                 ElementsAre(0, 0)));

  int dest3[3][2] = {{0}};
  c_memcpy(dest3, src);
  EXPECT_THAT(dest3, ElementsAre(ElementsAre(1, 2), ElementsAre(3, 0),
                                 ElementsAre(0, 0)));

  // Copies from a multidimensional array.
  const int multi_src[2][2] = {{1, 2}, {3, 4}};
  int flat_dest[4] = {0, 0, 0, 0};
  c_memcpy(flat_dest, multi_src);
  EXPECT_THAT(flat_dest, ElementsAre(1, 2, 3, 4));
}

TEST(CMemcpyTest, WorksForContainersOfSingleByteType) {
  // Identical 1-byte element types (delegates to absl::c_copy_n).
  std::vector<char> src = {'a', 'b', 'c'};
  std::array<char, 3> dest = {'1', '2', '3'};
  CMemcpyViaCCopyN(dest, src, 2);
  EXPECT_THAT(dest, ElementsAre('a', 'b', '3'));

  // Different 1-byte element types (uses memcpy).
  std::vector<uint8_t> byte_src = {'x', 'y', 'z'};
  std::array<char, 3> char_dest = {'1', '2', '3'};
  c_memcpy(char_dest, byte_src, 2);
  EXPECT_THAT(char_dest, ElementsAre('x', 'y', '3'));

  std::string str_dest = "123";
  CMemcpyViaCCopyN(str_dest, absl::string_view("ab"), 2);
  EXPECT_EQ(str_dest, "ab3");
}

TEST(CMemcpyTest, WorksForContainersOfDifferentElementTypes) {
  std::vector<char> src = {'a', 'b', 'c', 'd'};
  std::vector<int> dest = {0};
  c_memcpy(dest, src, 4);
  EXPECT_THAT(dest, ElementsAre('a' | ('b' << 8) | ('c' << 16) | ('d' << 24)));
}

TEST(CMemcpyTest, WorksWhenLengthIsElided) {
  // Identical multi-byte element types (delegates to absl::c_copy).
  std::vector<int> src = {1, 2, 3};
  std::vector<int> dest = {0, 0, 0};
  CMemcpyViaCCopy(dest, src);
  EXPECT_THAT(dest, ElementsAre(1, 2, 3));

  // Different element types (uses memcpy).
  std::vector<char> src2 = {'a', 'b', 'c', 'd'};
  std::vector<int> dest2 = {0};
  c_memcpy(dest2, src2);
  EXPECT_THAT(dest2, ElementsAre('a' | ('b' << 8) | ('c' << 16) | ('d' << 24)));

  // Identical single-byte element types (delegates to absl::c_copy).
  std::vector<char> src3 = {'a', 'b', 'c'};
  std::array<char, 3> dest3 = {'1', '2', '3'};
  CMemcpyViaCCopy(dest3, src3);
  EXPECT_THAT(dest3, ElementsAre('a', 'b', 'c'));
}

struct TrivialInts {
  int value1;
  int value2;

  bool operator==(const TrivialInts& other) const {
    return value1 == other.value1 && value2 == other.value2;
  }
};

TEST(CMemcpyTest, WorksForCustomTrivialType) {
  std::vector<TrivialInts> src = {{1, 2}, {3, 4}};
  std::vector<TrivialInts> dest = {{0, 0}, {0, 0}};
  c_memcpy(dest, src, 2 * sizeof(TrivialInts));
  EXPECT_THAT(dest, ElementsAre(TrivialInts{1, 2}, TrivialInts{3, 4}));

  std::vector<TrivialInts> dest2 = {{0, 0}, {0, 0}};
  CMemcpyViaCCopy(dest2, src);
  EXPECT_THAT(dest2, ElementsAre(TrivialInts{1, 2}, TrivialInts{3, 4}));
}

TEST(CMemcpyTest, WorksForSpans) {
  std::vector<int> v = {1, 2, 3, 4};
  c_memcpy(absl::MakeSpan(v).subspan(2, 2),
           absl::MakeConstSpan(v).subspan(0, 2), 2 * sizeof(int));
  EXPECT_THAT(v, ElementsAre(1, 2, 1, 2));

  std::vector<int> v2 = {1, 2, 3, 4};
  CMemcpyViaCCopy(absl::MakeSpan(v2).subspan(2, 2),
                  absl::MakeConstSpan(v2).subspan(0, 2));
  EXPECT_THAT(v2, ElementsAre(1, 2, 1, 2));

  std::vector<char> v3 = {'a', 'b', 'c', 'd'};
  CMemcpyViaCCopyN(absl::MakeSpan(v3).subspan(2, 2),
                   absl::MakeConstSpan(v3).subspan(0, 2), 2);
  EXPECT_THAT(v3, ElementsAre('a', 'b', 'a', 'b'));

  std::vector<int> v4 = {1, 2, 3, 4};
  absl::Span<int> lvalue_span = absl::MakeSpan(v4).subspan(2, 2);
  CMemcpyViaCCopy(lvalue_span, absl::MakeConstSpan(v4).subspan(0, 2));
  EXPECT_THAT(v4, ElementsAre(1, 2, 1, 2));
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

TEST(CMemcpyTest, EnforcesDestinationValueCategory) {
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
  // std::vector<int> of size 3 has 3 * sizeof(int) = 12 bytes capacity.
  std::vector<int> src = {1, 2, 3, 4};
  std::vector<int> dest = {0, 0, 0};
  std::vector<uint32_t> dest_diff_type = {0, 0, 0};
  (void)src;
  (void)dest;
  (void)dest_diff_type;

#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    EXPECT_DEATH(c_memcpy(dest, src, 4 * sizeof(int)), "");
    EXPECT_DEATH(CMemcpyViaCCopy(dest, src), "");
    EXPECT_DEATH(c_memcpy(dest_diff_type, src), "");
  }
#endif
}

TEST(CMemcpyDeathTest, CrashesOnOutOfBoundsWriteForSingleByteTypes) {
  std::vector<char> src = {'a', 'b', 'c', 'd'};
  std::vector<char> dest = {'1', '2', '3'};
  std::vector<uint8_t> dest_diff_type = {'1', '2', '3'};
  (void)src;
  (void)dest;
  (void)dest_diff_type;

#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    EXPECT_DEATH(CMemcpyViaCCopyN(dest, src, 4), "");
    EXPECT_DEATH(CMemcpyViaCCopy(dest, src), "");
    EXPECT_DEATH(c_memcpy(dest_diff_type, src, 4), "");
    EXPECT_DEATH(c_memcpy(dest_diff_type, src), "");
  }
#endif
}

TEST(CMemcpyDeathTest, CrashesOnOutOfBoundsRead) {
  std::vector<char> src = {'a', 'b', 'c'};
  std::vector<char> dest = {'1', '2', '3', '4'};
  std::vector<uint8_t> dest_diff_type = {'1', '2', '3', '4'};
  std::vector<int> src_int = {1, 2, 3};
  std::vector<int> dest_int = {0, 0, 0, 0};
  (void)src;
  (void)dest;
  (void)dest_diff_type;
  (void)src_int;
  (void)dest_int;

#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    EXPECT_DEATH(CMemcpyViaCCopyN(dest, src, 4), "");
    EXPECT_DEATH(c_memcpy(dest_diff_type, src, 4), "");
    EXPECT_DEATH(c_memcpy(dest_int, src_int, 4 * sizeof(int)), "");
  }
#endif
}

TEST(CMemcpyDeathTest, RejectsPartialElementsInDebugBuilds) {
  // Permitted, as with memcpy(), but DCHECK-ed as a likely unit mix-up.
  const std::vector<char> src = {1, 2, 3, 4, 5, 6};
  std::vector<int> dest = {0, 0};
  (void)src;
  (void)dest;

#if GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(c_memcpy(dest, src, 2), "not a multiple");
  EXPECT_DEBUG_DEATH(c_memcpy(dest, src), "not a multiple");
#endif
}

}  // namespace
