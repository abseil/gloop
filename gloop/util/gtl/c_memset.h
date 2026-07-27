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

#include <cstring>
#include <iterator>
#include <type_traits>

#include "absl/algorithm/container.h"
#include "absl/base/internal/hardening.h"
#include "absl/base/macros.h"

namespace gtl {

// A container-based memset().
//
// Wrapper around std::memset. It sets all the bytes owned by the container to
// the given value. The container must have a contiguous underlying buffer.
template <typename C>
  requires(sizeof(typename std::remove_reference_t<C>::value_type) != 1 &&
           std::is_trivial_v<typename std::remove_reference_t<C>::value_type> &&
           absl::container_algorithm_internal::IsPermissibleDestinationRange<
               C>::value)
void c_memset(C&& c, int ch) {
  std::memset(
      std::data(c), ch,
      std::size(c) * sizeof(typename std::remove_reference_t<C>::value_type));
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
template <typename C>
  requires(sizeof(typename std::remove_reference_t<C>::value_type) == 1 &&
           std::is_trivial_v<typename std::remove_reference_t<C>::value_type> &&
           absl::container_algorithm_internal::IsPermissibleDestinationRange<
               C>::value)
ABSL_DEPRECATE_AND_INLINE()
void c_memset(C&& c, typename std::remove_reference_t<C>::value_type ch) {
  absl::c_fill(std::forward<C>(c), ch);
}

// A container-based memset() with explicit byte count and bounds checking.
//
// Wrapper around std::memset, but in some build modes performs a bounds check
// before writing. It sets the first num_bytes bytes inside the container to the
// given value. The container must have a contiguous underlying buffer.
template <typename C>
  requires(std::is_trivial_v<typename std::remove_reference_t<C>::value_type> &&
           absl::container_algorithm_internal::IsPermissibleDestinationRange<
               C>::value)
void c_memset_n(C&& c, int ch, size_t num_bytes) {
  absl::base_internal::HardeningAssertLE(
      num_bytes,
      std::size(c) * sizeof(typename std::remove_reference_t<C>::value_type));
  std::memset(std::data(c), ch, num_bytes);
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_C_MEMSET_H_
