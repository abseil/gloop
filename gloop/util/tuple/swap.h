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

//
// Function template swap(Tuple&, Tuple&) swaps two tuples. A standard swap
// idiom is used to swap each element:
//
//   using std::swap;
//   swap(lhs_element, rhs_element);

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_SWAP_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_SWAP_H_

#include <stddef.h>

#include <algorithm>

#include "gloop/util/tuple/for_each.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/ref.h"
#include "gloop/util/tuple/std_tuple.h"
#include "gloop/util/tuple/zip.h"

namespace util {
namespace tuple {

namespace internal_swap {

// All symbols defined within namespace internal_swap are internal
// to swap.h. Do not reference them from outside or your code can break
// without notice.

struct elem_swap {
  template <class Pair>
  constexpr void operator()(const Pair& p) const {
    using ::std::swap;
    swap(get<0>(p), get<1>(p));
  }
};

}  // namespace internal_swap

// This namespace is used to disable argument dependent lookup for functions
// defined in it. Later all symbols from it are brough to namespace util::tuple
// with a using directive.
namespace adl_barrier_swap {

// Swaps two tuples.
template <class T>
constexpr void swap(T& lhs, T& rhs) noexcept {
  for_each(internal_swap::elem_swap(),
           zip(tuple::ref<std_tuple_tag>(lhs), tuple::ref<std_tuple_tag>(rhs)));
}

}  // namespace adl_barrier_swap

// Using directive is used to bring all symbols from namespace
// adl_barrier_swap into util::tuple. adl_barrier_swap is a closed
// namespace that isn't used in any other file. This using directive can't be
// replaced with a bunch of using declarations because using declarations
// enable ADL.
using namespace adl_barrier_swap;

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_SWAP_H_
