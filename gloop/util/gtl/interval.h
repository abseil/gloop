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

// An Interval<T> is a data structure used to represent a contiguous, mutable
// range over an ordered type T. Supported operations include testing a value to
// see whether it is included in the interval, comparing two intervals, and
// performing their union, intersection, and difference. For the purposes of
// this library, an "ordered type" is any type that induces a total order on its
// values via its less-than operator (operator<()). Examples of such types are
// basic arithmetic types like int and double as well as class types like
// string.
//
// An Interval<T> is represented using the usual C++ STL convention, namely as
// the half-open interval [start, limit). A point p is considered to be
// contained in the interval iff `p >= start && p < limit`.
//
// There is no canonical representation for the empty interval; rather, any
// interval where `limit <= start` is regarded as empty. As a consequence, two
// empty intervals will still compare as equal despite possibly having different
// underlying start or limit values.
//
// T is required to be default- and copy-constructable, to have an assignment
// operator, and the full complement of comparison operators (<, <=, ==, !=, >=,
// >).  A difference operator (operator-()) is required if Interval<T>::Length
// is used.
//
// Interval supports operator==. Two intervals are considered equal if either
// they are both empty or if their corresponding start and limit fields compare
// equal. Interval also provides an operator<.
// Unfortunately, operator< is currently buggy because its behavior is
// inconsistent with operator==: two empty ranges with different representations
// may be regarded as equal by operator== but regarded as different by
// operator< (b/9240050).
//
// This class is thread-compatible if T is thread-compatible. (See
// <link>).
//
// Examples:
//   Interval<int> r1(0, 100);  // The interval [0, 100).
//   EXPECT_TRUE(r1.contains(0));
//   EXPECT_TRUE(r1.contains(50));
//   EXPECT_FALSE(r1.contains(100));  // 100 is just outside the interval.
//
//   Interval<int> r2(50, 150);  // The interval [50, 150).
//   EXPECT_TRUE(r1.Intersects(r2));
//   EXPECT_FALSE(r1.contains(r2));
//   EXPECT_TRUE(r1.IntersectWith(r2));  // Mutates r1.
//   EXPECT_EQ(Interval<int>(50, 100), r1);  // r1 is now [50, 100).
//
//   Interval<int> r3(1000, 2000);  // The interval [1000, 2000).
//   EXPECT_TRUE(r1.IntersectWith(r3));  // Mutates r1.
//   EXPECT_TRUE(r1.empty());  // Now r1 is empty.
//   EXPECT_FALSE(r1.contains(r1.start()));  // doesn't contain its own start.

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_INTERVAL_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_INTERVAL_H_

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/cord.h"
#include "absl/strings/has_absl_stringify.h"
#include "absl/strings/str_format.h"
#include "gloop/util/gtl/heterogeneous_lookup.h"

namespace gtl {
namespace internal_interval {

// Type trait for deriving the return type for `Interval::length`.  If
// operator-() is not defined for T, then the return type is void.  This makes
// the signature for `length` compile so that the class can be used for such
// T, but code that calls `length` would still generate a compilation error.
struct NoValue;
template <typename V>
auto TestSubtract(const V*)
    -> decltype(std::declval<const V&>() - std::declval<const V&>());
template <typename V>
NoValue TestSubtract(...);

template <typename T>
using DiffType = std::decay_t<decltype(TestSubtract<T>(nullptr))>;

// A  class that allows for use with `gtl::HeterogeneousLookupKeyArg`. By
// default heterogeneous lookup is disabled.
template <typename T>
struct AllowHeterogeneousLookup {};

// We allow heterogeneous lookup for string-like types.
template <>
struct AllowHeterogeneousLookup<std::string> {
  using is_transparent = void;
};
template <>
struct AllowHeterogeneousLookup<absl::Cord> {
  using is_transparent = void;
};

}  // namespace internal_interval

template <typename T>
class Interval {
 private:
  using value_type = T;

  template <class U>
  using key_arg =
      HeterogeneousLookupKeyArg<U, T,
                                internal_interval::AllowHeterogeneousLookup<T>>;

 public:
  // Construct an Interval representing an empty interval.
  constexpr Interval() : start_(), limit_() {}

  // Construct an `Interval` representing the half-open interval [start, limit).
  //
  // If `start < limit`, the constructed object will represent the non-empty
  // interval containing all values from start up to (but not including) limit.
  // On the other hand, if `start >= limit`, the constructed object will
  // represent the empty interval.
  constexpr Interval(const T& start, const T& limit)
      : start_(start), limit_(limit) {}

  template <typename U1, typename U2,
            typename = std::enable_if_t<std::is_convertible_v<U1, T> &&
                                        std::is_convertible_v<U2, T>>>
  constexpr Interval(U1&& start, U2&& limit)
      : start_(std::forward<U1>(start)), limit_(std::forward<U2>(limit)) {}

  constexpr const T& start() const { return start_; }
  constexpr const T& limit() const { return limit_; }
  void set_start(const T& t) { start_ = t; }
  void set_limit(const T& t) { limit_ = t; }

  void Set(const T& start, const T& limit) {
    set_start(start);
    set_limit(limit);
  }

  void clear() { *this = {}; }

  ABSL_DEPRECATE_AND_INLINE()
  constexpr bool Empty() const { return empty(); }
  constexpr bool empty() const { return start() >= limit(); }

  // Returns the length of this interval. The value returned is zero if
  // `empty()` is true; otherwise the value returned is `limit() - start()`.
  ABSL_DEPRECATE_AND_INLINE()
  constexpr internal_interval::DiffType<T> Length() const { return length(); }
  constexpr internal_interval::DiffType<T> length() const {
    return (empty() ? start() : limit()) - start();
  }

  // Returns true if and only if `t >= start() && t < limit()`.
  ABSL_DEPRECATE_AND_INLINE()
  constexpr bool Contains(const T& t) const { return contains(t); }
  template <int&... ExplicitParameterBarrier, typename U = T>
  constexpr bool contains(const key_arg<U>& t) const {
    return start() <= t && limit() > t;
  }

  // Returns true if and only if `*this` and `i` are non-empty, and whenever a
  // value `t` is contained in `i`, then it is also contained in `*this`.
  // Note the unintuitive consequence of this definition: this method always
  // returns false when i is the empty interval.
  ABSL_DEPRECATE_AND_INLINE()
  constexpr bool Contains(const Interval& i) const { return contains(i); }
  template <int&... ExplicitParameterBarrier, typename U = T>
  constexpr bool contains(const Interval<key_arg<U>>& i) const {
    return !empty() && !i.empty() && start() <= i.start() &&
           limit() >= i.limit();
  }

  // Returns true if and only if there exists some point `t` for which
  // `this->contains(t) && i.contains(t)` evaluates to true, i.e. if the
  // intersection is non-empty.
  template <int&... ExplicitParameterBarrier, typename U = T>
  constexpr bool Intersects(const Interval<key_arg<U>>& i) const {
    return !empty() && !i.empty() && start() < i.limit() && limit() > i.start();
  }

  // Returns true if and only if there exists some point `t` for which
  // `this->contains(t) && i.contains(t)` evaluates to true, i.e. if the
  // intersection is non-empty.  Furthermore, if the intersection is non-empty
  // and the out pointer is not null, this method stores the calculated
  // intersection in *out.
  template <int&... ExplicitParameterBarrier, typename U = T>
  bool Intersects(const Interval<key_arg<U>>& i, Interval* out) const;

  // Sets `*this` to be the intersection of itself with `i`. Returns true if and
  // only if `*this` was modified.
  template <int&... ExplicitParameterBarrier, typename U = T>
  bool IntersectWith(const Interval<key_arg<U>>& i);

  // Calculates the smallest interval containing both `*this` and `i`, updates
  // `*this` to represent that interval, and returns true if and only if `*this`
  // was modified.
  template <int&... ExplicitParameterBarrier, typename U = T>
  bool SpanningUnion(const Interval<key_arg<U>>& i);

  // Determines the difference between two intervals as in
  // `Difference(Interval&, vector*)`, but stores the results directly in out
  // parameters rather than dynamically allocating an `Interval*` and appending
  // it to a vector. If two results are generated, the one with the smaller
  // value of start() will be stored in `lo` and the other in `hi`. Otherwise
  // (if fewer than two results are generated), unused arguments will be set to
  // the empty interval (it is possible that `lo` will be empty and `hi`
  // non-empty). The method returns true if and only if the intersection of
  // `*this` and `i` is non-empty.
  ABSL_DEPRECATE_AND_INLINE()
  bool Difference(const Interval& i, Interval* lo, Interval* hi) const {
    return Difference(i, *lo, *hi);
  }
  template <int&... ExplicitParameterBarrier, typename U = T>
  bool Difference(const Interval<key_arg<U>>& i, Interval& lo,
                  Interval& hi) const;

  // Splits the given interval into a sequence of sub-intervals.
  // If `*this` is the empty interval, an empty vector is returned.
  //
  // The start of each sub-interval corresponds to the end of the previous one,
  // or to the start of the full interval for the first sub-interval.
  // The end of each sub-interval is obtained by using the value returned by
  // `next_si_limit_fn` when called with the start of sub-interval.
  // The process is repeated until a call of `next_si_limit_fn` returns the end
  // of the full interval as output.
  //
  // `next_si_limit_fn` is a callback that takes the start value of the current
  // sub-interval and returns the end value for the same sub-interval.
  // It requires the returned value to be greater than the input value provided
  // or else it has an undefined behaviour.
  // If the end value returned by the function for the current sub-interval is
  // greater than the full interval end, the full interval end is used instead.
  // When implementing `next_si_limit_fn` care needs to be taken when splitting
  // infinite intervals to avoid infinite loops.
  //
  // Example Usage:
  // const Interval<int> i(0, 25);
  // const std::vector<int> si = i.Split([](const int& s) { return s + 10; });
  // // => {{0, 10}, {10, 20}, {20, 25}}
  template <typename Fn>
  std::vector<Interval> Split(Fn next_si_limit_fn) const;

  template <typename H>
  friend H AbslHashValue(H h, const Interval& i) {
    if (i.empty()) {
      return H::combine(std::move(h), false);
    } else {
      return H::combine(std::move(h), i.start(), i.limit(), true);
    }
  }

  template <typename U = T, typename = decltype(std::declval<const T&>() ==
                                                std::declval<const U&>())>
  friend constexpr bool operator==(const Interval& a,
                                   const Interval<key_arg<U>>& b) {
    bool ae = a.empty();
    bool be = b.empty();
    if (ae && be) return true;   // All empties are equal.
    if (ae != be) return false;  // Empty cannot equal nonempty.
    return a.start() == b.start() && a.limit() == b.limit();
  }

  template <typename U = T, typename = decltype(std::declval<const T&>() ==
                                                std::declval<const U&>())>
  friend constexpr bool operator!=(const Interval& a,
                                   const Interval<key_arg<U>>& b) {
    return !(a == b);
  }

  // Defines a comparator which can be used to induce an order on `Interval`s,
  // so that, for example, they can be stored in an ordered container such as
  // `std::set`. The ordering is arbitrary, but does provide the guarantee that,
  // for non-empty intervals X and Y, if X contains Y, then X <= Y.
  //
  // TODO: The current implementation of this comparator has a problem
  // because the ordering it induces is inconsistent with that of Equals(). In
  // particular, this comparator does not properly consider all empty intervals
  // equivalent. Bug b/9240050 has been created to track this.
  template <typename U = T, typename = decltype(std::declval<const T&>() <
                                                std::declval<const U&>())>
  friend constexpr bool operator<(const Interval& a,
                                  const Interval<key_arg<U>>& b) {
    bool a_empty = !(a.start() < a.limit());
    bool b_empty = !(b.start() < b.limit());
    if (a_empty && b_empty) return false;
    if (a_empty) return true;
    if (b_empty) return false;
    return a.start() < b.start() ||
           (!(b.start() < a.start()) && b.limit() < a.limit());
  }

  template <int I>
  T& get() & {
    return I == 0 ? start_ : limit_;
  }
  template <int I>
  T&& get() && {
    return std::move(I == 0 ? start_ : limit_);
  }
  template <int I>
  constexpr const T& get() const& {
    return I == 0 ? start_ : limit_;
  }
  template <int I>
  constexpr const T&& get() const&& {
    return std::move(I == 0 ? start_ : limit_);
  }

 private:
  T start_;  // Inclusive lower bound.
  T limit_;  // Exclusive upper bound.
};

// Class template argument deduction guide, allowing you to create an interval
// without specifying the value type:
//
//     // Automatically creates gtl::Interval<int>.
//     gtl::Interval(17, 19)
//
template <int&..., typename T, typename U,
          typename = std::enable_if_t<
              std::is_same_v<absl::decay_t<T>, absl::decay_t<U>>>>
Interval(T&&, U&&) -> Interval<std::decay_t<T>>;

// An old-fashioned way to deduce the value type. Use class template argument
// deduction in new code.
template <int&... ExplicitParameterBarrier, typename T, typename U,
          absl::enable_if_t<std::is_same_v<absl::decay_t<T>, absl::decay_t<U>>,
                            int> = 0>
ABSL_DEPRECATE_AND_INLINE()
Interval<absl::decay_t<T>> MakeInterval(T&& lhs, U&& rhs) {
  return Interval<absl::decay_t<T>>(std::forward<T>(lhs), std::forward<U>(rhs));
}

// Note: ideally we'd use
//   decltype(out << "[" << i.start() << ", " << i.limit() << ")")
// as return type of the function, but as of July 2017 this triggers g++
// "sorry, unimplemented: string literal in function template signature" error.
template <typename T>
auto operator<<(std::ostream& out, const Interval<T>& i)
    -> decltype(out << i.start()) {
  return out << "[" << i.start() << ", " << i.limit() << ")";
}

template <typename Sink, typename T>
  requires(absl::HasAbslStringify<T>::value)
void AbslStringify(Sink& sink, const Interval<T>& i) {
  absl::Format(&sink, "[%v, %v)", i.start(), i.limit());
}

//==============================================================================
// Implementation details: Clients can stop reading here.

template <typename T>
template <int&..., typename U>
bool Interval<T>::Intersects(const Interval<key_arg<U>>& i,
                             Interval* out) const {
  if (!Intersects(i)) return false;
  if (out != nullptr) {
    *out = Interval(std::max(start(), i.start()), std::min(limit(), i.limit()));
  }
  return true;
}

template <typename T>
template <int&..., typename U>
bool Interval<T>::IntersectWith(const Interval<key_arg<U>>& i) {
  if (empty()) return false;
  bool modified = false;
  if (i.start() > start()) {
    set_start(i.start());
    modified = true;
  }
  if (i.limit() < limit()) {
    set_limit(i.limit());
    modified = true;
  }
  return modified;
}

template <typename T>
template <int&..., typename U>
bool Interval<T>::SpanningUnion(const Interval<key_arg<U>>& i) {
  if (i.empty()) return false;
  if (empty()) {
    *this = i;
    return true;
  }
  bool modified = false;
  if (i.start() < start()) {
    set_start(i.start());
    modified = true;
  }
  if (i.limit() > limit()) {
    set_limit(i.limit());
    modified = true;
  }
  return modified;
}

template <typename T>
template <int&..., typename U>
bool Interval<T>::Difference(const Interval<key_arg<U>>& i, Interval& lo,
                             Interval& hi) const {
  // Initialize `lo` and `hi` to empty
  lo = {};
  hi = {};
  if (empty()) return false;
  if (i.empty()) {
    lo = *this;
    return false;
  }
  if (start() < i.limit() && start() >= i.start() && limit() > i.limit()) {
    //            [------ this ------)
    // [------ i ------)
    //                 [-- result ---)
    hi = Interval(i.limit(), limit());
    return true;
  }
  if (limit() > i.start() && limit() <= i.limit() && start() < i.start()) {
    // [------ this ------)
    //            [------ i ------)
    // [- result -)
    lo = Interval(start(), i.start());
    return true;
  }
  if (start() < i.start() && limit() > i.limit()) {
    // [------- this --------)
    //      [---- i ----)
    // [ R1 )           [ R2 )
    // There are two results: R1 and R2.
    lo = Interval(start(), i.start());
    hi = Interval(i.limit(), limit());
    return true;
  }
  if (start() >= i.start() && limit() <= i.limit()) {
    //   [--- this ---)
    // [------ i --------)
    // Intersection is <this>, so difference yields the empty interval.
    return true;
  }
  lo = *this;  // No intersection.
  return false;
}

template <typename T>
template <typename Fn>
std::vector<Interval<T>> Interval<T>::Split(Fn next_si_limit_fn) const {
  T si_start = start();
  std::vector<Interval> sub_intervals;
  while (si_start < limit()) {
    T si_limit = std::min(next_si_limit_fn(si_start), limit());
    assert(si_start < si_limit);
    sub_intervals.push_back({si_start, si_limit});
    si_start = std::move(si_limit);
  }
  return sub_intervals;
}

}  // namespace gtl

namespace std {
template <typename T>
struct tuple_size<gtl::Interval<T>> : integral_constant<size_t, 2> {};
template <size_t I, typename T>
struct tuple_element<I, gtl::Interval<T>> {
  using type = T;
};
}  // namespace std

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_INTERVAL_H_
