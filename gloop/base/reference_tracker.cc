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

#include "gloop/base/reference_tracker.h"

#include <utility>
#include <vector>

#include "absl/base/internal/raw_logging.h"
#include "absl/debugging/stacktrace.h"
#include "gloop/base/spinlock.h"

namespace base {

void ReferenceTracker::GetReferenceTraces(
    std::vector<StackTrace>* traces) const {
  SpinLockHolder l(mu_);
  for (auto& p : traces_) {
    traces->push_back(p.second);
  }
}

void ReferenceTracker::Ref(const void* owner) {
  const int kMaxStackFrames = 30;
  void* pcs[kMaxStackFrames];
  int num_frames = absl::GetStackTrace(pcs, kMaxStackFrames, 1);

  SpinLockHolder l(mu_);
  if (!traces_
           .insert(std::make_pair(
               owner, std::vector<const void*>(pcs, pcs + num_frames)))
           .second) {
    ABSL_RAW_LOG(FATAL, "Already have a ref from %p", owner);
  }
}

void ReferenceTracker::Unref(const void* owner) {
  SpinLockHolder l(mu_);
  if (!traces_.erase(owner)) {
    ABSL_RAW_LOG(FATAL, "No ref from %p", owner);
  }
}

}  // namespace base
