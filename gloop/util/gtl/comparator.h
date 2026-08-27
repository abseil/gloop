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

// This file defines some commonly used comparators and utilities for
// composing comparators.  Read <link> for the full
// documentation.

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_COMPARATOR_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_COMPARATOR_H_

#include <stddef.h>

#include <algorithm>
#include <compare>
#include <functional>
#include <limits>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "gloop/util/gtl/compressed_tuple.h"

namespace gtl {

namespace internal {

// Don't use definitions in this namespace directly.  They are subject
// to change without notice.

// Compare3Way is a helper that performs three-way comparison using a
// comparator that may or may not support it directly.
// If 'c' has an int-valued Compare method, Compare3Way(c, x, y) simply returns
// c.Compare(x, y).
// A negative return value means x<y, 0 means x==y, positive means x>y.

template <typename Cmp, typename T1, typename T2, typename = void>
struct Has3wayCompare : std::false_type {};

template <typename Cmp, typename T1, typename T2>
struct Has3wayCompare<
    Cmp, T1, T2,
    std::enable_if_t<
        std::is_same_v<int, decltype(std::declval<const Cmp&>().Compare(
                                std::declval<const T1&>(),
                                std::declval<const T2&>()))>,
        void>> : std::true_type {};

// Returning +/-1 is often more efficient, but we don't want callers to depend
// on specific positive/negative values.
#ifdef NDEBUG
constexpr int kLessCompareResult = -1;
constexpr int kGreaterCompareResult = 1;
#else
constexpr int kLessCompareResult = -723653489;
constexpr int kGreaterCompareResult = 65232356;
#endif

// For comparators with int Compare().
template <typename Cmp, typename T1, typename T2>
int Compare3WayImpl(const Cmp& c, const T1& x, const T2& y, std::true_type) {
  return c.Compare(x, y);
}

// For comparators without int Compare().
template <typename Cmp, typename T1, typename T2>
int Compare3WayImpl(const Cmp& c, const T1& x, const T2& y, std::false_type) {
  if (c(x, y)) return kLessCompareResult;
  if (c(y, x)) return kGreaterCompareResult;
  return 0;
}

template <typename Cmp, typename T1, typename T2>
int Compare3Way(const Cmp& c, const T1& x, const T2& y) {
  return Compare3WayImpl(c, x, y, Has3wayCompare<Cmp, T1, T2>());
}

}  // namespace internal

struct Less;

////////////////////////////////////////////////////////////
// A comparator class that uses an Extractor to extract the property
// of interest from the input values and then uses a Comparator to
// compare the properties.  The Comparator type argument defaults to
// Less (the < operator), which is the most common case.
template <typename Extractor, typename Comparator = Less>
class OrderBy : private gtl::CompressedTuple<Extractor, Comparator> {
  using Base = gtl::CompressedTuple<Extractor, Comparator>;

 public:
  constexpr OrderBy() = default;
  explicit constexpr OrderBy(Extractor extractor)
      : Base(std::move(extractor), Comparator()) {}
  constexpr OrderBy(Extractor extractor, Comparator comp)
      : Base(std::move(extractor), std::move(comp)) {}

  template <typename T1, typename T2>
  bool operator()(const T1& x, const T2& y) const {
    return AsC()(std::invoke(AsE(), x), std::invoke(AsE(), y));
  }

  template <typename T1, typename T2>
  int Compare(const T1& x, const T2& y) const {
    return internal::Compare3Way(AsC(), std::invoke(AsE(), x),
                                 std::invoke(AsE(), y));
  }

 private:
  const Extractor& AsE() const { return Base::template get<0>(); }
  const Comparator& AsC() const { return Base::template get<1>(); }
};

// Explicit CTAD guides, same as what the default ones would be.
template <typename Extractor>
OrderBy(Extractor) -> OrderBy<Extractor>;
template <typename Extractor, typename Comparator>
OrderBy(Extractor, Comparator) -> OrderBy<Extractor, Comparator>;

// To support overloaded pointer-to-member expressions.
// This is needed to transparently support the old signature in case of multiple
// overloads for the function:
//   template <class C, typename T>
//   constexpr auto OrderByGetter(T (C::*member_ptr)() const);
template <typename T, typename O>
OrderBy(T (O::*)() const) -> OrderBy<T (O::*)() const>;
template <typename T, typename O, typename Comparator>
OrderBy(T (O::*)() const, Comparator) -> OrderBy<T (O::*)() const, Comparator>;

namespace internal {

template <typename... C>
class ChainComparators : private gtl::CompressedTuple<C...> {
  using Base = gtl::CompressedTuple<C...>;

 public:
  explicit constexpr ChainComparators(C... c) : Base(std::move(c)...) {}

  // Constraint resolves ambiguity with the above constructor when C... is
  // empty.
  constexpr ChainComparators()
    requires(sizeof...(C) > 0)
  = default;

  template <typename T1, typename T2>
  bool operator()(const T1& x, const T2& y) const {
    return Call2Way(x, y, Tag<0>());
  }

  // Ternary result: <0 means x<y, 0 means x==y, >0 means x>y.
  template <typename T1, typename T2>
  int Compare(const T1& x, const T2& y) const {
    return Call3Way(x, y, Tag<0>());
  }

 private:
  template <size_t I>
  struct Tag {};

  // 2-way comparison implementation. In the worst case, performs 3-way
  // comparison of the first n-1 elements, and a 2-way comparison of the
  // last element.
  template <typename T1, typename T2, size_t I>
  bool Call2Way(const T1& x, const T2& y, Tag<I>) const {
    if constexpr (I == sizeof...(C)) {
      return false;
    } else if constexpr (I == sizeof...(C) - 1) {
      return std::invoke(this->template get<sizeof...(C) - 1>(), x, y);
    } else {
      if (int r = Compare3Way(this->template get<I>(), x, y)) return r < 0;
      return Call2Way(x, y, Tag<I + 1>());
    }
  }

  // 3-way comparison implementation. In the worst case, performs 3-way
  // comparison of all elements.
  template <typename T1, typename T2, size_t I>
  int Call3Way(const T1& x, const T2& y, Tag<I>) const {
    if constexpr (I == sizeof...(C)) {
      return 0;
    } else {
      if (int r = Compare3Way(Base::template get<I>(), x, y)) return r;
      return Call3Way(x, y, Tag<I + 1>());
    }
  }
};

}  // namespace internal

////////////////////////////////////////////////////////////
// Extractors.

// Returns a const reference to its argument.
struct ExtractIdentity {
  template <class T>
  const T& operator()(const T& x) const {
    return x;
  }
};

// Given 'x', returns '*x'.
struct ExtractPointee {
  template <typename T>
  auto operator()(const T& x) const -> decltype(*x) {
    return *x;
  }
};

// A functor that returns the .first field of an object (usually a pair).
struct First {
  template <class P>
  const typename P::first_type& operator()(const P& p) const {
    return p.first;
  }
};

// A functor that returns the .second field of an object (usually a pair).
struct Second {
  template <class P>
  const typename P::second_type& operator()(const P& p) const {
    return p.second;
  }
};

// A functor that returns the N-th element of a tuple-like type (typically
// std::pair, std::tuple or std::array).  N is 0-based, as for std::get<N>.
template <size_t N>
struct TupleElement {
  template <class P>
  const typename std::tuple_element<N, P>::type& operator()(const P& p) const {
    using std::get;
    return get<N>(p);
  }
};

// A functor that returns the size() property of an object.
struct Size {
  template <class T>
  size_t operator()(const T& x) const {
    return x.size();
  }
};

////////////////////////////////////////////////////////////
// Simple comparators.

// A comparator that uses operator < to compare values.
struct Less {
  template <typename T1, typename T2>
  decltype(std::declval<T1>() < std::declval<T2>()) operator()(
      const T1& x, const T2& y) const {
    return x < y;
  }

  // NOTE: change the return type to match the type of x <=> y
  // (std::strong_ordering, std::partial_ordering, etc.)
  template <typename T1, typename T2>
    requires std::three_way_comparable_with<T1, T2>
  int Compare(const T1& x, const T2& y) const {
    auto ordering = x <=> y;
    if (ordering == ordering.less) return internal::kLessCompareResult;
    if (ordering == ordering.greater) return internal::kGreaterCompareResult;
    return 0;
  }
};

// A comparator that uses operator > to compare values.
struct Greater {
  template <typename T1, typename T2>
  bool operator()(const T1& x, const T2& y) const {
    return x > y;
  }

  template <typename T1, typename T2>
    requires std::three_way_comparable_with<T1, T2>
  // NOTE: change the return type to match the type of x <=> y
  // (std::strong_ordering, std::partial_ordering, etc.)
  int Compare(const T1& x, const T2& y) const {
    auto ordering = x <=> y;
    if (ordering == ordering.less) return internal::kGreaterCompareResult;
    if (ordering == ordering.greater) return internal::kLessCompareResult;
    return 0;
  }
};

// A comparator that reverses the ordering of another comparator, Cmp.
template <typename Cmp>
class Reverse : private gtl::CompressedTuple<Cmp> {
  using Base = gtl::CompressedTuple<Cmp>;

 public:
  constexpr Reverse() = default;
  explicit constexpr Reverse(Cmp cmp) : Base(std::move(cmp)) {}

  template <typename T1, typename T2>
  bool operator()(const T1& lhs, const T2& rhs) const {
    return Impl()(rhs, lhs);
  }

  // NOTE: change the return type to match the type of x <=> y
  // (std::strong_ordering, std::partial_ordering, etc.)
  template <typename T1, typename T2>
  int Compare(const T1& lhs, const T2& rhs) const {
    return internal::Compare3Way(Impl(), rhs, lhs);
  }

 private:
  const Cmp& Impl() const { return Base::template get<0>(); }
};
// Explicit CTAD deduction guide.
template <typename Cmp>
Reverse(Cmp) -> Reverse<Cmp>;

// A comparator for floating-point numbers that models a strict weak order in
// the presence of NaNs (it sorts them first), see <link>
// and https://en.cppreference.com/w/cpp/concepts/strict_weak_order.
//
// Warning: this is not a total order, all NaNs are treated as equivalent.
// More precisely, for any two NaNs `x` and `y` we have `!(x < y) && !(y < x)`.
// This is similar to how +0.0 and -0.0 are treated by the regular comparison.
// For example, this is a sorted sequence according to `NanFirstLess`:
// {nan, -nan, nan, -nan, -inf, -1.0, +0.0, -0.0, +0.0, -0.0, 1.0, +inf}.
struct NanFirstLess {
  template <typename T>
  constexpr bool operator()(T x, T y) const {  // Intentional pass-by-value.
    static_assert(std::is_floating_point_v<T>);
    static_assert(std::numeric_limits<T>::is_iec559);
    // We sort the NaNs first, that is:
    // |  x    y  | result | expression
    // +----------+--------+------------
    // | 0.0  1.0 |  true  | x < y
    // | NaN  1.0 |  true  | y == y
    // | 0.0  NaN | false  | y == y
    // | NaN  NaN | false  | y == y
    // Implementation note:
    // Both the order of `y != y` and `x != x` and the use of `|` are important.
    // On x86 machines the floating point comparison can be done using the
    // `ucomisd` instruction that sets the parity flag if *any* of the inputs
    // are NaNs. By arranging the condition this particular way we allow the
    // compiler to merge NaN tests and non-NaN comparison into a single
    // instruction, which shaves off one test on the critical path (see also
    // the benchmark in experimental/users/dtl/misc/nan_first_test.cc).
    return ABSL_PREDICT_FALSE(y != y | x != x) ? y == y : x < y;
  }

  template <typename T>
  constexpr int Compare(T x, T y) const {  // 3-way compare.
    static_assert(std::is_floating_point_v<T>);
    static_assert(std::numeric_limits<T>::is_iec559);
    // The expression below were written so that they can be compiled to
    // a single comparison instruction on x86_64 and become trivial when
    // assuming NaNs are not present. Also, for whatever reason, at the
    // moment of writing, the order in which the variables are listed
    // makes a difference in the compiled output (it's quite brittle).
    bool no_nans = y == y & x == x;                // PF == 0.
    int greater = ((!no_nans) | (y < x)) ? 1 : 0;  // CF == 1.
    int less = (no_nans & (x < y)) ? 1 : 0;        // CF == 0 && ZF == 0.
    if (ABSL_PREDICT_TRUE(no_nans)) {
      return greater - less;
    } else {
      return (y != y ? 1 : 0) - (x != x ? 1 : 0);
    }
  }
};

// Similar to the above, but uses `>` to compare the values.
// Warning: this is not a total order, see the comments for `NanFirstLess`.
// As an example, this is a sorted sequence according to `NanFirstGreater`:
// {nan, -nan, nan, -nan, +inf, 1.0, +0.0, -0.0, +0.0, -0.0, -1.0, -inf}.
struct NanFirstGreater {
  template <typename T>
  constexpr bool operator()(T x, T y) const {  // Intentional pass-by-value.
    static_assert(std::is_floating_point_v<T>);
    static_assert(std::numeric_limits<T>::is_iec559);
    // The order of `x != x` and `y != y` changed intentionally, see above.
    return ABSL_PREDICT_FALSE(x != x | y != y) ? y == y : x > y;
  }

  template <typename T>
  constexpr int Compare(T x, T y) const {  // 3-way compare.
    static_assert(std::is_floating_point_v<T>);
    static_assert(std::numeric_limits<T>::is_iec559);
    // The order of `less` and `greater` was swapped on purpose.
    bool no_nans = y == y & x == x;                // PF == 0.
    int less = (no_nans & (x < y)) ? 1 : 0;        // CF == 0 && ZF == 0.
    int greater = ((!no_nans) | (y < x)) ? 1 : 0;  // CF == 1.
    if (ABSL_PREDICT_TRUE(no_nans)) {
      return less - greater;
    } else {
      return (y != y ? 1 : 0) - (x != x ? 1 : 0);
    }
  }
};

// A comparator equivalent to std::lexicographical_compare for comparing
// containers, using a specified per-element comparator.
template <typename ElementComparator = Less>
class LexicographicalComparator
    : private gtl::CompressedTuple<ElementComparator> {
  using Base = gtl::CompressedTuple<ElementComparator>;

 public:
  constexpr explicit LexicographicalComparator(ElementComparator cmp)
      : Base(std::move(cmp)) {}
  constexpr LexicographicalComparator() = default;

  template <typename T1, typename T2>
  bool operator()(const T1& x, const T2& y) const {
    return Compare(x, y) < 0;
  }

  template <typename T1, typename T2>
  int Compare(const T1& x, const T2& y) const {
    const ElementComparator& cmp = Base::template get<0>();
    auto x_iter = std::begin(x), x_end = std::end(x);
    auto y_iter = std::begin(y), y_end = std::end(y);
    for (; y_iter != y_end; ++x_iter, ++y_iter) {
      if (x_iter == x_end) return internal::kLessCompareResult;
      if (int r = internal::Compare3Way(cmp, *x_iter, *y_iter)) return r;
    }
    return x_iter == x_end ? 0 : internal::kGreaterCompareResult;
  }
};
template <typename ElementComparator>
LexicographicalComparator(ElementComparator)
    -> LexicographicalComparator<ElementComparator>;

template <typename Cmp>
ABSL_DEPRECATE_AND_INLINE()
constexpr auto MakeLexicographicalComparator(Cmp cmp) {
  return gtl::LexicographicalComparator(cmp);
}
ABSL_DEPRECATE_AND_INLINE()
inline constexpr auto MakeLexicographicalComparator() {
  return gtl::LexicographicalComparator();
}

////////////////////////////////////////////////////////////
// Short-hands for frequently-used OrderBy<> patterns.

using OrderByFirst = OrderBy<First>;
using OrderByFirstGreater = OrderBy<First, Greater>;

using OrderBySecond = OrderBy<Second>;
using OrderBySecondGreater = OrderBy<Second, Greater>;

template <size_t N>
using OrderByTupleElement = OrderBy<TupleElement<N>>;

template <size_t N>
using OrderByTupleElementGreater = OrderBy<TupleElement<N>, Greater>;

////////////////////////////////////////////////////////////
// Useful wrapper functions.
//
// These wrappers allow us to provide the extractor/comparator functor
// *object* (as opposed to just the functor type).  They are handy
// when we don't want to create the extractor/comparator functor via
// its default constructor, or when we want to use a function pointer
// instead of a functor for the extractor/comparator.

// OrderByPointee(c), where c defaults to Less().
// Returns a comparator that will apply 'c' to the pointees of its arguments.
// That is, 'OrderByPointee(c)(a, b)' is like 'c(*a, *b)'.
template <typename C = Less>
constexpr OrderBy<ExtractPointee, C> OrderByPointee(C c = C()) {
  return {{}, std::move(c)};
}

// ChainComparators(c...) --
// Returns a comparator that will order by the specified comparators 'c',
// where each comparator is called in order until the tie is broken.
template <typename... C>
constexpr internal::ChainComparators<C...> ChainComparators(C... c) {
  return internal::ChainComparators<C...>(std::move(c)...);
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_COMPARATOR_H_
