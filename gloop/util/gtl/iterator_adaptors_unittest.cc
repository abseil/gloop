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

#include "gloop/util/gtl/iterator_adaptors.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <forward_list>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/casts.h"
#include "absl/base/macros.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/strings/string_view.h"
#include "gloop/util/gtl/flat_map.h"
#include "gloop/util/gtl/flat_set.h"
#include "gmock/gmock.h"
#include "google/protobuf/duration.pb.h"
#include "gtest/gtest.h"

namespace gtl {
namespace {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Pair;
using ::testing::Pointee;
using ::testing::Pointwise;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

const char* kFirst[] = {"foo", "bar"};
int kSecond[] = {1, 2};
const int kCount = std::size(kFirst);

template <typename T>
struct IsConst : std::false_type {};
template <typename T>
struct IsConst<const T> : std::true_type {};
template <typename T>
struct IsConst<T&> : IsConst<T> {};

class IteratorAdaptorTest : public testing::Test {
 protected:
  // Objects declared here can be used by all tests in the test case for Foo.

  void SetUp() override { ASSERT_EQ(std::size(kFirst), std::size(kSecond)); }

  void TearDown() override {}

  template <typename T>
  class InlineStorageIter : public std::iterator<std::input_iterator_tag, T> {
   public:
    T* operator->() const { return get(); }
    T& operator*() const { return *get(); }

   private:
    T* get() const { return &v_; }
    mutable T v_;
  };

  struct X {
    int d;
  };
};

TEST_F(IteratorAdaptorTest, HashMapFirst) {
  // Adapts an iterator to return the first value of a hash_map::iterator.
  typedef absl::node_hash_map<std::string, int> my_container;
  my_container values;
  for (int i = 0; i < kCount; ++i) {
    values[kFirst[i]] = kSecond[i];
  }
  for (iterator_first<my_container::iterator> it = values.begin();
       it != values.end(); ++it) {
    ASSERT_GT(it->length(), 0);
  }
}

TEST_F(IteratorAdaptorTest, IteratorPtrUniquePtr) {
  // Tests iterator_ptr with a vector<unique_ptr<int>>.
  typedef std::vector<std::unique_ptr<int>> my_container;
  typedef iterator_ptr<my_container::iterator> my_iterator;
  my_container values;
  for (int i = 0; i < kCount; ++i) {
    values.push_back(std::make_unique<int>(kSecond[i]));
  }
  int i = 0;
  for (my_iterator it = values.begin(); it != values.end(); ++it, ++i) {
    int v = *it;
    *it = v;
    ASSERT_EQ(v, kSecond[i]);
  }
}

TEST_F(IteratorAdaptorTest, IteratorFirstConvertsToConst) {
  // Adapts an iterator to return the first value of a hash_map::iterator.
  typedef absl::node_hash_map<std::string, int> my_container;
  my_container values;
  for (int i = 0; i < kCount; ++i) {
    values[kFirst[i]] = kSecond[i];
  }
  iterator_first<my_container::iterator> iter = values.begin();
  iterator_first<my_container::const_iterator> c_iter = iter;
  for (; c_iter != values.end(); ++c_iter) {
    ASSERT_GT(c_iter->length(), 0);
  }
}

TEST_F(IteratorAdaptorTest, IteratorFirstConstEqNonConst) {
  // verify that const and non-const iterators return the same reference.
  typedef std::vector<std::pair<int, int>> my_container;
  typedef iterator_first<my_container::iterator> my_iterator;
  typedef iterator_first<my_container::const_iterator> my_const_iterator;
  my_container values;
  for (int i = 0; i < kCount; ++i) {
    values.push_back(std::make_pair(i, i + 1));
  }
  my_iterator iter1 = values.begin();
  const my_iterator iter2 = iter1;
  my_const_iterator c_iter1 = iter1;
  const my_const_iterator c_iter2 = c_iter1;
  for (int i = 0; i < kCount; ++i) {
    int& v1 = iter1[i];
    int& v2 = iter2[i];
    EXPECT_EQ(&v1, &values[i].first);
    EXPECT_EQ(&v1, &v2);
    const int& cv1 = c_iter1[i];
    const int& cv2 = c_iter2[i];
    EXPECT_EQ(&cv1, &values[i].first);
    EXPECT_EQ(&cv1, &cv2);
  }
}

TEST_F(IteratorAdaptorTest, IteratorFirstFactory) {
  std::map<int, int> v = {{1, 2}};
  auto with_make = make_iterator_first(v.begin());
  EXPECT_TRUE((std::is_same_v<iterator_first<std::map<int, int>::iterator>,
                              decltype(with_make)>));
  EXPECT_EQ(*with_make, 1);

  auto with_ctad = iterator_first(v.begin());
  EXPECT_TRUE((std::is_same_v<iterator_first<std::map<int, int>::iterator>,
                              decltype(with_ctad)>));
  EXPECT_EQ(*with_ctad, 1);
}

TEST_F(IteratorAdaptorTest, HashMapSecond) {
  // Adapts an iterator to return the second value of a hash_map::iterator.
  typedef absl::node_hash_map<std::string, int> my_container;
  my_container values;
  for (int i = 0; i < kCount; ++i) {
    values[kFirst[i]] = kSecond[i];
  }
  for (iterator_second<my_container::iterator> it = values.begin();
       it != values.end(); ++it) {
    int v = *it;
    ASSERT_GT(v, 0);
  }
}

TEST_F(IteratorAdaptorTest, IteratorSecondConvertsToConst) {
  // Adapts an iterator to return the first value of a hash_map::iterator.
  typedef absl::node_hash_map<std::string, int> my_container;
  my_container values;
  for (int i = 0; i < kCount; ++i) {
    values[kFirst[i]] = kSecond[i];
  }
  iterator_second<my_container::iterator> iter = values.begin();
  iterator_second<my_container::const_iterator> c_iter = iter;
  for (; c_iter != values.end(); ++c_iter) {
    int v = *c_iter;
    ASSERT_GT(v, 0);
  }
}

TEST_F(IteratorAdaptorTest, IteratorSecondConstEqNonConst) {
  // verify that const and non-const iterators return the same reference.
  typedef std::vector<std::pair<int, int>> my_container;
  typedef iterator_second<my_container::iterator> my_iterator;
  typedef iterator_second<my_container::const_iterator> my_const_iterator;
  my_container values;
  for (int i = 0; i < kCount; ++i) {
    values.push_back(std::make_pair(i, i + 1));
  }
  my_iterator iter1 = values.begin();
  const my_iterator iter2 = iter1;
  my_const_iterator c_iter1 = iter1;
  const my_const_iterator c_iter2 = c_iter1;
  for (int i = 0; i < kCount; ++i) {
    int& v1 = iter1[i];
    int& v2 = iter2[i];
    EXPECT_EQ(&v1, &values[i].second);
    EXPECT_EQ(&v1, &v2);
    const int& cv1 = c_iter1[i];
    const int& cv2 = c_iter2[i];
    EXPECT_EQ(&cv1, &values[i].second);
    EXPECT_EQ(&cv1, &cv2);
  }
}

TEST_F(IteratorAdaptorTest, IteratorSecondFactory) {
  std::map<int, int> v = {{1, 2}};
  auto with_make = make_iterator_second(v.begin());
  EXPECT_TRUE((std::is_same_v<iterator_second<std::map<int, int>::iterator>,
                              decltype(with_make)>));
  EXPECT_EQ(*with_make, 2);

  auto with_ctad = iterator_second(v.begin());
  EXPECT_TRUE((std::is_same_v<iterator_second<std::map<int, int>::iterator>,
                              decltype(with_ctad)>));
  EXPECT_EQ(*with_ctad, 2);
}

TEST_F(IteratorAdaptorTest, IteratorSecondPtrConvertsToConst) {
  // Adapts an iterator to return the first value of a hash_map::iterator.
  typedef absl::flat_hash_map<std::string, int*> my_container;
  my_container values;
  for (int i = 0; i < kCount; ++i) {
    values[kFirst[i]] = &kSecond[i];
  }
  iterator_second_ptr<my_container::iterator> iter = values.begin();
  iterator_second_ptr<my_container::const_iterator> c_iter = iter;
  for (; c_iter != values.end(); ++c_iter) {
    int v = *c_iter;
    ASSERT_GT(v, 0);
  }
}

TEST_F(IteratorAdaptorTest, IteratorSecondPtrConstMap) {
  typedef const std::map<int, int*> ConstMap;
  ConstMap empty_map;

  iterator_second_ptr<ConstMap::const_iterator> it(empty_map.begin());
  ASSERT_TRUE(it == make_iterator_second_ptr(empty_map.end()));
  if (/* DISABLES CODE */ (false)) {
    // Just checking syntax/compilation/type-checking.
    // iterator_second_ptr<ConstMap::const_iterator>::value_type* v1 = &*it;
    iterator_second_ptr<ConstMap::const_iterator>::pointer v1 = &*it;
    iterator_second_ptr<ConstMap::const_iterator>::pointer v2 =
        &*it.operator->();
    if (&v1 != &v2) v1 = v2;
  }
}

TEST_F(IteratorAdaptorTest, IteratorSecondPtrFactory) {
  int i = 2;
  std::map<int, int*> v = {{1, &i}};
  auto with_make = make_iterator_second_ptr(v.begin());
  EXPECT_TRUE(
      (std::is_same_v<iterator_second_ptr<std::map<int, int*>::iterator>,
                      decltype(with_make)>));
  EXPECT_EQ(*with_make, 2);

  auto with_ctad = iterator_second_ptr(v.begin());
  EXPECT_TRUE(
      (std::is_same_v<iterator_second_ptr<std::map<int, int*>::iterator>,
                      decltype(with_ctad)>));
  EXPECT_EQ(*with_ctad, 2);
}

TEST_F(IteratorAdaptorTest, IteratorPtrConst) {
  // This is a regression test for a const-related bug that bit CL 47984515,
  // where a client created an iterator whose value type was "T* const".
  std::map<int*, int> m;
  make_iterator_ptr(make_iterator_first(m.begin()));
}

TEST_F(IteratorAdaptorTest, IteratorSecondPtrConstEqNonConst) {
  // verify that const and non-const iterators return the same reference.
  typedef std::vector<std::pair<int, int*>> my_container;
  typedef iterator_second_ptr<my_container::iterator> my_iterator;
  typedef iterator_second_ptr<my_container::const_iterator> my_const_iterator;
  my_container values;
  int ivalues[kCount];
  for (int i = 0; i < kCount; ++i) {
    ivalues[i] = i;
    values.push_back(std::make_pair(i, &ivalues[i]));
  }
  my_iterator iter1 = values.begin();
  const my_iterator iter2 = iter1;
  my_const_iterator c_iter1 = iter1;
  const my_const_iterator c_iter2 = c_iter1;
  for (int i = 0; i < kCount; ++i) {
    int& v1 = iter1[i];
    int& v2 = iter2[i];
    EXPECT_EQ(&v1, &ivalues[i]);
    EXPECT_EQ(&v1, &v2);
    const int& cv1 = c_iter1[i];
    const int& cv2 = c_iter2[i];
    EXPECT_EQ(&cv1, &ivalues[i]);
    EXPECT_EQ(&cv1, &cv2);
  }
}

TEST_F(IteratorAdaptorTest, HashMapFirstConst) {
  // Adapts an iterator to return the first value of a
  // hash_map::const_iterator.
  typedef absl::flat_hash_map<std::string, int> my_container;
  my_container values;
  for (int i = 0; i < kCount; ++i) {
    values[kFirst[i]] = kSecond[i];
  }
  const absl::flat_hash_map<std::string, int>* cvalues = &values;
  for (iterator_first<my_container::const_iterator> it = cvalues->begin();
       it != cvalues->end(); ++it) {
    ASSERT_GT(it->length(), 0);
  }
}

TEST_F(IteratorAdaptorTest, ListFirst) {
  // Adapts an iterator to return the first value of a list::iterator.
  typedef std::pair<std::string, int> my_pair;
  typedef std::list<my_pair> my_list;
  my_list values;
  for (int i = 0; i < kCount; ++i) {
    values.push_back(my_pair(kFirst[i], kSecond[i]));
  }
  int i = 0;
  for (iterator_first<my_list::iterator> it = values.begin();
       it != values.end(); ++it) {
    ASSERT_EQ(*it, kFirst[i++]);
  }
}

TEST_F(IteratorAdaptorTest, ListSecondConst) {
  // Adapts an iterator to return the second value from a list::const_iterator.
  typedef std::pair<std::string, int> my_pair;
  typedef std::list<my_pair> my_list;
  my_list values;
  for (int i = 0; i < kCount; ++i) {
    values.push_back(my_pair(kFirst[i], kSecond[i]));
  }
  int i = 0;
  const my_list* cvalues = &values;
  for (iterator_second<my_list::const_iterator> it = cvalues->begin();
       it != cvalues->end(); ++it) {
    ASSERT_EQ(*it, kSecond[i++]);
  }
}

TEST_F(IteratorAdaptorTest, VectorSecond) {
  // Adapts an iterator to return the second value of a vector::iterator.
  std::vector<std::pair<std::string, int>> values;
  for (int i = 0; i < kCount; ++i) {
    values.push_back(std::pair<std::string, int>(kFirst[i], kSecond[i]));
  }
  int i = 0;
  for (iterator_second<std::vector<std::pair<std::string, int>>::iterator> it =
           values.begin();
       it != values.end(); ++it) {
    ASSERT_EQ(*it, kSecond[i++]);
  }
}

// Tests iterator_second_ptr with a map where values are regular pointers.
TEST_F(IteratorAdaptorTest, HashMapSecondPtr) {
  typedef absl::flat_hash_map<std::string, int*> my_container;
  typedef iterator_second_ptr<my_container::iterator> my_iterator;
  my_container values;
  for (int i = 0; i < kCount; ++i) {
    values[kFirst[i]] = kSecond + i;
  }
  for (my_iterator it = values.begin(); it != values.end(); ++it) {
    int v = *it;

    // Make sure the iterator reference type is assignable ("int&" and not
    // "const int&").  If it isn't, this becomes a compile-time error.
    *it = v;

    ASSERT_GT(v, 0);
  }
}

// Tests iterator_second_ptr with a map where values are wrapped into
// linked_ptr.
TEST_F(IteratorAdaptorTest, HashMapSecondPtrLinkedPtr) {
  typedef absl::flat_hash_map<std::string, std::shared_ptr<int>> my_container;
  typedef iterator_second_ptr<my_container::iterator> my_iterator;
  my_container values;
  for (int i = 0; i < kCount; ++i) {
    values[kFirst[i]].reset(new int(kSecond[i]));
  }
  for (my_iterator it = values.begin(); it != values.end(); ++it) {
    ASSERT_EQ(&*it, it.operator->());
    int v = *it;
    *it = v;
    ASSERT_GT(v, 0);
  }
}

// Tests iterator_ptr with a vector where values are regular pointers.
TEST_F(IteratorAdaptorTest, IteratorPtrPtr) {
  typedef std::vector<int*> my_container;
  typedef iterator_ptr<my_container::iterator> my_iterator;
  my_container values;
  for (int i = 0; i < kCount; ++i) {
    values.push_back(kSecond + i);
  }
  int i = 0;
  for (my_iterator it = values.begin(); it != values.end(); ++it, ++i) {
    int v = *it;
    *it = v;
    ASSERT_EQ(v, kSecond[i]);
  }
}

TEST_F(IteratorAdaptorTest, IteratorPtrExplicitPtrType) {
  struct A {};
  struct B : A {};
  std::vector<B*> v;
  const std::vector<B*>& cv = v;
  iterator_ptr<std::vector<B*>::iterator> ip(v.begin());
  iterator_ptr<std::vector<B*>::const_iterator> cip(cv.begin());
}

TEST_F(IteratorAdaptorTest, IteratorPtrConstEqNonConst) {
  // verify that const and non-const iterators return the same reference.
  typedef std::vector<int*> my_container;
  typedef iterator_ptr<my_container::iterator> my_iterator;
  typedef iterator_ptr<my_container::const_iterator> my_const_iterator;
  my_container values;

  for (int i = 0; i < kCount; ++i) {
    values.push_back(kSecond + i);
  }
  my_iterator iter1 = values.begin();
  const my_iterator iter2 = iter1;
  my_const_iterator c_iter1 = iter1;
  const my_const_iterator c_iter2 = iter1;
  for (int i = 0; i < kCount; ++i) {
    int& v1 = iter1[i];
    int& v2 = iter2[i];
    EXPECT_EQ(&v1, kSecond + i);
    EXPECT_EQ(&v1, &v2);
    const int& cv1 = c_iter1[i];
    const int& cv2 = c_iter2[i];
    EXPECT_EQ(&cv1, kSecond + i);
    EXPECT_EQ(&cv1, &cv2);
  }
}

// Tests iterator_ptr with a vector where values are wrapped into linked_ptr.
TEST_F(IteratorAdaptorTest, IteratorPtrLinkedPtr) {
  typedef std::vector<std::shared_ptr<int>> my_container;
  typedef iterator_ptr<my_container::iterator> my_iterator;
  my_container values;
  for (int i = 0; i < kCount; ++i) {
    values.push_back(std::make_unique<int>(kSecond[i]));
  }
  int i = 0;
  for (my_iterator it = values.begin(); it != values.end(); ++it, ++i) {
    ASSERT_EQ(&*it, it.operator->());
    int v = *it;
    *it = v;
    ASSERT_EQ(v, kSecond[i]);
  }
}

TEST_F(IteratorAdaptorTest, IteratorPtrConvertsToConst) {
  int value = 1;
  std::vector<int*> values;
  values.push_back(&value);
  iterator_ptr<std::vector<int*>::iterator> iter = values.begin();
  iterator_ptr<std::vector<int*>::const_iterator> c_iter = iter;
  EXPECT_EQ(1, *c_iter);
}

TEST_F(IteratorAdaptorTest, IteratorPtrFactory) {
  int i = 2;
  std::vector<int*> v = {&i};
  auto with_make = make_iterator_ptr(v.begin());
  EXPECT_TRUE((std::is_same_v<iterator_ptr<std::vector<int*>::iterator>,
                              decltype(with_make)>));
  EXPECT_EQ(*with_make, 2);

  auto with_ctad = iterator_ptr(v.begin());
  EXPECT_TRUE((std::is_same_v<iterator_ptr<std::vector<int*>::iterator>,
                              decltype(with_ctad)>));
  EXPECT_EQ(*with_ctad, 2);
}

TEST_F(IteratorAdaptorTest, MutableIteratorPtrFactory) {
  int i = 2;
  std::vector<int*> v = {&i};
  auto with_make = make_mutable_iterator_ptr(v.begin());
  EXPECT_TRUE((std::is_same_v<mutable_iterator_ptr<std::vector<int*>::iterator>,
                              decltype(with_make)>));
  EXPECT_EQ(*with_make, 2);

  auto with_ctad = mutable_iterator_ptr(v.begin());
  EXPECT_TRUE((std::is_same_v<mutable_iterator_ptr<std::vector<int*>::iterator>,
                              decltype(with_ctad)>));
  EXPECT_EQ(*with_ctad, 2);
}

// Tests mutable_iterator_ptr with a const vector of raw pointers.
TEST_F(IteratorAdaptorTest, MutableIteratorPtrPtrOverConstContainer) {
  using my_iterator = mutable_iterator_ptr<std::vector<int*>::const_iterator>;
  std::vector<int*> mutable_values;
  for (int i = 0; i < kCount; ++i) {
    mutable_values.push_back(kSecond + i);
  }
  const std::vector<int*>& values = mutable_values;
  int i = 0;
  for (my_iterator it = values.begin(); it != values.end(); ++it, ++i) {
    int v = *it;
    *it = v;
    ASSERT_EQ(v, kSecond[i]);
  }
}

TEST_F(IteratorAdaptorTest, IteratorFirstHasRandomAccessMethods) {
  typedef std::vector<std::pair<std::string, int>> my_container;
  typedef iterator_first<my_container::iterator> my_iterator;

  my_container values;
  for (int i = 0; i < kCount; ++i) {
    values.push_back(std::pair<std::string, int>(kFirst[i], kSecond[i]));
  }

  my_iterator it1 = values.begin(), it2 = values.end();

  EXPECT_EQ(kCount, it2 - it1);
  EXPECT_TRUE(it1 < it2);
  it1 += kCount;
  EXPECT_TRUE(it1 == it2);
  it1 -= kCount;
  EXPECT_EQ(kFirst[0], *it1);
  EXPECT_EQ(kFirst[1], *(it1 + 1));
  EXPECT_TRUE(it1 == it2 - kCount);
  EXPECT_TRUE(kCount + it1 == it2);
  EXPECT_EQ(kFirst[1], it1[1]);
  it2[-1] = "baz";
  EXPECT_EQ("baz", values[kCount - 1].first);
}

TEST_F(IteratorAdaptorTest, IteratorSecondHasRandomAccessMethods) {
  typedef std::vector<std::pair<std::string, int>> my_container;
  typedef iterator_second<my_container::iterator> my_iterator;

  my_container values;
  for (int i = 0; i < kCount; ++i) {
    values.push_back(std::pair<std::string, int>(kFirst[i], kSecond[i]));
  }

  my_iterator it1 = values.begin(), it2 = values.end();

  EXPECT_EQ(kCount, it2 - it1);
  EXPECT_TRUE(it1 < it2);
  it1 += kCount;
  EXPECT_TRUE(it1 == it2);
  it1 -= kCount;
  EXPECT_EQ(kSecond[0], *it1);
  EXPECT_EQ(kSecond[1], *(it1 + 1));
  EXPECT_TRUE(it1 == it2 - kCount);
  EXPECT_TRUE(kCount + it1 == it2);
  EXPECT_EQ(kSecond[1], it1[1]);
  it2[-1] = 99;
  EXPECT_EQ(99, values[kCount - 1].second);
}

TEST_F(IteratorAdaptorTest, IteratorSecondPtrHasRandomAccessMethods) {
  typedef std::vector<std::pair<std::string, int*>> my_container;
  typedef iterator_second_ptr<my_container::iterator> my_iterator;

  ASSERT_GE(kCount, 2);
  int value1 = 17;
  int value2 = 99;
  my_container values;
  values.push_back(std::pair<std::string, int*>(kFirst[0], &value1));
  values.push_back(std::pair<std::string, int*>(kFirst[1], &value2));

  my_iterator it1 = values.begin(), it2 = values.end();

  EXPECT_EQ(2, it2 - it1);
  EXPECT_TRUE(it1 < it2);
  it1 += 2;
  EXPECT_TRUE(it1 == it2);
  it1 -= 2;
  EXPECT_EQ(17, *it1);
  EXPECT_EQ(99, *(it1 + 1));
  EXPECT_TRUE(it1 == it2 - 2);
  EXPECT_TRUE(2 + it1 == it2);
  EXPECT_EQ(99, it1[1]);
  it2[-1] = 88;
  EXPECT_EQ(88, value2);
}

TEST_F(IteratorAdaptorTest, IteratorPtrHasRandomAccessMethods) {
  typedef std::vector<int*> my_container;
  typedef iterator_ptr<my_container::iterator> my_iterator;

  int value1 = 17;
  int value2 = 99;
  my_container values;
  values.push_back(&value1);
  values.push_back(&value2);

  my_iterator it1 = values.begin(), it2 = values.end();

  EXPECT_EQ(2, it2 - it1);
  EXPECT_TRUE(it1 < it2);
  it1 += 2;
  EXPECT_TRUE(it1 == it2);
  it1 -= 2;
  EXPECT_EQ(17, *it1);
  EXPECT_EQ(99, *(it1 + 1));
  EXPECT_TRUE(it1 == it2 - 2);
  EXPECT_TRUE(2 + it1 == it2);
  EXPECT_EQ(99, it1[1]);
  it2[-1] = 88;
  EXPECT_EQ(88, value2);
}

class MyInputIterator
    : public std::iterator<std::input_iterator_tag, const int*> {
 private:
  using ptr = int*;

 public:
  using reference = const ptr&;

  explicit MyInputIterator(int* x) : x_(x) {}
  reference operator*() const { return x_; }
  MyInputIterator& operator++() {
    ++*x_;
    return *this;
  }

 private:
  int* x_;
};

TEST_F(IteratorAdaptorTest, IteratorPtrCanWrapInputIterator) {
  int x = 0;
  MyInputIterator it(&x);
  iterator_ptr<MyInputIterator> it1(it);

  EXPECT_EQ(0, *it1);
  ++it1;
  EXPECT_EQ(1, *it1);
  ++it1;
  EXPECT_EQ(2, *it1);
  ++it1;
}

// Tests that a default-constructed adaptor is equal to an adaptor explicitly
// constructed with a default underlying iterator.
TEST_F(IteratorAdaptorTest, DefaultAdaptorConstructorUsesDefaultValue) {
  iterator_first<std::pair<int, int>*> first_default;
  iterator_first<std::pair<int, int>*> first_null(nullptr);
  ASSERT_TRUE(first_default == first_null);

  iterator_second<std::pair<int, int>*> second_default;
  iterator_second<std::pair<int, int>*> second_null(nullptr);
  ASSERT_TRUE(second_default == second_null);

  iterator_second_ptr<std::pair<int, int*>*> second_ptr_default;
  iterator_second_ptr<std::pair<int, int*>*> second_ptr_null(nullptr);
  ASSERT_TRUE(second_ptr_default == second_ptr_null);

  iterator_ptr<int**> ptr_default;
  iterator_ptr<int**> ptr_null(nullptr);
  ASSERT_TRUE(ptr_default == ptr_null);
}

static absl::flat_hash_map<int, std::string> MakeMap() {
  absl::flat_hash_map<int, std::string> map;
  map[0] = "a";
  map[1] = "b";
  map[2] = "c";
  return map;
}

TEST_F(IteratorAdaptorTest, ValueView) {
  {
    auto c_map = MakeMap();

    absl::flat_hash_set<std::string> vals;
    absl::c_copy(value_view(c_map), std::inserter(vals, vals.end()));

    EXPECT_THAT(vals, UnorderedElementsAre("a", "b", "c"));
  }

  // Test that value_view can be called on a moved container.
  {
    auto c_map = MakeMap();
    EXPECT_THAT(value_view(std::move(c_map)),
                UnorderedElementsAre("a", "b", "c"));
  }

  // Test that value_view can be called on a temporary container.
  EXPECT_THAT(value_view(MakeMap()), UnorderedElementsAre("a", "b", "c"));
}

TEST_F(IteratorAdaptorTest, ValueView_Modify) {
  typedef std::map<int, int> MapType;
  MapType my_map;
  my_map[0] = 0;
  my_map[1] = 1;
  my_map[2] = 2;
  EXPECT_THAT(my_map, ElementsAre(Pair(0, 0), Pair(1, 1), Pair(2, 2)));

  value_view_t<MapType> vv = value_view(my_map);
  absl::c_replace(vv, 2, 3);
  absl::c_replace(vv, 1, 2);

  EXPECT_THAT(my_map, ElementsAre(Pair(0, 0), Pair(1, 2), Pair(2, 3)));
}

TEST_F(IteratorAdaptorTest, ValueViewOfValueView) {
  typedef std::pair<int, std::string> pair_int_str;
  typedef std::map<int, pair_int_str> map_int_pair_int_str;
  map_int_pair_int_str my_map;
  my_map[0] = std::make_pair(1, std::string("a"));
  my_map[2] = std::make_pair(3, std::string("b"));
  my_map[4] = std::make_pair(5, std::string("c"));

  // This is basically typechecking of the generated views. So we generate the
  // types and have the compiler verify the generated template instantiation.
  typedef value_view_t<map_int_pair_int_str>
      value_view_map_int_pair_int_str_type;

  static_assert(
      (std::is_same_v<pair_int_str,
                      value_view_map_int_pair_int_str_type::value_type>),
      "value_view_value_type_");

  typedef value_view_t<value_view_map_int_pair_int_str_type> view_view_type;

  static_assert((std::is_same_v<std::string, view_view_type::value_type>),
                "view_view_type_");

  value_view_map_int_pair_int_str_type vv = value_view(my_map);
  view_view_type helper = value_view(vv);

  EXPECT_THAT(std::set<std::string>(helper.begin(), helper.end()),
              ElementsAre("a", "b", "c"));
}

TEST_F(IteratorAdaptorTest, ValueViewAndKeyViewCopy) {
  std::map<int, std::string> my_map;
  my_map[0] = "0";
  my_map[1] = "1";
  my_map[2] = "2";
  std::set<int> keys;
  std::set<std::string> vals;
  absl::c_copy(key_view(my_map), std::inserter(keys, keys.end()));
  absl::c_copy(value_view(my_map), std::inserter(vals, vals.end()));
  EXPECT_THAT(keys, ElementsAre(0, 1, 2));
  EXPECT_THAT(vals, ElementsAre("0", "1", "2"));
}

TEST_F(IteratorAdaptorTest, ValueViewAndKeyViewRangeBasedLoop) {
  std::map<int, std::string> my_map;
  my_map[0] = "0";
  my_map[1] = "1";
  my_map[2] = "2";
  std::set<int> keys;
  std::set<std::string> vals;
  for (auto key : key_view(my_map)) {
    keys.insert(key);
  }
  for (const auto& val : value_view(my_map)) {
    vals.insert(val);
  }
  EXPECT_THAT(keys, ElementsAre(0, 1, 2));
  EXPECT_THAT(vals, ElementsAre("0", "1", "2"));
}

template <int N, typename Value, typename Key>
class FixedSizeContainer {
 public:
  // NOTE: the container does on purpose not define:
  // reference, const_reference, pointer, const_pointer, size_type,
  // difference_type, empty().
  typedef std::pair<Value, Key> value_type;
  typedef value_type* iterator;
  typedef const value_type* const_iterator;

  FixedSizeContainer() = default;
  const_iterator begin() const { return &values_[0]; }
  iterator begin() { return &values_[0]; }
  const_iterator end() const { return &values_[N]; }
  iterator end() { return &values_[N]; }
  value_type at(int n) const { return values_[n]; }
  value_type& operator[](int n) { return values_[n]; }
  int size() const { return N; }

 private:
  value_type values_[N ? N : 1];
  // NOTE: the container does on purpose not define:
  // reference, const_reference, pointer, const_pointer, size_type,
  // difference_type, empty().
};

TEST_F(IteratorAdaptorTest, ProvidesEmpty) {
  {
    FixedSizeContainer<0, int, int> container0;
    EXPECT_TRUE(value_view(container0).empty());
    FixedSizeContainer<1, int, int> container1;
    EXPECT_FALSE(value_view(container1).empty());
  }
  {
    std::map<int, int> container;
    EXPECT_TRUE(value_view(container).empty());
    container.insert(std::make_pair(0, 0));
    EXPECT_FALSE(value_view(container).empty());
  }
}

TEST_F(IteratorAdaptorTest, ValueViewWithPoorlyTypedHomeGrownContainer) {
  FixedSizeContainer<3, int, std::string> container;
  container[0] = std::make_pair(0, std::string("0"));
  container[1] = std::make_pair(1, std::string("1"));
  container[2] = std::make_pair(2, std::string("2"));
  EXPECT_EQ(3, container.size());
  EXPECT_EQ(container.at(0), std::make_pair(0, std::string("0")));
  EXPECT_EQ(container.at(1), std::make_pair(1, std::string("1")));
  EXPECT_EQ(container.at(2), std::make_pair(2, std::string("2")));
  std::vector<int> keys;
  std::vector<std::string> vals;
  absl::c_copy(key_view(container), std::back_inserter(keys));
  absl::c_copy(value_view(container), std::back_inserter(vals));
  EXPECT_THAT(keys, ElementsAre(0, 1, 2));
  EXPECT_THAT(vals, ElementsAre("0", "1", "2"));
}

TEST_F(IteratorAdaptorTest, ValueViewConstIterators) {
  absl::flat_hash_map<int, std::string> my_map;
  my_map[0] = "a";
  my_map[1] = "b";
  my_map[2] = "c";

  std::set<std::string> vals;
  // projection_view defines cbegin() and cend(); we're not invoking the
  // C++11 functions of the same name.
  for (iterator_second<absl::flat_hash_map<int, std::string>::const_iterator>
           it = value_view(my_map).cbegin();
       it != value_view(my_map).cend(); ++it) {
    vals.insert(*it);
  }

  EXPECT_TRUE(vals.find("a") != vals.end());
  EXPECT_TRUE(vals.find("b") != vals.end());
  EXPECT_TRUE(vals.find("c") != vals.end());
}

TEST_F(IteratorAdaptorTest, ValueViewInConstContext) {
  absl::flat_hash_map<int, std::string> my_map;
  my_map[0] = "a";
  my_map[1] = "b";
  my_map[2] = "c";

  std::set<std::string> vals;
  const auto const_view = value_view(my_map);
  for (iterator_second<absl::flat_hash_map<int, std::string>::const_iterator>
           it = const_view.begin();
       it != const_view.end(); ++it) {
    vals.insert(*it);
  }

  EXPECT_TRUE(vals.find("a") != vals.end());
  EXPECT_TRUE(vals.find("b") != vals.end());
  EXPECT_TRUE(vals.find("c") != vals.end());
}

TEST_F(IteratorAdaptorTest, ConstValueView) {
  absl::flat_hash_map<int, std::string> my_map;
  my_map[0] = "a";
  my_map[1] = "b";
  my_map[2] = "c";

  const absl::flat_hash_map<int, std::string>& const_map = my_map;

  std::set<std::string> vals;
  for (iterator_second<absl::flat_hash_map<int, std::string>::const_iterator>
           it = value_view(const_map).begin();
       it != value_view(const_map).end(); ++it) {
    vals.insert(*it);
  }

  EXPECT_TRUE(vals.find("a") != vals.end());
  EXPECT_TRUE(vals.find("b") != vals.end());
  EXPECT_TRUE(vals.find("c") != vals.end());
}

TEST_F(IteratorAdaptorTest, ConstValueViewConstIterators) {
  absl::flat_hash_map<int, std::string> my_map;
  my_map[0] = "a";
  my_map[1] = "b";
  my_map[2] = "c";

  const absl::flat_hash_map<int, std::string>& const_map = my_map;

  std::set<std::string> vals;
  // projection_view defines cbegin() and cend(); we're not invoking the
  // C++11 functions of the same name.
  for (iterator_second<absl::flat_hash_map<int, std::string>::const_iterator>
           it = value_view(const_map).cbegin();
       it != value_view(const_map).cend(); ++it) {
    vals.insert(*it);
  }

  EXPECT_TRUE(vals.find("a") != vals.end());
  EXPECT_TRUE(vals.find("b") != vals.end());
  EXPECT_TRUE(vals.find("c") != vals.end());
}

TEST_F(IteratorAdaptorTest, ValueViewOfReferences) {
  // Create a map containing a reference type value. absl::flat_hash_map does
  // not allow the use of reference types as template arguments, so we use
  // std::map instead.
  using MapType = std::map<int, std::string&>;
  std::vector<std::string> strings = {"a", "b", "c"};
  MapType my_map = {{0, strings[0]}, {1, strings[1]}, {2, strings[2]}};

  std::set<std::string> vals;
  absl::c_copy(value_view(my_map), std::inserter(vals, vals.end()));
  EXPECT_THAT(vals, ElementsAreArray(strings));
}

TEST_F(IteratorAdaptorTest, ConstValueViewInConstContext) {
  absl::flat_hash_map<int, std::string> my_map;
  my_map[0] = "a";
  my_map[1] = "b";
  my_map[2] = "c";

  const absl::flat_hash_map<int, std::string>& const_map = my_map;

  std::set<std::string> vals;
  const value_view_t<const absl::flat_hash_map<int, std::string>> const_view =
      value_view(const_map);
  for (iterator_second<absl::flat_hash_map<int, std::string>::const_iterator>
           it = const_view.begin();
       it != const_view.end(); ++it) {
    vals.insert(*it);
  }

  EXPECT_TRUE(vals.find("a") != vals.end());
  EXPECT_TRUE(vals.find("b") != vals.end());
  EXPECT_TRUE(vals.find("c") != vals.end());
}

TEST_F(IteratorAdaptorTest, KeyView) {
  absl::flat_hash_map<int, std::string> my_map;
  my_map[0] = "a";
  my_map[1] = "b";
  my_map[2] = "c";

  std::set<int> vals;
  for (iterator_first<absl::flat_hash_map<int, std::string>::iterator> it =
           key_view(my_map).begin();
       it != key_view(my_map).end(); ++it) {
    vals.insert(*it);
  }

  EXPECT_TRUE(vals.find(0) != vals.end());
  EXPECT_TRUE(vals.find(1) != vals.end());
  EXPECT_TRUE(vals.find(2) != vals.end());
}

TEST_F(IteratorAdaptorTest, KeyViewConstIterators) {
  absl::flat_hash_map<int, std::string> my_map;
  my_map[0] = "a";
  my_map[1] = "b";
  my_map[2] = "c";

  std::set<int> vals;
  // projection_view defines cbegin() and cend(); we're not invoking the
  // C++11 functions of the same name.
  for (iterator_first<absl::flat_hash_map<int, std::string>::const_iterator>
           it = key_view(my_map).cbegin();
       it != key_view(my_map).cend(); ++it) {
    vals.insert(*it);
  }

  EXPECT_TRUE(vals.find(0) != vals.end());
  EXPECT_TRUE(vals.find(1) != vals.end());
  EXPECT_TRUE(vals.find(2) != vals.end());
}

TEST_F(IteratorAdaptorTest, KeyViewInConstContext) {
  absl::flat_hash_map<int, std::string> my_map;
  my_map[0] = "a";
  my_map[1] = "b";
  my_map[2] = "c";

  std::set<int> vals;
  const key_view_t<absl::flat_hash_map<int, std::string>> const_view =
      key_view(my_map);
  for (iterator_first<absl::flat_hash_map<int, std::string>::const_iterator>
           it = const_view.begin();
       it != const_view.end(); ++it) {
    vals.insert(*it);
  }

  EXPECT_TRUE(vals.find(0) != vals.end());
  EXPECT_TRUE(vals.find(1) != vals.end());
  EXPECT_TRUE(vals.find(2) != vals.end());
}

TEST_F(IteratorAdaptorTest, ConstKeyView) {
  absl::flat_hash_map<int, std::string> my_map;
  my_map[0] = "a";
  my_map[1] = "b";
  my_map[2] = "c";

  const absl::flat_hash_map<int, std::string>& const_map = my_map;

  std::set<int> vals;
  for (iterator_first<absl::flat_hash_map<int, std::string>::const_iterator>
           it = key_view(const_map).begin();
       it != key_view(const_map).end(); ++it) {
    vals.insert(*it);
  }

  EXPECT_TRUE(vals.find(0) != vals.end());
  EXPECT_TRUE(vals.find(1) != vals.end());
  EXPECT_TRUE(vals.find(2) != vals.end());
}

TEST_F(IteratorAdaptorTest, ConstKeyViewConstIterators) {
  absl::flat_hash_map<int, std::string> my_map;
  my_map[0] = "a";
  my_map[1] = "b";
  my_map[2] = "c";

  const absl::flat_hash_map<int, std::string>& const_map = my_map;

  std::set<int> vals;
  // projection_view defines cbegin() and cend(); we're not invoking the
  // C++11 functions of the same name.
  for (iterator_first<absl::flat_hash_map<int, std::string>::const_iterator>
           it = key_view(const_map).cbegin();
       it != key_view(const_map).cend(); ++it) {
    vals.insert(*it);
  }

  EXPECT_TRUE(vals.find(0) != vals.end());
  EXPECT_TRUE(vals.find(1) != vals.end());
  EXPECT_TRUE(vals.find(2) != vals.end());
}

TEST_F(IteratorAdaptorTest, ConstKeyViewInConstContext) {
  absl::flat_hash_map<int, std::string> my_map;
  my_map[0] = "a";
  my_map[1] = "b";
  my_map[2] = "c";

  const absl::flat_hash_map<int, std::string>& const_map = my_map;

  std::set<int> vals;
  const key_view_t<const absl::flat_hash_map<int, std::string>> const_view =
      key_view(const_map);
  for (iterator_first<absl::flat_hash_map<int, std::string>::const_iterator>
           it = const_view.begin();
       it != const_view.end(); ++it) {
    vals.insert(*it);
  }

  EXPECT_TRUE(vals.find(0) != vals.end());
  EXPECT_TRUE(vals.find(1) != vals.end());
  EXPECT_TRUE(vals.find(2) != vals.end());
}

TEST_F(IteratorAdaptorTest, IteratorViewHelperDefinesIterator) {
  absl::flat_hash_set<int> my_set;
  my_set.insert(1);
  my_set.insert(0);
  my_set.insert(2);

  using SetView = internal::container_view<absl::flat_hash_set<int>,
                                           internal::ForwardPolicy>;
  SetView set_view(my_set);
  absl::flat_hash_set<int> vals;
  for (auto it = set_view.begin(); it != set_view.end(); ++it) {
    vals.insert(*it);
  }

  EXPECT_TRUE(vals.find(0) != vals.end());
  EXPECT_TRUE(vals.find(1) != vals.end());
  EXPECT_TRUE(vals.find(2) != vals.end());
}

TEST_F(IteratorAdaptorTest, IteratorViewHelperDefinesConstIterator) {
  absl::flat_hash_set<int> my_set;
  my_set.insert(1);
  my_set.insert(0);
  my_set.insert(2);

  using SetView = internal::container_view<absl::flat_hash_set<int>,
                                           internal::ForwardPolicy>;
  SetView set_view(my_set);
  absl::flat_hash_set<int> vals;
  for (SetView::const_iterator it = set_view.begin(); it != set_view.end();
       ++it) {
    vals.insert(*it);
  }

  EXPECT_TRUE(vals.find(0) != vals.end());
  EXPECT_TRUE(vals.find(1) != vals.end());
  EXPECT_TRUE(vals.find(2) != vals.end());
}

TEST_F(IteratorAdaptorTest, ViewTypeParameterConstVsNonConst) {
  typedef absl::flat_hash_map<int, int> M;
  M m;
  const M& cm = m;

  // key_view:
  typedef key_view_t<M> KV;
  typedef key_view_t<const M> KVC;
  (void)absl::implicit_cast<KV>(key_view(m));     // lvalue
  (void)absl::implicit_cast<KVC>(key_view(m));    // conversion to const
  (void)absl::implicit_cast<KVC>(key_view(cm));   // const from const lvalue
  (void)absl::implicit_cast<KVC>(key_view(M()));  // const from rvalue
  // Direct initialization (without key_view function)
  (void)KV(m);
  (void)KVC(m);
  (void)KVC(cm);
  (void)KVC(M{});

  // value_view:
  typedef value_view_t<M> VV;
  typedef value_view_t<const M> VVC;
  (void)absl::implicit_cast<VV>(value_view(m));     // lvalue
  (void)absl::implicit_cast<VVC>(value_view(m));    // conversion to const
  (void)absl::implicit_cast<VVC>(value_view(cm));   // const from const lvalue
  (void)absl::implicit_cast<VVC>(value_view(M()));  // const from rvalue
  // Direct initialization (without value_view function)
  (void)VV(m);
  (void)VVC(m);
  (void)VVC(cm);
  (void)VVC(M{});
}

TEST_F(IteratorAdaptorTest, EmptyAndSize) {
  {
    FixedSizeContainer<0, int, std::string*> container;
    EXPECT_TRUE(key_view(container).empty());
    EXPECT_TRUE(value_view(container).empty());
    EXPECT_EQ(0, key_view(container).size());
    EXPECT_EQ(0, value_view(container).size());
  }
  {
    FixedSizeContainer<2, int, std::string*> container;
    EXPECT_FALSE(key_view(container).empty());
    EXPECT_FALSE(value_view(container).empty());
    EXPECT_EQ(2, key_view(container).size());
    EXPECT_EQ(2, value_view(container).size());
  }
  {
    std::map<std::string, std::string*> container;
    EXPECT_TRUE(key_view(container).empty());
    EXPECT_TRUE(value_view(container).empty());
    EXPECT_EQ(0, key_view(container).size());
    EXPECT_EQ(0, value_view(container).size());
    std::string s0 = "s0";
    std::string s1 = "s1";
    container.insert(std::make_pair("0", &s0));
    container.insert(std::make_pair("1", &s0));
    EXPECT_FALSE(key_view(container).empty());
    EXPECT_FALSE(value_view(container).empty());
    EXPECT_EQ(2, key_view(container).size());
    EXPECT_EQ(2, value_view(container).size());
  }
}

TEST_F(IteratorAdaptorTest, View_IsEmpty) {
  EXPECT_THAT(key_view(std::map<int, int>()), IsEmpty());
  EXPECT_THAT(key_view(FixedSizeContainer<2, int, int>()), Not(IsEmpty()));
}

TEST_F(IteratorAdaptorTest, View_SizeIs) {
  EXPECT_THAT(key_view(std::map<int, int>()), SizeIs(0));
  EXPECT_THAT(key_view(FixedSizeContainer<2, int, int>()), SizeIs(2));
}

TEST_F(IteratorAdaptorTest, View_Pointwise) {
  typedef std::map<int, std::string> MapType;
  MapType my_map;
  my_map[0] = "a";
  my_map[1] = "b";
  my_map[2] = "c";

  std::vector<std::string> expected;
  expected.push_back("a");
  expected.push_back("b");
  expected.push_back("c");

  EXPECT_THAT(value_view(my_map), Pointwise(Eq(), expected));
}

TEST_F(IteratorAdaptorTest, DerefView) {
  typedef std::vector<int*> ContainerType;
  int v0 = 0;
  int v1 = 1;
  ContainerType c;
  c.push_back(&v0);
  c.push_back(&v1);
  EXPECT_THAT(deref_view(c), ElementsAre(0, 1));
  *deref_view(c).begin() = 2;
  EXPECT_THAT(v0, 2);
  EXPECT_THAT(deref_view(c), ElementsAre(2, 1));
  const std::vector<int*> cc(c);
  EXPECT_THAT(deref_view(cc), ElementsAre(2, 1));
}

TEST_F(IteratorAdaptorTest, ConstDerefView) {
  typedef std::vector<const std::string*> ContainerType;
  const std::string s0 = "0";
  const std::string s1 = "1";
  ContainerType c;
  c.push_back(&s0);
  c.push_back(&s1);
  EXPECT_THAT(deref_view(c), ElementsAre("0", "1"));
}

template <typename ContainerType>
void MutableDerefViewTest() {
  std::string s0 = "0";
  std::string s1 = "1";
  ContainerType c = {&s0, &s1};
  for (std::string& s : mutable_deref_view(c)) {
    s.append(".0");
  }
  EXPECT_THAT(deref_view(c), ElementsAre("0.0", "1.0"));
}

TEST_F(IteratorAdaptorTest, MutableDerefView) {
  MutableDerefViewTest<std::vector<std::string*>>();
  MutableDerefViewTest<const std::vector<std::string*>>();
}

TEST_F(IteratorAdaptorTest, DerefSecondView) {
  typedef std::map<int, int*> ContainerType;
  int v0 = 0;
  int v1 = 1;
  ContainerType c;
  c.insert({10, &v0});
  c.insert({11, &v1});
  EXPECT_THAT(deref_second_view(c), ElementsAre(0, 1));
  *deref_second_view(c).begin() = 2;
  EXPECT_THAT(v0, 2);
  EXPECT_THAT(deref_second_view(c), ElementsAre(2, 1));
  const std::map<int, int*> cc(c);
  EXPECT_THAT(deref_second_view(cc), ElementsAre(2, 1));
}

TEST_F(IteratorAdaptorTest, ConstDerefSecondView) {
  typedef std::map<int, const std::string*> ContainerType;
  const std::string s0 = "0";
  const std::string s1 = "1";
  ContainerType c;
  c.insert({10, &s0});
  c.insert({11, &s1});
  EXPECT_THAT(deref_second_view(c), ElementsAre("0", "1"));
}

namespace {
template <class T>
std::vector<int> ToVec(const T& t) {
  return std::vector<int>(t.begin(), t.end());
}
}  // namespace

TEST_F(IteratorAdaptorTest, ReverseView) {
  using gtl::reversed_view;

  int arr[] = {0, 1, 2, 3, 4, 5, 6};
  int* arr_end = arr + std::size(arr);
  std::vector<int> vec(arr, arr_end);
  const std::vector<int> cvec(arr, arr_end);

  EXPECT_THAT(ToVec(reversed_view(vec)), ElementsAre(6, 5, 4, 3, 2, 1, 0));
  EXPECT_THAT(ToVec(reversed_view(cvec)), ElementsAre(6, 5, 4, 3, 2, 1, 0));
}

// Testing reversed_view on a container without rbegin/rend methods.
TEST_F(IteratorAdaptorTest, ReverseViewMakeReverseIterator) {
  struct MyVec {
    using container = std::vector<int>;
    using iterator = typename container::iterator;
    using const_iterator = typename container::const_iterator;

    const_iterator begin() const { return vec.begin(); }
    const_iterator end() const { return vec.end(); }
    iterator begin() { return vec.begin(); }
    iterator end() { return vec.end(); }

    container vec;
  };
  MyVec v = {.vec = {1, 2, 3}};
  EXPECT_THAT(gtl::reversed_view(v), ElementsAre(3, 2, 1));
}

TEST_F(IteratorAdaptorTest, IteratorPtrConstConversions) {
  // Users depend on this. It has to keep working.
  std::vector<int*> v;
  const std::vector<int*>& cv = v;
  EXPECT_TRUE(make_iterator_ptr(cv.end()) == make_iterator_ptr(v.end()));
  EXPECT_FALSE(make_iterator_ptr(cv.end()) != make_iterator_ptr(v.end()));
  // EXPECT_TRUE(make_iterator_ptr(v.end()) == make_iterator_ptr(cv.end()));
  // EXPECT_FALSE(make_iterator_ptr(v.end()) != make_iterator_ptr(cv.end()));
}

TEST_F(IteratorAdaptorTest, IteratorPtrDeepConst) {
  typedef std::vector<int*> PtrsToMutable;
  typedef gtl::iterator_ptr<PtrsToMutable::const_iterator> ConstIter;
  EXPECT_TRUE((std::is_same_v<ConstIter::reference, const int&>));
  EXPECT_TRUE(IsConst<ConstIter::reference>::value);

  typedef gtl::iterator_ptr<PtrsToMutable::iterator> Iter;
  EXPECT_TRUE((std::is_same_v<Iter::reference, int&>));
  EXPECT_FALSE(IsConst<Iter::reference>::value);
}

TEST_F(IteratorAdaptorTest, IteratorPtrSubtract) {
  std::vector<std::unique_ptr<int>> v;
  v.push_back(std::make_unique<int>(239));
  EXPECT_EQ(gtl::make_iterator_ptr(v.end()) - gtl::make_iterator_ptr(v.begin()),
            1);

  auto view = deref_view(v);
  EXPECT_EQ(view.end() - view.begin(), 1);
}

TEST_F(IteratorAdaptorTest, DerefViewPointersToConst) {
  int a = 0, b = 1, c = 2;
  std::vector<const int*> v = {&a, &b, &c};
  auto view = deref_view(v);
  EXPECT_THAT(view, ElementsAre(0, 1, 2));
  std::vector<int> deref_v(view.begin(), view.end());
  EXPECT_THAT(deref_v, ElementsAre(0, 1, 2));
}

TEST_F(IteratorAdaptorTest, DerefDropsValueQualifiers) {
  // We test that `deref_view` and `deref_second_view` remove cv-qualifiers
  // from value_type (cv qualifiers are kept on pointer/reference types).
  using deref_t = deref_view_t<std::vector<const int*>>;
  EXPECT_TRUE((std::is_same_v<deref_t::value_type, int>));
  EXPECT_TRUE((std::is_same_v<std::iterator_traits<deref_t::iterator>::pointer,
                              const int*>));
  EXPECT_TRUE(
      (std::is_same_v<std::iterator_traits<deref_t::iterator>::reference,
                      const int&>));

  using deref_second_t = deref_second_view_t<std::map<int, const int*>>;
  EXPECT_TRUE((std::is_same_v<deref_second_t::value_type, int>));
  EXPECT_TRUE(
      (std::is_same_v<std::iterator_traits<deref_second_t::iterator>::pointer,
                      const int*>));
  EXPECT_TRUE(
      (std::is_same_v<std::iterator_traits<deref_second_t::iterator>::reference,
                      const int&>));
}

TEST_F(IteratorAdaptorTest, ReverseViewCxx11) {
  using gtl::reversed_view;

  int arr[] = {0, 1, 2, 3, 4, 5, 6};
  int* arr_end = arr + std::size(arr);
  std::vector<int> vec(arr, arr_end);

  // Try updates and demonstrate this work with C++11 for loops.
  for (auto& i : reversed_view(vec)) ++i;
  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 4, 5, 6, 7));
}

TEST_F(IteratorAdaptorTest, ConstViewOverMutableContainer) {
  using gtl::reversed_view;

  std::vector<int> vec{0, 1, 2, 3, 4, 5, 6};

  const auto vec_reversed = reversed_view(vec);
  for (auto& i : vec_reversed) ++i;
  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 4, 5, 6, 7));

  const auto vec_reversed_reversed = reversed_view(vec_reversed);
  for (auto& i : vec_reversed_reversed) ++i;
  EXPECT_THAT(vec, ElementsAre(2, 3, 4, 5, 6, 7, 8));
}

TEST_F(IteratorAdaptorTest, BaseIterDanglingRefFirst) {
  // Some iterators will hold 'on-board storage' for a synthesized value.
  // We must take care not to pull our adapted reference from
  // a temporary copy of the base iterator. See b/15113033.
  typedef std::pair<X, int> Val;
  InlineStorageIter<Val> iter;
  gtl::iterator_first<InlineStorageIter<Val>> iter2(iter);
  EXPECT_EQ(&iter2.base()->first, &*iter2);
  EXPECT_EQ(&iter2.base()->first.d, &iter2->d);
}

TEST_F(IteratorAdaptorTest, BaseIterDanglingRefSecond) {
  typedef std::pair<int, X> Val;
  InlineStorageIter<Val> iter;
  gtl::iterator_second<InlineStorageIter<Val>> iter2(iter);
  EXPECT_EQ(&iter2.base()->second, &*iter2);
  EXPECT_EQ(&iter2.base()->second.d, &iter2->d);
}

TEST_F(IteratorAdaptorTest, ProjectionView) {
  struct Foo {
    std::string a;
    int b;
  };
  std::vector<Foo> v = {{.a = "1", .b = 2}, {.a = "3", .b = 4}};

  EXPECT_THAT(projection_view(v, &Foo::a), ElementsAre("1", "3"));
  EXPECT_THAT(projection_view(v, &Foo::b), ElementsAre(2, 4));
}

TEST_F(IteratorAdaptorTest, ProjectionViewWithLambda) {
  struct Foo {
    std::string a;
    int b;
  };
  std::vector<Foo> v = {{.a = "1", .b = 2}, {.a = "3", .b = 4}};

  auto get_a = [](const Foo& f) -> const std::string& { return f.a; };
  auto get_b = [](const Foo& f) -> int { return f.b; };

  EXPECT_THAT(projection_view(v, get_a), ElementsAre("1", "3"));
  EXPECT_THAT(projection_view(v, std::move(get_b)), ElementsAre(2, 4));
}

TEST_F(IteratorAdaptorTest, ProjectionViewInputIteratorWithArrow) {
  struct IntView {
    explicit IntView(const int* val) : val_(val) {}
    int get() const { return *val_; }

   private:
    const int* val_;
  };

  std::vector<int> v = {1, 2};
  auto view = projection_view(v, [](const int& val) { return IntView(&val); });

  auto it = view.begin();
  EXPECT_EQ(it->get(), 1);

  // Mutate the first element of v directly and verify it is reflected through
  // the proxy.
  v[0] = 10;
  EXPECT_EQ(it->get(), 10);

  ++it;
  EXPECT_EQ(it->get(), 2);
}

TEST_F(IteratorAdaptorTest, ProjectionViewWithFunction) {
  struct Foo {
    std::string a;
    int b;
    static const std::string& GetA(const Foo& f) { return f.a; }
    static int GetB(const Foo& f) { return f.b; }
  };
  std::vector<Foo> v = {{.a = "1", .b = 2}, {.a = "3", .b = 4}};

  EXPECT_THAT(projection_view(v, &Foo::GetA), ElementsAre("1", "3"));
  EXPECT_THAT(projection_view(v, Foo::GetB), ElementsAre(2, 4));
}

TEST_F(IteratorAdaptorTest, ProjectionViewFind) {
  struct Foo {
    std::string a;
    int b;
  };
  std::vector<Foo> v = {{.a = "1", .b = 2}, {.a = "3", .b = 4}};

  auto view = projection_view(v, &Foo::a);
  auto it = absl::c_find(view, "3").base();
  ASSERT_NE(it, v.end());
  EXPECT_EQ(it->b, 4);

  EXPECT_TRUE(absl::c_linear_search(projection_view(v, &Foo::b), 2));
}

TEST_F(IteratorAdaptorTest, ProjectionViewSort) {
  struct Foo {
    std::string a;
    int b;
  };
  std::vector<Foo> v = {
      {.a = "5", .b = 3}, {.a = "1", .b = 2}, {.a = "3", .b = 4}};
  EXPECT_THAT(projection_view(v, &Foo::a), ElementsAre("5", "1", "3"));
  EXPECT_THAT(projection_view(v, &Foo::b), ElementsAre(3, 2, 4));
  auto view = projection_view(v, &Foo::a);
  absl::c_sort(view);
  EXPECT_THAT(projection_view(v, &Foo::a), ElementsAre("1", "3", "5"));
  EXPECT_THAT(projection_view(v, &Foo::b), ElementsAre(3, 2, 4));
}

TEST_F(IteratorAdaptorTest, StatefulExtractor) {
  struct IndexExtractor {
    std::string& operator()(int x) const { return (*data)[x]; }
    std::vector<std::string>* data;
  };

  std::vector<std::string> v = {"a", "b", "c"};
  std::vector<int> indices = {2, 0, 1};

  EXPECT_THAT(projection_view(indices, IndexExtractor{.data = &v}),
              ElementsAre("c", "a", "b"));

  auto view = projection_view(indices, IndexExtractor{.data = &v});
  absl::c_sort(view);
  EXPECT_THAT(v, ElementsAre("b", "c", "a"));
}

TEST_F(IteratorAdaptorTest, ProjectionViewMove) {
  struct Foo {
    int number;
    std::unique_ptr<int> pointer;
  };
  std::vector<Foo> v;
  v.push_back({1, std::make_unique<int>(1)});
  v.push_back({2, std::make_unique<int>(2)});
  EXPECT_THAT(projection_view(v, &Foo::pointer),
              ElementsAre(Pointee(1), Pointee(2)));

  std::vector<std::unique_ptr<int>> pointers;
  absl::c_move(projection_view(v, &Foo::pointer), std::back_inserter(pointers));

  EXPECT_THAT(pointers, ElementsAre(Pointee(1), Pointee(2)));
}

TEST_F(IteratorAdaptorTest, ProjectionViewTie) {
  struct Person {
    std::string first_name;
    std::string second_name;
    std::string city;
    std::string occupation;
  };
  std::vector<Person> persons = {{"Anton", "M", "Dublin", "SWE"},
                                 {"Pierre", "L", "Paris", "Manager"}};
  struct FullNameExtractor {
    auto operator()(const Person& p) const {
      return std::tie(p.first_name, p.second_name);
    }
  };
  EXPECT_TRUE(absl::c_linear_search(
      projection_view(persons, FullNameExtractor{}), std::tuple{"Anton", "M"}));

  EXPECT_TRUE(absl::c_linear_search(projection_view(persons,
                                                    [](const auto& p) {
                                                      return std::tie(
                                                          p.city, p.occupation);
                                                    }),
                                    std::tuple{"Paris", "Manager"}));
}

TEST_F(IteratorAdaptorTest, ProjectionViewExample) {
  struct Googler {
    int64_t id;
    std::string username;
  };
  std::vector<Googler> googlers = {{.id = 1, .username = "sergey"},
                                   {.id = 2, .username = "page"}};

  EXPECT_THAT(gtl::projection_view(googlers, &Googler::username),
              ElementsAre("sergey", "page"));

  std::vector<std::string> usernames;
  for (const std::string& username :
       gtl::projection_view(googlers, &Googler::username)) {
    usernames.push_back(username);
  }
  EXPECT_THAT(usernames, ElementsAre("sergey", "page"));
}

TEST_F(IteratorAdaptorTest, ProjectionViewProto) {
  using ::google::protobuf::Duration;
  auto make_duration = [](int64_t seconds, int32_t nanos) {
    google::protobuf::Duration d;
    d.set_seconds(seconds);
    d.set_nanos(nanos);
    return d;
  };
  std::vector<Duration> durations = {make_duration(12, 34),
                                     make_duration(56, 78)};
  EXPECT_THAT(projection_view(durations, &Duration::seconds),
              ElementsAre(12, 56));
  EXPECT_THAT(projection_view(durations, &Duration::nanos),
              ElementsAre(34, 78));
  EXPECT_THAT(
      projection_view(durations, [](const auto& d) { return d.seconds(); }),
      ElementsAre(12, 56));
  EXPECT_THAT(
      projection_view(durations, [](const auto& d) { return d.nanos(); }),
      ElementsAre(34, 78));
}

template <typename T>
using iter_category = typename T::iterator::iterator_category;

template <typename T, typename Category>
constexpr bool kHasIterCategory = std::is_same_v<iter_category<T>, Category>;

struct DoublingExtractor {
  template <typename T>
  auto operator()(T& x) const {
    return x + x;
  }
};

TEST_F(IteratorAdaptorTest, ProjectionViewIteratorCategory) {
  using d = std::deque<int>;
  using l = std::forward_list<int>;
  using s = std::set<int>;

  ASSERT_TRUE((kHasIterCategory<d, std::random_access_iterator_tag>));
  ASSERT_TRUE((kHasIterCategory<l, std::forward_iterator_tag>));
  ASSERT_TRUE((kHasIterCategory<s, std::bidirectional_iterator_tag>));

  {
    using extracted_d = projection_view_t<d, DoublingExtractor>;
    using extracted_l = projection_view_t<l, DoublingExtractor>;
    using extracted_s = projection_view_t<s, DoublingExtractor>;

    EXPECT_TRUE((kHasIterCategory<extracted_d, std::input_iterator_tag>));
    EXPECT_TRUE((kHasIterCategory<extracted_l, std::input_iterator_tag>));
    EXPECT_TRUE((kHasIterCategory<extracted_s, std::input_iterator_tag>));
  }

  {
    using extracted_d = projection_view_t<d, internal::NoopExtractor>;
    using extracted_l = projection_view_t<l, internal::NoopExtractor>;
    using extracted_s = projection_view_t<s, internal::NoopExtractor>;

    EXPECT_TRUE(
        (kHasIterCategory<extracted_d, std::random_access_iterator_tag>));
    EXPECT_TRUE((kHasIterCategory<extracted_l, std::forward_iterator_tag>));
    EXPECT_TRUE(
        (kHasIterCategory<extracted_s, std::bidirectional_iterator_tag>));
  }
}

TEST_F(IteratorAdaptorTest, ProjectionViewOperatorAt) {
  struct User {
    int64_t id;
    std::string name;
  };
  std::vector<User> users = {{.id = 1, .name = "michael"},
                             {.id = 2, .name = "elliot"}};

  auto view = gtl::projection_view(users, &User::name);
  EXPECT_THAT(view[0], Eq("michael"));
  EXPECT_THAT(view[1], Eq("elliot"));

  // Checking mutability
  view[0] = "john";
  view[1] = "doe";

  EXPECT_THAT(users[0].name, Eq("john"));
  EXPECT_THAT(users[1].name, Eq("doe"));
}

TEST_F(IteratorAdaptorTest, ProjectionViewAt) {
  struct User {
    int64_t id;
    std::string name;
  };
  std::vector<User> users = {{.id = 1, .name = "michael"},
                             {.id = 2, .name = "elliot"}};

  auto view = gtl::projection_view(users, &User::name);
  EXPECT_THAT(view.at(0), Eq("michael"));
  EXPECT_THAT(view.at(1), Eq("elliot"));

  // Checking mutability
  view.at(0) = "john";
  view.at(1) = "doe";

  EXPECT_THAT(users[0].name, Eq("john"));
  EXPECT_THAT(users[1].name, Eq("doe"));
}

TEST_F(IteratorAdaptorTest, ProjectionViewAtThrows) {
  struct User {
    int64_t id;
    std::string name;
  };
  std::vector<User> users = {{.id = 1, .name = "sergey"},
                             {.id = 2, .name = "page"}};

  auto view = gtl::projection_view(users, &User::name);
  EXPECT_DEATH_IF_SUPPORTED(view.at(239), "failed bounds check");

  const auto const_view = gtl::projection_view(users, &User::name);
  EXPECT_DEATH_IF_SUPPORTED(const_view.at(239), "failed bounds check");
}

TEST_F(IteratorAdaptorTest, StackedProjectionViewOperatorAt) {
  std::vector<int> v = {1, 2, 3};
  auto view = gtl::projection_view(v, [](int x) { return x * 2; });
  EXPECT_THAT(view[1], Eq(4));
  EXPECT_THAT(view[2], Eq(6));
  EXPECT_THAT(view[0], Eq(2));

  auto stacked_view = gtl::projection_view(view, [](int x) { return x * 3; });
  EXPECT_THAT(stacked_view[2], Eq(18));
  EXPECT_THAT(stacked_view[1], Eq(12));
  EXPECT_THAT(stacked_view[0], Eq(6));
}

TEST_F(IteratorAdaptorTest, StackedProjectionViewAt) {
  std::vector<int> v = {1, 2, 3};
  auto view = gtl::projection_view(v, [](int x) { return x * 2; });
  EXPECT_THAT(view.at(1), Eq(4));
  EXPECT_THAT(view.at(2), Eq(6));
  EXPECT_THAT(view.at(0), Eq(2));

  auto stacked_view = gtl::projection_view(view, [](int x) { return x * 3; });
  EXPECT_THAT(stacked_view.at(2), Eq(18));
  EXPECT_THAT(stacked_view.at(1), Eq(12));
  EXPECT_THAT(stacked_view.at(0), Eq(6));
}

template <typename C>
inline constexpr bool kHasSubscript =
    gtl::Requires<C>([](auto&& view) -> decltype(view[0]) {});
template <typename C>
inline constexpr bool kHasAt =
    gtl::Requires<C>([](auto&& view) -> decltype(view.at(0)) {});

TEST_F(IteratorAdaptorTest, ProjectionViewSubscriptIsConditional) {
  ASSERT_TRUE(kHasSubscript<std::deque<int>>);
  using extracted_d =
      projection_view_t<std::deque<int>, internal::NoopExtractor>;
  EXPECT_TRUE(kHasSubscript<extracted_d>);
  EXPECT_TRUE(kHasSubscript<const extracted_d>);
  EXPECT_TRUE(kHasAt<extracted_d>);
  EXPECT_TRUE(kHasAt<const extracted_d>);

  ASSERT_FALSE(kHasSubscript<std::forward_list<int>>);
  using extracted_l =
      projection_view_t<std::forward_list<int>, internal::NoopExtractor>;
  EXPECT_FALSE(kHasSubscript<extracted_l>);
  EXPECT_FALSE(kHasSubscript<const extracted_l>);
  EXPECT_FALSE(kHasAt<extracted_l>);
  EXPECT_FALSE(kHasAt<const extracted_l>);
}

TEST_F(IteratorAdaptorTest, KeyViewConstexpr) {
  static constexpr auto kMap = gtl::fixed_flat_map_of<int, absl::string_view>(
      {{1, "a"}, {2, "b"}, {3, "c"}});
  static constexpr std::array kKeys = {1, 2, 3};
  static_assert(std::equal(key_view(kMap).begin(), key_view(kMap).end(),
                           kKeys.begin(), kKeys.end()));
}

TEST_F(IteratorAdaptorTest, ValueViewConstexpr) {
  static constexpr auto kMap = gtl::fixed_flat_map_of<int, absl::string_view>(
      {{1, "a"}, {2, "b"}, {3, "c"}});
  static constexpr std::array kValues = {"a", "b", "c"};
  static_assert(std::equal(value_view(kMap).begin(), value_view(kMap).end(),
                           kValues.begin(), kValues.end()));
}

TEST_F(IteratorAdaptorTest, DerefSecondViewConstexpr) {
  static constexpr auto kValues =
      std::to_array<absl::string_view>({"a", "b", "c"});
  static constexpr auto kMap =
      gtl::fixed_flat_map_of<int, const absl::string_view*>(
          {{1, &kValues[0]}, {2, &kValues[1]}, {3, &kValues[2]}});
  static_assert(std::equal(deref_second_view(kMap).begin(),
                           deref_second_view(kMap).end(), kValues.begin(),
                           kValues.end()));
}

TEST_F(IteratorAdaptorTest, DerefViewConstexpr) {
  static constexpr auto kKeys =
      std::to_array<absl::string_view>({"a", "b", "c"});
  static constexpr auto kSet = gtl::fixed_flat_set_of<const absl::string_view*>(
      {&kKeys[0], &kKeys[1], &kKeys[2]});
  static_assert(std::equal(deref_view(kSet).begin(), deref_view(kSet).end(),
                           kKeys.begin(), kKeys.end()));
}

TEST_F(IteratorAdaptorTest, MutableDerefViewConstexpr) {
  static constexpr auto kKeys = [] {
    std::array keys = {1, 2, 3};
    std::array key_ptrs = {&keys[0], &keys[1], &keys[2]};
    for (int& key : mutable_deref_view(key_ptrs)) {
      ++key;
    }
    return keys;
  }();

  static_assert(kKeys == std::array{2, 3, 4});
}

TEST_F(IteratorAdaptorTest, ReversedViewConstexpr) {
  static constexpr auto kValues =
      std::to_array<absl::string_view>({"a", "b", "c"});
  static_assert(std::equal(reversed_view(kValues).begin(),
                           reversed_view(kValues).end(), kValues.rbegin(),
                           kValues.rend()));
}

TEST_F(IteratorAdaptorTest, ProjectionViewConstexpr) {
  struct Googler {
    int64_t id;
    absl::string_view username;
  };
  static constexpr auto kGooglers = std::to_array<Googler>(
      {{.id = 1, .username = "sergey"}, {.id = 2, .username = "page"}});

  static constexpr std::array kUsernames = {"sergey", "page"};
  static_assert(
      std::equal(projection_view(kGooglers, &Googler::username).begin(),
                 projection_view(kGooglers, &Googler::username).end(),
                 kUsernames.begin(), kUsernames.end()));
}

}  // namespace
}  // namespace gtl
