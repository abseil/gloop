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

#include "gloop/thread/fiber/scheduler-types.h"

#ifdef __linux__
#include <linux/futex.h>
#include <syscall.h>
#endif

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <ctime>
#include <limits>

#include "absl/base/internal/cycleclock.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/spinlock_wait.h"
#include "absl/base/optimization.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/util/atomic_danger/atomic_danger.h"

namespace thread {
namespace internal {

FixedSlotStack::FixedSlotStack(int max_size) : stack_(max_size) {
  height_.store(0, std::memory_order_relaxed);
  for (int i = 0; i < max_size; i++) {  // FixedArray does not zero-init.
    stack_[i].store(0, std::memory_order_relaxed);
  }
}

FixedSlotStack::~FixedSlotStack() {
  ABSL_RAW_CHECK(height_.load(std::memory_order_relaxed) == 0,
                 "stack not empty");
}

void FixedSlotStack::Push(base::scheduling::Slot slot) {
  int idx = height_.fetch_add(1, std::memory_order_relaxed);
  idx = (idx + stack_.size()) % stack_.size();  // Handle wrap.
  // We use CompareAndSwap to handle the case where:
  //   Thread A: Push() reserved slot.
  //   Thread B: Pop() reserved a value from the slot.
  //   Thread C: Push() needs to insert into same slot.
  // Here, one of A or C will succeed.  The other will queue behind B.
  while (atomic_danger::CompareAndSwap(&stack_[idx], 0, slot.AsWord(),
                                       std::memory_order_release)) {
  }
}

base::scheduling::Slot FixedSlotStack::Pop() {
  int idx = height_.fetch_sub(1, std::memory_order_relaxed) - 1;
  idx = (idx + stack_.size()) % stack_.size();  // Handle wrap.

  intptr_t word;
  do {
    word = stack_[idx].exchange(0, std::memory_order_seq_cst);
    // Guaranteed to eventually exist.
  } while (ABSL_PREDICT_FALSE(word == 0));

  return base::scheduling::Slot::FromWord(word);
}

struct CombinerLockWaitBlock {
  CombinerLockWaitBlock* prev;  // Points to the block of the previous thread to
                                // arrive, or 0.
  std::atomic<int32_t> done;    // Set to 1 if "result = f(arg)" has executed.
  int64_t wait_start;
  CombinerLock::CombinableFunction f = nullptr;  // function to run
  void* arg;                                     // argument to function
  intptr_t result;  // Return value of f(arg) when done == 1
};

enum { kLock = 0x1 };  // Lock is held when set on CombinerLock->queue_.
CombinerLock::CombinerLock() {
  queue_.store(0, std::memory_order_relaxed);
  waiters_.store(0, std::memory_order_relaxed);
}

CombinerLock::~CombinerLock() {
  ABSL_RAW_CHECK(queue_.load(std::memory_order_relaxed) == 0,
                 "queue not empty");
}

// Wait() & WakeWaiters() are used to synchronize the lock-holder with threads
// that have waited too long for the lock-holder to complete their remote
// execution.
//
// A generational scheme, stored in waiters_ is used.
// The 1-bit represents whether a waiter exists in the current generation.
// The remaining bits form a sequence number that is increased every time
// WakeWaiters() is called.
//
// Each call to WakeWaiters interrupts all prior calls to Wait, either via:
// (a) Futex value mismatch (generation increase)
// (b) Explicit wake
static const int32_t kWaiter = 1;
static const int32_t kNewGeneration = 2;

namespace {
// This is a temporary fork of base::subtle::SpinLockDelay, with the distinction
// that this implementation does not permit "deep sleep" (i.e. sleep without a
// timeout).
//
// This fork is used to work around a known bug in CombinerLock which can
// cause missed wakeups. Since the version here doesn't permit deep sleep,
// we'll be sure to eventually wake up even if we missed the explicit wakeup.
//
// TODO: Remove this as part of cl/345764333 which fixes the underlying
// missed-wakeup bug.
void SpinLockWaitWithBoundedSleep(std::atomic<uint32_t>* w, uint32_t value,
                                  int loop) {
  int save_errno = errno;
  struct timespec tm;

  tm.tv_sec = 0;
  tm.tv_nsec = absl::base_internal::SpinLockSuggestedDelayNS(loop);
#ifdef __linux__
  syscall(SYS_futex, w, FUTEX_WAIT_PRIVATE, value, &tm);
#else
  nanosleep(&tm, nullptr);
#endif
  errno = save_errno;
}
}  // namespace

// Waits for an arbitrary (but finite) amount of time.  May be interrupted by
// WakeWaiters().
void CombinerLock::Wait(std::atomic<int32_t>* done) {
  uint32_t v = waiters_.load(std::memory_order_acquire);
  if ((v & kWaiter) == kWaiter || waiters_.compare_exchange_strong(
                                      v, v | kWaiter, std::memory_order_acquire,
                                      std::memory_order_relaxed)) {
    // After advertising ourselves as a waiter, we re-read done.  This avoids
    // the edge case where we observe the start of a generation following the
    // one we were executed in.
    if (done->load(std::memory_order_relaxed) == 1) {
      return;
    }
    // We're now guaranteed that either the generation will advance before we
    // sleep or, or we'll be explicitly woken via SpinLockWake (when supported).
    SpinLockWaitWithBoundedSleep(&waiters_, v | kWaiter, 1);
  }
}

// Interrupts any previous call to Wait().  Returns true if a futex wake is
// needed.
bool CombinerLock::WakeWaiters() {
  uint32_t v;
  do {
    v = waiters_.load(std::memory_order_acquire);
  } while (!waiters_.compare_exchange_weak(v, (v & ~kWaiter) + kNewGeneration,
                                           std::memory_order_release,
                                           std::memory_order_relaxed));
  return (v & kWaiter);  // Did waiter(s) exist in the previous generation?
}

intptr_t CombinerLock::ExecuteLocked(CombinableFunction f, void* arg) {
  CombinerLockWaitBlock wait_block = {};
  CombinerLockWaitBlock* p;
  bool queued = false;

  // First, acquire the lock, or perhaps exit with our critical section
  // executed.
  unsigned int loop = 0;
  for (;;) {
    intptr_t queue_value = this->queue_.load(std::memory_order_relaxed);
    if ((queue_value & kLock) != 0) {
      // lock already held
      if (!queued) {  // Not yet queued; queue ourselves.
        if (wait_block.f == nullptr) {
          wait_block.f = f;
          wait_block.arg = arg;
          wait_block.done.store(0, std::memory_order_relaxed);
          wait_block.wait_start = absl::base_internal::CycleClock::Now();
        }

        wait_block.prev =
            reinterpret_cast<CombinerLockWaitBlock*>(queue_value & ~kLock);
        queued = (atomic_danger::CompareAndSwap(
                      &this->queue_, queue_value,
                      reinterpret_cast<intptr_t>(&wait_block) | kLock,
                      std::memory_order_release) == queue_value);
      }
      if (queued) {
        if (loop++ > 10000) {
          Wait(&wait_block.done);
        }
        if (wait_block.done.load(std::memory_order_acquire) == 1) {
          return wait_block.result;  // Thread's region is done.
        }
        // This thread has a chance to acquire.
      }
    } else if (atomic_danger::CompareAndSwap(&this->queue_, queue_value, kLock,
                                             std::memory_order_acquire) ==
               queue_value) {
      p = reinterpret_cast<CombinerLockWaitBlock*>(queue_value);
      break;  // This thread just acquired the lock.
    }
  }

  // This thread now holds the lock, and p is a list of waiters, which included
  // this thread if queued==true.  Run their critical sections.  We will refresh
  // the list up to "tries" times so that waiters that arrived while we were
  // processing other critical sections may be accumulated.
  int tries = 2, complete = 0;
  // Using unsigned because the summation may overflow. However, the eventual
  // result `total_wait_cycles` will still be correct even in case of overflow.
  [[maybe_unused]]
  uint64_t total_wait_start = 0;
  int64_t min_wait_start = std::numeric_limits<int64_t>::max();
  while (p != nullptr) {
    CombinerLockWaitBlock* prev = p->prev;
    p->result = (*p->f)(p->arg);
    total_wait_start += static_cast<uint64_t>(p->wait_start);
    min_wait_start = std::min(p->wait_start, min_wait_start);
    p->done.store(1, std::memory_order_release);
    p = prev;
    complete++;

    if (p == nullptr && complete < 16 && --tries != 0) {
      // We reached the tail; try to load potential new waiters.
      intptr_t queue_value = this->queue_.load(std::memory_order_relaxed);
      if (queue_value != kLock) {
        p = reinterpret_cast<CombinerLockWaitBlock*>(
            this->queue_.exchange(kLock, std::memory_order_seq_cst) & ~kLock);
      }
    }
  }

  if (!queued) {
    wait_block.result = (*f)(arg);  // Run this thread's critical section.
    complete++;
  }

  // Release the lock.
  intptr_t queue_value;
  do {
    queue_value = this->queue_.load(std::memory_order_relaxed);
    p = reinterpret_cast<CombinerLockWaitBlock*>(queue_value & ~kLock);
  } while (atomic_danger::CompareAndSwap(
               &this->queue_, queue_value, queue_value & ~kLock,
               std::memory_order_release) != queue_value);

  // Wake up waiters if there is, or was, work in the queue.
  if ((p != nullptr || complete > 1) && WakeWaiters()) {
    absl::base_internal::SpinLockWake(&waiters_, true);
  }

  return wait_block.result;
}

}  // namespace internal
}  // namespace thread
