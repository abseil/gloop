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

#ifndef THIRD_PARTY_GLOOP_BASE_ATOMIC_STATS_COUNTER_H_
#define THIRD_PARTY_GLOOP_BASE_ATOMIC_STATS_COUNTER_H_
// Atomic update of a statistics counter.
// Example:
//   StatsCounter counter;
//   ...
//   counter.Add(1);    // this is thread-safe
//   ...
//   count_value = counter.value();

// If you are updating more than two statistics counter,
// it will be faster to use simple counters protected by a Mutex.
// If an exact count is unnecessary and performance is critical,
// consider LossyAdd(), which is similar to Add() but compromises
// correctness for speed.

// For generating sequence numbers, see atomic_sequence_num.h.
// For reference counting, see atomic_refcount.h.

#include <stdint.h>

#include <atomic>

namespace base {

class StatsCounter {
 public:
  constexpr StatsCounter() : value_(0) {}

  // This type is neither copyable nor movable.
  StatsCounter(const StatsCounter&) = delete;
  StatsCounter& operator=(const StatsCounter&) = delete;

  ~StatsCounter() = default;

#if defined(__myriad2__)
  using Value = int32_t;  // myriad lacks 64-bit atomics
#else
  using Value = int64_t;
#endif

  // Add "increment" to this statistics counter.
  // "increment" may take any value, including negative ones.
  // Counts are not lost in the face of concurrent uses of Add().
  // Counts added by this call may be lost in the face of concurrent calls
  // by other calls, such as Clear() or LossyAdd().
  // This call is suitable for maintaining statistics.   It is not suitable
  // for other purposes; in particular, it should not be used for
  // data synchronization, generating sequence numbers, or reference counting.
  void Add(Value increment) {
    // As always, clients may not assume properties implied by the
    // implementation, which may change.
    this->value_.fetch_add(increment, std::memory_order_relaxed);
  }

  // Clear the counter to zero.  Equivalent to atomically executing
  // this->Add(-this->value()).
  void Clear() { this->value_.store(0, std::memory_order_relaxed); }

  // Return the current value of the counter.
  Value value() const { return this->value_.load(std::memory_order_relaxed); }

  // Add "increment" to this lossy statistics counter.  Counts (including those
  // added by other calls) _may be lost_ if this call is used concurrently with
  // other calls to LossyAdd() or Add().  This call is suitable for maintaining
  // statistics where performance is more important than not losing counts.  It
  // is not suitable for other purposes; in particular, it should not be used
  // for data synchronization, generating sequence numbers, or reference
  // counting.
  void LossyAdd(Value increment) {
    this->value_.store(this->value_.load(std::memory_order_relaxed) + increment,
                       std::memory_order_relaxed);
  }

 private:
  std::atomic<Value> value_;
};

}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_ATOMIC_STATS_COUNTER_H_
