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

// Utility function to call memmove on containers. Before copying, it checks
// that the source container holds the number of bytes to read and that the
// destination container has enough space to hold the number of bytes to write.
// This is to replace memmove.

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_C_MEMMOVE_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_C_MEMMOVE_H_

#include <cstring>
#include <iterator>
#include <type_traits>

#include "absl/algorithm/container.h"
#include "absl/base/internal/hardening.h"

namespace gtl {

namespace memmove_internal {

// Don't use definitions in this namespace directly. They are subject to change
// without notice.

template <typename C>
using element_type_t =
    std::remove_reference_t<decltype(*std::data(std::declval<C&>()))>;

// Helper function for c_memmove. It enforces the contracts for input types.
template <typename D, typename S>
void do_c_memmove(D&& dest, const S& src, size_t num_bytes) {
  static_assert(std::is_trivial_v<element_type_t<std::remove_reference_t<D>>>,
                "Destination container must have a trivial value type.");
  static_assert(std::is_trivially_copyable_v<element_type_t<S>>,
                "Source container must have a trivially copyable value type.");

  absl::base_internal::HardeningAssertLE(
      num_bytes, std::size(dest) * sizeof(*std::data(dest)));
  memmove(std::data(dest), std::data(src), num_bytes);
}

}  // namespace memmove_internal

// A container-based memmove() with explicit byte count and bounds checking.
//
// Wrapper around memmove, but in some build modes performs a bounds check
// before reading and writing. It copies the first num_bytes _bytes_ (note: not
// elements!) of src to the beginning of dest. As with memmove, the source and
// the destination byte ranges may overlap. Both containers must have a
// contiguous underlying buffer.
//
// As with memmove(), this function requires the element type of the destination
// container to be trivial.
template <
    typename D, typename S,
    typename = std::enable_if_t<absl::container_algorithm_internal::
                                    IsPermissibleDestinationRange<D>::value>>
void c_memmove(D&& dest, const S& src, size_t num_bytes) {
  absl::base_internal::HardeningAssertLE(
      num_bytes, std::size(src) * sizeof(*std::data(src)));
  memmove_internal::do_c_memmove(std::forward<D>(dest), src, num_bytes);
}

// A container-based memmove() with bounds checking.
//
// Wrapper around memmove, but in some build modes performs a bounds check
// before reading and writing. It copies all bytes of src to the beginning of
// dest. As with memmove, the source and the destination byte ranges may
// overlap. Both containers must have a contiguous underlying buffer.
//
// As with memmove(), this function requires the element type of the destination
// container to be trivial.
template <
    typename D, typename S,
    typename = std::enable_if_t<absl::container_algorithm_internal::
                                    IsPermissibleDestinationRange<D>::value>>
void c_memmove(D&& dest, const S& src) {
  size_t num_bytes = std::size(src) * sizeof(*std::data(src));
  memmove_internal::do_c_memmove(std::forward<D>(dest), src, num_bytes);
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_C_MEMMOVE_H_
