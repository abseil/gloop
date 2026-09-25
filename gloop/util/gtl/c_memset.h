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

// Utility functions to call memset on a container with a byte value. Before
// writing the value, it checks that the container has enough space to hold the
// number of bytes to write. This is to replace std::memset.

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_C_MEMSET_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_C_MEMSET_H_

#include <cstddef>
#include <cstring>
#include <iterator>
#include <type_traits>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/base/macros.h"
#include "gloop/util/gtl/c_mem_internal.h"

namespace gtl {

// A container-based memset().
//
// Wrapper around std::memset. It sets all the bytes owned by the container to
// the given value. The container must have a contiguous underlying buffer.
// Multidimensional arrays are not supported.
template <typename C>
std::enable_if_t<c_mem_internal::kIsTrivialDestRange<C> &&
                     !c_mem_internal::kCanUseCFill<C>,
                 void>
c_memset(C&& c, int ch) {
  std::memset(std::data(c), ch, c_mem_internal::byte_size(c));
}

// A container-based memset().
//
// Specialized for containers of single-byte sized elements. Wrapper around
// absl::c_fill().
//
// This overload requires the element type of the container to be trivial,
// because 1) it may be used to write to memory where objects are not
// constructed yet, thus requiring the element type to be at least trivially
// copyable, and 2) it ends the lifetime of elements that were previously in the
// container if there were any, which requires the element type to have implicit
// lifetimes.
// Multidimensional arrays are not supported.
template <typename C>
ABSL_DEPRECATE_AND_INLINE()
std::enable_if_t<c_mem_internal::kCanUseCFill<C>, void> c_memset(
    C&& c, c_mem_internal::element_type_t<C> ch) {
  absl::c_fill(std::forward<C>(c), ch);
}

// A container-based memset() with explicit byte count and bounds checking.
//
// Wrapper around std::memset, but in some build modes performs a bounds check
// before writing. It sets the first num_bytes bytes inside the container to the
// given value. The container must have a contiguous underlying buffer.
// Multidimensional arrays are not supported.
template <typename C>
std::enable_if_t<c_mem_internal::kIsTrivialDestRange<C>, void> c_memset_n(
    C&& c, int ch, size_t num_bytes) {
  c_mem_internal::CheckDestPreconditions(c, num_bytes);
  std::memset(std::data(c), ch, num_bytes);
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_C_MEMSET_H_
