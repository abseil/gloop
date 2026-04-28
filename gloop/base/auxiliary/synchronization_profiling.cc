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

#include "gloop/base/auxiliary/synchronization_profiling.h"

#include <errno.h>

#include <type_traits>
#include <utility>

#include "absl/base/nullability.h"

#ifdef _WIN32
#include <windows.h>
#else  // !_WIN32
#include <pthread.h>
#endif  // !_WIN32

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/internal/cycleclock.h"
#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/debugging/stacktrace.h"
#include "absl/flags/flag.h"
#include "absl/functional/function_ref.h"
#include "absl/hash/hash.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "gloop/base/auxiliary/synchronization_profile.h"
#include "gloop/base/auxiliary/synchronization_tags.h"
#include "gloop/base/examine_stack.h"
#include "gloop/base/profile.h"
#include "gloop/base/scheduling/scheduling_mode.h"
#include "gloop/base/spinlock.h"

namespace base {

void SubmitMutexProfileData(int64_t wait_cycles) {}

void SubmitMutexLockProfileData(const void* mutex, int64_t wait_cycles) {}

void SubmitMutexUnlockProfileData(const void* mutex, int64_t total_wait_cycles,
                                  int64_t max_wait_cycles,
                                  int64_t num_waiters) {}

void SubmitSpinLockProfileData(const void* contendedlock, int64_t wait_cycles) {
}

void SampledTraceCV(const char* msg, const void* obj) {}

void SampledTraceMutex(const char* msg, const void* obj, int64_t wait_cycles) {}

namespace internal {

void SetProfilingDirectories(const std::vector<std::string>& dirs) {}

}  // namespace internal
}  // namespace base

#else

ABSL_CONST_INIT static absl::base_internal::SpinLock trace_mu(
    absl::base_internal::SCHEDULE_KERNEL_ONLY);

namespace base {
namespace internal {
// Flags for debugging and profiling Mutex use
ABSL_CONST_INIT std::atomic<int32_t> synch_profile_period{100};

}  // namespace internal
}  // namespace base

ABSL_FLAG(int32_t, synch_profile_period, 100,
          "if non-zero, collect contended mutex and spinlock stack "
          "traces at random with probability 1/synch_profile_period")
    .OnUpdate([] {
      ::base::internal::synch_profile_period.store(
          absl::GetFlag(FLAGS_synch_profile_period), std::memory_order_relaxed);
    });

ABSL_CONST_INIT std::atomic<int32_t> synch_contend_trace{0};

// Tracing code
ABSL_FLAG(int32_t, synch_contend_trace, 0,
          "if non-zero, trace contended mutex operations at random with "
          "probability 1/synch_contend_trace")
    .OnUpdate([] {
      synch_contend_trace.store(absl::GetFlag(FLAGS_synch_contend_trace),
                                std::memory_order_relaxed);
    });

ABSL_CONST_INIT std::atomic<int32_t> synch_cv_trace{0};

ABSL_FLAG(int32_t, synch_cv_trace, 0,
          "if non-zero, trace CondVar operations at random with "
          "probability 1/synch_cv_trace")
    .OnUpdate([] {
      synch_cv_trace.store(absl::GetFlag(FLAGS_synch_cv_trace),
                           std::memory_order_relaxed);
    });

namespace {

ABSL_CONST_INIT char kDefaultSynchTraceFile[] = "google_synch.trace";

// synch_trace_file is only called from contexts already holding the spinlock.
std::string* synch_trace_file() ABSL_EXCLUSIVE_LOCKS_REQUIRED(trace_mu) {
  static auto* trace_file = new std::string(kDefaultSynchTraceFile);
  return trace_file;
}

}  // namespace

ABSL_FLAG(std::string, synch_trace_file, kDefaultSynchTraceFile,
          "trace file name for synch_contend_trace and synch_cv_trace. "
          "If path is not specified, it will be created in "
          "logging directories.")
    .OnUpdate([] {
      absl::base_internal::SpinLockHolder l(trace_mu);
      synch_trace_file()->assign(absl::GetFlag(FLAGS_synch_trace_file));
    });

ABSL_CONST_INIT std::atomic<bool> synch_trace_to_file{false};

ABSL_FLAG(bool, synch_trace_to_file, false, "enable tracing to file")
    .OnUpdate([] {
      synch_trace_to_file.store(absl::GetFlag(FLAGS_synch_trace_to_file),
                                std::memory_order_relaxed);
    });

ABSL_CONST_INIT std::atomic<int32_t> synch_stack_depth{40};

ABSL_FLAG(int32_t, synch_stack_depth, 40,
          "on contention, output the top synch_stack_depth activiation "
          "records.  Regardless of the requested synch_stack_depth, "
          "never output more than 40.")
    .OnUpdate([] {
      synch_stack_depth.store(absl::GetFlag(FLAGS_synch_stack_depth),
                              std::memory_order_relaxed);
    });

ABSL_CONST_INIT std::atomic<int32_t> synch_blocked_threshold_cycles{0};

ABSL_FLAG(int32_t, synch_blocked_threshold_cycles, 0,
          "do not output traces for blocked time less than "
          "synch_blocked_threshold_cycles.")
    .OnUpdate([] {
      synch_blocked_threshold_cycles.store(
          absl::GetFlag(FLAGS_synch_blocked_threshold_cycles),
          std::memory_order_relaxed);
    });

ABSL_CONST_INIT std::atomic<bool> synch_use_stack_pointer_for_compression{
    false};

ABSL_FLAG(bool, synch_use_stack_pointer_for_compression, false,
          "if true, don't output a stack trace if it has a stack pointer "
          "for which we have already outputted the trace.")
    .OnUpdate([] {
      synch_use_stack_pointer_for_compression.store(
          absl::GetFlag(FLAGS_synch_use_stack_pointer_for_compression),
          std::memory_order_relaxed);
    });

namespace base {

// ------------------------------------------ support for stack trace cache

// The following array and its accessor functions cache the stack pointer for
// which we have previously recorded a stack trace.  If we see the same stack
// pointer again, we output just the stack pointer and avoid calculating and
// outputting the full stack trace.

// These constants must all be power of two
enum StackTraceCacheConfig {
  // 4 way associative cache; each set takes 8 * 4 bytes on a 64 bit machine
  // and thus fits nicely in a L1 D-Cache line.
  kStackTraceCacheAssociativity = 4,
  kStackTraceCacheSets = 64,
  kNumCacheEntries = kStackTraceCacheAssociativity * kStackTraceCacheSets
};

static std::atomic<uintptr_t> ABSL_CACHELINE_ALIGNED
    stack_trace_cache[kStackTraceCacheSets * kStackTraceCacheAssociativity];

static int SpToSet(uintptr_t sp) {
  uintptr_t temp = (sp >> 16) ^ sp;
  return temp & (kStackTraceCacheSets - 1);
}

static uintptr_t GetCurrentSP() {
  int dummy;  // address of dummy used as proxy for stack pointer
  uintptr_t retval = reinterpret_cast<uintptr_t>(&dummy);
  return retval;
}

// GetSeenSP returns true if we have seen this stack pointer (sp) before.
// If we have not seen it before, it returns false and also records the
// sp as "seen".
static bool GetSeenSP(uintptr_t sp) {
  // set_i is the starting index of a set in stack_trace_cache.
  int set_i = SpToSet(sp) * kStackTraceCacheAssociativity;
  int empty_i = -1;  // Index of empty slot
  int sp_i = -1;     // Index of sp in cache

  // Go through the cache set to (i) determine that sp is already there; or
  // (ii) find an empty entry in which we can put sp.
  for (int i = 0; i < kStackTraceCacheAssociativity; ++i) {
    uintptr_t value =
        stack_trace_cache[set_i + i].load(std::memory_order_relaxed);
    if (value == sp) {
      sp_i = set_i + i;  // Found sp in cache
      break;
    } else if (empty_i == -1 && value == 0) {
      empty_i = set_i + i;  // Save the position of an empty slot
    }
  }

  // Use a random number generated using the cycle counter to pick a
  // victim to evict.
  int victim_i = CycleClock::Now() & (kNumCacheEntries - 1);
  // If victim_i == sp_i don't overwrite the entry.
  if (victim_i != sp_i) {
    stack_trace_cache[victim_i].store(0, std::memory_order_relaxed);
  }

  // SP was not in cache so insert it
  if (sp_i == -1) {
    if (empty_i == -1) {  // If no empty slot, overwrite a slot in set
      // Use the lower bits of victim_i as a "random number" to pick an
      // entry to overwrite.
      empty_i = set_i + (victim_i & (kStackTraceCacheAssociativity - 1));
    }
    stack_trace_cache[empty_i].store(sp, std::memory_order_relaxed);
  }
  return sp_i != -1;  // i.e., return whether sp was already in the cache
}

// ------------------------------------------ lock profile support

ABSL_CONST_INIT static const std::vector<std::string>* absl_nullable
    logging_directories ABSL_GUARDED_BY(trace_mu) = nullptr;
ABSL_CONST_INIT static FILE* absl_nullable trace_fp ABSL_GUARDED_BY(trace_mu)
    ABSL_PT_GUARDED_BY(trace_mu) = nullptr;

namespace internal {

ABSL_CONST_INIT SpinLock
    contentionz_lock(base::scheduling::SCHEDULE_KERNEL_ONLY);
ABSL_CONST_INIT SpinLock
    deltacontentionz_lock(base::scheduling::SCHEDULE_KERNEL_ONLY);
ContentionzData contentionz_data ABSL_GUARDED_BY(contentionz_lock);
ABSL_CONST_INIT int64_t total_cycles ABSL_GUARDED_BY(contentionz_lock) = 0;
ABSL_CONST_INIT std::array<DeltaContentionProfile*, kMaxInflightDeltaProfiles>
    deltacontention_profiles ABSL_GUARDED_BY(deltacontentionz_lock) = {};
ABSL_CONST_INIT std::atomic<int32_t> num_deltacontention_profiles{0};

// For recording cycles that the profiler could not record as the profiler was
// busy recording data from another thread.
ABSL_CONST_INIT std::atomic<int64_t> g_not_recorded_locking_cycles{0};
ABSL_CONST_INIT std::atomic<int64_t> g_not_recorded_unlocking_cycles{0};

ABSL_CONST_INIT static HookArray<MutexLockProfilingHook> mutex_lock_hooks = {
    {0}, {}};
ABSL_CONST_INIT static HookArray<MutexUnlockProfilingHook> mutex_unlock_hooks =
    {{0}, {}};

bool AddMutexLockProfilingHook(MutexLockProfilingHook h) {
  return mutex_lock_hooks.Add(h);
}

bool AddMutexUnlockProfilingHook(MutexUnlockProfilingHook h) {
  return mutex_unlock_hooks.Add(h);
}

// Increment sample count, and sum cycles for this stack trace
static void AddStackTrace(void** stack, int depth, int64_t cycles,
                          bool has_context) {
  static_assert(sizeof(void*) == sizeof(uintptr_t), "broken_stdint_h");

  // Get hash key
  uintptr_t h = absl::Hash<absl::Span<void*> >()(absl::MakeSpan(stack, depth));
  h %= kMutexProfileHashTableSize;

  // Select slot
  ContentionzData::StackTrace* e;
  ContentionzData::StackTrace* least_used;
  least_used = contentionz_data.traces[h];

  // Do not block while attempting to acquire contentionz_lock.  TryLock is
  // required on the acquisition below because SpinLock contention profiling
  // calls this code, and SpinLock is async-signal-safe.  A signal handler can
  // interrupt a thread holding contentionz_lock, then report contention on
  // another SpinLock it used, and arrive here with contentionz_lock already
  // held.
  if (!contentionz_lock.try_lock()) {
    return;
  }
  if (cycles < 0) {
    contentionz_lock.unlock();
    return;
  }

  // We track total cycles separately as it will differ from the sum of
  // per-trace 'cycles' if we ever evict a trace with nonzero cycles.
  total_cycles += cycles;

  for (int a = 0; a < kMutexProfileAssociativity; a++) {
    e = &(contentionz_data.traces[h][a]);
    if (e->counters[0].count + e->counters[1].count != 0 && e->depth == depth &&
        memcmp(stack, e->stack, depth * sizeof(stack[0])) == 0) {
      // Entry found in table, update cycles and count
      e->counters[has_context].cycles += cycles;
      e->counters[has_context].count++;
      contentionz_lock.unlock();
      return;
    }
    if (e->counters[0].cycles + e->counters[1].cycles <
        least_used->counters[0].cycles + least_used->counters[1].cycles) {
      least_used = e;
    }
  }

  // Count any evicted contention data. Note that least_used will be
  // zero if there's no data, so it's ok to just add without checking for
  // valid data.
  for (int context = 0; context < kMutexProfileContexts; ++context) {
    contentionz_data.evicted[context].cycles +=
        least_used->counters[context].cycles;
    contentionz_data.evicted[context].count +=
        least_used->counters[context].count;
    least_used->counters[context].cycles = 0;
    least_used->counters[context].count = 0;
  }
  // Insert to slot with lowest or zero cycles
  least_used->counters[has_context].cycles = cycles;
  least_used->counters[has_context].count = 1;
  least_used->depth = depth;
  memcpy(least_used->stack, stack, depth * sizeof(stack[0]));

  contentionz_lock.unlock();
}

}  // namespace internal

// Open a trace file and return FILE *, or 0 on failure.
// Reports errors to stderr.
static FILE* OpenTraceFile(const char* filename) {
  FILE* fp = fopen(filename, "w");
  if (fp == nullptr) {
    fprintf(stderr,
            "synchronization.cc: failed to open trace file %s, errno %d\n",
            filename, errno);
  }
  return fp;
}

namespace {
template <typename T>
uintptr_t numeric_cast(
    const T& thread_id,
    typename std::enable_if<std::is_integral<T>::value>::type* absl_nullable =
        nullptr) {
  return static_cast<uintptr_t>(thread_id);
}

template <typename T>
uintptr_t numeric_cast(
    const T& thread_id,
    typename std::enable_if<!std::is_integral<T>::value>::type* = nullptr) {
  return reinterpret_cast<uintptr_t>(thread_id);
}

uintptr_t GetNumericThreadID() {
#if defined(_POSIX_THREADS)
  return numeric_cast(pthread_self());
#elif defined(_WIN32)
  return GetCurrentThreadId();
#else
#error do not know how to get the thread id
#endif
}
}  // namespace

// Tracing routine for synchronization ops.
// Write a line out to the trace file starting with msg,
// and containing the address of object "obj" and the stack trace.
static void Trace(const char* msg, const void* obj, int64_t count,
                  const char* absl_nullable trace) {
  // Do not block while attempting to acquire trace_mu.  TryLock is
  // required on the acquisition below because SpinLock contention profiling
  // calls this code, and SpinLock is async-signal-safe.  A signal handler can
  // interrupt a thread holding trace_mu, then report contention on
  // another SpinLock it used, and arrive here with trace_mu already
  // held.
  if (!trace_mu.try_lock()) {
    return;
  }
  if (logging_directories == nullptr) {
    // InitGoogle() hasn't initialized logging_directories yet or tracing is
    // turned off via flags.
    trace_mu.unlock();
    return;
  }
  const std::string& trace_file = *synch_trace_file();
  if (trace_file.empty()) {
    synch_trace_to_file.store(false, std::memory_order_relaxed);
  } else {
    if (!absl::StrContains(trace_file, "/")) {
      for (const std::string& dir : *logging_directories) {
        std::string trace_file_name;
        trace_file_name = absl::StrFormat("%s/%s", dir, trace_file);
        trace_fp = OpenTraceFile(trace_file.c_str());
        if (trace_fp != nullptr) {
          // Found a valid file, no need to continue.
          break;
        }
      }
    } else {  // name has a slash; use it directly
      trace_fp = OpenTraceFile(trace_file.c_str());
    }
    if (trace_fp == nullptr) {
      // turn off tracing on error
      synch_trace_to_file.store(false, std::memory_order_relaxed);
    } else {
      // write trace file header
      DumpAddressMap(DebugWriteToFile, reinterpret_cast<void*>(trace_fp));
      fprintf(trace_fp, "--- Trace: ---\nsampling period = %d\n",
              std::max(synch_contend_trace.load(std::memory_order_relaxed),
                       synch_cv_trace.load(std::memory_order_relaxed)));
    }
  }
  if (trace_fp != nullptr) {
    const double rightnow = absl::ToUnixNanos(absl::Now()) * 1e-9;

    uint64_t sp = GetCurrentSP();
    void* pcs[40];
    const int stack_depth = synch_stack_depth.load(std::memory_order_relaxed);
    const int size_to_output =
        (stack_depth >= 0 &&
         static_cast<size_t>(stack_depth) <= ABSL_ARRAYSIZE(pcs))
            ? stack_depth
            : ABSL_ARRAYSIZE(pcs);
    if (synch_use_stack_pointer_for_compression.load(
            std::memory_order_relaxed) &&
        trace == nullptr) {
      if (!GetSeenSP(sp)) {
        // Not in cache: get and output the stack trace
        absl::FPrintF(trace_fp, "%17.6f %s  obj %p thread %u %d %u @", rightnow,
                      msg, obj, GetNumericThreadID(), count, sp);
        int n = absl::GetStackTrace(pcs, size_to_output, 2);
        for (int i = 0; i != n; i++) {
          fprintf(trace_fp, "\t%p", pcs[i]);
        }
      } else {
        // In cache: just output an abbreviated prefix and no stack trace
        absl::FPrintF(trace_fp, "%17.6f %d %u @", rightnow, count, sp);
      }
      fputc('\n', trace_fp);
    } else {
      absl::FPrintF(trace_fp, "%17.6f %s  obj %p thread %u %d %u @", rightnow,
                    msg, obj, GetNumericThreadID(), count, sp);
      if (trace != nullptr) {
        fprintf(trace_fp, "%s\n", trace);
      } else {
        int n = absl::GetStackTrace(pcs, size_to_output, 2);
        for (int i = 0; i != n; i++) {
          fprintf(trace_fp, "\t%p", pcs[i]);
        }
        fputc('\n', trace_fp);
      }
    }
  }
  trace_mu.unlock();
}

// x^32+x^22+x^2+x^1+1 is a primitive polynomial for random numbers
constexpr uint32_t poly = (1 << 22) | (1 << 2) | (1 << 1) | (1 << 0);
ABSL_CONST_INIT static std::atomic<uint32_t> trace_rand(1);
ABSL_CONST_INIT static std::atomic<uint32_t> pprof_rand(1);

// Sampled tracing.   Traces with probability 1/p, or not at all if p<=0.
static inline bool OneIn(std::atomic<uint32_t>* rand_ptr, uint32_t p) {
  if (p == 0) {
    return false;
  }
  // not protected by lock for speed
  uint32_t r = rand_ptr->load(std::memory_order_relaxed);
  // TODO: The cast to int32_t assumes 2s-complement and
  // sign-extension, but technically the implementation could choose
  // what happens here. It could produce 1 or -1, depending on the
  // implementation.
  uint32_t new_val = (r << 1) ^ ((static_cast<int32_t>(r) >> 31) & poly);
  rand_ptr->store(new_val, std::memory_order_relaxed);
  return (r % p) == 0;
}

void SampledTraceCV(const char* msg, const void* obj) {
  if (ABSL_PREDICT_FALSE(synch_trace_to_file.load(std::memory_order_relaxed)) &&
      ABSL_PREDICT_FALSE(
          OneIn(&trace_rand, synch_cv_trace.load(std::memory_order_relaxed)))) {
    int64_t time_cycles = CycleClock::Now();
    Trace(msg, obj, time_cycles, nullptr);
  }
}

void SampledTraceMutex(const char* msg, const void* obj, int64_t wait_cycles) {
  if (ABSL_PREDICT_FALSE(synch_trace_to_file.load(std::memory_order_relaxed)) &&
      synch_blocked_threshold_cycles.load(std::memory_order_relaxed) <
          wait_cycles &&
      OneIn(&trace_rand, synch_contend_trace.load(std::memory_order_relaxed))) {
    Trace(msg, obj, wait_cycles, nullptr);
  }
}

// Common recording routine for SpinLocks and Mutexes to record the
// wait_cycles and stack trace for a contended lock.
static void RecordContentionzData(int64_t wait_cycles) {
  void* pcs[internal::kMutexProfileLegacyStackDepth];
  int n = absl::GetStackTrace(pcs, ABSL_ARRAYSIZE(pcs), 0);
  // Determines whether a Context is present or not.
  bool has_context = base::GetCensusRootIdentifierSignalSafe() != 0;
  internal::AddStackTrace(pcs, n, wait_cycles, has_context);
}

static void RecordMutexLockProfileData(const void* /*mutex*/,
                                       int32_t /*sampling_period*/,
                                       int64_t wait_cycles) {
  if (internal::num_deltacontention_profiles.load(std::memory_order_relaxed) ==
      0) {
    return;
  }
  internal::DeltaContentionData::StackTrace pcs;
  pcs.stack_depth =
      absl::GetStackTrace(pcs.stack.data(), pcs.stack.max_size(), 0);
  bool has_context = base::GetCensusRootIdentifierSignalSafe() != 0;
  const uintptr_t h = absl::HashOf(pcs) % internal::kMutexProfileHashTableSize;

  // Do not block while attempting to acquire deltacontentionz_lock.  TryLock is
  // required on the acquisition below because SpinLock contention profiling
  // calls this code, and SpinLock is async-signal-safe.  A signal handler can
  // interrupt a thread holding deltacontentionz_lock, then report contention on
  // another SpinLock it used, and arrive here with deltacontentionz_lock
  // already held.
  if (!internal::deltacontentionz_lock.try_lock()) {
    internal::g_not_recorded_locking_cycles.fetch_add(
        wait_cycles, std::memory_order_relaxed);
    return;
  }
  internal::deltacontentionz_lock.unlock();
}

static void RecordMutexUnlockProfileData(const void* /*mutex*/,
                                         int32_t /*sampling_period*/,
                                         int64_t total_wait_cycles) {
  if (internal::num_deltacontention_profiles.load(std::memory_order_relaxed) ==
      0) {
    return;
  }
  internal::DeltaContentionData::StackTrace pcs;
  pcs.stack_depth =
      absl::GetStackTrace(pcs.stack.data(), pcs.stack.max_size(), 0);
  bool has_context = base::GetCensusRootIdentifierSignalSafe() != 0;
  const uintptr_t h = absl::HashOf(pcs) % internal::kMutexProfileHashTableSize;

  // Do not block while attempting to acquire deltacontentionz_lock.  TryLock is
  // required on the acquisition below because SpinLock contention profiling
  // calls this code, and SpinLock is async-signal-safe.  A signal handler can
  // interrupt a thread holding deltacontentionz_lock, then report contention on
  // another SpinLock it used, and arrive here with deltacontentionz_lock
  // already held.
  if (!internal::deltacontentionz_lock.try_lock()) {
    internal::g_not_recorded_unlocking_cycles.fetch_add(
        total_wait_cycles, std::memory_order_relaxed);
    return;
  }

  internal::deltacontentionz_lock.unlock();
}

namespace internal {

void DeltaContentionData::Histogram::Update(const Histogram& other) {
  for (int i = 0; i < kHistogramBuckets; ++i) {
    duration[i] += other.duration[i];
    counts[i] += other.counts[i];
  }
  total_duration += other.total_duration;
  total_count += other.total_count;
}

void DeltaContentionData::Histogram::Update(absl::Duration inc_duration) {
  static_assert(kHistogramBuckets >= 1, "Invalid number of histogram buckets");
  size_t index = 0;
  static_assert(kMutexHistogramLimits[kHistogramBuckets - 1] ==
                absl::InfiniteDuration());
  // This avoids overflow because
  // kMutexHistogramLimits[ABSL_ARRAYSIZE(kMutexHistogramLimits) - 1] is
  // std::numeric_limits<>::max() as asserted above.
  while (inc_duration > kMutexHistogramLimits[index]) {
    index += 1;
  }
  duration[index] += inc_duration;
  counts[index]++;
  total_duration += inc_duration;
  total_count++;
}

void DeltaContentionData::Histogram::Reset() {
  for (int i = 0; i < kHistogramBuckets; ++i) {
    duration[i] = absl::ZeroDuration();
    counts[i] = 0;
  }
  total_duration = absl::ZeroDuration();
  total_count = 0;
}

void DeltaContentionData::StackTraceAndStats::UpdateCounters(
    bool has_context, absl::Duration wait_duration) {
  histograms[has_context].Update(wait_duration);
}

DeltaContentionData::DeltaContentionData(int64_t g_not_recorded_cycles)
    : not_recorded_cycles(g_not_recorded_cycles) {}

bool DeltaContentionData::StackTrace::operator==(
    const DeltaContentionData::StackTrace& other) const {
  return stack_depth == other.stack_depth &&
         memcmp(stack.data(), other.stack.data(),
                stack_depth * sizeof(stack[0])) == 0;
}

void DeltaContentionData::InsertOrUpdateTrace(
    uintptr_t hash, const DeltaContentionData::StackAndTags& stack_and_tags,
    bool has_context, absl::Duration wait_duration) {
  static_assert(internal::kMutexProfileAssociativity >= 1);
  StackTraceAndStats* absl_nonnull least_used = &(traces[hash][0]);
  absl::Duration least_total_duration = absl::InfiniteDuration();
  // Search through the associated traces.  If a match is found, update the
  // counters.  Else, find the trace with least number of contention cycles and
  // replace it with the new trace.
  for (int a = 0; a < internal::kMutexProfileAssociativity; ++a) {
    StackTraceAndStats* e = &(traces[hash][a]);
    int64_t total_count = 0;
    for (const Histogram& hist : e->histograms) total_count += hist.total_count;
    if (total_count != 0 && e->stack_and_tags == stack_and_tags) {
      // Entry found in table, update cycles and count
      e->UpdateCounters(has_context, wait_duration);
      return;
    }
    absl::Duration e_total_duration = absl::ZeroDuration();
    for (const Histogram& hist : e->histograms) {
      e_total_duration += hist.total_duration;
    }
    if (e_total_duration < least_total_duration) {
      least_used = e;
      least_total_duration = e_total_duration;
    }
  }
  // Count any evicted contention data. Note that least_used will be
  // zero if there's no data, so it's ok to just add without checking for
  // valid data.
  for (int context = 0; context < internal::kMutexProfileContexts; ++context) {
    evicted_traces_hist[context].Update(least_used->histograms[context]);
    least_used->histograms[context].Reset();
  }
  // Insert to slot with lowest or zero cycles
  least_used->stack_and_tags = stack_and_tags;
  least_used->UpdateCounters(has_context, wait_duration);

  // The profile is no longer empty.
  empty = false;
}

void DeltaContentionData::SetFinalNotRecordedStats(
    int64_t g_not_recorded_cycles) {
  not_recorded_cycles = g_not_recorded_cycles - not_recorded_cycles;
}

}  // namespace internal

// used in Mutex::UnlockSlow
void SubmitMutexProfileData(int64_t wait_cycles) {
  if (OneIn(&pprof_rand,
            internal::synch_profile_period.load(std::memory_order_relaxed))) {
    RecordContentionzData(wait_cycles);
  }
}

void SubmitMutexLockProfileData(const void* mutex, int64_t wait_cycles) {
  int32_t period =
      internal::synch_profile_period.load(std::memory_order_relaxed);
  if (!OneIn(&pprof_rand, period)) {
    return;
  }
  // memory_order_acquire ensures the order between reading size and hooks is
  // preserved.
  int size = internal::mutex_lock_hooks.size.load(std::memory_order_acquire);
  for (int i = 0; i < size; ++i) {
    internal::MutexLockProfilingHook h =
        reinterpret_cast<internal::MutexLockProfilingHook>(
            internal::mutex_lock_hooks.hooks[i].load(
                std::memory_order_acquire));
    h(mutex, period, wait_cycles);
  }
}

void SubmitMutexUnlockProfileData(const void* mutex, int64_t total_wait_cycles,
                                  int64_t /*max_wait_cycles*/,
                                  int64_t /*num_waiters*/) {
  int32_t period =
      internal::synch_profile_period.load(std::memory_order_relaxed);
  if (!OneIn(&pprof_rand, period)) {
    return;
  }
  // memory_order_acquire ensures the order between reading size and hooks is
  // preserved.
  int size = internal::mutex_unlock_hooks.size.load(std::memory_order_acquire);
  for (int i = 0; i < size; ++i) {
    internal::MutexUnlockProfilingHook h =
        reinterpret_cast<internal::MutexUnlockProfilingHook>(
            internal::mutex_unlock_hooks.hooks[i].load(
                std::memory_order_acquire));
    h(mutex, period, total_wait_cycles);
  }
}

// used in SpinLock::Release
void SubmitSpinLockProfileData(const void* contendedlock, int64_t wait_cycles) {
  if (contendedlock == &internal::contentionz_lock ||
      contendedlock == &internal::deltacontentionz_lock ||
      contendedlock == &trace_mu) {
    // This case prevents unwanted recursion in the case when either
    // contentionz_lock or trace_mu (declared above) is highly
    // contended and FLAGS_synch_profile_period is too close to one.
    return;
  }
  if (ABSL_PREDICT_FALSE(synch_trace_to_file.load(std::memory_order_relaxed)) &&
      synch_blocked_threshold_cycles.load(std::memory_order_relaxed) <
          wait_cycles &&
      OneIn(&trace_rand, synch_contend_trace.load(std::memory_order_relaxed))) {
    Trace("slow release", contendedlock, wait_cycles, nullptr);
  }
  // Toss a coin to figure if we should record the current event in the
  // profiler.
  int32_t period =
      internal::synch_profile_period.load(std::memory_order_relaxed);
  if (!OneIn(&pprof_rand, period)) {
    return;
  }
  RecordContentionzData(wait_cycles);
  // Record spinlock wait cycles in deltacontentionz.
  //
  // We currently record the locking_delay for the spinlock, but add it to the
  // unlocking_delay metric.  This so that we keep the behavior that
  // contentionz has.  <link> mentions other
  // possible choices.
  //
  // We will remove the RecordContentionzData above once deltacontentionz is in
  // use everywhere.
  //
  // memory_order_acquire ensures the order between reading size and hooks is
  // preserved.
  int size = internal::mutex_unlock_hooks.size.load(std::memory_order_acquire);
  for (int i = 0; i < size; ++i) {
    internal::MutexLockProfilingHook h =
        reinterpret_cast<internal::MutexUnlockProfilingHook>(
            internal::mutex_unlock_hooks.hooks[i].load(
                std::memory_order_acquire));
    h(contendedlock, period, wait_cycles);
  }
}

namespace internal {
namespace {

// Stub routine to produce a helpfully named stack for any contention data that
// was lost as the spinlock protecting the data was not available.
ABSL_ATTRIBUTE_NOINLINE
void LostContentionData(base::Profile::Entry* e) {
  // Record just the leaf frame with no call stack.
  // This will make the dropped data easier to detect.
  e->depth = absl::GetStackTrace(e->stack, 1, 0);
}

// Stub routine to produce a helpfully named stack for any contention data that
// was evicted from the cache.
ABSL_ATTRIBUTE_NOINLINE
void EvictedContentionData(base::Profile::Entry* e) {
  // Record just the leaf frame with no call stack.
  // This will make the dropped data easier to detect.
  e->depth = absl::GetStackTrace(e->stack, 1, 0);
}

}  // namespace

void ContentionProfile::RecordEntry(
    const base::internal::ContentionzData::StackCounts* counters,
    double period_micros, int depth, void** stack, void* arg,
    Handler func) const {
  int64_t total_cycles = 0;
  int64_t total_count = 0;
  uintptr_t tags[static_cast<int>(base::ContentionzIndexOffsets::kNumTags)] = {
      0};
  // Computes the total for the mutex and tags both without and with
  // context.
  for (int context = 0; context < base::internal::kMutexProfileContexts;
       ++context) {
    total_cycles += counters[context].cycles;
    total_count += counters[context].count;
  }
  if (total_count > 0) {
    // Computes the delay scaled to microseconds.
    int64_t micros = total_cycles * period_micros;
    // count is the number of lock acquisitions, unsampled.
    int64_t count = total_count * period_;
    base::Profile::Entry e;
    // Note that the sum and count record the total for both with
    // and without Context. The tags split records with and without
    // Context. Hence there will be two records for many call stacks.
    // Note that ContentionProfileEncoder uses the values recorded in
    // the tags, ignoring e.sum and e.count.
    e.sum = micros;
    e.count = count;
    e.depth = depth;
    e.stack = stack;
    e.ntags = static_cast<int>(base::ContentionzIndexOffsets::kNumTags);
    e.tags = tags;
    for (int context = 0; context < base::internal::kMutexProfileContexts;
         ++context) {
      int64_t context_micros = counters[context].cycles * period_micros;
      int64_t context_count = counters[context].count * period_;
      // Only report records that contain data.
      if (context_count > 0) {
        tags[static_cast<int>(base::ContentionzIndexOffsets::kMicrosIndex)] =
            context_micros;
        tags[static_cast<int>(base::ContentionzIndexOffsets::kCountIndex)] =
            context_count;
        tags[static_cast<int>(
            base::ContentionzIndexOffsets::kHasContextIndex)] = context;
        (*func)(arg, e);
      }
    }
  }
}

void ContentionProfile::Iterate(void* arg, Handler func) const {
  double period_micros = period_ / frequency_micros_;
  for (int i = 0; i < base::internal::kMutexProfileHashTableSize; ++i) {
    for (int a = 0; a < base::internal::kMutexProfileAssociativity; ++a) {
      RecordEntry(data_.traces[i][a].counters, period_micros,
                  data_.traces[i][a].depth,
                  const_cast<void**>(&data_.traces[i][a].stack[0]), arg, func);
    }
  }
  // Records a pointer to within the LostContentionData function to
  // capture evicted data.
  uintptr_t stack = reinterpret_cast<uintptr_t>(&LostContentionData) + 1;
  RecordEntry(data_.evicted, period_micros, 1, reinterpret_cast<void**>(&stack),
              arg, func);
}

}  // namespace internal

void DeltaContentionProfile::RecordLockEntry(
    const std::array<absl::Duration, internal::kHistogramBuckets>&
        histogram_bucket_limits,
    const internal::DeltaContentionData::StackAndTags& stack_and_tags,
    const internal::DeltaContentionData::Histogram& locking_histogram,
    int context, absl::FunctionRef<void(const ValuesAndLabels&)> func) const {
  // Only report records that contain data.
  if (locking_histogram.total_count <= 0) return;
  for (int b = 0; b < internal::kHistogramBuckets; ++b) {
    if (locking_histogram.counts[b] == 0) continue;
    ValuesAndLabels vl{
        .stack = stack_and_tags.stack.stack.data(),
        .depth = stack_and_tags.stack.stack_depth,
        .tags = stack_and_tags.tags,
    };
    vl.histogram_bucket_min =
        (b == 0) ? absl::ZeroDuration() : histogram_bucket_limits[b - 1];
    vl.histogram_bucket_max = (b == internal::kHistogramBuckets - 1)
                                  ? absl::InfiniteDuration()
                                  : histogram_bucket_limits[b];
    vl.locking_contentions = locking_histogram.counts[b] * sampling_period_;
    vl.locking_delay = locking_histogram.duration[b] * sampling_period_;
    vl.has_context = context;
    func(vl);
  }
}

void DeltaContentionProfile::RecordLockEntries(
    const std::array<absl::Duration, internal::kHistogramBuckets>&
        histogram_bucket_limits,
    const internal::DeltaContentionData::StackAndTags& stack_and_tags,
    const std::array<internal::DeltaContentionData::Histogram,
                     internal::kMutexProfileContexts>& counters,
    absl::FunctionRef<void(const ValuesAndLabels&)> func) const {
  for (int context = 0; context < internal::kMutexProfileContexts; ++context) {
    const internal::DeltaContentionData::Histogram& locking_histogram =
        counters[context];
    RecordLockEntry(histogram_bucket_limits, stack_and_tags, locking_histogram,
                    context, func);
  }
}

void DeltaContentionProfile::RecordUnlockEntry(
    const std::array<absl::Duration, internal::kHistogramBuckets>&
        histogram_bucket_limits,
    const internal::DeltaContentionData::StackAndTags& stack_and_tags,
    const internal::DeltaContentionData::Histogram& locked_histogram,
    int context, absl::FunctionRef<void(const ValuesAndLabels&)> func) const {
  // Only report records that contain data.
  if (locked_histogram.total_count <= 0) return;
  for (int b = 0; b < internal::kHistogramBuckets; ++b) {
    if (locked_histogram.counts[b] == 0) continue;
    ValuesAndLabels vl{
        .stack = stack_and_tags.stack.stack.data(),
        .depth = stack_and_tags.stack.stack_depth,
        .tags = stack_and_tags.tags,
    };
    vl.histogram_bucket_min =
        (b == 0) ? absl::ZeroDuration() : histogram_bucket_limits[b - 1];
    vl.histogram_bucket_max = (b == internal::kHistogramBuckets - 1)
                                  ? absl::InfiniteDuration()
                                  : histogram_bucket_limits[b];
    vl.unlocking_contentions = locked_histogram.counts[b] * sampling_period_;
    vl.unlocking_delay = locked_histogram.duration[b] * sampling_period_;
    vl.has_context = context;
    func(vl);
  }
}

void DeltaContentionProfile::RecordUnlockEntries(
    const std::array<absl::Duration, internal::kHistogramBuckets>&
        histogram_bucket_limits,
    const internal::DeltaContentionData::StackAndTags& stack_and_tags,
    const std::array<internal::DeltaContentionData::Histogram,
                     internal::kMutexProfileContexts>& counters,
    absl::FunctionRef<void(const ValuesAndLabels&)> func) const {
  for (int context = 0; context < internal::kMutexProfileContexts; ++context) {
    const internal::DeltaContentionData::Histogram& locked_histogram =
        counters[context];
    RecordUnlockEntry(histogram_bucket_limits, stack_and_tags, locked_histogram,
                      context, func);
  }
}

void DeltaContentionProfile::Iterate(
    absl::FunctionRef<void(const ValuesAndLabels&)> func) const {
  for (int i = 0; i < internal::kMutexProfileHashTableSize; ++i) {
    for (int a = 0; a < internal::kMutexProfileAssociativity; ++a) {
      const internal::DeltaContentionData::StackTraceAndStats& trace_and_stats =
          locking_data_.traces[i][a];
      RecordLockEntries(internal::kMutexHistogramLimits,
                        trace_and_stats.stack_and_tags,
                        trace_and_stats.histograms, func);
      const internal::DeltaContentionData::StackTraceAndStats&
          locked_trace_and_stats = unlocking_data_.traces[i][a];
      RecordUnlockEntries(internal::kMutexHistogramLimits,
                          locked_trace_and_stats.stack_and_tags,
                          locked_trace_and_stats.histograms, func);
    }
  }
  // Record a pointer to the EvictedContentionData function to capture evicted
  // data.
  internal::DeltaContentionData::StackAndTags stack_and_tags;
  stack_and_tags.stack.stack[0] = reinterpret_cast<void*>(
      reinterpret_cast<uintptr_t>(&internal::EvictedContentionData) + 1);
  stack_and_tags.stack.stack_depth = 1;
  RecordLockEntries(internal::kMutexHistogramLimits, stack_and_tags,
                    locking_data_.evicted_traces_hist, func);
  RecordUnlockEntries(internal::kMutexHistogramLimits, stack_and_tags,
                      unlocking_data_.evicted_traces_hist, func);
  // Record a pointer to the NotRecordedContentionData function to capture
  // evicted data.
  stack_and_tags.stack.stack[0] = reinterpret_cast<void*>(
      reinterpret_cast<uintptr_t>(&internal::LostContentionData) + 1);
  stack_and_tags.stack.stack_depth = 1;
  if (locking_data_.not_recorded_cycles > 0) {
    internal::DeltaContentionData::Histogram h = {};
    h.Update(locking_data_.not_recorded_cycles * clock_cycle_duration_);
    RecordLockEntry(internal::kMutexHistogramLimits, stack_and_tags, h,
                    /*context=*/0, func);
  }
  if (unlocking_data_.not_recorded_cycles > 0) {
    internal::DeltaContentionData::Histogram h = {};
    h.Update(unlocking_data_.not_recorded_cycles * clock_cycle_duration_);
    RecordUnlockEntry(internal::kMutexHistogramLimits, stack_and_tags, h,
                      /*context=*/0, func);
  }
}

DeltaContentionProfile::DeltaContentionProfile()
    : sampling_period_(
          internal::synch_profile_period.load(std::memory_order_relaxed)),
      // TODO: make frequency used more dynamic as it may change
      // over time.
      clock_cycle_duration_(absl::Seconds(1 / CycleClock::Frequency())),
      start_time_(absl::Now()),
      locking_data_(internal::g_not_recorded_locking_cycles.load(
          std::memory_order_relaxed)),
      unlocking_data_(internal::g_not_recorded_unlocking_cycles.load(
          std::memory_order_relaxed)) {}

void DeltaContentionProfile::SetFinalNotRecordedStats() {
  locking_data_.SetFinalNotRecordedStats(
      internal::g_not_recorded_locking_cycles.load(std::memory_order_relaxed));
  unlocking_data_.SetFinalNotRecordedStats(
      internal::g_not_recorded_unlocking_cycles.load(
          std::memory_order_relaxed));
}

absl::Duration DeltaContentionProfile::TotalLockingDelayForTesting() const {
  absl::Duration total_duration = absl::ZeroDuration();
  for (int i = 0; i < internal::kMutexProfileHashTableSize; ++i) {
    for (int j = 0; j < internal::kMutexProfileAssociativity; ++j) {
      const auto& trace_and_stats = locking_data_.traces[i][j];
      for (int k = 0; k < internal::kMutexProfileContexts; ++k) {
        const auto& histogram = trace_and_stats.histograms[k];
        total_duration += (histogram.total_duration * sampling_period_);
      }
    }
  }
  for (int k = 0; k < internal::kMutexProfileContexts; ++k) {
    const auto& histogram = locking_data_.evicted_traces_hist[k];
    total_duration += (histogram.total_duration * sampling_period_);
  }
  return total_duration;
}

DeltaContentionToken DeltaContentionToken::StartProfiling() {
  auto* profile = new DeltaContentionProfile();
  DeltaContentionToken token(profile);
  SpinLockHolder holder(internal::deltacontentionz_lock);
  for (int i = 0; i < internal::kMaxInflightDeltaProfiles; ++i) {
    if (internal::deltacontention_profiles[i] == nullptr) {
      internal::deltacontention_profiles[i] = profile;
      int32_t num_profiles = internal::num_deltacontention_profiles.fetch_add(
          1, std::memory_order_relaxed);
      ABSL_RAW_CHECK(num_profiles <= internal::kMaxInflightDeltaProfiles,
                     "Incorrect number of deltacontention profiles.");
      return token;
    }
  }
  return DeltaContentionToken();
}

absl_nullable std::unique_ptr<DeltaContentionProfile>
DeltaContentionToken::StopProfiling() && {
  if (!IsValid()) return nullptr;
  StopProfilingHelper();
  return std::move(profile_);
}

DeltaContentionToken::~DeltaContentionToken() {
  if (!IsValid()) return;
  StopProfilingHelper();
}

void DeltaContentionToken::StopProfilingHelper() {
  SpinLockHolder holder(internal::deltacontentionz_lock);
  for (int i = 0; i < internal::kMaxInflightDeltaProfiles; ++i) {
    if (internal::deltacontention_profiles[i] == profile_.get()) {
      profile_->SetFinalNotRecordedStats();
      internal::deltacontention_profiles[i] = nullptr;
      int32_t num_profiles = internal::num_deltacontention_profiles.fetch_sub(
          1, std::memory_order_relaxed);
      ABSL_RAW_CHECK(num_profiles >= 0,
                     "Incorrect number of deltacontention profiles.");
      profile_->set_end_time(absl::Now());
      break;
    }
  }
}

}  // namespace base

namespace {

// When synchronization_profiling.cc is linked into a program, register the
// lock profiling hooks on process startup.
class SynchronzationHookInstaller {
 public:
  SynchronzationHookInstaller() {
    base::RegisterSpinLockProfiler(&base::SubmitSpinLockProfileData);
    absl::RegisterMutexProfiler(&base::SubmitMutexProfileData);
    absl::RegisterMutexLockProfiler(&base::SubmitMutexLockProfileData);
    absl::RegisterMutexUnlockProfiler(&base::SubmitMutexUnlockProfileData);
    absl::RegisterMutexTracer(&base::SampledTraceMutex);
    absl::RegisterCondVarTracer(&base::SampledTraceCV);
    ABSL_RAW_CHECK(base::internal::AddMutexLockProfilingHook(
                       &base::RecordMutexLockProfileData),
                   "Failed to add lock hook");
    ABSL_RAW_CHECK(base::internal::AddMutexUnlockProfilingHook(
                       &base::RecordMutexUnlockProfileData),
                   "Failed to add unlock hook");
  }
} install_synchronization_hooks;

}  // namespace

namespace base {
namespace internal {

void SetProfilingDirectories(const std::vector<std::string>& dirs) {
  // We make a copy of GetLoggingDirectories() here from InitGoogle() so we can
  // use the value without taking the any lock other than trace_mu in
  // base::Trace(). If we take any other lock, it may recursively try to profile
  // itself.
  absl::base_internal::SpinLockHolder l(trace_mu);
  if (synch_trace_to_file.load(std::memory_order_relaxed) &&
      !absl::GetFlag(FLAGS_synch_trace_file).empty()) {
    logging_directories = new std::vector<std::string>(dirs);
  }
}

}  // namespace internal

}  // namespace base

// Routine for stack trace cache unit test.
// Returns true iff the stack cache invariants are not broken.
// The stack_trace_cache is not locked and could mutate while
// the check is running but it shouldn't matter.
// This test code is here in the module because otherwise we
// have to widen the interface more than is desirable.
bool CheckStackCacheInvariants() {
  bool result = true;
  for (int i = 0; i < base::kStackTraceCacheSets; ++i) {
    for (int j = 0; j < base::kStackTraceCacheAssociativity; ++j) {
      int index = i * base::kStackTraceCacheAssociativity + j;
      uintptr_t element =
          base::stack_trace_cache[index].load(std::memory_order_relaxed);
      if (element != 0 && base::SpToSet(element) != i) {
        result = false;
        break;
      }
    }
    if (result == false) {
      break;
    }
  }
  return result;
}

#endif  // PORTABLE_BASE
