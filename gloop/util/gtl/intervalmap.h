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

// A map from interval to value.  For example, a map from string
// ranges to a value per range.  None of the kept intervals are
// overlapping.  I.e., there is at most one value per point.
//
// Keys can be strings.
//
// Ranges are always specified as inclusive on the small end and
// exclusive on the large end.  I.e., [start,limit).
//
// This class differs from util/intervaltree in that the latter
// can keep track of overlapping ranges, but as a result has a more
// complicated implementation and a harder to use interface than
// necessary.
//
// Iterators have one surprising limitation: passing iter->start or
// iter->limit keys to non-const IntervalMap methods produces
// undefined behavior.  This is unlike STL iterators, which are always
// valid as arguments to their containers despite becoming
// subsequently invalid.

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_INTERVALMAP_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_INTERVALMAP_H_

#include <functional>
#include <memory>
#include <set>
#include <type_traits>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/container/internal/compressed_tuple.h"
#include "absl/log/check.h"
#include "gloop/util/gtl/heterogeneous_lookup.h"

namespace gtl {

namespace internal_intervalmap {

// TODO: make Entry private after callers are updated.
template <typename K, typename V>
struct Entry {
  Entry() = default;

  template <class K1 = const K&, class K2 = const K&, class V2 = const V&,
            class = std::enable_if_t<std::is_convertible_v<K1&&, K>>,
            class = std::enable_if_t<std::is_convertible_v<K2&&, K>>,
            class = std::enable_if_t<std::is_convertible_v<V2&&, V>>>
  Entry(K1&& limit, K2&& start, V2&& value)
      : limit(std::forward<K1>(limit)),
        start(std::forward<K2>(start)),
        value(std::forward<V2>(value)) {}

  K limit;
  K start;
  V value;
};

// Helper struct for comparing `Entry`s. Since `Comparison` works on
// keys, the helper adapts it to compare the `limit`s of two `Entry`s.
template <typename K, typename V, typename Comparison>
struct EntryComparison : absl::container_internal::CompressedTuple<Comparison> {
  EntryComparison() = default;
  EntryComparison(const EntryComparison& comp) = default;
  explicit EntryComparison(const Comparison& comp)
      : absl::container_internal::CompressedTuple<Comparison>(comp) {}
  // Comparison operator for two `K`s.
  template <typename K1, typename K2>
  bool operator()(const K1& a, const K2& b) const {
    return this->template get<0>().operator()(a, b);
  }
  // Comparison operator for the `limit`s of two `Entry`s.
  bool operator()(const Entry<K, V>& a, const Entry<K, V>& b) const {
    return operator()(a.limit, b.limit);
  }
  // Comparison operator for an Entry and a limit.
  template <typename K2>
  bool operator()(const Entry<K, V>& a, const K2& b) const {
    return operator()(a.limit, b);
  }
  // Comparison operator for a limit and an Entry.
  template <typename K1>
  bool operator()(const K1& a, const Entry<K, V>& b) const {
    return operator()(a, b.limit);
  }
  // Enable heterogeneous lookup.
  using is_transparent = void;
};

}  // namespace internal_intervalmap

// The type `K` must satisfy the C++ Compare concept under `Comparison`.
// `K`, `V`, and `Comparison` must be copy-assignable (see:
// https://en.cppreference.com/w/cpp/named_req/Compare).
template <class K, class V, class Comparison = std::less<K>,
          class Allocator = std::allocator<std::pair<const K, V>>,
          typename TableType = void>
class IntervalMap {
 public:
  using Entry = internal_intervalmap::Entry<K, V>;
  using EntryComparison =
      internal_intervalmap::EntryComparison<K, V, Comparison>;

 private:
  using Table =
      std::conditional_t<std::is_void_v<TableType>,
                         std::set<Entry, EntryComparison,
                                  typename std::allocator_traits<
                                      Allocator>::template rebind_alloc<Entry>>,
                         TableType>;

  // TODO: Match the behavior of btree_set. In particular, support
  // heterogeneous lookup for string-like objects by default.
  template <class K1>
  using key_arg = HeterogeneousLookupKeyArg<K1, K, Comparison>;

 public:
  using value_type = typename Table::value_type;
  using reference = typename Table::reference;
  using const_reference = typename Table::const_reference;
  using pointer = typename Table::pointer;
  using const_pointer = typename Table::const_pointer;
  using iterator = typename Table::iterator;
  using const_iterator = typename Table::const_iterator;
  using reverse_iterator = typename Table::reverse_iterator;
  using const_reverse_iterator = typename Table::const_reverse_iterator;
  using allocator_type = typename Table::allocator_type;
  using size_type = typename Table::size_type;
  using difference_type = typename Table::difference_type;

  using key_type = K;
  using mapped_type = V;
  using key_compare = Comparison;

  IntervalMap() = default;
  explicit IntervalMap(const Allocator& alloc) : table_(alloc) {}
  explicit IntervalMap(const Comparison& comp,
                       const Allocator& alloc = Allocator())
      : table_(EntryComparison(comp), alloc) {}

  // Set [start,limit) to "value".  Any existing intervals
  // that overlap [start,limit) are split or removed to
  // ensure that there is no overlap in the resulting set.
  // Returns the iterator referencing entry that holds [start, limit) interval.
  // REQUIRES: start < limit
  template <class K1 = const K&, class K2 = const K&, class V2 = const V&,
            class = std::enable_if_t<std::is_convertible_v<K1&&, K>>,
            class = std::enable_if_t<std::is_convertible_v<K2&&, K>>,
            class = std::enable_if_t<std::is_convertible_v<V2&&, V>>>
  iterator Set(K1&& start, K2&& limit, V2&& value);

  // Set [start,limit) to "value".  Coalesce with adjoining
  // ranges that have the same value.
  // REQUIRES: start < limit
  // REQUIRES: V::operator==() is present
  template <class K2 = const K&, class V2 = const V&,
            class = std::enable_if_t<std::is_convertible_v<K2&&, K>>,
            class = std::enable_if_t<std::is_convertible_v<V2&&, V>>>
  void SetAndCoalesce(K start, K2&& limit, V2&& value);

  // Sets [start,limit) to "value".  This is a faster version
  // of Set when the client can guarantee that [start,limit) does
  // not overlap with any intervals in "this".
  // Returns the iterator referencing entry that holds [start, limit) interval.
  // REQUIRES: start < limit
  // REQUIRES: IsEmptyInterval(start, limit)
  template <class K1 = const K&, class K2 = const K&, class V2 = const V&,
            class = std::enable_if_t<std::is_convertible_v<K1&&, K>>,
            class = std::enable_if_t<std::is_convertible_v<K2&&, K>>,
            class = std::enable_if_t<std::is_convertible_v<V2&&, V>>>
  iterator SetNoOverlap(K1&& start, K2&& limit, V2&& value);

  // Adds all the entries in "entries[0]..entries[num_entries-1]" to
  // "this".  Performance is best if the entries array is sorted in
  // increasing order, but this is not required.
  //
  // REQUIRES: this->IsEmptyInterval(entries[i].start, entries[i].limit) for
  //           0 <= i < num_entries
  // REQUIRES: entries[i].start < entries[i].limit for 0 <= i < num_entries
  // REQUIRES: entries[i] does not overlap entries[j] for all i != j
  void SetManyNoOverlap(int num_entries, const Entry* entries);

  // Get the mutable value from the Entry referenced by the `iterator`.
  // Iterator is NOT invalidated by this operation.
  V* MutableValue(iterator it);

  // Erases all data for the range [start,limit).  Any existing
  // interval that is covered by the range is removed; any
  // existing interval that overlaps the range is split at the
  // boundaries of the range, and the overlapping
  // piece is removed.
  // Returns the first position with start >= "limit", or table_.end().
  // REQUIRES: start < limit
  iterator Erase(const K& start, const K& limit);

  // Same as above, but erases the exact iterator provided with no chance of
  // any overlapping ranges.
  iterator Erase(iterator it) { return table_.erase(it); }

  // Remove all entries from this map
  void Clear() { table_.clear(); }

  // Combine all adjacent ranges in the map which have the same value.
  // REQUIRES: V::operator==() is present
  void Coalesce();

  // Iteration support using an STL-like iterator.
  //
  // Example:
  //   for (iter = map.begin(); iter != map.end(); ++iter) {
  //     Process(iter->start, iter->limit, iter->value);
  //   }
  //
  // Intervals are returned in sorted order.  See the restriction on
  // iter->start and iter->limit use above.
  const_iterator begin() const { return table_.begin(); }
  const_iterator end() const { return table_.end(); }
  iterator begin() { return table_.begin(); }
  iterator end() { return table_.end(); }

  const_reverse_iterator rbegin() const { return table_.rbegin(); }
  const_reverse_iterator rend() const { return table_.rend(); }
  reverse_iterator rbegin() { return table_.rbegin(); }
  reverse_iterator rend() { return table_.rend(); }

  // Iterator version of FindNext(). May return a non-end() iterator even if
  // "key" does not fall within a recorded interval.
  template <typename K1 = K>
  const_iterator find(const key_arg<K1>& key) const {
    return table_.upper_bound(key);
  }
  template <typename K1 = K>
  iterator find(const key_arg<K1>& key) {
    return table_.upper_bound(key);
  }

  // Returns whether "key" falls within a recorded interval.
  template <typename K1 = K>
  bool contains(const key_arg<K1>& key) const {
    return find_at(key) != end();
  }

  // If "key" falls within a recorded interval, store the value
  // for that interval in "*value" and return true.  Else return false.
  // Returns the same as contains(key).
  template <typename K1 = K>
  bool Lookup(const key_arg<K1>& key, V* value) const;

  // Same as Lookup() but returns a pointer to the value found, nullptr if none.
  // Returns a non-null pointer iff contains(key).
  // More efficient than Lookup() if values are large objects.
  // Pointer-stability is guaranteed only after mutations that do not remove or
  // coalesce intervals, such as SetNoOverlap().
  template <typename K1 = K>
  const V* absl_nullable LookupPtr(const key_arg<K1>& key) const;

  // If "key" falls within a recorded interval, store the containing
  // interval in "*start/*limit", its value in "*value", and return
  // true.  Else return false.
  // Returns the same as contains(key).
  template <typename K1 = K>
  bool FindInterval(const key_arg<K1>& key, K* start, K* limit, V* value) const;

  // Find the first interval that contains something >= "key".
  // If not found, returns false.  Otherwise, stores the
  // end-points of the containing interval in "*start/*limit",
  // the associated value in "*value", and returns true.
  // Note that the returned interval may start before "key".
  //
  // This method may be useful when iterating over all intervals
  // while modifying the interval map.  Example:
  //    K k = 0;
  //    K start, limit;
  //    V value;
  //    while (map.FindNext(k, &start, &limit, &value)) {
  //      Process(start, limit, value);
  //      k = limit;
  //    }
  template <typename K1 = K>
  bool FindNext(const key_arg<K1>& key, K* start, K* limit, V* value) const;

  // Find the first point in the map that is >= "key".
  // If not found, returns false.  Otherwise, stores the
  // point in "*result" and return true.
  template <typename K1 = K>
  bool FindNextPoint(const key_arg<K1>& key, K* result) const;

  // Return number of distinct intervals stored inside the map.
  int size() const { return table_.size(); }

  // Return true iff this map is empty
  bool empty() const { return table_.empty(); }

  // Returns true iff there are no entries in "this" that overlap
  // [start,limit).
  template <typename K1 = K, typename K2 = K>
  bool IsEmptyInterval(const key_arg<K1>& start,
                       const key_arg<K2>& limit) const;

  // Returns true iff "this" contains entries that cover the entire range
  // [start,limit).
  template <typename K1 = K, typename K2 = K>
  bool CoversRange(const key_arg<K1>& start, const key_arg<K2>& limit) const;

  // Copy intervals in "src" to "this".  Existing intervals in "this"
  // that overlap some copied interval are overwritten.  Other
  // existing entries in "this" are left alone.
  // REQUIRES: &src != this
  void MergeFrom(const IntervalMap& src);

  // Copy any portion of "src" that falls within "[start,limit)" to
  // "this".  Existing intervals in "this" that overlap some copied interval
  // are overwritten.  Other existing intervals in "this" are left alone.
  // REQUIRES: &src != this
  void MergeSubRangeFrom(const IntervalMap& src, const K& start,
                         const K& limit);

  void swap(IntervalMap& x) noexcept { table_.swap(x.table_); }

  // Merge "value" into every point in the range [start,limit).
  //
  // Sub-ranges of [start,limit) that are disjoint from any pre-existing
  // range are initialized to "value".
  //
  // Sub-ranges of [start,limit) that overlap a pre-existing range
  // are initialized to the value placed in *dst by calling
  //     (*merger)(value, dst)
  // where *dst is initially set to the pre-existing range's value.
  // Note that pre-existing ranges which overlap [start,limit) partially
  // will be split.
  //
  // E.g., the following code adds 17 to pre-existing ranges and
  // sets missing ranges to 17.
  //   void Add(const int& increment, int* dst) {
  //     *dst += increment;
  //   }
  //   intervalmap.MergeValue(start, limit, 17, &Add);
  template <typename F>
  void MergeValue(const K& start, const K& limit, const V& value, F merger);

  // Lower-level version of the preceding method.  This one is harder
  // to use, but provides more control:
  //
  // Apply the specified merging function to compute the value for every
  // point in the range [start,limit).
  //
  // "merge_function(istart, ilimit, existing_value, scratch)" will be called
  // once per sub-range of keys in [start,limit], with istart and ilimit being
  // const K& bounds of the sub-range being merged, and existing_value and
  // scratch being V*.  If the sub-range is missing in the initial state of
  // "*this" (i.e. has no associated value), merge_function is called with
  // existing_value==nullptr.  Otherwise, merge_function is called with
  // existing_value pointing to the current value of the sub-range.  The
  // sub-range is passed to "merge_function" in the "start" and "limit"
  // arguments.
  // Note that pre-existing ranges which overlap [start,limit) partially
  // will be split.
  //
  // "merge_function" should return nullptr if the sub-range should be
  // cleared (or remain missing if it was initially missing).
  //
  // Otherwise, "merge_function" should return a pointer to a value
  // that should be stored into the sub-range. It can:
  //  - modify "existing_value" in place and return it, or
  //  - store the value in "*scratch" and return "scratch", or
  //  - return a pointer to any object, as long as it remains live while
  //    MergeValue is running. "merge_function" is called sequentially,
  //    so it can return the same pointer for multiple invocations.
  template <typename F>
  void MergeValue(const K& start, const K& limit, F merge_function);

  typedef const V* (*MergeFunction)(void* arg, const K& start, const K& limit,
                                    V* existing_value, V* scratch);
  // DEPRECATED(jpsugar): Use functor MergeValue instead (without arg).
  void MergeValue(const K& start, const K& limit, MergeFunction merge_function,
                  void* arg);

  // If k falls in the middle of a range in the table, split that
  // range into two entries (preserving the value of the range in the
  // two split pieces) and return an iterator positioned at the second
  // entry.  If k falls before a valid range, return an iterator
  // positioned at that range.  If there are no ranges after k in the
  // table, returns table_.end().
  iterator SplitAt(const K& k);

  // Returns true if both maps are equal. K and V must have == operators,
  // which are used to compare the maps (the custom comparator is not used).
  friend bool operator==(const IntervalMap& lhs, const IntervalMap& rhs) {
    if (lhs.size() != rhs.size()) return false;
    for (auto lhs_it = lhs.begin(), rhs_it = rhs.begin(); lhs_it != lhs.end();
         ++lhs_it, ++rhs_it) {
      if (!(lhs_it->start == rhs_it->start && lhs_it->limit == rhs_it->limit &&
            lhs_it->value == rhs_it->value)) {
        return false;
      }
    }
    return true;
  }

  // Returns true if both maps are different. K and V must have an == operator,
  // which are used to compare the maps (the custom comparator is not used).
  friend bool operator!=(const IntervalMap& lhs, const IntervalMap& rhs) {
    return !(lhs == rhs);
  }

 private:
  // Iterator version of Lookup() and FindInterval(): returns an iterator
  // starting at the interval that contains "key", end() if none.
  // Private until they can replace the misleading, historical find() methods.
  template <typename K1 = K>
  const_iterator find_at(const key_arg<K1>& key) const {
    return const_cast<IntervalMap*>(this)->find_at(key);
  }
  template <typename K1 = K>
  iterator find_at(const key_arg<K1>& key);

  // Helper routine to insert entry for "[start..limit) -> value".
  // Returns the iterator referencing the new entry we just inserted.
  // REQUIRES: No existing entries for interval "[start..limit)"
  template <class K1, class K2, class V2>
  iterator InsertEntry(K1&& start, K2&& limit, V2&& value);

  // A faster version of InsertEntry, used when the caller knows the position
  // of the inserted range.
  // Returns the iterator referencing the new entry we just inserted.
  //
  // REQUIRES: [start,limit) is just before *insert_before. That is, the
  // following two conditions must hold:
  // (1) insert_before==table_.end() or insert_before->start >= limit
  // (2) (insert_before-1)==table_.begin() or (insert_before-1)->limit <= start
  template <class K1, class K2, class V2>
  iterator InsertEntryWithHint(typename Table::const_iterator insert_before,
                               K1&& start, K2&& limit, V2&& value);

  template <class K1 = const K&>
  typename Table::iterator MutateStart(typename Table::iterator it,
                                       K1&& new_start);

  // Private helper that uses `table_`'s comparator to compare keys.
  template <typename K1, typename K2>
  bool comp(const K1& l, const K2& r) const {
    // This is enough for our purposes, and ensures that we always interpret the
    // keys as "like K". For example, this avoids comparing char* as pointers
    // when K=std::string and Comparison=std::less<>.
    static_assert(std::is_same_v<K1, K> || std::is_same_v<K2, K>,
                  "At least one arg must be K, to avoid unsafe comparisons");
    return table_.key_comp()(l, r);
  }

  Table table_;
};

// -------------------------------------------------------------------
// Implementation details follow; clients should ignore

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
inline typename IntervalMap<K, V, Comparison, Allocator, TableType>::iterator
IntervalMap<K, V, Comparison, Allocator, TableType>::SplitAt(const K& k) {
  auto iter = table_.upper_bound(k);
  if (iter != table_.end() && comp(iter->start, k)) {
    // Current entry [iter->start, iter->limit) contains k.
    // Split it into [iter->start, k) and [k, iter->limit).
    Entry entry = *iter;
    table_.erase(iter);
    table_.insert(Entry(k, entry.start, entry.value));
    return table_.insert(Entry(entry.limit, k, entry.value)).first;
  }
  return iter;
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
template <class K1, class K2, class V2, class, class, class>
typename IntervalMap<K, V, Comparison, Allocator, TableType>::iterator
IntervalMap<K, V, Comparison, Allocator, TableType>::Set(K1&& start, K2&& limit,
                                                         V2&& value) {
  DCHECK(comp(K(start), limit));
  auto it = Erase(start, limit);  // Ensure no overlap
  return InsertEntryWithHint(it, std::forward<K1>(start),
                             std::forward<K2>(limit), std::forward<V2>(value));
}

// Erases [start, limit) first to simplify to four disjoint cases:
//     need to merge with left,
//     need to merge with right,
//     need to merge both together,
//     need to create a new Entry.
template <class K, class V, class Comparison, class Allocator,
          typename TableType>
template <class K2, class V2, class, class>
void IntervalMap<K, V, Comparison, Allocator, TableType>::SetAndCoalesce(
    K start, K2&& limit, V2&& value) {
  DCHECK(comp(start, limit));
  Erase(start, limit);
  auto right = table_.upper_bound(limit);
  auto left = right;
  if (left == table_.begin()) {
    left = table_.end();  // sentinel for no entry to left
  } else {
    --left;
  }

  // Supersede entry to left if present, has same value, and is adjoining.
  if (left != table_.end() && !comp(left->limit, start)  // ==, since it's not >
      && left->value == value) {
    // We could extend left->limit instead, but mutating the sort key
    // could confuse some set implementations.
    DCHECK(left != right);
    start = std::move(left->start);
    table_.erase(left);
    right = table_.upper_bound(limit);
  }

  if (right != table_.end() && !comp(limit, right->start) &&
      right->value == value) {
    this->MutateStart(right, std::move(start));
  } else {
    // Create a new entry.
    table_.emplace(std::forward<K2>(limit), std::move(start),
                   std::forward<V2>(value));
  }
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
template <class K1, class K2, class V2, class, class, class>
typename IntervalMap<K, V, Comparison, Allocator, TableType>::iterator
IntervalMap<K, V, Comparison, Allocator, TableType>::SetNoOverlap(K1&& start,
                                                                  K2&& limit,
                                                                  V2&& value) {
  DCHECK(comp(K(start), limit));
  return InsertEntry(std::forward<K1>(start), std::forward<K2>(limit),
                     std::forward<V2>(value));
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
void IntervalMap<K, V, Comparison, Allocator, TableType>::SetManyNoOverlap(
    int num_entries, const Entry* entries) {
  if (num_entries == 0) return;
  auto hint = table_.end();
  for (int i = 0; i < num_entries; i++) {
    DCHECK(IsEmptyInterval(entries[i].start, entries[i].limit));
    hint = table_.insert(hint, entries[i]);
  }
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
V* IntervalMap<K, V, Comparison, Allocator, TableType>::MutableValue(
    iterator it) {
  DCHECK(it != table_.end());
  return &const_cast<Entry&>(*it).value;
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
void IntervalMap<K, V, Comparison, Allocator, TableType>::Coalesce() {
  if (table_.empty()) return;
  auto p = table_.begin();
  while (p != table_.end()) {
    auto next = p;
    if (++next == table_.end()) break;
    if (next->start <= p->limit && next->value == p->value) {
      K new_start = p->start;
      K new_limit = next->limit;
      V value = p->value;
      table_.erase(p);
      table_.erase(next);
      p = table_.insert(Entry(new_limit, new_start, value)).first;
    } else {
      p = next;
    }
  }
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
template <class K1, class K2, class V2>
typename IntervalMap<K, V, Comparison, Allocator, TableType>::iterator
IntervalMap<K, V, Comparison, Allocator, TableType>::InsertEntry(K1&& start,
                                                                 K2&& limit,
                                                                 V2&& value) {
  DCHECK(comp(K(start), limit));
  DCHECK(IsEmptyInterval(start, limit));
  return table_
      .emplace(std::forward<K2>(limit), std::forward<K1>(start),
               std::forward<V2>(value))
      .first;
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
template <class K1, class K2, class V2>
typename IntervalMap<K, V, Comparison, Allocator, TableType>::iterator
IntervalMap<K, V, Comparison, Allocator, TableType>::InsertEntryWithHint(
    typename Table::const_iterator insert_before, K1&& start, K2&& limit,
    V2&& value) {
#ifndef NDEBUG
  CHECK(comp(K(start), limit));
  // Make sure that insert_before is just after [start,end), and that
  // [start,end) doesn't overlap with existing ranges.
  DCHECK(insert_before == table_.end() || !comp(insert_before->start, limit));
  auto prev = insert_before;
  if (prev != table_.begin()) {
    --prev;
    CHECK(!comp(start, prev->limit));
  }
#endif

  return table_.emplace_hint(insert_before, std::forward<K2>(limit),
                             std::forward<K1>(start), std::forward<V2>(value));
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
void IntervalMap<K, V, Comparison, Allocator, TableType>::MergeValue(
    const K& start, const K& limit, MergeFunction merge_function,
    void* extra_arg) {
  MergeValue(
      start, limit,
      [&](const K& istart, const K& ilimit, V* value, V* scratch) -> const V* {
        return merge_function(extra_arg, istart, ilimit, value, scratch);
      });
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
template <typename F>
void IntervalMap<K, V, Comparison, Allocator, TableType>::MergeValue(
    const K& start, const K& limit, F merge_function) {
  DCHECK(comp(start, limit));
  SplitAt(start);
  K handled = start;
  V scratch;
  while (comp(handled, limit)) {
    auto i = table_.upper_bound(handled);
    if (i == table_.end()) break;
    if (!comp(i->start, limit)) break;

    if (comp(handled, i->start)) {
      // Invoke merge function for non-existing range [handled..i->start)
      const V* new_value = merge_function(handled, i->start, nullptr, &scratch);
      K gap_end = i->start;
      if (new_value != nullptr) {
        table_.insert(Entry(gap_end, handled, *new_value));
      }
      handled = gap_end;
      continue;
    }

    const bool overlaps_limit = comp(limit, i->limit);
    if (overlaps_limit) {
      SplitAt(limit);
      i = table_.upper_bound(handled);
    }

    // Invoke merge function for range [i->start, i->limit)
    DCHECK(!comp(i->start, handled));
    const V* new_value = merge_function(
        i->start, i->limit, &const_cast<Entry*>(&*i)->value, &scratch);
    K next_handled = i->limit;
    if (new_value == nullptr) {
      table_.erase(i);
    } else {
      if (new_value != &i->value) {
        *this->MutableValue(i) = *new_value;
      }
    }
    handled = next_handled;
  }
  if (comp(handled, limit)) {
    // Invoke merge function for non-existing range [handled..limit)
    const V* new_value = merge_function(handled, limit, nullptr, &scratch);
    if (new_value != nullptr) {
      table_.insert(Entry(limit, handled, *new_value));
    }
  }
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
template <typename F>
void IntervalMap<K, V, Comparison, Allocator, TableType>::MergeValue(
    const K& start, const K& limit, const V& value, F merger) {
  MergeValue(start, limit, [&](const K&, const K&, V* current, V*) -> const V* {
    if (current == nullptr) {
      return &value;
    }
    merger(value, current);
    return current;
  });
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
inline typename IntervalMap<K, V, Comparison, Allocator, TableType>::iterator
IntervalMap<K, V, Comparison, Allocator, TableType>::Erase(const K& start,
                                                           const K& limit) {
  DCHECK(comp(start, limit));
  SplitAt(start);
  SplitAt(limit);
  auto i = table_.upper_bound(start);
  while (i != table_.end() && comp(i->start, limit)) {
    i = table_.erase(i);
  }
  return i;
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
template <typename K1>
bool IntervalMap<K, V, Comparison, Allocator, TableType>::FindNext(
    const key_arg<K1>& key, K* start, K* limit, V* value) const {
  if (auto iter = find(key); iter != end()) {
    *start = iter->start;
    *limit = iter->limit;
    *value = iter->value;
    return true;
  }
  return false;
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
template <typename K1>
bool IntervalMap<K, V, Comparison, Allocator, TableType>::FindNextPoint(
    const key_arg<K1>& key, K* result) const {
  auto iter = find(key);
  if (iter == end()) {
    return false;
  } else if (comp(iter->start, key)) {
    *result = key;
    return true;
  } else {
    *result = iter->start;
    return true;
  }
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
template <typename K1>
typename IntervalMap<K, V, Comparison, Allocator, TableType>::iterator
IntervalMap<K, V, Comparison, Allocator, TableType>::find_at(
    const key_arg<K1>& key) {
  auto iter = find(key);
  return (iter != end() && !comp(key, iter->start)) ? iter : end();
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
template <typename K1>
const V* absl_nullable
IntervalMap<K, V, Comparison, Allocator, TableType>::LookupPtr(
    const key_arg<K1>& key) const {
  auto iter = find_at(key);
  return (iter != end()) ? &iter->value : nullptr;
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
template <typename K1>
bool IntervalMap<K, V, Comparison, Allocator, TableType>::Lookup(
    const key_arg<K1>& key, V* value) const {
  if (auto iter = find_at(key); iter != end()) {
    *value = iter->value;
    return true;
  }
  return false;
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
template <typename K1>
bool IntervalMap<K, V, Comparison, Allocator, TableType>::FindInterval(
    const key_arg<K1>& key, K* start, K* limit, V* value) const {
  if (auto iter = find_at(key); iter != end()) {
    *start = iter->start;
    *limit = iter->limit;
    *value = iter->value;
    return true;
  }
  return false;
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
template <typename K1, typename K2>
bool IntervalMap<K, V, Comparison, Allocator, TableType>::IsEmptyInterval(
    const key_arg<K1>& start, const key_arg<K2>& limit) const {
  auto iter = find(start);
  // Since [start,limit) is half-open, return `true` if iter->start == limit.
  return iter == end() || !comp(iter->start, limit);
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
template <typename K1, typename K2>
bool IntervalMap<K, V, Comparison, Allocator, TableType>::CoversRange(
    const key_arg<K1>& start, const key_arg<K2>& limit) const {
  if constexpr (std::is_same_v<K1, K> || std::is_same_v<K2, K>) {
    CHECK(comp(start, limit));
  }
  auto iter = table_.upper_bound(start);
  if (iter == table_.end() || comp(start, iter->start)) {
    return false;
  } else if (!comp(iter->limit, limit)) {
    return true;
  }

  auto prev = iter++;
  for (; iter != table_.end(); ++iter) {
    if (iter->start != prev->limit) return false;  // There's a gap
    if (!comp(iter->limit, limit)) return true;    // Covered the entire range.
    prev = iter;
  }
  return false;  // Ran out of entries before covering the entire range.
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
void IntervalMap<K, V, Comparison, Allocator, TableType>::MergeSubRangeFrom(
    const IntervalMap<K, V, Comparison, Allocator, TableType>& src,
    const K& start, const K& limit) {
  auto src_iter = src.table_.upper_bound(start);
  if (src_iter == src.table_.end()) return;  // Nothing to move
  auto dst_iter = table_.upper_bound(start);
  if (dst_iter == table_.end() || (limit <= dst_iter->start)) {
    // Nothing in [start .. limit) in "this", so we can copy data in bulk

    // Special case: we might want to drop a prefix of the first entry
    if (start > src_iter->start) {
      // Drop a prefix of *src_iter
      if (limit <= src_iter->limit) {
        // This is also the last entry we are interested in
        table_.emplace(limit, start, src_iter->value);
        return;
      } else {
        dst_iter = table_.emplace_hint(dst_iter, src_iter->limit, start,
                                       src_iter->value);
      }
      ++src_iter;  // We have now processed everything we want from this entry.
    }

    // Now copy all entries that should be moved entirely
    while (src_iter != src.table_.end() && src_iter->limit <= limit) {
      dst_iter = table_.insert(dst_iter, *src_iter);
      ++src_iter;
      // According to the STL spec for insert with an iterator hint,
      // we should need to do '++dst_iter' here.  However, empirical
      // measurement shows that the MergeSubRangeFrom benchmark slows
      // down by about 25%-40% if we do so.  If we switch to a
      // different STL implementation, we might want to revisit this.
    }

    if (src_iter != src.table_.end() && comp(src_iter->start, limit)) {
      // Also copy a prefix of *src_iter
      // Note: that because of the loop above that copied entire
      // entries, we are guaranteed that *src_iter extends past limit.
      DCHECK(comp(limit, src_iter->limit));
      table_.emplace_hint(dst_iter, limit, src_iter->start, src_iter->value);
    }
  } else {
    while (src_iter != src.table_.end()) {
      const K* s =
          comp(src_iter->start, start) ? &start : &src_iter->start;  // max
      const K* l =
          comp(limit, src_iter->limit) ? &limit : &src_iter->limit;  // min
      if (comp(*s, *l)) {
        this->Set(*s, *l, src_iter->value);
      } else {
        break;
      }
      ++src_iter;
    }
  }
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
void IntervalMap<K, V, Comparison, Allocator, TableType>::MergeFrom(
    const IntervalMap<K, V, Comparison, Allocator, TableType>& src) {
  if (table_.empty()) {
    // Fast path: just copy all the elements, since there's no overlapping
    table_ = src.table_;
  } else {
    for (auto iter = src.table_.begin(); iter != src.table_.end(); ++iter) {
      this->Set(iter->start, iter->limit, iter->value);
    }
  }
}

template <class K, class V, class Comparison, class Allocator,
          typename TableType>
template <class K1>
typename IntervalMap<K, V, Comparison, Allocator, TableType>::Table::iterator
IntervalMap<K, V, Comparison, Allocator, TableType>::MutateStart(
    typename Table::iterator it, K1&& new_start) {
  Entry entry = std::move(const_cast<Entry&>(*it));
  entry.start = std::forward<K1>(new_start);
  table_.erase(it);
  return table_.insert(std::move(entry)).first;
}

// TODO: remove this alias
template <class K, class V, class Comparison = std::less<K>>
using IntervalMapEntry = typename IntervalMap<K, V, Comparison>::value_type;

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_INTERVALMAP_H_
