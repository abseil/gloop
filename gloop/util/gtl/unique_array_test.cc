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

#include "gloop/util/gtl/unique_array.h"

#include <array>
#include <cstddef>
#include <iterator>
#include <memory>
#include <numeric>
#include <optional>
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/internal/hardening.h"
#include "absl/base/optimization.h"
#include "absl/base/options.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace gtl {
namespace {

using ::testing::ContainsRegex;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::MatchesRegex;
using ::testing::Not;
using ::testing::SizeIs;

static constexpr size_t kArraySize = 16;
static constexpr int kDeleterSignature = 0x12345;

struct CustomDeleter {
  static int signature;

  CustomDeleter() { signature = 0; }

  void operator()(int* ptr) {
    EXPECT_THAT(ptr, Not(IsNull()));
    signature = kDeleterSignature;
    delete[] ptr;
  }
};

int CustomDeleter::signature = 0;

class DeleterWithNoDefaultCtor {
 public:
  explicit DeleterWithNoDefaultCtor(int data) : data_(data) {
    (void)data_;  // Unused.
  }

  void operator()(int* ptr) { delete[] ptr; }

  int GetData() { return data_; }

 private:
  int data_;
};

TEST(UniqueArrayTest, MakeUniqueArray) {
  auto buffer = MakeUniqueArray<int>(kArraySize);
  EXPECT_THAT(buffer, Not(IsNull()));
  EXPECT_THAT(buffer, SizeIs(kArraySize));
  EXPECT_THAT(absl::MakeSpan(buffer), Each(Eq(0)));
}

TEST(UniqueArrayTest, MakeUniqueZeroSizedArray) {
  auto buffer = MakeUniqueArray<int>(0);
  EXPECT_THAT(buffer, Not(IsNull()));
}

TEST(UniqueArrayTest, MakeUniqueArrayForOverwrite) {
  auto buffer = MakeUniqueArrayForOverwrite<int>(kArraySize);
  EXPECT_THAT(buffer, Not(IsNull()));
  EXPECT_THAT(buffer, SizeIs(kArraySize));
}

TEST(UniqueArrayTest, MakeUniqueZeroSizedForOverwriteArray) {
  auto buffer = MakeUniqueArrayForOverwrite<int>(0);
  EXPECT_THAT(buffer, Not(IsNull()));
}

TEST(UniqueArrayTest, DefaultCtor) {
  UniqueArray<int> buffer;
  EXPECT_THAT(buffer, IsNull());
}

TEST(UniqueArrayTest, CtorFromNullPtr) {
  UniqueArray<int> buffer = nullptr;
  EXPECT_THAT(buffer, IsNull());
}

TEST(UniqueArrayTest, CtorFromRawPointer) {
  int* raw = new int[kArraySize];
  UniqueArray<int> buffer(raw, kArraySize);
  EXPECT_THAT(buffer, Not(IsNull()));
  EXPECT_THAT(buffer, SizeIs(kArraySize));
  EXPECT_THAT(buffer.data(), Eq(raw));
}

TEST(UniqueArrayTest, CtorFromNullRawPointer) {
  int* raw = nullptr;
  UniqueArray<int> buffer(raw, 0);
  EXPECT_THAT(buffer, IsNull());
}

TEST(UniqueArrayTest, CtorFromNullRawPointerFailsWithBadSize) {
  int* raw = nullptr;
  EXPECT_DEATH(UniqueArray<int> buffer(raw, kArraySize), "");
}

TEST(UniqueArrayTest, CtorFromRawPointerWithZeroSize) {
  int* raw = new int[0];
  UniqueArray<int> buffer(raw, 0);
  EXPECT_THAT(buffer, Not(IsNull()));
  EXPECT_THAT(buffer, SizeIs(0));
}

TEST(UniqueArrayTest, CtorFromRawPointerFailsWithBadSize) {
#if !defined(NDEBUG) || ABSL_OPTION_HARDENED
  std::vector<int>* raw = new std::vector<int>[kArraySize];
  absl::base_internal::ScopedSetAbslHardeningForTesting hardener(true);
  EXPECT_DEATH(UniqueArray<std::vector<int>> buffer(raw, kArraySize + 1), "");
  delete[] raw;
#endif
}

TEST(UniqueArrayTest, CtorFromRawPointerReadsCookieWithSanitizer) {
  std::vector<int>* raw = new std::vector<int>[kArraySize];
  UniqueArray<std::vector<int>> buffer(raw, kArraySize);
  EXPECT_THAT(buffer, SizeIs(kArraySize));
}

TEST(UniqueArrayTest,
     CtorFromRawPointerDoesNotValidateSizeForTriviallyDestructibleType) {
  int* ptr = new int[kArraySize];
  // Use optional to control destruction.  We need to extract the pointer rather
  // than allowing ~UniqueArray to fire with the wrong size.
  std::optional<UniqueArray<int>> buffer;
#if !defined(NDEBUG)
  EXPECT_DEATH(buffer.emplace(ptr, kArraySize + 1), "size_le_than_cookie");
  delete[] ptr;
#else
  buffer.emplace(ptr, kArraySize + 1);
  EXPECT_THAT(*buffer, Not(IsNull()));
  EXPECT_THAT(*buffer, SizeIs(kArraySize + 1));

#if !defined(NDEBUG) || ABSL_HAVE_ADDRESS_SANITIZER
  EXPECT_DEATH(
      { buffer.reset(); }, "size check failed|new-delete-type-mismatch");
#endif

  buffer->reset();
#endif
}

#if !defined(NDEBUG)
TEST(UniqueArrayTest, MallocExtensionsCanBeUsedWithVolatileType) {
  volatile int* ptr = new volatile int[kArraySize];
  EXPECT_DEATH(UniqueArray<volatile int> buffer(ptr, kArraySize + 1),
               "size_le_than_cookie");
  delete[] ptr;
}
#endif

struct alignas(ABSL_CACHELINE_SIZE) Overaligned {};

TEST(UniqueArrayTest,
     CtorFromRawPointerWithTriviallyDestructibleOveralignedType) {
  Overaligned* ptr = new Overaligned[kArraySize];
  // Use optional to control destruction.  We need to extract the pointer rather
  // than allowing ~UniqueArray to fire with the wrong size.
  std::optional<UniqueArray<Overaligned>> buffer;
#if !defined(NDEBUG)
  EXPECT_DEATH(buffer.emplace(ptr, kArraySize + 1), "size_le_than_cookie");
  delete[] ptr;
#else
  buffer.emplace(ptr, kArraySize + 1);
  EXPECT_THAT(*buffer, Not(IsNull()));
  EXPECT_THAT(*buffer, SizeIs(kArraySize + 1));

#if !defined(NDEBUG) || ABSL_HAVE_ADDRESS_SANITIZER
  EXPECT_DEATH(
      { buffer.reset(); }, "size check failed|new-delete-type-mismatch");
#endif

  buffer->reset();
#endif
}

// Tests successful compilation with an UniqueArray of a const and trivially
// destructible type (const int), so the contents of the test are trivial as a
// result.
TEST(UniqueArrayTest, InstantiationWithConstTriviallyDestructibleType) {
  UniqueArray<const int> buffer;
  EXPECT_THAT(buffer, IsNull());
}

TEST(UniqueArrayTest, CtorFromUniquePtr) {
  auto ptr = std::unique_ptr<int[]>(new int[kArraySize]);
  UniqueArray<int> buffer(std::move(ptr), kArraySize);
  EXPECT_THAT(ptr, IsNull());
  EXPECT_THAT(buffer, Not(IsNull()));
  EXPECT_THAT(buffer.size(), Eq(kArraySize));
}

TEST(UniqueArrayTest, CtorFromNullUniquePtr) {
  auto ptr = std::unique_ptr<int[]>();
  EXPECT_THAT(ptr, IsNull());
  UniqueArray<int> buffer(std::move(ptr), 0);
  EXPECT_THAT(buffer, IsNull());
}

TEST(UniqueArrayTest, CtorFromNullUniquePtrFailsWithBadSize) {
  auto ptr = std::unique_ptr<int[]>();
  EXPECT_THAT(ptr, IsNull());
  EXPECT_DEATH(UniqueArray<int> buffer(std::move(ptr), kArraySize), "");
}

TEST(UniqueArrayTest, CtorFromUniquePtrOveraligned) {
  auto ptr = std::unique_ptr<Overaligned[]>(new Overaligned[kArraySize]);
  UniqueArray<Overaligned> buffer(std::move(ptr), kArraySize);
  EXPECT_THAT(ptr, IsNull());
  EXPECT_THAT(buffer, Not(IsNull()));
  EXPECT_THAT(buffer.size(), Eq(kArraySize));
}

TEST(UniqueArrayTest, CtorFromUniquePtrWithZeroSize) {
  auto ptr = std::unique_ptr<int[]>(new int[0]);
  UniqueArray<int> buffer(std::move(ptr), 0);
  EXPECT_THAT(buffer, Not(IsNull()));
  EXPECT_THAT(buffer, SizeIs(0));
}

TEST(UniqueArrayTest, CtorFromUniquePtrFailsWithBadSize) {
#if !defined(NDEBUG) || ABSL_OPTION_HARDENED
  auto ptr =
      std::unique_ptr<std::vector<int>[]>(new std::vector<int>[kArraySize]);
  absl::base_internal::ScopedSetAbslHardeningForTesting hardener(true);
  EXPECT_DEATH(
      UniqueArray<std::vector<int>> buffer(std::move(ptr), kArraySize + 1), "");
#endif
}

TEST(UniqueArrayTest, CtorFromUniquePtrFailsWithBadSizeOveraligned) {
#if !defined(NDEBUG)
  auto ptr = std::unique_ptr<Overaligned[]>(new Overaligned[kArraySize]);
  EXPECT_DEATH(UniqueArray<Overaligned> buffer(std::move(ptr), kArraySize + 1),
               "");
#endif
}

TEST(UniqueArrayTest,
     CtorFromUniquePtrDoesNotValidateSizeForTriviallyDestructibleType) {
  auto ptr = std::unique_ptr<int[]>(new int[kArraySize]);
  // Use optional to control destruction.  We need to extract the pointer rather
  // than allowing ~UniqueArray to fire with the wrong size.
  std::optional<UniqueArray<int>> buffer;
#if !defined(NDEBUG)
  EXPECT_DEATH(buffer.emplace(std::move(ptr), kArraySize + 1),
               "size_le_than_cookie");
#else
  buffer.emplace(std::move(ptr), kArraySize + 1);
  EXPECT_THAT(*buffer, Not(IsNull()));
  EXPECT_THAT(*buffer, SizeIs(kArraySize + 1));

#if !defined(NDEBUG) || ABSL_HAVE_ADDRESS_SANITIZER
  EXPECT_DEATH(
      { buffer.reset(); }, "size check failed|new-delete-type-mismatch");
#endif

  buffer->reset();
#endif
}

TEST(UniqueArrayTest, CtorFromIterators) {
  std::array<int, 4> numbers({1, 2, 3, 4});
  auto buffer = UniqueArray<int>(numbers.begin(), numbers.end());
  EXPECT_THAT(buffer, Not(IsNull()));
  ASSERT_THAT(buffer.size(), Eq(numbers.size()));
  for (std::size_t i = 0; i < numbers.size(); i++) {
    EXPECT_THAT(buffer[i], Eq(numbers[i]));
  }
}

TEST(UniqueArrayTest, CtorFromList) {
  auto buffer = UniqueArray<int>({1, 2, 3, 4});
  EXPECT_THAT(buffer, Not(IsNull()));
  ASSERT_THAT(buffer.size(), Eq(4));
}

TEST(UniqueArrayTest, MoveCtor) {
  auto src = MakeUniqueArray<int>(kArraySize);
  UniqueArray<int> buffer(std::move(src));
  EXPECT_THAT(src, IsNull());
  EXPECT_THAT(src, SizeIs(0));
  EXPECT_THAT(buffer, Not(IsNull()));
  EXPECT_THAT(buffer, SizeIs(kArraySize));
}

TEST(UniqueArrayTest, ConvertingCtor) {
  auto src = MakeUniqueArray<int>(kArraySize);
  UniqueArray<const int> buffer(std::move(src));
  EXPECT_THAT(src, IsNull());
  EXPECT_THAT(src, SizeIs(0));
  EXPECT_THAT(buffer, Not(IsNull()));
  EXPECT_THAT(buffer, SizeIs(kArraySize));
}

TEST(UniqueArrayTest, MoveAssignment) {
  auto src = MakeUniqueArray<int>(kArraySize);
  UniqueArray<int> buffer;
  buffer = std::move(src);
  EXPECT_THAT(src, IsNull());
  EXPECT_THAT(src, SizeIs(0));
  EXPECT_THAT(buffer, Not(IsNull()));
  EXPECT_THAT(buffer, SizeIs(kArraySize));
}

TEST(UniqueArrayTest, MoveAssignmentFromRvalue) {
  UniqueArray<int> buffer;
  buffer = MakeUniqueArray<int>(kArraySize);
  EXPECT_THAT(buffer, Not(IsNull()));
  EXPECT_THAT(buffer, SizeIs(kArraySize));
}

TEST(UniqueArrayTest, ConvertingAssignment) {
  UniqueArray<int> src = MakeUniqueArray<int>(kArraySize);
  UniqueArray<const int> dest;
  dest = std::move(src);
  EXPECT_THAT(src, IsNull());
  EXPECT_THAT(src, SizeIs(0));
  EXPECT_THAT(dest, Not(IsNull()));
  EXPECT_THAT(dest, SizeIs(kArraySize));
}

TEST(UniqueArrayTest, AssignmentFromNullPointer) {
  UniqueArray<int> buffer = MakeUniqueArray<int>(kArraySize);
  EXPECT_THAT(buffer, Not(IsNull()));
  buffer = nullptr;
  EXPECT_THAT(buffer, IsNull());
}

TEST(UniqueArrayTest, HardenedIndexing) {
#if !defined(NDEBUG) || ABSL_OPTION_HARDENED
  auto buffer = MakeUniqueArray<int>(kArraySize);
  absl::base_internal::ScopedSetAbslHardeningForTesting hardener(true);
  EXPECT_DEATH([[maybe_unused]] int result = buffer[kArraySize], "");
#endif
}

TEST(UniqueArrayTest, SupportsArrayWithKnownBoundAsElement) {
  // Test allocation
  UniqueArray<int[3]> buffer = MakeUniqueArray<int[3]>(kArraySize);
  EXPECT_THAT(buffer, SizeIs(kArraySize));

  // Test access
  for (std::size_t i = 0; i < kArraySize; i++) {
    buffer[i][0] = 1;
    buffer[i][1] = 2;
    buffer[i][2] = 3;
  }

  for (std::size_t i = 0; i < kArraySize; i++) {
    EXPECT_THAT(buffer[i][0], Eq(1));
    EXPECT_THAT(buffer[i][1], Eq(2));
    EXPECT_THAT(buffer[i][2], Eq(3));
  }

  // Test hardening
#if !defined(NDEBUG) || ABSL_OPTION_HARDENED
  absl::base_internal::ScopedSetAbslHardeningForTesting hardener(true);
  EXPECT_DEATH([[maybe_unused]] int result = buffer[kArraySize][0], "");
#endif
}

TEST(UniqueArrayTest, IteratorIsRandomAccess) {
  static_assert(
      std::is_same_v<std::iterator_traits<  //
                         UniqueArray<int>::iterator>::iterator_category,
                     std::random_access_iterator_tag>);

  static_assert(
      std::is_same_v<std::iterator_traits<
                         UniqueArray<int>::const_iterator>::iterator_category,
                     std::random_access_iterator_tag>);

  static_assert(
      std::is_same_v<std::iterator_traits<
                         UniqueArray<int>::reverse_iterator>::iterator_category,
                     std::random_access_iterator_tag>);

  static_assert(
      std::is_same_v<std::iterator_traits<UniqueArray<
                         int>::const_reverse_iterator>::iterator_category,
                     std::random_access_iterator_tag>);
}

TEST(UniqueArrayTest, CanIterate) {
  auto buffer = MakeUniqueArray<int>(3);
  absl::c_fill(buffer, 6);
  for (int& n : buffer) {
    EXPECT_EQ(n, 6);
    ++n;
  }
  for (const int& n : std::as_const(buffer)) {
    EXPECT_EQ(n, 7);
  }
}

TEST(UniqueArrayTest, CanReverseIterate) {
  auto buffer = MakeUniqueArray<int>(3);
  std::iota(buffer.rbegin(), buffer.rend(), 9);
  EXPECT_THAT(buffer, ElementsAre(11, 10, 9));
}

TEST(UniqueArrayTest, OperatorBool) {
  UniqueArray<int> buffer;
  EXPECT_FALSE(buffer);
  buffer = MakeUniqueArray<int>(kArraySize);
  ASSERT_TRUE(buffer);
  std::unique_ptr<int[]> ptr = buffer.release().ptr;
  EXPECT_THAT(ptr, Not(IsNull()));
}

TEST(UniqueArrayTest, EqualityAndInequalityOperator) {
  UniqueArray<int> buffer;
  EXPECT_TRUE(buffer == nullptr);
  EXPECT_TRUE(nullptr == buffer);
  buffer = MakeUniqueArray<int>(kArraySize);
  EXPECT_TRUE(buffer != nullptr);
  EXPECT_TRUE(nullptr != buffer);
}

TEST(UniqueArrayTest, release) {
  auto buffer = MakeUniqueArray<int>(kArraySize);
  UniqueArray<int>::OwningPointer ptr = buffer.release();
  EXPECT_THAT(ptr.size, Eq(kArraySize));
  EXPECT_THAT(ptr.ptr, Not(IsNull()));
}

TEST(UniqueArrayTest, ReleaseArrayWithDeleterWithNoDefaultCtor) {
  UniqueArray<int, DeleterWithNoDefaultCtor> buffer(
      {new int[kArraySize], DeleterWithNoDefaultCtor(0)}, kArraySize);
  auto owning_ptr = buffer.release();
  EXPECT_THAT(buffer, IsNull());
  EXPECT_THAT(buffer, SizeIs(0));
  EXPECT_THAT(owning_ptr.ptr, Not(IsNull()));
  EXPECT_THAT(owning_ptr.size, Eq(kArraySize));
}

TEST(UniqueArrayTest, reset) {
  UniqueArray<int> buffer = MakeUniqueArray<int>(kArraySize);
  EXPECT_THAT(buffer, Not(IsNull()));
  buffer.reset();
  EXPECT_THAT(buffer, IsNull());
}

TEST(UniqueArrayTest, ResetWitRawPointer) {
  UniqueArray<int> buffer = MakeUniqueArray<int>(kArraySize);
  EXPECT_THAT(buffer, SizeIs(kArraySize));
  int* raw = new int[kArraySize + 1];
  buffer.reset(raw, kArraySize + 1);
  EXPECT_THAT(buffer, SizeIs(kArraySize + 1));
  EXPECT_THAT(buffer.data(), Eq(raw));
}

TEST(UniqueArrayTest, ResetWithRawPointerFailsWithBadSize) {
  UniqueArray<std::vector<int>> buffer;
#if !defined(NDEBUG) || ABSL_OPTION_HARDENED
  std::vector<int>* raw = new std::vector<int>[kArraySize];
  absl::base_internal::ScopedSetAbslHardeningForTesting hardener(true);
  EXPECT_DEATH(buffer.reset(raw, kArraySize + 1), "");
  delete[] raw;
#endif
}

TEST(UniqueArrayTest,
     ResetWithRawPointerDoesNotValidateSizeForTriviallyDestructibleType) {
  int* ptr = new int[kArraySize];
  int* ptr2 = new int[kArraySize];
  // Use optional to control destruction.  We need to extract the pointer rather
  // than allowing ~UniqueArray to fire with the wrong size.
  std::optional<UniqueArray<int>> buffer;
#if !defined(NDEBUG)
  buffer.emplace(ptr, kArraySize);
  EXPECT_DEATH(buffer->reset(ptr2, kArraySize + 1), "size_le_than_cookie");
  delete[] ptr2;
#else
  buffer.emplace(ptr, kArraySize);
  buffer->reset(ptr2, kArraySize + 1);
  EXPECT_THAT(*buffer, Not(IsNull()));
  EXPECT_THAT(*buffer, SizeIs(kArraySize + 1));

#if !defined(NDEBUG) || ABSL_HAVE_ADDRESS_SANITIZER
  EXPECT_DEATH(
      { buffer.reset(); }, "size check failed|new-delete-type-mismatch");
#endif

  buffer->reset();
#endif
}

TEST(UniqueArrayTest, CustomDeleter) {
  {
    std::unique_ptr<int[], CustomDeleter> ptr(new int[kArraySize]);
    UniqueArray<int, CustomDeleter> buffer(std::move(ptr), kArraySize);
    EXPECT_THAT(buffer, Not(IsNull()));
    EXPECT_THAT(CustomDeleter::signature, Eq(0));
  }
  EXPECT_THAT(CustomDeleter::signature, Eq(kDeleterSignature));
}

TEST(UniqueArrayTest, ResetWithCustomDeleter) {
  std::unique_ptr<int[], DeleterWithNoDefaultCtor> ptr(
      new int[kArraySize], DeleterWithNoDefaultCtor(kDeleterSignature));
  UniqueArray<int, DeleterWithNoDefaultCtor> buffer(std::move(ptr), kArraySize);
  int* raw = new int[kArraySize];
  buffer.reset(raw, kArraySize);
  EXPECT_THAT(buffer, Not(IsNull()));
  auto owned_ptr = buffer.release();
  EXPECT_THAT(owned_ptr.ptr.get_deleter().GetData(), Eq(kDeleterSignature));
}

TEST(UniqueArrayTest, LambdaDeleter) {
  size_t len = kArraySize;
  int signature = 0;

  auto deallocator = [len, &signature](char* c) {
    signature = kDeleterSignature;
    std::allocator<char>().deallocate(c, len);
  };

  {
    std::unique_ptr<char[], decltype(deallocator)> ptr(
        std::allocator<char>().allocate(len), deallocator);
    UniqueArray<char, decltype(deallocator)> buffer(std::move(ptr), len);
    EXPECT_THAT(buffer, SizeIs(kArraySize));
  }

  EXPECT_THAT(signature, Eq(kDeleterSignature));
}

TEST(UniqueArrayTest, swap) {
  auto src = MakeUniqueArray<int>(kArraySize);
  auto dst = MakeUniqueArray<int>(kArraySize * 2);
  src[0] = src.size();
  dst[0] = dst.size();
  std::swap(src, dst);
  EXPECT_THAT(src, SizeIs(kArraySize * 2));
  EXPECT_THAT(dst, SizeIs(kArraySize));
  EXPECT_THAT(src[0], Eq(kArraySize * 2));
  EXPECT_THAT(dst[0], Eq(kArraySize));
}

TEST(UniqueArrayTest, MakeSpanFromUniqueArray) {
  auto buffer = MakeUniqueArray<int>(kArraySize);
  auto buffer_view = absl::MakeSpan(buffer);
  buffer_view[0] = buffer.size();
  EXPECT_THAT(buffer_view, SizeIs(kArraySize));
  EXPECT_THAT(buffer[0], Eq(kArraySize));
  EXPECT_THAT(buffer_view[0], Eq(kArraySize));
}

TEST(UniqueArrayTest, MakeConstSpanFromUniqueArray) {
  auto buffer = MakeUniqueArray<int>(kArraySize);
  buffer[0] = buffer.size();
  auto buffer_view = absl::MakeConstSpan(buffer);
  EXPECT_THAT(buffer_view, SizeIs(kArraySize));
  EXPECT_THAT(buffer[0], Eq(kArraySize));
  EXPECT_THAT(buffer_view[0], Eq(kArraySize));
}

TEST(UniqueArrayTest, AbslStringify) {
  UniqueArray<int> null_array;
  EXPECT_EQ(absl::StrCat(null_array), "(nil)");

  auto buffer = MakeUniqueArray<int>(kArraySize);
  EXPECT_THAT(absl::StrCat(buffer), MatchesRegex("0x[0-9a-f]+"));
}

TEST(UniqueArrayTest, PrintsToOutputStream) {
  std::ostringstream out;
  auto buffer = MakeUniqueArray<int>(kArraySize);
  out << buffer;
  EXPECT_THAT(out.str(), MatchesRegex("0x[0-9a-f]+"));
}

TEST(UniqueArrayTest, PrintsNullArrayToOutputStream) {
  std::ostringstream out;
  UniqueArray<int> null_array;
  out << null_array;
  EXPECT_THAT(out.str(), ContainsRegex(R"(\(nil\)|0)"));
}

}  // namespace
}  // namespace gtl
