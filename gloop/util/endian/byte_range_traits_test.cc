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

#include "gloop/util/endian/byte_range_traits.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gtest/gtest.h"

namespace endian_internal {
namespace {

// Multi-field trivially copyable element type.
struct TriviallyCopyable {
  uint32_t a;
  uint16_t b;
};

// Element type with a user-provided destructor (not trivially copyable).
struct NonTriviallyCopyable {
  ~NonTriviallyCopyable() = default;
  uint32_t a = 0;
};

// Unrelated type that is not convertible to size_t.
struct Unrelated {};

// Range-like type with data() but no size().
struct OnlyData {
  const uint8_t* data() const { return nullptr; }
};

// Range-like type with size() but no data().
struct OnlySize {
  size_t size() const { return 0; }
};

// Range-like type whose data() does not return a pointer.
struct NonPointerData {
  int data() const { return 0; }
  size_t size() const { return 0; }
};

// Range-like type whose size() is not convertible to size_t.
struct NonConvertibleSize {
  const uint8_t* data() const { return nullptr; }
  Unrelated size() const { return {}; }
};

// Range type whose data() is only available on non-const lvalues.
struct NonConstOnlyRange {
  uint8_t* data() { return nullptr; }
  size_t size() const { return 0; }
};

// Range type whose size() is only available on non-const lvalues.
struct NonConstSizeRange {
  const uint8_t* data() const { return nullptr; }
  size_t size() { return 0; }
};

// Range type whose data() returns const void* (incomplete element type).
struct VoidDataRange {
  const void* data() const { return nullptr; }
  size_t size() const { return 0; }
};

// Range type whose data() returns void* (incomplete element type).
struct MutableVoidDataRange {
  void* data() const { return nullptr; }
  size_t size() const { return 0; }
};

TEST(ByteRangeTraits, HasDataAndSize) {
  static_assert(HasDataAndSize<absl::string_view>::value);
  static_assert(HasDataAndSize<const absl::string_view&>::value);
  static_assert(HasDataAndSize<absl::Span<uint8_t>>::value);
  static_assert(HasDataAndSize<absl::Span<const uint8_t>>::value);
  static_assert(HasDataAndSize<std::string>::value);
  static_assert(HasDataAndSize<const std::string&>::value);
  static_assert(HasDataAndSize<std::vector<uint8_t>>::value);
  static_assert(HasDataAndSize<const std::vector<uint32_t>&>::value);
  static_assert(HasDataAndSize<std::array<uint8_t, 4>>::value);
  static_assert(HasDataAndSize<uint8_t (&)[4]>::value);
  static_assert(HasDataAndSize<const char (&)[8]>::value);
  static_assert(HasDataAndSize<NonConstOnlyRange&>::value);

  static_assert(!HasDataAndSize<int>::value);
  static_assert(!HasDataAndSize<void*>::value);
  static_assert(!HasDataAndSize<const char*>::value);
  static_assert(!HasDataAndSize<uint8_t[4]>::value);
  static_assert(!HasDataAndSize<std::vector<bool>>::value);
  static_assert(!HasDataAndSize<OnlyData>::value);
  static_assert(!HasDataAndSize<OnlySize>::value);
  static_assert(!HasDataAndSize<NonPointerData>::value);
  static_assert(!HasDataAndSize<NonConvertibleSize>::value);
  static_assert(!HasDataAndSize<const NonConstOnlyRange&>::value);
  static_assert(!HasDataAndSize<NonConstSizeRange&>::value);
}

TEST(ByteRangeTraits, IsReadableByteRange) {
  static_assert(IsReadableByteRange<absl::string_view>::value);
  static_assert(IsReadableByteRange<const absl::string_view&>::value);
  static_assert(IsReadableByteRange<absl::string_view&>::value);
  static_assert(IsReadableByteRange<absl::Span<const uint8_t>>::value);
  static_assert(IsReadableByteRange<absl::Span<uint8_t>>::value);
  static_assert(IsReadableByteRange<const absl::Span<uint8_t>&>::value);
  static_assert(IsReadableByteRange<std::string>::value);
  static_assert(IsReadableByteRange<const std::string&>::value);
  static_assert(IsReadableByteRange<std::string&>::value);
  static_assert(IsReadableByteRange<std::string&&>::value);
  static_assert(IsReadableByteRange<std::vector<uint8_t>>::value);
  static_assert(IsReadableByteRange<const std::vector<uint8_t>&>::value);
  static_assert(IsReadableByteRange<std::vector<uint32_t>>::value);
  static_assert(IsReadableByteRange<std::array<char, 8>>::value);
  static_assert(IsReadableByteRange<const std::array<uint64_t, 2>&>::value);
  static_assert(
      IsReadableByteRange<absl::Span<const TriviallyCopyable>>::value);
  static_assert(IsReadableByteRange<std::vector<TriviallyCopyable>>::value);

  // Non-ranges and non-const data()/size().
  static_assert(!IsReadableByteRange<int>::value);
  static_assert(!IsReadableByteRange<NonConstOnlyRange>::value);
  static_assert(!IsReadableByteRange<NonConstOnlyRange&>::value);

  // C arrays and raw pointers are excluded so dedicated overloads match
  // instead.
  static_assert(!IsReadableByteRange<uint8_t[4]>::value);
  static_assert(!IsReadableByteRange<uint8_t (&)[4]>::value);
  static_assert(!IsReadableByteRange<const char (&)[8]>::value);
  static_assert(!IsReadableByteRange<void*>::value);
  static_assert(!IsReadableByteRange<const void*>::value);
  static_assert(!IsReadableByteRange<uint8_t*>::value);
  static_assert(!IsReadableByteRange<const uint8_t*>::value);
  static_assert(!IsReadableByteRange<uint8_t*&>::value);

  // Disallowed element types: void, pointers, arrays, non-trivially-copyable.
  static_assert(!IsReadableByteRange<VoidDataRange>::value);
  static_assert(!IsReadableByteRange<std::vector<int*>>::value);
  static_assert(!IsReadableByteRange<absl::Span<const char*>>::value);
  static_assert(!IsReadableByteRange<std::array<void*, 2>>::value);
  static_assert(!IsReadableByteRange<std::array<uint8_t[4], 2>>::value);
  static_assert(!IsReadableByteRange<absl::Span<const uint8_t[4]>>::value);
  static_assert(!IsReadableByteRange<std::vector<std::string>>::value);
  static_assert(!IsReadableByteRange<absl::Span<const std::string>>::value);
  static_assert(!IsReadableByteRange<std::vector<NonTriviallyCopyable>>::value);
}

TEST(ByteRangeTraits, IsWritableByteRange) {
  // Mutable lvalue containers.
  static_assert(IsWritableByteRange<std::vector<uint8_t>&>::value);
  static_assert(IsWritableByteRange<std::string&>::value);
  static_assert(IsWritableByteRange<std::array<uint8_t, 8>&>::value);
  static_assert(IsWritableByteRange<std::array<uint32_t, 2>&>::value);
  static_assert(IsWritableByteRange<std::vector<TriviallyCopyable>&>::value);
  static_assert(IsWritableByteRange<NonConstOnlyRange&>::value);

  // Mutable spans (lvalue, const lvalue, and rvalue).
  static_assert(IsWritableByteRange<absl::Span<uint8_t>>::value);
  static_assert(IsWritableByteRange<const absl::Span<uint8_t>>::value);
  static_assert(IsWritableByteRange<absl::Span<uint8_t>&>::value);
  static_assert(IsWritableByteRange<const absl::Span<uint8_t>&>::value);
  static_assert(IsWritableByteRange<absl::Span<uint32_t>>::value);
  static_assert(IsWritableByteRange<absl::Span<TriviallyCopyable>>::value);

  // Const containers and read-only views are rejected.
  static_assert(!IsWritableByteRange<const std::vector<uint8_t>&>::value);
  static_assert(!IsWritableByteRange<const std::string&>::value);
  static_assert(!IsWritableByteRange<const std::array<uint8_t, 8>&>::value);
  static_assert(!IsWritableByteRange<absl::string_view>::value);
  static_assert(!IsWritableByteRange<absl::string_view&>::value);
  static_assert(!IsWritableByteRange<const absl::string_view&>::value);
  static_assert(!IsWritableByteRange<absl::Span<const uint8_t>>::value);
  static_assert(!IsWritableByteRange<absl::Span<const uint8_t>&>::value);
  static_assert(!IsWritableByteRange<const absl::Span<const uint8_t>&>::value);

  // Rvalue/temporary owning containers are rejected.
  static_assert(!IsWritableByteRange<std::vector<uint8_t>>::value);
  static_assert(!IsWritableByteRange<std::vector<uint8_t>&&>::value);
  static_assert(!IsWritableByteRange<std::string>::value);
  static_assert(!IsWritableByteRange<std::string&&>::value);
  static_assert(!IsWritableByteRange<std::array<uint8_t, 8>>::value);
  static_assert(!IsWritableByteRange<std::array<uint8_t, 8>&&>::value);
  static_assert(!IsWritableByteRange<NonConstOnlyRange>::value);

  // C arrays and raw pointers are excluded.
  static_assert(!IsWritableByteRange<uint8_t[4]>::value);
  static_assert(!IsWritableByteRange<uint8_t (&)[4]>::value);
  static_assert(!IsWritableByteRange<void*>::value);
  static_assert(!IsWritableByteRange<uint8_t*>::value);
  static_assert(!IsWritableByteRange<char*&>::value);

  // Disallowed element types: void, pointers, arrays, non-trivially-copyable.
  static_assert(!IsWritableByteRange<MutableVoidDataRange>::value);
  static_assert(!IsWritableByteRange<MutableVoidDataRange&>::value);
  static_assert(!IsWritableByteRange<std::vector<int*>&>::value);
  static_assert(!IsWritableByteRange<absl::Span<int*>>::value);
  static_assert(!IsWritableByteRange<std::array<uint8_t[4], 2>&>::value);
  static_assert(!IsWritableByteRange<absl::Span<uint8_t[4]>>::value);
  static_assert(!IsWritableByteRange<std::vector<std::string>&>::value);
  static_assert(!IsWritableByteRange<absl::Span<std::string>>::value);
  static_assert(
      !IsWritableByteRange<std::vector<NonTriviallyCopyable>&>::value);
  static_assert(!IsWritableByteRange<absl::Span<NonTriviallyCopyable>>::value);
}

TEST(ByteRangeTraits, ByteSize) {
  constexpr std::array<uint8_t, 5> kBytes = {};
  constexpr std::array<uint32_t, 3> kWords = {};
  constexpr std::array<uint64_t, 0> kEmpty = {};
  constexpr absl::string_view kView = "abcdef";

  static_assert(noexcept(ByteSize(kBytes)));
  static_assert(ByteSize(kBytes) == 5);
  static_assert(ByteSize(kWords) == 12);
  static_assert(ByteSize(kEmpty) == 0);
  static_assert(ByteSize(kView) == 6);

  std::vector<uint16_t> u16_vec(7);
  EXPECT_EQ(ByteSize(u16_vec), 7 * sizeof(uint16_t));

  std::vector<TriviallyCopyable> struct_vec(4);
  EXPECT_EQ(ByteSize(struct_vec), 4 * sizeof(TriviallyCopyable));
  EXPECT_EQ(ByteSize(absl::MakeConstSpan(struct_vec).subspan(1, 2)),
            2 * sizeof(TriviallyCopyable));
  EXPECT_EQ(ByteSize(absl::Span<const uint64_t>()), 0);
}

}  // namespace
}  // namespace endian_internal
