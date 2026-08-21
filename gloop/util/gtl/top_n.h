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

// This simple class finds the top n elements of an incrementally provided set
// of elements which you push one at a time.  If the number of elements exceeds
// n, the lowest elements are incrementally dropped.  At the end you get
// a vector of the top elements sorted in descending order (through Take() or
// TakeNondestructive()), or a vector of the top elements but not sorted
// (through TakeUnsorted() or TakeUnsortedNondestructive()).
//
// The value n is specified in the constructor.  If there are p elements pushed
// altogether:
//   The total storage requirements are O(min(n, p)) elements
//   The running time is O(p * log(min(n, p))) comparisons
// If n is a constant, the total storage required is a constant and the running
// time is linear in p.
//
// For p > n uniformly distributed elements, average-case comparison count is
//
//             O(p + n * log(n) * log(p/n))
//
// Here, we have O(p) comparisons between input elements and the lowest element
// seen before, and possibly performing the one-time heapify operation.  After
// the first n - 1 elements, each new element will *not* be ignored
// in a decreasing fraction of cases: n / n + n / (n + 1) + ... + n / p =
//  n * ((1 + 1/2 + 1/3 + ... + 1/p) - (1 + 1/2 + ... + 1/ (n - 1))).
// Recognizing two Harmonic summations, we approximate this expression by
//  C * n * (log(p) - log(n - 1)). So, on average we perform O(n * log(p / n))
// heap insertions and removals, each of which with O(log(n)) comparisons.
// Hence, O(p + n * log(n) * log(p/n)) comparisons overall.
//
// For p >> n, the leading constant in the above expression is 1, and the
// overall performance is comparable to that of a linear scan.

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_TOP_N_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_TOP_N_H_

#include <stddef.h>

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include "absl/log/check.h"

namespace gtl {

// Cmp is an stl binary predicate.  Note that Cmp is the "greater" predicate,
// not the more commonly used "less" predicate.
//
// If you use a "less" predicate here, the TopN will pick out the bottom N
// elements out of the ones passed to it, and it will return them sorted in
// ascending order.
//
// TopN is rule-of-zero copyable and movable if its members are.
template <typename T, typename Cmp = std::greater<T>>
class TopN {
 public:
  // The TopN is in one of the three states:
  //
  //  o unordered: this is the state an instance is originally in,
  //    where the elements are completely orderless.
  //
  //  o bottom_known: in this state, we keep the invariant that there
  //    is at least one element in it, and the lowest element is at
  //    position 0. The elements in other positions remain
  //    unsorted. This state is reached if the state was originally
  //    unordered and a peek_bottom() function call is invoked.
  //
  //  o heap_sorted: in this state, the array is kept as a heap and
  //    there are exactly (limit_+1) elements in the array. This
  //    state is reached when at least (limit_+1) elements are
  //    pushed in.
  //
  //  The state transition graph is as follows:
  //
  //             peek_bottom()                (limit_+1) elements
  //  unordered --------------> bottom_known --------------------> heap_sorted
  //      |                                                           ^
  //      |                      (limit_+1) elements                  |
  //      +-----------------------------------------------------------+
  enum class State { unordered, bottom_known, heap_sorted };

  using value_type = T;

  using UnsortedIterator = typename std::vector<value_type>::const_iterator;

  // 'limit' is the maximum number of top results to return.
  explicit TopN(size_t limit) : TopN(limit, Cmp()) {}
  TopN(size_t limit, const Cmp& cmp) : limit_(limit), cmp_(cmp) {}

  TopN(const TopN& top_n) = default;
  TopN& operator=(const TopN& top_n) = default;

  // Move constructor. `other` is left in the empty state after move.
  TopN(TopN&& other) noexcept
      : elements_(std::move(other.elements_)),
        limit_(other.limit_),
        cmp_(other.cmp_),
        state_(other.state_) {
    other.Reset();
  }

  // Move assignment. `other` is left in the empty state after move.
  TopN& operator=(TopN&& other) noexcept {
    elements_ = std::move(other.elements_);
    limit_ = other.limit_;
    cmp_ = other.cmp_;
    state_ = other.state_;
    other.Reset();
    return *this;
  }

  size_t limit() const { return limit_; }

  // Number of elements currently held by this TopN object.  This
  // will be no greater than 'limit' passed to the constructor.
  size_t size() const { return std::min(elements_.size(), limit_); }

  bool empty() const { return size() == 0; }

  // If you know how many elements you will push at the time you create the
  // TopN object, you can call reserve to preallocate the memory that TopN
  // will need to process all 'n' pushes.  Calling this method is optional.
  void reserve(size_t n) { elements_.reserve(std::min(n, limit_ + 1)); }

  // Push 'v'.  If the maximum number of elements was exceeded, drop the
  // lowest element and return it in 'dropped' (if given). If the maximum is not
  // exceeded, 'dropped' will remain unchanged. 'dropped' may be omitted or
  // nullptr, in which case it is not filled in.
  // Requires: value_type is CopyAssignable, Swappable
  void push(const value_type& v) { push(v, nullptr); }
  void push(const value_type& v, value_type* dropped) {
    PushInternal(v, dropped);
  }

  // Move overloads of push.
  // Requires: value_type is MoveAssignable, Swappable
  void push(value_type&& v) {  // NOLINT(build/c++11)
    push(std::move(v), nullptr);
  }
  void push(value_type&& v, value_type* dropped) {  // NOLINT(build/c++11)
    PushInternal(std::move(v), dropped);
  }

  // Peeks the bottom result without calling Take.
  const value_type& peek_bottom();

  // Destructively extract the elements as a vector, sorted in descending order.
  // Leaves TopN in an empty state.
  std::vector<value_type> Take();
  // Like Take, but with unsorted output.
  std::vector<value_type> TakeUnsorted();
  // A non-destructive version of Take. The caller can continue to push new
  // elements afterwards.
  std::vector<value_type> TakeNondestructive() const;
  // Like TakeNondestructive, but with unsorted output.
  std::vector<value_type> TakeUnsortedNondestructive() const;

  // Variants of Take() that fill an 'output' vector with extracted
  // elements instead of returning a fresh vector.
  void ExtractNondestructive(std::vector<value_type>* output) const;
  void ExtractUnsortedNondestructive(std::vector<value_type>* output) const;

  // Return an iterator to the beginning (end) of the container,
  // with no guarantees about the order of iteration. These iterators are
  // invalidated by mutation of the data structure.
  UnsortedIterator unsorted_begin() const { return elements_.begin(); }
  UnsortedIterator unsorted_end() const { return elements_.begin() + size(); }

  // Accessor for comparator template argument.
  const Cmp& key_comp() const { return cmp_; }

  // This removes all elements.
  void Reset() {
    elements_.clear();
    state_ = State::unordered;
  }

  // This removes all elements and reconfigures the TopN limit.
  void Reset(size_t limit) {
    Reset();
    limit_ = limit;
  }

 private:
  template <typename U>
  void PushInternal(U&& v, value_type* dropped);  // NOLINT(build/c++11)

  // elements_ can be in one of two states:
  //   elements_.size() <= limit_:  elements_ is an unsorted vector of elements
  //      pushed so far.
  //   elements_.size() > limit_:  The last element of elements_ is unused;
  //      the other elements of elements_ are an stl heap whose size is exactly
  //      limit_.  In this case elements_.size() is exactly one greater than
  //      limit_, but don't use "elements_.size() == limit_ + 1" to check for
  //      that because you'll get a false positive if limit_ == size_t(-1).
  std::vector<value_type> elements_;
  size_t limit_;  // Maximum number of elements to find
  Cmp cmp_;       // Greater-than comparison function
  State state_ = State::unordered;
};

// Returns an instance of TopN inferring the type `Cmp` from the passed `cmp`
// argument. This makes it safer and more readable to instantiate TopN with a
// custom or complex (not default-constructible) comparator.
//
// For example, instead of:
//
//    auto cmp = gtl::OrderBy(Extractor(param), gtl::Greater());
//    gtl::TopN<Element, decltype(cmp)> topn(limit, cmp);
//
// It can be simplified to:
//
//    auto topn = gtl::MakeTopN<Element>(gtl::OrderBy(Extractor(param)));
//
// Note: CTAD cannot be used in this case because `T` can't be inferred from
// constructor arguments, and must still be specified by the user.
template <typename T, typename Cmp>
TopN<T, Cmp> MakeTopN(size_t limit, const Cmp& cmp) {
  return TopN<T, Cmp>(limit, cmp);
}

// ----------------------------------------------------------------------
// Implementations of non-inline functions

template <typename T, typename Cmp>
template <typename U>
void TopN<T, Cmp>::PushInternal(U&& v, value_type* dropped) {
  if (limit_ == 0) {
    if (dropped) *dropped = std::forward<U>(v);
    return;
  }
  if (state_ != State::heap_sorted) {
    elements_.push_back(std::forward<U>(v));
    if (state_ == State::unordered ||
        cmp_(elements_.back(), elements_.front())) {
      // Easy case: we just pushed the new element back
    } else {
      // To maintain the bottom_known state, we need to make sure that
      // the element at position 0 is always the smallest. So we put
      // the new element at position 0 and push the original bottom
      // element in the back.
      // Subtle: see http://b/8417410
      using std::swap;  // <link>
      swap(elements_.front(), elements_.back());
    }
    if (elements_.size() == limit_ + 1) {
      // Transition from unsorted vector to a heap.
      std::make_heap(elements_.begin(), elements_.end(), cmp_);
      if (dropped) *dropped = std::move(elements_.front());
      std::pop_heap(elements_.begin(), elements_.end(), cmp_);
      state_ = State::heap_sorted;
    }
  } else {
    // Only insert the new element if it is greater than the least element.
    if (cmp_(v, elements_.front())) {
      elements_.back() = std::forward<U>(v);
      std::push_heap(elements_.begin(), elements_.end(), cmp_);
      if (dropped) *dropped = std::move(elements_.front());
      std::pop_heap(elements_.begin(), elements_.end(), cmp_);
    } else {
      if (dropped) *dropped = std::forward<U>(v);
    }
  }
}

template <typename T, typename Cmp>
auto TopN<T, Cmp>::peek_bottom() -> const value_type& {
  CHECK(!empty());
  if (state_ == State::unordered) {
    auto min_iter = std::max_element(elements_.begin(), elements_.end(), cmp_);
    if (min_iter != elements_.begin()) {
      std::iter_swap(min_iter, elements_.begin());
    }
    state_ = State::bottom_known;
  }
  return elements_.front();
}

template <typename T, typename Cmp>
auto TopN<T, Cmp>::Take() -> std::vector<value_type> {
  std::vector<value_type> out = std::move(elements_);
  if (state_ != State::heap_sorted) {
    std::sort(out.begin(), out.end(), cmp_);
  } else {
    out.pop_back();
    std::sort_heap(out.begin(), out.end(), cmp_);
  }
  Reset();
  return out;
}

template <typename T, typename Cmp>
auto TopN<T, Cmp>::TakeUnsorted() -> std::vector<value_type> {
  std::vector<value_type> out = std::move(elements_);
  if (state_ == State::heap_sorted) {
    out.pop_back();  // Remove the limit_+1'th element.
  }
  Reset();
  return out;
}

template <typename T, typename Cmp>
auto TopN<T, Cmp>::TakeNondestructive() const -> std::vector<value_type> {
  std::vector<value_type> out;
  ExtractNondestructive(&out);
  return out;
}

template <typename T, typename Cmp>
auto TopN<T, Cmp>::TakeUnsortedNondestructive() const
    -> std::vector<value_type> {
  std::vector<value_type> out;
  ExtractUnsortedNondestructive(&out);
  return out;
}

template <typename T, typename Cmp>
void TopN<T, Cmp>::ExtractNondestructive(
    std::vector<value_type>* output) const {
  CHECK(output);
  *output = elements_;
  if (state_ != State::heap_sorted) {
    std::sort(output->begin(), output->end(), cmp_);
  } else {
    output->pop_back();
    std::sort_heap(output->begin(), output->end(), cmp_);
  }
}

template <typename T, typename Cmp>
void TopN<T, Cmp>::ExtractUnsortedNondestructive(
    std::vector<value_type>* output) const {
  CHECK(output);
  *output = elements_;
  if (state_ == State::heap_sorted) {
    // Remove the limit_+1'th element.
    output->pop_back();
  }
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_TOP_N_H_
