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

#ifndef THIRD_PARTY_GLOOP_BASE_AUXILIARY_SYNCHRONIZATION_PROFILING_H_
#define THIRD_PARTY_GLOOP_BASE_AUXILIARY_SYNCHRONIZATION_PROFILING_H_

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/flags/declare.h"
#include "gloop/base/auxiliary/synchronization_profile.h"
#include "gloop/base/spinlock.h"

ABSL_DECLARE_FLAG(int32_t, synch_profile_period);

ABSL_DECLARE_FLAG(int32_t, synch_contend_trace);
ABSL_DECLARE_FLAG(int32_t, synch_cv_trace);

ABSL_DECLARE_FLAG(std::string, synch_trace_file);

ABSL_DECLARE_FLAG(int32_t, synch_stack_depth);
ABSL_DECLARE_FLAG(int32_t, synch_blocked_threshold_cycles);
ABSL_DECLARE_FLAG(bool, synch_use_stack_pointer_for_compression);

namespace base {

// Submit the cycles spent contending for the mutex.
extern void SubmitMutexProfileData(int64_t wait_cycles);

// Submit the cycles spent contending while acquiring the mutex.
// 'Contended' here means that the acquiring thread took the slow path for
// acquiring the mutex.
//
// - mutex: pointer to acquired mutex
// - wait_cycles: cycles for which the thread waited (as measured by
// //absl/base/internal/cycleclock.h, and which may not be real
// "cycle" counts).
//
// Used for absl::Mutex and other mutex-like things.
extern void SubmitMutexLockProfileData(const void* mutex, int64_t wait_cycles);

// Called whenever a mutex that is contentended for is released.  'Contended'
// here means the releasing thread woke up at least one other thread that was
// waiting to acquire the mutex.
//
// - mutex: pointer to the released mutex
// - total_wait_cycles: total wait cycles all waken up threads had to wait
// for.
// - max_wait_cycles: maximum wait cycles any of the waken up threads had to
//   wait for.
// - num_waiters: number of waken up threads.
// Used for absl::Mutex and other mutex-like things.
extern void SubmitMutexUnlockProfileData(const void* mutex,
                                         int64_t total_wait_cycles,
                                         int64_t max_wait_cycles,
                                         int64_t num_waiters);

// Tracing routine for synchronization ops.
// Write a line out to the trace file starting with msg.
// and containing the address of object "obj" and the stack trace.
extern void SampledTraceCV(const char* msg, const void* obj);
extern void SampledTraceMutex(const char* msg, const void* obj,
                              int64_t wait_cycles);

// Submit the number of cycles the spinlock spent contending.  Also, if
// sampled tracing is enabled, potentially record a "slow unlock" event for
// the spinlock.
extern void SubmitSpinLockProfileData(const void* contendedlock,
                                      int64_t wait_cycles);

namespace internal {

void SetProfilingDirectories(const std::vector<std::string>& dirs);

extern SpinLock deltacontentionz_lock;

// Currently there are two consumers for contention data: deltacontentionz and
// Census.
static constexpr int kMaxHooks = 2;

template <typename T>
struct HookArray {
  static_assert(sizeof(T) <= sizeof(intptr_t),
                "Hook type should fit in intptr_t");

  // Adds a hook to the array of hooks.  Duplicates are allowed.  Thread safe.
  // Returns true on success; false otherwise.
  bool Add(T value) {
    SpinLockHolder sl(deltacontentionz_lock);
    // Find the first slot in data that is 0.
    int index = size.load(std::memory_order_relaxed);
    if (index >= kMaxHooks) {
      return false;
    }
    // memory_order_release ensures the ordering between the next two writes is
    // preserved.
    hooks[index].store(reinterpret_cast<intptr_t>(value),
                       std::memory_order_release);
    size.store(index + 1, std::memory_order_release);
    return true;
  }

  std::atomic<int> size;
  std::atomic<intptr_t> hooks[kMaxHooks];
};

// Support for adding hooks to the hooks array.
typedef void (*MutexLockProfilingHook)(const void* mutex,
                                       int32_t sampling_period,
                                       int64_t wait_cycles);
ABSL_MUST_USE_RESULT bool AddMutexLockProfilingHook(
    MutexLockProfilingHook hook);

typedef void (*MutexUnlockProfilingHook)(const void* mutex,
                                         int32_t sampling_period,
                                         int64_t max_wait_cycles);
ABSL_MUST_USE_RESULT bool AddMutexUnlockProfilingHook(
    MutexUnlockProfilingHook hook);

extern std::atomic<int32_t> synch_profile_period;
extern SpinLock contentionz_lock;
extern ContentionzData contentionz_data ABSL_GUARDED_BY(contentionz_lock);
extern int64_t total_cycles ABSL_GUARDED_BY(contentionz_lock);

}  // namespace internal

}  // namespace base

// Routine for stack trace cache unit test.
// Returns true iff the stack cache invariants are not broken.
extern bool CheckStackCacheInvariants();

#endif  // THIRD_PARTY_GLOOP_BASE_AUXILIARY_SYNCHRONIZATION_PROFILING_H_
