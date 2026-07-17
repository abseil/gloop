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

// IntervalSet<T> is a data structure used to represent a sorted set of
// non-empty, non-adjacent, and mutually disjoint intervals. Mutations to an
// interval set preserve these properties, altering the set as needed. For
// example, adding [2, 3) to a set containing only [1, 2) would result in the
// set containing the single interval [1, 3).
//
// Supported operations include testing whether an Interval is contained in the
// IntervalSet, comparing two IntervalSets, and performing IntervalSet union,
// intersection, and difference.
//
// IntervalSet maintains the minimum number of entries needed to represent the
// set of underlying intervals. When the IntervalSet is modified (e.g. due to an
// Add operation), other interval entries may be coalesced, removed, or
// otherwise modified in order to maintain this invariant. The intervals are
// maintained in sorted order, by ascending start value.
//
// Note that as with gtl::Interval, the intervals in this library are half-open:
// [start, limit) in the usual notation. An interval is considered empty if
// start >= limit.
//
// T is required to be default- and copy-constructible, to have an assignment
// operator, and the full complement of comparison operators (<, <=, ==, !=,
// >=, >). (These requirements are inherited from Interval<T>). The IntervalSet
// implementation does not call Interval<T>::Length(), so a difference operator
// (operator-()) is not necessary.
//
// IntervalSet has constant-time move operations.
//
// This class is thread-compatible if T is thread-compatible. (See
// <link>).
//
// Examples:
//   IntervalSet<int> intervals;
//   intervals.Add(Interval<int>(10, 20));
//   intervals.Add(Interval<int>(30, 40));
//   // intervals contains [10,20) and [30,40).
//   intervals.Add(Interval<int>(15, 35));
//   // intervals has been coalesced. It now contains the single range [10,40).
//   EXPECT_EQ(1, intervals.size());
//   EXPECT_TRUE(intervals.Contains(Interval<int>(10, 40)));
//
//   intervals.Difference(Interval<int>(10, 20));
//   // intervals should now contain the single range [20, 40).
//   EXPECT_EQ(1, intervals.size());
//   EXPECT_TRUE(intervals.Contains(Interval<int>(20, 40)));

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_INTERVAL_SET_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_INTERVAL_SET_H_

#include <stddef.h>

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/btree_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gloop/util/gtl/container_logging.h"
#include "gloop/util/gtl/heterogeneous_lookup.h"
#include "gloop/util/gtl/interval.h"

namespace gtl {
namespace internal_interval_set {
struct UtilMemoryAccess;

// Maps `T` to a lightweight view type for heterogeneous range probes.
template <typename T>
struct IntervalViewTraits {
  using type = T;
};
template <>
struct IntervalViewTraits<std::string> {
  using type = absl::string_view;
};
template <>
struct IntervalViewTraits<absl::Cord> {
  using type = absl::string_view;
};

}  // namespace internal_interval_set

template <typename T, typename Allocator = std::allocator<Interval<T>>>
class IntervalSet {
 public:
  typedef Interval<T> value_type;

 private:
  static_assert(std::is_copy_assignable_v<T>,
                "IntervalSet requires a copy-assignable type");

  using AllowHeterogeneousLookup =
      internal_interval::AllowHeterogeneousLookup<T>;

  // KeyType for range lookups is derived from the dedicated view traits struct.
  using RangeKeyType =
      typename internal_interval_set::IntervalViewTraits<T>::type;

  template <class U>
  using key_arg = HeterogeneousLookupKeyArg<U, T, AllowHeterogeneousLookup>;

  // True if U1/U2 can construct RangeKeyType for transparent lookup. Uses const
  // lvalue references to support types like std::reference_wrapper.
  template <typename U1, typename U2>
  static constexpr bool kUseHeterogeneousRangeKeyType =
      gtl::IsTransparent<AllowHeterogeneousLookup>::value &&
      std::is_constructible_v<RangeKeyType, const U1&> &&
      std::is_constructible_v<RangeKeyType, const U2&>;

  // SFINAE constraint for 2-argument range lookup methods (Contains, Find).
  template <typename U1, typename U2>
  using EnableIfRangeConstructible =
      std::enable_if_t<kUseHeterogeneousRangeKeyType<U1, U2> ||
                       (std::is_constructible_v<T, const U1&> &&
                        std::is_constructible_v<T, const U2&>)>;

  struct IntervalLess {
    using is_transparent = void;
    bool operator()(const value_type& a, const value_type& b) const;

    // Transparent overload (https://abseil.io/tips/144) for an implicit point
    // interval `Interval<T>(a, a)`.
    template <typename U>
    bool operator()(const U& a, const value_type& b) const;

    // Transparent overload for an implicit point interval `Interval<T>(b, b)`.
    template <typename U>
    bool operator()(const value_type& a, const U& b) const;
  };
  using Set =
      absl::btree_set<value_type, IntervalLess,
                      typename std::allocator_traits<
                          Allocator>::template rebind_alloc<value_type>>;

  // A type implicitly convertible to `T`. Implicit conversion is required for
  // insertion methods (`Add`) that bind via copy-initialization.
  template <typename U,
            typename = std::enable_if_t<std::is_convertible_v<U, T>>>
  using ConvertibleToT = U;

  // A helper to create a temporary `T` from a `ConvertibleToT` only when
  // needed:
  //   MaybeT(std::forward<U>(arg))
  // will forward when `U`==`T` or `U`==`const T&`, and will create a temporary
  // `T` else, calling the appropriate constructor based on `U`.
  T&& MaybeT(T&& s) { return std::move(s); }
  const T& MaybeT(const T& s) { return s; }

 public:
  typedef typename Set::const_iterator const_iterator;
  typedef typename Set::const_reverse_iterator const_reverse_iterator;

  // Instantiates an empty IntervalSet.
  IntervalSet() = default;

  // Instantiates an IntervalSet containing exactly one initial half-open
  // interval [start, limit), unless the given interval is empty, in which case
  // the IntervalSet will be empty.
  explicit IntervalSet(const Interval<T>& interval) { InitImpl(interval); }
  explicit IntervalSet(Interval<T>&& interval) {
    InitImpl(std::move(interval));
  }

  // Instantiates an IntervalSet containing the half-open interval
  // [start, limit).
  template <typename U1 = T, typename U2 = T>
  IntervalSet(ConvertibleToT<U1>&& start, ConvertibleToT<U2>&& limit) {
    InitImpl(MaybeT(std::forward<U1>(start)), MaybeT(std::forward<U2>(limit)));
  }

  IntervalSet(std::initializer_list<value_type> il) { assign(il); }

  // Clears this IntervalSet.
  void Clear() { intervals_.clear(); }

  // Returns the number of disjoint intervals contained in this IntervalSet.
  size_t size() const { return intervals_.size(); }

  // Returns the smallest interval that contains all intervals in this
  // IntervalSet, or the empty interval if the set is empty.
  Interval<T> SpanningInterval() const;

  // Adds "interval" to this IntervalSet. Adding the empty interval has no
  // effect.
  void Add(const Interval<T>& interval) { AddImpl(interval); }
  void Add(Interval<T>&& interval) { AddImpl(std::move(interval)); }

  // Adds the interval [start, limit) to this IntervalSet. Adding the empty
  // interval has no effect.
  template <typename U1 = T, typename U2 = T>
  void Add(ConvertibleToT<U1>&& start, ConvertibleToT<U2>&& limit) {
    AddImpl(MaybeT(std::forward<U1>(start)), MaybeT(std::forward<U2>(limit)));
  }

  // DEPRECATED(kosak). Use Union() instead. This method merges all of the
  // values contained in "other" into this IntervalSet.
  void Add(const IntervalSet& other);

  // Returns true if this IntervalSet is empty.
  bool empty() const { return intervals_.empty(); }

  // Returns true if any interval in this IntervalSet contains the indicated
  // value.
  template <int&... ExplicitParameterBarrier, typename U = T>
  bool Contains(const key_arg<U>& value) const;

  // Returns true if there is some interval in this IntervalSet that wholly
  // contains the given interval. An interval O "wholly contains" a non-empty
  // interval I if O.Contains(p) is true for every p in I. This is the same
  // definition used by Interval<T>::Contains(). This method returns false on
  // the empty interval, due to a (perhaps unintuitive) convention inherited
  // from Interval<T>.
  // Example:
  //   Assume an IntervalSet containing the entries { [10,20), [30,40) }.
  //   Contains(Interval(15, 16)) returns true, because [10,20) contains
  //   [15,16). However, Contains(Interval(15, 35)) returns false.
  template <int&... ExplicitParameterBarrier, typename U = T>
  bool Contains(const Interval<key_arg<U>>& interval) const;

  // Returns true if for each interval in "other", there is some (possibly
  // different) interval in this IntervalSet which wholly contains it. See
  // Contains(const Interval<T>& interval) for the meaning of "wholly contains".
  // Perhaps unintuitively, this method returns false if "other" is the empty
  // set. The algorithmic complexity of this method is O(other.Size() *
  // log(this->Size())). The method could be rewritten to run in O(other.Size()
  // + this->Size()), and this alternative could be implemented as a free
  // function using the public API.
  bool Contains(const IntervalSet& other) const;

  // Returns true if there is some interval in this IntervalSet that wholly
  // contains the interval [start, limit). See Contains(const Interval<T>&).
  template <typename U1 = T, typename U2 = T,
            typename = EnableIfRangeConstructible<U1, U2>>
  bool Contains(const U1& start, const U2& limit) const {
    if constexpr (kUseHeterogeneousRangeKeyType<U1, U2>) {
      return Contains(Interval<RangeKeyType>(start, limit));
    } else {
      return Contains(Interval<T>(start, limit));
    }
  }

  // Returns true if for some interval in "other", there is some interval in
  // this IntervalSet that intersects with it. See Interval<T>::Intersects()
  // for the definition of interval intersection.
  bool Intersects(const IntervalSet& other) const;

  // Returns true if some interval in this IntervalSet intersects with the
  // interval [start, limit). See Interval<T>::Intersects() for the definition
  // of interval intersection.
  template <int&... ExplicitParameterBarrier, typename U = T>
  bool IntersectsInterval(const Interval<key_arg<U>>& interval) const;

  // Returns an iterator to the Interval<T> in the IntervalSet that contains the
  // given value. In other words, returns an iterator to the unique interval
  // [start, limit) in the IntervalSet that has the property start <= value <
  // limit. If there is no such interval, this method returns end().
  template <int&... ExplicitParameterBarrier, typename U = T>
  const_iterator Find(const key_arg<U>& value) const;

  // Returns an iterator to the Interval<T> in the IntervalSet that wholly
  // contains the given interval. In other words, returns an iterator to the
  // unique interval outer in the IntervalSet that has the property that
  // outer.Contains(interval). If there is no such interval, or if interval is
  // empty, returns end().
  template <int&... ExplicitParameterBarrier, typename U = T>
  const_iterator Find(const Interval<key_arg<U>>& probe) const;

  // Returns an iterator to the Interval<T> in the IntervalSet that wholly
  // contains [start, limit). In other words, returns an iterator to the unique
  // interval outer in the IntervalSet that has the property that
  // outer.Contains(Interval<T>(start, limit)). If there is no such interval, or
  // if interval is empty, returns end().
  template <typename U1 = T, typename U2 = T,
            typename = EnableIfRangeConstructible<U1, U2>>
  const_iterator Find(const U1& start, const U2& limit) const {
    if constexpr (kUseHeterogeneousRangeKeyType<U1, U2>) {
      return Find(Interval<RangeKeyType>(start, limit));
    } else {
      return Find(Interval<T>(start, limit));
    }
  }

  // Returns an iterator pointing to the first Interval<T> which contains or
  // starts after the given value.
  //
  // Example:
  //   [10, 20)  [30, 40)
  //   ^                    LowerBound(10)
  //   ^                    LowerBound(15)
  //             ^          LowerBound(20)
  //             ^          LowerBound(25)
  template <int&... ExplicitParameterBarrier, typename U = T>
  const_iterator LowerBound(const key_arg<U>& value) const;

  // Returns an iterator pointing to the first Interval<T> which starts after
  // the given value.
  //
  // Example:
  //   [10, 20)  [30, 40)
  //             ^          UpperBound(10)
  //             ^          UpperBound(15)
  //             ^          UpperBound(20)
  //             ^          UpperBound(25)
  template <int&... ExplicitParameterBarrier, typename U = T>
  const_iterator UpperBound(const key_arg<U>& value) const;

  // Returns true if every value within the passed interval is not Contained
  // within the IntervalSet. Equivalent to `!IntersectsInterval(interval)`.
  // Note that empty intervals are always considered disjoint from the
  // IntervalSet, consistent with `Interval::Contains`.
  template <int&... ExplicitParameterBarrier, typename U = T>
  bool IsDisjoint(const Interval<key_arg<U>>& interval) const;

  // Merges all the values contained in "other" into this IntervalSet.
  void Union(const IntervalSet& other);

  // Modifies this IntervalSet so that it contains only those values that are
  // currently present both in *this and in the Interval [start, limit).
  void Intersection(const T& start, const T& limit);

  // Modifies this IntervalSet so that it contains only those values that are
  // currently present both in *this and in the IntervalSet "other".
  void Intersection(const IntervalSet& other);

  // Returns the result of calling `Intersection` on a copy of `*this`.
  IntervalSet GetIntersection(const Interval<T>& other) const {
    auto copy = *this;
    copy.Intersection(IntervalSet<T>(other));
    return copy;
  }

  // Returns the result of calling `Intersection` on a copy of `*this`.
  IntervalSet GetIntersection(const T& start, const T& limit) const {
    auto copy = *this;
    copy.Intersection(start, limit);
    return copy;
  }

  // Returns the result of calling `Intersection` on a copy of `*this`.
  IntervalSet GetIntersection(const IntervalSet& other) const {
    auto copy = *this;
    copy.Intersection(other);
    return copy;
  }

  // Mutates this IntervalSet so that it contains only those values that are
  // currently in *this but not in "interval".
  void Difference(const Interval<T>& interval);

  // Mutates this IntervalSet so that it contains only those values that are
  // currently in *this but not in the interval [start, limit).
  void Difference(const T& start, const T& limit);

  // Mutates this IntervalSet so that it contains only those values that are
  // currently in *this but not in the IntervalSet "other".
  void Difference(const IntervalSet& other);

  // Mutates this IntervalSet so that it contains only those values that are
  // in [start, limit) but not currently in *this.
  void Complement(const T& start, const T& limit);

  // Returns the result of calling `Difference` on a copy of `*this`.
  IntervalSet Without(const Interval<T>& interval) const {
    auto copy = *this;
    copy.Difference(interval);
    return copy;
  }

  // Returns the result of calling `Difference` on a copy of `*this`.
  IntervalSet Without(const IntervalSet& other) const {
    auto copy = *this;
    copy.Difference(other);
    return copy;
  }

  // IntervalSet's begin() iterator. The invariants of IntervalSet guarantee
  // that for each entry e in the set, e.start() < e.limit() (because the
  // entries are non-empty) and for each entry f that appears later in the set,
  // e.limit() < f.start() (because the entries are ordered, pairwise-disjoint,
  // and non-adjacent). Modifications to this IntervalSet invalidate these
  // iterators.
  const_iterator begin() const { return intervals_.begin(); }

  // IntervalSet's end() iterator.
  const_iterator end() const { return intervals_.end(); }

  // IntervalSet's rbegin() and rend() iterators. Iterator invalidation
  // semantics are the same as those for begin() / end().
  const_reverse_iterator rbegin() const { return intervals_.rbegin(); }

  const_reverse_iterator rend() const { return intervals_.rend(); }

  template <typename Iter>
  void assign(Iter first, Iter last) {
    Clear();
    for (; first != last; ++first) Add(*first);
  }

  void assign(std::initializer_list<value_type> il) {
    assign(il.begin(), il.end());
  }

  // Removes the given interval from the interval set and returns an iterator to
  // the remaining next interval.
  const_iterator erase(const_iterator pos) { return intervals_.erase(pos); }

  // Returns a human-readable representation of this set. This will typically be
  // (though is not guaranteed to be) of the form
  //   "[a1, b1) [a2, b2) ... [an, bn)"
  // where the intervals are in the same order as given by traversal from
  // begin() to end(). This representation is intended for human consumption;
  // computer programs should not rely on the output being in exactly this form.
  std::string ToString() const;

  IntervalSet& operator=(std::initializer_list<value_type> il) {
    assign(il.begin(), il.end());
    return *this;
  }

  // Swap this IntervalSet with *other. This is a constant-time operation.
  void Swap(IntervalSet* other) { intervals_.swap(other->intervals_); }

  // <link>
  template <typename H>
  friend H AbslHashValue(H h, const IntervalSet& set) {
    for (const auto& interval : set.intervals_) {
      h = H::combine(std::move(h), interval.start(), interval.limit());
    }
    return H::combine(std::move(h), set.size());
  }

  friend bool operator==(const IntervalSet& a, const IntervalSet& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), NonemptyIntervalEq());
  }

  friend bool operator!=(const IntervalSet& a, const IntervalSet& b) {
    return !(a == b);
  }

 private:
  friend struct internal_interval_set::UtilMemoryAccess;

  // Simple member-wise equality, since all intervals are non-empty.
  struct NonemptyIntervalEq {
    bool operator()(const value_type& a, const value_type& b) const {
      return a.start() == b.start() && a.limit() == b.limit();
    }
  };

  // Removes overlapping ranges and coalesces adjacent intervals as needed.
  // Process intervals starting at begin, and ending at the end of the container
  // or when should_continue(interval) is false.
  template <typename Func>
  void Compact(const_iterator begin, Func should_continue);

  // Returns true if an interval is empty.
  bool IsEmpty(const Interval<T>& interval) { return interval.Empty(); }
  bool IsEmpty(const T& start, const T& limit) { return start >= limit; }

  // The common implementation for both `const&` and `&&` overloads of `Add`.
  template <typename... Args>
  void AddImpl(Args&&... args);

  // Same as AddImpl, when the set is empty.
  template <typename... Args>
  void InitImpl(Args&&... args);

  // Returns true if this set is valid (i.e. all intervals in it are non-empty,
  // non-adjacent, and mutually disjoint). Currently this is used as an
  // integrity check by the Intersection() and Difference() methods, but is only
  // invoked for debug builds (via DCHECK).
  bool Valid() const;

  // Finds the first interval that potentially intersects 'other'.
  const_iterator FindIntersectionCandidate(const IntervalSet& other) const;

  // Finds the first interval that potentially intersects 'interval'.
  const_iterator FindIntersectionCandidate(const Interval<T>& interval) const;

  // Helper for Intersection() and Difference(): Finds the next pair of
  // intervals from 'x' and 'y' that intersect. 'mine' is an iterator
  // over x->intervals_. 'theirs' is an iterator over y.intervals_. 'mine'
  // and 'theirs' are advanced until an intersecting pair is found.
  // Non-intersecting intervals (aka "holes") from x->intervals_ can be
  // optionally erased by "on_hole".
  template <typename X, typename Func>
  static bool FindNextIntersectingPairImpl(X* x, const IntervalSet& y,
                                           const_iterator* mine,
                                           const_iterator* theirs,
                                           Func on_hole);

  // The variant of the above method that doesn't mutate this IntervalSet.
  bool FindNextIntersectingPair(const IntervalSet& other, const_iterator* mine,
                                const_iterator* theirs) const {
    return FindNextIntersectingPairImpl(this, other, mine, theirs,
                                        [](const IntervalSet*, const_iterator,
                                           const_iterator to) { return to; });
  }

  // The variant of the above method that mutates this IntervalSet by erasing
  // holes.
  bool FindNextIntersectingPairAndEraseHoles(const IntervalSet& other,
                                             const_iterator* mine,
                                             const_iterator* theirs) {
    return FindNextIntersectingPairImpl(
        this, other, mine, theirs,
        [](IntervalSet* x, const_iterator from, const_iterator to) {
          return x->intervals_.erase(from, to);
        });
  }

  // Replaces the interval pointed by `it` with a new content. Requires that its
  // position in the container doesn't change because of this operation. The
  // iterator to the element remains valid and usable.
  void ReplaceInterval(const_iterator& it, const Interval<T>& interval) {
    // TODO: we could potentially add an `intervals_.replace(it,
    // interval)` API to btree_set. Then we could avoid all the rebalancing.
    it = intervals_.erase(it);
    it = intervals_.insert(it, interval);
  }

  // The representation for the intervals. The intervals in this set are
  // non-empty, pairwise-disjoint, non-adjacent and ordered in ascending order
  // by start().
  Set intervals_;
};

template <typename T, typename Allocator>
auto operator<<(std::ostream& out, const IntervalSet<T, Allocator>& seq)
    -> decltype(out << *seq.begin()) {
  gtl::LogRangeToStream(out, seq.begin(), seq.end(), gtl::LogLegacy());
  return out;
}

template <typename T, typename Allocator>
void swap(IntervalSet<T, Allocator>& x, IntervalSet<T, Allocator>& y) noexcept;

//==============================================================================
// Implementation details: Clients can stop reading here.

template <typename T, typename Allocator>
Interval<T> IntervalSet<T, Allocator>::SpanningInterval() const {
  Interval<T> result;
  if (!intervals_.empty()) {
    result.set_start(intervals_.begin()->start());
    result.set_limit(intervals_.rbegin()->limit());
  }
  return result;
}

template <typename T, typename Allocator>
template <typename... Args>
void IntervalSet<T, Allocator>::AddImpl(Args&&... args) {
  if (IsEmpty(args...)) return;
  auto [it, inserted] = intervals_.emplace(std::forward<Args>(args)...);
  if (!inserted) {
    // This interval already exists.
    return;
  }
  // Determine the minimal range that will have to be compacted.  We know that
  // the IntervalSet was valid before the addition of the interval, so only
  // need to start with the interval itself (although Compact takes an open
  // range so begin needs to be the interval to the left).  We don't know how
  // many ranges this interval may cover, so we need to find the appropriate
  // interval to end with on the right.
  const T& limit = it->limit();
  if (it != intervals_.begin()) --it;
  const auto end = intervals_.upper_bound(limit);
  if (end == intervals_.end()) {
    Compact(it, [](const Interval<T>&) { return true; });
  } else {
    // We must copy here because the `end` iterator will be invalidated when
    // intervals_ is mutated during Compact().
    const T actual_end = end->start();  // NOLINT
    Compact(it, [&actual_end](const Interval<T>& current) {
      return IntervalLess()(current, actual_end);
    });
  }
}

template <typename T, typename Allocator>
template <typename... Args>
void IntervalSet<T, Allocator>::InitImpl(Args&&... args) {
  if (IsEmpty(args...)) return;
  intervals_.emplace(std::forward<Args>(args)...);
}

template <typename T, typename Allocator>
void IntervalSet<T, Allocator>::Add(const IntervalSet& other) {
  for (const_iterator it = other.begin(); it != other.end(); ++it) {
    Add(*it);
  }
}

template <typename T, typename Allocator>
template <int&..., typename U>
bool IntervalSet<T, Allocator>::Contains(const key_arg<U>& value) const {
  // Find the first interval with start() > value, then move back one step
  auto it = intervals_.upper_bound(value);
  if (it == intervals_.begin()) return false;
  --it;
  return it->contains(value);
}

template <typename T, typename Allocator>
template <int&..., typename U>
bool IntervalSet<T, Allocator>::Contains(
    const Interval<key_arg<U>>& interval) const {
  if (interval.empty()) return false;
  // Find the first interval with start() > value, then move back one step.
  const_iterator it = intervals_.upper_bound(interval.start());
  if (it == intervals_.begin()) return false;
  --it;
  return it->contains(interval);
}

template <typename T, typename Allocator>
bool IntervalSet<T, Allocator>::Contains(
    const IntervalSet<T, Allocator>& other) const {
  // !SpanningInterval().Contains(other.SpanningInterval())
  if (empty() || other.empty() ||
      intervals_.begin()->start() > other.intervals_.begin()->start() ||
      intervals_.rbegin()->limit() < other.intervals_.rbegin()->limit()) {
    return false;
  }

  for (const_iterator i = other.begin(); i != other.end(); ++i) {
    // If we don't contain the interval, can return false now.
    if (!Contains(*i)) {
      return false;
    }
  }
  return true;
}

// This method finds the interval that Contains() "value", if such an interval
// exists in the IntervalSet. The way this is done is to locate the "candidate
// interval", the only interval that could *possibly* contain value, and test it
// using Contains(). The candidate interval is the interval with the largest
// start() having start() <= value.
//
// Determining the candidate interval takes a couple of steps. First, since the
// underlying btree_set stores intervals, not values, we need to create a "probe
// interval" suitable for use as a search key. The probe interval used is
// [value, value). Now we can restate the problem as finding the largest
// interval in the IntervalSet that is <= the probe interval.
//
// This restatement only works if the set's comparator behaves in a certain way.
// In particular it needs to order first by ascending start(), and then by
// descending limit(). The comparator used by this library is defined in exactly
// this way. To see why descending limit() is required, consider the following
// example. Assume an IntervalSet containing these intervals:
//
//   [0, 5)  [10, 20)  [50, 60)
//
// Consider searching for the value 15. The probe interval [15, 15) is created,
// and [10, 20) is identified as the largest interval in the set <= the probe
// interval. This is the correct interval needed for the Contains() test, which
// will then return true.
//
// Now consider searching for the value 30. The probe interval [30, 30) is
// created, and again [10, 20] is identified as the largest interval <= the
// probe interval. This is again the correct interval needed for the Contains()
// test, which in this case returns false.
//
// Finally, consider searching for the value 10. The probe interval [10, 10) is
// created. Here the ordering relationship between [10, 10) and [10, 20) becomes
// vitally important. If [10, 10) were to come before [10, 20), then [0, 5)
// would be the largest interval <= the probe, leading to the wrong choice of
// interval for the Contains() test. Therefore [10, 10) needs to come after
// [10, 20). The simplest way to make this work in the general case is to order
// by ascending start() but descending limit(). In this ordering, the empty
// interval is larger than any non-empty interval with the same start(). The
// comparator used by this library is careful to induce this ordering.
//
// Another detail involves the choice of which method to use to try to
// find the candidate interval. The most appropriate entry point is
// upper_bound(), which finds the smallest interval which is > the probe
// interval. The semantics of upper_bound() are slightly different from what we
// want (namely, to find the largest interval which is <= the probe interval)
// but they are close enough; the interval found by upper_bound() will always be
// one step past the interval we are looking for (if it exists) or at begin()
// (if it does not). Getting to the proper interval is a simple matter of
// decrementing the iterator.
template <typename T, typename Allocator>
template <int&..., typename U>
typename IntervalSet<T, Allocator>::const_iterator
IntervalSet<T, Allocator>::Find(const key_arg<U>& value) const {
  auto it = intervals_.upper_bound(value);
  if (it == intervals_.begin()) return intervals_.end();
  --it;
  if (it->contains(value))
    return it;
  else
    return intervals_.end();
}

// This method finds the interval that Contains() the interval "probe", if such
// an interval exists in the IntervalSet. The way this is done is to locate the
// "candidate interval", the only interval that could *possibly* contain
// "probe", and test it using Contains(). The candidate interval is the largest
// interval that is <= the probe interval.
//
// The search for the candidate interval only works if the comparator used
// behaves in a certain way. In particular it needs to order first by ascending
// start(), and then by descending limit(). The comparator used by this library
// is defined in exactly this way. To see why descending limit() is required,
// consider the following example. Assume an IntervalSet containing these
// intervals:
//
//   [0, 5)  [10, 20)  [50, 60)
//
// Consider searching for the probe [15, 17). [10, 20) is the largest interval
// in the set which is <= the probe interval. This is the correct interval
// needed for the Contains() test, which will then return true, because [10, 20)
// contains [15, 17).
//
// Now consider searching for the probe [30, 32). Again [10, 20] is the largest
// interval <= the probe interval. This is again the correct interval needed for
// the Contains() test, which in this case returns false, because [10, 20) does
// not contain [30, 32).
//
// Finally, consider searching for the probe [10, 12). Here the ordering
// relationship between [10, 12) and [10, 20) becomes vitally important. If
// [10, 12) were to come before [10, 20), then [0, 5) would be the largest
// interval <= the probe, leading to the wrong choice of interval for the
// Contains() test. Therefore [10, 12) needs to come after [10, 20). The
// simplest way to make this work in the general case is to order by ascending
// start() but descending limit(). In this ordering, given two intervals with
// the same start(), the wider one goes before the narrower one. The comparator
// used by this library is careful to induce this ordering.
//
// Another detail involves the choice of which std::set method to use to try to
// find the candidate interval. The most appropriate entry point is
// set::upper_bound(), which finds the smallest interval which is > the probe
// interval. The semantics of upper_bound() are slightly different from what we
// want (namely, to find the largest interval which is <= the probe interval)
// but they are close enough; the interval found by upper_bound() will always be
// one step past the interval we are looking for (if it exists) or at begin()
// (if it does not). Getting to the proper interval is a simple matter of
// decrementing the iterator.
template <typename T, typename Allocator>
template <int&..., typename U>
typename IntervalSet<T, Allocator>::const_iterator
IntervalSet<T, Allocator>::Find(const Interval<key_arg<U>>& probe) const {
  if (probe.empty()) return intervals_.end();
  const_iterator it = intervals_.upper_bound(probe.start());
  if (it == intervals_.begin()) return intervals_.end();
  --it;
  if (it->contains(probe))
    return it;
  else
    return intervals_.end();
}

template <typename T, typename Allocator>
template <int&..., typename U>
typename IntervalSet<T, Allocator>::const_iterator
IntervalSet<T, Allocator>::LowerBound(const key_arg<U>& value) const {
  const_iterator it = intervals_.lower_bound(value);
  if (it == intervals_.begin()) {
    return it;
  }

  // The previous intervals_.lower_bound() checking is essentially based on
  // interval.start(), so we need to check whether the `value` is contained in
  // the previous interval.
  --it;
  if (it->contains(value)) {
    return it;
  } else {
    return ++it;
  }
}

template <typename T, typename Allocator>
template <int&..., typename U>
typename IntervalSet<T, Allocator>::const_iterator
IntervalSet<T, Allocator>::UpperBound(const key_arg<U>& value) const {
  return intervals_.upper_bound(value);
}

template <typename T, typename Allocator>
template <int&..., typename U>
bool IntervalSet<T, Allocator>::IsDisjoint(
    const Interval<key_arg<U>>& interval) const {
  if (interval.empty()) return true;
  // Find the first interval with start() > interval.start()
  const_iterator it = intervals_.upper_bound(interval.start());
  if (it != intervals_.end() && interval.limit() > it->start()) return false;
  if (it == intervals_.begin()) return true;
  --it;
  return it->limit() <= interval.start();
}

template <typename T, typename Allocator>
void IntervalSet<T, Allocator>::Union(const IntervalSet& other) {
  intervals_.insert(other.begin(), other.end());
  Compact(intervals_.begin(), [](const Interval<T>&) { return true; });
}

template <typename T, typename Allocator>
typename IntervalSet<T, Allocator>::const_iterator
IntervalSet<T, Allocator>::FindIntersectionCandidate(
    const IntervalSet& other) const {
  return FindIntersectionCandidate(*other.intervals_.begin());
}

template <typename T, typename Allocator>
typename IntervalSet<T, Allocator>::const_iterator
IntervalSet<T, Allocator>::FindIntersectionCandidate(
    const Interval<T>& interval) const {
  // Use upper_bound to efficiently find the first interval in intervals_
  // where start() is greater than interval.start().  If the result
  // isn't the beginning of intervals_ then move backwards one interval since
  // the interval before it is the first candidate where limit() may be
  // greater than interval.start().
  // In other words, no interval before that can possibly intersect with any
  // of other.intervals_.
  const_iterator mine = intervals_.upper_bound(interval);
  if (mine != intervals_.begin()) {
    --mine;
  }
  return mine;
}

template <typename T, typename Allocator>
template <typename X, typename Func>
bool IntervalSet<T, Allocator>::FindNextIntersectingPairImpl(
    X* x, const IntervalSet& y, const_iterator* mine, const_iterator* theirs,
    Func on_hole) {
  CHECK(x != nullptr);
  if ((*mine == x->intervals_.end()) || (*theirs == y.intervals_.end())) {
    return false;
  }
  while (!(**mine).Intersects(**theirs)) {
    const_iterator erase_first = *mine;
    // Skip over intervals in 'mine' that don't reach 'theirs'.
    while (*mine != x->intervals_.end() &&
           (**mine).limit() <= (**theirs).start()) {
      ++(*mine);
    }
    *mine = on_hole(x, erase_first, *mine);
    // We're done if the end of intervals_ is reached.
    if (*mine == x->intervals_.end()) {
      return false;
    }
    // Skip over intervals 'theirs' that don't reach 'mine'.
    while (*theirs != y.intervals_.end() &&
           (**theirs).limit() <= (**mine).start()) {
      ++(*theirs);
    }
    // If the end of other.intervals_ is reached, we're done.
    if (*theirs == y.intervals_.end()) {
      *mine = on_hole(x, *mine, x->intervals_.end());
      return false;
    }
  }
  return true;
}

template <typename T, typename Allocator>
void IntervalSet<T, Allocator>::Intersection(const T& start, const T& limit) {
  Intersection(IntervalSet<T>({{start, limit}}));
}

template <typename T, typename Allocator>
void IntervalSet<T, Allocator>::Intersection(const IntervalSet& other) {
  if (this == &other) return;

  // !SpanningInterval().Intersects(other.SpanningInterval())
  if (empty() || other.empty() ||
      intervals_.begin()->start() >= other.intervals_.rbegin()->limit() ||
      intervals_.rbegin()->limit() <= other.intervals_.begin()->start()) {
    intervals_.clear();
    return;
  }

  const_iterator mine = FindIntersectionCandidate(other);
  // Remove any intervals that cannot possibly intersect with other.intervals_.
  mine = intervals_.erase(intervals_.begin(), mine);
  const_iterator theirs = other.FindIntersectionCandidate(*this);

  while (FindNextIntersectingPairAndEraseHoles(other, &mine, &theirs)) {
    // OK, *mine and *theirs intersect.  Now, we find the largest
    // span of intervals in other (starting at theirs) - say [a..b]
    // - that intersect *mine, and we replace *mine with (*mine
    // intersect x) for all x in [a..b] Note that subsequent
    // intervals in this can't intersect any intervals in [a..b) --
    // they may only intersect b or subsequent intervals in other.
    Interval<T> i(*mine);
    intervals_.erase(mine);
    mine = intervals_.end();
    Interval<T> intersection;
    while (theirs != other.intervals_.end() &&
           i.Intersects(*theirs, &intersection)) {
      mine = intervals_.insert(mine, intersection);
      ++theirs;
    }
    DCHECK(mine != intervals_.end());
    --theirs;
    ++mine;
  }
  DCHECK(Valid());
}

template <typename T, typename Allocator>
bool IntervalSet<T, Allocator>::Intersects(const IntervalSet& other) const {
  // !SpanningInterval().Intersects(other.SpanningInterval())
  if (empty() || other.empty() ||
      intervals_.begin()->start() >= other.intervals_.rbegin()->limit() ||
      intervals_.rbegin()->limit() <= other.intervals_.begin()->start()) {
    return false;
  }

  const_iterator mine = FindIntersectionCandidate(other);
  if (mine == intervals_.end()) {
    return false;
  }
  const_iterator theirs = other.FindIntersectionCandidate(*mine);

  return FindNextIntersectingPair(other, &mine, &theirs);
}

template <typename T, typename Allocator>
template <int&..., typename U>
bool IntervalSet<T, Allocator>::IntersectsInterval(
    const Interval<key_arg<U>>& interval) const {
  return !IsDisjoint(interval);
}

template <typename T, typename Allocator>
void IntervalSet<T, Allocator>::Difference(const Interval<T>& interval) {
  if (!SpanningInterval().Intersects(interval)) {
    return;
  }
  const_iterator it = FindIntersectionCandidate(interval);
  if (it == intervals_.end()) {
    return;
  }
  if (&*it == &interval) {
    // Handle the case when `interval` is itself part of this interval set.
    intervals_.erase(it);
    return;
  }
  // Handle the first not fully covered candidate specially.
  if (it->start() < interval.start()) {
    Interval<T> lo, hi;
    if (it->Difference(interval, &lo, &hi)) {
      // If there was an intersection, the lower interval will surely be there.
      DCHECK(!lo.Empty());
      // Shorten the previous interval.
      ReplaceInterval(it, lo);
      if (!hi.Empty()) {
        // Handle the case where `interval` was fully nested in the first
        // candidate. In such case we can return early.
        it = intervals_.insert(it, hi);
        return;
      }
    }
    ++it;
  }
  // Erase all fully covered intervals.
  while (it != intervals_.end() && it->limit() <= interval.limit()) {
    it = intervals_.erase(it);
  }
  // Shorten the partially covered last interval.
  if (it != intervals_.end() && it->start() < interval.limit()) {
    ReplaceInterval(it, {interval.limit(), it->limit()});
  }
}

template <typename T, typename Allocator>
void IntervalSet<T, Allocator>::Difference(const T& start, const T& limit) {
  Difference(Interval<T>(start, limit));
}

template <typename T, typename Allocator>
void IntervalSet<T, Allocator>::Difference(const IntervalSet& other) {
  if (this == &other) {
    intervals_.clear();
    return;
  }

  // !SpanningInterval().Intersects(other.SpanningInterval())
  if (empty() || other.empty() ||
      intervals_.begin()->start() >= other.intervals_.rbegin()->limit() ||
      intervals_.rbegin()->limit() <= other.intervals_.begin()->start()) {
    return;
  }

  const_iterator mine = FindIntersectionCandidate(other);
  // If no interval in mine reaches the first interval of theirs, we're done.
  if (mine == intervals_.end()) {
    return;
  }
  const_iterator theirs = other.FindIntersectionCandidate(*this);
  while (FindNextIntersectingPair(other, &mine, &theirs)) {
    // At this point *mine and *theirs overlap.  Replace mine with the possibly
    // two intervals that are the difference between mine and theirs.
    Interval<T> lo, hi;
    mine->Difference(*theirs, &lo, &hi);
    if (!hi.Empty()) {
      // Skip incrementing `mine`, because `hi` may need further intersections.
      ReplaceInterval(mine, hi);
      // The lower interval (if any) is inserted right before the higher.
      if (!lo.Empty()) mine = intervals_.insert(mine, lo);
      continue;
    }
    if (!lo.Empty()) {
      // We have a low end only, which can't intersect anything else.
      ReplaceInterval(mine, lo);
      ++mine;
      continue;
    }
    // Interval was fully subtracted.
    mine = intervals_.erase(mine);
  }
  DCHECK(Valid());
}

template <typename T, typename Allocator>
void IntervalSet<T, Allocator>::Complement(const T& start, const T& limit) {
  IntervalSet<T, Allocator> span(start, limit);
  span.Difference(*this);
  intervals_.swap(span.intervals_);
}

template <typename T, typename Allocator>
std::string IntervalSet<T, Allocator>::ToString() const {
  std::ostringstream os;
  os << *this;
  return os.str();
}

// This method compacts the IntervalSet, merging pairs of overlapping intervals
// into a single interval. In the steady state, the IntervalSet does not contain
// any such pairs. However, the way the Union() and Add() methods work is to
// temporarily put the IntervalSet into such a state and then to call Compact()
// to "fix it up" so that it is no longer in that state.
//
// Compact() needs the interval set to allow two intervals [a,b) and [a,c)
// (having the same start() but different limit()) to briefly coexist in the set
// at the same time, and be adjacent to each other, so that they can be
// efficiently located and merged into a single interval. This state would be
// impossible with a comparator which only looked at start(), as such a
// comparator would consider such pairs equal. Fortunately, the comparator used
// by IntervalSet does exactly what is needed, ordering first by ascending
// start(), then by descending limit().
template <typename T, typename Allocator>
template <typename Func>
void IntervalSet<T, Allocator>::Compact(const_iterator begin,
                                        Func should_continue) {
  if (begin == intervals_.end()) return;
  const_iterator prev = begin, it = std::next(begin);
  while (it != intervals_.end() && should_continue(*it)) {
    if (prev->limit() >= it->start()) {
      if (prev->limit() < it->limit()) {
        // Overlapping / coalesced range; merge into the preceding interval.
        ReplaceInterval(prev, Interval<T>(prev->start(), it->limit()));
        it = std::next(prev);
      }
      it = intervals_.erase(it);
      prev = std::prev(it);
    } else {
      prev = it++;
    }
  }
}

template <typename T, typename Allocator>
bool IntervalSet<T, Allocator>::Valid() const {
  const_iterator prev = end();
  for (const_iterator it = begin(); it != end(); ++it) {
    // invalid or empty interval.
    if (it->start() >= it->limit()) return false;
    // Not sorted, not disjoint, or adjacent.
    if (prev != end() && prev->limit() >= it->start()) return false;
    prev = it;
  }
  return true;
}

template <typename T, typename Allocator>
void swap(IntervalSet<T, Allocator>& x, IntervalSet<T, Allocator>& y) noexcept {
  x.Swap(&y);
}

// This comparator orders intervals first by ascending start() and then by
// descending limit(). Readers who are satisfied with that explanation can stop
// reading here. The remainder of this comment is for the benefit of future
// maintainers of this library.
//
// The reason for this ordering is that this comparator has to serve two
// masters. First, it has to maintain the intervals in its internal set in the
// order that clients expect to see them. Clients see these intervals via the
// iterators provided by begin()/end() or as a result of invoking Get(). For
// this reason, the comparator orders intervals by ascending start().
//
// If client iteration were the only consideration, then ordering by ascending
// start() would be good enough. This is because the intervals in the
// IntervalSet are non-empty, non-adjacent, and mutually disjoint; such
// intervals happen to always have disjoint start() values, so such a comparator
// would never even have to look at limit() in order to work correctly for this
// class.
//
// However, in addition to ordering by ascending start(), this comparator also
// has a second responsibility: satisfying the special needs of this library's
// peculiar internal implementation. These needs require the comparator to order
// first by ascending start() and then by descending limit(). The best way to
// understand why this is so is to check out the comments associated with the
// Find() and Compact() methods.
template <typename T, typename Allocator>
bool IntervalSet<T, Allocator>::IntervalLess::operator()(
    const value_type& a, const value_type& b) const {
  return a.start() < b.start() ||
         (a.start() == b.start() && a.limit() > b.limit());
}

template <typename T, typename Allocator>
template <typename U>
bool IntervalSet<T, Allocator>::IntervalLess::operator()(
    const U& a, const value_type& b) const {
  // This is the same as above, with invariants folded.
  // "a == b.start()" => b.start() > b.limit(), which is false according to the
  // invariant "b.start <= b.limit", therefore the right part of the `||` is
  // false.
  return a < b.start();
}

template <typename T, typename Allocator>
template <typename U>
bool IntervalSet<T, Allocator>::IntervalLess::operator()(const value_type& a,
                                                         const U& b) const {
  return a.start() < b || (a.start() == b && a.limit() > b);
}

namespace internal_interval_set {

// UnionImpl constructs an IntervalSet which is the result of calling
// result.Union(...) for each of the provided IntervalSets.
template <typename T, typename Allocator>
IntervalSet<T, Allocator> UnionImpl(
    absl::Span<const IntervalSet<T, Allocator>* const> input_sets) {
  using const_iterator = typename IntervalSet<T, Allocator>::const_iterator;
  using HeapElement = std::pair<const_iterator, const_iterator>;

  // Construct a max-heap of the begin and end iterators for each node, then
  // processes each Interval in order, combining into the final IntervalSet.
  std::vector<HeapElement> heap;
  for (const auto* interval_set : input_sets) {
    auto begin = interval_set->begin();
    auto end = interval_set->end();
    if (begin != end) {
      heap.emplace_back(begin, end);
    }
  }

  IntervalSet<T, Allocator> result;
  if (heap.empty()) {
    return result;
  }

  // min_heap_compare constructs a min-heap based on the Interval.
  const auto min_heap_compare = [](const HeapElement& a, const HeapElement& b) {
    return *b.first < *a.first;
  };
  absl::c_make_heap(heap, min_heap_compare);

  Interval<T> prior = *heap[0].first;
  while (!heap.empty()) {
    // Get the next element.
    Interval<T> current = *heap[0].first;

    // Adjust the heap.
    // 1. std::pop_heap moves element[0] to .back().
    // 2. Remove one element from .back().
    // 3. If the entry is now empty (first == last), then we remove
    // it, otherwise use std::push_heap, which moves .back() into the
    // correct position in the heap.
    absl::c_pop_heap(heap, min_heap_compare);
    heap.back().first++;
    if (heap.back().first == heap.back().second) {
      heap.pop_back();
    } else {
      absl::c_push_heap(heap, min_heap_compare);
    }

    // If the current element either intersects or is adjacent
    // to the prior element, then we can merge them. Otherwise
    // we insert the prior element and start again.
    if (current.start() <= prior.limit() && current.limit() >= prior.start()) {
      prior = Interval<T>(std::min(prior.start(), current.start()),
                          std::max(prior.limit(), current.limit()));
    } else {
      result.Add(prior);
      prior = current;
    }
  }
  result.Add(prior);  // Add the last interval.
  return result;
}

// For use in mem-usage-util.h only.
struct UtilMemoryAccess {
  template <typename IntervalSet>
  static size_t BytesUsed(const IntervalSet& set) {
    return sizeof(set) + set.intervals_.bytes_used() - sizeof(set.intervals_);
  }
};

}  // namespace internal_interval_set

template <typename T, typename Allocator>
IntervalSet<T, Allocator> IntervalSetUnion(
    std::initializer_list<const IntervalSet<T, Allocator>* const> il) {
  return internal_interval_set::UnionImpl(
      absl::Span<const IntervalSet<T, Allocator>* const>(il.begin(),
                                                         il.size()));
}

template <typename Container>
auto IntervalSetUnion(const Container& c) {
  return internal_interval_set::UnionImpl(absl::MakeConstSpan(c));
}

// An optimized helper to add integers to an interval set.  This is more
// efficient than adding each integer as a range [i, i+1) when the input
// contains contiguous ranges of adjacent integers.
//
// Note: If the input container contains numeric_limits<T>::max() it will
// be omitted from the interval set because it is not representable in
// the interval set.
template <typename Container, typename T>
void AddIntsToIntervalSet(const Container& ints_arg,
                          ::gtl::IntervalSet<T>& set) {
  static_assert(std::is_integral_v<T>, "The type T must be an integral type.");

  static_assert(std::is_same_v<T, typename Container::value_type>,
                "Input container and interval must contain the same type.");

  auto start = ints_arg.begin();
  auto end = ints_arg.end();

  // Skip over invalid values.
  while (start != end) {
    if (*start != std::numeric_limits<T>::max()) break;
    ++start;
  }
  if (start == end) return;
  // We have at least one valid item to add.
  auto last_start = start;
  T last_value = *last_start;
  auto i = last_start;
  ++i;
  for (; i != end; ++i) {
    const T value = *i;
    // Skip values that can never be represented in the final output.
    if (value == std::numeric_limits<T>::max()) {
      continue;
    }
    if (last_value + 1 != value) {
      const T last_start_value = *last_start;
      set.Add(last_start_value, last_value + 1);
      last_start = i;
    }
    last_value = value;
  }
  const T last_start_value = *last_start;
  set.Add(last_start_value, last_value + 1);
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_INTERVAL_SET_H_
