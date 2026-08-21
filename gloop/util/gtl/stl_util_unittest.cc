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

#include "gloop/util/gtl/stl_util.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <forward_list>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/container/chunked_queue.h"
#include "absl/container/node_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "benchmark/benchmark.h"
#include "gloop/util/gtl/unique_array.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace gtl {
namespace {

using testing::ElementsAre;
using testing::ElementsAreArray;
using testing::IsEmpty;
using testing::SizeIs;

// Test STLClearObject() which deallocates memory of STL container
TEST(STLClearObjectTest, Basic) {
  {
    std::vector<int> a;
    // test to make sure empty object stays empty
    STLClearObject(&a);
    EXPECT_EQ(0, a.size());
    EXPECT_EQ(0, a.capacity());

    for (int i = 0; i < 10; ++i) a.push_back(i);
    STLClearObject(&a);
    EXPECT_EQ(0, a.size());
    EXPECT_EQ(0, a.capacity());
  }
  {
    std::string a("this tests string");
    STLClearObject(&a);
    EXPECT_EQ(0, a.size());
    EXPECT_GE(a.capacity(), a.size());
    // The C++ standard does not guarantee that the next test will pass.
    // But if we get a string implementation where it does not,
    // then we do not to release any programs using that string implementation.
    EXPECT_LE(a.capacity(), std::string().capacity());
  }
}

// Check TestSTLClearIfBig(), which clears a container and eliminates
// excessive capacity.
TEST(STLClearIfBigTest, Basic) {
  {
    // Create a 1000-element vector.
    std::vector<int> test_vector;
    for (int i = 0; i < 1000; ++i) {
      test_vector.push_back(i);
    }
    // Clearing with a high capacity tolerance should clear the
    // vector, but leave the capacity intact.
    int capacity_before_clear = test_vector.capacity();
    STLClearIfBig(&test_vector, 100000);
    EXPECT_EQ(0, test_vector.size());
    EXPECT_EQ(capacity_before_clear, test_vector.capacity());
  }

  {
    // Again, create a 1000-element vector.
    std::vector<int> test_vector;
    for (int i = 0; i < 1000; ++i) {
      test_vector.push_back(i);
    }
    // Clearing with a low capacity tolerance should not only clear
    // the vector, but also reduce the capacity.
    int capacity_before_clear = test_vector.capacity();
    STLClearIfBig(&test_vector, 10);
    EXPECT_EQ(0, test_vector.size());
    EXPECT_LE(test_vector.capacity(), capacity_before_clear - 1);
  }

  {
    // Test that a deque gets cleared
    std::deque<int> test_deque;
    for (int i = 0; i < 1000; ++i) {
      test_deque.push_back(i * i);
    }
    STLClearIfBig(&test_deque, 100);
    EXPECT_EQ(0, test_deque.size());
  }

  {
    // Test that clear() is called when the deque is smaller than size.
    std::deque<int> test_deque;
    for (int i = 0; i < 1000; ++i) {
      test_deque.push_back(i * i);
    }
    STLClearIfBig(&test_deque, 10001);
    EXPECT_EQ(0, test_deque.size());
  }

  {
    // Create a 1000-element unordered_map.
    std::unordered_map<int, int> test_map;
    for (int i = 0; i < 1000; ++i) {
      test_map.insert(std::make_pair(i, i * i));
    }
    // Clearing with a high capacity tolerance should clear the map,
    // but leave the bucket count unchanged.
    int buckets_before_clear = test_map.bucket_count();
    STLClearHashIfBig(&test_map, 100000);
    EXPECT_EQ(0, test_map.size());
    EXPECT_EQ(buckets_before_clear, test_map.bucket_count());
  }

  {
    // Create a 1000-element unordered_map again.
    std::unordered_map<int, int> test_map;
    for (int i = 0; i < 1000; ++i) {
      test_map.insert(std::make_pair(i, i * i));
    }
    // Clearing with a low capacity tolerance should not only clear
    // the map, but also reduce the bucket count.
    int buckets_before_clear = test_map.bucket_count();
    STLClearHashIfBig(&test_map, 10);
    EXPECT_EQ(0, test_map.size());
    EXPECT_LE(test_map.bucket_count(), buckets_before_clear - 1);
  }

  {
    // This is a minimal test for unordered_set, which is treated much like
    // a unordered_map.
    std::unordered_set<int> test_set;
    for (int i = 0; i < 1000; ++i) {
      test_set.insert(i * i);
    }
    int buckets_before_clear = test_set.bucket_count();
    STLClearHashIfBig(&test_set, 100);
    EXPECT_LE(test_set.bucket_count(), buckets_before_clear - 1);
  }
}

TEST(FastStringAssignmentTest, Basic) {
  std::string str;
  str.assign("foo", 3);
  EXPECT_THAT(str, ElementsAre('f', 'o', 'o'));

  str.assign("X", 1);
  EXPECT_THAT(str, ElementsAre('X'));

  static const int kLargeSize = 100000;
  auto buffer = gtl::MakeUniqueArrayForOverwrite<char>(kLargeSize);
  for (int i = 0; i < kLargeSize; i++) {
    buffer[i] = i & 0xff;
  }
  str.assign(buffer.data(), kLargeSize);
  EXPECT_THAT(str, ElementsAreArray(buffer.data(), buffer.data() + kLargeSize));

  str.assign("X", 1);
  EXPECT_THAT(str, ElementsAre('X'));

  str.assign("Y", 0);
  EXPECT_THAT(str, IsEmpty());

  str = "Y";
  str.assign(nullptr, 0);
  EXPECT_THAT(str, IsEmpty());
}

template <typename T>
struct CustomAllocator : std::allocator<T> {
  ~CustomAllocator() { EXPECT_EQ(sentinel, 123); }
  template <class U>
  struct rebind {
    using other = CustomAllocator<U>;
  };
  int sentinel = 123;
};

template <typename T>
class StringResizeTest : public ::testing::Test {};
using StringResizeTestTypes = ::testing::Types<
    std::string,
    std::basic_string<char, std::char_traits<char>, CustomAllocator<char>>,
    std::basic_string<wchar_t>>;
TYPED_TEST_SUITE(StringResizeTest, StringResizeTestTypes);

TYPED_TEST(StringResizeTest, Basic) {
  absl::AnyInvocable<void(TypeParam*, size_t)> resizers[] = {
      [](TypeParam* s, size_t size) { STLStringResizeUninitialized(s, size); },
      [](TypeParam* s, size_t size) {
        STLStringResizeUninitializedAmortized(s, size);
      },
      [](TypeParam* s, size_t size) { STLStringResizeAmortized(s, size); },
  };
  for (size_t resize_i = 0; resize_i < std::size(resizers); ++resize_i) {
    SCOPED_TRACE(resize_i);
    TypeParam str;
    // Test with increasing lengths
    for (int i = 0; i <= 100; i++) {
      SCOPED_TRACE(i);
      size_t old_len = str.size();
      resizers[resize_i](&str, i);
      EXPECT_THAT(str, SizeIs(i));
      for (size_t j = 0; j <= str.size(); j++) {
        if (j < old_len) {
          EXPECT_EQ(str[j], 'a');
        } else if (j < str.size()) {
          str[j] = 'a';
        } else {
          EXPECT_EQ(str[j], 0);
        }
      }
    }
    // Test with decreasing lengths
    for (int i = 100; i >= 0; i -= 10) {
      SCOPED_TRACE(i);
      resizers[resize_i](&str, i);
      EXPECT_THAT(str, SizeIs(i));
      for (size_t j = 0; j <= str.size(); j++) {
        EXPECT_EQ(str[j], j == str.size() ? 0 : 'a');
      }
    }
    EXPECT_THAT(str, SizeIs(0));
    EXPECT_EQ(str[0], 0);
    EXPECT_THAT(str, IsEmpty());
  }
}

TYPED_TEST(StringResizeTest, Amortized) {
  absl::AnyInvocable<void(TypeParam*, size_t)> resizers[] = {
      [](TypeParam* s, size_t size) {
        STLStringResizeUninitializedAmortized(s, size);
      },
      [](TypeParam* s, size_t size) { STLStringResizeAmortized(s, size); },
  };
  for (size_t resize_i = 0; resize_i < std::size(resizers); ++resize_i) {
    SCOPED_TRACE(resize_i);
    TypeParam str;
    size_t prev_cap = str.capacity();
    int cap_increase_count = 0;
    for (int i = 0; i < 1000; ++i) {
      resizers[resize_i](&str, i);
      size_t new_cap = str.capacity();
      if (new_cap > prev_cap) ++cap_increase_count;
      prev_cap = new_cap;
    }
    EXPECT_LT(cap_increase_count, 50);
  }
}

TEST(StringAppendTest, Basic) {
  std::string str;
  std::string expected;
  for (int i = 0; i < 100; i++) {
    SCOPED_TRACE(i);
    int N = i;
    std::string tmp;
    for (int j = 0; j < N; j++) {
      tmp += static_cast<char>(i);
    }
    expected += tmp;
    str.append(tmp.data(), tmp.size());
    EXPECT_EQ(expected, str);
  }
}

TEST(StringAsArrayTest, Empty) {
  std::string empty;
  EXPECT_EQ(static_cast<char*>(nullptr), string_as_array(&empty));
}

TEST(StringAsArrayTest, NullTerminated) {
  // Returning a null-terminated string is required by C++11.
  std::string str("abcde");
  str.resize(3);
  EXPECT_STREQ("abc", string_as_array(&str));
}

TEST(StringAsArrayTest, WriteCopy) {
  // With a COW implementation and C++98, this would have failed if
  // string_as_array(&str) were implemented as
  // const_cast<char*>(str->data()).  As of C++11, COW is no longer allowed.
  std::string s1("abc");
  const std::string s2(s1);
  string_as_array(&s1)[1] = 'x';
  EXPECT_EQ("axc", s1);
  EXPECT_EQ("abc", s2);
}

TEST(HashSetEqualityTest, Basic) {
  std::unordered_set<int> a, b;
  int testdata[] = {1, 87432432, 495839, 2398394, 39832, 2983, -1298354};

  for (int i = 0; i < std::size(testdata); ++i) {
    SCOPED_TRACE(i);
    a.insert(testdata[i]);
    b.insert(testdata[(i + 1) % std::size(testdata)]);

    EXPECT_EQ(i == std::size(testdata) - 1, HashSetEquality(a, b));
    EXPECT_EQ(HashSetEquality(a, b), HashSetEquality(b, a));
  }

  for (int i = 1; i < 10000; ++i) a.insert(87432432 + i);
  for (int i = 1; i < 10000; ++i) a.erase(87432432 + i);

  EXPECT_TRUE(HashSetEquality(a, b));
  EXPECT_TRUE(HashSetEquality(b, a));
}

template <typename Type>
static void TestHashMapEqualityNonFunctor() {
  Type a, b;
  std::pair<int, int> testdata[] = {
      std::make_pair(1, 1),       std::make_pair(87432432, 2),
      std::make_pair(495839, 3),  std::make_pair(2398394, 4),
      std::make_pair(39832, 5),   std::make_pair(2983, 6),
      std::make_pair(-1298354, 7)};

  for (int i = 0; i < std::size(testdata); ++i) {
    SCOPED_TRACE(i);
    a.insert(testdata[i]);
    b.insert(testdata[(i + 1) % std::size(testdata)]);
    EXPECT_EQ((i == std::size(testdata) - 1), HashMapEquality(a, b));
    EXPECT_EQ(HashMapEquality(a, b), HashMapEquality(b, a));
  }

  for (int i = 1; i < 10000; ++i) a.insert(std::make_pair(87432432 + i, i));
  for (int i = 1; i < 10000; ++i) a.erase(87432432 + i);

  ASSERT_TRUE(HashMapEquality(a, b));
  ASSERT_TRUE(HashMapEquality(b, a));
}

TEST(TestHashMapEquality, NonFunctor_HashMap) {
  TestHashMapEqualityNonFunctor<std::unordered_map<int, int>>();
}

TEST(TestHashMapEquality, NonFunctor_Map) {
  TestHashMapEqualityNonFunctor<std::map<int, int>>();
}

namespace unordered_map_equality_tests {
class TestHelper {
 public:
  explicit TestHelper(int val) : value_(val) {}
  int value() const { return value_; }

 private:
  int value_;
};

struct EqualTestHelper {
  bool operator()(const TestHelper& lhs, const TestHelper& rhs) const {
    return lhs.value() == rhs.value();
  }
};

template <typename Type>
static void TestHashMapEqualityWithFunctor() {
  // Test with a functor.
  Type a, b;
  // Just a simple test ensuring functors work.
  using ValueType = typename Type::value_type;
  a.insert(ValueType(1, TestHelper(2)));
  a.insert(ValueType(2, TestHelper(4)));
  a.insert(ValueType(4, TestHelper(16)));
  b.insert(ValueType(4, TestHelper(16)));
  b.insert(ValueType(2, TestHelper(4)));
  b.insert(ValueType(1, TestHelper(2)));

  EXPECT_TRUE(HashMapEquality(a, b, EqualTestHelper()));
  EXPECT_TRUE(HashMapEquality(b, a, EqualTestHelper()));

  // Inserting different values.
  a.insert(ValueType(5, TestHelper(25)));
  b.insert(ValueType(6, TestHelper(36)));
  EXPECT_FALSE(HashMapEquality(a, b, EqualTestHelper()));
  EXPECT_FALSE(HashMapEquality(b, a, EqualTestHelper()));
}

TEST(TestHashMapEquality, WithFunctor_HashMap) {
  TestHashMapEqualityWithFunctor<absl::node_hash_map<int, TestHelper>>();
}

TEST(TestHashMapEquality, WithFunctor_Map) {
  TestHashMapEqualityWithFunctor<std::map<int, TestHelper>>();
}
}  // namespace unordered_map_equality_tests

// This templatized helper can subclass any class and count how many times
// instances have been constructed or destructed.  It is used to make sure
// that deletes are actually occuring.

template <typename T>
class InstanceCounter : public T {
 public:
  InstanceCounter<T>() : T() { ++instance_count; }
  ~InstanceCounter<T>() { --instance_count; }
  static int instance_count;
};
template <typename T>
int InstanceCounter<T>::instance_count = 0;

TEST(STLDeleteElementsTest, STLDeleteElements) {
  std::vector<InstanceCounter<std::string>*> v;
  v.push_back(new InstanceCounter<std::string>());
  v.push_back(new InstanceCounter<std::string>());
  v.push_back(new InstanceCounter<std::string>());
  EXPECT_EQ(3, InstanceCounter<std::string>::instance_count);
  STLDeleteElements(&v);
  EXPECT_EQ(0, InstanceCounter<std::string>::instance_count);
  EXPECT_EQ(0, v.size());

  // Deleting nullptrs to containers is ok.
  std::vector<InstanceCounter<std::string>*>* p = nullptr;
  STLDeleteElements(p);

  // Passing a container of unique_ptr just calls clear.
  std::vector<std::unique_ptr<int>> v2;
  v2.push_back(std::make_unique<int>(1));
  v2.push_back(std::make_unique<int>(2));
  v2.push_back(std::make_unique<int>(3));
  EXPECT_EQ(3, v2.size());
  v2.clear();
  EXPECT_EQ(0, v2.size());
}

TEST(STLDeleteElementsTest, STLElementDeleter) {
  std::vector<InstanceCounter<std::string>*> v;
  {  // Create a new scope
    STLElementDeleter<std::vector<InstanceCounter<std::string>*>> d(&v);
    v.push_back(new InstanceCounter<std::string>());
    v.push_back(new InstanceCounter<std::string>());
    v.push_back(new InstanceCounter<std::string>());
    EXPECT_EQ(3, InstanceCounter<std::string>::instance_count);
  }
  EXPECT_EQ(0, InstanceCounter<std::string>::instance_count);
  EXPECT_EQ(0, v.size());
}

TEST(STLDeleteElementsTest, ElementDeleter) {
  std::vector<InstanceCounter<std::string>*> v;
  {  // Create a new scope
    ElementDeleter d(&v);
    v.push_back(new InstanceCounter<std::string>());
    v.push_back(new InstanceCounter<std::string>());
    v.push_back(new InstanceCounter<std::string>());
    EXPECT_EQ(3, InstanceCounter<std::string>::instance_count);
  }
  EXPECT_EQ(0, InstanceCounter<std::string>::instance_count);
  EXPECT_EQ(0, v.size());
}

TEST(STLDeleteValuesTest, STLValueDeleter) {
  std::map<int, InstanceCounter<std::string>*> m;
  {  // Create a new scope
    STLValueDeleter<std::map<int, InstanceCounter<std::string>*>> d(&m);
    m[0] = new InstanceCounter<std::string>();
    m[1] = new InstanceCounter<std::string>();
    m[2] = new InstanceCounter<std::string>();
    EXPECT_EQ(3, InstanceCounter<std::string>::instance_count);
  }
  EXPECT_EQ(0, InstanceCounter<std::string>::instance_count);
  EXPECT_EQ(0, m.size());

  std::map<int, std::unique_ptr<int>> m2;
  m2[0] = std::make_unique<int>(1);
  m2[1] = std::make_unique<int>(2);
  m2[2] = std::make_unique<int>(3);
  EXPECT_EQ(3, m2.size());
  m2.clear();
  EXPECT_EQ(0, m2.size());
}

TEST(STLDeleteValuesTest, ValueDeleter) {
  std::map<int, InstanceCounter<std::string>*> m;
  {  // Create a new scope
    ValueDeleter d(&m);
    m[0] = new InstanceCounter<std::string>();
    m[1] = new InstanceCounter<std::string>();
    m[2] = new InstanceCounter<std::string>();
    EXPECT_EQ(3, InstanceCounter<std::string>::instance_count);
  }
  EXPECT_EQ(0, InstanceCounter<std::string>::instance_count);
  EXPECT_EQ(0, m.size());
}

// Tests the case where deleting the value also invalidates the key.
TEST(STLDeleteValuesTest, ValueDeleterInvalidatingKeys) {
  // We use a map where the keys point into the value objects.
  std::unordered_map<const void*, InstanceCounter<std::string>*> m;
  {  // Create a new scope
    ValueDeleter d(&m);
    InstanceCounter<std::string>* s1 = new InstanceCounter<std::string>();
    s1->assign("foo");
    m[s1->c_str()] = s1;
    InstanceCounter<std::string>* s2 = new InstanceCounter<std::string>();
    s2->assign("bar");
    m[s2->c_str()] = s2;
    InstanceCounter<std::string>* s3 = new InstanceCounter<std::string>();
    s3->assign("baz");
    m[s3->c_str()] = s3;
    EXPECT_EQ(3, InstanceCounter<std::string>::instance_count);
  }
  EXPECT_EQ(0, InstanceCounter<std::string>::instance_count);
  EXPECT_EQ(0, m.size());
}

TEST(STLSetDifference, SimpleSet) {
  std::set<int> a, b, c;
  a.insert(1);
  a.insert(3);
  b = a;
  a.insert(2);
  STLSetDifference(a, b, &c);
  ASSERT_EQ(1, c.size());
  ASSERT_TRUE(c.count(2));
  std::set<int> d = STLSetDifference(a, b);
  ASSERT_TRUE(c == d);
}

// We only test that vectors work for differencing since
// it's all identical for unions and intersections.
TEST(STLSetDifference, SimpleVector) {
  std::vector<int> a, b, c;
  a.push_back(1);
  a.push_back(2);
  a.push_back(3);
  b.push_back(1);
  b.push_back(3);
  STLSetDifference(a, b, &c);
  ASSERT_EQ(1, c.size());
  ASSERT_EQ(c[0], 2);
  std::vector<int> d = STLSetDifference(a, b);
  ASSERT_TRUE(c == d);
}

TEST(STLSetDifference, MultipleTypes) {
  // Same as above, but a is a set and b is a vector instead of set.
  std::set<int> a, c;
  std::vector<int> b;
  a.insert(1);
  a.insert(2);
  a.insert(3);
  b.push_back(1);
  b.push_back(3);
  STLSetDifference(a, b, &c);
  ASSERT_EQ(1, c.size());
  ASSERT_TRUE(c.count(2));
}

TEST(STLSetDifference, CustomCompare) {
  // Same as above, but a is a set and b is a vector instead of set.
  const int a_arr[] = {3, 2, 1};
  const int b_arr[] = {3, 1};
  std::set<int, std::greater<int>> a(a_arr, a_arr + std::size(a_arr));
  std::vector<int> b(b_arr, b_arr + std::size(b_arr));
  std::vector<int> c;
  STLSetDifference(a, b, &c, a.key_comp());
  EXPECT_THAT(c, ElementsAre(2));
}

static bool Greater(int a, int b) { return b < a; }

TEST(STLSetDifference, CustomCompareWithFuncPtr) {
  // Same as above, but a is a set and b is a vector instead of set.
  const int a_arr[] = {1, 2, 3};
  const int b_arr[] = {1, 3};
  std::vector<int> a(a_arr, a_arr + std::size(a_arr));
  std::vector<int> b(b_arr, b_arr + std::size(b_arr));
  std::vector<int> c;
  std::sort(a.begin(), a.end(), Greater);
  std::sort(b.begin(), b.end(), Greater);
  STLSetDifference(a, b, &c, Greater);
  EXPECT_THAT(c, ElementsAre(2));
  EXPECT_THAT(STLSetDifference(a, b, Greater), ElementsAre(2));
  EXPECT_THAT(STLSetDifferenceAs<std::deque<int>>(a, b, Greater),
              ElementsAre(2));
  EXPECT_THAT(STLSetDifferenceAs<std::vector<int>>(a, b, Greater),
              ElementsAre(2));
}

template <typename R, typename F, typename A1, typename A2>
static R TakesFunctor(F f, const A1& a1, const A2& a2) {
  return f(a1, a2);
}

TEST(STLSetDifference, UsableAsFunctors) {
  std::vector<int> vec_a;
  std::vector<int> vec_b;
  std::vector<int> vec_c;
  std::deque<int> deque_b;
  std::deque<int> deque_c;

  // STLSetDifference: 1-arg form must be usable as functor.
  vec_c = TakesFunctor<std::vector<int>>(&STLSetDifference<std::vector<int>>,
                                         vec_a, vec_b);

  // STLSetDifference: 2-arg form must be usable as functor.
  vec_c = TakesFunctor<std::vector<int>>(
      &STLSetDifference<std::vector<int>, std::deque<int>>, vec_a, deque_b);

  // STLSetDifferenceAs: 3-arg form usable as functor.
  deque_c = TakesFunctor<std::deque<int>>(
      &STLSetDifferenceAs<std::deque<int>, std::vector<int>, std::deque<int>>,
      vec_a, deque_b);
}

TEST(STLSetDifferenceDeathTest, BadArgs) {
  // Make sure that in debug mode we crash (assert only runs in debug mode).
#if NDEBUG
#else
  std::set<int> a, b;
  ASSERT_DEATH(STLSetDifference(a, b, &a), "");
#endif
}

TEST(STLSetSymmetricDifference, SimpleSet) {
  std::set<int> a, b, c;
  a.insert(1);
  a.insert(2);
  a.insert(3);
  b.insert(3);
  b.insert(4);
  b.insert(5);
  STLSetSymmetricDifference(a, b, &c);
  ASSERT_EQ(4, c.size());
  ASSERT_TRUE(c.count(1));
  ASSERT_TRUE(c.count(2));
  ASSERT_TRUE(c.count(4));
  ASSERT_TRUE(c.count(5));
  std::set<int> d = STLSetSymmetricDifference(a, b);
  ASSERT_TRUE(c == d);
}

TEST(STLSetSymmetricDifference, SimpleVector) {
  std::vector<int> a, b, c;
  a.push_back(1);
  a.push_back(2);
  a.push_back(3);
  b.push_back(1);
  b.push_back(3);
  b.push_back(4);
  STLSetSymmetricDifference(a, b, &c);
  ASSERT_EQ(2, c.size());
  ASSERT_EQ(c[0], 2);
  ASSERT_EQ(c[1], 4);
  std::vector<int> d = STLSetSymmetricDifference(a, b);
  ASSERT_TRUE(c == d);
}

TEST(STLSetSymmetricDifference, MultipleTypes) {
  // Same as above, but a is a set and b is a vector instead of set.
  std::set<int> a, c;
  std::vector<int> b;
  a.insert(1);
  a.insert(2);
  a.insert(3);
  b.push_back(1);
  b.push_back(3);
  b.push_back(4);
  STLSetSymmetricDifference(a, b, &c);
  ASSERT_EQ(2, c.size());
  ASSERT_TRUE(c.count(2));
  ASSERT_TRUE(c.count(4));
}

TEST(STLSetSymmetricDifferenceDeathTest, BadArgs) {
  // Make sure that in debug mode we crash (assert only runs in debug mode).
#if NDEBUG
#else
  std::set<int> a, b;
  ASSERT_DEATH(STLSetSymmetricDifference(a, b, &a), "");
#endif
}

TEST(STLSetUnion, Simple) {
  std::set<int> a, b, c;
  a.insert(1);
  b.insert(2);
  STLSetUnion(a, b, &c);
  ASSERT_EQ(2, c.size());
  ASSERT_TRUE(c.count(1));
  ASSERT_TRUE(c.count(2));
  std::set<int> d = STLSetUnion(a, b);
  ASSERT_TRUE(c == d);
}

TEST(STLSetIntersection, Simple) {
  std::set<int> a, b, c;
  a.insert(1);
  a.insert(2);
  b.insert(2);
  b.insert(3);
  c = STLSetIntersection(a, b);
  ASSERT_EQ(1, c.size());
  ASSERT_TRUE(c.count(2));
  std::set<int> d = STLSetIntersection(a, b);
  ASSERT_TRUE(c == d);
}

TEST(STLIncludes, SimpleSet) {
  std::set<int> a, b, empty;
  a.insert(1);
  a.insert(2);
  a.insert(3);

  b.insert(1);
  b.insert(2);
  ASSERT_TRUE(STLIncludes(a, b));
  ASSERT_FALSE(STLIncludes(b, a));
  ASSERT_TRUE(STLIncludes(a, a));

  ASSERT_TRUE(STLIncludes(a, empty));
  ASSERT_FALSE(STLIncludes(empty, a));
}

TEST(STLIncludes, SimpleVector) {
  std::vector<int> a, b, empty;
  a.push_back(1);
  a.push_back(2);
  a.push_back(3);

  b.push_back(1);
  b.push_back(2);
  ASSERT_TRUE(STLIncludes(a, b));
  ASSERT_FALSE(STLIncludes(b, a));
  ASSERT_TRUE(STLIncludes(a, a));

  ASSERT_TRUE(STLIncludes(a, empty));
  ASSERT_FALSE(STLIncludes(empty, a));
}

TEST(STLIncludes, MultipleTypes) {
  std::set<int> a;
  a.insert(1);
  a.insert(2);
  a.insert(3);

  std::vector<int> b;
  b.push_back(1);
  b.push_back(2);

  std::set<int> c;
  c.insert(1);

  ASSERT_TRUE(STLIncludes(a, b));
  ASSERT_FALSE(STLIncludes(b, a));
  ASSERT_TRUE(STLIncludes(a, a));
  ASSERT_TRUE(STLIncludes(b, c));
}

TEST(STLIncludesDeathTest, BadArgs) {
  // Make sure that in debug mode we crash (assert only runs in debug mode).
#if NDEBUG
#else
  std::vector<int> a, b;
  a.push_back(1);
  a.push_back(0);  // not sorted
  ASSERT_DEATH(STLIncludes(a, b), "");
  ASSERT_DEATH(STLIncludes(b, a), "");
#endif
}

TEST(STLCountingAllocator, CountingTest) {
  int64_t count = 0;
  STLCountingAllocator<int> a(&count);

  STLCountingAllocator<int>::pointer p = a.allocate(1);
  EXPECT_EQ(sizeof(int), count);
  a.deallocate(p, 1);
  EXPECT_EQ(0, count);
}

TEST(STLAtomicCountingAllocator, CountingTest) {
  std::atomic<int64_t> count = 0;
  STLAtomicCountingAllocator<int> a(&count);

  STLCountingAllocator<int>::pointer p = a.allocate(1);
  EXPECT_EQ(sizeof(int), count);
  a.deallocate(p, 1);
  EXPECT_EQ(0, count);
}

// Test that the allocator calls the relaxed fetch_add() method when it is
// available in the counter type.
TEST(STLAtomicCountingAllocator, CallsRelaxedAdd) {
  // Create a mock object with fetch_add() method.
  struct MockAtomic {
    MOCK_METHOD(void, fetch_add, (int64_t, std::memory_order), (const));
  };
  // Use the mock to test the allocator.
  MockAtomic mock;
  EXPECT_CALL(mock, fetch_add(sizeof(int), std::memory_order_relaxed)).Times(1);
  STLCountingAllocator<int, std::allocator<int>, MockAtomic> a(&mock);
  a.allocate(1);
}

TEST(STLCountingAllocator, Void) {
  using VoidA = STLCountingAllocator<void>;
  using IntA = VoidA::rebind<int>::other;
  int64_t n = 0;
  IntA alloc(&n);
  std::vector<int, IntA> v(alloc);
  v.push_back(123);
  EXPECT_GT(n, 0);
}

TEST(STLCountingAllocator, StatefulRebind) {
  using VoidA = STLCountingAllocator<void>;
  using IntA = VoidA::rebind<int>::other;
  int64_t n = 0;
  VoidA void_alloc(&n);
  IntA int_alloc(void_alloc);
  {
    std::vector<int, IntA> v(int_alloc);
    v.push_back(123);
    EXPECT_GT(n, 0);
  }
  EXPECT_EQ(0, n);
  {
    std::vector<int, IntA> v(void_alloc);
    v.push_back(123);
    EXPECT_GT(n, 0);
  }
  EXPECT_EQ(0, n);
}

TEST(STLAtomicCountingAllocator, StatefulRebind) {
  using VoidA = STLAtomicCountingAllocator<void>;
  using IntA = VoidA::rebind<int>::other;
  std::atomic<int64_t> n = 0;
  VoidA void_alloc(&n);
  IntA int_alloc(void_alloc);
  {
    std::vector<int, IntA> v(int_alloc);
    v.push_back(123);
    EXPECT_GT(n, 0);
  }
  EXPECT_EQ(0, n);
  {
    std::vector<int, IntA> v(void_alloc);
    v.push_back(123);
    EXPECT_GT(n, 0);
  }
  EXPECT_EQ(0, n);
}

template <typename T>
struct CustomAlloc : std::allocator<T> {
  CustomAlloc() = default;
  template <typename U>
  CustomAlloc(const CustomAlloc<U>&) {}
  template <typename U>
  struct rebind {
    using other = CustomAlloc<U>;
  };
};
template <typename T>
bool operator==(const CustomAlloc<T>&, const CustomAlloc<T>&) {
  return true;
}
template <typename T>
bool operator!=(const CustomAlloc<T>&, const CustomAlloc<T>&) {
  return false;
}

TEST(STLCountingAllocator, WrappingCustomAlloc) {
  using VoidA = STLCountingAllocator<void, CustomAlloc<void>>;
  using IntA = VoidA::rebind<int>::other;
  int64_t n = 0;
  VoidA void_alloc(&n);
  IntA int_alloc(void_alloc);
  {
    std::vector<int, IntA> v(int_alloc);
    v.push_back(123);
    EXPECT_GT(n, 0);
  }
  EXPECT_EQ(0, n);
  {
    std::vector<int, IntA> v(void_alloc);
    v.push_back(123);
    EXPECT_GT(n, 0);
  }
  EXPECT_EQ(0, n);
}

TEST(STLCountingAllocator, RebindComparisons) {
  using VoidA = STLCountingAllocator<void>;
  using IntA = VoidA::rebind<int>::other;
  int64_t n = 0;
  VoidA void_alloc(&n);
  IntA int_alloc = void_alloc;
  EXPECT_TRUE(void_alloc == int_alloc);
  EXPECT_TRUE(int_alloc == void_alloc);
  EXPECT_FALSE(void_alloc != int_alloc);
  EXPECT_FALSE(int_alloc != void_alloc);
  IntA int_alloc2;
  int_alloc2 = int_alloc;   // assign from same
  int_alloc2 = void_alloc;  // assign from rebind
}

struct TestingAllocCountInt {
  explicit TestingAllocCountInt(int i) : my_int_(i) { ++refcount_; }
  ~TestingAllocCountInt() { --refcount_; }
  int my_int_;
  static int refcount_;
};

int TestingAllocCountInt::refcount_ = 0;

TEST(DeleteContainerPointersTest, Basic) {
  std::vector<TestingAllocCountInt*> v;
  v.push_back(new TestingAllocCountInt(1));
  v.push_back(nullptr);
  v.push_back(new TestingAllocCountInt(3));
  EXPECT_EQ(2, TestingAllocCountInt::refcount_);
  STLDeleteContainerPointers(v.begin(), v.end());
  EXPECT_EQ(0, TestingAllocCountInt::refcount_);

  std::unordered_map<TestingAllocCountInt*, TestingAllocCountInt*> pair_map;
  pair_map.insert(
      std::make_pair(new TestingAllocCountInt(1), new TestingAllocCountInt(2)));
  pair_map.insert(
      std::make_pair(new TestingAllocCountInt(3), new TestingAllocCountInt(4)));
  pair_map.insert(
      std::make_pair(new TestingAllocCountInt(5), new TestingAllocCountInt(6)));
  EXPECT_EQ(6, TestingAllocCountInt::refcount_);
  STLDeleteContainerPairPointers(pair_map.begin(), pair_map.end());
  EXPECT_EQ(0, TestingAllocCountInt::refcount_);

  std::unordered_map<int, TestingAllocCountInt*> m;
  m.insert(std::make_pair(1, new TestingAllocCountInt(1)));
  m.insert(std::make_pair(2, new TestingAllocCountInt(2)));
  EXPECT_EQ(2, TestingAllocCountInt::refcount_);
  STLDeleteContainerPairSecondPointers(m.begin(), m.end());
  EXPECT_EQ(0, TestingAllocCountInt::refcount_);
}

TEST(STLSortAndRemoveDuplicates, WorksOnVectors) {
  const int ia[] = {2, 4, 1, 3, 2, 1, 3, 0};
  std::vector<int> v(ia, ia + std::size(ia));
  STLSortAndRemoveDuplicates(&v);
  EXPECT_THAT(v, ElementsAre(0, 1, 2, 3, 4));
}

TEST(STLSortAndRemoveDuplicates, WorksOnDeque) {
  const int ia[] = {2, 3, 1, 4, 2, 1, 3, 0};
  std::deque<int> d(ia, ia + std::size(ia));
  STLSortAndRemoveDuplicates(&d);
  EXPECT_THAT(d, ElementsAre(0, 1, 2, 3, 4));
}

TEST(STLSortAndRemoveDuplicates, CustomComparator) {
  const int ia[] = {2, 4, 1, 3, 2, 1, 3, 0};
  std::vector<int> v(ia, ia + std::size(ia));
  STLSortAndRemoveDuplicates(&v, std::greater<int>());
  EXPECT_THAT(v, ElementsAre(4, 3, 2, 1, 0));
}

// Type to be ordered by value only; the stability_index is provided to check
// that the first equivalent object is retained.
struct ValueAndStabilityIndex {
  int value;
  int stability_index;
};

bool operator==(const ValueAndStabilityIndex& lhs,
                const ValueAndStabilityIndex& rhs) {
  return lhs.value == rhs.value;
}

bool operator<(const ValueAndStabilityIndex& lhs,
               const ValueAndStabilityIndex& rhs) {
  return lhs.value < rhs.value;
}

TEST(STLStableSortAndRemoveDuplicates, WorksOnVectors) {
  using T = ValueAndStabilityIndex;
  std::vector<T> v = {{0, 0}, {1, 0}, {0, 1}, {3, 0}, {4, 0}, {3, 1}};
  STLStableSortAndRemoveDuplicates(&v);
  EXPECT_THAT(v, ElementsAre(T{0, 0}, T{1, 0}, T{3, 0}, T{4, 0}));
}

TEST(STLStableSortAndRemoveDuplicates, WorksOnDeque) {
  using T = ValueAndStabilityIndex;
  std::deque<T> d = {{0, 0}, {0, 1}, {1, 0}, {1, 1}, {1, 2}, {3, 0}};
  STLStableSortAndRemoveDuplicates(&d);
  EXPECT_THAT(d, ElementsAre(T{0, 0}, T{1, 0}, T{3, 0}));
}

TEST(STLStableSortAndRemoveDuplicates, CustomComparator) {
  using T = ValueAndStabilityIndex;
  std::vector<T> v = {{0, 0}, {1, 0}, {0, 1}, {3, 0}, {4, 0}, {3, 1}};
  STLStableSortAndRemoveDuplicates(&v, std::less<T>());
  EXPECT_THAT(v, ElementsAre(T{0, 0}, T{1, 0}, T{3, 0}, T{4, 0}));
}

TEST(STLEraseAllFromSequence, Vector) {
  std::vector<int> container{3, 1, 2, 1, 3, 2, 4};
  STLEraseAllFromSequence(&container, 1);
  EXPECT_THAT(container, ElementsAre(3, 2, 3, 2, 4));
}

TEST(STLEraseAllFromSequence, List) {
  std::list<int> container{3, 1, 2, 1, 3, 2, 4};
  STLEraseAllFromSequence(&container, 1);
  EXPECT_THAT(container, ElementsAre(3, 2, 3, 2, 4));
}

TEST(STLEraseAllFromSequenceIf, VectorWithLambda) {
  std::vector<int> container{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  STLEraseAllFromSequenceIf(&container, [](int element) {
    return element % 3 == 0 || element % 5 == 0;
  });
  EXPECT_THAT(container, ElementsAre(1, 2, 4, 7, 8));
}

TEST(STLEraseAllFromSequenceIf, DequeWithFunctor) {
  std::deque<int> container{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  struct Predicate {
    bool operator()(int element) const {
      return element % 3 == 0 || element % 5 == 0;
    }
  };
  STLEraseAllFromSequenceIf(&container, Predicate());
  EXPECT_THAT(container, ElementsAre(1, 2, 4, 7, 8));
}

namespace {
bool IsMultipleOfTreeOrFive(int element) {
  return element % 3 == 0 || element % 5 == 0;
}
}  // namespace

TEST(STLEraseAllFromSequenceIf, ListWithFunction) {
  std::list<int> container{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  STLEraseAllFromSequenceIf(&container, IsMultipleOfTreeOrFive);
  EXPECT_THAT(container, ElementsAre(1, 2, 4, 7, 8));
}

TEST(STLEraseAllFromSequence, ForwardList) {
  std::forward_list<int> container{3, 1, 2, 1, 3, 2, 4};
  STLEraseAllFromSequence(&container, 1);
  EXPECT_THAT(container, ElementsAre(3, 2, 3, 2, 4));
}

TEST(STLEraseAllFromSequenceIf, ForwardList) {
  std::forward_list<int> container{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  STLEraseAllFromSequenceIf(&container, [](int element) {
    return element % 3 == 0 || element % 5 == 0;
  });
  EXPECT_THAT(container, ElementsAre(1, 2, 4, 7, 8));
}

// Check the simplest case: begin() and end() of two containers of the same
// type.
TEST(SortedRangesHaveIntersection, WorksOnVectorSameType) {
  const int ia1[] = {1, 5, 8, 9};
  const int ia2[] = {3, 6, 10};
  std::vector<int> v1(ia1, ia1 + std::size(ia1));
  std::vector<int> v2(ia2, ia2 + std::size(ia2));

  EXPECT_FALSE(
      SortedRangesHaveIntersection(v1.begin(), v1.end(), v2.begin(), v2.end()));
  EXPECT_FALSE(
      SortedRangesHaveIntersection(v2.begin(), v2.end(), v1.begin(), v1.end()));
  EXPECT_FALSE(SortedRangesHaveIntersection(v1.rbegin(), v1.rend(), v2.rbegin(),
                                            v2.rend(), std::greater<int>()));
  EXPECT_FALSE(SortedRangesHaveIntersection(v2.rbegin(), v2.rend(), v1.rbegin(),
                                            v1.rend(), std::greater<int>()));

  v1.push_back(12);
  v1.push_back(15);
  v1.push_back(18);
  v2.push_back(12);

  EXPECT_TRUE(
      SortedRangesHaveIntersection(v1.begin(), v1.end(), v2.begin(), v2.end()));
  EXPECT_TRUE(
      SortedRangesHaveIntersection(v2.begin(), v2.end(), v1.begin(), v1.end()));
  EXPECT_TRUE(SortedRangesHaveIntersection(v1.rbegin(), v1.rend(), v2.rbegin(),
                                           v2.rend(), std::greater<int>()));
  EXPECT_TRUE(SortedRangesHaveIntersection(v2.rbegin(), v2.rend(), v1.rbegin(),
                                           v1.rend(), std::greater<int>()));
}

// Two containers with value_types which are different but comparable.
TEST(SortedRangesHaveIntersection, WorksOnVectorDifferentType) {
  const int ia1[] = {1, 5, 8, 9};
  const char ca2[] = {3, 6, 10};
  std::vector<int> v1(ia1, ia1 + std::size(ia1));
  std::vector<char> v2(ca2, ca2 + std::size(ca2));

  EXPECT_FALSE(
      SortedRangesHaveIntersection(v1.begin(), v1.end(), v2.begin(), v2.end()));
  EXPECT_FALSE(
      SortedRangesHaveIntersection(v2.begin(), v2.end(), v1.begin(), v1.end()));
  EXPECT_FALSE(SortedRangesHaveIntersection(v1.rbegin(), v1.rend(), v2.rbegin(),
                                            v2.rend(), std::greater<int>()));
  EXPECT_FALSE(SortedRangesHaveIntersection(v2.rbegin(), v2.rend(), v1.rbegin(),
                                            v1.rend(), std::greater<int>()));

  v1.push_back(12);
  v1.push_back(15);
  v1.push_back(18);
  v2.push_back(12);

  EXPECT_TRUE(
      SortedRangesHaveIntersection(v1.begin(), v1.end(), v2.begin(), v2.end()));
  EXPECT_TRUE(
      SortedRangesHaveIntersection(v2.begin(), v2.end(), v1.begin(), v1.end()));
  EXPECT_TRUE(SortedRangesHaveIntersection(v1.rbegin(), v1.rend(), v2.rbegin(),
                                           v2.rend(), std::greater<int>()));
  EXPECT_TRUE(SortedRangesHaveIntersection(v2.rbegin(), v2.rend(), v1.rbegin(),
                                           v1.rend(), std::greater<int>()));
}

// Containers of different types (and with different value_types.)
TEST(SortedRangesHaveIntersection, WorksOnDifferentContainers) {
  const int ia1[] = {1, 5, 8, 9};
  const char ca2[] = {3, 6, 10};

  std::set<int> c1(ia1, ia1 + std::size(ia1));
  std::vector<char> c2(ca2, ca2 + std::size(ca2));

  EXPECT_FALSE(
      SortedRangesHaveIntersection(c1.begin(), c1.end(), c2.begin(), c2.end()));
  EXPECT_FALSE(
      SortedRangesHaveIntersection(c2.begin(), c2.end(), c1.begin(), c1.end()));
  EXPECT_FALSE(SortedRangesHaveIntersection(c1.rbegin(), c1.rend(), c2.rbegin(),
                                            c2.rend(), std::greater<int>()));
  EXPECT_FALSE(SortedRangesHaveIntersection(c2.rbegin(), c2.rend(), c1.rbegin(),
                                            c1.rend(), std::greater<int>()));

  c1.insert(12);
  c1.insert(15);
  c1.insert(18);
  c2.push_back(12);

  EXPECT_TRUE(
      SortedRangesHaveIntersection(c1.begin(), c1.end(), c2.begin(), c2.end()));
  EXPECT_TRUE(
      SortedRangesHaveIntersection(c2.begin(), c2.end(), c1.begin(), c1.end()));
  EXPECT_TRUE(SortedRangesHaveIntersection(c1.rbegin(), c1.rend(), c2.rbegin(),
                                           c2.rend(), std::greater<int>()));
  EXPECT_TRUE(SortedRangesHaveIntersection(c2.rbegin(), c2.rend(), c1.rbegin(),
                                           c1.rend(), std::greater<int>()));
}

// Check the case where we don't compare the begin() and end() of a container.
TEST(SortedRangesHaveIntersection, WorksOnParts) {
  std::set<int> s1, s2;
  s1.insert(1);
  s1.insert(2);
  s1.insert(3);
  s1.insert(4);

  s2.insert(1);
  s2.insert(5);

  EXPECT_TRUE(
      SortedRangesHaveIntersection(s1.begin(), s1.end(), s2.begin(), s2.end()));

  std::set<int>::const_iterator iter = s1.begin();
  ++iter;
  EXPECT_FALSE(
      SortedRangesHaveIntersection(iter, s1.end(), s2.begin(), s2.end()));
}

// Check where the ranges are empty.
TEST(SortedRangesHaveIntersection, WorksOnEmpty) {
  std::set<int> s1, s2;
  EXPECT_FALSE(
      SortedRangesHaveIntersection(s1.begin(), s1.end(), s2.begin(), s2.end()));
}

TEST(SortedRangesHaveIntersectionDeathTest, UnsortedRangesDie) {
  std::vector<int> v;
  v.push_back(4);
  v.push_back(5);
  v.push_back(3);
  EXPECT_DEBUG_DEATH(
      SortedRangesHaveIntersection(v.begin(), v.begin(), v.begin(), v.end()),
      "");
}

TEST(SortedRangesHaveIntersectionDeathTest, UnsortedRangesDieCustomComparator) {
  std::vector<int> v;
  v.push_back(4);
  v.push_back(5);
  v.push_back(3);
  EXPECT_DEBUG_DEATH(
      SortedRangesHaveIntersection(v.begin(), v.end(), v.begin(), v.begin(),
                                   std::greater<int>()),
      "");
}

TEST(SortedContainersHaveIntersection, WorksOnSameContainer) {
  EXPECT_TRUE(SortedContainersHaveIntersection(std::vector<int>{1, 3, 5},
                                               std::vector<int>{2, 3, 4}));
  EXPECT_FALSE(SortedContainersHaveIntersection(std::vector<int>{1, 2, 3},
                                                std::vector<int>{4, 5, 6}));
}

TEST(SortedContainersHaveIntersection, WorksOnDifferentContainer) {
  EXPECT_TRUE(SortedContainersHaveIntersection(std::vector<int>{1, 3, 5},
                                               std::set<int>{2, 3, 4}));
  EXPECT_FALSE(SortedContainersHaveIntersection(std::set<int>{1, 2, 3},
                                                std::vector<int>{4, 5, 6}));
}

TEST(SortedContainersHaveIntersection, WorksOnDifferentValueType) {
  EXPECT_TRUE(SortedContainersHaveIntersection(std::vector<int>{1, 3, 5},
                                               std::vector<char>{2, 3, 4}));
  EXPECT_FALSE(SortedContainersHaveIntersection(std::vector<int>{1, 2, 3},
                                                std::vector<char>{4, 5, 6}));
}

TEST(SortedContainersHaveIntersection, WorksOnEmpty) {
  EXPECT_FALSE(SortedContainersHaveIntersection(std::vector<int>{1, 3, 5},
                                                std::vector<int>{}));
}

TEST(SortedContainersHaveIntersection, WorksWithCustomComparator) {
  EXPECT_TRUE(SortedContainersHaveIntersection(std::vector<int>{5, 3, 1},
                                               std::vector<int>{4, 3, 2},
                                               std::greater<int>()));
  EXPECT_FALSE(SortedContainersHaveIntersection(std::vector<int>{3, 2, 1},
                                                std::vector<int>{6, 5, 4},
                                                std::greater<int>()));
}

TEST(SortedContainersHaveIntersectionDeathTest, UnsortedContainersDie) {
  EXPECT_DEBUG_DEATH(SortedContainersHaveIntersection(std::vector<int>{1, 3, 2},
                                                      std::vector<int>{1, 2, 3},
                                                      std::greater<int>()),
                     "");
}

TEST(SortedContainersHaveIntersectionDeathTest,
     UnsortedContainersDieCustomComparator) {
  EXPECT_DEBUG_DEATH(SortedContainersHaveIntersection(std::vector<int>{3, 2, 1},
                                                      std::vector<int>{3, 2, 3},
                                                      std::greater<int>()),
                     "");
}

TEST(ReleasePointer, Simple) {
  int a[] = {1, 2, 3};
  int* ap[] = {&a[0], &a[1], &a[2]};
  std::vector<int*> v(ap, ap + std::size(ap));

  // Check that release returns the pointer and sets the
  // element to nullptr.
  int* ptr = release_ptr(&v[1]);
  EXPECT_EQ(&a[1], ptr);

  // Also make sure that the rest of v was not affected.
  EXPECT_THAT(v, ElementsAre(&a[0], static_cast<int*>(nullptr), &a[2]));
}

TEST(STLSetSymmetricDifferenceAllOutputs, CorrectOutputsBothOrders) {
  std::set<int> left{1, 2, 4, 7};
  std::vector<int> right{1, 3, 4, 8, 10, 22};
  std::deque<int> left_only;
  std::list<int> both;
  absl::chunked_queue<int> right_only;

  STLSetSymmetricDifference(left, right, std::back_inserter(left_only),
                            std::back_inserter(both),
                            std::back_inserter(right_only));
  EXPECT_THAT(left_only, ElementsAre(2, 7));
  EXPECT_THAT(both, ElementsAre(1, 4));
  EXPECT_THAT(right_only, ElementsAre(3, 8, 10, 22));

  // Flip order for testing.
  left_only.clear();
  both.clear();
  right_only.clear();
  STLSetSymmetricDifference(right, left, std::back_inserter(right_only),
                            std::back_inserter(both),
                            std::back_inserter(left_only));
  EXPECT_THAT(left_only, ElementsAre(2, 7));
  EXPECT_THAT(both, ElementsAre(1, 4));
  EXPECT_THAT(right_only, ElementsAre(3, 8, 10, 22));
}

TEST(NoOpOutputIterator, ValidAsOutputIterator) {
  std::vector<int> a{1, 2, 3, 4};
  std::vector<int> evens;
  std::partition_copy(a.begin(), a.end(),
                      /*true_elts=*/std::back_inserter(evens),
                      /*false_elts=*/NoOpOutputIterator{},
                      [](int val) { return (val % 2) == 0; });
  EXPECT_THAT(evens, ElementsAre(2, 4));
}

// ========================================
// Benchmarks below.
//

// Benchmarks: string::assign
void BM_StringAssign(benchmark::State& state) {
  const int size = state.range(0);

  const std::string data(size, 'x');
  for (auto i : state) {
    std::string s;
    s.assign(data.begin(), data.end());
  }
}
BENCHMARK(BM_StringAssign)->Range(8, 1 << 20);

// Benchmarks: string::append
void BM_StringAppend(benchmark::State& state) {
  const int size = state.range(0);

  const std::string data(size, 'x');
  std::string s;
  int i = 0;
  for (auto st : state) {
    s.append(data.begin(), data.end());
    if (i % 100 == 0) s.clear();  // clear so 's' doesn't get too big
    ++i;
  }
}
BENCHMARK(BM_StringAppend)->Range(8, 1 << 20);

// Benchmarks: STLAppendToString
void BM_STLAppendToString(benchmark::State& state) {
  const int size = state.range(0);

  const std::string data(size, 'x');
  std::string s;
  int i = 0;
  for (auto si : state) {
    s.append(data.c_str(), data.size());
    if (i % 100 == 0) s.clear();  // clear so 's' doesn't get too big
    ++i;
  }
}
BENCHMARK(BM_STLAppendToString)->Range(8, 1 << 20);

void BM_STLStringResizeUninitialized(benchmark::State& state) {
  const int size = state.range(0);

  for (auto i : state) {
    std::string s;
    STLStringResizeUninitialized(&s, size);
  }
}
BENCHMARK(BM_STLStringResizeUninitialized)->Range(8, 1 << 20);

void BM_STLStringResizeUninitializedAmortized(benchmark::State& state) {
  const size_t size_increment = state.range(0);
  const size_t max_size = 1 << 20;
  for (auto i : state) {
    std::string s;
    for (size_t size = size_increment; size <= max_size;
         size += size_increment) {
      STLStringResizeUninitializedAmortized(&s, size);
    }
  }
}
BENCHMARK(BM_STLStringResizeUninitializedAmortized)
    ->RangeMultiplier(2)
    ->Range(2, 1 << 18);

void BM_STLStringResize(benchmark::State& state) {
  const int size = state.range(0);

  for (auto i : state) {
    std::string s;
    s.resize(size);
  }
}
BENCHMARK(BM_STLStringResize)->Range(8, 1 << 20);

void BM_STLElementDeleter(benchmark::State& state) {
  const int size = state.range(0);

  for (auto s : state) {
    std::vector<std::string*> v(size, nullptr);
    STLElementDeleter<std::vector<std::string*>> d(&v);
  }
}
BENCHMARK(BM_STLElementDeleter)->Range(8, 1 << 20);

void BM_ElementDeleter(benchmark::State& state) {
  const int size = state.range(0);

  for (auto s : state) {
    std::vector<std::string*> v(size, nullptr);
    ElementDeleter d(&v);
  }
}
BENCHMARK(BM_ElementDeleter)->Range(8, 1 << 20);

}  // namespace
}  // namespace gtl
