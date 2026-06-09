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

#include "gloop/util/gtl/interval.h"

#include <cstdint>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/hash/hash_testing.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace gtl {
namespace {

class ConstructorListener {
 public:
  ConstructorListener(int* copy_construct_counter, int* move_construct_counter)
      : copy_construct_counter_(copy_construct_counter),
        move_construct_counter_(move_construct_counter) {
    *copy_construct_counter_ = 0;
    *move_construct_counter_ = 0;
  }
  ConstructorListener(const ConstructorListener& other) {
    copy_construct_counter_ = other.copy_construct_counter_;
    move_construct_counter_ = other.move_construct_counter_;
    ++*copy_construct_counter_;
  }
  ConstructorListener(ConstructorListener&& other) {
    copy_construct_counter_ = other.copy_construct_counter_;
    move_construct_counter_ = other.move_construct_counter_;
    ++*move_construct_counter_;
  }
  bool operator<(const ConstructorListener&) { return false; }
  bool operator>(const ConstructorListener&) { return false; }
  bool operator<=(const ConstructorListener&) { return true; }

 private:
  int* copy_construct_counter_;
  int* move_construct_counter_;
};

TEST(IntervalConstructorTest, Move) {
  int object1_copy_count, object1_move_count;
  ConstructorListener object1(&object1_copy_count, &object1_move_count);
  int object2_copy_count, object2_move_count;
  ConstructorListener object2(&object2_copy_count, &object2_move_count);

  Interval<ConstructorListener> interval(object1, std::move(object2));
  EXPECT_EQ(1, object1_copy_count);
  EXPECT_EQ(0, object1_move_count);
  EXPECT_EQ(0, object2_copy_count);
  EXPECT_EQ(1, object2_move_count);
}

TEST(IntervalConstructorTest, ImplicitConversion) {
  struct WrappedInt {
    WrappedInt(int value)  // NOLINT(google-explicit-constructor)
        : value(value) {}
    bool operator<(const WrappedInt& other) const {
      return value < other.value;
    }
    bool operator>(const WrappedInt& other) const {
      return value > other.value;
    }
    bool operator<=(const WrappedInt& other) const {
      return value <= other.value;
    }
    bool operator>=(const WrappedInt& other) const {
      return value >= other.value;
    }
    bool operator==(const WrappedInt& other) const {
      return value == other.value;
    }
    int value;
  };

  static_assert(std::is_convertible_v<int, WrappedInt>, "");
  static_assert(std::is_constructible_v<Interval<WrappedInt>, int, int>, "");

  Interval<WrappedInt> i(10, 20);
  EXPECT_EQ(10, i.start().value);
  EXPECT_EQ(20, i.limit().value);
}

TEST(Interval, ConstructorsCopyAndClear) {
  Interval<int32_t> empty;
  EXPECT_TRUE(empty.empty());

  Interval<int32_t> d2(0, 100);
  EXPECT_EQ(0, d2.start());
  EXPECT_EQ(100, d2.limit());
  EXPECT_EQ(Interval<int32_t>(0, 100), d2);
  EXPECT_NE(Interval<int32_t>(0, 99), d2);

  empty = d2;
  EXPECT_EQ(0, d2.start());
  EXPECT_EQ(100, d2.limit());
  EXPECT_TRUE(empty == d2);
  EXPECT_EQ(empty, d2);
  EXPECT_TRUE(d2 == empty);
  EXPECT_EQ(d2, empty);

  Interval<int32_t> max_less_than_min(40, 20);
  EXPECT_TRUE(max_less_than_min.empty());
  EXPECT_EQ(40, max_less_than_min.start());
  EXPECT_EQ(20, max_less_than_min.limit());

  Interval<int> d3(10, 20);
  d3.clear();
  EXPECT_TRUE(d3.empty());
}

template <typename T, typename U>
bool MakeIntervalReturnsInt(T&& t, U&& u) {
  auto res = MakeInterval(std::forward<T>(t), std::forward<U>(u));
  return std::is_same_v<gtl::Interval<int>, decltype(res)>;
}

TEST(Interval, MakeInterval) {
  static_assert(
      std::is_same_v<gtl::Interval<int>, decltype(MakeInterval(0, 3))>,
      "Type is deduced incorrectly.");
  static_assert(
      std::is_same_v<gtl::Interval<double>, decltype(MakeInterval(0., 3.))>,
      "Type is deduced incorrectly.");

  EXPECT_EQ(MakeInterval(0., 3.), Interval<double>(0, 3));

  // Make sure that all combinations of value categories work.
  int i = 0;
  const int ci = 0;
  auto ri = [&]() -> int&& { return std::move(i); };          // NOLINT
  auto rci = [&]() -> const int&& { return std::move(ci); };  // NOLINT
  EXPECT_TRUE(MakeIntervalReturnsInt(i, i));
  EXPECT_TRUE(MakeIntervalReturnsInt(i, ci));
  EXPECT_TRUE(MakeIntervalReturnsInt(i, ri()));
  EXPECT_TRUE(MakeIntervalReturnsInt(i, rci()));
  EXPECT_TRUE(MakeIntervalReturnsInt(ci, i));
  EXPECT_TRUE(MakeIntervalReturnsInt(ci, ci));
  EXPECT_TRUE(MakeIntervalReturnsInt(ci, ri()));
  EXPECT_TRUE(MakeIntervalReturnsInt(ci, rci()));
  EXPECT_TRUE(MakeIntervalReturnsInt(ri(), i));
  EXPECT_TRUE(MakeIntervalReturnsInt(ri(), ci));
  EXPECT_TRUE(MakeIntervalReturnsInt(ri(), ri()));
  EXPECT_TRUE(MakeIntervalReturnsInt(ri(), rci()));
  EXPECT_TRUE(MakeIntervalReturnsInt(rci(), i));
  EXPECT_TRUE(MakeIntervalReturnsInt(rci(), ci));
  EXPECT_TRUE(MakeIntervalReturnsInt(rci(), ri()));
  EXPECT_TRUE(MakeIntervalReturnsInt(rci(), rci()));
}

TEST(Interval, Accessors) {
  Interval<int32_t> d1(100, 200);

  // set_start:
  d1.set_start(30);
  EXPECT_EQ(30, d1.start());
  EXPECT_EQ(200, d1.limit());

  // set_limit:
  d1.set_limit(220);
  EXPECT_EQ(30, d1.start());
  EXPECT_EQ(220, d1.limit());

  // Set:
  d1.clear();
  d1.Set(30, 220);
  EXPECT_EQ(30, d1.start());
  EXPECT_EQ(220, d1.limit());

  // SpanningUnion:
  Interval<int32_t> d2;
  EXPECT_TRUE(!d1.SpanningUnion(d2));
  EXPECT_EQ(30, d1.start());
  EXPECT_EQ(220, d1.limit());

  EXPECT_TRUE(d2.SpanningUnion(d1));
  EXPECT_EQ(30, d2.start());
  EXPECT_EQ(220, d2.limit());

  d2.set_start(40);
  d2.set_limit(100);
  EXPECT_TRUE(!d1.SpanningUnion(d2));
  EXPECT_EQ(30, d1.start());
  EXPECT_EQ(220, d1.limit());

  d2.set_start(20);
  d2.set_limit(100);
  EXPECT_TRUE(d1.SpanningUnion(d2));
  EXPECT_EQ(20, d1.start());
  EXPECT_EQ(220, d1.limit());

  d2.set_start(50);
  d2.set_limit(300);
  EXPECT_TRUE(d1.SpanningUnion(d2));
  EXPECT_EQ(20, d1.start());
  EXPECT_EQ(300, d1.limit());

  d2.set_start(0);
  d2.set_limit(500);
  EXPECT_TRUE(d1.SpanningUnion(d2));
  EXPECT_EQ(0, d1.start());
  EXPECT_EQ(500, d1.limit());

  d2.set_start(100);
  d2.set_limit(0);
  EXPECT_TRUE(!d1.SpanningUnion(d2));
  EXPECT_EQ(0, d1.start());
  EXPECT_EQ(500, d1.limit());
  EXPECT_TRUE(d2.SpanningUnion(d1));
  EXPECT_EQ(0, d2.start());
  EXPECT_EQ(500, d2.limit());
}

// Test intersection between the two intervals i1 and i2.  Tries
// i1.IntersectWith(i2) and vice versa. The intersection should change i1 iff
// changes_i1 is true, and the same for changes_i2.  The resulting intersection
// should be result.
static void TestIntersect(const Interval<int64_t>& i1,
                          const Interval<int64_t>& i2, bool changes_i1,
                          bool changes_i2, const Interval<int64_t>& result) {
  Interval<int64_t> i;
  i = i1;
  EXPECT_TRUE(i.IntersectWith(i2) == changes_i1 && i == result);
  i = i2;
  EXPECT_TRUE(i.IntersectWith(i1) == changes_i2 && i == result);
}

TEST(Interval, CoveringOps) {
  const Interval<int64_t> empty;
  const Interval<int64_t> d(100, 200);
  const Interval<int64_t> d1(0, 50);
  const Interval<int64_t> d2(50, 110);
  const Interval<int64_t> d3(110, 180);
  const Interval<int64_t> d4(180, 220);
  const Interval<int64_t> d5(220, 300);
  const Interval<int64_t> d6(100, 150);
  const Interval<int64_t> d7(150, 200);
  const Interval<int64_t> d8(0, 300);

  // Intersection:
  EXPECT_TRUE(d.Intersects(d));
  EXPECT_TRUE(!empty.Intersects(d) && !d.Intersects(empty));
  EXPECT_TRUE(!d.Intersects(d1) && !d1.Intersects(d));
  EXPECT_TRUE(d.Intersects(d2) && d2.Intersects(d));
  EXPECT_TRUE(d.Intersects(d3) && d3.Intersects(d));
  EXPECT_TRUE(d.Intersects(d4) && d4.Intersects(d));
  EXPECT_TRUE(!d.Intersects(d5) && !d5.Intersects(d));
  EXPECT_TRUE(d.Intersects(d6) && d6.Intersects(d));
  EXPECT_TRUE(d.Intersects(d7) && d7.Intersects(d));
  EXPECT_TRUE(d.Intersects(d8) && d8.Intersects(d));

  Interval<int64_t> i;
  EXPECT_TRUE(d.Intersects(d, &i) && d == i);
  EXPECT_TRUE(!empty.Intersects(d, nullptr) && !d.Intersects(empty, nullptr));
  EXPECT_TRUE(!d.Intersects(d1, nullptr) && !d1.Intersects(d, nullptr));
  EXPECT_TRUE(d.Intersects(d2, &i) && i == Interval<int64_t>(100, 110));
  EXPECT_TRUE(d2.Intersects(d, &i) && i == Interval<int64_t>(100, 110));
  EXPECT_TRUE(d.Intersects(d3, &i) && i == d3);
  EXPECT_TRUE(d3.Intersects(d, &i) && i == d3);
  EXPECT_TRUE(d.Intersects(d4, &i) && i == Interval<int64_t>(180, 200));
  EXPECT_TRUE(d4.Intersects(d, &i) && i == Interval<int64_t>(180, 200));
  EXPECT_TRUE(!d.Intersects(d5, nullptr) && !d5.Intersects(d, nullptr));
  EXPECT_TRUE(d.Intersects(d6, &i) && i == d6);
  EXPECT_TRUE(d6.Intersects(d, &i) && i == d6);
  EXPECT_TRUE(d.Intersects(d7, &i) && i == d7);
  EXPECT_TRUE(d7.Intersects(d, &i) && i == d7);
  EXPECT_TRUE(d.Intersects(d8, &i) && i == d);
  EXPECT_TRUE(d8.Intersects(d, &i) && i == d);

  // Test IntersectsWith().
  // Arguments are TestIntersect(i1, i2, changes_i1, changes_i2, result).
  TestIntersect(empty, d, false, true, empty);
  TestIntersect(d, d1, true, true, empty);
  TestIntersect(d1, d2, true, true, empty);
  TestIntersect(d, d2, true, true, Interval<int64_t>(100, 110));
  TestIntersect(d8, d, true, false, d);
  TestIntersect(d8, d1, true, false, d1);
  TestIntersect(d8, d5, true, false, d5);

  // Contains:
  EXPECT_TRUE(!empty.contains(d) && !d.contains(empty));
  EXPECT_TRUE(d.contains(d));
  EXPECT_TRUE(!d.contains(d1) && !d1.contains(d));
  EXPECT_TRUE(!d.contains(d2) && !d2.contains(d));
  EXPECT_TRUE(d.contains(d3) && !d3.contains(d));
  EXPECT_TRUE(!d.contains(d4) && !d4.contains(d));
  EXPECT_TRUE(!d.contains(d5) && !d5.contains(d));
  EXPECT_TRUE(d.contains(d6) && !d6.contains(d));
  EXPECT_TRUE(d.contains(d7) && !d7.contains(d));
  EXPECT_TRUE(!d.contains(d8) && d8.contains(d));

  EXPECT_TRUE(d.contains(100));
  EXPECT_TRUE(!d.contains(200));
  EXPECT_TRUE(d.contains(150));
  EXPECT_TRUE(!d.contains(99));
  EXPECT_TRUE(!d.contains(201));

  Interval<int64_t> lo;
  Interval<int64_t> hi;

  EXPECT_TRUE(d.Difference(d2, lo, hi));
  EXPECT_TRUE(lo.empty());
  EXPECT_EQ(110, hi.start());
  EXPECT_EQ(200, hi.limit());

  EXPECT_TRUE(d.Difference(d3, lo, hi));
  EXPECT_EQ(100, lo.start());
  EXPECT_EQ(110, lo.limit());
  EXPECT_EQ(180, hi.start());
  EXPECT_EQ(200, hi.limit());

  EXPECT_TRUE(d.Difference(d4, lo, hi));
  EXPECT_EQ(100, lo.start());
  EXPECT_EQ(180, lo.limit());
  EXPECT_TRUE(hi.empty());

  EXPECT_FALSE(d.Difference(d5, lo, hi));
  EXPECT_EQ(100, lo.start());
  EXPECT_EQ(200, lo.limit());
  EXPECT_TRUE(hi.empty());

  EXPECT_TRUE(d.Difference(d6, lo, hi));
  EXPECT_TRUE(lo.empty());
  EXPECT_EQ(150, hi.start());
  EXPECT_EQ(200, hi.limit());

  EXPECT_TRUE(d.Difference(d7, lo, hi));
  EXPECT_EQ(100, lo.start());
  EXPECT_EQ(150, lo.limit());
  EXPECT_TRUE(hi.empty());

  EXPECT_TRUE(d.Difference(d8, lo, hi));
  EXPECT_TRUE(lo.empty());
  EXPECT_TRUE(hi.empty());
}

TEST(Interval, Length) {
  const Interval<int> empty1;
  const Interval<int> empty2(1, 1);
  const Interval<int> empty3(1, 0);
  const Interval<absl::Time> empty4(absl::UnixEpoch() + absl::Seconds(1),
                                    absl::UnixEpoch());
  const Interval<int> d1(1, 2);
  const Interval<int> d2(0, 50);
  const Interval<absl::Time> d3(absl::UnixEpoch(),
                                absl::UnixEpoch() + absl::Seconds(1));
  const Interval<absl::Time> d4(absl::UnixEpoch() + absl::Hours(1),
                                absl::UnixEpoch() + absl::Minutes(90));

  EXPECT_EQ(0, empty1.length());
  EXPECT_EQ(0, empty2.length());
  EXPECT_EQ(0, empty3.length());
  EXPECT_EQ(absl::ZeroDuration(), empty4.length());
  EXPECT_EQ(1, d1.length());
  EXPECT_EQ(50, d2.length());
  EXPECT_EQ(absl::Seconds(1), d3.length());
  EXPECT_EQ(absl::Minutes(30), d4.length());
}

TEST(Interval, IntervalOfTypeWithNoOperatorMinus) {
  // Interval<T> should work even if T does not support operator-().  We just
  // can't call Interval<T>::Length() for such types.
  const Interval<std::string> d1("a", "b");
  const Interval<std::pair<int, int>> d2({1, 2}, {4, 3});
  EXPECT_EQ("a", d1.start());
  EXPECT_EQ("b", d1.limit());
  EXPECT_EQ(std::make_pair(1, 2), d2.start());
  EXPECT_EQ(std::make_pair(4, 3), d2.limit());
}

TEST(Interval, Split) {
  double double_inf = std::numeric_limits<double>::infinity();
  absl::Time time_zero = absl::UnixEpoch();

  const Interval<int> empty_i(1, 0);
  const Interval<double> inf_i(-double_inf, double_inf);
  const Interval<absl::Time> finite_i(time_zero, time_zero + absl::Hours(1));

  EXPECT_THAT(empty_i.Split([](const auto& s) { return s + 1; }),
              testing::ElementsAre());

  EXPECT_THAT(inf_i.Split([&](const auto& s) {
    return std::isinf(s) ? 0.0 : double_inf;
  }),
              testing::ElementsAre(MakeInterval(-double_inf, 0.0),
                                   MakeInterval(0.0, double_inf)));

  EXPECT_THAT(
      finite_i.Split([&](const auto& s) { return s + absl::Minutes(25); }),
      testing::ElementsAreArray(std::vector<Interval<absl::Time>>{
          {time_zero, time_zero + absl::Minutes(25)},
          {time_zero + absl::Minutes(25), time_zero + absl::Minutes(50)},
          {time_zero + absl::Minutes(50), time_zero + absl::Minutes(60)}}));

#ifndef NDEBUG
  // The assert that makes this crash only works in debug mode.
  EXPECT_DEATH(
      { finite_i.Split([](const auto& s) { return s; }); },
      testing::HasSubstr("si_start < si_limit"));
#endif
}

struct NoEquals {
  NoEquals(int v) : value(v) {}  // NOLINT
  int value;
  bool operator<(const NoEquals& other) const { return value < other.value; }
};

TEST(Interval, OrderedComparisonForTypeWithoutEquals) {
  const Interval<NoEquals> d1(0, 4);
  const Interval<NoEquals> d2(0, 3);
  const Interval<NoEquals> d3(1, 4);
  const Interval<NoEquals> d4(1, 5);
  const Interval<NoEquals> d6(0, 4);
  EXPECT_TRUE(d1 < d2);
  EXPECT_TRUE(d1 < d3);
  EXPECT_TRUE(d1 < d4);
  EXPECT_FALSE(d1 < d6);
}

TEST(Interval, HashesCorrectly) {
  using I = Interval<int>;
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      {I(1, -1), I(1, 1), I(0, 0), I(-2, 2), I(3, 9), I(9, 3), I(3, 10)}));
}

TEST(Interval, OutputReturnsOstreamRef) {
  std::stringstream ss;
  const Interval<int> v(1, 2);
  // If (ss << v) were to return a value, it wouldn't match the signature of
  // return_type_is_a_ref() function.
  auto return_type_is_a_ref = [](std::ostream&) {};
  return_type_is_a_ref(ss << v);
}

struct NotOstreamable {
  bool operator<(const NotOstreamable& /*other*/) const { return false; }
  bool operator>=(const NotOstreamable& /*other*/) const { return true; }
  bool operator==(const NotOstreamable& /*other*/) const { return true; }
};

TEST(Interval, IntervalOfTypeWithNoOstreamSupport) {
  const NotOstreamable v;
  const Interval<NotOstreamable> d(v, v);
  // EXPECT_EQ builds a string representation of d. If d::operator<<() would be
  // defined then this test would not compile because NotOstreamable objects
  // lack the operator<<() support.
  EXPECT_EQ(d, d);
}

struct Stringifiable {
  explicit Stringifiable(int v) : value(v) {}

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const Stringifiable& s) {
    absl::Format(&sink, "%v", s.value);
  }

  auto operator<=>(const Stringifiable&) const = default;

  int value;
};

TEST(Interval, AbslStringify) {
  const Interval<Stringifiable> v(Stringifiable(1), Stringifiable(2));
  EXPECT_EQ(absl::StrCat(v), "[1, 2)");
}

TEST(Interval, StructuredBindings) {
  Interval<int> x(1, 2);
  auto [a, b] = x;
  EXPECT_EQ(a, 1);
  EXPECT_EQ(b, 2);
  auto [c, d] = Interval<int>(1, 2);
  EXPECT_EQ(c, 1);
  EXPECT_EQ(d, 2);
  auto& [f, g] = x;
  f += 1;
  g += 2;
  EXPECT_EQ(x.start(), 2);
  EXPECT_EQ(x.limit(), 4);
}

TEST(Interval, ClassTemplateArgumentDeduction) {
  // From prvalue
  {
    const Interval interval = {17, 19};
    static_assert(std::is_same_v<decltype(interval), const Interval<int>>);

    EXPECT_EQ(17, interval.start());
    EXPECT_EQ(19, interval.limit());
  }

  // From xvalue
  {
    int a = 17;
    int b = 19;

    const Interval interval = {std::move(a), std::move(b)};
    static_assert(std::is_same_v<decltype(interval), const Interval<int>>);

    EXPECT_EQ(17, interval.start());
    EXPECT_EQ(19, interval.limit());
  }

  // From lvalue
  {
    const int a = 17;
    const int b = 19;

    const Interval interval = {a, b};
    static_assert(std::is_same_v<decltype(interval), const Interval<int>>);

    EXPECT_EQ(17, interval.start());
    EXPECT_EQ(19, interval.limit());
  }

  // From mix of value categories
  {
    const int a = 17;
    const Interval interval = {a, 19};
    static_assert(std::is_same_v<decltype(interval), const Interval<int>>);

    EXPECT_EQ(17, interval.start());
    EXPECT_EQ(19, interval.limit());
  }
}

////////////////////////////////////////////////////////////////////////
// Static assertion tests
////////////////////////////////////////////////////////////////////////

TEST(IntervalStaticAssertTest, Constexprs) {
  constexpr Interval<int> x(1, 10);
  static_assert(x.start() == 1);
  static_assert(x.limit() == 10);
  static_assert(!x.empty());
  static_assert(x.length() == 9);
  static_assert(x.contains(4));

  constexpr Interval<int> y(5, 20);
  static_assert(x.Intersects(y));

  constexpr Interval<int> x_copy(1, 10);
  static_assert(x == x_copy);
  static_assert(x != y);
  static_assert(x < y);
  static_assert(x.get<0>() == 1);
  static_assert(x.get<1>() == 10);
  // Make sure the const-rvalue overload works in a constexpr context too.
  static_assert(std::move(x).get<0>() == 1);
}

////////////////////////////////////////////////////////////////////////
// Heterogeneous access tests
////////////////////////////////////////////////////////////////////////

TEST(IntervalHeterogeneousTest, Heterogeneous) {
  const Interval<std::string> string_interval("a", "b");
  EXPECT_TRUE(string_interval.contains(absl::string_view("aa")));
  EXPECT_FALSE(string_interval.contains(absl::string_view("ba")));
  EXPECT_TRUE(string_interval.contains(absl::Cord("aa")));
  EXPECT_FALSE(string_interval.contains(absl::Cord("ba")));
  EXPECT_TRUE(string_interval.contains("aa"));
  EXPECT_FALSE(string_interval.contains("ba"));

  const Interval<absl::Cord> cord_interval(absl::Cord("a"), absl::Cord("b"));
  EXPECT_TRUE(cord_interval.contains(absl::string_view("aa")));
  EXPECT_FALSE(cord_interval.contains(absl::string_view("ba")));
  EXPECT_TRUE(cord_interval.contains(std::string("aa")));
  EXPECT_FALSE(cord_interval.contains(std::string("ba")));
  EXPECT_TRUE(cord_interval.contains("aa"));
  EXPECT_FALSE(cord_interval.contains("ba"));

  const Interval<std::string> string_view_interval("a", "b");

  EXPECT_TRUE(string_interval == cord_interval);
  EXPECT_TRUE(cord_interval == string_interval);
  EXPECT_TRUE(string_interval == string_view_interval);
  EXPECT_TRUE(cord_interval == string_view_interval);
  EXPECT_TRUE(string_view_interval == string_view_interval);

  EXPECT_FALSE(string_interval != cord_interval);
  EXPECT_FALSE(cord_interval != string_interval);
  EXPECT_FALSE(string_interval != string_view_interval);
  EXPECT_FALSE(cord_interval != string_view_interval);
  EXPECT_FALSE(string_view_interval != string_view_interval);

  EXPECT_FALSE(string_interval < cord_interval);
  EXPECT_FALSE(cord_interval < string_interval);
  EXPECT_FALSE(string_interval < cord_interval);
  EXPECT_FALSE(cord_interval < string_interval);
  EXPECT_FALSE(string_interval < string_view_interval);
  EXPECT_FALSE(cord_interval < string_view_interval);
  EXPECT_FALSE(string_view_interval < string_view_interval);
}

}  // unnamed namespace
}  // namespace gtl
