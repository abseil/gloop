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

// Copyright 2007 Google Inc.
// All rights reserved.
//

// Saito.

#include "gloop/util/random/global_id.h"

#include <cstdint>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/no_destructor.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"

namespace {

class PerThreadIDGenerator {
 public:
  constexpr PerThreadIDGenerator() : bits_(0), remaining_(0), increment_(0) {}
  inline uint64_t NewID();

  // Called by ResetPerThreadGlobalIDGenerator() to cause NewID() to reset
  // the internal state before the next id is generated.
  void ClearRemaining() { remaining_ = 0; }

 private:
  // Reset bits_, remaining_, and increment_.  This is only called by NewID().
  // post-condition: remaining_ == kNumIDsInBatch.
  // post-condition: increment_ & 1 == 1
  void Reset();

  // The last ID value; the upper bits are randomized by Reset() after every
  // kNumIDsInBatch calls to NewID().  The lower kLog2NumIDsInBatch bits are
  // incremented for each NewID() call.
  uint64_t bits_;

  // The number of IDs that can be generated before we call Reset() again.
  uint32_t remaining_;

  // The increment of lower part of bits_ for every allocation.  It's smaller
  // than kNumIDsInBatch. The value must be relatively prime to 2**64
  // to avoid decreasing entropy -- that is, any odd number will do.
  uint32_t increment_;
};

ABSL_CONST_INIT absl::Mutex g_mu(absl::kConstInit);

// The number of future IDs that are given out to a thread at once.
constexpr int kLog2NumIDsInBatch = 12;
constexpr uint64_t kNumIDsInBatch = 1 << kLog2NumIDsInBatch;
constexpr uint64_t kBatchMask = kNumIDsInBatch - 1;

ABSL_CONST_INIT thread_local PerThreadIDGenerator g_per_thread;

// Generate a 64bit random number. This could be slow, as it's called
// only when per-thread batch is exhausted.
uint64_t NewRandom() ABSL_EXCLUSIVE_LOCKS_REQUIRED(g_mu) {
  static absl::NoDestructor<absl::BitGen> bitgen;
  return absl::Uniform<uint64_t>(*bitgen);
}

uint64_t PerThreadIDGenerator::NewID() {
  if (ABSL_PREDICT_FALSE(remaining_ == 0) ||
      ABSL_PREDICT_FALSE(remaining_ >= kNumIDsInBatch) ||
      ABSL_PREDICT_FALSE((increment_ & 1) == 0)) {
    Reset();
  }

  --remaining_;
  uint64_t l = (bits_ & kBatchMask) + increment_;
  bits_ = (bits_ & ~kBatchMask) | (l & kBatchMask);
  return bits_;
}

ABSL_ATTRIBUTE_NOINLINE void PerThreadIDGenerator::Reset()
    ABSL_LOCKS_EXCLUDED(g_mu) {
  if (remaining_ != 0) {
    // Log if the class invariants are broken; this indicates memory corruption.
    LOG(DFATAL) << absl::StrFormat(
        "Global ID corrupted: increment_=%x remaining_=%d", increment_,
        remaining_);
  }

  absl::MutexLock lock(g_mu);

  bits_ = NewRandom();

  increment_ = NewRandom() & kBatchMask;
  // Force increment to be an odd number, since it must be relatively
  // prime to 2**64.
  increment_ |= 1;

  remaining_ = kNumIDsInBatch;
}

}  // namespace

uint64_t util::random::NewGlobalID() { return g_per_thread.NewID(); }

void util::random::ResetPerThreadGlobalIDGenerator() {
  g_per_thread.ClearRemaining();
}
