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

#ifndef THIRD_PARTY_GLOOP_CONCURRENT_BARRIER_BARRIER_H_
#define THIRD_PARTY_GLOOP_CONCURRENT_BARRIER_BARRIER_H_

#include "absl/log/check.h"
#include "gloop/concurrent/barrier/barrier_internal.h"  // IWYU pragma: export

namespace concurrent {

// Returns a functor that executes "done" after it (or any copy) has been
// invoked "n" times.  The result type is unspecified, but can be converted to
// std::function<void()>.  If "n" is 0, "done" is executed immediately.
// We require "n" to be greater or equal to 0.
// The returned functor allows fewer than "n" calls.
// E.g.
// {
//   auto f = NewBarrier(10, [] { LOG(FATAL); });
//   f();
//   f();
// }
// If the resultant functor is invoked fewer than "n" times during its lifetime,
// the wrapped functor is never called.
template <typename Functor>
internal::FixedBarrier<Functor> NewBarrier(int n, Functor&& done) {  // NOLINT
  DCHECK_GE(n, 0);
  return internal::FixedBarrier<Functor>(n, std::forward<Functor>(done));
}

}  // namespace concurrent

#endif  // THIRD_PARTY_GLOOP_CONCURRENT_BARRIER_BARRIER_H_
