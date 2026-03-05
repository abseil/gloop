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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_SET_VIEW_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_SET_VIEW_H_

#include <functional>
#include <initializer_list>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "gloop/util/gtl/associative_view_internal.h"  // IWYU pragma: export
#include "gloop/util/gtl/iterator_adaptors.h"

namespace gtl {

// A type that is provided to `gtl::SetView` as `ExtraLookupTypes` parameter
// in order to add lookup overloads.
//
// Example (please see <link> first):
//
//   using WithThreadIdLookup = gtl::AlsoSupportsLookupWith<std::thread::id>;
//   template <typename T>
//   using ThreadSetView = gtl::SetView<std::thread, WithThreadIdLookup>;
//
//   std::set<std::thread, ThreadCmp> threads = ...;
//   ThreadSetView thread_set_view = threads;
//
//   std::thread::id id = ...;
//   auto it = thread_set_view.find(id);  // Didn't have to create a thread!
//
// By default, `gtl::SetView` sets ExtraLookupTypes to
// `gtl::AlsoSupportsLookupWith<>` except for `std::string` and `absl::Cord` for
// which it is `gtl::AlsoSupportsLookupWith<absl::string_view>` allowing you to
// take advantage of sets that support heterogeneous lookup.
using ::gtl::AlsoSupportsLookupWith;  // NOLINT(misc-unused-using-decls)

// SetView is a type-erased, read-only view for associative containers. This
// class supports a useful subset of operations that are found in most ordered
// and unordered sets, namely find(), contains(), begin(), and end(), and it
// expects underlying sets to support the same operations (contains is
// optional).
//
// Note that SetView does not require or imply ordered or unique values. Code
// that uses SetView should generally handle unordered and repeated values.
//
// SetView does not take ownership of the underlying container. Callers must
// ensure that containers outlive any SetViews that point to them.
//
// The overhead of SetView should be substantially lower than a deep copy, which
// makes it useful for handling different set types when the alternatives (such
// as using a template or a specific type of set) are cumbersome or impossible.
//
// To write a function that can accept std::set, std::unordered_set, or
// absl::flat_hash_set as inputs, you can use SetView as a function argument:
//
//   void MyFunction(SetView<string> names);
//
// You can invoke MyFunction with any compatible associative container:
//
//  absl::flat_hash_set<string> names = ...;
//  MyFunction(names);
//
// Or an initializer list:
//
//  MyFunction({"a", "b"});
//
// Note that repeated values and multi-sets are also valid:
//
//  MyFunction({"a", "a"});
//
//  gtl::flat_multiset<string> multi = {"b", "b"};
//  MyFunction(multi);
template <typename K,
          typename ExtraLookupTypes =
              internal_associative_view::DefaultExtraLookupForKey<K>>
class ABSL_ATTRIBUTE_VIEW SetView
    : public internal_associative_view::AssociateView<K, K, ExtraLookupTypes> {
  using Base = typename SetView::AssociateView;
  template <typename C>
  using ViewEnabler = typename Base::template ViewEnabler<C>;

  template <typename C>
  using KeyViewEnabler = std::enable_if_t<
      std::is_same_v<std::remove_const_t<typename C::value_type>, K>>;

  struct KeyViewIteratorAdapter {
    template <typename Iter>
    static auto Wrap(Iter&& it) {
      return gtl::make_iterator_first(std::forward<Iter>(it));
    }
  };

 public:
  using size_type = typename Base::size_type;
  using iterator = typename Base::iterator;
  using key_type = typename Base::key_type;
  using value_type = typename Base::value_type;
  using absl_internal_is_view = std::true_type;

  // A default constructed SetView behaves as if it is wrapping an empty
  // container.
  constexpr SetView() : Base() {}

  // Constructs a SetView that wraps the given container. Behavior is
  // undefined if the container is resized after the view is constructed. The
  // resulting view must not outlive the container.
  template <typename C, typename = ViewEnabler<C>>
  constexpr SetView(  // NOLINT(google-explicit-constructor)
      const C& c ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : Base(c) {}

  // Constructs a SetView that wraps an initializer_list.  This constructor is
  // O(1) and performs no allocations nor copies, but find() may have poor
  // runtime characteristics for large lists.
  //
  // Beware of dangling references: `init` binds to temporaries (and
  // initializer_list is a view itself).
  //
  //   void foo(gtl::SetView<int> view);
  //
  //   // Okay, the temporary `init` outlives the view.
  //   foo({1, 2});
  //
  //   // Not okay, the temporary `init` is destroyed on this very line.
  //   auto view = gtl::SetView<int>({1, 2});
  constexpr SetView(  // NOLINT(google-explicit-constructor)
      const std::initializer_list<value_type>& init
          ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : Base(init, std::equal_to<>{}) {}  // Using transparent comparator.

  // Constructs a SetView that wraps a result of gtl::key_view.
  //
  // The resulting SetView is valid so long as the key_view's underlying
  // sequence is valid, and does not refer to the key_view itself.
  //
  // Example:
  //
  //   void foo(gtl::SetView<int> view);
  //
  //   absl::flat_hash_map<int, std::string> map;
  //   foo(gtl::key_view(map));
  //
  template <typename C, typename = KeyViewEnabler<key_view_t<C>>>
  SetView(  // NOLINT(google-explicit-constructor)
      const key_view_t<C>& key_view)
      : Base(key_view.container(), KeyViewIteratorAdapter{}) {}

  // Lookup methods are overloaded: they support `key_type` and all extra types
  // provided via `ExtraLookupTypes`.
  using Base::contains;
  using Base::find;

  using Base::begin;
  using Base::empty;
  using Base::end;
  using Base::size;
};

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_SET_VIEW_H_
