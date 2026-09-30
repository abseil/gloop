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

// Utility function to call memcmp on containers. Before comparing, it checks
// that both containers hold the number of bytes to read. This is to replace
// memcmp.

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_C_MEMCMP_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_C_MEMCMP_H_

#include <cstddef>
#include <cstring>
#include <iterator>

#include "gloop/util/gtl/c_mem_internal.h"

namespace gtl {

// A container-based memcmp() with explicit byte count and bounds checking.
//
// Wrapper around memcmp, but in some build modes performs a bounds check
// before reading. It compares the first num_bytes _bytes_ (note: not
// elements!) of lhs and rhs. Both containers must have a contiguous underlying
// buffer.
//
// As with memcmp, this function requires the element types of both containers
// to be trivial to prevent non-meaningful bytewise comparisons.
template <int&... ExplicitArgumentBarrier, typename L, typename R>
int c_memcmp_n(const L& lhs, const R& rhs, size_t num_bytes) {
  c_mem_internal::CheckComparePreconditions(lhs, rhs, num_bytes);
  // As with memcmp(), std::data() must be non-null even when num_bytes == 0.
  return std::memcmp(std::data(lhs), std::data(rhs), num_bytes);
}

// Equivalent to c_memcmp_n(lhs, rhs, num_bytes).
template <int&... ExplicitArgumentBarrier, typename L, typename R>
int c_memcmp(const L& lhs, const R& rhs, size_t num_bytes) {
  return c_memcmp_n(lhs, rhs, num_bytes);
}

// A container-based memcmp() with bounds checking.
//
// Wrapper around memcmp, but in some build modes performs a bounds check
// before reading. It compares the first byte_size(rhs) bytes of lhs with all
// bytes of rhs. Both containers must have a contiguous underlying buffer.
//
// As with memcmp, this function requires the element types of both containers
// to be trivial to prevent non-meaningful bytewise comparisons.
template <int&... ExplicitArgumentBarrier, typename L, typename R>
int c_memcmp(const L& lhs, const R& rhs) {
  return c_memcmp_n(lhs, rhs, c_mem_internal::byte_size(rhs));
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_C_MEMCMP_H_
