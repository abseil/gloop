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

// Internal helpers for flat_set.h and flat_map.h.

// IWYU pragma: private

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_FLAT_INTERNAL_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_FLAT_INTERNAL_H_

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "gloop/util/gtl/unchecked_tags.h"

namespace gtl {
namespace internal_flat {

template <typename T, typename = void>
struct HasReserve : std::false_type {};

template <typename T>
struct HasReserve<T, std::void_t<decltype(std::declval<T&>().reserve(239))>>
    : std::true_type {};

template <typename T>
constexpr bool kHasReserve = HasReserve<T>();

template <typename T>
struct ConstexprHelper {
  static constexpr void Assign(T* dest, T* src) {
    // std::move calls count towards the constexpr evaluation limit, so avoid
    // those.
    *dest = static_cast<T&&>(*src);
  }
};

// std::pair's assignment operator and swap aren't constexpr before C++20.
template <typename T, typename U>
struct ConstexprHelper<std::pair<T, U>> {
  static constexpr void Assign(std::pair<T, U>* dest, std::pair<T, U>* src) {
    ConstexprHelper<T>::Assign(&dest->first, &src->first);
    ConstexprHelper<U>::Assign(&dest->second, &src->second);
  }
};

// std::tuple's assignment operator and swap aren't constexpr before C++20.
template <class... Args>
struct ConstexprHelper<std::tuple<Args...>> {
  static constexpr void Assign(std::tuple<Args...>* dest,
                               std::tuple<Args...>* src) {
    std::apply(
        [&](auto&... dest_arg) {
          std::apply(
              [&](auto&... src_arg) {
                (ConstexprHelper<Args>::Assign(&dest_arg, &src_arg), ...);
              },
              *src);
        },
        *dest);
  }
};

template <typename T>
constexpr void abort_duplicate_entry(const T /*a*/, const T /*b*/) {
  std::abort();
}

template <typename T, typename Cmp>
constexpr void VerifyUnique(T* const data, const size_t size, Cmp cmp) {
  // Verify that there are no duplicates.
  for (size_t i = 0; i + 1 < size; ++i) {
    // On failure, make it non-constexpr
    if (!cmp(data[i], data[i + 1])) abort_duplicate_entry(data[i], data[i + 1]);
  }
}

// Simple stable merge sort function for constexpr support. std::stable_sort
// is not constexpr.
template <typename T, typename Cmp>
constexpr void Sort(T* const data, const size_t size, Cmp cmp,
                    T* const tmp_buffer) {
  if (size < 2) return;
  size_t half = size / 2;
  Sort(data, half, cmp, tmp_buffer);
  Sort(data + half, size - half, cmp, tmp_buffer);
  T* p1 = data;
  T* p2 = data + half;
  T* out = tmp_buffer;
  while (p1 != data + half && p2 != data + size) {
    T*& p = !cmp(*p2, *p1) ? p1 : p2;
    ConstexprHelper<T>::Assign(out++, p++);
  }
  while (p1 != data + half) {
    ConstexprHelper<T>::Assign(out++, p1++);
  }
  for (int i = 0; i < (p2 - data); ++i) {
    ConstexprHelper<T>::Assign(data + i, tmp_buffer + i);
  }
}

template <size_t N, typename T, typename Cmp>
constexpr void ConstexprSort(std::array<T, N>& data, Cmp cmp) {
  bool is_sorted = true;
  // Note: calls to std::array::operator[] counts towards the constexpr
  // evaluation limit, so avoid those.
  const T* it = data.data() + 1;
  for (size_t i = 1; i < N && is_sorted; ++i, ++it) {
    is_sorted = !cmp(*it, it[-1]);
  }
  // Note that the check was added because it is O(n) for sorted data,
  // increasing the maximum number of elements we can have in constexpr
  // evaluation.
  if (is_sorted) return;

  std::array<T, N> buffer = data;  // making a copy to avoid hitting a
                                   // potentially non-constexpr `T()`.
  Sort(data.data(), N, cmp, buffer.data());
}

template <typename Key>
struct DefaultLessHelper {
  using type = std::less<Key>;
};

template <>
struct DefaultLessHelper<absl::string_view> {
  using type = std::less<>;
};
template <>
struct DefaultLessHelper<std::string> {
  using type = std::less<>;
};
template <>
struct DefaultLessHelper<absl::Cord> {
  using type = std::less<>;
};

template <typename Key>
using DefaultLess = typename DefaultLessHelper<Key>::type;

// Same idea as std::forward_as_tuple, but for std::pair specifically.
template <class T, class U>
constexpr std::pair<T&&, U&&> forward_as_pair(T&& t, U&& u) noexcept {
  return {std::forward<T>(t), std::forward<U>(u)};
}

// Enum to control whether to use insert() vs insert_or_assign() logic
// in the shared utility functions below.
enum class OnKeyCollision { kKeepOldValue, kAssignNewValue };

template <typename Rep, typename... Args>
void reconstruct_element(Rep* rep, typename Rep::iterator pos, Args&&... args) {
  using allocator_traits = std::allocator_traits<typename Rep::allocator_type>;
  auto allocator = rep->get_allocator();
  auto* ptr = std::addressof(*pos);
  allocator_traits::destroy(allocator, ptr);
  allocator_traits::construct(allocator, ptr, std::forward<Args>(args)...);
}

template <typename Rep, typename... Args>
typename Rep::iterator emplace_non_assignable(Rep* rep,
                                              typename Rep::const_iterator pos,
                                              Args&&... args) {
  if (pos == rep->end()) {
    rep->emplace_back(std::forward<Args>(args)...);
    return rep->end() - 1;
  }
  auto shift = pos - rep->begin();
  rep->emplace_back(std::forward<Args>(args)...);
  typename Rep::value_type tmp = std::move(rep->back());
  auto it = rep->begin() + shift;
  for (auto last = rep->end() - 1; last != it; --last) {
    reconstruct_element(rep, last, std::move(last[-1]));
  }
  reconstruct_element(rep, it, std::move(tmp));
  return it;
}

template <typename Rep, typename... Args>
typename Rep::iterator container_emplace(Rep* rep,
                                         typename Rep::const_iterator pos,
                                         Args&&... args) {
  using value_type = typename Rep::value_type;
  if constexpr (std::is_move_assignable_v<value_type>) {
    return rep->emplace(pos, std::forward<Args>(args)...);
  } else {
    return emplace_non_assignable(rep, pos, std::forward<Args>(args)...);
  }
}

template <typename Rep>
auto erase_non_assignable(Rep* rep, typename Rep::const_iterator it,
                          int n = 1) {
  assert(n > 0);
  auto shift = it - rep->begin();
  for (auto first = rep->begin() + shift, last = rep->end() - n; first != last;
       ++first) {
    reconstruct_element(rep, first, std::move(first[n]));
  }
  while (n--) rep->pop_back();
  return rep->begin() + shift;
}

template <typename Rep>
auto container_erase(Rep* rep, typename Rep::const_iterator it) {
  using value_type = typename Rep::value_type;
  if constexpr (std::is_move_assignable_v<value_type>) {
    return rep->erase(it);
  } else {
    return erase_non_assignable(rep, it);
  }
}

template <typename Rep>
auto container_erase(Rep* rep, typename Rep::const_iterator first,
                     typename Rep::const_iterator last) {
  using value_type = typename Rep::value_type;
  if constexpr (std::is_move_assignable_v<value_type>) {
    return rep->erase(first, last);
  } else {
    auto n = std::distance(first, last);
    if (n > 0) {
      return erase_non_assignable(rep, first, n);
    } else {
      return rep->begin() + (first - rep->begin());
    }
  }
}

template <typename Rep, typename Predicate>
size_t erase_if_non_assignable(Rep* rep, Predicate pred) {
  auto last = rep->end();
  auto first = std::find_if(rep->begin(), last, pred);
  if (first == last) return 0;
  auto it = first;
  while (++it != last) {
    // CAUTION: using `pred(*it)` may cause false positive errors when compiling
    // with -Wthread-safety-analysis for the reasons unknown. Using std::invoke
    // as a workaround.
    if (!std::invoke(pred, *it)) {
      reconstruct_element(rep, first, std::move(*it));
      ++first;
    }
  }
  size_t n_erased = std::distance(first, last);
  if (n_erased > 0) {
    erase_non_assignable(rep, first, n_erased);
  }
  return n_erased;
}

template <typename Rep, typename Predicate>
size_t container_erase_if(Rep* rep, Predicate pred) {
  using value_type = typename Rep::value_type;
  if constexpr (std::is_move_assignable_v<value_type>) {
    const auto it = std::remove_if(rep->begin(), rep->end(), std::move(pred));
    auto n_erased = std::distance(it, rep->end());
    rep->erase(it, rep->end());
    return n_erased;
  } else {
    return erase_if_non_assignable(rep, std::move(pred));
  }
}

// Implementation of insert() variants. Note: since these methods are heavily
// templated, we keep their names different to avoid any ambiguity.

template <typename Rep, typename V, typename Cmp>
typename Rep::iterator multi_insert(Rep* rep, V&& v, Cmp cmp) {
  return container_emplace(rep,
                           std::upper_bound(rep->begin(), rep->end(), v, cmp),
                           std::forward<V>(v));
}

template <OnKeyCollision policy = OnKeyCollision::kKeepOldValue, typename Rep,
          typename V, typename Cmp>
std::pair<typename Rep::iterator, bool> insert(Rep* rep, V&& v, Cmp cmp) {
  auto it = std::lower_bound(rep->begin(), rep->end(), v, cmp);
  if ((it == rep->end()) || (cmp(v, *it))) {
    return {container_emplace(rep, it, std::forward<V>(v)), true};
  } else {
    if constexpr (policy == OnKeyCollision::kAssignNewValue) {
      static_assert(
          !std::is_lvalue_reference_v<V>,
          "kAssignNewValue does not support pair<K, V>&. Use forward_as_pair.");
      it->second = std::forward<typename V::second_type>(v.second);
    }
    return {it, false};
  }
}

// Possible alignments of 'hint' and 'v' against each other.
enum class VerifyHintResult {
  kPerfectHint,  // Insertion of 'v' on 'hint' is ordered by 'cmp'.
  kKeyExists,    // A duplicate was found at 'hint' or at 'hint-1'.
  kBadHint,      // Hint couldn't be used. Fallback to default operation.
};
// Finds out how 'hint' could be used for insertion of 'v'. Returns a pair of
// 'VerifyHintResult' and 'Iterator' where the latter is:
// - for 'kPerfectHint': set to the good position for insertion;
// - for 'kKeyExists': set to the location of the duplicate;
// - for 'kBadHint': not set.
template <typename Container, typename V, typename Cmp, typename Iterator>
std::pair<VerifyHintResult, Iterator> verify_hint(const Container& c,
                                                  const Iterator& hint,
                                                  const V& v, const Cmp& cmp) {
  if (hint != c.end() && !cmp(v, *hint)) {
    return cmp(*hint, v)
               ? std::make_pair(VerifyHintResult::kBadHint, Iterator())
               : std::make_pair(VerifyHintResult::kKeyExists, hint);
  }
  if (hint != c.begin() && !cmp(*(hint - 1), v)) {
    return cmp(v, *(hint - 1))
               ? std::make_pair(VerifyHintResult::kBadHint, Iterator())
               : std::make_pair(VerifyHintResult::kKeyExists, hint - 1);
  }
  return {VerifyHintResult::kPerfectHint, hint};
}

template <typename Rep, typename V, typename Cmp>
typename Rep::iterator multi_insert_hint(Rep* rep,
                                         typename Rep::const_iterator hint,
                                         V&& v, Cmp cmp) {
  if (hint != rep->end() && cmp(*hint, v)) {
    // Hint points below an actual location of `v`. Insert at lower bound.
    hint = std::lower_bound(rep->begin(), rep->end(), v, cmp);
  } else if (hint != rep->begin() && cmp(v, *(hint - 1))) {
    // Hint points above an actual location of `v`. Insert at upper bound.
    hint = std::upper_bound(rep->begin(), rep->end(), v, cmp);
  }
  // Either hint is already perfect, or we have just adjusted it as needed.
  return container_emplace(rep, hint, std::forward<V>(v));
}

template <OnKeyCollision policy = OnKeyCollision::kKeepOldValue, typename Rep,
          typename V, typename Cmp>
typename Rep::iterator insert_hint(Rep* rep, typename Rep::const_iterator hint,
                                   V&& v, Cmp cmp) {
  const auto result = verify_hint(*rep, hint, v, cmp);
  switch (result.first) {
    case VerifyHintResult::kPerfectHint:
      return container_emplace(rep, result.second, std::forward<V>(v));
    case VerifyHintResult::kKeyExists: {
      const auto existing_it = rep->begin() + (result.second - rep->begin());
      if constexpr (policy == OnKeyCollision::kAssignNewValue) {
        static_assert(!std::is_lvalue_reference_v<V>,
                      "kAssignNewValue does not support pair<K, V>&. Use "
                      "forward_as_pair.");
        existing_it->second = std::forward<typename V::second_type>(v.second);
      }
      return existing_it;
    }
    case VerifyHintResult::kBadHint:
      // Hint is useless, fallback to insert without hint.
      return insert<policy>(rep, std::forward<V>(v), cmp).first;
  }
}

template <typename init_type, typename Rep, typename InputIterator,
          typename Cmp>
void multi_insert_range(Rep* rep, InputIterator first, InputIterator last,
                        Cmp cmp) {
  // We use stable_sort to guarantee that the relative order of multiple equal
  // inserted values is preserved.
  if constexpr (std::is_copy_assignable_v<typename Rep::value_type>) {
    const auto original_size = rep->size();
    rep->insert(rep->end(), first, last);
    std::stable_sort(rep->begin() + original_size, rep->end(), cmp);
    std::inplace_merge(rep->begin(), rep->begin() + original_size, rep->end(),
                       cmp);
  } else {
    auto old = std::exchange(*rep, {});
    std::vector<init_type> tmp(first, last);
    std::stable_sort(tmp.begin(), tmp.end(), cmp);
    std::merge(std::make_move_iterator(old.begin()),
               std::make_move_iterator(old.end()),
               std::make_move_iterator(tmp.begin()),
               std::make_move_iterator(tmp.end()), std::back_inserter(*rep),
               cmp);
  }
}

// Removes duplicates from the given container, which must be sorted.
template <typename Rep, typename Cmp>
void remove_duplicates(Rep* rep, Cmp cmp) {
  using value_type = typename Rep::value_type;
  auto eq = [cmp](const value_type& left, const value_type& right) {
    // We don't need to compare both ways.
    // comp(right, left) can never be true because the range is sorted.
    return !cmp(left, right);
  };
  rep->erase(std::unique(rep->begin(), rep->end(), eq), rep->end());
}

template <typename Rep, typename InputIterator, typename Cmp>
void insert_range(Rep* rep, InputIterator first, InputIterator last, Cmp cmp) {
  if (first == last) return;
  // First insert the whole range allowing duplicates if any.
  multi_insert_range<typename Rep::value_type>(rep, first, last, cmp);
  // Then remove any duplicates.
  remove_duplicates(rep, cmp);
}

template <typename init_type, typename Rep, typename InputIterator,
          typename Cmp>
void insert_range_non_assignable(Rep* rep, InputIterator first,
                                 InputIterator last, Cmp cmp) {
  if (first == last) return;
  // Same as `insert_range`: stable_sort, merge and deduplication. The
  // difference:
  //  - sorting uses intermediate buffer (`insert_range` does it in place);
  //  - merge and dedup is done in one pass (`insert_range` does it in two).
  auto old = std::exchange(*rep, {});
  std::vector<init_type> tmp(first, last);
  if (tmp.size() > 1) std::stable_sort(tmp.begin(), tmp.end(), cmp);
  if constexpr (kHasReserve<Rep>) {
    rep->reserve(old.size() + tmp.size());
  }

  auto first1 = old.begin(), last1 = old.end();
  auto first2 = tmp.begin(), last2 = tmp.end();

  const auto maybe_append = [&](auto& it) {
    if (rep->empty() || cmp(rep->back(), *it)) {
      rep->emplace_back(std::move(*it));
    }
    ++it;
  };

  // merging two sorted containers, duplicate elements are skipped.
  while (first1 != last1 && first2 != last2) {
    // in case of equality `old` has priority.
    if (cmp(*first2, *first1)) {
      maybe_append(first2);
    } else {
      maybe_append(first1);
    }
  }
  // `old` does not contain any duplicates, only need to check the head.
  if (first1 != last1 && (rep->empty() || cmp(rep->back(), *first1))) {
    std::move(first1, last1, std::back_inserter(*rep));
  }
  // `tmp` may contain duplicates, need to check all elements.
  while (first2 != last2) {
    maybe_append(first2);
  }
}

// Constructor helpers: sorts and optionally deduplicates the given container.
template <bool allow_duplicates, typename Rep, typename Cmp>
void prepare_container(Rep* rep, Cmp cmp) {
  // We use stable_sort to guarantee that the relative order of multiple equal
  // values in the container is preserved.
  std::stable_sort(rep->begin(), rep->end(), cmp);
  if constexpr (!allow_duplicates) {
    remove_duplicates(rep, cmp);
  }
}
template <typename init_type, bool allow_duplicates, typename Rep, typename Cmp>
void prepare_container_non_assignable(Rep* rep, Cmp cmp) {
  std::vector<init_type> tmp(std::make_move_iterator(rep->begin()),
                             std::make_move_iterator(rep->end()));
  rep->clear();
  prepare_container<allow_duplicates>(&tmp, cmp);
  std::move(tmp.begin(), tmp.end(), std::back_inserter(*rep));
}

template <typename Rep, typename V, typename Cmp>
std::pair<typename Rep::iterator, bool> insert_or_assign(Rep* rep, V&& v,
                                                         Cmp cmp) {
  return insert<OnKeyCollision::kAssignNewValue>(rep, std::forward<V>(v), cmp);
}

template <typename Rep, typename V, typename Cmp>
typename Rep::iterator insert_or_assign_hint(Rep* rep,
                                             typename Rep::const_iterator hint,
                                             V&& v, Cmp cmp) {
  return insert_hint<OnKeyCollision::kAssignNewValue>(rep, hint,
                                                      std::forward<V>(v), cmp);
}

template <typename Iterator>
constexpr typename std::iterator_traits<Iterator>::difference_type count(
    const std::pair<Iterator, Iterator>& rng) {
  return std::distance(rng.first, rng.second);
}

template <typename Rep, typename Iterator>
typename Rep::size_type erase(Rep* rep,
                              const std::pair<Iterator, Iterator>& rng) {
  const auto count_rng = count(rng);
  if (count_rng != 0) {
    container_erase(rep, rng.first, rng.second);
  }
  return count_rng;
}

// Finds a value in an ordered sequence.
template <typename Iterator, typename V, typename Cmp>
constexpr Iterator ordered_find(Iterator begin, Iterator end, const V& v,
                                Cmp cmp) {
  auto it = std::lower_bound(begin, end, v, cmp);
  if (it == end) return it;
  if (cmp(v, *it)) return end;
  return it;
}

// Value comparator for flat_map. Inherits from Compare for Empty Base Class
// Optimization to work.
template <typename Compare>
struct value_compare : public Compare {
  constexpr value_compare() = default;
  constexpr explicit value_compare(Compare cmp) : Compare(std::move(cmp)) {}

  template <typename T, typename U>
  constexpr bool operator()(const T& left, const U& right) const {
    return Compare::operator()(left.first, right.first);
  }
};

// Helpers for handling "clearable" vs "non-clearable" containers in non-uniform
// ways. See the "Impl" class below for details.
template <typename, typename = void>
struct has_clear : std::false_type {};
template <typename T>
struct has_clear<T, std::void_t<decltype(std::declval<T>().clear())>>
    : std::true_type {};

template <typename T>
constexpr T&& MoveIf(std::true_type, T* t) {
  return std::move(*t);
}
template <typename T>
constexpr const T& MoveIf(std::false_type, T* t) {
  return *t;
}
template <typename T>
constexpr void ClearIf(std::true_type, T* t) {
  t->clear();
}
template <typename T>
constexpr void ClearIf(std::false_type, T*) {}

// EBCO-enabled storage for flat_set and flat_map. The reason why we use this
// instead of std::tuple is that tuple lacks "piecewise" constructor.
//
// To ensure flat_set/map's invariants, we must clear() a moved-from flat
// container. This is not possible if its Rep does not have a clear() method. In
// that case, we fall back to a copy (at compile time).
template <typename Compare, typename Rep, bool allow_duplicates = false,
          typename init_type = typename Rep::value_type>
struct Impl : private Compare {
  constexpr Impl(const Impl& other) = default;
  Impl& operator=(const Impl& other) {
    if (this == &other) return *this;
    Compare::operator=(other.cmp());
    if constexpr (std::is_copy_assignable_v<typename Rep::value_type>) {
      rep = other.rep;
    } else {
      rep.clear();
      if constexpr (kHasReserve<Rep>) {
        rep.reserve(other.rep.size());
      }
      std::copy(other.rep.begin(), other.rep.end(), std::back_inserter(rep));
    }
    return *this;
  }
  // We always copy Compare; moving it could leave 'other' in an invalid state.
  constexpr Impl(Impl&& other) noexcept(
      std::is_nothrow_copy_constructible_v<Compare> &&
      (has_clear<Rep>::value ? std::is_nothrow_move_constructible_v<Rep>
                             : std::is_nothrow_copy_constructible_v<Rep>))
      : Compare(other.cmp()),  // NOLINT misc-move-constructor-init
        rep(MoveIf(has_clear<Rep>(), &other.rep)) {
    ClearIf(has_clear<Rep>(), &other.rep);
  }
  Impl& operator=(Impl&& other) noexcept(
      std::is_nothrow_copy_assignable_v<Compare> &&
      (has_clear<Rep>::value ? std::is_nothrow_move_assignable_v<Rep>
                             : std::is_nothrow_copy_assignable_v<Rep>)) {
    Compare::operator=(other.cmp());
    rep = MoveIf(has_clear<Rep>(), &other.rep);
    ClearIf(has_clear<Rep>(), &other.rep);
    return *this;
  }

  constexpr explicit Impl(const Compare& cmp) : Compare(cmp), rep() {
    // Assert invariants to avoid any funny business of the part of Rep's
    // default constructor, as it is user-supplied.
    assert(check_invariants());
  }

  Impl(const Compare& cmp, Rep rep_arg)
      : Compare(cmp), rep(std::move(rep_arg)) {
    using value_type = typename Rep::value_type;
    if constexpr (std::is_move_assignable_v<value_type>) {
      prepare_container<allow_duplicates>(&rep, cmp);
    } else {
      prepare_container_non_assignable<init_type, allow_duplicates>(&rep, cmp);
    }
    assert(check_invariants());
  }

  // This constructor asserts that the Rep instance constructed from args need
  // not be sorted and possibly deduplicated. It says "sorted_container_t", but
  // if allow_duplicates is false we really mean that the rep is sorted and
  // that entries are unique.
  template <typename... Args>
  constexpr Impl(sorted_container_t, const Compare& cmp, Args&&... args)
      : Compare(cmp), rep(std::forward<Args>(args)...) {
    assert(check_invariants());
  }

  // Converting Impl to Compare may invoke a conversion constructor, rather than
  // Compare's copy constructor (e.g. if Compare is std::function<...>).
  // Do not allow relying on the implicit conversion, forcing the explicit cast
  // through this method.
  constexpr const Compare& cmp() const { return *this; }

  Rep rep;

 private:
  constexpr bool check_invariants() const {
    if (!rep.empty()) {
      auto next_it = rep.begin();
      for (auto it = next_it++; next_it != rep.end(); it = next_it++) {
        if (allow_duplicates ? cmp()(*next_it, *it) : !cmp()(*it, *next_it)) {
          return false;
        }
      }
    }
    return true;
  }
};

// Allows external libraries such as util_memory::EstimateMemoryOwned() to
// access the internal representation.
template <typename R, typename T>
const R& GetInternalRepresentation(const T& container) {
  return container.rep();
}

// Empty type that can be constructed with the `{}` syntax.
// This allows implementations of the fixed_flat_* factories to have an overload
// for empty (ie `{}`) inputs.
// The base implementations use arrays, which don't work for `{}` because the
// arrays can't have zero size.
struct EmptyInitializerList {
  EmptyInitializerList() = default;
  EmptyInitializerList(const EmptyInitializerList&) = delete;
};

// CRTP base class for flat associative containers, that will provide a public
// data() member if the underlying representation has it.

// Default case, when the container doesn't have data().
template <typename Rep, typename CRTP, typename = void>
class FlatContainersMaybeExportData {};

// Specialization for when the container has data().
template <typename Rep, typename CRTP>
class FlatContainersMaybeExportData<
    Rep, CRTP, std::void_t<decltype(std::declval<const Rep>().data())>> {
 public:
  typename Rep::const_pointer data() const noexcept {
    return GetInternalRepresentation<Rep>(this->self()).data();
  }

 private:
  const CRTP& self() const { return static_cast<const CRTP&>(*this); }
};

template <typename K, typename V, std::size_t N, std::size_t... Idx>
constexpr std::array<std::pair<const K, V>, N> MakeKeysConstInternal(
    std::array<std::pair<K, V>, N> ts, std::index_sequence<Idx...>) {
  return {{std::move(ts[Idx])...}};
}

template <typename K, typename V, size_t N>
constexpr std::array<std::pair<const K, V>, N> MakeKeysConst(
    std::array<std::pair<K, V>, N> a) {
  return MakeKeysConstInternal(std::move(a), std::make_index_sequence<N>{});
}

}  // namespace internal_flat
}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_FLAT_INTERNAL_H_
