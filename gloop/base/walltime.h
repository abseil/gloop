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

//
// Prefer to use the functions in base/time.h (<link>) in new code
// and APIs since they provide better type safety and more precision than
// WallTime.

#ifndef THIRD_PARTY_GLOOP_BASE_WALLTIME_H_
#define THIRD_PARTY_GLOOP_BASE_WALLTIME_H_

#include <stdint.h>

#include "absl/time/time.h"

// WallTime is the number of seconds since the Unix epoch (1970-01-01 00:00:00
// UTC).  We represent this as a double so we can keep track of fractional
// seconds.
typedef double WallTime;

#ifndef SWIG
namespace base {

// Functions for converting a WallTime to/from the recommended absl::Time type.
// Use these only when necessary. In general, prefer using absl::Time over
// WallTime.
absl::Time FromWallTime(WallTime walltime);
WallTime ToWallTime(absl::Time t);

}  // namespace base
#endif

// Return the current effective cycles-per-second.  Thread-safe.
extern int64_t WallTime_CPS();

// Return the seconds-per-cycle value that is currently being used to
// convert from cycles to walltime.  This is the inverse of
// WallTime_CPS().  Thread-safe.
extern double WallTime_SPC();

// ------------------------------------------------------------------------
// Conversion support
// ------------------------------------------------------------------------

// This class provides a mechanism for converting cycle timer values
// to WallTimes in a way that avoids magnifying small inaccuracies in
// CycleClock::Frequency(), especially when converting very old cycle times.
// An instance of this class is initialized with a pair of
// walltime/cycletime measurements that were taken at approximately
// the same time.  Thread-compatible.
class WallTimeConverter {
 public:
  // Default: uninitialized
  WallTimeConverter() {}

  // Construct from known WallTime and CycleClock values.
  WallTimeConverter(double walltime, int64_t cycletime)
      : walltime_(walltime), cycletime_(cycletime) {}

  // Returns a WallTimeConverter initialized to now.
  static WallTimeConverter Now();

  // Return the base walltime used for conversion by this.
  WallTime base_walltime() const { return walltime_; }

  // Return the base cycletime used for conversion by this.
  int64_t base_cycletime() const { return cycletime_; }

  // Converts a CycleClock::Now() value to a WallTime, maximizing
  // accuracy for nearby cycle times.
  WallTime CycleToWallTime(int64_t ts) const {
    return walltime_ + static_cast<WallTime>(ts - cycletime_) * WallTime_SPC();
  }

 private:
  WallTime walltime_;
  int64_t cycletime_;
};

#endif  // THIRD_PARTY_GLOOP_BASE_WALLTIME_H_
