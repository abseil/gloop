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

//
// Tuple() matcher allows to verify elements of a tuple against predicates.
//
//   std::tuple<int, std::string> t{2, "Hello world"};
//   EXPECT_THAT(t, Tuple(2, StartsWith("Hello")));
//
// It works with any object that implements tuple-like interface (specifically,
// specializes util::tuple::intrinsics).
//
//   std::array<int, 2> a{{2, 3}};
//   EXPECT_THAT(t, Tuple(2, Le(5)));
//
//   struct Person {
//     TUPLE_DEFINE_STRUCT(Person,
//                         (rel, swap, ostream),
//                         (std::string, name),
//                         (int, age));
//   };
//
//   Person someone{"John", 33};
//   EXPECT_THAT(someone, Tuple(_, Ge(21)));
//
// It also works with any object decomposable by structured bindings.
//
//   struct NotATuple {
//     int x;
//     std::string y;
//     // Syntax used to expand:
//     // auto& [x, y] = not_a_tuple;
//   };
//
//   NotATuple value{.x=85, .y="abc"};
//   EXPECT_THAT(value, Tuple(Eq(85), Eq("abc")));
//
//
// FieldPairsAre() matcher allows to verify two-tuple of tuples, for
// example enabling usage from within a testing::Pointwise matcher
//
//   struct Person {
//     TUPLE_DEFINE_STRUCT(Person,
//                         (rel, swap, ostream),
//                         (std::string, name),
//                         (int, age),
//                         (int, id));
//   };
//   vector<Person> actual = {{"Homer", 38, 0},
//                            {"Marge", 38, 0},
//                            {"Bart", 10, 0},
//                            {"Lisa", 12, 0}};
//
//   vector<Person> expected = {{"Homer", 38, 0x1},
//                              {"Marge", 34, 0x2},
//                              {"Bart", 10, 0x666},
//                              {"Lisa", 8, 0x1337}};
//
//   EXPECT_THAT(actual,
//               Pointwise(FieldPairsAre(Eq(), Ge(), _), expected));

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_MATCHERS_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_MATCHERS_H_

#include <stddef.h>

#include <ostream>
#include <string>
#include <tuple>
#include <type_traits>

#include "absl/strings/str_cat.h"
#include "gloop/gloop_test.h"
#include "gloop/util/tuple/bindings/bindings.h"
#include "gloop/util/tuple/for_each.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/is_tuple.h"

namespace util {
namespace tuple {
namespace testing {

namespace internal_matchers {

// Implementation of MatcherInterface to be used in monomorphic matcher.
template <class UnderlyingT, class TupleT, class MatchersT>
class TupleMatcherImpl
    : public ::testing::MatcherInterface<const UnderlyingT&> {
  // To avoid binary bloat, we only allow TupleMatcherImpl to be
  // instantiated with a TupleT that's a "raw" class (i.e. not a
  // reference or CV-qualified).
  static_assert(!std::is_reference<UnderlyingT>::value,
                "TupleT must not be a reference.");
  static_assert(!std::is_const<UnderlyingT>::value,
                "TupleT must not be CV-qualified.");
  static_assert(!std::is_volatile<UnderlyingT>::value,
                "TupleT must not be CV-qualified.");

 public:
  explicit TupleMatcherImpl(const MatchersT& matchers) : matchers_(matchers) {}

  virtual void DescribeTo(::std::ostream* os) const {
    if (!kTupleSize) {
      *os << "empty tuple";
      return;
    }
    for_each_index(DescribeElement{os, false}, matchers_);
  }

  virtual void DescribeNegationTo(::std::ostream* os) const {
    if (!kTupleSize) {
      *os << "not empty tuple";
      return;
    }
    for_each_index(DescribeElement{os, true}, matchers_);
  }

  virtual bool MatchAndExplain(const UnderlyingT& a_tuple,
                               ::testing::MatchResultListener* listener) const {
    bool result = true;
    bool explanation_started = false;
    if constexpr (is_tuple<UnderlyingT>::value) {
      for_each_index(
          MatchElement{&matchers_, listener, &result, &explanation_started},
          a_tuple);
    } else {
      for_each_index(
          MatchElement{&matchers_, listener, &result, &explanation_started},
          bindings::bindings_traits_with_size<
              UnderlyingT, size<TupleT>::value>::field_refs(a_tuple));
    }
    return result;
  }

 private:
  static constexpr ::size_t kTupleSize = size<TupleT>::value;

  struct DescribeElement {
    template <::size_t I, class M>
    void operator()(const M& m) const {
      auto matcher =
          ::testing::SafeMatcherCast<const typename std::remove_reference<
              typename element<I, TupleT>::type>::type&>(m);
      if (const char* key = name<I, TupleT>())
        *os_ << "has " << key << " field that ";
      else
        *os_ << "has [" << I << "] field that ";
      if (negated_)
        matcher.DescribeNegationTo(os_);
      else
        matcher.DescribeTo(os_);
      if (I == kTupleSize - 1) return;
      if (negated_)
        *os_ << ", or ";
      else
        *os_ << ", and ";
    }

    ::std::ostream* const os_;
    const bool negated_;
  };

  struct MatchElement {
    template <::size_t I, class T>
    void operator()(const T& v) const {
      auto matcher = ::testing::SafeMatcherCast<const T&>(get<I>(*matchers_));
      if (!listener_->IsInterested()) {
        *matches_ = *matches_ && matcher.Matches(v);
        return;
      }
      ::testing::StringMatchResultListener inner_listener;
      const bool r = matcher.MatchAndExplain(v, &inner_listener);
      *matches_ &= r;
      const auto inner_explanation = inner_listener.str();
      // According to MatcherInterface::MatchAndExplain() comment, explanation
      // should only be provided when one can add some valuable information to
      // DescribeTo(). Tuple matcher can add valuable information only if it
      // gets it from its submatchers. Thus, on empty info we return early, and
      // proceed to next submatcher.
      if (inner_explanation.empty()) return;
      if (!*explanation_started_) {
        *explanation_started_ = true;
        *listener_ << "whose ";
      } else {
        *listener_ << ", and ";
      }
      if (const char* key = name<I, TupleT>())
        *listener_ << key;
      else
        *listener_ << "[" << I << "]";
      *listener_ << " field " << (r ? "matches" : "does not match") << " ("
                 << inner_explanation << ")";
    }

    const MatchersT* const matchers_;
    ::testing::MatchResultListener* const listener_;
    bool* const matches_;
    bool* const explanation_started_;
  };

  // Tuple of potentially polymorphic matchers.
  const MatchersT matchers_;
};

template <class... Matchers>
class TupleMatcher {
 public:
  explicit TupleMatcher(const Matchers&... matchers) : matchers_(matchers...) {}

  template <class TupleT>
  operator ::testing::Matcher<TupleT>() const {
    if constexpr (is_tuple<TupleT>::value) {
      static_assert(size<TupleT>::value == sizeof...(Matchers),
                    "Unable to match tuples of different sizes.");
      // TupleT might be a reference and/or CV-qualified. Get the
      // tuple-like type without the reference or CV-qualifiers.
      typedef typename std::remove_cv<
          typename std::remove_reference<TupleT>::type>::type RawTupleT;
      // MakeMatcher() below returns a Matcher<const RawTupleT&>, which
      // is not the same as Matcher<TupleT> unless TupleT is a reference
      // to const. Hence the MatcherCast.
      return ::testing::MatcherCast<TupleT>(::testing::MakeMatcher(
          new TupleMatcherImpl<RawTupleT, RawTupleT, ::std::tuple<Matchers...>>(
              matchers_)));
    } else {
      using binding_traits =
          bindings::bindings_traits_with_size<TupleT, sizeof...(Matchers)>;
      using BindingTupleT = typename binding_traits::field_types;
      static_assert(size<BindingTupleT>::value == sizeof...(Matchers),
                    "Unable to match tuples of different sizes.");
      typedef typename std::remove_cv<
          typename std::remove_reference<TupleT>::type>::type RawTupleT;
      return ::testing::MatcherCast<TupleT>(::testing::MakeMatcher(
          new TupleMatcherImpl<RawTupleT, BindingTupleT,
                               ::std::tuple<Matchers...>>(matchers_)));
    }
  }

 private:
  const ::std::tuple<Matchers...> matchers_;
};

// Pointwise Tuple matcher.
template <class TuplePairT, class MatchersT>
class FieldPairsAreMatcherImpl
    : public ::testing::MatcherInterface<TuplePairT> {
 public:
  explicit FieldPairsAreMatcherImpl(const MatchersT& matchers)
      : matchers_(matchers) {}

  virtual void DescribeTo(::std::ostream* os) const {
    if (!kTupleSize) {
      *os << "empty tuple";
      return;
    }
    for_each_index(DescribeElement{os, false}, matchers_);
  }

  virtual void DescribeNegationTo(::std::ostream* os) const {
    if (!kTupleSize) {
      *os << "not empty tuple";
      return;
    }
    for_each_index(DescribeElement{os, true}, matchers_);
  }

  virtual bool MatchAndExplain(TuplePairT tuplepair,
                               ::testing::MatchResultListener* listener) const {
    bool result = true;
    bool explanation_started = false;
    // We need to loop over the tuple of matchers, not the argument tuple, since
    // in piarwise mode the (outer) tuple is always of size 2.
    for_each_index(
        MatchElement{tuplepair, listener, &result, &explanation_started},
        matchers_);
    return result;
  }

 private:
  // Convenience aliases.
  template <size_t I, typename TupleT>
  using ElementT = typename element<I, TupleT>::type;
  template <typename TupleT>
  using RemRefT = typename std::remove_reference<TupleT>::type;

  // Construct tuple element description based on either name field or index.
  template <size_t I, typename TupleT>
  static std::string ElementDescription() {
    const char* n = name<I, TupleT>();
    return n ? n : absl::StrCat("[", I, "]");
  }

  // Construct tuple description for actual and/or expected tuples.
  template <size_t I, typename ActualTupleT, typename ExpectedTupleT>
  static std::string TupleDescription() {
    const std::string actual = ElementDescription<I, ActualTupleT>();
    const std::string expected = ElementDescription<I, ExpectedTupleT>();
    return actual == expected ? actual
                              : absl::StrCat("(", actual, ", ", expected, ")");
  }

  // Get actual and expected tuple type from argument type.
  using ActualTupleT = ElementT<0, TuplePairT>;
  using ExpectedTupleT = ElementT<1, TuplePairT>;

  // We can get tuple size from either actual or expected tuple, since it is
  // already asserted that they are the same size.
  static constexpr ::size_t kTupleSize = size<ActualTupleT>::value;

  struct DescribeElement {
    template <size_t I, class M>
    void operator()(const M& matcher_input) const {
      // In this iteration, get the I'th element from each tuple.
      using ActualT = ElementT<I, ActualTupleT>;
      using ExpectedT = ElementT<I, ExpectedTupleT>;
      using ElemPair = std::tuple<const ActualT&, const ExpectedT&>;

      auto matcher = ::testing::SafeMatcherCast<ElemPair>(matcher_input);

      *os_ << "have " << TupleDescription<I, ActualTupleT, ExpectedTupleT>()
           << " fields that ";
      if (negated_)
        matcher.DescribeNegationTo(os_);
      else
        matcher.DescribeTo(os_);
      if (I == kTupleSize - 1) return;
      if (negated_)
        *os_ << ", or ";
      else
        *os_ << ", and ";
    }

    ::std::ostream* const os_;
    const bool negated_;
  };

  struct MatchElement {
    template <size_t I, class M>
    void operator()(const M& matcher_input) const {
      // Get actual and expected tuple from argument with type
      // tuple<ActualTupleT, ExpectedTupleT>.
      const auto& tuple_actual = get<0>(tuplepair_);
      const auto& tuple_expected = get<1>(tuplepair_);

      // In this iteration, get the I'th element from each tuple.
      const auto& element_actual = get<I>(tuple_actual);
      const auto& element_expected = get<I>(tuple_expected);

      using ActualT = RemRefT<ElementT<I, ActualTupleT>>;
      using ExpectedT = RemRefT<ElementT<I, ExpectedTupleT>>;
      using ElemPair = std::tuple<const ActualT&, const ExpectedT&>;

      auto matcher = ::testing::SafeMatcherCast<ElemPair>(matcher_input);

      if (!listener_->IsInterested()) {
        *matches_ = *matches_ &&
                    matcher.Matches(ElemPair(element_actual, element_expected));
        return;
      }

      ::testing::StringMatchResultListener inner_listener;
      const bool r = matcher.MatchAndExplain(
          ElemPair(element_actual, element_expected), &inner_listener);
      *matches_ &= r;

      // We always give an explanation, since in pairwise matching we can give
      // some information about the matching of individual fields even without
      // inner matcher message.
      const auto inner_explanation = inner_listener.str();
      if (!*explanation_started_) {
        *explanation_started_ = true;
        *listener_ << "whose ";
      } else {
        *listener_ << ", and ";
      }
      *listener_ << TupleDescription<I, ActualTupleT, ExpectedTupleT>();
      *listener_ << " fields" << (r ? " match" : " do not match");

      if (!(inner_explanation.empty()))
        *listener_ << " (" << inner_explanation << ")";
    }

    TuplePairT tuplepair_;
    ::testing::MatchResultListener* const listener_;
    bool* const matches_;
    bool* const explanation_started_;
  };

  // Tuple of potentially polymorphic matchers.
  const MatchersT matchers_;
};

template <class... Matchers>
class FieldPairsAreMatcher {
 public:
  explicit FieldPairsAreMatcher(const Matchers&... matchers)
      : matchers_(matchers...) {}

  template <typename TuplePairT>
  operator ::testing::Matcher<TuplePairT>() const {
    static_assert(size<typename element<0, TuplePairT>::type>::value ==
                      sizeof...(Matchers),
                  "Matcher size needs to match tuple size.");
    static_assert(size<typename element<1, TuplePairT>::type>::value ==
                      sizeof...(Matchers),
                  "Matcher size needs to match tuple size.");
    static_assert(size<typename element<0, TuplePairT>::type>::value ==
                      size<typename element<1, TuplePairT>::type>::value,
                  "Unable to match tuples of different sizes.");
    return ::testing::MakeMatcher(
        new FieldPairsAreMatcherImpl<TuplePairT, ::std::tuple<Matchers...>>(
            matchers_));
  }

 private:
  const ::std::tuple<Matchers...> matchers_;
};

}  // namespace internal_matchers

// Constructs a matcher for tuple-like structures: std::tuple, std::array, and
// other structures that have util::tuple::intrinsics defined.
//
// Examples:
//   std::tuple<int, std::string> t{2, "Hello world"};
//   EXPECT_THAT(t, Tuple(2, StartsWith("Hello")));
//
//   std::array<int, 2> a{{2, 3}};
//   EXPECT_THAT(a, Tuple(2, Le(5)));
template <class... Matchers>
internal_matchers::TupleMatcher<typename ::std::decay<const Matchers>::type...>
Tuple(const Matchers&... matchers) {
  return internal_matchers::TupleMatcher<
      typename ::std::decay<const Matchers>::type...>{matchers...};
}

// Constructs a matcher for the fields of pairs of tuple-like structures:
// std::tuple, std::array, and other structures that have
// util::tuple::intrinsics defined.
//
// Example:
//
//   std::tuple<int, int, int> a{2, 3, 4};
//   std::vector<std::tuple<int, int, int>> v{a, a, a};
//   EXPECT_THAT(v, testing::Pointwise(FieldPairsAre(Eq(), Le(), Ge()), v));
template <class... Matchers>
internal_matchers::FieldPairsAreMatcher<
    typename ::std::decay<Matchers>::type...>
FieldPairsAre(const Matchers&... matchers) {
  return internal_matchers::FieldPairsAreMatcher<
      typename ::std::decay<Matchers>::type...>{matchers...};
}

}  // namespace testing
}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_MATCHERS_H_
