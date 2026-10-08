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

#include "gloop/util/tuple/matchers.h"

#include <array>
#include <ostream>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/base/casts.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/struct.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace testing {
namespace {

using ::std::array;
using ::std::make_tuple;
using ::std::pair;
using ::std::string;
using ::std::tuple;
using ::std::vector;
using ::testing::_;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::Le;
using ::testing::Matcher;
using ::testing::Not;
using ::testing::Pointwise;
using ::testing::StartsWith;
using ::testing::UnorderedPointwise;

template <class T>
class ExplainingEq : public ::testing::MatcherInterface<T> {
 public:
  explicit ExplainingEq(const T& rhs) : rhs_(rhs) {}

  virtual ~ExplainingEq() {}

  virtual void DescribeTo(::std::ostream* os) const {
    *os << "is equal to " << rhs_;
  }

  virtual bool MatchAndExplain(T lhs,
                               ::testing::MatchResultListener* listener) const {
    *listener << "which is equal to " << lhs;
    return lhs == rhs_;
  }

 private:
  T rhs_;
};

template <class T>
Matcher<T> ExEq(const T& n) {
  return ::testing::MakeMatcher(new ExplainingEq<T>(n));
}

// Polymorphic equality matcher which explains matching a tuple of values.
template <typename TupleT>
class ExplainingEqPair : public ::testing::MatcherInterface<TupleT> {
 public:
  virtual void DescribeTo(::std::ostream* os) const {
    *os << " are an equal pair";
  }

  virtual bool MatchAndExplain(TupleT arg,
                               ::testing::MatchResultListener* listener) const {
    auto lhs = get<0>(arg);
    auto rhs = get<1>(arg);

    const bool match = (lhs == rhs);

    if (match) {
      *listener << lhs << " is equal to " << rhs;
      return true;
    } else {
      *listener << lhs << " is not equal to " << rhs;
      return false;
    }
  }
};

class ExEqPair {
 public:
  template <typename T1, typename T2>
  operator Matcher<tuple<T1, T2>>() const {
    return MakeMatcher(new ExplainingEqPair<tuple<T1, T2>>);
  }
  template <typename T1, typename T2>
  operator Matcher<const tuple<T1, T2>&>() const {
    return MakeMatcher(new ExplainingEqPair<const tuple<T1, T2>&>);
  }
};

inline ExEqPair ExEq() { return ExEqPair(); }

// A series of (inner) matchers for testing the self-description propagation.
MATCHER(DescEq, std::string(negation ? "aren't" : "are") + " an equal pair") {
  return std::get<0>(arg) == std::get<1>(arg);
}

MATCHER(DescLe, std::string(negation ? "aren't" : "are") +
                    " a pair where the first <= "
                    "the second") {
  return std::get<0>(arg) <= std::get<1>(arg);
}

MATCHER(DescGe, std::string(negation ? "aren't" : "are") +
                    " a pair where the first >= "
                    "the second") {
  return std::get<0>(arg) >= std::get<1>(arg);
}

struct Person {
  TUPLE_DEFINE_STRUCT(Person, (), (std::string, name), (int, age));
};

struct PersonID {
  TUPLE_DEFINE_STRUCT(PersonID, (), (::std::string, name), (int, age),
                      (int, id));
};

struct Passport {
  TUPLE_DEFINE_STRUCT(Passport, (), (Person, person));
};

struct NonTupleStruct {
  int x;
  double y;
  int z;
};

template <class T>
std::string Describe(const Matcher<T>& m) {
  ::std::stringstream ss;
  m.DescribeTo(&ss);
  return ss.str();
}

template <class T>
std::string DescribeNegation(const Matcher<T>& m) {
  ::std::stringstream ss;
  m.DescribeNegationTo(&ss);
  return ss.str();
}

template <class M, class T>
std::string Explain(const M& m, const T& v) {
  ::testing::StringMatchResultListener listener;
  ::testing::ExplainMatchResult(m, v, &listener);
  return listener.str();
}

template <class To, class From>
void CastToMatcher(const From& from) {
  (void)::absl::implicit_cast<Matcher<const To&>>(from);
}

// Hierarchy of CastToMatcher variants to test all possible combinations of
// const, const-ref, etc. variants for the tuple and tuple elements.
template <class To, class From>
void CastToMatcherTypes(const From& from) {
  CastToMatcher<To>(from);
  CastToMatcher<const To>(from);
  CastToMatcher<To&>(from);
  CastToMatcher<const To&>(from);
}

template <class To, class From>
void CastToMatcherExpectedTypes(const From& from) {
  using Actual = typename element<0, To>::type;
  using Expected = typename element<1, To>::type;

  CastToMatcherTypes<tuple<Actual, Expected>>(from);
  CastToMatcherTypes<tuple<Actual, Expected&>>(from);
  CastToMatcherTypes<tuple<Actual, const Expected>>(from);
  CastToMatcherTypes<tuple<Actual, const Expected&>>(from);
}

template <class To, class From>
void CastToMatcherPairTypes(const From& from) {
  using Actual = typename element<0, To>::type;
  using Expected = typename element<1, To>::type;

  CastToMatcherExpectedTypes<tuple<Actual, Expected>>(from);
  CastToMatcherExpectedTypes<tuple<Actual&, Expected>>(from);
  CastToMatcherExpectedTypes<tuple<const Actual, Expected>>(from);
  CastToMatcherExpectedTypes<tuple<const Actual&, Expected>>(from);
}

TEST(Matchers, ArrayTypecasts) {
  CastToMatcher<array<int, 0>>(Tuple());
  CastToMatcher<array<int, 0>&>(Tuple());
  CastToMatcher<const array<int, 0>>(Tuple());
  CastToMatcher<const array<int, 0>&>(Tuple());

  CastToMatcher<array<int, 1>>(Tuple(1));
  CastToMatcher<array<int, 1>&>(Tuple(1));
  CastToMatcher<const array<int, 1>>(Tuple(1));
  CastToMatcher<const array<int, 1>&>(Tuple(1));

  CastToMatcher<array<int, 1>>(Tuple(Eq(1)));
  CastToMatcher<array<int, 1>&>(Tuple(Eq(1)));
  CastToMatcher<const array<int, 1>>(Tuple(Eq(1)));
  CastToMatcher<const array<int, 1>&>(Tuple(Eq(1)));

  CastToMatcher<array<int, 2>>(Tuple(Eq(1), 2));
  CastToMatcher<array<int, 2>&>(Tuple(Eq(1), 2));
  CastToMatcher<const array<int, 2>>(Tuple(Eq(1), 2));
  CastToMatcher<const array<int, 2>&>(Tuple(Eq(1), 2));
}

TEST(Matchers, TupleTypecasts) {
  CastToMatcher<tuple<>>(Tuple());
  CastToMatcher<tuple<>&>(Tuple());
  CastToMatcher<const tuple<>>(Tuple());
  CastToMatcher<const tuple<>&>(Tuple());

  CastToMatcher<tuple<int>>(Tuple(1));
  CastToMatcher<tuple<int>&>(Tuple(1));
  CastToMatcher<const tuple<int>>(Tuple(1));
  CastToMatcher<const tuple<int>&>(Tuple(1));

  CastToMatcher<tuple<int>>(Tuple(Eq(1)));
  CastToMatcher<tuple<int>&>(Tuple(Eq(1)));
  CastToMatcher<const tuple<int>>(Tuple(Eq(1)));
  CastToMatcher<const tuple<int>&>(Tuple(Eq(1)));

  CastToMatcher<tuple<int, std::string>>(Tuple(Eq(1), "a"));
  CastToMatcher<tuple<int, std::string>&>(Tuple(Eq(1), "a"));
  CastToMatcher<const tuple<int, std::string>>(Tuple(Eq(1), "a"));
  CastToMatcher<const tuple<int, std::string>&>(Tuple(Eq(1), "a"));
}

TEST(Matchers, PairTypecasts) {
  CastToMatcher<pair<int, std::string>>(Tuple(Eq(1), "a"));
  CastToMatcher<pair<int, std::string>&>(Tuple(Eq(1), "a"));
  CastToMatcher<const pair<int, std::string>>(Tuple(Eq(1), "a"));
  CastToMatcher<const pair<int, std::string>&>(Tuple(Eq(1), "a"));
}

TEST(Matchers, StructTypecasts) {
  CastToMatcher<Person>(Tuple("a", Eq(1)));
  CastToMatcher<Person&>(Tuple("a", Eq(1)));
  CastToMatcher<const Person>(Tuple("a", Eq(1)));
  CastToMatcher<const Person&>(Tuple("a", Eq(1)));
}

TEST(Matchers, NonTupleStructTypecasts) {
  CastToMatcher<NonTupleStruct>(Tuple(Eq(0), Eq(1.0), Eq(2)));
  CastToMatcher<NonTupleStruct&>(Tuple(Eq(0), Eq(1.0), Eq(2)));
  CastToMatcher<const NonTupleStruct>(Tuple(Eq(0), Eq(1.0), Eq(2)));
  CastToMatcher<const NonTupleStruct&>(Tuple(Eq(0), Eq(1.0), Eq(2)));
}

TEST(Matchers, DescriptionArray) {
  {
    Matcher<const array<int, 0>&> matcher = Tuple();
    EXPECT_EQ("empty tuple", Describe(matcher));
    EXPECT_EQ("not empty tuple", DescribeNegation(matcher));
  }
  {
    Matcher<const array<int, 1>&> matcher = Tuple(1);
    EXPECT_EQ("has [0] field that is equal to 1", Describe(matcher));
    EXPECT_EQ("has [0] field that isn't equal to 1", DescribeNegation(matcher));
  }
  {
    Matcher<const array<int, 2>&> matcher = Tuple(1, 2);
    EXPECT_EQ(
        "has [0] field that is equal to 1"
        ", and has [1] field that is equal to 2",
        Describe(matcher));
    EXPECT_EQ(
        "has [0] field that isn't equal to 1"
        ", or has [1] field that isn't equal to 2",
        DescribeNegation(matcher));
  }
  {
    Matcher<const array<int, 3>&> matcher = Tuple(1, 2, 3);
    EXPECT_EQ(
        "has [0] field that is equal to 1"
        ", and has [1] field that is equal to 2"
        ", and has [2] field that is equal to 3",
        Describe(matcher));
    EXPECT_EQ(
        "has [0] field that isn't equal to 1"
        ", or has [1] field that isn't equal to 2"
        ", or has [2] field that isn't equal to 3",
        DescribeNegation(matcher));
  }
}

TEST(Matchers, DescriptionTuple) {
  {
    Matcher<const tuple<>&> matcher = Tuple();
    EXPECT_EQ("empty tuple", Describe(matcher));
    EXPECT_EQ("not empty tuple", DescribeNegation(matcher));
  }
  {
    Matcher<const tuple<int>&> matcher = Tuple(1);
    EXPECT_EQ("has [0] field that is equal to 1", Describe(matcher));
    EXPECT_EQ("has [0] field that isn't equal to 1", DescribeNegation(matcher));
  }
  {
    Matcher<const tuple<int, std::string>&> matcher = Tuple(1, "foo");
    EXPECT_EQ(
        "has [0] field that is equal to 1"
        ", and has [1] field that is equal to \"foo\"",
        Describe(matcher));
    EXPECT_EQ(
        "has [0] field that isn't equal to 1"
        ", or has [1] field that isn't equal to \"foo\"",
        DescribeNegation(matcher));
  }
  {
    Matcher<const tuple<int, std::string, char>&> matcher =
        Tuple(1, "foo", 'c');
    EXPECT_EQ(
        "has [0] field that is equal to 1"
        ", and has [1] field that is equal to \"foo\""
        ", and has [2] field that is equal to 'c' (99, 0x63)",
        Describe(matcher));
    EXPECT_EQ(
        "has [0] field that isn't equal to 1"
        ", or has [1] field that isn't equal to \"foo\""
        ", or has [2] field that isn't equal to 'c' (99, 0x63)",
        DescribeNegation(matcher));
  }
}

TEST(Matchers, DescriptionStruct) {
  Matcher<const Person&> matcher = Tuple(StartsWith("Billy"), Ge(18));
  EXPECT_EQ(
      "has name field that starts with \"Billy\""
      ", and has age field that is >= 18",
      Describe(matcher));
  EXPECT_EQ(
      "has name field that doesn't start with \"Billy\""
      ", or has age field that isn't >= 18",
      DescribeNegation(matcher));
}

TEST(Matchers, ExplainArray) {
  {
    Matcher<const array<int, 0>&> matcher = Tuple();
    EXPECT_EQ("", Explain(matcher, array<int, 0>{}));
  }
  {
    Matcher<const array<int, 1>&> matcher = Tuple(ExEq(1));
    EXPECT_EQ("whose [0] field matches (which is equal to 1)",
              Explain(matcher, array<int, 1>{{1}}));
    EXPECT_EQ("whose [0] field does not match (which is equal to 2)",
              Explain(matcher, array<int, 1>{{2}}));
  }
  {
    // Do not provide explanation, if we have nothing to add to description.
    Matcher<const array<int, 1>&> matcher = Tuple(1);
    EXPECT_EQ("", Explain(matcher, array<int, 1>{{1}}));
    EXPECT_EQ("", Explain(matcher, array<int, 1>{{2}}));
  }
  {
    Matcher<const array<int, 2>&> matcher = Tuple(ExEq(1), ExEq(2));
    EXPECT_EQ(
        "whose [0] field matches (which is equal to 1)"
        ", and [1] field matches (which is equal to 2)",
        Explain(matcher, array<int, 2>{{1, 2}}));
    EXPECT_EQ(
        "whose [0] field matches (which is equal to 1)"
        ", and [1] field does not match (which is equal to 1)",
        Explain(matcher, array<int, 2>{{1, 1}}));
    EXPECT_EQ(
        "whose [0] field does not match (which is equal to 2)"
        ", and [1] field does not match (which is equal to 1)",
        Explain(matcher, array<int, 2>{{2, 1}}));
    EXPECT_EQ(
        "whose [0] field does not match (which is equal to 2)"
        ", and [1] field matches (which is equal to 2)",
        Explain(matcher, array<int, 2>{{2, 2}}));
  }
  {
    // Do not provide explanation, if we have nothing to add to description.
    Matcher<const array<int, 2>&> matcher = Tuple(1, ExEq(2));
    EXPECT_EQ("whose [1] field matches (which is equal to 2)",
              Explain(matcher, array<int, 2>{{1, 2}}));
    EXPECT_EQ("whose [1] field does not match (which is equal to 1)",
              Explain(matcher, array<int, 2>{{1, 1}}));
    EXPECT_EQ("whose [1] field does not match (which is equal to 1)",
              Explain(matcher, array<int, 2>{{2, 1}}));
    EXPECT_EQ("whose [1] field matches (which is equal to 2)",
              Explain(matcher, array<int, 2>{{2, 2}}));
  }
  {
    Matcher<const array<int, 2>&> matcher = Tuple(ExEq(1), 2);
    EXPECT_EQ("whose [0] field matches (which is equal to 1)",
              Explain(matcher, array<int, 2>{{1, 2}}));
    EXPECT_EQ("whose [0] field matches (which is equal to 1)",
              Explain(matcher, array<int, 2>{{1, 1}}));
    EXPECT_EQ("whose [0] field does not match (which is equal to 2)",
              Explain(matcher, array<int, 2>{{2, 1}}));
    EXPECT_EQ("whose [0] field does not match (which is equal to 2)",
              Explain(matcher, array<int, 2>{{2, 2}}));
  }
  {
    Matcher<const array<int, 2>&> matcher = Tuple(1, 2);
    EXPECT_EQ("", Explain(matcher, array<int, 2>{{1, 2}}));
    EXPECT_EQ("", Explain(matcher, array<int, 2>{{1, 1}}));
    EXPECT_EQ("", Explain(matcher, array<int, 2>{{2, 1}}));
    EXPECT_EQ("", Explain(matcher, array<int, 2>{{2, 2}}));
  }
}

TEST(Matchers, ExplainTuple) {
  {
    Matcher<const tuple<>&> matcher = Tuple();
    EXPECT_EQ("", Explain(matcher, tuple<>{}));
  }
  {
    Matcher<const tuple<int>&> matcher = Tuple(ExEq(1));
    EXPECT_EQ("whose [0] field matches (which is equal to 1)",
              Explain(matcher, tuple<int>{1}));
    EXPECT_EQ("whose [0] field does not match (which is equal to 2)",
              Explain(matcher, tuple<int>{2}));
  }
  {
    // Do not provide explanation, if we have nothing to add to description.
    Matcher<const tuple<int>&> matcher = Tuple(1);
    EXPECT_EQ("", Explain(matcher, tuple<int>{1}));
    EXPECT_EQ("", Explain(matcher, tuple<int>{2}));
  }
  {
    Matcher<const tuple<int, int>&> matcher = Tuple(ExEq(1), ExEq(2));
    EXPECT_EQ(
        "whose [0] field matches (which is equal to 1)"
        ", and [1] field matches (which is equal to 2)",
        Explain(matcher, tuple<int, int>{1, 2}));
    EXPECT_EQ(
        "whose [0] field matches (which is equal to 1)"
        ", and [1] field does not match (which is equal to 1)",
        Explain(matcher, tuple<int, int>{1, 1}));
    EXPECT_EQ(
        "whose [0] field does not match (which is equal to 2)"
        ", and [1] field does not match (which is equal to 1)",
        Explain(matcher, tuple<int, int>{2, 1}));
    EXPECT_EQ(
        "whose [0] field does not match (which is equal to 2)"
        ", and [1] field matches (which is equal to 2)",
        Explain(matcher, tuple<int, int>{2, 2}));
  }
  {
    // Do not provide explanation, if we have nothing to add to description.
    Matcher<const tuple<int, int>&> matcher = Tuple(1, ExEq(2));
    EXPECT_EQ("whose [1] field matches (which is equal to 2)",
              Explain(matcher, tuple<int, int>{1, 2}));
    EXPECT_EQ("whose [1] field does not match (which is equal to 1)",
              Explain(matcher, tuple<int, int>{1, 1}));
    EXPECT_EQ("whose [1] field does not match (which is equal to 1)",
              Explain(matcher, tuple<int, int>{2, 1}));
    EXPECT_EQ("whose [1] field matches (which is equal to 2)",
              Explain(matcher, tuple<int, int>{2, 2}));
  }
  {
    Matcher<const tuple<int, int>&> matcher = Tuple(ExEq(1), 2);
    EXPECT_EQ("whose [0] field matches (which is equal to 1)",
              Explain(matcher, tuple<int, int>{1, 2}));
    EXPECT_EQ("whose [0] field matches (which is equal to 1)",
              Explain(matcher, tuple<int, int>{1, 1}));
    EXPECT_EQ("whose [0] field does not match (which is equal to 2)",
              Explain(matcher, tuple<int, int>{2, 1}));
    EXPECT_EQ("whose [0] field does not match (which is equal to 2)",
              Explain(matcher, tuple<int, int>{2, 2}));
  }
  {
    Matcher<const tuple<int, int>&> matcher = Tuple(1, 2);
    EXPECT_EQ("", Explain(matcher, tuple<int, int>{1, 2}));
    EXPECT_EQ("", Explain(matcher, tuple<int, int>{1, 1}));
    EXPECT_EQ("", Explain(matcher, tuple<int, int>{2, 1}));
    EXPECT_EQ("", Explain(matcher, tuple<int, int>{2, 2}));
  }
}

TEST(Matchers, ExplainStruct) {
  {
    Matcher<const Person&> matcher =
        Tuple(ExEq(std::string("Billy")), ExEq(18));
    EXPECT_EQ(
        "whose name field matches (which is equal to Billy)"
        ", and age field matches (which is equal to 18)",
        Explain(matcher, Person{"Billy", 18}));
    EXPECT_EQ(
        "whose name field does not match (which is equal to Bob)"
        ", and age field matches (which is equal to 18)",
        Explain(matcher, Person{"Bob", 18}));
    EXPECT_EQ(
        "whose name field does not match (which is equal to Bob)"
        ", and age field does not match (which is equal to 12)",
        Explain(matcher, Person{"Bob", 12}));
  }
  {
    Matcher<const Person&> matcher = Tuple("Billy", ExEq(18));
    EXPECT_EQ("whose age field matches (which is equal to 18)",
              Explain(matcher, Person{"Billy", 18}));
    EXPECT_EQ("whose age field does not match (which is equal to 12)",
              Explain(matcher, Person{"Bob", 12}));
  }
}

TEST(Matchers, ExplainNestedStruct) {
  Matcher<const Passport&> matcher =
      Tuple(Tuple(ExEq(std::string("Billy")), ExEq(18)));
  EXPECT_EQ(
      "whose person field matches "
      "(whose name field matches (which is equal to Billy), "
      "and age field matches (which is equal to 18))",
      Explain(matcher, Passport{{"Billy", 18}}));
}

TEST(Matchers, Matches) {
  {
    const tuple<> t{};
    EXPECT_THAT(t, Tuple());
  }
  {
    const tuple<int> t{10};
    EXPECT_THAT(t, Tuple(10));
    EXPECT_THAT(t, Tuple(Eq(10)));
    EXPECT_THAT(t, Tuple(Le(10)));
    EXPECT_THAT(t, Tuple(Not(Le(8))));
    EXPECT_THAT(t, Tuple(_));
  }
  {
    const tuple<int, std::string> t{10, "foo"};
    EXPECT_THAT(t, Tuple(10, "foo"));
    EXPECT_THAT(t, Tuple(Eq(10), Not("boo")));
    EXPECT_THAT(t, Tuple(Not(7), Not("boo")));
    EXPECT_THAT(t, Tuple(_, _));
  }
  {
    const tuple<int, std::string, char> t{10, "foo", 'c'};
    EXPECT_THAT(t, Tuple(10, "foo", 'c'));
    EXPECT_THAT(t, Tuple(Eq(10), Not("boo"), Not('a')));
    EXPECT_THAT(t, Tuple(Not(7), Not("boo"), Le('d')));
    EXPECT_THAT(t, Tuple(_, _, _));
  }
}

TEST(Matchers, TupleOfReferences) {
  int n = 42;
  tuple<int&, const int&> t(n, n);
  EXPECT_THAT(t, Tuple(42, 42));
  EXPECT_THAT(t, Not(Tuple(42, 777)));
  EXPECT_THAT(t, Not(Tuple(777, 42)));
}

TEST(TupleMatcher, CanMatchByValue) {
  // All other tests use Tuple(...) as a Matcher<const Tp&>.  This
  // verifies that it can also be used as a Matcher<Tp>, which is
  // useful for mocking methods that take tuple-like types by value.
  Matcher<tuple<int, bool>> m = Tuple(42, false);
  EXPECT_THAT(make_tuple(42, false), m);
  EXPECT_THAT(make_tuple(42, true), Not(m));
  EXPECT_THAT(make_tuple(41, false), Not(m));
}

TEST(FieldPairsAreMatchers, ArrayTypecasts) {
  {
    typedef array<int, 0> ArrayT;
    CastToMatcherPairTypes<tuple<ArrayT, ArrayT>>(FieldPairsAre());
  }
  {
    typedef array<int, 1> ArrayT;
    CastToMatcherPairTypes<tuple<ArrayT, ArrayT>>(FieldPairsAre(Eq()));
  }
  {
    typedef array<int, 2> ArrayT;
    CastToMatcherPairTypes<tuple<ArrayT, ArrayT>>(FieldPairsAre(Eq(), Eq()));
  }
}

TEST(FieldPairsAreMatchers, TupleTypecasts) {
  {
    typedef tuple<> TupleT;
    CastToMatcherPairTypes<tuple<TupleT, TupleT>>(FieldPairsAre());
  }
  {
    typedef tuple<int> TupleT;
    CastToMatcherPairTypes<tuple<TupleT, TupleT>>(FieldPairsAre(Eq()));
  }
  {
    typedef tuple<int, std::string> TupleT;
    CastToMatcherPairTypes<tuple<TupleT, TupleT>>(FieldPairsAre(Eq(), Eq()));
  }
}

TEST(FieldPairsAreMatchers, PairTypecasts) {
  typedef pair<int, std::string> PairT;
  CastToMatcherPairTypes<tuple<PairT, PairT>>(FieldPairsAre(Eq(), Eq()));
}

TEST(FieldPairsAreMatchers, StructTypecasts) {
  CastToMatcherPairTypes<tuple<Person, Person>>(FieldPairsAre(Eq(), Eq()));
}

TEST(FieldPairsAreMatchers, DescriptionArray) {
  {
    typedef array<int, 0> ArrayT;
    typedef tuple<ArrayT, ArrayT> TuplePair;
    Matcher<TuplePair> matcher = FieldPairsAre();
    EXPECT_EQ("empty tuple", Describe(matcher));
    EXPECT_EQ("not empty tuple", DescribeNegation(matcher));
  }
  {
    typedef array<int, 1> ArrayT;
    typedef tuple<ArrayT, ArrayT> TuplePair;
    Matcher<TuplePair> matcher = FieldPairsAre(DescEq());
    EXPECT_EQ("have [0] fields that are an equal pair", Describe(matcher));
    EXPECT_EQ("have [0] fields that aren't an equal pair",
              DescribeNegation(matcher));
  }
  {
    typedef array<int, 2> ArrayT;
    typedef tuple<ArrayT, ArrayT> TuplePair;
    Matcher<TuplePair> matcher = FieldPairsAre(DescEq(), DescEq());
    EXPECT_EQ(
        "have [0] fields that are an equal pair"
        ", and have [1] fields that are an equal pair",
        Describe(matcher));
    EXPECT_EQ(
        "have [0] fields that aren't an equal pair"
        ", or have [1] fields that aren't an equal pair",
        DescribeNegation(matcher));
  }
  {
    typedef array<int, 3> ArrayT;
    typedef tuple<ArrayT, ArrayT> TuplePair;
    Matcher<TuplePair> matcher = FieldPairsAre(DescEq(), DescEq(), DescEq());
    EXPECT_EQ(
        "have [0] fields that are an equal pair"
        ", and have [1] fields that are an equal pair"
        ", and have [2] fields that are an equal pair",
        Describe(matcher));
    EXPECT_EQ(
        "have [0] fields that aren't an equal pair"
        ", or have [1] fields that aren't an equal pair"
        ", or have [2] fields that aren't an equal pair",
        DescribeNegation(matcher));
  }
  {
    typedef array<int, 3> ArrayT;
    typedef tuple<ArrayT, ArrayT> TuplePair;
    Matcher<TuplePair> matcher = FieldPairsAre(DescEq(), DescLe(), DescGe());
    EXPECT_EQ(
        "have [0] fields that are an equal pair"
        ", and have [1] fields that are a pair where the first <= "
        "the second"
        ", and have [2] fields that are a pair where the first >= "
        "the second",
        Describe(matcher));
    EXPECT_EQ(
        "have [0] fields that aren't an equal pair"
        ", or have [1] fields that aren't a pair where the first <= "
        "the second"
        ", or have [2] fields that aren't a pair where the first >= "
        "the second",
        DescribeNegation(matcher));
  }
}

TEST(FieldPairsAreMatchers, DescriptionTuple) {
  {
    typedef tuple<> TupleT;
    typedef tuple<TupleT, TupleT> TuplePair;
    Matcher<TuplePair> matcher = FieldPairsAre();
    EXPECT_EQ("empty tuple", Describe(matcher));
    EXPECT_EQ("not empty tuple", DescribeNegation(matcher));
  }
  {
    typedef tuple<int> TupleT;
    typedef tuple<TupleT, TupleT> TuplePair;
    Matcher<TuplePair> matcher = FieldPairsAre(DescEq());
    EXPECT_EQ("have [0] fields that are an equal pair", Describe(matcher));
    EXPECT_EQ("have [0] fields that aren't an equal pair",
              DescribeNegation(matcher));
  }
  {
    typedef tuple<int, std::string> TupleT;
    typedef tuple<TupleT, TupleT> TuplePair;
    Matcher<TuplePair> matcher = FieldPairsAre(DescEq(), DescEq());
    EXPECT_EQ(
        "have [0] fields that are an equal pair"
        ", and have [1] fields that are an equal pair",
        Describe(matcher));
    EXPECT_EQ(
        "have [0] fields that aren't an equal pair"
        ", or have [1] fields that aren't an equal pair",
        DescribeNegation(matcher));
  }
  {
    typedef tuple<int, std::string, char> TupleT;
    typedef tuple<TupleT, TupleT> TuplePair;
    Matcher<TuplePair> matcher = FieldPairsAre(DescEq(), DescEq(), DescEq());
    EXPECT_EQ(
        "have [0] fields that are an equal pair"
        ", and have [1] fields that are an equal pair"
        ", and have [2] fields that are an equal pair",
        Describe(matcher));
    EXPECT_EQ(
        "have [0] fields that aren't an equal pair"
        ", or have [1] fields that aren't an equal pair"
        ", or have [2] fields that aren't an equal pair",
        DescribeNegation(matcher));
  }
  {
    typedef tuple<int, std::string, char> TupleT;
    typedef tuple<TupleT, TupleT> TuplePair;
    Matcher<TuplePair> matcher = FieldPairsAre(DescEq(), DescLe(), DescGe());
    EXPECT_EQ(
        "have [0] fields that are an equal pair"
        ", and have [1] fields that are a pair where the first <= "
        "the second"
        ", and have [2] fields that are a pair where the first >= "
        "the second",
        Describe(matcher));
    EXPECT_EQ(
        "have [0] fields that aren't an equal pair"
        ", or have [1] fields that aren't a pair where the first <= "
        "the second"
        ", or have [2] fields that aren't a pair where the first >= "
        "the second",
        DescribeNegation(matcher));
  }
}

TEST(FieldPairsAreMatchers, DescriptionStruct) {
  typedef tuple<const Person&, const Person&> TuplePair;
  Matcher<TuplePair> matcher = FieldPairsAre(DescEq(), DescGe());
  EXPECT_EQ(
      "have name fields that are an equal pair"
      ", and have age fields that are a pair where the first >= the "
      "second",
      Describe(matcher));
  EXPECT_EQ(
      "have name fields that aren't an equal pair"
      ", or have age fields that aren't a pair where the first >= "
      "the second",
      DescribeNegation(matcher));
}

TEST(FieldPairsAreMatchers, DescriptionPartialStruct) {
  typedef tuple<const Person&, tuple<std::string, int>> TuplePair;
  Matcher<TuplePair> matcher = FieldPairsAre(DescEq(), DescGe());
  EXPECT_EQ(
      "have (name, [0]) fields that are an equal pair"
      ", and have (age, [1]) fields that are a pair where the "
      "first >= the second",
      Describe(matcher));
  EXPECT_EQ(
      "have (name, [0]) fields that aren't an equal pair"
      ", or have (age, [1]) fields that aren't a pair where the "
      "first >= the second",
      DescribeNegation(matcher));
}

TEST(FieldPairsAreMatchers, ExplainArray) {
  {
    typedef array<int, 0> ArrayT;
    const ArrayT t{};
    typedef tuple<ArrayT, ArrayT> TuplePair;
    Matcher<TuplePair> matcher = FieldPairsAre();
    EXPECT_EQ("", Explain(matcher, TuplePair(t, ArrayT{})));
  }
  {
    typedef array<int, 1> ArrayT;
    const ArrayT t{{1}};
    typedef tuple<ArrayT, ArrayT> TuplePair;
    Matcher<TuplePair> matcher = FieldPairsAre(ExEq());
    EXPECT_EQ("whose [0] fields match (1 is equal to 1)",
              Explain(matcher, TuplePair(t, ArrayT{{1}})));
    EXPECT_EQ("whose [0] fields do not match (1 is not equal to 2)",
              Explain(matcher, TuplePair(t, ArrayT{{2}})));
  }
  {
    // Inner matcher not providing information
    typedef array<int, 1> ArrayT;
    const ArrayT t{{1}};
    typedef tuple<ArrayT, ArrayT> TuplePair;
    Matcher<TuplePair> matcher = FieldPairsAre(Eq());
    EXPECT_EQ("whose [0] fields match",
              Explain(matcher, TuplePair(t, ArrayT{{1}})));
    EXPECT_EQ("whose [0] fields do not match",
              Explain(matcher, TuplePair(t, ArrayT{{2}})));
  }
  {
    typedef array<int, 2> ArrayT;
    const ArrayT t{{1, 2}};
    typedef tuple<ArrayT, ArrayT> TuplePair;
    Matcher<TuplePair> matcher = FieldPairsAre(ExEq(), ExEq());
    EXPECT_EQ(
        "whose [0] fields match (1 is equal to 1)"
        ", and [1] fields match (2 is equal to 2)",
        Explain(matcher, TuplePair(t, ArrayT{{1, 2}})));
    EXPECT_EQ(
        "whose [0] fields match (1 is equal to 1)"
        ", and [1] fields do not match (2 is not equal to 1)",
        Explain(matcher, TuplePair(t, ArrayT{{1, 1}})));
    EXPECT_EQ(
        "whose [0] fields do not match (1 is not equal to 2)"
        ", and [1] fields do not match (2 is not equal to 1)",
        Explain(matcher, TuplePair(t, ArrayT{{2, 1}})));
    EXPECT_EQ(
        "whose [0] fields do not match (1 is not equal to 2)"
        ", and [1] fields match (2 is equal to 2)",
        Explain(matcher, TuplePair(t, ArrayT{{2, 2}})));
  }
  {
    // Inner matcher not providing information
    typedef array<int, 2> ArrayT;
    const ArrayT t{{1, 2}};
    typedef tuple<ArrayT, ArrayT> TuplePair;
    Matcher<TuplePair> matcher = FieldPairsAre(Eq(), Eq());
    EXPECT_EQ(
        "whose [0] fields match"
        ", and [1] fields match",
        Explain(matcher, TuplePair(t, ArrayT{{1, 2}})));
    EXPECT_EQ(
        "whose [0] fields match"
        ", and [1] fields do not match",
        Explain(matcher, TuplePair(t, ArrayT{{1, 1}})));
    EXPECT_EQ(
        "whose [0] fields do not match"
        ", and [1] fields do not match",
        Explain(matcher, TuplePair(t, ArrayT{{2, 1}})));
    EXPECT_EQ(
        "whose [0] fields do not match"
        ", and [1] fields match",
        Explain(matcher, TuplePair(t, ArrayT{{2, 2}})));
  }
}

TEST(FieldPairsAreMatchers, ExplainTuple) {
  {
    typedef tuple<> TupleT;
    const TupleT t{};
    typedef tuple<TupleT, TupleT> TuplePair;
    Matcher<TuplePair> matcher = FieldPairsAre();
    EXPECT_EQ("", Explain(matcher, TuplePair(t, TupleT())));
  }
  {
    typedef tuple<int> TupleT;
    const TupleT t{1};
    typedef tuple<TupleT, TupleT> TuplePair;
    Matcher<TuplePair> matcher = FieldPairsAre(ExEq());
    EXPECT_EQ("whose [0] fields match (1 is equal to 1)",
              Explain(matcher, TuplePair(t, TupleT(1))));
    EXPECT_EQ("whose [0] fields do not match (1 is not equal to 2)",
              Explain(matcher, TuplePair(t, TupleT(2))));
  }
  {
    // Inner matcher not providing information
    typedef tuple<int> TupleT;
    const TupleT t{1};
    typedef tuple<TupleT, TupleT> TuplePair;
    Matcher<TuplePair> matcher = FieldPairsAre(Eq());
    EXPECT_EQ("whose [0] fields match",
              Explain(matcher, TuplePair(t, TupleT(1))));
    EXPECT_EQ("whose [0] fields do not match",
              Explain(matcher, TuplePair(t, TupleT(2))));
  }
  {
    typedef tuple<int, int> TupleT;
    const TupleT t{1, 2};
    typedef tuple<TupleT, TupleT> TuplePair;
    Matcher<TuplePair> matcher = FieldPairsAre(ExEq(), ExEq());
    EXPECT_EQ(
        "whose [0] fields match (1 is equal to 1)"
        ", and [1] fields match (2 is equal to 2)",
        Explain(matcher, TuplePair(t, TupleT(1, 2))));
    EXPECT_EQ(
        "whose [0] fields match (1 is equal to 1)"
        ", and [1] fields do not match (2 is not equal to 1)",
        Explain(matcher, TuplePair(t, TupleT(1, 1))));
    EXPECT_EQ(
        "whose [0] fields do not match (1 is not equal to 2)"
        ", and [1] fields do not match (2 is not equal to 1)",
        Explain(matcher, TuplePair(t, TupleT(2, 1))));
    EXPECT_EQ(
        "whose [0] fields do not match (1 is not equal to 2)"
        ", and [1] fields match (2 is equal to 2)",
        Explain(matcher, TuplePair(t, TupleT(2, 2))));
  }
  {
    // Inner matcher not providing information
    typedef tuple<int, int> TupleT;
    const TupleT t{1, 2};
    typedef tuple<TupleT, TupleT> TuplePair;
    Matcher<TuplePair> matcher = FieldPairsAre(Eq(), Eq());
    EXPECT_EQ(
        "whose [0] fields match"
        ", and [1] fields match",
        Explain(matcher, TuplePair(t, TupleT(1, 2))));
    EXPECT_EQ(
        "whose [0] fields match"
        ", and [1] fields do not match",
        Explain(matcher, TuplePair(t, TupleT(1, 1))));
    EXPECT_EQ(
        "whose [0] fields do not match"
        ", and [1] fields do not match",
        Explain(matcher, TuplePair(t, TupleT(2, 1))));
    EXPECT_EQ(
        "whose [0] fields do not match"
        ", and [1] fields match",
        Explain(matcher, TuplePair(t, TupleT(2, 2))));
  }
}

TEST(FieldPairsAreMatchers, ExplainStruct) {
  const Person& p{"Billy", 18};
  typedef tuple<const Person&, const Person&> TuplePair;

  Matcher<TuplePair> matcher = FieldPairsAre(ExEq(), ExEq());
  EXPECT_EQ(
      "whose name fields match (Billy is equal to Billy)"
      ", and age fields match (18 is equal to 18)",
      Explain(matcher, TuplePair(p, Person{"Billy", 18})));
  EXPECT_EQ(
      "whose name fields do not match (Billy is not equal to Bob)"
      ", and age fields match (18 is equal to 18)",
      Explain(matcher, TuplePair(p, Person{"Bob", 18})));
  EXPECT_EQ(
      "whose name fields do not match (Billy is not equal to Bob)"
      ", and age fields do not match (18 is not equal to 12)",
      Explain(matcher, TuplePair(p, Person{"Bob", 12})));
}

TEST(FieldPairsAreMatchers, ExplainPartialStruct) {
  typedef tuple<std::string, int> PersonTuple;
  const Person& p{"Billy", 18};
  typedef tuple<const Person&, PersonTuple> TuplePair;

  Matcher<TuplePair> matcher = FieldPairsAre(ExEq(), ExEq());
  EXPECT_EQ(
      "whose (name, [0]) fields match (Billy is equal to Billy)"
      ", and (age, [1]) fields match (18 is equal to 18)",
      Explain(matcher, TuplePair(p, PersonTuple("Billy", 18))));
  EXPECT_EQ(
      "whose (name, [0]) fields do not match (Billy is not equal to Bob)"
      ", and (age, [1]) fields do not match (18 is not equal to 12)",
      Explain(matcher, TuplePair(p, PersonTuple("Bob", 12))));
}

TEST(FieldPairsAreMatchers, PairwiseMatches) {
  {
    typedef tuple<> TupleT;
    const TupleT t{};

    typedef tuple<TupleT, TupleT> TuplePair;
    Matcher<TuplePair> m1 = FieldPairsAre();
    EXPECT_TRUE(m1.Matches(TuplePair(t, TupleT())));
  }
  {
    typedef tuple<int> TupleT;
    const TupleT t{10};

    typedef tuple<TupleT, TupleT> TuplePair;
    Matcher<TuplePair> m1 = FieldPairsAre(Eq());
    Matcher<TuplePair> m2 = FieldPairsAre(Le());
    Matcher<TuplePair> m3 = FieldPairsAre(_);

    EXPECT_TRUE(m1.Matches(TuplePair(t, TupleT(10))));
    EXPECT_TRUE(m2.Matches(TuplePair(t, TupleT(10))));
    EXPECT_TRUE(m2.Matches(TuplePair(t, TupleT(12))));
    EXPECT_FALSE(m2.Matches(TuplePair(t, TupleT(8))));
    EXPECT_TRUE(m3.Matches(TuplePair(t, TupleT(8))));
  }
  {
    typedef tuple<int, std::string> TupleT;
    const TupleT t{10, "foo"};

    typedef tuple<TupleT, TupleT> TuplePair;
    Matcher<TuplePair> m1 = FieldPairsAre(Eq(), Eq());
    Matcher<TuplePair> m2 = FieldPairsAre(_, _);

    EXPECT_TRUE(m1.Matches(TuplePair(t, TupleT(10, "foo"))));
    EXPECT_FALSE(m1.Matches(TuplePair(t, TupleT(7, "foo"))));
    EXPECT_TRUE(m2.Matches(TuplePair(t, TupleT(12, "boo"))));
  }
  {
    typedef tuple<int, std::string, char> TupleT;
    const TupleT t{10, "foo", 'c'};

    typedef tuple<TupleT, TupleT> TuplePair;
    Matcher<TuplePair> m1 = FieldPairsAre(Eq(), Eq(), Eq());
    Matcher<TuplePair> m2 = FieldPairsAre(Eq(), Not(Eq()), Le());
    Matcher<TuplePair> m3 = FieldPairsAre(_, _, _);

    EXPECT_TRUE(m1.Matches(TuplePair(t, TupleT(10, "foo", 'c'))));
    EXPECT_TRUE(m2.Matches(TuplePair(t, TupleT(10, "boo", 'c'))));
    EXPECT_TRUE(m2.Matches(TuplePair(t, TupleT(10, "boo", 'd'))));
    EXPECT_TRUE(m3.Matches(TuplePair(t, TupleT(10, "boo", 'd'))));
  }
}

TEST(FieldPairsAreMatchers, CanBeUsedInsidePointwise) {
  {
    const tuple<int, int> a{2, 3};
    const vector<tuple<int, int>> v{a, a};
    EXPECT_THAT(v, Pointwise(FieldPairsAre(Eq(), Le()), v));
  }
  {
    const tuple<int, std::string, char> t1{10, "foo", 'c'};
    const tuple<int, std::string, char> t2{10, "boo", 'c'};
    const tuple<int, std::string, char> t3{10, "doo", 'd'};
    const vector<tuple<int, std::string, char>> v1{t1, t1};
    const vector<tuple<int, std::string, char>> v2{t2, t3};
    EXPECT_THAT(v1, Pointwise(FieldPairsAre(Eq(), Not(Eq()), Le()), v2));
    EXPECT_THAT(v1, Pointwise(FieldPairsAre(_, _, _), v2));
  }
}

TEST(FieldPairsAreMatchers, CanBeUsedInsideUnorderedPointwise) {
  {
    tuple<int, int> a{2, 3};
    vector<tuple<int, int>> v{a, a};
    EXPECT_THAT(v, UnorderedPointwise(FieldPairsAre(Eq(), Le()), v));
  }
  {
    const tuple<int, std::string, char> t1{10, "foo", 'c'};
    const tuple<int, std::string, char> t2{12, "boo", 'd'};
    const vector<tuple<int, std::string, char>> v1{t1, t2};
    const vector<tuple<int, std::string, char>> v2{t2, t1};
    EXPECT_THAT(v1, Not(Pointwise(FieldPairsAre(Eq(), Eq(), Eq()), v2)));
    EXPECT_THAT(v1, UnorderedPointwise(FieldPairsAre(Eq(), Eq(), Eq()), v2));
    EXPECT_THAT(v1, UnorderedPointwise(FieldPairsAre(_, _, _), v2));
  }
}

TEST(FieldPairsAreMatchers, PairwiseVectorOfTupleOfReferences) {
  int n = 42;
  tuple<int&, const int&> t1(n, n);
  const tuple<const int, int> t2{42, 42};
  const tuple<int, int> t3{42, 777};
  vector<tuple<int&, const int&>> v1{t1, t1};
  const vector<tuple<int, int>> v2{t2, t2};
  const vector<tuple<int, int>> v3{t2, t3};
  EXPECT_THAT(v1, Pointwise(FieldPairsAre(Eq(), Eq()), v2));
  EXPECT_THAT(v1, Not(Pointwise(FieldPairsAre(Eq(), Eq()), v3)));
  EXPECT_THAT(v1, Pointwise(FieldPairsAre(Eq(), Le()), v3));
}

// This test demonstrates the need for a seperately-named matcher for matching
// pairs of tuples.
TEST(FieldPairsAreMatchers, DemonstrateAmbiguity) {
  const tuple<int, int> nums(1, 2);
  const vector<tuple<int, int>> v = {nums};
  EXPECT_THAT(v, Not(Pointwise(FieldPairsAre(nums, nums), v)));
  EXPECT_THAT(v, Pointwise(FieldPairsAre(Eq(), Eq()), v));
}

TEST(FieldPairsAreMatchers, Example) {
  vector<PersonID> actual = {
      {"Homer", 38, 0}, {"Marge", 38, 0}, {"Bart", 10, 0}, {"Lisa", 12, 0}};

  vector<PersonID> expected = {{"Homer", 38, 0x1},
                               {"Marge", 34, 0x2},
                               {"Bart", 10, 0x666},
                               {"Lisa", 8, 0x1337}};

  EXPECT_THAT(actual, Pointwise(FieldPairsAre(Eq(), Ge(), _), expected));
}

}  // namespace
}  // namespace testing
}  // namespace tuple
}  // namespace util
