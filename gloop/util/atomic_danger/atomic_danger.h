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

// The routines exported by this module are subtle.  If you use them, even if
// you get the code right, it will depend on careful reasoning about atomicity
// and memory ordering. It will be less readable and harder to maintain.  If
// you plan to use these routines, you should have a good reason, such as solid
// evidence that performance would otherwise suffer.
//
// If you do not know what you are doing, avoid these routines, and use an
// absl::Mutex.

#ifndef THIRD_PARTY_GLOOP_UTIL_ATOMIC_DANGER_ATOMIC_DANGER_H_
#define THIRD_PARTY_GLOOP_UTIL_ATOMIC_DANGER_ATOMIC_DANGER_H_

#include <atomic>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/meta/type_traits.h"

namespace atomic_danger {

// Atomically compares `*ptr` to `expected`. Replaces `*ptr` with `desired` if
// `*ptr` used to be `expected`. Always returns the value of `*ptr` from before
// this call.
// `success_order` is the memory synchronization ordering for the
// read-modify-write operation if the comparison between `*ptr` and `desired`
// succeeds. If the comparison fails, the load of the existing value is relaxed.
template <typename T, class Ex, class Des,
          typename = absl::enable_if_t<std::is_integral<T>::value &&
                                       std::is_convertible<Ex, T>::value &&
                                       std::is_convertible<Des, T>::value>>
ABSL_MUST_USE_RESULT T CompareAndSwap(std::atomic<T>* ptr, Ex&& expected,
                                      Des&& desired,
                                      std::memory_order success_order) {
  T expected_t = std::forward<Ex>(expected);
  ptr->compare_exchange_strong(expected_t, std::forward<Des>(desired),
                               success_order);
  return expected_t;  // `compare_exchange_strong` may have modified
                      // `expected_t`.
}

// Decrements a reference count by `decrement` and returns whether the result is
// zero, ensuring that state written before the reference count became zero
// will be visible to a thread that has just made the count zero.
template <typename T, class U,
          typename = absl::enable_if_t<std::is_integral<T>::value &&
                                       std::is_convertible<U, T>::value>>
ABSL_MUST_USE_RESULT bool IsZeroAfterDecrement(std::atomic<T>* ptr,
                                               U&& decrement) {
  const T dec = std::forward<U>(decrement);
  return ptr->fetch_sub(dec, std::memory_order_seq_cst) - dec == 0;
}

// Casts the address of a std::atomic<IntType> to the address of an IntType.
//
// This is almost certainly not the function you are looking for! It is
// undefined behavior, as the object under a std::atomic<int> isn't
// fundamentally an int. This function is intended for passing the address of an
// atomic integer to syscalls or for assembly interpretation.
//
// Callers should be migrated if C++ standardizes a better way to do this:
// * http://wg21.link/n4013 (Atomic operations on non-atomic data)
// * http://wg21.link/p0019 (Atomic Ref, merged into C++20)
// * http://wg21.link/p1478 (Byte-wise atomic memcpy)
template <typename IntType>
IntType* CastToIntegral(std::atomic<IntType>* atomic_for_syscall) {
  static_assert(std::is_integral<IntType>::value,
                "CastToIntegral must be instantiated with an integral type.");
  static_assert(std::atomic<IntType>::is_always_lock_free,
                "CastToIntegral must be instantiated with a lock-free type.");
  return reinterpret_cast<IntType*>(atomic_for_syscall);
}
}  // namespace atomic_danger

#endif  // THIRD_PARTY_GLOOP_UTIL_ATOMIC_DANGER_ATOMIC_DANGER_H_
