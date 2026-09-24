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

// Utility function to call memcpy on containers. Before copying, it checks
// that the source container holds the number of bytes to read and that the
// destination container has enough space to hold the number of bytes to write.
// This is to replace memcpy.

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_C_MEMCPY_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_C_MEMCPY_H_

#include <cstddef>
#include <cstring>
#include <iterator>
#include <type_traits>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/base/macros.h"
#include "gloop/util/gtl/c_mem_internal.h"

namespace gtl {

// A container-based memcpy() with explicit byte count and bounds checking.
//
// Wrapper around memcpy, but in some build modes performs a bounds check
// before reading and writing. It copies the first num_bytes _bytes_ (note: not
// elements!) of src to the beginning of dest. As with memcpy, the source and
// the destination byte ranges must not overlap. Both containers must have a
// contiguous underlying buffer.
//
// As with memcpy(), this function requires the element type of the destination
// container to be trivial.
template <typename D, typename S,
          std::enable_if_t<absl::container_algorithm_internal::
                                   IsPermissibleDestinationRange<D>::value &&
                               !c_mem_internal::kCanUseCCopyN<D, S>,
                           int> = 0>
void c_memcpy(D&& dest, const S& src, size_t num_bytes) {
  c_mem_internal::CheckCopyPreconditions(dest, src, num_bytes);
  // As with memcpy(), std::data() must be non-null even when num_bytes == 0.
  std::memcpy(std::data(dest), std::data(src), num_bytes);
}

// Specialized for containers with identical single-byte trivial element types,
// where absl::c_copy_n() is a drop-in replacement. Deprecated so that such
// callers migrate to absl::c_copy_n().
template <typename D, typename S,
          std::enable_if_t<c_mem_internal::kCanUseCCopyN<D, S>, int> = 0>
[[deprecated("Use absl::c_copy_n(src, num_bytes, dest) instead")]]  //
ABSL_REFACTOR_INLINE void c_memcpy(D&& dest, const S& src, size_t num_bytes) {
  absl::c_copy_n(src, num_bytes, std::forward<D>(dest));
}

// A container-based memcpy() with bounds checking.
//
// Wrapper around memcpy, but in some build modes performs a bounds check
// before reading and writing. It copies all bytes of src to the beginning of
// dest. As with memcpy, the source and the destination byte ranges must not
// overlap. Both containers must have a contiguous underlying buffer.
//
// As with memcpy(), this function requires the element type of the destination
// container to be trivial.
template <typename D, typename S,
          std::enable_if_t<absl::container_algorithm_internal::
                                   IsPermissibleDestinationRange<D>::value &&
                               !c_mem_internal::kCanUseCCopy<D, S>,
                           int> = 0>
void c_memcpy(D&& dest, const S& src) {
  const size_t num_bytes = c_mem_internal::byte_size(src);
  c_mem_internal::CheckCopyPreconditions(dest, src, num_bytes);
  // As with memcpy(), std::data() must be non-null even when num_bytes == 0.
  std::memcpy(std::data(dest), std::data(src), num_bytes);
}

// Specialized for containers with identical trivial element types, where
// absl::c_copy() is a drop-in replacement. Deprecated so that such callers
// migrate to absl::c_copy().
template <typename D, typename S,
          std::enable_if_t<c_mem_internal::kCanUseCCopy<D, S>, int> = 0>
[[deprecated("Use absl::c_copy(src, dest) instead")]]  //
ABSL_REFACTOR_INLINE void c_memcpy(D&& dest, const S& src) {
  absl::c_copy(src, std::forward<D>(dest));
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_C_MEMCPY_H_
