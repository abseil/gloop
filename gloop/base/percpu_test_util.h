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

#ifndef THIRD_PARTY_GLOOP_BASE_PERCPU_TEST_UTIL_H_
#define THIRD_PARTY_GLOOP_BASE_PERCPU_TEST_UTIL_H_

#include "absl/base/internal/raw_logging.h"
#include "gloop/base/percpu.h"

namespace base {
namespace subtle {
namespace percpu {

// Unregisters the current thread from rseq if it is currently registered.
//
// Do not use this unless you know what you're doing! It is typically useful
// only when testing the rseq implementation and working around rseq bugs.
#if PERCPU_USE_RSEQ
void UnregisterThread();
#else
inline void UnregisterThread() {}
#endif

class ScopedUnregisterRseq {
 public:
  ScopedUnregisterRseq() : was_registered_(IsFastNoInit()) {
    UnregisterThread();
  }

  ScopedUnregisterRseq(const ScopedUnregisterRseq&) = delete;
  ScopedUnregisterRseq& operator=(const ScopedUnregisterRseq&) = delete;

  ~ScopedUnregisterRseq() {
    if (was_registered_) {
      ABSL_RAW_CHECK(IsFast(), "unable to reinitialize rseq");
    }
  }

 private:
  const bool was_registered_;
};

}  // namespace percpu
}  // namespace subtle
}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_PERCPU_TEST_UTIL_H_
