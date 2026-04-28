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

#ifndef THIRD_PARTY_GLOOP_BASE_AUXILIARY_SYNCHRONIZATION_PROFILE_H_
#define THIRD_PARTY_GLOOP_BASE_AUXILIARY_SYNCHRONIZATION_PROFILE_H_

#include <array>
#include <cstdint>
#include <memory>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/functional/function_ref.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "gloop/base/profile.h"

namespace base {

namespace internal {

// Mutex profile data
constexpr int kMutexProfileLegacyStackDepth = 16;
constexpr int kMutexProfileMaxStackDepth = 64;
constexpr int kMutexProfileHashTableSize = 128;
constexpr int kMutexProfileAssociativity = 8;
constexpr int kMutexProfileContexts = 2;

struct ContentionzData {
  // Stores total number of contended cycles and total number of observed
  // contentions for a particular call stack.
  struct StackCounts {
    int64_t cycles;
    int64_t count;
  };

  struct StackTrace {
    // Extend cycles and count to have a value without and with context.
    StackCounts counters[kMutexProfileContexts];
    int32_t depth;  // length of stack trace
    void* stack[kMutexProfileLegacyStackDepth];
  };

  StackTrace traces[kMutexProfileHashTableSize][kMutexProfileAssociativity];
  absl::Duration uptime_of_last_reset;
  StackCounts evicted[kMutexProfileContexts];
};

// base::Profile specialization to return the data from ContentionzData.
// count represents the number of contentions that were encountered.
// weight represents the amount of time spent waiting, in microseconds.
class ContentionProfile final : public base::Profile {
 public:
  ContentionProfile(int64_t period, double frequency_micros)
      : period_(period), frequency_micros_(frequency_micros) {}

  // Generates a Profile::Entry and passes it to the Handler function.
  void RecordEntry(const base::internal::ContentionzData::StackCounts* counters,
                   double period_micros, int depth, void** stack, void* arg,
                   Handler func) const;

  void Iterate(void* arg, Handler func) const override;

  void CopyFrom(const base::internal::ContentionzData* data) { data_ = *data; }

 private:
  int64_t period_;
  double frequency_micros_;

  ContentionzData data_;
  ContentionProfile(const ContentionProfile&) = delete;
  ContentionProfile& operator=(const ContentionProfile&) = delete;
};

// Upper limits on the histogram bins for mutex operation duration distribution.
// The limits below represent 1ms, 10ms, 100ms, 1s, 10s and 100s.
constexpr int kHistogramBuckets = 7;
constexpr std::array<absl::Duration, kHistogramBuckets> kMutexHistogramLimits =
    {
        absl::Milliseconds(1),    absl::Milliseconds(10),
        absl::Milliseconds(100),  absl::Seconds(1),
        absl::Seconds(10),        absl::Seconds(100),
        absl::InfiniteDuration(),
};

// Only allow recording two delta contention profiles concurrently: one for GWP
// and another for users like Superroot.
constexpr int kMaxInflightDeltaProfiles = 2;

struct DeltaContentionData {
  // Initializes the not_recorded_traces histogram with the current global
  // histogram for such traces.
  explicit DeltaContentionData(int64_t g_not_recorded_cycles);

  struct Histogram {
    std::array<absl::Duration, kHistogramBuckets> duration = {};
    std::array<int64_t, kHistogramBuckets> counts = {};
    absl::Duration total_duration = absl::ZeroDuration();
    int64_t total_count = 0;

    void Update(const Histogram& other);
    void Update(absl::Duration inc_duration);
    void Reset();
  };

  struct StackTrace {
    int32_t stack_depth = 0;
    std::array<void*, internal::kMutexProfileMaxStackDepth> stack = {};

    bool operator==(const StackTrace& other) const;
    bool operator!=(const StackTrace& other) const {
      return !operator==(other);
    }
    template <typename H>
    friend H AbslHashValue(H h, const StackTrace& c) {
      return H::combine(std::move(h),
                        absl::MakeSpan(c.stack.data(), c.stack_depth));
    }
  };

  // Other tags or labels associated with a sample.
  struct Tags {
    // The number of TPU programs running during this operation.
    // `-1` means to exclude this tag from the profile.
    int tpu_program_count = -1;

    friend bool operator==(const Tags&, const Tags&) = default;

    template <typename H>
    friend H AbslHashValue(H h, const Tags& tags) {
      return H::combine(std::move(h), tags.tpu_program_count);
    }
  };

  // Samples can be identified by the combination of the stack trace and tags.
  struct StackAndTags {
    Tags tags;
    StackTrace stack;

    friend bool operator==(const StackAndTags&, const StackAndTags&) = default;

    template <typename H>
    friend H AbslHashValue(H h, const StackAndTags& stack_and_tags) {
      return H::combine(std::move(h), stack_and_tags.tags,
                        stack_and_tags.stack);
    }
  };

  struct StackTraceAndStats {
    std::array<Histogram, internal::kMutexProfileContexts> histograms = {};
    StackAndTags stack_and_tags;
    void UpdateCounters(bool has_context, absl::Duration wait_duration);
  };

  void InsertOrUpdateTrace(uintptr_t hash, const StackAndTags& stack_and_tags,
                           bool has_context, absl::Duration wait_duration);

  void SetFinalNotRecordedStats(int64_t g_not_recorded_cycles);

  std::array<
      std::array<StackTraceAndStats, internal::kMutexProfileAssociativity>,
      internal::kMutexProfileHashTableSize>
      traces = {};
  std::array<Histogram, internal::kMutexProfileContexts> evicted_traces_hist =
      {};
  int64_t not_recorded_cycles = 0;
  bool empty = true;
};

}  // namespace internal

// In-memory data structure for recording delta contention profiles.
class DeltaContentionProfile final {
 public:
  DeltaContentionProfile();
  void set_end_time(absl::Time t) { end_time_ = t; }
  absl::Duration duration() const { return end_time_ - start_time_; }

  // Updates the profiling data for lock acquisition.
  //
  // This function is called with deltacontentionz_lock held.  To reduce the
  // computation done with the lock held, the hash of the stack trace is
  // computed before and then passed as an argument.
  void UpdateLockData(
      uintptr_t hash,
      const internal::DeltaContentionData::StackAndTags& stack_and_tags,
      bool has_context, int64_t wait_cycles) {
    absl::Duration wait_duration = wait_cycles * clock_cycle_duration_;
    locking_data_.InsertOrUpdateTrace(hash, stack_and_tags, has_context,
                                      wait_duration);
  }

  // Updates the profiling data with lock hold duration.
  //
  // This function is called with deltacontentionz_lock held.  To reduce the
  // computation done with the lock held, the hash of the stack trace is
  // computed before and then passed as an argument.
  void UpdateUnlockData(
      uintptr_t hash,
      const internal::DeltaContentionData::StackAndTags& stack_and_tags,
      bool has_context, int64_t total_wait_cycles) {
    absl::Duration wait_duration = total_wait_cycles * clock_cycle_duration_;
    unlocking_data_.InsertOrUpdateTrace(hash, stack_and_tags, has_context,
                                        wait_duration);
  }

  // Intermediate data structure for converting in-memory profiles to
  // perftools::profiles::Profile.
  struct ValuesAndLabels {
    void* const* stack = nullptr;
    int depth = 0;
    internal::DeltaContentionData::Tags tags;
    absl::Duration histogram_bucket_min = absl::ZeroDuration();
    absl::Duration histogram_bucket_max = absl::ZeroDuration();
    int64_t locking_contentions = 0;
    absl::Duration locking_delay = absl::ZeroDuration();
    int64_t unlocking_contentions = 0;
    absl::Duration unlocking_delay = absl::ZeroDuration();
    bool has_context = false;
  };

  // Calls (*func)(sample) for every sample stored in the profile.  Used for
  // converting the in-memory profiles to perftools::profiles::Profile.
  void Iterate(absl::FunctionRef<void(const ValuesAndLabels&)> func) const;

  // Invokes the corresponding function on locking_data_.
  void SetFinalNotRecordedStats();

  // Functions for making testing the implementation easier.
  absl::Duration TotalLockingDelayForTesting() const;
  int64_t LockingNotRecordedCyclesForTesting() const {
    return locking_data_.not_recorded_cycles;
  }
  int64_t UnlockingNotRecordedCyclesForTesting() const {
    return unlocking_data_.not_recorded_cycles;
  }

  bool IsEmpty() const { return locking_data_.empty && unlocking_data_.empty; }

 private:
  // Generates ValuesAndLabels objects for the locking stack and passes them to
  // the Handler function.
  void RecordLockEntry(
      const std::array<absl::Duration, internal::kHistogramBuckets>&
          histogram_bucket_limits,
      const internal::DeltaContentionData::StackAndTags& stack_and_tags,
      const internal::DeltaContentionData::Histogram& locking_histogram,
      int context, absl::FunctionRef<void(const ValuesAndLabels&)> f) const;

  // Invokes RecordLockEntry on each histogram.
  void RecordLockEntries(
      const std::array<absl::Duration, internal::kHistogramBuckets>&
          histogram_bucket_limits,
      const internal::DeltaContentionData::StackAndTags& stack_and_tags,
      const std::array<internal::DeltaContentionData::Histogram,
                       internal::kMutexProfileContexts>& counters,
      absl::FunctionRef<void(const ValuesAndLabels&)> f) const;

  // Generates a Profile::Entry for the unlocking stack and passes it to the
  // Handler function.
  void RecordUnlockEntry(
      const std::array<absl::Duration, internal::kHistogramBuckets>&
          histogram_bucket_limits,
      const internal::DeltaContentionData::StackAndTags& stack_and_tags,
      const internal::DeltaContentionData::Histogram& unlocking_histogram,
      int context, absl::FunctionRef<void(const ValuesAndLabels&)> func) const;

  // Invokes RecordUnlockEntry on each histogram.
  void RecordUnlockEntries(
      const std::array<absl::Duration, internal::kHistogramBuckets>&
          histogram_bucket_limits,
      const internal::DeltaContentionData::StackAndTags& stack_and_tags,
      const std::array<internal::DeltaContentionData::Histogram,
                       internal::kMutexProfileContexts>& counters,
      absl::FunctionRef<void(const ValuesAndLabels&)> f) const;

  const int64_t sampling_period_ = 0;
  const absl::Duration clock_cycle_duration_ = absl::ZeroDuration();
  const absl::Time start_time_ = absl::InfiniteFuture();
  absl::Time end_time_ = absl::InfinitePast();

  // Records the duration that went by while 'locking' the mutex.
  internal::DeltaContentionData locking_data_;
  // Records the wait duration that waited while the mutex was 'locked'.
  // Observations are recorded when the thread unlocks the mutex.
  internal::DeltaContentionData unlocking_data_;
  DeltaContentionProfile(const DeltaContentionProfile&) = delete;
  DeltaContentionProfile& operator=(const DeltaContentionProfile&) = delete;
  DeltaContentionProfile& operator=(DeltaContentionProfile&&) = delete;
};

// Move-only token for starting and stopping delta contention collection.
class DeltaContentionToken final {
 public:
  DeltaContentionToken() = default;
  ~DeltaContentionToken();
  DeltaContentionToken(DeltaContentionToken&&) = default;
  DeltaContentionToken(const DeltaContentionToken&) = delete;
  DeltaContentionToken& operator=(DeltaContentionToken&&) = default;
  DeltaContentionToken& operator=(const DeltaContentionToken&) = delete;

  bool IsValid() const { return profile_ != nullptr; }

  // Allocates memory for profile_ and starts collecting data in it.  Returns a
  // DeltaContentionToken that can be used to stop profiling.  An invalid token
  // is returned in case the profiler is not able to start profiling.
  //
  // Profiling can be stopped by calling token.StopProfiling().
  static DeltaContentionToken StartProfiling();

  // Stops collecting data for this token and returns the profile with the
  // collected data.  The returned profile can be converted to a Profile proto
  // using perftools::profiles::MakeDeltaContentionProfile.
  absl_nullable std::unique_ptr<DeltaContentionProfile> StopProfiling() &&;

 private:
  explicit DeltaContentionToken(DeltaContentionProfile* profile)
      : profile_(profile) {}

  void StopProfilingHelper();

  std::unique_ptr<DeltaContentionProfile> profile_;
};

}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_AUXILIARY_SYNCHRONIZATION_PROFILE_H_
