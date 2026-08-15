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

#ifndef THIRD_PARTY_GLOOP_BASE_TIME_TIME_UNIX_NANOS_H_
#define THIRD_PARTY_GLOOP_BASE_TIME_TIME_UNIX_NANOS_H_

#include <cstdint>
#include <limits>
#include <ostream>

#include "absl/log/check.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace base {

// TimeUnixNanos holds a time value as nanoseconds since the Unix Epoch.
//
// This class is intended for use only in performance critical low-level
// libraries. The performance improvements come from (1) reducing the size
// from the 16 bytes occupied by `absl::Time` to 8 bytes. (2) avoiding repeated
// divisions and multiplications needed to convert between `absl::Time` and
// `int64_t` nanosecond values.
//
// The class uses Abseil (low level) APIs. I.e.; `TimeUnixNanos::Now()` returns
// the same values as `absl::Now` would, and the below calls are equivalent:
//   absl::Time time = absl::Now();
//   absl::Time time = TimeUnixNanos::Now().AsTime();
//
// TimeUnixNanos values must be in the range `Unix Epoch +/- int64_t::max()`
// which is roughly `1677-09-21` to `2262-04-11`. This is more than good enough
// for the typical and allow-listed purposes such as RPC, Dapper and tracing
// libraries wanting to cheaply record 'current time' values, where any
// `Now()` value outside that range is clearly erroneous.
class TimeUnixNanos {
 public:
  // Creates a default `TimeUnixNanos` instance representing the Unix Epoch.
  constexpr TimeUnixNanos() = default;

  // Default copy and assign.
  constexpr TimeUnixNanos(const TimeUnixNanos&) = default;
  constexpr TimeUnixNanos& operator=(const TimeUnixNanos&) = default;

  // TODO: spaceship
  // auto operator<=>(const TimeUnixNanos&) const = default;

  // The min() and max() constexpr methods define the minimum and maximum
  // values that can be held by a `TimeUnixNanos` instance. These values are
  // unrelated to `absl::InfinitePast` and `absl::InfiniteFuture`: the value
  // returned by `AsTime()` for these constants are simply the `absl::Time`
  // representations for the minimum and maximum possible time values.
  static constexpr TimeUnixNanos min();
  static constexpr TimeUnixNanos max();

  // Creates an instance holding the current wallclock time.
  static TimeUnixNanos Now();

  // Creates an instance from the provided absl time value. `time` must be
  // inside [absl::FromUnixNanos(INT64_MIN), absl::FromUnixNanos(INT64_MAX)].
  static TimeUnixNanos FromTime(absl::Time time);

  // Creates an instance from the `timestamp_ns` value which represents
  // nanoseconds since the Unix epoch.
  static constexpr TimeUnixNanos FromUnixNanos(int64_t timestamp_ns);

  // Returns the value of this instance as nanoseconds since the Unix Epoch.
  constexpr int64_t AsUnixNanos() const;

  // Returns the value of this instance as an absl::Time value.
  constexpr absl::Time AsTime() const;

 private:
  explicit constexpr TimeUnixNanos(int64_t timestamp_ns)
      : value_(timestamp_ns) {}

  int64_t value_ = 0;
};

inline TimeUnixNanos TimeUnixNanos::Now() {
  return TimeUnixNanos(absl::GetCurrentTimeNanos());
}

inline TimeUnixNanos TimeUnixNanos::FromTime(absl::Time time) {
  DCHECK_GE(time, absl::FromUnixNanos(std::numeric_limits<int64_t>::min()));
  DCHECK_LE(time, absl::FromUnixNanos(std::numeric_limits<int64_t>::max()));
  return TimeUnixNanos(absl::ToUnixNanos(time));
}

inline constexpr TimeUnixNanos TimeUnixNanos::FromUnixNanos(
    int64_t timestamp_ns) {
  return TimeUnixNanos(timestamp_ns);
}

inline constexpr TimeUnixNanos TimeUnixNanos::min() {
  return TimeUnixNanos(std::numeric_limits<int64_t>::min());
}

inline constexpr TimeUnixNanos TimeUnixNanos::max() {
  return TimeUnixNanos(std::numeric_limits<int64_t>::max());
}

inline constexpr int64_t TimeUnixNanos::AsUnixNanos() const { return value_; }

inline constexpr absl::Time TimeUnixNanos::AsTime() const {
  return absl::FromUnixNanos(value_);
}

// Comparison
inline bool operator==(TimeUnixNanos lhs, TimeUnixNanos rhs) {
  return lhs.AsUnixNanos() == rhs.AsUnixNanos();
}
inline bool operator!=(TimeUnixNanos lhs, TimeUnixNanos rhs) {
  return !operator==(lhs, rhs);
}
inline bool operator<(TimeUnixNanos lhs, TimeUnixNanos rhs) {
  return lhs.AsUnixNanos() < rhs.AsUnixNanos();
}
inline bool operator>(TimeUnixNanos lhs, TimeUnixNanos rhs) {
  return lhs.AsUnixNanos() > rhs.AsUnixNanos();
}
inline bool operator<=(TimeUnixNanos lhs, TimeUnixNanos rhs) {
  return !operator>(lhs, rhs);
}
inline bool operator>=(TimeUnixNanos lhs, TimeUnixNanos rhs) {
  return !operator<(lhs, rhs);
}

inline std::ostream& operator<<(std::ostream& os, TimeUnixNanos time) {
  os << time.AsTime();
  return os;
}

template <typename Sink>
void AbslStringify(Sink& sink, TimeUnixNanos time) {
  absl::AbslStringify(sink, time.AsTime());
}

}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_TIME_TIME_UNIX_NANOS_H_
