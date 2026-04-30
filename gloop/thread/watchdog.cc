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

#include "gloop/thread/watchdog.h"

#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/cleanup/cleanup.h"
#include "absl/debugging/stacktrace.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/config.h"
#include "gloop/base/context.h"
#include "gloop/base/crash.h"
#include "gloop/base/examine_stack.h"
#include "gloop/base/port.h"
#include "gloop/base/sysinfo.h"
#include "gloop/base/tracecontext.h"
#include "gloop/thread/thread.h"

// Global WatchDog data.
ABSL_CONST_INIT absl::Mutex WatchDog::dogs_lock_(absl::kConstInit);
ABSL_CONST_INIT std::vector<WatchDog*>* WatchDog::dogs_ = nullptr;
ABSL_CONST_INIT int WatchDog::generation_count_ = 0;
ABSL_CONST_INIT bool WatchDog::check_in_progress_ = false;
ABSL_CONST_INIT int WatchDog::active_pauses_ = 0;

// Methods for doing fast approximate reads of the current clock.
// WatchDog::Alive() is called very often, sometimes once per callback in some
// executors. Calling absl::Now() would become a significant source of CPU
// cycles, so we cache the result and update it in a background thread
// instead. The value is int64_t instead of absl::Time so that it becomes a
// lockfree atomic.
static std::atomic<int64_t> cached_unix_nanos(0);

// Updates the cached time and return the new cached time.
//
// `WatchDog::CheckTimeout()` calls this method in a background thread every
// `WatchDog::kInternalCheckSeconds`, so `ReadApproximateClock()` should never
// be more than `WatchDog::kInternalCheckSeconds` stale.
// `WatchDog::CheckTimeout()` must grant slightly more than
// `WatchDog::kInternalCheckSeconds` leeway before triggering the expiration.
static absl::Time UpdateApproximateClock() {
  absl::Time now = absl::Now();
  // This release store pairs with the acquire load in RefreshAliveTimestamp()
  cached_unix_nanos.store(absl::ToUnixNanos(now), std::memory_order_release);
  return now;
}

// Reads the cached time.
static absl::Time ReadApproximateClock() {
  int64_t cached = cached_unix_nanos.load(std::memory_order_relaxed);
  if (ABSL_PREDICT_FALSE(cached == 0)) {  // Uninitialized.
    return UpdateApproximateClock();
  }
  return absl::FromUnixNanos(cached);
}

WatchDog::WatchDog() : WatchDog("?", DefaultTimeout()) {}

WatchDog::WatchDog(const char* name, absl::Duration timeout)
    : WatchDog(std::string(name), timeout) {}
WatchDog::WatchDog(absl::string_view name, absl::Duration timeout)
    : WatchDog(std::string(name), timeout) {}
WatchDog::WatchDog(std::string name, absl::Duration timeout)
    : alive_called_(false),
      disabled_(false),
      tid_(GetTID()),
      callback_tid_(0),
      pthread_id_(pthread_self()),
      name_(std::move(name)) {
  CHECK_GE(timeout, absl::Seconds(1));

  timeout_duration_nanos_.store(timeout / absl::Nanoseconds(1),
                                std::memory_order_relaxed);

  absl::Time now = UpdateApproximateClock();
  last_called_alive_unix_nanos_.store(absl::ToUnixNanos(now),
                                      std::memory_order_relaxed);

  {
    absl::MutexLock l(dogs_lock_);
    if (dogs_ == nullptr) {
      dogs_ = new std::vector<WatchDog*>();
    }
    dogs_index_ = dogs_->size();
    dogs_->push_back(this);
    generation_ = generation_count_++;
  }
}

// Used with Condition() to test for callback_tid_ == 0
static bool IntIsZero(int* x) { return *x == 0; }

WatchDog::~WatchDog() {
  absl::MutexLock lock(dogs_lock_);

  if (callback_tid_ != 0 && callback_tid_ != GetTID()) {
    // If not deleting from callback, wait for any callbacks to finish.
    dogs_lock_.Await(absl::Condition(&IntIsZero, &callback_tid_));
  }
  CHECK_EQ((*dogs_)[dogs_index_], this) << "Watchdog " << this << " not found";
  WatchDog* last = dogs_->back();
  (*dogs_)[dogs_index_] = last;
  last->dogs_index_ = dogs_index_;
  dogs_->pop_back();
}

/*static*/ void WatchDog::RunCallbacks(std::vector<DogCall>* expiry_calls) {
  const absl::Time now = ReadApproximateClock();
  const int64_t now_unix_nanos = absl::ToUnixNanos(now);
  for (const DogCall& dog_call : *expiry_calls) {
    dog_call.cb->Run(dog_call.dog);
    // Reset the dog only after we run the callback, so that it may read the
    // correct time while in the callback.
    dog_call.dog->last_called_alive_unix_nanos_.store(
        now_unix_nanos, std::memory_order_relaxed);
  }

  absl::MutexLock lock(dogs_lock_);
  for (const WatchDog::DogCall& call : *expiry_calls) {
    // zero callback_tid_ iff dog has not been deleted
    WatchDog* dog = call.dog;
    size_t dogs_index = call.dogs_index;
    bool zero_tid = (dogs_index < dogs_->size() && (*dogs_)[dogs_index] == dog);
    for (size_t j = 0; !zero_tid && j != dogs_->size(); j++) {
      zero_tid = ((*dogs_)[j] == dog);
    }
    if (zero_tid && call.gen == dog->generation_) {
      dog->callback_tid_ = 0;
    }
  }
}

void WatchDog::SetCallback(WatchdogCallback callback) {
  absl::MutexLock lock(dogs_lock_);
  callback_.reset(callback);
}

void WatchDog::RefreshAliveTimestamp() {
  // Reading from TSC here is fairly expensive (~1.5kgcu fleetwide as of 2019)
  // so we have a counter store the last observed time on WatchDog::CheckAlive()
  // calls. A background thread (ThreadLivenessWatcher in thread.cc)
  // periodically calls CheckAlive() which will thus ensure a relatively fresh
  // time.
  //
  // Since this is a generally increasing counter only used for timing on the
  // order of seconds, we do not care if we read a slightly stale value (as long
  // as it's not zero).
  //
  // However to handle a case when the calling thread is paused in the middle
  // of this function and then ResumeChecks() automatically refreshes the dogs,
  // we need to do a compare-exchange to avoid the risk of storing an old
  // alive timestamp. The acquire load operations pair with release stores in
  // UpdateApproximateClock() and ResumeChecks().
  //
  // This path is hot enough that we read the cached value directly instead of
  // calling absl::ToUnixNanos(ReadApproximateClock());
  int64_t prev_alive_nanos =
      last_called_alive_unix_nanos_.load(std::memory_order_acquire);
  const int64_t now_unix_nanos =
      cached_unix_nanos.load(std::memory_order_acquire);
  last_called_alive_unix_nanos_.compare_exchange_strong(
      prev_alive_nanos, now_unix_nanos, std::memory_order_release);
}

absl::Duration WatchDog::SetTimeoutDuration(absl::Duration timeout) {
  CHECK_GT(timeout, absl::ZeroDuration());

  const absl::Duration old_timeout = absl::Nanoseconds(
      timeout_duration_nanos_.load(std::memory_order_relaxed));

  timeout_duration_nanos_.store(timeout / absl::Nanoseconds(1),
                                std::memory_order_relaxed);

  UpdateApproximateClock();
  RefreshAliveTimestamp();

  return old_timeout;
}

void WatchDog::Alive() {
  RefreshAliveTimestamp();

  if (ABSL_PREDICT_FALSE(!alive_called_.load(std::memory_order_relaxed))) {
    tid_.store(GetTID(), std::memory_order_relaxed);
    pthread_id_.store(pthread_self(), std::memory_order_relaxed);
    alive_called_.store(true, std::memory_order_relaxed);
  }

  // We must publish `disabled_` after `last_called_alive_unix_nanos` so that we
  // cannot observe a stale expiry on the disabled -> enabled transition in
  // CheckTimeout().
  disabled_.store(false, std::memory_order_release);
}

absl::Duration WatchDog::timeout_duration() const {
  return absl::Nanoseconds(
      timeout_duration_nanos_.load(std::memory_order_relaxed));
}

WatchDog::WatchDogState WatchDog::ReadCurrentState() const {
  return {absl::FromUnixNanos(
              last_called_alive_unix_nanos_.load(std::memory_order_relaxed)),
          absl::Nanoseconds(
              timeout_duration_nanos_.load(std::memory_order_relaxed))};
}

void WatchDog::PrintStatus(char* buf, int buf_size) const {
  PrintStatusInternal(buf, buf_size, ReadCurrentState(),
                      ReadApproximateClock());
}

void WatchDog::PrintStatusInternal(char* buf, int buf_size,
                                   const WatchDogState& state,
                                   absl::Time now_for_check) const {
  absl::Time now = absl::Now();
  absl::Duration seconds_ago = now_for_check - state.last_called_alive;
  absl::TimeZone::CivilInfo last_alive =
      absl::LocalTimeZone().At(state.last_called_alive);
  // CheckTimeout() doesn't update the current time until it exits so that the
  // cached_clock_lag is accurate. We expect cached_clock_lag to be no more than
  // approximately kInternalCheckSeconds. A large value might indicate delays in
  // the Watchdog thread itself.
  absl::Duration cached_clock_lag = now - now_for_check;
  // If the watchdog thread is stalled, its time measurement is unreliable. This
  // can cause false positives. A lag of more than double the check interval is
  // a strong indicator of this.
  if (cached_clock_lag > absl::Seconds(kInternalCheckSeconds * 2)) {
    absl::SNPrintF(buf, buf_size,
                   "Thread id %d last called Alive() %ds ago (%02d:%02d:%02d);"
                   " timeout is %ds. WARNING: Watchdog is stale by %dms; this "
                   "timeout may be a false positive due to system-wide stalls "
                   "(e.g. kernel freeze).\n",
                   tid(), seconds_ago / absl::Seconds(1), last_alive.cs.hour(),
                   last_alive.cs.minute(), last_alive.cs.second(),
                   state.timeout / absl::Seconds(1),
                   cached_clock_lag / absl::Milliseconds(1));
  } else {
    absl::SNPrintF(buf, buf_size,
                   "Thread id %d last called Alive() %ds ago (%02d:%02d:%02d);"
                   " timeout is %ds\n",
                   tid(), seconds_ago / absl::Seconds(1), last_alive.cs.hour(),
                   last_alive.cs.minute(), last_alive.cs.second(),
                   state.timeout / absl::Seconds(1));
  }
}

namespace {
struct PrintStackTraceState {
  pid_t tid;
  bool found;
  DebugWriter* debug_writer;
  void* debug_writer_arg;
};
}  // end namespace

static bool PickThreadWithTid(void* arg, const LiveThread* thread) {
  PrintStackTraceState* state = static_cast<PrintStackTraceState*>(arg);
  if (state->tid == LiveThread_OS_TID(thread)) {
    state->found = true;
    return true;
  }
  return false;
}

static void PrintThreadStackTrace(void* arg, const LiveThread* thread,
                                  const StackTrace* trace) {
  if (trace != nullptr) {
    std::array<void*, 32> pcs;
    int n = StackTrace_GetPCs(trace, pcs.size(),
                              const_cast<const void**>(pcs.data()));
    if (n > 0) {
      PrintStackTraceState* state = static_cast<PrintStackTraceState*>(arg);
      DumpPCAndStackTrace(pcs[0], pcs.data() + 1, n - 1, state->debug_writer,
                          state->debug_writer_arg);
    }
  }
}

void WatchDog::PrintStackTraceTo(DebugWriter* debug_writer,
                                 void* debug_writer_arg) {
  // Get the kernel stack first; dumping the user stack requires delivering a
  // signal to the given thread, and running a signal handler in its context.
  // This is destructive to thread state, if the thread is waiting in an
  // interruptible system call (e.g. clone(2)), the signal may abort the call.
  // Keep the old output order, dumping the kernel stack after the user stack.
  pid_t target_tid = tid();
  std::string kernel_stack;
  bool kernel_stack_success = GetKernelStack(target_tid, &kernel_stack);

  PrintStackTraceState state;
  state.tid = target_tid;
  state.found = false;
  state.debug_writer = debug_writer;
  state.debug_writer_arg = debug_writer_arg;

  ABSL_RAW_LOG(ERROR, "Stack trace of thread %d:", state.tid);

  Thread_ProcessStackTracesArg arg;
  arg.filter = PickThreadWithTid;
  arg.filter_arg = &state;
  arg.process_trace = PrintThreadStackTrace;
  arg.process_trace_arg = &state;
  arg.per_thread_timeout_ms = 10 * 1000;
  // While not officially documented, Thread_ProcessStackTraces() returns
  // the number of threads that we could not get a stack from, after our
  // filter has been applied.
  int num_missed = Thread_ProcessStackTraces(arg);

  if (state.found) {
    ABSL_RAW_LOG(ERROR, "Stack dump of thread %d done.", state.tid);
  } else {
    ABSL_RAW_LOG(ERROR, "Thread %d not found.", state.tid);
  }

  // If we couldn't get a stack trace, let the user know instead of silently
  // failing to print it.
  if (num_missed != 0) {
    debug_writer("Unable to extract user stack.\n", debug_writer_arg);
  }

  // Now dump the kernel stack that we'd saved above.
  if (kernel_stack_success) {
    debug_writer("Kernel stack is:\n", debug_writer_arg);
    debug_writer(kernel_stack.c_str(), debug_writer_arg);
  } else {
    debug_writer("Kernel stack unavailable.\n", debug_writer_arg);
  }
}

// static
bool WatchDog::GetKernelStack(pid_t tid, std::string* stack) {
#if defined(__linux__)
  return ReadProcFileToString("/proc/%d/stack", tid, 1024, stack) >= 0;
#else
  return false;
#endif
}

void WatchDog::PrintExpirationMessage(char* buf, int buf_size,
                                      const WatchDogState& state,
                                      absl::Time now_for_check) const {
  int len;
  len = absl::SNPrintF(buf, buf_size,
                       "Watchdog: %s (pthread id: %x, tid: %u) expired; ",
                       name().c_str(), PRINTABLE_PTHREAD(pthread_id()), tid());
  PrintStatusInternal(buf + len, buf_size - len, state, now_for_check);
}

void WatchDog::SetCrashReasonFromStuckThread() {
#if BASE_HAVE_CRASHREASON
  auto match_pthread = [](void* arg, const LiveThread* thread) {
    pthread_t target_thread = *static_cast<pthread_t*>(arg);
    return LiveThread_Pthread_TID(thread) == target_thread;
  };
  auto run_in_stuck_thread = [](void* arg, ucontext_t* uc,
                                const LiveThread* thread) {
    auto* watchdog = static_cast<WatchDog*>(arg);

    const TraceContext* tc = base::CurrentTraceContextNoAlloc();
    if (tc) {
      ABSL_RAW_LOG(
          INFO,
          "stuck thread TraceContext: global_id = 0x%016lx, rpc_id = 0x%016lx",
          tc->global_id(), tc->rpc_id());
    }

    // Unwind from the stuck thread (current thread) to produce a useful
    // crashing stacktrace in Coroner.
    static base::CrashReason reason;
    reason.message = watchdog->crash_reason_message_;
    reason.filename = __FILE__;
    reason.line_number = __LINE__;
    reason.depth =
        absl::GetStackTrace(reason.stack, ABSL_ARRAYSIZE(reason.stack), 0);
    reason.tc = tc;
    base::SetCrashReason(&reason);
  };
  // Iterate through all threads looking for the stuck thread and collect crash
  // data from it.
  auto pt_id = pthread_id();
  const int failures =
      Thread_ForEach(match_pthread, &pt_id, run_in_stuck_thread, this, 5000);
  if (failures) {
    ABSL_RAW_LOG(ERROR, "Encountered %d failures while iterating over threads.",
                 failures);
  }
#endif  // BASE_HAVE_CRASHREASON
}

void WatchDog::TimedOut() {
  TimedOutInternal(ReadCurrentState(), ReadApproximateClock());
}

void WatchDog::TimedOutInternal(const WatchDogState& state,
                                absl::Time cached_expiration_time) {
  // Timeout exceeded.
  char buf[kPrintStatusBufSize + 200];
  PrintExpirationMessage(buf, sizeof(buf), state, cached_expiration_time);
  ABSL_RAW_LOG(ERROR, "%s", buf);
  PrintStackTrace();
  // Save the expiration message so it doesn't need to be regenerated at a later
  // time in a signal handler.
  crash_reason_message_ = buf;
  SetCrashReasonFromStuckThread();
  LOG(FATAL) << buf;
}

void WatchDog::CheckAlive() {
  std::vector<DogCall> expiry_calls;
  {
    absl::MutexLock lock(dogs_lock_);
    if (dogs_ == nullptr || active_pauses_ > 0) {
      return;
    }

    check_in_progress_ = true;
    CheckTimeout(&expiry_calls);
    check_in_progress_ = false;
  }
  RunCallbacks(&expiry_calls);
}

void WatchDog::CheckTimeout(std::vector<DogCall>* expiry_calls)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(dogs_lock_) {
  dogs_lock_.AssertHeld();
  DCHECK(check_in_progress_);
  DCHECK_EQ(active_pauses_, 0);

  // The periodic calls to this function are responsible for updated the cached
  // clock. However, for timeout checking, use the cached time. This way we will
  // never timeout using a time that Alive() never saw. Wait until we leave this
  // function to update the cached clock. This way, if we do timeout, we can
  // easily log the difference between the current time and the cached time for
  // debugging purposes.
  absl::Cleanup update_clock = []() { UpdateApproximateClock(); };
  const absl::Time now = ReadApproximateClock();

  // Because we only update the clock value here periodically, the clock value
  // used in Alive() calls are stale. This could cause false positives when we
  // check for WatchDogs that have gone too long since their last Alive() call.
  //
  // We know that we call CheckAlive() at least once every
  // kInternalCheckSeconds, so we can bound the maximum staleness (with some
  // fudge factor). Give this much leeway to lock expiry times.
  constexpr absl::Duration max_staleness =
      absl::Seconds(WatchDog::kInternalCheckSeconds + 1);

  pid_t tid = GetTID();

  for (WatchDog* dog : *dogs_) {
    // disabled_ uses release-acquire ordering. If Alive() clears disabled_ and
    // we see that here, then we must have seen the entirety of Alive(),
    // including the latest alive call time. That means we must read disabled_
    // before reading anything else.
    //
    // It's possible to see the latest alive call time but not the latest
    // disabled_, but this is fine. A stale disabled WatchDog from Alive() just
    // means we skip this round, while a stale undisabled WatchDog from
    // Disable() should still have recently checked in.
    const bool dog_disabled = dog->disabled_.load(std::memory_order_acquire);

    const absl::Time last_called_alive = absl::FromUnixNanos(
        dog->last_called_alive_unix_nanos_.load(std::memory_order_relaxed));
    const absl::Duration timeout = absl::Nanoseconds(
        dog->timeout_duration_nanos_.load(std::memory_order_relaxed));
    const absl::Time expire_time = last_called_alive + timeout;

    if (!dog_disabled && expire_time + max_staleness < now &&
        dog->callback_tid_ == 0) {
      if (dog->callback_.get() == nullptr) {
        // Print messages using the exact values used to determine that this
        // WatchDog has expired.
        const WatchDogState state = {last_called_alive, timeout};
        dog->TimedOutInternal(state, now);
        // Reset the dog's expiration time.
        dog->last_called_alive_unix_nanos_.store(absl::ToUnixNanos(now),
                                                 std::memory_order_relaxed);
      } else {
        dog->callback_tid_ = tid;
        DogCall ref_and_call;
        ref_and_call.dog = dog;
        ref_and_call.dogs_index = dog->dogs_index_;
        ref_and_call.gen = dog->generation_;
        ref_and_call.cb = dog->callback_;
        expiry_calls->push_back(ref_and_call);
        // When the DogCall runs, we will reset the dog's expiration time. This
        // is so that any callback that runs will see the old expire time.
      }
    }
  }
}

void WatchDog::PauseChecks() {
  auto check_not_in_progress = []() {
    dogs_lock_.AssertReaderHeld();
    return !check_in_progress_;
  };
  absl::MutexLock lock(dogs_lock_, absl::Condition(&check_not_in_progress));

  ++active_pauses_;
}

void WatchDog::ResumeChecks() {
  absl::MutexLock lock(dogs_lock_);
  DCHECK_GT(active_pauses_, 0);

  --active_pauses_;
  if (active_pauses_ == 0) {
    absl::Time now = UpdateApproximateClock();
    int64_t unix_nanos = absl::ToUnixNanos(now);
    if (dogs_ != nullptr) {
      for (WatchDog* dog : *dogs_) {
        // This release store pairs with the acquire load in
        // RefreshAliveTimestamp()
        dog->last_called_alive_unix_nanos_.store(unix_nanos,
                                                 std::memory_order_release);
      }
    }
  }
}
