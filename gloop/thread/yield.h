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

#ifndef THIRD_PARTY_GLOOP_THREAD_YIELD_H_
#define THIRD_PARTY_GLOOP_THREAD_YIELD_H_

#include <thread>  // NOLINT

namespace thread::subtle {

// Yield the current executing thread to the kernel scheduler. This is a
// functional no-op, but allows for other threads to continue executing when the
// current thread is running non-locking CPU bound code.
//
// This has no visible effect but does have approximately equivalent cost to
// nanosleep(1), it is an expensive no-op. If you are using this for
// optimization in widely used code, make sure to measure the impact on many
// binaries before using this.
inline void Yield() { std::this_thread::yield(); }

}  // namespace thread::subtle

#endif  // THIRD_PARTY_GLOOP_THREAD_YIELD_H_
