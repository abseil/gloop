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

#ifndef THIRD_PARTY_GLOOP_THREAD_WAIT_STATE_H_
#define THIRD_PARTY_GLOOP_THREAD_WAIT_STATE_H_

#include "gloop/base/thread-identity.h"

namespace thread {

// RAII object used by threads to mark their wait state.
//
// A `kWaitingForWork` wait state indicates that the worker is not doing
// anything interesting, and is just waiting for work to be assigned to the
// worker. Its primary use is to detect uninteresting threads and suppress their
// printing, e.g., when a process crashes.
//
// `kWaitingForWork` should only be set if the thread stack contains nothing
// interesting. E.g., a ThreadPool thread that is just waiting for work is a
// good candidate for `kWaitingForWork`. But a thread that is in the middle of
// some user code and is waiting for an event (e.g., waiting for an RPC to
// finish), should not be marked as `kWaitingForWork`.
//
// Example usage:
//  void MyThreadPool::Worker() {
//    absl::MutexLock l(&mu_);
//    while (...) {
//      {
//        // Change the worker wait state here, instead of inside Wait(), since
//        // Wait() may be called from other contexts where `kWaitingForWork` is
//        // not appropriate.
//        WaitStateScope scope(WaitStateScope::kWaitingForWork);
//        Wait();
//      }
//      ... handle available work ...
//    }
//  }
class WaitStateScope {
 public:
  using WaitState = base::ThreadIdentity::WaitState;

  // TODO: b/357097463 - Use `using enum WaitState` once C++20 is allowed here.
  inline static constexpr WaitState kActive = WaitState::kActive;
  inline static constexpr WaitState kWaitingForWork =
      WaitState::kWaitingForWork;

  // Set the current thread wait state to state for the duration of this scope.
  explicit WaitStateScope(WaitState state);

  // If enabled, set the current thread wait state to state for the duration of
  // this scope.
  WaitStateScope(WaitState state, bool enabled);

  ~WaitStateScope();

 private:
  WaitStateScope(base::ThreadIdentity* ti, WaitState state);

  // Pointer to this thread's metadata. If nullptr, this scope is not enabled.
  base::ThreadIdentity* ti_;
  // Only set if ti_ != nullptr.
  WaitState old_state_;
};

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_WAIT_STATE_H_
