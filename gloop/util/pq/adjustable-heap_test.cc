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

#include "gloop/util/pq/adjustable-heap.h"

#include <stddef.h>

#include <map>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/random/random.h"
#include "gloop/util/random/acmrandom.h"
#include "gtest/gtest.h"

namespace util {
namespace pq {
namespace {

template <typename L, typename R>
struct no_copy_pair : public std::pair<L, R> {
  no_copy_pair() : std::pair<L, R>() {}
  no_copy_pair(L l, R r) : std::pair<L, R>(l, r) {}
  no_copy_pair(const no_copy_pair&) = delete;
  no_copy_pair(no_copy_pair&& p) = default;
  no_copy_pair& operator=(const no_copy_pair&) = delete;
  no_copy_pair& operator=(no_copy_pair&&) = default;
  operator std::pair<L, R>() const { return {this->first, this->second}; }
};

template <typename T>
class AdjustableHeapTest : public testing::Test {
 protected:
  AdjustableHeapTest() : r_(GTEST_FLAG_GET(random_seed)) {}

  // Lexicographic comparison functor for pairs of integers.  The only reason
  // we test the code on elements of non-trivial type is that we want to make
  // sure that Adjust{Up,Down}wards() behaves gracefully in the presence of
  // RandomAccessIterators over non-POD data types.
  struct Compare {
    bool operator()(const T& a, const T& b) const {
      // Lexicographic comparison.
      if (a.first < b.first) return true;
      if (b.first < a.first) return false;
      return a.second < b.second;
    }
  };

  // A class that contains a heap with a map that acts as an index into this
  // heap.  The index is maintained by calling operator() on a certain offset
  // after the heap itself is updated, outside of the control of this class.
  class MapMaintainer {
   public:
    void operator()(int offset) { index_into_heap_[heap_[offset]] = offset; }
    std::vector<T> heap_;

    // We use pair<int, int> for the index, because when we're simulating
    // a move-only type, we still need to be able to index the object in this
    // map. But we need the original object to still exist in the heap. So
    // rather than take a copy, which is barred by the deletion of the copy
    // constructor, we use the implicit conversion operator.
    std::map<std::pair<int, int>, int> index_into_heap_;
  };

  MapMaintainer index_;
  ACMRandom r_;
};

// We test once with a copyable type, and once with a move-only type.
typedef ::testing::Types<std::pair<int, int>, no_copy_pair<int, int>> Types;
TYPED_TEST_SUITE(AdjustableHeapTest, Types);

TYPED_TEST(AdjustableHeapTest, SimpleTest) {
  const int kNumElements = 5000;

  for (int i = 0; i < kNumElements; i++) {
    // Find a new value for the element which is not in use right now.
    TypeParam up;
    do {
      up = TypeParam(absl::Uniform<int32_t>(this->r_, 0, 65536),
                     absl::Uniform<int32_t>(this->r_, 0, 65536));
    } while (this->index_.index_into_heap_.find(up) !=
             this->index_.index_into_heap_.end());
    // Add another random element into the heap, first by appending it...
    this->index_.heap_.resize(1 + this->index_.heap_.size());
    // ..., then by adjusting it upwards, all the way to the right position.
    typename TestFixture::Compare cmp;
    AdjustUpwards(this->index_.heap_.begin(), this->index_.heap_.size() - 1,
                  std::move(up), cmp, &this->index_);

    // Make sure the index into the heap is consistent with the heap itself.
    //
    // To this end, first verify the size of the index.
    ASSERT_EQ(i + 1, this->index_.index_into_heap_.size());
    for (std::map<std::pair<int, int>, int>::const_iterator it =
             this->index_.index_into_heap_.begin();
         it != this->index_.index_into_heap_.end(); ++it) {
      // Every offset (as recorded in the index) should be a legal one.
      ASSERT_GE(it->second, 0);
      ASSERT_LT(it->second, this->index_.heap_.size());
      // At the recorded offset inside the heap, we expect the same value.
      ASSERT_TRUE(this->index_.heap_[it->second] == it->first);
    }
  }

  const int kNumDowngrades = 10000;

  for (int i = 0; i < kNumDowngrades; i++) {
    // Adjust a random element of the heap downwards.  To do so, we create a
    // "hole" in the heap at the position we picked, which involves erasing
    // the old value from the index.
    size_t where =
        absl::Uniform<int32_t>(this->r_, 0, this->index_.heap_.size());
    this->index_.index_into_heap_.erase(this->index_.heap_[where]);

    // Find a new value for the element which is not in use right now.
    TypeParam down(0, this->index_.heap_[where].second);
    while (this->index_.index_into_heap_.find(down) !=
           this->index_.index_into_heap_.end())
      down.second--;

    // And adjust the hole downwards, which updates the index into the heap
    // automatically.  (Also, if the adjustment is trivial, i.e., if nothing
    // has to be done, we still update the index to undo the previous erasure.)
    typename TestFixture::Compare cmp;
    AdjustDownwards(this->index_.heap_.begin(), where,
                    this->index_.heap_.size(), std::move(down), cmp,
                    &this->index_);

    // Make sure the index into the heap continues to be consistent with the
    // heap itself.
    // For one thing, its size must not have changed.
    ASSERT_EQ(kNumElements, this->index_.index_into_heap_.size());
    for (std::map<std::pair<int, int>, int>::const_iterator it =
             this->index_.index_into_heap_.begin();
         it != this->index_.index_into_heap_.end(); ++it) {
      // Just like in the case of AdjustUpwards, verify that all recorded
      // offsets are legal, and that we have the same value in the heap that
      // is used to key the index into the heap.
      ASSERT_GE(it->second, 0);
      ASSERT_LT(it->second, this->index_.heap_.size());
      ASSERT_TRUE(this->index_.heap_[it->second] == it->first);
    }
  }
}

}  // namespace
}  // namespace pq
}  // namespace util
