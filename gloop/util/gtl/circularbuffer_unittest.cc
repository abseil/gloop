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

#include "gloop/util/gtl/circularbuffer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <new>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace gtl {
namespace {

using testing::ElementsAre;
using testing::Pointee;

struct NonCopyablePair {
  explicit NonCopyablePair(int first, int second)
      : first(first), second(second) {}

  NonCopyablePair(const NonCopyablePair&) = delete;
  NonCopyablePair& operator=(const NonCopyablePair&) = delete;

  friend std::ostream& operator<<(std::ostream& os, const NonCopyablePair& p) {
    return os << "(" << p.first << ", " << p.second << ")";
  }

  int first;
  int second;
};

// Matcher for NonCopyablePair.
MATCHER_P2(PairIs, first, second, "") {
  return arg.first == first && arg.second == second;
}

template <typename C>
void PushBackSequence(C* c, int lo, int hi) {
  for (; lo != hi; ++lo) {
    c->push_back(lo);
  }
}

template <typename C>
void PushFrontSequence(C* c, int lo, int hi) {
  for (; lo != hi; ++lo) {
    c->push_front(lo);
  }
}

template <typename C>
void EmplaceBackSequence(C* c, int lo, int hi) {
  for (; lo != hi; ++lo) {
    c->emplace_back(lo, hi);
  }
}

template <typename C>
void EmplaceFrontSequence(C* c, int lo, int hi) {
  for (; lo != hi; ++lo) {
    c->emplace_front(lo, hi);
  }
}

class CircularBufferTest : public ::testing::Test {
 public:
  void SetUp() override { count_ = 0; }

 protected:
  struct Canary {
    explicit Canary(int val = 0) : val_(val) { ++(*count_); }
    Canary(const Canary& o) : val_(o.val_) { ++(*count_); }
    Canary& operator=(const Canary& o) = default;
    ~Canary() { --(*count_); }

    friend std::ostream& operator<<(std::ostream& os, const Canary& v) {
      return os << v.val_;
    }

    friend bool operator==(const Canary& a, const Canary& b) {
      return a.val_ == b.val_;
    }

    int val_;
    static int* count_;
  };

  static int count() { return count_; }
  static int count_;
};
int CircularBufferTest::count_ = 0;
int* CircularBufferTest::Canary::count_ = &CircularBufferTest::count_;

TEST_F(CircularBufferTest, PushBack) {
  CircularBuffer<int> cb(3);
  PushBackSequence(&cb, 0, 2);
  EXPECT_THAT(cb, ElementsAre(0, 1));
  PushBackSequence(&cb, 2, 6);
  EXPECT_TRUE(cb.full());
  EXPECT_THAT(cb, ElementsAre(3, 4, 5));
}

TEST_F(CircularBufferTest, EmplaceBack) {
  CircularBuffer<NonCopyablePair> cb(3);
  EmplaceBackSequence(&cb, 0, 2);
  EXPECT_THAT(cb, ElementsAre(PairIs(0, 2), PairIs(1, 2)));
  EmplaceBackSequence(&cb, 2, 6);
  EXPECT_TRUE(cb.full());
  EXPECT_THAT(cb, ElementsAre(PairIs(3, 6), PairIs(4, 6), PairIs(5, 6)));
}

TEST_F(CircularBufferTest, EmplaceReturnsReference) {
  CircularBuffer<int> cb(3);
  int& ref_back = cb.emplace_back(511);
  EXPECT_EQ(511, ref_back);
  int& ref_front = cb.emplace_front(1963);
  EXPECT_EQ(1963, ref_front);
}

TEST_F(CircularBufferTest, At) {
  CircularBuffer<int> cb(3);
  PushBackSequence(&cb, 3, 6);
  EXPECT_EQ(3, cb.at(0));
  EXPECT_EQ(4, cb.at(1));
  EXPECT_EQ(5, cb.at(2));
  EXPECT_EQ(5, cb.at(-1));
  EXPECT_EQ(4, cb.at(-2));
  EXPECT_EQ(3, cb.at(-3));
}

TEST_F(CircularBufferTest, PopFront) {
  CircularBuffer<int> cb(3);
  PushBackSequence(&cb, 3, 6);
  EXPECT_EQ(3, cb.front());
  cb.pop_front();
  EXPECT_EQ(4, cb.front());
  cb.pop_front();
  EXPECT_EQ(5, cb.front());
  cb.pop_front();
  EXPECT_TRUE(cb.empty());
}

TEST_F(CircularBufferTest, PopBack) {
  CircularBuffer<int> cb(3);
  PushBackSequence(&cb, 3, 6);
  EXPECT_EQ(5, cb.back());
  cb.pop_back();
  EXPECT_EQ(4, cb.back());
  cb.pop_back();
  EXPECT_EQ(3, cb.back());
  cb.pop_back();
  EXPECT_TRUE(cb.empty());
}

TEST_F(CircularBufferTest, Clear) {
  CircularBuffer<int> cb(3);
  PushBackSequence(&cb, 2, 6);
  cb.clear();
  EXPECT_TRUE(cb.empty());
}

TEST_F(CircularBufferTest, PushFront) {
  CircularBuffer<int> cb(3);
  PushFrontSequence(&cb, 0, 5);
  EXPECT_THAT(cb, ElementsAre(4, 3, 2));
  // Test at()
  EXPECT_EQ(4, cb.at(0));
  EXPECT_EQ(&cb[0], &cb.at(0));
  EXPECT_EQ(3, cb.at(1));
  EXPECT_EQ(&cb[1], &cb.at(1));
  EXPECT_EQ(2, cb.at(2));
  EXPECT_EQ(&cb[2], &cb.at(2));
  // Test mutating.
  typedef CircularBuffer<int>::iterator Iter;
  for (Iter ii = cb.begin(); ii != cb.end(); ++ii) {
    *ii *= 2;
  }
  EXPECT_THAT(cb, ElementsAre(8, 6, 4));
}

TEST_F(CircularBufferTest, EmplaceFront) {
  CircularBuffer<NonCopyablePair> cb(3);
  EmplaceFrontSequence(&cb, 0, 5);
  EXPECT_THAT(cb, ElementsAre(PairIs(4, 5), PairIs(3, 5), PairIs(2, 5)));
  // Test at()
  EXPECT_THAT(cb.at(0), PairIs(4, 5));
  EXPECT_THAT(cb.at(1), PairIs(3, 5));
  EXPECT_THAT(cb.at(2), PairIs(2, 5));
  // Test mutating.
  typedef CircularBuffer<NonCopyablePair>::iterator Iter;
  for (Iter ii = cb.begin(); ii != cb.end(); ++ii) {
    ii->first *= 2;
  }
  EXPECT_THAT(cb, ElementsAre(PairIs(8, 5), PairIs(6, 5), PairIs(4, 5)));
}

TEST_F(CircularBufferTest, Assignment) {
  CircularBuffer<int> cb1(3);
  PushBackSequence(&cb1, 0, 3);
  CircularBuffer<int> cb2(3);
  cb2 = cb1;
  EXPECT_THAT(cb1, ElementsAre(0, 1, 2));
  EXPECT_THAT(cb2, ElementsAre(0, 1, 2));
}

TEST_F(CircularBufferTest, ResizingLarger) {
  CircularBuffer<int> cb(3);
  PushBackSequence(&cb, 0, 3);
  cb.resize(4);
  EXPECT_THAT(cb, ElementsAre(0, 1, 2));
}

TEST_F(CircularBufferTest, ResizingSmaller) {
  CircularBuffer<int> cb(3);
  PushBackSequence(&cb, 0, 3);
  cb.resize(2);
  EXPECT_THAT(cb, ElementsAre(0, 1));
}

TEST_F(CircularBufferTest, CanBePutIntoMaps) {
  std::map<int, CircularBuffer<int>> m;
  m[0] = CircularBuffer<int>();
}

TEST_F(CircularBufferTest, EmptyConstruction) {
  // Test that a CircularBuffer creates no live elements.
  CircularBuffer<Canary> cb(2);
  EXPECT_EQ(0, count());
}

TEST_F(CircularBufferTest, PartiallyFilledLifetimes) {
  CircularBuffer<Canary> cb(3);
  cb.push_back(Canary(1));
  EXPECT_EQ(1, count());
  cb.push_back(Canary(2));
  EXPECT_EQ(2, count());
  cb.clear();
  EXPECT_TRUE(cb.empty());
  EXPECT_EQ(0, count());
}

TEST_F(CircularBufferTest, ClearDestruction) {
  // Test that a cleared CircularBuffer has no live elements.
  CircularBuffer<Canary> cb(2);
  cb.push_back(Canary(1));
  EXPECT_EQ(1, count());
  cb.push_back(Canary(2));
  EXPECT_EQ(2, count());
  cb.push_back(Canary(3));
  EXPECT_EQ(2, count());
  cb.clear();
  EXPECT_TRUE(cb.empty());
  EXPECT_EQ(0, count());
}

TEST_F(CircularBufferTest, AllBeginPositionsPushBack) {
  for (int i = 0; i < 3; ++i) {
    SCOPED_TRACE(i);
    CircularBuffer<Canary> cb(3);
    // Rotate 'begin_' up to position i:
    for (int j = 0; j < i; ++j) {
      cb.push_back(Canary(j));
    }
    for (int j = 0; j < i; ++j) {
      cb.pop_front();
    }
    cb.push_back(Canary(10));
    EXPECT_THAT(cb, ElementsAre(Canary(10)));
    cb.pop_back();
    EXPECT_THAT(cb, ElementsAre());
  }
}

template <typename T>
void Accept(const T& x) {}

// The default constructor makes a CircularBuffer of capacity 0.
// It has to be resized before it can hold elements, but it should
// otherwise be functional.
TEST_F(CircularBufferTest, DefaultConstructedTrivial) {
  // Try some different syntax
  {
    CircularBuffer<Canary> cb;
  }
  {
    CircularBuffer<Canary> cb{};
  }
  {
    CircularBuffer<Canary> cb = {};
  }
  {
    auto cb = CircularBuffer<Canary>{};
  }
  {
    auto cb = CircularBuffer<Canary>();
  }
  Accept<CircularBuffer<Canary>>({});
  CircularBuffer<Canary> cb;
  EXPECT_TRUE(cb.empty());
  EXPECT_EQ(0, cb.size());
  EXPECT_EQ(0, cb.capacity());
  EXPECT_EQ(0, std::distance(cb.begin(), cb.end()));
  EXPECT_EQ(0, cb.begin() - cb.end());
  EXPECT_EQ(0, (cb.begin() + 0) - cb.end());
}

TEST_F(CircularBufferTest, DefaultConstructed) {
  CircularBuffer<Canary> cb;

  cb.resize(1);
  EXPECT_TRUE(cb.empty());
  EXPECT_EQ(0, cb.size());
  EXPECT_EQ(1, cb.capacity());
  EXPECT_EQ(0, count());

  cb.push_back(Canary(1));
  EXPECT_EQ(1, cb.size());
  EXPECT_THAT(cb, ElementsAre(Canary(1)));
  EXPECT_EQ(1, count());

  cb.pop_back();
  EXPECT_THAT(cb, ElementsAre());
  EXPECT_EQ(0, count());
}

TEST_F(CircularBufferTest, AllBeginPositionsPushFront) {
  for (int i = 0; i < 3; ++i) {
    SCOPED_TRACE(i);
    CircularBuffer<Canary> cb(3);
    // Rotate 'begin_' up to position i:
    for (int j = 0; j < i; ++j) {
      cb.push_back(Canary(j));
    }
    for (int j = 0; j < i; ++j) {
      cb.pop_front();
    }
    cb.push_front(Canary(10));
    EXPECT_THAT(cb, ElementsAre(Canary(10)));
    cb.pop_front();
    EXPECT_THAT(cb, ElementsAre());
  }
}

TEST(CircularBufferIteratorTest, Iterators) {
  CircularBuffer<int> cb(10);
  // Position begin() roughly in the middle
  PushFrontSequence(&cb, 0, 7);
  for (int i = 0; i < 6; i++) {
    cb.pop_back();
  }
  // Fill up the circular buffer
  PushFrontSequence(&cb, 0, 10);
  CircularBuffer<int>::iterator ibegin = cb.begin();
  // Test assignment
  CircularBuffer<int>::iterator icurrent;
  icurrent = ibegin;
  EXPECT_TRUE(ibegin == icurrent);
  EXPECT_FALSE(ibegin != icurrent);
  EXPECT_EQ(0, ibegin - icurrent);
  EXPECT_EQ(0, icurrent - ibegin);

  // Test various operators
  icurrent++;
  EXPECT_EQ(1, icurrent - ibegin);
  icurrent--;
  EXPECT_EQ(0, ibegin - icurrent);
  icurrent += 3;
  EXPECT_EQ(3, icurrent - ibegin);
  icurrent -= 3;
  EXPECT_EQ(0, ibegin - icurrent);
  icurrent += 5;
  --icurrent;
  EXPECT_EQ(4, icurrent - ibegin);
  ++icurrent;
  EXPECT_EQ(5, icurrent - ibegin);
  EXPECT_EQ(-5, ibegin - icurrent);
  EXPECT_TRUE(ibegin < icurrent);
}

TEST(CircularBufferIteratorTest, ConstIterator) {
  CircularBuffer<int> cb(10);
  PushFrontSequence(&cb, 0, 10);
  const CircularBuffer<int>& ccb = cb;
  CircularBuffer<int>::const_iterator cit = ccb.begin();
  CircularBuffer<int>::iterator it = cb.begin();

  // Test cast from const_iterator from iterator.
  CircularBuffer<int>::const_iterator ABSL_ATTRIBUTE_UNUSED test_cast = it;

  // Test copy constructor of iterator.
  CircularBuffer<int>::iterator ABSL_ATTRIBUTE_UNUSED test_copy = it;

  // Test copy constructor of const_iterator.
  CircularBuffer<int>::const_iterator ABSL_ATTRIBUTE_UNUSED test_copy_const =
      cit;

  // Test Comparisons between iterator and const_iterator.
  EXPECT_TRUE(it == cit);
  EXPECT_TRUE(cit == it);
  EXPECT_FALSE(it != cit);
  EXPECT_FALSE(cit != it);
  EXPECT_FALSE(it < cit);
  EXPECT_FALSE(cit < it);
  EXPECT_FALSE(it > cit);
  EXPECT_FALSE(cit > it);
  EXPECT_TRUE(it <= cit);
  EXPECT_TRUE(cit <= it);
  EXPECT_TRUE(it >= cit);
  EXPECT_TRUE(cit >= it);
}

TEST(CircularBufferIteratorTest, ReverseIterators) {
  CircularBuffer<int> cb(10);
  PushBackSequence(&cb, 0, 10);
  const CircularBuffer<int>& ccb = cb;
  std::vector<int> reversed(cb.rbegin(), cb.rend());
  std::vector<int> const_reversed(ccb.rbegin(), ccb.rend());
  EXPECT_THAT(reversed, ElementsAre(9, 8, 7, 6, 5, 4, 3, 2, 1, 0));
  EXPECT_THAT(const_reversed, ElementsAre(9, 8, 7, 6, 5, 4, 3, 2, 1, 0));
}

TEST(CircularBufferOrderingTest, Comparators) {
  CircularBuffer<int> cb_a(10);
  PushBackSequence(&cb_a, 0, 10);

  CircularBuffer<int> cb_b(5);
  PushBackSequence(&cb_b, 5, 10);

  EXPECT_TRUE(cb_a == cb_a);
  EXPECT_FALSE(cb_a == cb_b);
  EXPECT_TRUE(cb_a != cb_b);
  EXPECT_FALSE(cb_a != cb_a);
  EXPECT_TRUE(cb_a < cb_b);
  EXPECT_FALSE(cb_b < cb_a);
  EXPECT_TRUE(cb_a <= cb_b);
  EXPECT_FALSE(cb_b <= cb_a);
  EXPECT_TRUE(cb_b > cb_a);
  EXPECT_FALSE(cb_a > cb_b);
  EXPECT_TRUE(cb_b >= cb_a);
  EXPECT_FALSE(cb_a >= cb_b);
}

TEST(CircularBufferInAssociativeContainterTest, Map) {
  std::map<CircularBuffer<std::string>, int> m;
  CircularBuffer<std::string> cb(5);
  for (int i = 0; i < 10; ++i) {
    m.insert(std::make_pair(cb, i));
    cb.push_back(absl::StrCat(i));
  }

  EXPECT_EQ(m.size(), 10);

  cb.clear();
  for (int i = 0; i < 10; ++i) {
    const auto it = m.find(cb);
    ASSERT_TRUE(it != m.end());
    EXPECT_TRUE(cb == it->first);
    EXPECT_EQ(i, it->second);
    cb.push_back(absl::StrCat(i));
  }
}

TEST_F(CircularBufferTest, MoveOnly) {
  CircularBuffer<std::unique_ptr<Canary>> cb;

  cb.resize(1);
  EXPECT_TRUE(cb.empty());
  EXPECT_EQ(0, cb.size());
  EXPECT_EQ(1, cb.capacity());
  EXPECT_EQ(0, count());

  cb.push_back(std::make_unique<Canary>(123));
  EXPECT_EQ(1, cb.size());
  EXPECT_THAT(cb, ElementsAre(Pointee(Canary(123))));
  EXPECT_EQ(1, count());

  cb.pop_back();
  EXPECT_THAT(cb, ElementsAre());
  EXPECT_EQ(0, count());
}

TEST_F(CircularBufferTest, MoveConstruct) {
  CircularBuffer<std::unique_ptr<Canary>> cb1(1);
  cb1.push_back(std::make_unique<Canary>(123));
  CircularBuffer<std::unique_ptr<Canary>> cb2(std::move(cb1));
  EXPECT_THAT(cb1, ElementsAre());  // not specified, but test anyway
  EXPECT_THAT(cb2, ElementsAre(Pointee(Canary(123))));
  EXPECT_EQ(1, count());
  cb2.pop_back();
  EXPECT_THAT(cb2, ElementsAre());
  EXPECT_EQ(0, count());
}

TEST_F(CircularBufferTest, MoveAssign) {
  CircularBuffer<std::unique_ptr<Canary>> cb1(1);
  cb1.push_back(std::make_unique<Canary>(123));
  CircularBuffer<std::unique_ptr<Canary>> cb2(1);
  cb2.push_back(std::make_unique<Canary>(456));
  EXPECT_EQ(2, count());
  cb2 = std::move(cb1);
  EXPECT_EQ(1, count());
  EXPECT_THAT(cb1, ElementsAre());  // not specified, but test anyway
  EXPECT_THAT(cb2, ElementsAre(Pointee(Canary(123))));
  cb2.pop_back();
  EXPECT_THAT(cb2, ElementsAre());
  EXPECT_EQ(0, count());
}

template <typename T, size_t N>
struct ArrayAllocatorStorage {
  std::array<std::aligned_storage_t<sizeof(T), alignof(T)>, N> array;
  bool allocated = false;
};

template <typename T, size_t N>
class ArrayAllocator {
 public:
  using value_type = T;
  using propagate_on_container_swap = std::true_type;

  ArrayAllocator() : storage_(nullptr) {}
  ArrayAllocator(ArrayAllocatorStorage<T, N>* storage) : storage_(storage) {}

  T* allocate(size_t n) {
    if (storage_ != nullptr && !storage_->allocated && n <= N) {
      storage_->allocated = true;
      return reinterpret_cast<T*>(storage_->array.data());
    }
    return reinterpret_cast<T*>(
        ::operator new(n * sizeof(T), std::align_val_t(alignof(T))));
  }

  void deallocate(T* p, size_t n) {
    if (storage_ != nullptr &&
        p == reinterpret_cast<T*>(storage_->array.data())) {
      storage_->allocated = false;
      return;
    }
    ::operator delete(p, n * sizeof(T), std::align_val_t(alignof(T)));
  }

  ArrayAllocatorStorage<T, N>* storage() const { return storage_; }

 private:
  ArrayAllocatorStorage<T, N>* storage_;
};

template <typename T, size_t N>
bool operator==(const ArrayAllocator<T, N>& lhs,
                const ArrayAllocator<T, N>& rhs) {
  return lhs.storage() == rhs.storage();
}

TEST_F(CircularBufferTest, CustomAllocatorCopyMove) {
  ArrayAllocatorStorage<int, 3> storage;
  ArrayAllocator<int, 3> allocator(&storage);

  {
    CircularBuffer<int, ArrayAllocator<int, 3>> cb1(3, allocator);
    cb1.push_back(1);
    cb1.push_back(2);
    cb1.push_back(3);
    EXPECT_THAT(cb1, ElementsAre(1, 2, 3));
    EXPECT_EQ(&cb1.front(), reinterpret_cast<int*>(storage.array.data()));

    CircularBuffer<int, ArrayAllocator<int, 3>> cb2(cb1);
    EXPECT_THAT(cb2, ElementsAre(1, 2, 3));
    EXPECT_NE(&cb2.front(), reinterpret_cast<int*>(storage.array.data()));

    CircularBuffer<int, ArrayAllocator<int, 3>> cb3 = cb1;
    EXPECT_THAT(cb3, ElementsAre(1, 2, 3));
    EXPECT_NE(&cb3.front(), reinterpret_cast<int*>(storage.array.data()));

    CircularBuffer<int, ArrayAllocator<int, 3>> cb4(std::move(cb1));
    EXPECT_EQ(&cb4.front(), reinterpret_cast<int*>(storage.array.data()));

    CircularBuffer<int, ArrayAllocator<int, 3>> cb5 = std::move(cb4);
    EXPECT_EQ(&cb5.front(), reinterpret_cast<int*>(storage.array.data()));

    cb5.ChangeCapacity(5);
    EXPECT_NE(&cb5.front(), reinterpret_cast<int*>(storage.array.data()));
  }

  EXPECT_FALSE(storage.allocated);
}

TEST_F(CircularBufferTest, CustomAllocatorSwap) {
  ArrayAllocatorStorage<int, 3> storage;
  ArrayAllocator<int, 3> allocator(&storage);

  CircularBuffer<int, ArrayAllocator<int, 3>> cb1(3, allocator);
  cb1.push_back(1);
  cb1.push_back(2);
  cb1.push_back(3);
  EXPECT_THAT(cb1, ElementsAre(1, 2, 3));
  EXPECT_EQ(&cb1.front(), reinterpret_cast<int*>(storage.array.data()));

  CircularBuffer<int, ArrayAllocator<int, 3>> cb2(
      3, ArrayAllocator<int, 3>(nullptr));
  cb2.push_back(4);
  cb2.push_back(5);
  cb2.push_back(6);
  EXPECT_THAT(cb2, ElementsAre(4, 5, 6));
  EXPECT_NE(&cb2.front(), reinterpret_cast<int*>(storage.array.data()));

  cb1.swap(cb2);

  EXPECT_THAT(cb1, ElementsAre(4, 5, 6));
  EXPECT_NE(&cb1.front(), reinterpret_cast<int*>(storage.array.data()));

  EXPECT_THAT(cb2, ElementsAre(1, 2, 3));
  EXPECT_EQ(&cb2.front(), reinterpret_cast<int*>(storage.array.data()));
}

template <size_t size, size_t alignment>
void TestAligned() {
  using aligned_struct = std::aligned_storage_t<size, alignment>;

  CircularBuffer<aligned_struct> cb(1);
  cb.push_back({});
  EXPECT_EQ(0, reinterpret_cast<uintptr_t>(&cb.back()) % alignment)
      << "alignment=" << alignment << " size=" << size
      << " alloc=" << sizeof(aligned_struct);
}

TEST_F(CircularBufferTest, Aligned) {
  // Check that object alignment is respected.
  TestAligned<11 * 8, 16>();
  TestAligned<11 * 16, 32>();
  TestAligned<11 * 32, 64>();
  TestAligned<7 * 4, 16>();
  TestAligned<7 * 8, 32>();
  TestAligned<7 * 16, 64>();
  TestAligned<9 * 32, 16>();
  TestAligned<9 * 32, 32>();
  TestAligned<9 * 32, 64>();
}

void BM_Moves(benchmark::State& state) {
  CircularBuffer<std::string> cb1(100);
  CircularBuffer<std::string> cb2(100);
  for (auto s : state) {
    cb2 = std::move(cb1);
    cb1 = std::move(cb2);
  }
}
BENCHMARK(BM_Moves);

CircularBuffer<std::string> MakeBuf() {
  auto cb = CircularBuffer<std::string>(100);
  std::string s(100, 'x');
  for (size_t b = 0; b < cb.capacity(); ++b) cb.push_back(s);
  return cb;
}

void BM_MoveVector(benchmark::State& state) {
  std::vector<CircularBuffer<std::string>> src(100, MakeBuf());
  for (auto s : state) {
    std::vector<CircularBuffer<std::string>> v;
    for (CircularBuffer<std::string>& b : src) v.push_back(std::move(b));

    // Put all the values back in 'src'.
    std::copy(std::make_move_iterator(v.begin()),
              std::make_move_iterator(v.end()), src.begin());
  }
}
BENCHMARK(BM_MoveVector);

}  // namespace
}  // namespace gtl
