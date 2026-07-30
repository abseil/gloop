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

// This provides a very simple, boring adaptor for a begin and end iterator
// into a range type. This should be used to build range views that work well
// with range based for loops and range based constructors.

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_ITERATOR_RANGE_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_ITERATOR_RANGE_H_

#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/iterator_traits.h"

namespace gtl {

// Diagnostic macro that ensures the iterator is actually valid.
// Otherwise, the contract does not guarantee that the iterator would work.
//
// We only use this macro during function template instantiation rather than
// during class template instantiation, in order to avoid over-eager
// instantiations from blocking compilations.
#if ABSL_INTERNAL_CPLUSPLUS_LANG >= 202002L
#if ABSL_HAVE_ATTRIBUTE(diagnose_if)
#define GTL_INTERNAL_ITERATOR_RANGE_VALIDATE(iter)                  \
  __attribute__((diagnose_if(                                       \
      !std::input_or_output_iterator<iter>,                         \
      "underlying iterator `Iter` is invalid and does not satisfy " \
      "`static_assert(std::input_or_output_iterator<Iter>)`",       \
      "warning")))
#endif
#endif

#ifndef GTL_INTERNAL_ITERATOR_RANGE_VALIDATE
#define GTL_INTERNAL_ITERATOR_RANGE_VALIDATE(iter)
#endif

// A range adaptor for a pair of iterators.
//
// This just wraps two iterators into a range-compatible interface. Nothing
// fancy at all.
template <typename IteratorT>
class iterator_range {
 public:
  using iterator = IteratorT;
  using const_iterator = IteratorT;
  using value_type = typename std::iterator_traits<IteratorT>::value_type;

  iterator_range() : begin_iterator_(), end_iterator_() {}
  iterator_range(IteratorT begin_iterator, IteratorT end_iterator)
      GTL_INTERNAL_ITERATOR_RANGE_VALIDATE(IteratorT)
      : begin_iterator_(std::move(begin_iterator)),
        end_iterator_(std::move(end_iterator)) {}

  IteratorT begin() const { return begin_iterator_; }
  IteratorT end() const { return end_iterator_; }

  // Returns the size of the wrapped range.  Does not participate in overload
  // resolution for non-random-access iterators, since in those cases this is a
  // slow operation (it must walk the entire range and maintain a count).
  //
  // Users who need to know the "size" of a non-random-access iterator_range
  // should pass the range to `absl::c_distance()` instead.
  template <class It = IteratorT>
  std::enable_if_t<absl::base_internal::IsAtLeastIterator<
                       std::random_access_iterator_tag, It>::value,
                   size_t>
  size() const {
    return std::distance(begin_iterator_, end_iterator_);
  }
  // Returns true if this iterator range refers to an empty sequence, and false
  // otherwise.
  bool empty() const { return begin_iterator_ == end_iterator_; }

 private:
  IteratorT begin_iterator_, end_iterator_;
};

// Convenience function for iterating over sub-ranges.
//
// This provides a bit of syntactic sugar to make using sub-ranges
// in for loops a bit easier. Analogous to std::make_pair().
template <typename T>
iterator_range<T> make_range(T x, T y) GTL_INTERNAL_ITERATOR_RANGE_VALIDATE(T) {
  return iterator_range<T>(std::move(x), std::move(y));
}

// Converts std::pair<Iter,Iter> to iterator_range<Iter>. E.g.:
//   for (const auto& e : make_range(m.equal_range(k))) ...
template <typename T>
iterator_range<T> make_range(std::pair<T, T> p)
    GTL_INTERNAL_ITERATOR_RANGE_VALIDATE(T) {
  return iterator_range<T>(std::move(p.first), std::move(p.second));
}

#undef GTL_INTERNAL_ITERATOR_RANGE_VALIDATE

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_ITERATOR_RANGE_H_
