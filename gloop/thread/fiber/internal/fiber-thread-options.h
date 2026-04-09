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

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_INTERNAL_FIBER_THREAD_OPTIONS_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_INTERNAL_FIBER_THREAD_OPTIONS_H_

namespace thread::internal {

// Limit the stack size that a user can request for a thread. This corresponds
// to 2^30 bytes or lower for mobile platforms (these platforms do not honor
// requests about 2^16 bytes).
#if defined(__Fuchsia__) || defined(__APPLE__) || defined(__ANDROID__)
constexpr inline int kMaxStackSizeLog2 = 16;
#else
constexpr inline int kMaxStackSizeLog2 = 30;
#endif

#ifdef NDEBUG
constexpr inline int kMinStackSizeLog2 = 12;
#else
// dbg binaries empirically cannot be run with threads of stack size 2^12
// (4096), so we raise the minimum if we compile with -c dbg. This will
// silently round up.
constexpr inline int kMinStackSizeLog2 = 13;
#endif

}  // namespace thread::internal

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_INTERNAL_FIBER_THREAD_OPTIONS_H_
