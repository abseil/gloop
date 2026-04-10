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

#include "gloop/thread/fiber/futex-domain.h"

#include <sys/time.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <ctime>

#include "absl/base/casts.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/flags/flag.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/internal/kernel_timeout.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/futex.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/downcalls.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/perftools/tracing/public/tracecontext_util.h"
#include "gloop/thread/fiber/internal/fiber-domain-support.h"
#include "gloop/thread/stack_reclaimer.h"

using base::KernelTimeout;
using base::scheduling::Downcalls;
using base::scheduling::Schedulable;

ABSL_FLAG(bool, futex_domain_offload_blocking, true,
          "Immediately issue a FUTEX_SWAP op when encountering blocking "
          "execution.  Only has effect when FutexDomains are used.");

namespace thread {

namespace {

#define SHOULD_TRACE 0

#if SHOULD_TRACE
#define FUTEX_DOMAIN_TRACE(...)       \
  do {                                \
    ABSL_RAW_LOG(ERROR, __VA_ARGS__); \
  } while (0)
#else
void UNUSED(...) {}
#define FUTEX_DOMAIN_TRACE(...) \
  do {                          \
    UNUSED(__VA_ARGS__);        \
  } while (0)
#endif

class FutexDomainThread;

class FutexDomain final : public CommonFiberDomain {
 public:
  // REQUIRES: FutexDomainAvailable() == true
  FutexDomain(absl::string_view name_prefix, int max_concurrency);

  // This type is neither copyable nor movable.
  FutexDomain(const FutexDomain&) = delete;
  FutexDomain& operator=(const FutexDomain&) = delete;

  // Destructor does not return until all threads associated with this domain
  // have been released.
  // REQUIRES: root_scheduler() is deletable.
  ~FutexDomain() override { WaitForThreads(); }

  //----------------------------------------------------------------------------
  // Implementation of the Domain interface.
  void ResumeAdditionalSchedulable(
      base::scheduling::Schedulable* additional) override;
  bool BlockCurrent(base::scheduling::Schedulable* current,
                    KernelTimeout timeout) override;
  bool SwapCurrent(base::scheduling::Schedulable* current,
                   base::scheduling::Schedulable* next,
                   KernelTimeout timeout) override;

  void DomainStartPotentiallyBlockingRegion(
      base::scheduling::Schedulable* current) override;

  // End of Domain implementation.
  //----------------------------------------------------------------------------

  // Wrappers (with domain addins) for SwitchTo
  void CoordinateBlockingTimeout(Schedulable* current);
  bool RawBlock(CommonFiberDomainThread* curr, KernelTimeout timeout) override {
    return RawBlock(absl::down_cast<FutexDomainThread*>(curr), timeout);
  }
  void RawResume(CommonFiberDomainThread* additional) override {
    return RawResume(absl::down_cast<FutexDomainThread*>(additional));
  }
  bool RawSwap(CommonFiberDomainThread* prev, CommonFiberDomainThread* next,
               KernelTimeout timeout) override {
    return RawSwap(absl::down_cast<FutexDomainThread*>(prev),
                   absl::down_cast<FutexDomainThread*>(next), timeout);
  }
  bool RawBlock(FutexDomainThread* curr, KernelTimeout abs_timeout);
  void RawResume(FutexDomainThread* next);
  bool RawSwap(FutexDomainThread* curr, FutexDomainThread* next,
               KernelTimeout timeout);

  FutexDomainThread* RunSchedulableOnThread(Schedulable* sched) {
    return absl::down_cast<FutexDomainThread*>(
        CommonFiberDomain::RunSchedulableOnThread(sched));
  }

  FutexDomainThread* ThreadFromSchedulable(Schedulable* schedulable) {
    return absl::down_cast<FutexDomainThread*>(
        CommonFiberDomain::DomainThreadFromSchedulable(schedulable));
  }

  void StartNewThread(base::scheduling::Schedulable* schedulable) override;

  friend class FutexDomainThread;
};

//------------------------------------------------------------------------------
// FutexDomainThread
//------------------------------------------------------------------------------

// We use Futexes to modulate an individual FutexDomainThread's runnability.
//
// When IsBoundToThread(schedulable) == true:
//   schedulable is bound to a thread.
//   schedulable->managed.work == a pointer to the bound FutexDomainThread.
// When IsBoundToThread(schedulable) == false:
//   schedulable is not yet bound to any thread.
//   schedulable->managed.work == address of closure to execute.
class FutexDomainThread final : public CommonFiberDomainThread {
 public:
  // Possible values of futex_. Constants rather than an enum are used
  // because futexes are atomics over int32_t and because we do arithmetic
  // over the values.
  constexpr static int32_t kBlocked = -1;  // The thread is blocked on futex_.
  constexpr static int32_t kRunning = 0;
  constexpr static int32_t kWakeUpQueued = 1;

  // A new FutexDomainThread will always immediately begin execution of
  // 'first_schedule'.  This allows work to be specified without synchronizing
  // on thread creation.
  //
  // REQUIRES: first_schedulable != nullptr
  FutexDomainThread(FutexDomain* domain, Schedulable* first_schedulable)
      : CommonFiberDomainThread(domain,
                                absl::StrCat(domain->name(), "-SDomainT"),
                                first_schedulable) {}

  std::atomic<int32_t>* futex() { return &futex_; }

 private:
  // FutexDomainThreads are self-deleting.
  ~FutexDomainThread() override = default;

  void WorkLoop() override {
    // We are initially only reclaiming from users who have raised the default
    // stack size to accommodate larger execution.  We expect to expand this
    // coverage to all fibers (and other re-used threads) over time.
    ::thread::internal::StackReclaimer reclaimer;

    Schedulable *prev, *next;
    do {
      prev = RunOneSchedulable();
      next = Downcalls::DomainObservedBlocking(prev);

      if (reclaim_active_) reclaimer.ReduceMemoryUsage();
    } while (WaitForNextWorkItem<FutexDomain>(prev, next));
  }

  // Note: futex_ should be read/modified with memory_order_acquire/release
  // as appropriate because one worker thread may change next_schedulable_
  // of another thread and then resume or swap into it, and this can happen
  // all in the userspace bypassing syscalls, so next_schedulable_ should be
  // "protected" by operations on futex_.
  std::atomic<int32_t> futex_{kRunning};

  friend class FutexDomain;
};

//------------------------------------------------------------------------------
// FutexDomain
//------------------------------------------------------------------------------

FutexDomain::FutexDomain(absl::string_view name_prefix, int max_concurrency)
    : CommonFiberDomain(name_prefix, max_concurrency) {
  ABSL_RAW_CHECK(FutexDomainAvailable(), "FutexDomainAvailable() == false");
  MarkFullyConstructed();
}

void FutexDomain::StartNewThread(Schedulable* schedulable) {
  FutexDomainThread* thread;
  thread = new FutexDomainThread(this, schedulable);
  thread->Start();
}

void FutexDomain::CoordinateBlockingTimeout(Schedulable* current) {
  Schedulable* to_run = Downcalls::DomainObservedTimeout(current);
  SwapOrBlockCurrent(current, to_run, KernelTimeout::Never());
}

// static
static void ValidateUnblockingInvariants(Schedulable* current,
                                         FutexDomainThread* curr) {
#ifndef NDEBUG
  ABSL_RAW_DCHECK(
      !current || current->runnable_count.load(std::memory_order_relaxed) >= 0,
      "not runnable");
  ABSL_RAW_DCHECK(curr->futex()->load(std::memory_order_relaxed) >
                      FutexDomainThread::kBlocked,
                  "blocked on futex");
#endif
}

bool FutexDomain::SwapCurrent(Schedulable* current, Schedulable* next,
                              KernelTimeout timeout) {
  ABSL_RAW_CHECK(current != next, "current == next");

  FutexDomainThread* n = RunSchedulableOnThread(next);
  if (n == nullptr) {
    // "next" already running.
    return BlockCurrent(current, timeout);
  }

  FutexDomainThread* c = ThreadFromSchedulable(current);

  bool resumed = RawSwap(c, n, timeout);
  if (!resumed) {
    CoordinateBlockingTimeout(current);
  }
  ValidateUnblockingInvariants(current, c);
  return resumed;
}

bool FutexDomain::BlockCurrent(Schedulable* current, KernelTimeout timeout) {
  FutexDomainThread* c = ThreadFromSchedulable(current);
  bool resumed = RawBlock(c, timeout);
  if (!resumed) {
    CoordinateBlockingTimeout(current);
  }
  ValidateUnblockingInvariants(current, c);
  return resumed;
}

// Returns false iff timeout was set and it expired.
inline bool HandleFirstWakeup(FutexDomainThread* curr, int ret,
                              KernelTimeout abs_timeout) {
  FUTEX_DOMAIN_TRACE("HandleFirstWakeup %p: %d", curr, ret);
  // Waiting on a futex thread can wake up due to:
  // - remote FUTEX_WAKE (ret == 0, val != kBlocked)
  // - timeout expiring (ret == -ETIMEOUT)
  // - signal (ret == -EINTR)
  // - futex value not kBlocked (ret == -EAGAIN)
  // - spurious (ret == 0, val == kBlocked)
  //
  // We should retry on signals and spurious wakeups; we should always
  // mark ourselves as running upon return.
  while (true) {
    if (curr->futex()->load(std::memory_order_acquire) >
        FutexDomainThread::kBlocked) {
      return true;
    }

    if (ret == -ETIMEDOUT) {
      ABSL_RAW_DCHECK(abs_timeout.has_timeout(), "ETIMEOUT without a timeout");
      int32_t blocked = FutexDomainThread::kBlocked;
      // Mark this thread as running. Return true if a concurrent wakeup
      // happened.
      //
      // Note: if the compare_exchange below fails, it means that a concurrent
      // write happened after the futex load above, so this thread needs
      // to synchronize with it, thus memory_order_acquire is needed on failure.
      // In addition, before C++17, the memory order on failure was not allowed
      // to be stronger than the memory order on success, so we use
      // memory_order_acquire in both cases.
      const bool result = !curr->futex()->compare_exchange_strong(
          blocked, FutexDomainThread::kRunning, std::memory_order_acquire,
          std::memory_order_acquire);
      return result;
    }

    // Still blocked, and did not time out - this was either a spurious
    // wakeup, or an interrupt.
    if (ret != 0 && ret != -EINTR) {
      ABSL_RAW_LOG(DFATAL, "Unexpected futex wakeup result: %d", ret);
    }

    FUTEX_DOMAIN_TRACE("HandleFirstWakeup %p: calling Futex::Wait again", curr);
    // Do it again.
    ret = Futex::WaitUntil(curr->futex(), FutexDomainThread::kBlocked,
                           abs_timeout);
  }
}

void FutexDomain::ResumeAdditionalSchedulable(Schedulable* additional) {
  FutexDomainThread* t = RunSchedulableOnThread(additional);
  if (t != nullptr) {
    RawResume(t);
  }
}

void FutexDomain::DomainStartPotentiallyBlockingRegion(Schedulable* current) {
  ABSL_RAW_CHECK(Domain::DisableRescheduling(),
                 "Unexpected DomainStartPBR call: no thread identity.");
  Schedulable* to_run = Downcalls::DomainObservedBlocking(current);

  FUTEX_DOMAIN_TRACE("DomainStartPBR");
  if (to_run) {
    FutexDomainThread* run_thread = RunSchedulableOnThread(to_run);
    if (run_thread == nullptr) {
      // "to_run" already running in new thread.
      return;
    }

    if (absl::GetFlag(FLAGS_futex_domain_offload_blocking)) {
      // to_run has useful work to do; we're just going to block. If we resume
      // it, it'll run eventually but only after making its way through the
      // kernel scheduler. If we instead switchto it with a very small timeout,
      // it'll run immediately and we'll suffer the wait through kernel wakeup.
      // Since our blocking is less important than their work, this is better.
      FutexDomainThread* c = ThreadFromSchedulable(current);

      // TODO: remove the 500ns timeout if/when we switch
      // to absolute timeouts.
      absl::Time timo = absl::Now() + absl::Nanoseconds(500);
      bool resumed = RawSwap(c, run_thread, KernelTimeout(timo));
      ABSL_RAW_DCHECK(!resumed, "Timeout expected");
    } else {
      ResumeAdditionalSchedulable(to_run);
    }
  }
}

inline void FutexDomain::RawResume(FutexDomainThread* next) {
  const int32_t prev = next->futex()->fetch_add(1, std::memory_order_release);
  ABSL_RAW_CHECK(prev < FutexDomainThread::kWakeUpQueued,
                 "more than one wakeup queued");
  if (prev == FutexDomainThread::kBlocked) {
    FUTEX_DOMAIN_TRACE("RawResume %p: doing Futex::Wake", next);
    Futex::Wake(next->futex(), 1);
  } else {
    FUTEX_DOMAIN_TRACE("RawResume %p: skipping", next);
  }
}

// Returns false iff timeout was set and it expired.
inline bool FutexDomain::RawSwap(FutexDomainThread* curr,
                                 FutexDomainThread* next,
                                 KernelTimeout abs_timeout) {
  ABSL_RAW_CHECK(curr != next, "curr == next");

  // Note that the order of steps here is important, as we change futexes.

  // Step 1: check if next is already running.
  const int32_t prev_of_next =
      next->futex()->fetch_add(1, std::memory_order_release);
  ABSL_RAW_CHECK(prev_of_next < FutexDomainThread::kWakeUpQueued,
                 "more than one wakeup queued");
  if (prev_of_next == FutexDomainThread::kRunning) {
    FUTEX_DOMAIN_TRACE("RawSwap %p => %p: next already running", curr, next);
    return RawBlock(curr, abs_timeout);
  }

  // Step 2: check if curr has a wakeup queued.
  const int32_t prev_of_curr =
      curr->futex()->fetch_sub(1, std::memory_order_acquire);
  ABSL_RAW_CHECK(prev_of_curr > FutexDomainThread::kBlocked,
                 "double-blocked??");
  if (prev_of_curr == FutexDomainThread::kWakeUpQueued) {
    FUTEX_DOMAIN_TRACE("RawSwap %p => %p: wake queued", curr, next);
    // next->futex() has been incremented, so instead of RawResume we do
    // a direct futex wakeup.
    Futex::Wake(next->futex(), 1);
    return true;
  }

  // Step 3: do the Swap.
  FUTEX_DOMAIN_TRACE("RawSwap %p => %p %p", curr, next, abs_timeout);
  int ret;
  if (!abs_timeout.has_timeout()) {
    ret = Futex::Swap(curr->futex(), FutexDomainThread::kBlocked, nullptr,
                      next->futex());
  } else {
    // TODO: use absolute timeouts if/when available.
    const absl::Time tout =
        absl::TimeFromTimespec(abs_timeout.MakeAbsTimespec());
    const absl::Time now = absl::Now();
    if (tout <= now) {
      // sys_futex returns -EINVAL if rel timeout is negative, so we bypass
      // this situation.
      FUTEX_DOMAIN_TRACE("RawSwap %p => %p: timeout expired", curr, next);
      // next->futex() has been incremented, so instead of RawResume we do
      // a direct futex wakeup.
      Futex::Wake(next->futex(), 1);

      // The current "block" was "consumed" by the timeout.
      curr->futex()->fetch_add(1, std::memory_order_relaxed);
      return false;  // timeout expired
    }

    // TODO: use absolute timeouts if/when available.
    timespec ts;
    ts = absl::ToTimespec(tout - now);
    ret = Futex::Swap(curr->futex(), FutexDomainThread::kBlocked, &ts,
                      next->futex());
  }
  return HandleFirstWakeup(curr, ret, abs_timeout);
}

// Returns false iff timeout was set and it expired.
inline bool FutexDomain::RawBlock(FutexDomainThread* curr,
                                  KernelTimeout abs_timeout) {
  const int32_t prev = curr->futex()->fetch_sub(1, std::memory_order_acquire);
  ABSL_RAW_CHECK(prev > FutexDomainThread::kBlocked, "double-blocked??");
  if (prev == FutexDomainThread::kWakeUpQueued) {
    FUTEX_DOMAIN_TRACE("RawBlock %p: skipping", curr);
    return true;
  }

  const int ret =
      Futex::WaitUntil(curr->futex(), FutexDomainThread::kBlocked, abs_timeout);
  return HandleFirstWakeup(curr, ret, abs_timeout);
}

}  // namespace

base::scheduling::Domain* NewFutexDomain(absl::string_view name_prefix,
                                         int max_concurrency) {
  // We create threads and callbacks that are not tied to the current
  // request.
  perftools::tracing::BlankTraceSpan blank_span;
  return new FutexDomain(name_prefix, max_concurrency);
}

bool FutexDomainAvailable() {
  return true;  // Always available on Linux.
}

}  // namespace thread
