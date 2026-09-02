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

#include "gloop/util/refcount/blocking_refcount.h"

#include <atomic>
#include <cstdint>

#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace util {

bool BlockingRefcount::WaitForZeroWithDeadline(absl::Time deadline) const {
  absl::MutexLock l(mu_);
  auto previous = count_.load(std::memory_order_acquire);
  if (previous == 0) return true;  // Optimize for empty wait.
  if ((previous & kWaiting) == 0) {
    // No other waiters.
    // Flag the wait; then wait if the count was greater than zero.
    previous = count_.fetch_or(kWaiting, std::memory_order_acquire);
  }
  if (previous) {
    return mu_.AwaitWithDeadline(
        absl::Condition(this, &BlockingRefcount::WaitCondition), deadline);
  } else {
    count_.fetch_and(~kWaiting, std::memory_order_relaxed);
  }
  return true;
}

bool BlockingRefcount::WaitForZeroWithTimeout(absl::Duration timeout) const {
  return WaitForZeroWithDeadline(absl::Now() + timeout);
}

void BlockingRefcount::WaitForZero() const {
  WaitForZeroWithDeadline(absl::InfiniteFuture());
}

constexpr uint64_t BlockingRefcount::kWaiting;

}  // namespace util
