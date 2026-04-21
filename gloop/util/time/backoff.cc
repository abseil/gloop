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

#include "gloop/util/time/backoff.h"

#include <algorithm>
#include <cstdint>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/util/random/acmrandom.h"

ABSL_FLAG(int32_t, util_time_backoff_seed, 0,
          "The seed used to initialize the PRNG used by "
          "util_time::ComputeBackoff. If not set, "
          "ACMRandom::HostnamePidTimeSeed() will be used.");

namespace util_time {
namespace {

// For randomizing backoff delays
ABSL_CONST_INIT absl::Mutex rnd_lock(absl::kConstInit);
// Default exponential backoff base.
constexpr double kDefaultBackoffBase = 1.3;

}  // namespace

absl::Duration ComputeBackoff(absl::Duration min_delay,
                              absl::Duration max_delay, double backoff_base,
                              int previous_retries) {
  DCHECK_GE(min_delay, absl::Duration());
  DCHECK_GE(max_delay, min_delay);
  DCHECK_GT(backoff_base, 1.0);
  DCHECK_GE(previous_retries, 0);

  // Make sure min_delay is at least one nanosecond (the resolution advertised
  // by absl::Duration) so that we don't always yield a backoff of zero. Correct
  // other args similarly.
  if (min_delay < absl::Nanoseconds(1)) {
    LOG_FIRST_N(WARNING, 500) << "ComputeBackoff increasing min_delay from "
                              << min_delay << " to 1ns.";
    min_delay = absl::Nanoseconds(1);
  }

  if (max_delay < min_delay) {
    LOG_FIRST_N(WARNING, 500)
        << "ComputeBackoff increasing max_delay of " << max_delay
        << " to match min_delay of " << min_delay;
    max_delay = min_delay;
  }

  if (backoff_base <= 1.0) {
    LOG_FIRST_N(WARNING, 500) << "ComputeBackoff increasing backoff_base from "
                              << backoff_base << " to " << kDefaultBackoffBase;
    backoff_base = kDefaultBackoffBase;
  }

  if (previous_retries < 0) {
    LOG_FIRST_N(WARNING, 500)
        << "ComputeBackoff increasing previous_retries from "
        << previous_retries << " to zero.";
    previous_retries = 0;
  }

  // NOTE: Any CL that changes the algorithm below, must include similar changes
  // to backoff_test_util.cc.

  // Without taking applied boundaries into account, the backoff equation is
  // effectively:
  //
  //   0.4 * min_delay + (rand in [0.6,1.0]) * min_delay * backoff_base^retries
  //
  // The first term (0.4 * min_delay) is a constant (doesn't vary with retries
  // or random numbers) term that is just large enough to guarantee that the sum
  // of the two terms has a minimum value of min_delay.
  //
  // Pseudocode:
  //
  //   first_term = 0.4 * min_delay
  //   second_term = min_delay * (backoff_base ^ retries)
  //   cap second_term at max_delay - first_term
  //   multiply second_term by a random amount in [0.6, 1.0]
  //   return first_term + second_term, which is in requested bounds
  //
  // The returned backoff delay is randomized to prevent a cluster of
  // requests from synchronizing retries.
  // The random multiplier only reduces the second term, so that the max is
  // always upheld. Note that for all values of 'retries', including zero and
  // large values that cap the second term, the random multiplier
  // can cause up to a 40% variation in the second term.

  static const double kBackoffRandMult = 0.4;
  const absl::Duration first_term = kBackoffRandMult * min_delay;
  absl::Duration uncapped_second_term = min_delay;
  while (previous_retries > 0 &&
         uncapped_second_term < max_delay - first_term) {
    previous_retries--;
    uncapped_second_term *= backoff_base;
  }
  // Note that first_term + uncapped_second_term can exceed max_delay here
  // because of the final multiply by kBackoffBase.  We fix that problem with
  // the min() below.
  absl::Duration second_term =
      std::min(uncapped_second_term, max_delay - first_term);

  static ACMRandom* rnd = []() {
    int32_t seed = absl::GetFlag(FLAGS_util_time_backoff_seed);
    if (seed == 0) {
      seed = ACMRandom::HostnamePidTimeSeed();
    }

    LOG(INFO) << "Using --util_time_backoff_seed=" << seed;
    return new ACMRandom(seed);
  }();

  {
    absl::MutexLock l(rnd_lock);
    second_term *= rnd->UniformDouble(1.0 - kBackoffRandMult, 1.0);
  }

  // Ensure that floating point error didn't cause us to violate the contract.
  return std::clamp(first_term + second_term, min_delay, max_delay);
}

absl::Duration ComputeBackoff(absl::Duration min_delay,
                              absl::Duration max_delay, int previous_retries) {
  return ComputeBackoff(min_delay, max_delay, kDefaultBackoffBase,
                        previous_retries);
}

}  // namespace util_time
