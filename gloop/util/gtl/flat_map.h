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

// flat_map is a map implementation that uses a sequence container (by default,
// std::vector) instead of a binary tree to store ordered values.
//
// The main benefit of flat_map is memory efficiency and fast read access.
// Storing values in a contiguous range allows zero per-node memory overhead,
// and improves memory locality. The downside is that mutations (insert, erase)
// are potentially linear and invalidate iterators.
//
// The typical use_cases for flat_map include:
//  * minimizing memory usage of the map (or many small maps)
//  * "const" maps - created once and then accessed in a read-only fashion
//  * small maps, where linear access is more efficient than std::map (with
//    InlinedVector as an underlying type, this also makes small maps inlined)
//
//
// Detailed comparison with std::map (using std::vector as an underlying type):
//   + zero per-value memory overhead
//   + smaller map object size
//   + random-access iterators provided
//   + reserve(), capacity(), shrink_to_fit() provided
//   + data() provided if underlying container has it
//   + mutations (insert/erase with hint) at the end are O(1)
//   ~ lookups (contains, find) are still O(log(n)) but are more cache-friendly
//   - mutations are generally O(n)
//   - mutations may invalidate all iterators & pointers to elements
//
// You can create a constexpr instance of flat_map like:
//
//    constexpr auto kMap = gtl::fixed_flat_map_of<absl::string_view, int>(
//        {{"foo", 1}, {"bar", 2}, {"baz", 3}});
//
// In this example, it creates a flat_map with absl::string_view as the key_type
// and int as the mapped_type.
// Keys do not need to be sorted, but they must be unique.

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_FLAT_MAP_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_FLAT_MAP_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/throw_delegate.h"
#include "absl/meta/type_traits.h"
#include "gloop/util/gtl/flat_internal.h"
#include "gloop/util/gtl/heterogeneous_lookup.h"
#include "gloop/util/gtl/iterator_adaptors.h"
#include "gloop/util/gtl/unchecked_tags.h"  // IWYU pragma: export

namespace gtl {

// Tag value indicating the container is already sorted. This tag
// allows the overload for constructing a `gtl::flat_map` from an
// existing sorted container to be selected.
using ::gtl::sorted_unique_container;

template <typename Key, typename T,
          typename Compare = internal_flat::DefaultLess<Key>,
          typename Rep = std::vector<std::pair<const Key, T>>>
class flat_map : public internal_flat::FlatContainersMaybeExportData<
                     Rep, flat_map<Key, T, Compare, Rep>> {
  template <class K>
  using key_arg = gtl::HeterogeneousLookupKeyArg<K, Key, Compare>;

  // We use `InsertPair` (as opposed to `std::pair`) as a
  // performance optimization. `std::pair` never has trivial copy while this
  // trait enables optimizations in STL implementation of some algorithms
  // including `std::merge_sort` (see b/287638715) which is used by the
  // `flat_map`. As a workaround we hand roll a struct that has trivial copy
  // whenever possible.
  // TODO: Switch back to std::pair once performance issue is
  // resolved inside of std::merge_sort.
  struct InsertPair {
    template <typename T1, typename T2>
    explicit InsertPair(std::pair<T1, T2>&& p)
        : first(std::move(std::get<0>(p))), second(std::move(std::get<1>(p))) {}
    template <typename T1, typename T2>
    explicit InsertPair(const std::pair<T1, T2>& p)
        : first(std::get<0>(p)), second(std::get<1>(p)) {}
    explicit operator typename Rep::value_type() && {
      return {std::move(first), std::move(second)};
    }

    Key first;
    T second;
  };

 public:
  using key_type = Key;
  using mapped_type = T;
  using value_type = typename Rep::value_type;
  using init_type = std::pair<Key, T>;
  using key_compare = Compare;
  using value_compare = typename internal_flat::value_compare<Compare>;

  using container_type = Rep;
  using size_type = typename container_type::size_type;
  using difference_type = typename container_type::difference_type;

  using reference = typename container_type::reference;
  using const_reference = typename container_type::const_reference;
  using pointer = typename container_type::pointer;
  using const_pointer = typename container_type::const_pointer;

  using iterator = typename container_type::iterator;
  using reverse_iterator = typename container_type::reverse_iterator;
  using const_iterator = typename container_type::const_iterator;
  using const_reverse_iterator =
      typename container_type::const_reverse_iterator;

  // construct/copy/destroy:
  constexpr flat_map() : flat_map(Compare()) {}
  constexpr explicit flat_map(const Compare& cmp) : impl_(value_compare(cmp)) {}

  // If multiple elements in the input range have the same key, the first of
  // them will be present in the map.
  template <typename InputIterator>
  flat_map(InputIterator first, InputIterator last,
           const Compare& cmp = Compare())
      : flat_map(cmp) {
    insert(first, last);
  }

  flat_map(std::initializer_list<init_type> init,
           const Compare& cmp = Compare())
      : flat_map(init.begin(), init.end(), cmp) {}

  constexpr flat_map(const flat_map& m) = default;
  constexpr flat_map(flat_map&& m) noexcept = default;
  flat_map& operator=(const flat_map& m) = default;
  flat_map& operator=(flat_map&& m) noexcept = default;

  // Constructs underlying container by moving the given one.
  //
  // This constructor enables a few advanced use cases, e.g.:
  // * Passing ownership of pre-constructed container:
  //     std::vector<std::pair<int, int>> v = ...;
  //     gtl::flat_map<int, int> f(std::move(v));
  // * Passing custom arguments (e.g. allocator) to the underlying container:
  //     using Rep = std::vector<std::pair<int, int>, MyAllocator>;
  //     gtl::flat_map<int, int, std::less<int>, Rep> f(Rep(MyAllocator(arg)));
  //
  // Note: the given container need not be pre-sorted.
  explicit flat_map(Rep rep, const Compare& cmp = Compare())
      : impl_(value_compare(cmp), std::move(rep)) {}

  // Constructs underlying container directly by perfect forwarding arguments to
  // its constructor.
  //
  // This constructor enables optimizing construction if the input is known to
  // be ordered, as well as passing custom arguments to the underlying
  // container.
  //
  // The constructed container MUST be strictly ordered and not contain any
  // duplicates. If not, the behavior is undefined (and the ordering is verified
  // with assert() in debug builds).
  template <typename... Args, typename = std::enable_if_t<
                                  std::is_constructible<Rep, Args...>::value>>
  constexpr explicit flat_map(sorted_unique_container_t, Args&&... args)
      : impl_(sorted_container, value_compare(), std::forward<Args>(args)...) {}

  template <typename... Args, typename = std::enable_if_t<
                                  std::is_constructible<Rep, Args...>::value>>
  constexpr flat_map(sorted_unique_container_t, const Compare& cmp,
                     Args&&... args)
      : impl_(sorted_container, value_compare(cmp),
              std::forward<Args>(args)...) {}

  // iterators:
  constexpr iterator begin() { return rep().begin(); }
  constexpr const_iterator begin() const { return rep().begin(); }
  constexpr iterator end() { return rep().end(); }
  constexpr const_iterator end() const { return rep().end(); }

  constexpr reverse_iterator rbegin() { return rep().rbegin(); }
  constexpr const_reverse_iterator rbegin() const { return rep().rbegin(); }
  constexpr const_reverse_iterator rend() const { return rep().rend(); }
  constexpr reverse_iterator rend() { return rep().rend(); }

  constexpr const_iterator cbegin() const { return rep().cbegin(); }
  constexpr const_iterator cend() const { return rep().cend(); }
  constexpr const_reverse_iterator crbegin() const { return rep().crbegin(); }
  constexpr const_reverse_iterator crend() const { return rep().crend(); }

  // capacity:
  constexpr bool empty() const { return rep().empty(); }
  constexpr size_type size() const { return rep().size(); }
  constexpr size_type max_size() const { return rep().max_size(); }

  // Moves the underlying container from `this`.
  Rep ExtractRep() && {
    static_assert(internal_flat::has_clear<Rep>::value,
                  "Can't efficiently extract rep");
    auto moved = std::move(*this);
    return std::move(moved.rep());
  }

  // element access:
  template <typename K = key_type>
  mapped_type& operator[](const key_arg<K>& k) {
    return try_emplace(k).first->second;
  }
  template <typename K = key_type, K* = nullptr>
  mapped_type& operator[](key_arg<K>&& k) {
    return try_emplace(std::move(k)).first->second;
  }

  template <typename K = key_type>
  constexpr mapped_type& at(const key_arg<K>& k) {
    auto it = find(k);
    if (it == end()) absl::ThrowStdOutOfRange("flat_map::at");
    return it->second;
  }
  template <typename K = key_type>
  constexpr const mapped_type& at(const key_arg<K>& k) const {
    auto it = find(k);
    if (it == end()) absl::ThrowStdOutOfRange("flat_map::at");
    return it->second;
  }

  // modifiers:

  template <typename... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    return insert(init_type(std::forward<Args>(args)...));
  }
  template <typename... Args>
  iterator emplace_hint(const_iterator position, Args&&... args) {
    return insert(position, init_type(std::forward<Args>(args)...));
  }

  template <
      typename K = key_type, typename... Args,
      std::enable_if_t<!std::is_convertible_v<K, const_iterator>, int> = 0>
  std::pair<iterator, bool> try_emplace(const key_arg<K>& k, Args&&... args) {
    return try_emplace_impl(k, std::forward<Args>(args)...);
  }
  template <
      typename K = key_type, typename... Args,
      std::enable_if_t<!std::is_convertible_v<K, const_iterator>, int> = 0>
  std::pair<iterator, bool> try_emplace(key_arg<K>&& k, Args&&... args) {
    return try_emplace_impl(std::move(k), std::forward<Args>(args)...);
  }
  template <typename K = key_type, typename... Args>
  iterator try_emplace(const_iterator hint, const key_arg<K>& k,
                       Args&&... args) {
    return try_emplace_hint_impl(hint, k, std::forward<Args>(args)...);
  }
  template <typename K = key_type, typename... Args, K* = nullptr>
  iterator try_emplace(const_iterator hint, key_arg<K>&& k, Args&&... args) {
    return try_emplace_hint_impl(hint, std::move(k),
                                 std::forward<Args>(args)...);
  }

  std::pair<iterator, bool> insert(init_type&& v) {
    return internal_flat::insert(&rep(), std::move(v), value_comp());
  }
  template <typename P, typename = std::enable_if_t<
                            std::is_constructible_v<init_type, P&&>>>
  std::pair<iterator, bool> insert(P&& v) {
    return emplace(std::forward<P>(v));
  }
  iterator insert(const_iterator hint, init_type&& v) {
    return internal_flat::insert_hint(&rep(), hint, std::move(v), value_comp());
  }
  template <typename P, typename = std::enable_if_t<
                            std::is_constructible_v<init_type, P&&>>>
  iterator insert(const_iterator hint, P&& v) {
    return internal_flat::insert_hint(&rep(), hint, std::forward<P>(v),
                                      value_comp());
  }
  // If multiple elements in the input range have the same key, the first of
  // them will be present in the map.
  template <typename InputIterator>
  void insert(InputIterator first, InputIterator last) {
    if constexpr (std::is_move_assignable_v<value_type>) {
      internal_flat::insert_range(&rep(), first, last, value_comp());
    } else {
      internal_flat::insert_range_non_assignable<InsertPair>(
          &rep(), first, last, value_comp());
    }
  }
  void insert(std::initializer_list<init_type> ilist) {
    insert(ilist.begin(), ilist.end());
  }

  template <typename K = key_type, typename M = mapped_type>
  std::pair<iterator, bool> insert_or_assign(const key_arg<K>& k, M&& obj) {
    return internal_flat::insert_or_assign(
        &rep(), internal_flat::forward_as_pair(k, std::forward<M>(obj)),
        value_comp());
  }
  template <typename K = key_type, typename M = mapped_type, K* = nullptr>
  std::pair<iterator, bool> insert_or_assign(key_arg<K>&& k, M&& obj) {
    return internal_flat::insert_or_assign(
        &rep(),
        internal_flat::forward_as_pair(std::move(k), std::forward<M>(obj)),
        value_comp());
  }
  template <typename K = key_type, typename M = mapped_type>
  iterator insert_or_assign(const_iterator hint, const key_arg<K>& k, M&& obj) {
    return internal_flat::insert_or_assign_hint(
        &rep(), hint, internal_flat::forward_as_pair(k, std::forward<M>(obj)),
        value_comp());
  }
  template <typename K = key_type, typename M = mapped_type, K* = nullptr>
  iterator insert_or_assign(const_iterator hint, key_arg<K>&& k, M&& obj) {
    return internal_flat::insert_or_assign_hint(
        &rep(), hint,
        internal_flat::forward_as_pair(std::move(k), std::forward<M>(obj)),
        value_comp());
  }

  template <
      typename K = key_type,
      std::enable_if_t<!std::is_convertible_v<K, const_iterator>, int> = 0>
  size_type erase(const key_arg<K>& k) {
    auto it = find(k);
    if (it == end()) return 0;
    internal_flat::container_erase(&rep(), it);
    return 1;
  }
  iterator erase(const_iterator it) {
    return internal_flat::container_erase(&rep(), it);
  }
  iterator erase(const_iterator first, const_iterator last) {
    return internal_flat::container_erase(&rep(), first, last);
  }

  // Removes all elements for which predicate 'p' returns 'true'. The predicate
  // operates on map elements - the key-value pairs.
  template <class UnaryPredicate>
  size_t remove_if(UnaryPredicate p) {
    return internal_flat::container_erase_if(&rep(), std::move(p));
  }

  void swap(flat_map& other) noexcept { rep().swap(other.rep()); }
  void clear() { rep().clear(); }

  // observers:
  constexpr key_compare key_comp() const {
    // Explicit cast is necessary in case value_compare is convertible to
    // key_compare (e.g. if key_compare is std::function).
    return static_cast<const key_compare&>(impl_.cmp());
  }
  constexpr value_compare value_comp() const { return impl_.cmp(); }

  // map operations:
  template <typename K = key_type>
  constexpr iterator find(const key_arg<K>& k) {
    return internal_flat::ordered_find(keys_begin(), keys_end(), k, key_comp())
        .base();
  }
  template <typename K = key_type>
  constexpr const_iterator find(const key_arg<K>& k) const {
    return const_cast<flat_map*>(this)->find(k);
  }

  template <typename K = key_type>
  constexpr size_type count(const key_arg<K>& k) const {
    return find(k) == end() ? 0 : 1;
  }

  template <typename K = key_type>
  constexpr bool contains(const key_arg<K>& k) const {
    return find(k) != end();
  }

  // binary search methods:
  template <typename K = key_type>
  constexpr const_iterator lower_bound(const key_arg<K>& k) const {
    return const_cast<flat_map*>(this)->lower_bound(k);
  }
  template <typename K = key_type>
  constexpr iterator lower_bound(const key_arg<K>& k) {
    return std::lower_bound(keys_begin(), keys_end(), k, key_comp()).base();
  }

  template <typename K = key_type>
  constexpr const_iterator upper_bound(const key_arg<K>& k) const {
    return const_cast<flat_map*>(this)->upper_bound(k);
  }
  template <typename K = key_type>
  constexpr iterator upper_bound(const key_arg<K>& k) {
    return std::upper_bound(keys_begin(), keys_end(), k, key_comp()).base();
  }

  template <typename K = key_type>
  constexpr std::pair<iterator, iterator> equal_range(const key_arg<K>& k) {
    return {lower_bound(k), upper_bound(k)};
  }
  template <typename K = key_type>
  constexpr std::pair<const_iterator, const_iterator> equal_range(
      const key_arg<K>& k) const {
    return const_cast<flat_map*>(this)->equal_range(k);
  }

  // capacity-related extensions from std::vector interface:
  void reserve(size_type capacity) { rep().reserve(capacity); }
  size_type capacity() const { return rep().capacity(); }
  void shrink_to_fit() { rep().shrink_to_fit(); }

  template <typename H>
  friend H AbslHashValue(H h, const flat_map& map) {
    return H::combine(std::move(h), map.rep());
  }

 private:
  friend const Rep& internal_flat::GetInternalRepresentation<>(
      const flat_map& container);

  constexpr Rep& rep() { return impl_.rep; }
  constexpr const Rep& rep() const { return impl_.rep; }

  template <typename K, typename... Args>
  std::pair<iterator, bool> try_emplace_impl(K&& k, Args&&... args) {
    auto it = lower_bound(k);
    if ((it == end()) || (key_comp()(k, it->first))) {
      return {internal_flat::container_emplace(
                  &rep(), it, std::piecewise_construct,
                  std::forward_as_tuple(std::forward<K>(k)),
                  std::forward_as_tuple(std::forward<Args>(args)...)),
              true};
    }
    return {it, false};
  }

  template <typename K, typename... Args>
  iterator try_emplace_hint_impl(const_iterator hint, K&& k, Args&&... args) {
    const auto key_hint = gtl::make_iterator_first(hint);
    auto result = internal_flat::verify_hint(rep(), key_hint, k, key_comp());
    switch (result.first) {
      case internal_flat::VerifyHintResult::kPerfectHint:
        return internal_flat::container_emplace(
            &rep(), result.second.base(), std::piecewise_construct,
            std::forward_as_tuple(std::forward<K>(k)),
            std::forward_as_tuple(std::forward<Args>(args)...));
      case internal_flat::VerifyHintResult::kKeyExists:
        return begin() + (result.second.base() - begin());
      case internal_flat::VerifyHintResult::kBadHint:
        return try_emplace_impl(std::forward<K>(k), std::forward<Args>(args)...)
            .first;
    }
  }

  constexpr iterator_first<iterator> keys_begin() {
    return make_iterator_first(begin());
  }
  constexpr iterator_first<iterator> keys_end() {
    return make_iterator_first(end());
  }
  constexpr iterator_first<const_iterator> keys_begin() const {
    return make_iterator_first(begin());
  }
  constexpr iterator_first<const_iterator> keys_end() const {
    return make_iterator_first(end());
  }

  friend bool operator==(const flat_map& x, const flat_map& y) {
    return x.rep() == y.rep();
  }
  friend bool operator!=(const flat_map& x, const flat_map& y) {
    return !(x == y);
  }
  friend bool operator<(const flat_map& x, const flat_map& y) {
    return x.rep() < y.rep();
  }
  friend bool operator>(const flat_map& x, const flat_map& y) { return y < x; }
  friend bool operator<=(const flat_map& x, const flat_map& y) {
    return !(y < x);
  }
  friend bool operator>=(const flat_map& x, const flat_map& y) {
    return !(x < y);
  }
  friend void swap(flat_map& x, flat_map& y) noexcept { return x.swap(y); }

  internal_flat::Impl<value_compare, Rep, false, init_type> impl_;
};

// Creates a constant flat_map object containing the elements from `values`.
// This function can be called in a constant expression if `K`, `V`, and `Cmp`
// are literal types, `cmp(k1, k2)` can be called in a constant expression
// for any two keys in `values`, and all keys are unique.
// The keys in `values` do not need to be sorted.
// Eg:
//    constexpr auto kMap = gtl::fixed_flat_map_of<absl::string_view, int>(
//        {{"foo", 1}, {"bar", 2}, {"baz", 3}});
template <typename K, typename V, typename Cmp = std::less<K>,
          int&... ExplicitBarrier, size_t N>
constexpr auto fixed_flat_map_of(std::array<std::pair<K, V>, N> data,
                                 const Cmp& cmp = Cmp{}) {
  // std::array::data is not constexpr on empty array on some platforms.
  if constexpr (N != 0) {
    internal_flat::ConstexprSort(data, internal_flat::value_compare<Cmp>(cmp));
    internal_flat::VerifyUnique(data.data(), data.size(),
                                internal_flat::value_compare<Cmp>(cmp));
  }
  return flat_map<K, V, Cmp, std::array<std::pair<const K, V>, N>>(
      sorted_unique_container, cmp,
      internal_flat::MakeKeysConst(std::move(data)));
}
template <typename K, typename V, typename Cmp = std::less<K>,
          int&... ExplicitBarrier, size_t N>
constexpr auto fixed_flat_map_of(const std::pair<K, V> (&values)[N],
                                 const Cmp& cmp = Cmp{}) {
#if defined(__cplusplus) && __cplusplus < 202002L
  return fixed_flat_map_of(::gtl::to_array(values), cmp);
#else
  return fixed_flat_map_of(::std::to_array(values), cmp);
#endif
}

// Special overload to support an empty list. Arrays can't have a zero length.
// Eg, to allow: gtl::fixed_flat_map_of<int, int>({})
template <typename K, typename V, typename Cmp = std::less<K>>
constexpr auto fixed_flat_map_of(internal_flat::EmptyInitializerList,
                                 const Cmp& cmp = Cmp{}) {
  return flat_map<K, V, Cmp, std::array<std::pair<const K, V>, 0>>(cmp);
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_FLAT_MAP_H_
