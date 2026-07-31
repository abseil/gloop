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

#include "gloop/base/walltime.h"

#include <stdint.h>

#include <limits>

#include "absl/base/internal/cycleclock.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#ifndef SWIG
namespace base {

absl::Time FromWallTime(WallTime walltime) {
  return absl::time_internal::FromUnixDuration(absl::Seconds(walltime));
}

WallTime ToWallTime(absl::Time t) {
  using absl::time_internal::GetRepHi;
  using absl::time_internal::GetRepLo;
  using absl::time_internal::IsInfiniteDuration;
  using absl::time_internal::ToUnixDuration;
  if (IsInfiniteDuration(ToUnixDuration(t))) {
    return (GetRepHi(ToUnixDuration(t)) >= 0)
               ? std::numeric_limits<WallTime>::infinity()
               : -std::numeric_limits<WallTime>::infinity();
  }
  return GetRepHi(ToUnixDuration(t)) + GetRepLo(ToUnixDuration(t)) * 2.5e-10;
}

}  // namespace base
#endif  // SWIG

using absl::base_internal::CycleClock;  // NOLINT

int64_t WallTime_CPS() { return CycleClock::Frequency(); }

double WallTime_SPC() { return 1.0 / CycleClock::Frequency(); }

WallTimeConverter WallTimeConverter::Now() {
  return WallTimeConverter(base::ToWallTime(absl::Now()), CycleClock::Now());
}
