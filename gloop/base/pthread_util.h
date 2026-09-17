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

#ifndef THIRD_PARTY_GLOOP_BASE_PTHREAD_UTIL_H_
#define THIRD_PARTY_GLOOP_BASE_PTHREAD_UTIL_H_

#include <pthread.h>

#include <cstdint>
#include <type_traits>

namespace base {

// Returns an integral identifier corresponding to `thread`.
inline uintptr_t GetPthreadNumericId(pthread_t thread) {
#if defined(__LLVM_LIBC__) && !defined(__Fuchsia__)
  pthread_id_np_t id = 0;
  pthread_getunique_np(&thread, &id);
  return id;
#else
  // A generic lambda is used here to create a dependent template context for
  // `if constexpr`. In a non-template function, both branches are semantically
  // analyzed, which breaks compilation on platforms where `pthread_t` cannot be
  // cast via both `reinterpret_cast` and `static_cast`.
  return [](auto t) -> uintptr_t {
    if constexpr (std::is_pointer_v<decltype(t)>) {
      return reinterpret_cast<uintptr_t>(t);
    } else {
      return static_cast<uintptr_t>(t);
    }
  }(thread);
#endif
}

}  // namespace base

// Use these macros after a % in a printf format string (or absl::StrFormat)
// to format the numeric thread identifier returned by
// base::GetPthreadNumericId(), like this:
//   pthread_t tid = pthread_self();
//   printf("Thread %" GPRIxPTHREAD "\n", base::GetPthreadNumericId(tid));
#define GPRIuPTHREAD "lu"
#define GPRIxPTHREAD "lx"

#endif  // THIRD_PARTY_GLOOP_BASE_PTHREAD_UTIL_H_
