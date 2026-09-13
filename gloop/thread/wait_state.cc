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

#include "gloop/thread/wait_state.h"

#include <atomic>

#include "gloop/base/thread-identity.h"

namespace thread {

WaitStateScope::WaitStateScope(WaitState state)
    : WaitStateScope(base::GetCurrentThreadIdentityIfPresent(), state) {}

WaitStateScope::WaitStateScope(WaitState state, bool enabled)
    : WaitStateScope(
          enabled ? base::GetCurrentThreadIdentityIfPresent() : nullptr,
          state) {}

WaitStateScope::WaitStateScope(base::ThreadIdentity* ti, WaitState state)
    : ti_(ti) {
  if (ti_ == nullptr) {
    return;
  }

  // The thread itself should be the only writer.
  old_state_ = ti_->wait_state.load(std::memory_order_relaxed);
  ti_->wait_state.store(state, std::memory_order_relaxed);
}

WaitStateScope::~WaitStateScope() {
  if (ti_ == nullptr) {
    return;
  }
  ti_->wait_state.store(old_state_, std::memory_order_relaxed);
}

}  // namespace thread
