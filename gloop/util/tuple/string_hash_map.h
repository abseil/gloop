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

// Defines tuple intrinsics for gtl::string_hash_map<>::value_type so it can be
// used with algorithms in //gloop/util/tuple.

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_STRING_HASH_MAP_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_STRING_HASH_MAP_H_

#include <cstdlib>
#include <type_traits>
#include <utility>

#include "absl/strings/string_view.h"
#include "gloop/util/gtl/string_hash_map.h"
#include "gloop/util/tuple/intrinsics.h"

namespace internal_string_hash_map_node {

template <::size_t>
void get();

template <::size_t N, class T>
decltype(get<N>(std::declval<T>())) get_impl(T&& t) {
  return get<N>(::std::forward<T>(t));
}

}  // namespace internal_string_hash_map_node

namespace util {
namespace tuple {

struct string_hash_map_node_tag {};

template <>
struct intrinsics<string_hash_map_node_tag> {
  using has_all_elements = std::true_type;

  template <class K, class V>
  struct assemble {
    static_assert(std::is_same<K, absl::string_view>::value, "");
    using type = typename ::gtl::string_hash_map<V>::value_type;
  };

  template <::size_t N, class T>
  struct element : std::conditional<N == 0, typename T::first_type,
                                    typename T::second_type> {
    static_assert(N < 2, "");
  };

  template <class T>
  using size = ::std::integral_constant<::size_t, 2>;

  template <::size_t N, class T>
  static decltype(internal_string_hash_map_node::get_impl<N>(std::declval<T>()))
  get(T&& t) {
    return internal_string_hash_map_node::get_impl<N>(::std::forward<T>(t));
  }

  template <::size_t N, class T>
  static constexpr const char* name() {
    return N == 0 ? "key" : "value";
  }
};

}  // namespace tuple
}  // namespace util

namespace gtl {
namespace internal_string_hash_map {

template <typename V>
::util::tuple::string_hash_map_node_tag get_tuple_tag(const Node<V>&);

}  // namespace internal_string_hash_map
}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_STRING_HASH_MAP_H_
