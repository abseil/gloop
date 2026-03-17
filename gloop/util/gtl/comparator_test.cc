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

#include "gloop/util/gtl/comparator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/casts.h"
#include "absl/functional/any_invocable.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::testing::ElementsAre;
using ::testing::NanSensitiveDoubleEq;
using ::testing::NanSensitiveFloatEq;
using ::testing::Pair;
using ::testing::Pointee;
using ::testing::Pointwise;

// The following using declarations ensure that the methods defined in
// comparator.h are in the right namespace.

// Extractors.
using ::gtl::ExtractIdentity;
using ::gtl::ExtractPointee;
using ::gtl::First;
using ::gtl::Second;
using ::gtl::Size;
using ::gtl::TupleElement;

// Atomic comparators.
using ::gtl::Greater;
using ::gtl::Less;
using ::gtl::NanFirstGreater;
using ::gtl::NanFirstLess;

// Composite comparators.
using ::gtl::ChainComparators;
using ::gtl::LexicographicalComparator;
using ::gtl::OrderBy;
using ::gtl::OrderByFirst;
using ::gtl::OrderByFirstGreater;
using ::gtl::OrderByPointee;
using ::gtl::OrderBySecond;
using ::gtl::OrderBySecondGreater;
using ::gtl::OrderByTupleElement;
using ::gtl::OrderByTupleElementGreater;
using ::gtl::Reverse;

typedef std::pair<std::string, int> StringInt;

struct A {
  int v;
};

struct B {
  int v;
};

auto operator<=>(const B& lhs, const A& rhs) { return lhs.v <=> rhs.v; }
auto operator<=>(const A& lhs, const B& rhs) { return lhs.v <=> rhs.v; }

struct ThreeWay {
  template <typename T1, typename T2>
  int Compare(const T1& lhs, const T2& rhs) const {
    auto result = lhs.v <=> rhs.v;
    return result <= 0 ? result >= 0 ? 0 : -1 : 1;
  }
};

// Tests combining simple extractors and inner comparators using OrderBy<>.

TEST(OrderByTest, WorksAsTplArgForSet) {
  // Elements in this set is sorted by the first field in descending
  // order.
  typedef std::set<StringInt, OrderBy<First, Greater>> FirstDescendingSet;
  FirstDescendingSet s1 = {StringInt("a", 2), StringInt("b", 1)};

  EXPECT_THAT(s1, ElementsAre(StringInt("b", 1), StringInt("a", 2)));

  // Elements in this set is sorted by the second field, ascending.
  typedef std::set<StringInt, OrderBy<Second>> SecondAscendingSet;
  SecondAscendingSet s2 = {StringInt("b", 2), StringInt("a", 1)};

  EXPECT_THAT(s2, ElementsAre(StringInt("a", 1), StringInt("b", 2)));
}

TEST(OrderByTest, WorksAsArgForSort) {
  std::vector<StringInt> v = {StringInt("a", 3), StringInt("c", 2),
                              StringInt("b", 1)};

  // Sort by the first field, ascending.
  std::sort(v.begin(), v.end(), OrderBy<First>());
  EXPECT_THAT(
      v, ElementsAre(StringInt("a", 3), StringInt("b", 1), StringInt("c", 2)));

  // Sort by the second field, descending.
  std::sort(v.begin(), v.end(), OrderBy<Second, Greater>());
  EXPECT_THAT(
      v, ElementsAre(StringInt("a", 3), StringInt("c", 2), StringInt("b", 1)));
}

TEST(OrderByTestForMap, OrderByFirst) {
  // Sort keys by the first field, ascending.
  std::map<StringInt, int, OrderBy<First>> m1(
      {{StringInt("a", 3), 1}, {StringInt("c", 2), 3}, {StringInt("b", 1), 2}});
  EXPECT_THAT(
      m1, ElementsAre(Pair(StringInt("a", 3), 1), Pair(StringInt("b", 1), 2),
                      Pair(StringInt("c", 2), 3)));
}

// Tests that gtl::TupleElement<0> can do what gtl::First does, for a std::pair.
TEST(OrderByTestForMap, OrderPairByTupleElement0) {
  // Sort keys by the first field, ascending.
  std::map<StringInt, int, OrderBy<TupleElement<0>>> m1(
      {{StringInt("a", 3), 1}, {StringInt("c", 2), 3}, {StringInt("b", 1), 2}});
  EXPECT_THAT(
      m1, ElementsAre(Pair(StringInt("a", 3), 1), Pair(StringInt("b", 1), 2),
                      Pair(StringInt("c", 2), 3)));
}

// Tests that gtl::TupleElement<0> works for a std::tuple.
TEST(OrderByTestForMap, OrderTupleByElement0) {
  using StringIntTuple = std::tuple<std::string, int>;
  // Sort keys by the first field, ascending.
  std::map<StringIntTuple, int, OrderBy<TupleElement<0>>> m1(
      {{StringIntTuple("a", 3), 1},
       {StringIntTuple("c", 2), 3},
       {StringIntTuple("b", 1), 2}});
  EXPECT_THAT(m1, ElementsAre(Pair(StringIntTuple("a", 3), 1),
                              Pair(StringIntTuple("b", 1), 2),
                              Pair(StringIntTuple("c", 2), 3)));
}

// Tests that gtl::TupleElement<1> can do what gtl::Second does, for a
// std::pair.
TEST(OrderByTestForMap, OrderPairByTupleElement2) {
  // Sort by the second field, descending.
  std::map<StringInt, int, OrderBy<TupleElement<1>, Greater>> m2(
      {{StringInt("a", 3), 1}, {StringInt("c", 2), 3}, {StringInt("b", 1), 2}});
  EXPECT_THAT(
      m2, ElementsAre(Pair(StringInt("a", 3), 1), Pair(StringInt("c", 2), 3),
                      Pair(StringInt("b", 1), 2)));
}

TEST(OrderByTestForMap, OrderBySecond) {
  // Sort by the second field, descending.
  std::map<StringInt, int, OrderBy<Second, Greater>> m2(
      {{StringInt("a", 3), 1}, {StringInt("c", 2), 3}, {StringInt("b", 1), 2}});
  EXPECT_THAT(
      m2, ElementsAre(Pair(StringInt("a", 3), 1), Pair(StringInt("c", 2), 3),
                      Pair(StringInt("b", 1), 2)));
}

TEST(OrderByTest, OrderByIdentity) {
  std::vector<std::string> v = {"abc", "c", "bc"};
  std::sort(v.begin(), v.end(), OrderBy(ExtractIdentity(), Less()));
  EXPECT_THAT(v, ElementsAre("abc", "bc", "c"));
  std::sort(v.begin(), v.end(), OrderBy(ExtractIdentity(), Greater()));
  EXPECT_THAT(v, ElementsAre("c", "bc", "abc"));
}

TEST(OrderByTest, OrderBySize) {
  std::vector<std::string> v = {"abc", "c", "bc"};

  std::sort(v.begin(), v.end(), OrderBy<Size>());
  EXPECT_THAT(v, ElementsAre("c", "bc", "abc"));
}

TEST(OrderByTest, CTAD) {
  std::vector<std::string> v = {"abc", "c", "bc"};

  std::sort(v.begin(), v.end(), OrderBy(Size{}));
  EXPECT_THAT(v, ElementsAre("c", "bc", "abc"));

  std::sort(v.begin(), v.end(), OrderBy(Size{}, std::greater<>{}));
  EXPECT_THAT(v, ElementsAre("abc", "bc", "c"));

  struct S {
    int neg() const { return -i; }
    void overloaded(int*) {}
    int overloaded() const { return i; }
    int i;

    // To simplify Eq.
    operator int() const { return i; }  // NOLINT
  };

  S s[4] = {{1}, {0}, {-1}, {3}};

  // Pointer to member field
  std::sort(std::begin(s), std::end(s), OrderBy(&S::i));
  EXPECT_THAT(s, ElementsAre(-1, 0, 1, 3));

  // Pointer to member function
  std::sort(std::begin(s), std::end(s), OrderBy(&S::neg));
  EXPECT_THAT(s, ElementsAre(3, 1, 0, -1));

  // Pointer to overloaded member function.
  // This is a regression test to support the old OrderByGette signature.
  std::sort(std::begin(s), std::end(s), OrderBy(&S::overloaded));
  EXPECT_THAT(s, ElementsAre(-1, 0, 1, 3));
}

// Tests the short-hands for common OrderBy<> combinations.

TEST(OrderByFirstTest, Works) {
  std::vector<StringInt> v = {StringInt("a", 3), StringInt("c", 2),
                              StringInt("b", 1)};

  // Sort by the first field, ascending.
  std::sort(v.begin(), v.end(), OrderByFirst());
  EXPECT_THAT(
      v, ElementsAre(StringInt("a", 3), StringInt("b", 1), StringInt("c", 2)));
}

TEST(OrderByFirstGreaterTest, Works) {
  std::vector<StringInt> v = {StringInt("a", 3), StringInt("c", 2),
                              StringInt("b", 1)};

  // Sort by the first field, descending.
  std::sort(v.begin(), v.end(), OrderByFirstGreater());
  EXPECT_THAT(
      v, ElementsAre(StringInt("c", 2), StringInt("b", 1), StringInt("a", 3)));
}

TEST(OrderBySecondTest, Works) {
  std::vector<StringInt> v = {StringInt("a", 3), StringInt("c", 2),
                              StringInt("b", 1)};

  // Sort by the second field, ascending.
  std::sort(v.begin(), v.end(), OrderBySecond());
  EXPECT_THAT(
      v, ElementsAre(StringInt("b", 1), StringInt("c", 2), StringInt("a", 3)));
}

TEST(OrderBySecondGreaterTest, Works) {
  std::vector<StringInt> v = {StringInt("a", 3), StringInt("c", 2),
                              StringInt("b", 1)};

  // Sort by the second field, descending.
  std::sort(v.begin(), v.end(), OrderBySecondGreater());
  EXPECT_THAT(
      v, ElementsAre(StringInt("a", 3), StringInt("c", 2), StringInt("b", 1)));
}

TEST(OrderByTupleElementTest, Works) {
  std::vector<StringInt> v = {StringInt("a", 3), StringInt("c", 2),
                              StringInt("b", 1)};

  // Sort by the first field, ascending.
  std::sort(v.begin(), v.end(), OrderByTupleElement<0>());
  EXPECT_THAT(
      v, ElementsAre(StringInt("a", 3), StringInt("b", 1), StringInt("c", 2)));
}

struct TupleLike {
  int value;
};

template <size_t>
const int& get(const TupleLike& t) {
  return t.value;
}

}  // namespace

namespace std {

template <>
struct tuple_size<TupleLike> : std::integral_constant<std::size_t, 1> {};

template <>
struct tuple_element<0, TupleLike> {
  using type = int;
};

}  // namespace std

namespace {

TEST(OrderByTupleElementTest, WorksWithTupleLikeObjects) {
  std::vector<TupleLike> v = {{3}, {2}};

  std::sort(v.begin(), v.end(), OrderByTupleElement<0>());
  EXPECT_EQ(v[0].value, 2);
  EXPECT_EQ(v[1].value, 3);
}

TEST(OrderByTupleElementGreaterTest, Works) {
  std::vector<StringInt> v = {StringInt("a", 3), StringInt("c", 2),
                              StringInt("b", 1)};

  // Sort by the second field, descending.
  std::sort(v.begin(), v.end(), OrderByTupleElementGreater<1>());
  EXPECT_THAT(
      v, ElementsAre(StringInt("a", 3), StringInt("c", 2), StringInt("b", 1)));
}

// Tests the OrderBy*() wrapper functions.

const std::string& GetStringIntFirst(const StringInt& p) { return p.first; }
bool StringLess(absl::string_view x, absl::string_view y) { return x < y; }

TEST(OrderByPropertyTest, WorksAsArgForSort) {
  std::vector<StringInt> v = {StringInt("a", 3), StringInt("c", 2),
                              StringInt("b", 1)};

  // Sort by the first field, ascending, using functors.
  std::sort(v.begin(), v.end(), OrderBy(First()));
  EXPECT_THAT(
      v, ElementsAre(StringInt("a", 3), StringInt("b", 1), StringInt("c", 2)));

  // Sort by the second field, descending, using functors.
  std::sort(v.begin(), v.end(), OrderBy(Second(), Greater()));
  EXPECT_THAT(
      v, ElementsAre(StringInt("a", 3), StringInt("c", 2), StringInt("b", 1)));

  // Sort by the first field, ascending, using function pointers.
  std::sort(v.begin(), v.end(), OrderBy(GetStringIntFirst, StringLess));
  EXPECT_THAT(
      v, ElementsAre(StringInt("a", 3), StringInt("b", 1), StringInt("c", 2)));
}

struct OrderByLength {
  bool operator()(absl::string_view lhs, absl::string_view rhs) const {
    return lhs.size() < rhs.size();
  }

  int Compare(absl::string_view lhs, absl::string_view rhs) const {
    if (lhs.size() == rhs.size()) {
      return 0;
    } else if (lhs.size() < rhs.size()) {
      return -1;
    } else {
      return 1;
    }
  }
};

TEST(ReverseTest, Construction) {
  Reverse<OrderByLength> cmp1;
  auto cmp2 = Reverse(OrderByLength());
  EXPECT_TRUE(std::is_empty_v<decltype(cmp1)>);
  EXPECT_TRUE((std::is_same_v<decltype(cmp1), decltype(cmp2)>));
}

TEST(ReverseTest, OrderingWorks) {
  using str = absl::string_view;

  OrderByLength base;
  auto cmp = Reverse(base);

  EXPECT_TRUE(base(str("x"), str("yy")));
  EXPECT_FALSE(cmp(str("x"), str("yy")));

  EXPECT_FALSE(base(str("x"), str("y")));
  EXPECT_FALSE(cmp(str("x"), str("y")));

  EXPECT_FALSE(base(str("xx"), str("y")));
  EXPECT_TRUE(cmp(str("xx"), str("y")));
}

TEST(ReverseTest, CompareWorks) {
  using str = absl::string_view;

  OrderByLength base;
  auto cmp = Reverse(base);

  EXPECT_LT(base.Compare(str("x"), str("yy")), 0);
  EXPECT_LE(base.Compare(str("x"), str("yy")), 0);
  EXPECT_GT(cmp.Compare(str("x"), str("yy")), 0);
  EXPECT_GE(cmp.Compare(str("x"), str("yy")), 0);

  EXPECT_EQ(base.Compare(str("x"), str("y")), 0);
  EXPECT_LE(base.Compare(str("x"), str("y")), 0);
  EXPECT_GE(base.Compare(str("x"), str("y")), 0);
  EXPECT_EQ(cmp.Compare(str("x"), str("y")), 0);
  EXPECT_GE(cmp.Compare(str("x"), str("y")), 0);
  EXPECT_LE(cmp.Compare(str("x"), str("y")), 0);

  EXPECT_GT(base.Compare(str("xx"), str("y")), 0);
  EXPECT_GT(base.Compare(str("xx"), str("y")), 0);
  EXPECT_LT(cmp.Compare(str("xx"), str("y")), 0);
  EXPECT_LE(cmp.Compare(str("xx"), str("y")), 0);
}

TEST(OrderByPropertyTest, OrderByIsSmall) {
  const auto extractor = Second();
  const auto comparator = Greater();
  static_assert(std::is_empty<Second>::value, "Second is not empty.");
  static_assert(std::is_empty<Greater>::value, "Greater is not empty.");
  static_assert(
      sizeof(std::tuple<Second, Greater>) < sizeof(Second) + sizeof(Greater),
      "std::tuple<Second, Greater> doesn't seem to use EBCO.");
  const auto ordering = OrderBy(extractor, comparator);
  EXPECT_LT(sizeof(ordering), sizeof(extractor) + sizeof(comparator));
}

TEST(OrderByPropertyTest, ChainComparators) {
  std::vector<std::pair<int, int>> pairs = {{2, 2}, {1, 2}, {3, 1}, {2, 3}};
  std::sort(pairs.begin(), pairs.end(),
            ChainComparators(OrderBySecondGreater(), OrderByFirst()));
  EXPECT_THAT(pairs,
              ElementsAre(Pair(2, 3), Pair(1, 2), Pair(2, 2), Pair(3, 1)));
}

TEST(LexicographicalComparatorTest, Works) {
  std::vector<std::vector<int>> v{{1, 2, 3}, {3}, {2}, {1, 2, 1}, {}, {1, 2}};
  std::sort(v.begin(), v.end(), gtl::LexicographicalComparator());
  EXPECT_THAT(
      v, ElementsAre(ElementsAre(), ElementsAre(1, 2), ElementsAre(1, 2, 1),
                     ElementsAre(1, 2, 3), ElementsAre(2), ElementsAre(3)));
}

TEST(LexicographicalComparatorTest, WorksNoMake) {
  std::vector<std::vector<int>> v{{1, 2, 3}, {3}, {2}, {1, 2, 1}, {}, {1, 2}};
  std::sort(v.begin(), v.end(), gtl::LexicographicalComparator());
  EXPECT_THAT(
      v, ElementsAre(ElementsAre(), ElementsAre(1, 2), ElementsAre(1, 2, 1),
                     ElementsAre(1, 2, 3), ElementsAre(2), ElementsAre(3)));
}

TEST(LexicographicalComparatorTest, WorksWithInnerComparator) {
  std::vector<std::vector<int>> v{{1, 2, 3}, {3}, {2}, {1, 2, 1}, {}, {1, 2}};
  std::sort(v.begin(), v.end(), gtl::LexicographicalComparator(Greater()));
  EXPECT_THAT(v, ElementsAre(ElementsAre(), ElementsAre(3), ElementsAre(2),
                             ElementsAre(1, 2), ElementsAre(1, 2, 3),
                             ElementsAre(1, 2, 1)));
}

TEST(LexicographicalComparatorTest, ObjectWorksWithInnerComparator) {
  std::vector<std::vector<int>> v{{1, 2, 3}, {3}, {2}, {1, 2, 1}, {}, {1, 2}};
  std::sort(v.begin(), v.end(), gtl::LexicographicalComparator(Greater()));
  EXPECT_THAT(v, ElementsAre(ElementsAre(), ElementsAre(3), ElementsAre(2),
                             ElementsAre(1, 2), ElementsAre(1, 2, 3),
                             ElementsAre(1, 2, 1)));
}

TEST(LexicographicalComparatorTest, VectorCustomSortExample) {
  std::vector<std::pair<int, int>> va[] = {{{1, 1}, {2, 2}, {3, 3}},
                                           {{3, 4}},
                                           {{2, 5}, {1, 6}, {2, 7}, {3, 8}},
                                           {{0, 9}, {0, 10}}};
  std::sort(std::begin(va), std::end(va),
            gtl::LexicographicalComparator(OrderByFirst()));
  EXPECT_THAT(va, ElementsAre(ElementsAre(Pair(0, 9), Pair(0, 10)),
                              ElementsAre(Pair(1, 1), Pair(2, 2), Pair(3, 3)),
                              ElementsAre(Pair(2, 5), Pair(1, 6), Pair(2, 7),
                                          Pair(3, 8)),
                              ElementsAre(Pair(3, 4))));
}

struct MyStruct {
  MyStruct(int an_x, int a_y) : x(an_x), y(a_y) {}

  bool operator==(const MyStruct& rhs) const {
    return x == rhs.x && y == rhs.y;
  }

  int x;
  int y;
};

TEST(OrderByFieldTest, WorksWithNonPointers) {
  std::vector<MyStruct> v = {MyStruct(1, 1), MyStruct(3, 2), MyStruct(2, 3)};

  // Sort by the .x field, descending.
  std::sort(v.begin(), v.end(), OrderBy(&MyStruct::x, Greater()));
  EXPECT_THAT(v, ElementsAre(MyStruct(3, 2), MyStruct(2, 3), MyStruct(1, 1)));

  // Sort by the .y field, ascending.
  std::sort(v.begin(), v.end(), OrderBy(&MyStruct::y));
  EXPECT_THAT(v, ElementsAre(MyStruct(1, 1), MyStruct(3, 2), MyStruct(2, 3)));
}

class MyClass {
 public:
  MyClass(int x, int y) : x_(x), y_(y) {}

  bool operator==(const MyClass& rhs) const {
    return x_ == rhs.x_ && y_ == rhs.y_;
  }

  int x() const { return x_; }
  int y() const { return y_; }

 private:
  int x_;
  int y_;
};

TEST(OrderByGetterTest, WorksWithNonPointers) {
  std::vector<MyClass> v = {MyClass(1, 1), MyClass(3, 2), MyClass(2, 3)};

  // Sort by the .x() getter, descending.
  std::sort(v.begin(), v.end(), OrderBy(&MyClass::x, Greater()));
  EXPECT_THAT(v, ElementsAre(MyClass(3, 2), MyClass(2, 3), MyClass(1, 1)));

  // Sort by the .y() getter, ascending.
  std::sort(v.begin(), v.end(), OrderBy(&MyClass::y));
  EXPECT_THAT(v, ElementsAre(MyClass(1, 1), MyClass(3, 2), MyClass(2, 3)));
}

class Base {
 public:
  Base(absl::string_view s, int x) : s_(s), x_(x) {}

  // For testing using extractors with reference-returning getters.
  const std::string& s() const { return s_; }
  int x() const { return x_; }

 private:
  std::string s_;
  int x_;
};

class Derived : public Base {
 public:
  Derived(absl::string_view s, int x, bool a_y, absl::string_view a_z)
      : Base(s, x), y(a_y), z(a_z) {}

  bool operator==(const Derived& rhs) const {
    return s() == rhs.s() && x() == rhs.x() && y == rhs.y && z == rhs.z;
  }

  bool y;
  std::string z;
};

void PrintTo(const Derived& d, ::std::ostream* os) {
  *os << "s: " << d.s() << " x: " << d.x() << " y: " << d.y << " z: " << d.z;
}

TEST(OrderByPointeeTest, ExtractPointeeBasic) {
  int i = 123;
  EXPECT_EQ(123, ExtractPointee()(&i));
  EXPECT_EQ(&i, &ExtractPointee()(&i));  // Make sure it is a ref.
  EXPECT_EQ('x', ExtractPointee()("x"));
}

TEST(OrderByPointeeTest, ExtractPointeeWithOrderBy) {
  std::vector<const char*> v{"b", "a", "c"};
  std::sort(v.begin(), v.end(), OrderBy(ExtractPointee()));
  EXPECT_THAT(v, ElementsAre("a", "b", "c"));
  std::sort(v.begin(), v.end(), OrderBy(ExtractPointee(), Greater()));
  EXPECT_THAT(v, ElementsAre("c", "b", "a"));
}

TEST(OrderByPointeeTest, Type) {
  static_assert(std::is_same<decltype(OrderByPointee(Greater{})),
                             OrderBy<ExtractPointee, Greater>>(),
                "");
}

TEST(OrderByPointeeTest, OrderByPointee) {
  std::vector<std::string> a{"b", "a", "c"};
  std::vector<const std::string*> pv{&a[0], &a[1], &a[2]};
  std::sort(pv.begin(), pv.end(), OrderByPointee());
  EXPECT_THAT(pv,
              ElementsAre(Pointee(std::string("a")), Pointee(std::string("b")),
                          Pointee(std::string("c"))));
  std::sort(pv.begin(), pv.end(), OrderByPointee(Greater()));
  EXPECT_THAT(pv,
              ElementsAre(Pointee(std::string("c")), Pointee(std::string("b")),
                          Pointee(std::string("a"))));
}

TEST(OrderByPointeeTest, OrderByPointeeSupportsMoveOnlyComparator) {
  std::vector<std::string> a{"b", "a", "c"};
  std::vector<const std::string*> pv{&a[0], &a[1], &a[2]};
  std::sort(pv.begin(), pv.end(),
            OrderByPointee(
                // AnyInvocable is not copyable.
                absl::AnyInvocable<bool(const std::string&, const std::string&)
                                       const>(std::less<>())));
  EXPECT_THAT(pv,
              ElementsAre(Pointee(std::string("a")), Pointee(std::string("b")),
                          Pointee(std::string("c"))));
}

TEST(ChainComparatorsTest, NoComparators) {
  std::vector<int> v = {2, 1, 3};
  std::stable_sort(v.begin(), v.end(), ChainComparators());
  EXPECT_THAT(v, ElementsAre(2, 1, 3));
}

TEST(ChainComparatorsTest, HeterogeneousComparison) {
  EXPECT_TRUE(ChainComparators(ThreeWay(), Greater())(A{1}, B{2}));
  EXPECT_FALSE(ChainComparators(ThreeWay(), Less())(B{2}, A{1}));
  EXPECT_TRUE(ChainComparators(Less(), Greater())(A{1}, B{2}));
  EXPECT_TRUE(ChainComparators(Greater(), Less())(B{2}, A{1}));
  EXPECT_LT(ChainComparators(Less(), Greater()).Compare(A{1}, B{2}), 0);
  EXPECT_LT(ChainComparators(Greater(), Less()).Compare(B{2}, A{1}), 0);
}

TEST(ChainComparatorsTest, OneComparator) {
  std::vector<int> v = {2, 1, 3};
  std::stable_sort(v.begin(), v.end(), ChainComparators(Less()));
  EXPECT_THAT(v, ElementsAre(1, 2, 3));
  std::stable_sort(v.begin(), v.end(), ChainComparators(Greater()));
  EXPECT_THAT(v, ElementsAre(3, 2, 1));
}

TEST(ChainComparatorsTest, WorksForTwoComparators) {
  std::vector<Derived> v = {
      Derived("a", 1, true, "a"), Derived("b", 2, false, "b"),
      Derived("b", 2, true, "c"), Derived("d", 2, false, "c")};

  std::sort(v.begin(), v.end(),
            ChainComparators(OrderBy(&Derived::y), OrderBy(&Derived::z)));
  EXPECT_THAT(
      v, ElementsAre(Derived("b", 2, false, "b"), Derived("d", 2, false, "c"),
                     Derived("a", 1, true, "a"), Derived("b", 2, true, "c")));

  std::sort(
      v.begin(), v.end(),
      ChainComparators(OrderBy(&Derived::y), OrderBy(&Derived::z, Greater())));
  EXPECT_THAT(
      v, ElementsAre(Derived("d", 2, false, "c"), Derived("b", 2, false, "b"),
                     Derived("b", 2, true, "c"), Derived("a", 1, true, "a")));

  std::sort(
      v.begin(), v.end(),
      ChainComparators(OrderBy(&Derived::y, Greater()), OrderBy(&Derived::z)));
  EXPECT_THAT(
      v, ElementsAre(Derived("a", 1, true, "a"), Derived("b", 2, true, "c"),
                     Derived("b", 2, false, "b"), Derived("d", 2, false, "c")));

  std::sort(
      v.begin(), v.end(),
      ChainComparators(OrderBy(&Derived::x), OrderBy(&Derived::y, Greater()),
                       OrderBy(&Derived::z, Greater())));
  EXPECT_THAT(
      v, ElementsAre(Derived("a", 1, true, "a"), Derived("b", 2, true, "c"),
                     Derived("d", 2, false, "c"), Derived("b", 2, false, "b")));

  std::sort(v.begin(), v.end(),
            ChainComparators(OrderBy(&Derived::x), OrderBy(&Derived::y),
                             OrderBy(&Derived::z, Greater())));
  EXPECT_THAT(
      v, ElementsAre(Derived("a", 1, true, "a"), Derived("d", 2, false, "c"),
                     Derived("b", 2, false, "b"), Derived("b", 2, true, "c")));

  // Go crazy. Chains inside chains.
  std::sort(
      v.begin(), v.end(),
      ChainComparators(ChainComparators(OrderBy(&Derived::s, Greater()),
                                        OrderBy(&Derived::x)),
                       ChainComparators(OrderBy(&Derived::y),
                                        OrderBy(&Derived::z, Greater()))));
  EXPECT_THAT(
      v, ElementsAre(Derived("d", 2, false, "c"), Derived("b", 2, false, "b"),
                     Derived("b", 2, true, "c"), Derived("a", 1, true, "a")));
}

TEST(ChainComparatorsTest, WorksForThreeComparators) {
  std::vector<Derived> v = {
      Derived("a", 1, true, "a"), Derived("b", 2, false, "b"),
      Derived("b", 2, true, "c"), Derived("d", 2, false, "c")};

  std::sort(v.begin(), v.end(),
            ChainComparators(OrderBy(&Derived::x), OrderBy(&Derived::y),
                             OrderBy(&Derived::z)));
  EXPECT_THAT(
      v, ElementsAre(Derived("a", 1, true, "a"), Derived("b", 2, false, "b"),
                     Derived("d", 2, false, "c"), Derived("b", 2, true, "c")));

  std::sort(
      v.begin(), v.end(),
      ChainComparators(OrderBy(&Derived::x), OrderBy(&Derived::y, Greater()),
                       OrderBy(&Derived::z)));
  EXPECT_THAT(
      v, ElementsAre(Derived("a", 1, true, "a"), Derived("b", 2, true, "c"),
                     Derived("b", 2, false, "b"), Derived("d", 2, false, "c")));

  std::sort(
      v.begin(), v.end(),
      ChainComparators(OrderBy(&Derived::x), OrderBy(&Derived::y, Greater()),
                       OrderBy(&Derived::z, Greater())));
  EXPECT_THAT(
      v, ElementsAre(Derived("a", 1, true, "a"), Derived("b", 2, true, "c"),
                     Derived("d", 2, false, "c"), Derived("b", 2, false, "b")));
}

TEST(ChainComparatorsTest, WorksForFourComparators) {
  std::vector<Derived> v = {
      Derived("a", 1, true, "a"), Derived("b", 2, false, "b"),
      Derived("b", 2, true, "a"), Derived("b", 2, true, "b")};

  std::sort(v.begin(), v.end(),
            ChainComparators(OrderBy(&Derived::s), OrderBy(&Derived::x),
                             OrderBy(&Derived::y), OrderBy(&Derived::z)));
  EXPECT_THAT(
      v, ElementsAre(Derived("a", 1, true, "a"), Derived("b", 2, false, "b"),
                     Derived("b", 2, true, "a"), Derived("b", 2, true, "b")));

  std::sort(
      v.begin(), v.end(),
      ChainComparators(OrderBy(&Derived::x), OrderBy(&Derived::y, Greater()),
                       OrderBy(&Derived::z, Greater()), OrderBy(&Derived::s)));
  EXPECT_THAT(
      v, ElementsAre(Derived("a", 1, true, "a"), Derived("b", 2, true, "b"),
                     Derived("b", 2, true, "a"), Derived("b", 2, false, "b")));
}

class FiveMembers : public Derived {
 public:
  FiveMembers(absl::string_view s, int x, bool y, absl::string_view z, float f)
      : Derived(s, x, y, z), f_(f) {}

  float f() const { return f_; }

 private:
  float f_;
};

TEST(ChainComparatorsTest, WorksForFiveComparators) {
  std::vector<FiveMembers> v = {FiveMembers("a", 1, true, "a", 1.0f),
                                FiveMembers("b", 2, false, "a", 1.5f),
                                FiveMembers("b", 3, false, "a", 0.3f),
                                FiveMembers("b", 3, true, "a", 1.1f),
                                FiveMembers("b", 3, true, "b", 1.2f),
                                FiveMembers("b", 3, true, "b", 2.0f)};

  std::sort(
      v.begin(), v.end(),
      ChainComparators(OrderBy(&FiveMembers::z), OrderBy(&FiveMembers::s),
                       OrderBy(&FiveMembers::f, Greater()),
                       OrderBy(&FiveMembers::x), OrderBy(&FiveMembers::y)));
  EXPECT_THAT(v, ElementsAre(FiveMembers("a", 1, true, "a", 1.0f),
                             FiveMembers("b", 2, false, "a", 1.5f),
                             FiveMembers("b", 3, true, "a", 1.1f),
                             FiveMembers("b", 3, false, "a", 0.3f),
                             FiveMembers("b", 3, true, "b", 2.0f),
                             FiveMembers("b", 3, true, "b", 1.2f)));

  std::sort(v.begin(), v.end(),
            ChainComparators(OrderBy(&FiveMembers::s), OrderBy(&FiveMembers::x),
                             OrderBy(&FiveMembers::y), OrderBy(&FiveMembers::z),
                             OrderBy(&FiveMembers::f)));
  EXPECT_THAT(v, ElementsAre(FiveMembers("a", 1, true, "a", 1.0f),
                             FiveMembers("b", 2, false, "a", 1.5f),
                             FiveMembers("b", 3, false, "a", 0.3f),
                             FiveMembers("b", 3, true, "a", 1.1f),
                             FiveMembers("b", 3, true, "b", 1.2f),
                             FiveMembers("b", 3, true, "b", 2.0f)));
}

TEST(ChainComparatorsTest, WorksWithMap) {
  struct Cmp {
    bool operator()(const MyClass& a, const MyClass& b) const {
      return ChainComparators(OrderBy(&MyClass::x), OrderBy(&MyClass::y))(a, b);
    }
  };
  std::map<MyClass, int, Cmp> m = {
      {{3, 5}, 4}, {{1, 4}, 1}, {{2, 3}, 3}, {{2, 2}, 2}};
  EXPECT_THAT(m, ElementsAre(Pair(MyClass(1, 4), 1), Pair(MyClass(2, 2), 2),
                             Pair(MyClass(2, 3), 3), Pair(MyClass(3, 5), 4)));
}

TEST(ChainComparatorsTest, CompareTernary) {
  struct X {
    int x;
    int y;
  };
  auto cmp = ChainComparators(OrderBy(&X::x), OrderBy(&X::y));
  EXPECT_GT(0, cmp.Compare(X{1, 1}, X{1, 2}));
  EXPECT_EQ(0, cmp.Compare(X{1, 2}, X{1, 2}));
  EXPECT_LT(0, cmp.Compare(X{1, 3}, X{1, 2}));
  EXPECT_GT(0, cmp.Compare(X{0, 2}, X{1, 2}));
  EXPECT_EQ(0, cmp.Compare(X{1, 2}, X{1, 2}));
  EXPECT_LT(0, cmp.Compare(X{2, 2}, X{1, 2}));
}

TEST(ChainComparatorTest, WithCustom3wayCompare) {
  struct X {
    int x;
    std::string y;
  };
  struct CanaryCmp {
    bool operator()(int a, int b) const {
      ADD_FAILURE() << "Shouldn't call two-way comparison operator";
      return false;
    }
    bool operator()(absl::string_view a, absl::string_view b) const {
      ADD_FAILURE() << "Shouldn't call two-way comparison operator";
      return false;
    }

    // This could overflow in the general case, but works for small numbers.
    int Compare(int a, int b) const { return b - a; }
    int Compare(absl::string_view a, absl::string_view b) const {
      return b.compare(a);
    }
  };
  auto cmp = ChainComparators(OrderBy(&X::x, CanaryCmp()),
                              OrderBy(&X::y, CanaryCmp()));
  EXPECT_LT(0, cmp.Compare(X{1, "a"}, X{1, "aaa"}));
  EXPECT_EQ(0, cmp.Compare(X{1, "foo"}, X{1, "foo"}));
  EXPECT_GT(0, cmp.Compare(X{1, "zz"}, X{1, "aa"}));
  EXPECT_LT(0, cmp.Compare(X{-100, "foo"}, X{100, "foo"}));
  EXPECT_EQ(0, cmp.Compare(X{25, "foo"}, X{25, "foo"}));
  EXPECT_GT(0, cmp.Compare(X{65, "foo"}, X{3, "foo"}));
}

TEST(ChainComparatorsTest, NestedCallCount) {
  struct CanaryCmp {
    int* n;
    bool operator()(const MyClass& a, const MyClass& b) const {
      ++(*n);
      return false;
    }
  };
  // Depth shouldn't add invocation counts.
  int n[3] = {0, 0, 0};
  auto cmp = ChainComparators(
      CanaryCmp{&n[0]},
      ChainComparators(CanaryCmp{&n[1]}, ChainComparators(CanaryCmp{&n[2]})));
  EXPECT_FALSE(cmp(MyClass{1, 4}, MyClass{1, 4}));
  EXPECT_EQ(2, n[0]);
  EXPECT_EQ(2, n[1]);
  EXPECT_EQ(1, n[2]);
}

TEST(ChainComparatorsTest, NestedExtractorCallCount) {
  // Extractors are only called once per input.
  int n_x = 0, n_y = 0;
  auto extract_x = [&n_x](const MyClass& obj) {
    ++n_x;
    return obj.x();
  };
  auto extract_y = [&n_y](const MyClass& obj) {
    ++n_y;
    return obj.y();
  };
  auto cmp = ChainComparators(OrderBy(extract_x), OrderBy(extract_y));
  EXPECT_FALSE(cmp(MyClass{1, 4}, MyClass{1, 4}));
  EXPECT_EQ(2, n_x);
  EXPECT_EQ(2, n_y);
}

TEST(ChainComparatorsTest, NestedCallCountWith3wayCompare) {
  struct CanaryCmp {
    int* n_2way;
    int* n_3way;

    bool operator()(const MyClass&, const MyClass&) const {
      ++(*n_2way);
      return false;
    }
    int Compare(const MyClass&, const MyClass&) const {
      ++(*n_3way);
      return 0;
    }
  };
  // Depth shouldn't add invocation counts, and since the comparator supports
  // 3-way comparison directly, it should only be called once per input pair.
  // Contrast this with ChainComparatorsTest.NestedCallCount, where the
  // comparator only supports two-way comparison (no Compare()) and therefore
  // gets called twice per input pair.
  int n_2way[3] = {0, 0, 0};
  int n_3way[3] = {0, 0, 0};
  auto cmp = ChainComparators(
      CanaryCmp{&n_2way[0], &n_3way[0]},
      ChainComparators(CanaryCmp{&n_2way[1], &n_3way[1]},
                       ChainComparators(CanaryCmp{&n_2way[2], &n_3way[2]})));
  EXPECT_FALSE(cmp(MyClass{1, 4}, MyClass{1, 4}));
  EXPECT_THAT(n_2way, ElementsAre(0, 0, 1));
  EXPECT_THAT(n_3way, ElementsAre(1, 1, 0));
}

TEST(ChainComparatorsTest, IgnoresBoolCompare) {
  // A bool Compare() shouldn't be mistaken for three-way comparison.
  struct Cmp {
    bool operator()(const MyClass&, const MyClass&) const { return false; }
    bool Compare(const MyClass&, const MyClass&) const {
      ADD_FAILURE();
      return true;
    }
  };
  auto cmp = ChainComparators(Cmp(), Cmp(), Cmp());
  EXPECT_FALSE(cmp(MyClass{1, 4}, MyClass{1, 4}));
}

// Test that the compiler recursion limit isn't breached by
// nesting ChainComparators. This sequence of 64 comparators
// would exceed the compiler's recursion limit if arranged in
// a flat ChainComparators sequence.
TEST(ChainComparatorsTest, NestingToExceedTupleLimit) {
  struct X {};
  struct Cmp {
    bool operator()(X, X) const { return false; }
  };
  Cmp c;
  auto chained = ChainComparators(ChainComparators(c, c, c, c, c, c, c, c),
                                  ChainComparators(c, c, c, c, c, c, c, c),
                                  ChainComparators(c, c, c, c, c, c, c, c),
                                  ChainComparators(c, c, c, c, c, c, c, c),
                                  ChainComparators(c, c, c, c, c, c, c, c),
                                  ChainComparators(c, c, c, c, c, c, c, c),
                                  ChainComparators(c, c, c, c, c, c, c, c),
                                  ChainComparators(c, c, c, c, c, c, c, c));
  EXPECT_FALSE(chained(X{}, X{}));
}

TEST(ChainComparatorsTest, TooManyComparators) {
  struct S {
    int a;
    int b;
    int c;
  };
  // This "comparator" is a noop. Just to inflate the number of comparators.
  struct X {
    bool operator()(S, S) const { return false; }
    std::unique_ptr<int> just_to_make_sure_we_are_not_copied;
  };
  auto cmp =
      ChainComparators(OrderBy(&S::a),                                    //
                       X(), X(), X(), X(), X(), X(), X(), X(), X(), X(),  // 10
                       X(), X(), X(), X(), X(), X(), X(), X(), X(), X(),  // 20
                       X(), X(), X(), X(), X(), X(), X(), X(), X(), X(),  // 30
                       X(), X(), X(), X(), X(), X(), X(), X(), X(), X(),  // 40
                       X(), X(), X(), X(), X(), X(), X(), X(), X(), X(),  // 50
                       OrderBy(&S::b),                                    //
                       X(), X(), X(), X(), X(), X(), X(), X(), X(), X(),  // 60
                       X(), X(), X(), X(), X(), X(), X(), X(), X(), X(),  // 70
                       X(), X(), X(), X(), X(), X(), X(), X(), X(), X(),  // 80
                       X(), X(), X(), X(), X(), X(), X(), X(), X(), X(),  // 90
                       X(), X(), X(), X(), X(), X(), X(), X(), X(), X(),  // 100
                       OrderBy(&S::c));
  // Same value.
  EXPECT_FALSE(cmp(S{0, 0, 0}, S{0, 0, 0}));

  // Each field works.
  EXPECT_TRUE(cmp(S{0, 0, 0}, S{0, 0, 1}));
  EXPECT_TRUE(cmp(S{0, 0, 0}, S{0, 1, 0}));
  EXPECT_TRUE(cmp(S{0, 0, 0}, S{1, 0, 0}));
  EXPECT_FALSE(cmp(S{0, 0, 1}, S{0, 0, 0}));
  EXPECT_FALSE(cmp(S{0, 1, 0}, S{0, 0, 0}));
  EXPECT_FALSE(cmp(S{1, 0, 0}, S{0, 0, 0}));

  // Precedence is still kept.
  EXPECT_FALSE(cmp(S{1, 0, 0}, S{0, 1, 0}));
  EXPECT_FALSE(cmp(S{1, 0, 0}, S{0, 0, 1}));
  EXPECT_TRUE(cmp(S{0, 1, 0}, S{1, 0, 0}));
  EXPECT_FALSE(cmp(S{0, 1, 0}, S{0, 0, 1}));
  EXPECT_TRUE(cmp(S{0, 0, 1}, S{1, 0, 0}));
  EXPECT_TRUE(cmp(S{0, 0, 1}, S{0, 1, 0}));
}

TEST(VariousComparatorsTest, PreserveEmptiness) {
  const auto extractor = Second();
  const auto comparator = Greater();
  const auto order_by = OrderBy(extractor, OrderBy(extractor));
  static_assert(std::is_empty_v<Second>, "Second is not empty.");
  static_assert(std::is_empty_v<Greater>, "Greater is not empty.");
  static_assert(std::is_empty_v<decltype(order_by)>, "OrderBy is not empty.");
  static_assert(
      sizeof(std::tuple<Second, Greater>) < sizeof(Second) + sizeof(Greater),
      "std::tuple<Second, Greater> doesn't seem to use EBCO.");
  const auto chained = ChainComparators(comparator, order_by);
  static_assert(std::is_empty_v<decltype(chained)>,
                "ChainComparators() is not empty.");
  EXPECT_LT(sizeof(chained), sizeof(extractor) + sizeof(order_by));

  const auto lexicographical = LexicographicalComparator(chained);
  static_assert(std::is_empty_v<decltype(lexicographical)>,
                "Lexicographical() is not empty.");

  const auto reversed = Reverse(Greater());
  static_assert(std::is_empty_v<decltype(reversed)>,
                "Reversed() is not empty.");

  const auto nested_comparator = LexicographicalComparator(ChainComparators(
      OrderBy(Second(), Reverse(LexicographicalComparator(
                            ChainComparators(OrderBy(Second())))))));
  static_assert(std::is_empty_v<decltype(nested_comparator)>,
                "Nesting comparators not empty.");
}

constexpr double Neg(double x) {  // We want to force the negation.
  static_assert(std::numeric_limits<double>::is_iec559);
  auto bit = absl::bit_cast<uint64_t>(x);
  return absl::bit_cast<double>(bit ^ 0x8000000000000000ul);
}
constexpr float Neg(float x) {  // We want to force the negation.
  static_assert(std::numeric_limits<float>::is_iec559);
  auto bit = absl::bit_cast<uint32_t>(x);
  return absl::bit_cast<float>(bit ^ 0x80000000u);
}
static_assert(std::numeric_limits<double>::has_quiet_NaN);
static_assert(std::numeric_limits<float>::has_quiet_NaN);
constexpr auto kBig = absl::bit_cast<double>(0x7feffffffffffffful);
constexpr auto kEps = absl::bit_cast<double>(0x0000000000000001ul);
constexpr auto kInf = std::numeric_limits<double>::infinity();
constexpr auto kNan = std::numeric_limits<double>::quiet_NaN();
constexpr auto kBigFloat = absl::bit_cast<float>(0x7f7fffffu);
constexpr auto kEpsFloat = absl::bit_cast<float>(0x00000001u);
constexpr auto kInfFloat = std::numeric_limits<float>::infinity();
constexpr auto kNanFloat = std::numeric_limits<float>::quiet_NaN();

TEST(NanFirstTest, Example) {
  std::vector<double> non_decreasing = {
      Neg(kNan), kNan, Neg(kNan), kNan, Neg(kInf), -1.0, 0.0,
      Neg(0.0),  0.0,  Neg(0.0),  0.0,  1.0,       kInf};
  EXPECT_TRUE(absl::c_is_sorted(non_decreasing, NanFirstLess()));

  std::vector<double> non_increasing = {
      Neg(kNan), kNan,     Neg(kNan), kNan,     kInf, 1.0,
      0.0,       Neg(0.0), 0.0,       Neg(0.0), -1.0, Neg(kInf)};
  EXPECT_TRUE(absl::c_is_sorted(non_increasing, NanFirstGreater()));
}

TEST(NanFirstTest, DifferentZeroRepresentationsAreEquivalentLessDouble) {
  EXPECT_FALSE(NanFirstLess()(0.0, Neg(0.0)));
  EXPECT_FALSE(NanFirstLess()(Neg(0.0), 0.0));
}

TEST(NanFirstTest, DifferentNanRepresentationsAreEquivalentGreaterDouble) {
  EXPECT_FALSE(NanFirstGreater()(0.0, Neg(0.0)));
  EXPECT_FALSE(NanFirstGreater()(Neg(0.0), 0.0));
}

TEST(NanFirstTest, DifferentZeroRepresentationsAreEquivalentLessFloat) {
  EXPECT_FALSE(NanFirstLess()(0.0f, Neg(0.0f)));
  EXPECT_FALSE(NanFirstLess()(Neg(0.0f), 0.0f));
}

TEST(NanFirstTest, DifferentNanRepresentationsAreEquivalentGreaterFloat) {
  EXPECT_FALSE(NanFirstGreater()(0.0f, Neg(0.0f)));
  EXPECT_FALSE(NanFirstGreater()(Neg(0.0f), 0.0f));
}

TEST(NanFirstTest, DifferentNanRepresentationsAreEquivalentDouble) {
  static_assert(std::numeric_limits<double>::is_iec559);
  static_assert(std::numeric_limits<double>::has_quiet_NaN);
  uint64_t qnan = absl::bit_cast<uint64_t>(kNan);
  for (uint64_t ii = 0; ii < 52; ++ii) {
    double n1 = absl::bit_cast<double>(qnan | (1ul << ii));
    for (uint64_t jj = 0; jj < 52; ++jj) {
      double n2 = absl::bit_cast<double>(qnan | (1ul << jj));
      SCOPED_TRACE(absl::StrCat("n1: ", absl::bit_cast<uint64_t>(n1),
                                " n2: ", absl::bit_cast<uint64_t>(n2)));
      EXPECT_FALSE(NanFirstLess()(n1, n2));
      EXPECT_FALSE(NanFirstGreater()(n1, n2));
    }
  }
}

TEST(NanFirstTest, DifferentNanRepresentationsAreEquivalentFloat) {
  static_assert(std::numeric_limits<float>::is_iec559);
  static_assert(std::numeric_limits<float>::has_quiet_NaN);
  uint32_t qnan = absl::bit_cast<uint32_t>(kNanFloat);
  for (uint32_t ii = 0; ii < 23; ++ii) {
    float n1 = absl::bit_cast<float>(qnan | (1u << ii));
    for (uint32_t jj = 0; jj < 23; ++jj) {
      float n2 = absl::bit_cast<float>(qnan | (1u << jj));
      SCOPED_TRACE(absl::StrCat("n1: ", absl::bit_cast<uint32_t>(n1),
                                " n2: ", absl::bit_cast<uint32_t>(n2)));
      EXPECT_FALSE(NanFirstLess()(n1, n2));
      EXPECT_FALSE(NanFirstGreater()(n1, n2));
    }
  }
}

TEST(NanFirstTest, NanFirstLessProvidesExpectedOrderOnSpecialValuesDouble) {
  static_assert(std::numeric_limits<float>::is_iec559);
  static_assert(std::numeric_limits<double>::is_iec559);
  const std::vector<double> non_decreasing = {
      kNan, -kInf,      -kBig, -kBigFloat, -2.0, -1.0,
      -0.5, -kEpsFloat, -kEps, 0.0,        kEps, kEpsFloat,
      0.5,  1.0,        2.0,   kBigFloat,  kBig, kInf};
  EXPECT_TRUE(absl::c_is_sorted(non_decreasing, NanFirstLess()));
}

TEST(NanFirstTest, NanFirstLessProvidesExpectedOrderOnSpecialValuesFloat) {
  static_assert(std::numeric_limits<float>::is_iec559);
  const std::vector<float> non_decreasing = {
      kNanFloat, -kInfFloat, -kBigFloat, -2.0f, -1.0f, -0.5f,     -kEpsFloat,
      0.0f,      kEpsFloat,  0.5f,       1.0f,  2.0f,  kBigFloat, kInfFloat};
  EXPECT_TRUE(absl::c_is_sorted(non_decreasing, NanFirstLess()));
}

TEST(NanFirstTest, NanFirstGreaterProvidesExpectedOrderOnSpecialValuesDouble) {
  static_assert(std::numeric_limits<float>::is_iec559);
  static_assert(std::numeric_limits<double>::is_iec559);
  const std::vector<double> non_increasing = {
      kNan, kInf,      kBig, kBigFloat,  2.0,   1.0,
      0.5,  kEpsFloat, kEps, 0.0,        -kEps, -kEpsFloat,
      -0.5, -1.0,      -2.0, -kBigFloat, -kBig, -kInf};
  EXPECT_TRUE(absl::c_is_sorted(non_increasing, NanFirstGreater()));
}

TEST(NanFirstTest, NanFirstGreaterProvidesExpectedOrderOnSpecialValuesFloat) {
  static_assert(std::numeric_limits<float>::is_iec559);
  const std::vector<float> non_increasing = {
      kNanFloat, kInfFloat,  kBigFloat, 2.0f,  1.0f,  0.5f,       kEpsFloat,
      0.0f,      -kEpsFloat, -0.5f,     -1.0f, -2.0f, -kBigFloat, -kInfFloat};
  EXPECT_TRUE(absl::c_is_sorted(non_increasing, NanFirstGreater()));
}

template <typename NanFirst, typename T>
void NanFirstCompareAndCallOpAreTheSame(T a, T b) {
  NanFirst cmp;
  ASSERT_FALSE(cmp(a, b) && cmp(b, a));
  EXPECT_EQ(cmp.Compare(a, b) < 0, cmp(a, b));
  EXPECT_EQ(cmp.Compare(b, a) > 0, cmp(a, b));
  EXPECT_EQ(cmp.Compare(a, b) == 0, !cmp(a, b) && !cmp(b, a));
  EXPECT_EQ(cmp.Compare(b, a) == 0, !cmp(a, b) && !cmp(b, a));
  EXPECT_EQ(cmp.Compare(a, b) > 0, cmp(b, a));
  EXPECT_EQ(cmp.Compare(b, a) < 0, cmp(b, a));
}

TEST(NanFirstTest, NanFirstCompareAndCallOpAreTheSameDouble) {
  const std::vector<double> interesting = {
      Neg(kNan), Neg(0.0), kNan,       -kInf,     -kBig, -kBigFloat, -2.0,
      -1.0,      -0.5,     -kEpsFloat, -kEps,     0.0,   kEps,       kEpsFloat,
      0.5,       1.0,      2.0,        kBigFloat, kBig,  kInf};
  for (auto x : interesting) {
    for (auto y : interesting) {
      SCOPED_TRACE(absl::StrCat("x: ", x, " y: ", y));
      NanFirstCompareAndCallOpAreTheSame<NanFirstLess>(x, y);
      NanFirstCompareAndCallOpAreTheSame<NanFirstGreater>(x, y);
    }
  }
}

TEST(NanFirstTest, NanFirstCompareAndCallOpAreTheSameFloat) {
  const std::vector<float> interesting = {
      Neg(kNanFloat), Neg(0.0f), kNanFloat,  -kInfFloat, -kBigFloat, -2.0f,
      -1.0f,          -0.5f,     -kEpsFloat, 0.0f,       kEpsFloat,  0.5f,
      1.0f,           2.0f,      kBigFloat,  kInfFloat};
  for (auto x : interesting) {
    for (auto y : interesting) {
      SCOPED_TRACE(absl::StrCat("x: ", x, " y: ", y));
      NanFirstCompareAndCallOpAreTheSame<NanFirstLess>(x, y);
      NanFirstCompareAndCallOpAreTheSame<NanFirstGreater>(x, y);
    }
  }
}

TEST(VariousComparatorsTest, Constexpr) {
  struct S {
    int a;
    int b;
    std::pair<int, int> c;
    std::vector<int> d;
    int get_b() const { return b; }
  };
  constexpr auto cmp = ChainComparators(
      OrderBy(&S::a), OrderBy(&S::get_b), OrderBy(&S::c, OrderByFirst()),
      OrderBy(&S::d, gtl::LexicographicalComparator()));
  EXPECT_TRUE(cmp(S{1, 2, {3, 4}, {1, 2, 3}}, S{4, 3, {2, 1}, {1, 2, 4}}));
}

TEST(VariousComparatorTest, MultiplyNested) {
  std::array<std::array<int, 3>, 3> a{}, b{};
  int n = 0;
  auto cmp1 = [&n](int x, int y) {
    ++n;
    return x < y;
  };
  auto cmp2 = gtl::LexicographicalComparator(cmp1);
  auto cmp3 = ChainComparators(cmp2, cmp2, cmp2);
  auto cmp4 = gtl::LexicographicalComparator(cmp3);
  auto cmp5 = ChainComparators(cmp4, cmp4, cmp4);
  EXPECT_FALSE(cmp5(a, b));
  EXPECT_EQ(n, 3 * 3 * 3 * 3 * 2);
  n = 0;
  EXPECT_EQ(cmp5.Compare(a, b), 0);
  EXPECT_EQ(n, 3 * 3 * 3 * 3 * 2);
}

TEST(VariousComparatorTest, MultiplyNestedWithCustom3WayCompare) {
  struct CanaryCmp {
    int* n_2way;
    int* n_3way;

    bool operator()(int x, int y) const {
      ++(*n_2way);
      return x < y;
    }
    int Compare(int x, int y) const {
      ++(*n_3way);
      return x - y;
    }
  };

  std::array<std::array<int, 3>, 3> a{}, b{};
  int n_2way = 0;
  int n_3way = 0;
  CanaryCmp cmp1{&n_2way, &n_3way};
  auto cmp2 = gtl::LexicographicalComparator(cmp1);
  auto cmp3 = ChainComparators(cmp2, cmp2, cmp2);
  auto cmp4 = gtl::LexicographicalComparator(cmp3);
  auto cmp5 = ChainComparators(cmp4, cmp4, cmp4);
  EXPECT_FALSE(cmp5(a, b));
  EXPECT_EQ(n_2way, 0);
  EXPECT_EQ(n_3way, 3 * 3 * 3 * 3);
  n_2way = n_3way = 0;
  EXPECT_EQ(cmp5.Compare(a, b), 0);
  EXPECT_EQ(n_2way, 0);
  EXPECT_EQ(n_3way, 3 * 3 * 3 * 3);
}

TEST(VariousComparatorTest, DefaultConstructible) {
  struct SimpleExtractor {
    int operator()(int) const { return 0; }
  };
  struct SimpleComparator {
    bool operator()(int, int) const { return false; }
  };

  auto cmp1 = OrderBy(SimpleExtractor(), SimpleComparator());
  static_assert(std::is_default_constructible_v<decltype(cmp1)>);
  auto cmp2 = ChainComparators(OrderBy(SimpleExtractor(), SimpleComparator()));
  static_assert(std::is_default_constructible_v<decltype(cmp2)>);
  auto cmp3 =
      LexicographicalComparator(OrderBy(SimpleExtractor(), SimpleComparator()));
  static_assert(std::is_default_constructible_v<decltype(cmp3)>);

  auto cmp4 = Reverse(SimpleComparator());
  static_assert(std::is_default_constructible_v<decltype(cmp4)>);

  auto cmp5 = LexicographicalComparator(ChainComparators(
      LexicographicalComparator(Reverse(OrderBy(SimpleExtractor())))));
  static_assert(std::is_default_constructible_v<decltype(cmp5)>);
}

TEST(VariousComparatorTest, NotDefaultConstructible) {
  struct SimpleExtractor {
    int operator()(int) const { return 0; }
  };
  struct StatefulExtractor {
    explicit StatefulExtractor(int) {}
    int operator()(int) const { return 0; }
  };
  struct StatefulComparator {
    explicit StatefulComparator(int) {}
    bool operator()(int, int) const { return false; }
  };

  auto cmp1 = OrderBy(StatefulExtractor(0));
  static_assert(!std::is_default_constructible_v<decltype(cmp1)>);
  auto cmp2 = OrderBy(SimpleExtractor(), StatefulComparator(0));
  static_assert(!std::is_default_constructible_v<decltype(cmp2)>);
  auto cmp3 = ChainComparators(OrderBy(StatefulExtractor(0)));
  static_assert(!std::is_default_constructible_v<decltype(cmp3)>);
  auto cmp4 = LexicographicalComparator(OrderBy(StatefulExtractor(0)));
  static_assert(!std::is_default_constructible_v<decltype(cmp4)>);
  auto cmp5 = Reverse(OrderBy(StatefulExtractor(0)));
  static_assert(!std::is_default_constructible_v<decltype(cmp5)>);
}

// Benchmarks the sorting of a random vector.
template <typename ItemType, typename ItemGenerator, typename ItemComparator>
void BM_Sort(benchmark::State& state, ItemGenerator generator,
             ItemComparator cmp) {
  constexpr int kNumItems = 10000;
  std::vector<ItemType> v(kNumItems);
  std::generate(v.begin(), v.end(), generator);
  std::vector<ItemType> v_copy = v;
  for (auto _ : state) {
    std::sort(v.begin(), v.end(), cmp);
    v = v_copy;
  }
}

// Benchmarks the sorting of a random vector using ChainComparators.
template <typename ElementType, typename ElementGenerator>
void BM_ChainComparatorsSort(benchmark::State& state,
                             ElementGenerator generator) {
  struct S {
    ElementType a, b, c;
  };
  auto gen = [&] { return S{generator(), generator(), generator()}; };
  auto cmp = ChainComparators(OrderBy(&S::a), OrderBy(&S::b), OrderBy(&S::c));
  BM_Sort<S>(state, gen, cmp);
}

void BM_ChainComparatorsIntSort(benchmark::State& state) {
  constexpr int kNumberRange = 10;
  absl::BitGen rng;
  absl::uniform_int_distribution<int> r(1, kNumberRange);
  BM_ChainComparatorsSort<int>(state, [&] { return r(rng); });
}

void BM_ChainComparatorsStringSort(benchmark::State& state) {
  constexpr int kStringLength = 3;
  constexpr int kCharRange = 2;
  absl::BitGen rng;
  absl::uniform_int_distribution<int> r('a', 'a' + kCharRange - 1);
  BM_ChainComparatorsSort<std::string>(state, [&] {
    std::string s(kStringLength, char{});
    std::generate(s.begin(), s.end(), [&] { return r(rng); });
    return s;
  });
}

// Benchmarks the sorting of a random vector of vectors using
// LexicographicalComparator.
template <typename ElementType, typename ElementGenerator>
void BM_LexicographicalComparatorSort(benchmark::State& state,
                                      ElementGenerator generator) {
  using Item = std::vector<ElementType>;
  constexpr int kNumElements = 3;
  auto gen = [&] {
    Item item(kNumElements);
    std::generate(item.begin(), item.end(), generator);
    return item;
  };
  auto cmp = gtl::LexicographicalComparator();
  BM_Sort<Item>(state, gen, cmp);
}

void BM_LexicographicalComparatorIntSort(benchmark::State& state) {
  constexpr int kNumberRange = 10;
  absl::BitGen rng;
  absl::uniform_int_distribution<int> r(1, kNumberRange);
  BM_LexicographicalComparatorSort<int>(state, [&] { return r(rng); });
}

void BM_LexicographicalComparatorStringSort(benchmark::State& state) {
  constexpr int kStringLength = 3;
  constexpr int kCharRange = 2;
  absl::BitGen rng;
  absl::uniform_int_distribution<int> r('a', 'a' + kCharRange - 1);
  BM_LexicographicalComparatorSort<std::string>(state, [&] {
    std::string s(kStringLength, char{});
    std::generate(s.begin(), s.end(), [&] { return r(rng); });
    return s;
  });
}

BENCHMARK(BM_ChainComparatorsIntSort);
BENCHMARK(BM_ChainComparatorsStringSort);
BENCHMARK(BM_LexicographicalComparatorIntSort);
BENCHMARK(BM_LexicographicalComparatorStringSort);

}  // namespace
