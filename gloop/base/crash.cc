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

#include "gloop/base/crash.h"

#include "gloop/base/config.h"

#if BASE_HAVE_CRASHREASON

#include <atomic>

#include "absl/base/attributes.h"

namespace base {

// We use an atomic to prevent problems with calling CrashReason
// from inside the Mutex implementation (potentially through RAW_CHECK).
ABSL_CONST_INIT static std::atomic<const CrashReason*> reason{nullptr};

void SetCrashReason(const CrashReason* r) {
  const CrashReason* compare_to = nullptr;
  reason.compare_exchange_strong(compare_to, r, std::memory_order_release,
                                 std::memory_order_relaxed);
}

const CrashReason* GetCrashReason() {
  return reason.load(std::memory_order_acquire);
}

}  // namespace base

#endif  // BASE_HAVE_CRASHREASON
