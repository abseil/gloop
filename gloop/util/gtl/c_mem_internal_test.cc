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

#include "gloop/util/gtl/c_mem_internal.h"

#include <array>
#include <string>
#include <type_traits>
#include <vector>

#include "absl/types/span.h"
#include "gtest/gtest.h"

namespace gtl::c_mem_internal {
namespace {

struct NonTrivial {
  std::string s;
};

struct NonAssignableByte {
  const char x;
};
static_assert(std::is_trivial_v<NonAssignableByte>);

TEST(CMemInternal, ElementTypeT) {
  static_assert(std::is_same_v<element_type_t<std::vector<int>>, int>);
  static_assert(std::is_same_v<element_type_t<std::vector<int>&>, int>);
  static_assert(std::is_same_v<element_type_t<std::vector<int>&&>, int>);
  static_assert(
      std::is_same_v<element_type_t<const std::vector<int>>, const int>);
  static_assert(std::is_same_v<element_type_t<absl::Span<int>>, int>);
  static_assert(
      std::is_same_v<element_type_t<absl::Span<const int>>, const int>);
  static_assert(std::is_same_v<element_type_t<int[4]>, int>);
  static_assert(std::is_same_v<element_type_t<const char[4]>, const char>);
  static_assert(std::is_same_v<element_type_t<int[3][2]>, int[2]>);
}

TEST(CMemInternal, IsTrivialDestRange) {
  static_assert(kIsTrivialDestRange<std::vector<int>&>);
  static_assert(kIsTrivialDestRange<std::array<int, 3>&>);
  static_assert(kIsTrivialDestRange<std::string&>);
  static_assert(kIsTrivialDestRange<int (&)[4]>);
  static_assert(kIsTrivialDestRange<absl::Span<char>>);
  static_assert(kIsTrivialDestRange<absl::Span<int>&>);
  static_assert(kIsTrivialDestRange<const absl::Span<int>>);

  // Const elements, rvalue non-spans, multidimensional arrays, and non-trivial
  // element types.
  static_assert(!kIsTrivialDestRange<absl::Span<const int>>);
  static_assert(!kIsTrivialDestRange<const int (&)[4]>);
  static_assert(!kIsTrivialDestRange<std::vector<int>>);
  static_assert(!kIsTrivialDestRange<std::array<int, 3>>);
  static_assert(!kIsTrivialDestRange<int[4]>);
  static_assert(!kIsTrivialDestRange<int (&)[2][2]>);
  static_assert(!kIsTrivialDestRange<std::vector<NonTrivial>&>);
}

TEST(CMemInternal, CanUseCFill) {
  static_assert(kCanUseCFill<std::string&>);
  static_assert(kCanUseCFill<std::vector<char>&>);
  static_assert(kCanUseCFill<absl::Span<char>>);

  static_assert(!kCanUseCFill<std::vector<int>&>);
  static_assert(!kCanUseCFill<absl::Span<const char>>);
  static_assert(!kCanUseCFill<std::array<NonAssignableByte, 3>&>);
}

TEST(CMemInternal, ByteSize) {
  constexpr std::array<int, 3> kArr = {1, 2, 3};
  static_assert(byte_size(kArr) == 3 * sizeof(int));

  constexpr int kRawArr[4] = {1, 2, 3, 4};
  static_assert(byte_size(kRawArr) == 4 * sizeof(int));

  constexpr int kMultiArr[3][2] = {};
  static_assert(byte_size(kMultiArr) == 6 * sizeof(int));

  constexpr std::array<char, 0> kEmpty = {};
  static_assert(byte_size(kEmpty) == 0);
}

}  // namespace
}  // namespace gtl::c_mem_internal
