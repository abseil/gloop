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

#include "gloop/concurrent/rcu/rcu.h"

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/debugging/leak_check.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/percpu.h"
#include "gloop/base/scheduling/scheduling_mode.h"
#include "gloop/base/spinlock.h"
#include "gloop/concurrent/rcu/llist.h"
#include "gloop/concurrent/rcu/pile.h"
#include "tcmalloc/internal/sysinfo.h"
#include "tcmalloc/malloc_extension.h"

// So we'd like to make run_domain_callbacks_thread a google3 thread, but
// dependency layering makes it tricky, so we use a raw pthread.  Now, our
// thread library provides an API for registering external threads (with this
// name!) but the same dependency chain means we can't #include thread.h to
// find it and link it normally (and in principle we could be running in a
// binary that doesn't link it?)  So declare it here as a weak extern symbol.
// In any reasonable binary, this will get overridden by the real definition
// and we'll be able to register our thread.
extern ABSL_ATTRIBUTE_WEAK void Thread_RegisterExternalThread(
    absl::string_view);

namespace base {
namespace rcu {

using ::base::subtle::percpu::AllocHandle;
using ::base::subtle::percpu::FreeHandle;
using ::base::subtle::percpu::Handle;
using ::base::subtle::percpu::IntFromHandle;

Domain::Domain() : destruct_(true) {
  for (int i = 0; i < kNumPhases; ++i) {
    reader_counts_[i] = AllocHandle();
    callbacks_[i].Init();
  }

  phase_handle_.store(IntFromHandle(reader_counts_[0]),
                      std::memory_order_relaxed);
  current_phase_ = cleanup_phase_ = 0;
  queued_for_run_.store(false, std::memory_order_relaxed);
  num_queues_.store(0, std::memory_order_relaxed);
}

Domain::~Domain() {
  // We don't want to do anything for global static domains because
  // we're running in atexit() at this point and one _hopes_ no one's
  // queued something which must run before exit on call_rcu.  (It's
  // almost certainly just free() traffic, which we don't care about
  // because we're tearing down the process.)  I'd like to clean up
  // properly here, to notice leaking reader locks for one thing, but
  // that leads to exit() hanging under large amounts of traffic,
  // which is bad.
  if (!destruct_) return;

  // OK, we should have no extant callbacks or readerlocks here. But
  // we might be on the run_domain_callbacks_thread list, and if so,
  // we need to wait for that thread to remove us.  This is tricky: we
  // can't just emit a Call and wait for it to be run, because we
  // might already be on their list (from a previous Call), which
  // might run our callback, while we still *also* languish on
  // <to_run>.
  //
  // So instead keep a "refcount" of the number of times we've moved
  // into/out of that thread's work list, and wait for it to nil out.

  // TODO: make this communication nicer + smarter.
  while (num_queues_.load(std::memory_order_acquire) > 0) {
    absl::SleepFor(::absl::Milliseconds(1));
  }

  {
    SpinLockHolder h(update_lock_);
    if (phase_handle_.load(std::memory_order_relaxed) != 0) {
      for (int i = 0; i < kNumPhases; ++i) {
        FreeHandle(reader_counts_[i]);
      }
    }
  }
  for (int i = 0; i < kNumPhases; ++i) {
    callbacks_[i].Destroy();
  }
}

// If you don't care about async-signal-safety, ignore this comment.
// Despite spec in header, this is async-signal-safe up to some limitations:
// - base::percpu must be async-signal-safe [1]
// - RestartableSequences::HaveFence() && base::percpu::IsFast() must be true.

// - not safe to call reentrantly, and the external synchronization
// - guaranteeing this must be signal safe
//
// - as it blocks until all previous ReadLocks terminate, must not be called
//   from a thread holding a ReadLock (even outside a signal handler)
//
// You should probably avoid a design requiring Update from signal handlers,
// but those are the limitations if you really, really need to.
//
// [1] :percpu "should" be async-signal-safe but isn't (everywhere). We're
// trying to fix it, but it requires a new glibc. There are some constrained
// cases where we know it to be safe; if you know you're running in such a case,
// you can use ReaderLock from signals (and Synchronize as additionally detailed
// above.)
void Domain::Synchronize() {
  EnableCleanup();
  // TryRunCallbacks will attempt to advance a phase if any callbacks need it.
  // Make sure we have such a callback so we'll be guaranteed to advance a phase
  // (though it doesn't need to do anything.)
  //
  // A note: one might think you could equivalently queue a Call to
  // Notification::Notify or similar and wait on it. You can't,
  // because Synchronize is guaranteed to return happen-after all
  // previous Calls, and callbacks aren't run in order! So the
  // notification might trigger while a thread is still slogging
  // through a list of callbacks.
  CallRaw(+[](void*) {}, nullptr);
  ::absl::Duration wait = ::absl::Microseconds(1);
  size_t fences;
  while (!TryRunCallbacks(&fences)) {
    absl::SleepFor(wait);
    wait = std::min(::absl::Milliseconds(2), wait * 2);
  }
}

// Returns true iff there's no more pending work, false if we couldn't
// run everything or can't be sure. <*fences> gets the number of calls
// to percpu::Fence() incurred.
bool Domain::TryRunCallbacks(size_t* fences) {
  *fences = 0;
  if (!update_lock_.try_lock()) return false;
  bool ret = TryRunCallbacksLocked(fences);
  update_lock_.unlock();
  return ret;
}

bool Domain::TryRunCallbacksLocked(size_t* fences) {
  MaybeInitStaticHandlesLocked();
  // Loop as long as we make progress--either end a phase that has
  // work to be done or cleanup a previously ended phase.
  while (TryAdvanceLocked(fences) || TryCleanupLocked(fences)) {
    // nothing
  }

  // Note that if these are equal here, this means in particular that
  // TryAdvanceLocked() didn't push forward current_phase_, which means in
  // turn there are no callbacks to run for this phase (and since we
  // don't advance cleanup_phase_ until all callbacks run, for any
  // previous.)
  return cleanup_phase_ == current_phase_;
}

bool Domain::TryAdvanceLocked(size_t* fences) {
  namespace pc = ::base::subtle::percpu;
  if (callbacks_[current_phase_].empty()) return false;  // no need
  size_t next = (current_phase_ + 1) % kNumPhases;
  if (next == cleanup_phase_) return false;  // out of unused phases
  current_phase_ = next;

  phase_handle_.store(IntFromHandle(reader_counts_[next]),
                      std::memory_order_release);
  // This fence guarantees that
  // a) no more ReadLocks will increment the old phase's handle
  // b) we will see the increments from all previous such readlocks
  // So its Sum is as high as it will ever get and will only decrease.
  // When we see the previous phase's handle sum to 0, all readers have
  // exited and we can safely run callbacks.
  ++(*fences);
  pc::Fence();

  return true;
}

int64_t Sum(Handle h) {
  int64_t total = 0;
  for (int i : Range(tcmalloc::tcmalloc_internal::NumCPUs())) {
    // use acquire to shut up TSAN about read-in-reader->write-after-synch
    // races.
    total += GetPointerAtomic(h, i)->load(std::memory_order_acquire);
  }
  return total;
}

static const bool kHasTotalStoreOrder =
#ifdef __x86_64__
    true;
#else
    false;
#endif

bool Domain::TryCleanupLocked(size_t* fences) {
  namespace pc = ::base::subtle::percpu;
  if (current_phase_ == cleanup_phase_) return false;  // no need
  Handle h = reader_counts_[cleanup_phase_];
  if (Sum(h) != 0) return false;  // still a reader out there

  // On TSO architectures, the stores to each of the per-CPU reader
  // counts act as release barriers that pair with the loads within
  // `Sum`. On weaker memory models, the per-CPU stores don't ensure
  // visibility of any earlier stores (e.g. to the per-CPU callback list).
  // So, we have to perform an asymmetric fence here to force the other
  // CPUs to publish their stores.
  //
  // TODO: The necessity of this barrier was proven out by looping
  // rcu_test for many hours. But, it would be much better if we could actually
  // reproduce this within a few minutes!
  if (!kHasTotalStoreOrder) {
    ++(*fences);
    pc::Fence();
  }

  RunCallbacksLocked(cleanup_phase_);
  cleanup_phase_ = (cleanup_phase_ + 1) % kNumPhases;
  return true;
}

void Domain::RunCallbacksLocked(size_t phase) {
  callbacks_[phase].Iterate(+[](CallData cd) { cd.func(cd.data); });
}

void Domain::MaybeInitStaticHandlesLocked() {
  // for global "linker initialized" domains we don't install handles
  // in constructor (to avoid ordering fiasco.) Do so now (the lock is
  // held, so we can't be racing with someone else doing it.)
  if (phase_handle_.load(std::memory_order_relaxed) == 0) {
    for (int i = 0; i < kNumPhases; ++i) {
      reader_counts_[i] = AllocHandle();
      callbacks_[i].Init();
    }
    // pairs with the acquire (/consume) in ReaderLock.
    phase_handle_.store(IntFromHandle(reader_counts_[0]),
                        std::memory_order_release);
    current_phase_ = cleanup_phase_ = 0;
  }
}

// We come here if ReaderLock didn't find a handle to increment
// against.  This means we used the LINKER_INITIALIZED domain
// constructor and no one had run the "real" constructor; do so now.
Token Domain::ReaderLockInit() {
  SpinLockHolder holder(update_lock_);
  MaybeInitStaticHandlesLocked();
  // We could now run the normal ReaderLock, but what the hey, we've
  // got the lock; no need.
  ::base::subtle::percpu::AtomicFetchAdd(reader_counts_[current_phase_], 1);
  return Token(reader_counts_[current_phase_]);
}

// We come here if IsFastNoInit() returns false.
Token Domain::ReaderLockSlow(int64_t handle) {
  namespace pc = ::base::subtle::percpu;

  while (true) {
    int cpu = pc::GetCurrentCpu();
    pc::Handle h = pc::HandleFromInt(handle);
    std::atomic<int64_t>* const loc = GetPointerAtomic(h, cpu);
    const int64_t old = loc->load(std::memory_order_relaxed);
    if (ABSL_PREDICT_TRUE(cpu == pc::CompareAndSwapCheck(cpu, loc, old, old + 1,
                                                         &phase_handle_,
                                                         handle))) {
      return Token(h);
    }
    handle = phase_handle_.load(std::memory_order_acquire);
  }
}

void Domain::QueueCallback(CallData cd) {
  Token t = ReaderLock();
  // We now hold a ref against an element of reader_counts_[], and
  // that phase can't end (and its callbacks won't be run) until we
  // drop it.  Sadly we have to scan the array to figure out which
  // phase that is (current_phase *can* change), but that's easy.
  // Doing it this way lets us safely ensure our callback will be run
  // at the right time, without taking any locks that would break
  // signal safety.
  int64_t phase = kNumPhases;
  for (int i = 0; i < kNumPhases; ++i) {
    if (t.h.rep == reader_counts_[i].rep) {
      phase = i;
      break;
    }
  }
  if (phase == kNumPhases) {
    ABSL_RAW_LOG(FATAL, "Could not interpret token!");
  }
  // This looks silly, but pairs with the Release_ in TryAdvanceLocked to
  // ensure that there are no data races between Add() and Iterate()
  // on the pile.  While the logic above guarantees our pile isn't
  // being iterated "now", ReaderLock doesn't (guarantee) any barriers
  // to order that.  This does.
  (void)phase_handle_.load(std::memory_order_acquire);
  callbacks_[phase].Add(cd);
  ReaderUnlock(t);
}

ABSL_CONST_INIT static LList<Domain> to_run;
// This semaphore "counts" the number of times to_run became
// non-empty; that is, we post it whenever work begins to accumulate
// (though not when we add work to an already non-empty list.)

// We then wait on it every time we run out of things to do.  Since we
// make sure to empty the list before waiting, this guarantees that
// whenever we block, we've certainly picked up all work that has been
// queued yet.
static sem_t domain_work;

void Domain::Enqueue() {
  if (queued_for_run_.load(std::memory_order_relaxed) ||
      queued_for_run_.exchange(true, std::memory_order_acquire)) {
    // already queued
    return;
  }
  num_queues_.fetch_add(1, std::memory_order_relaxed);
  if (to_run.Push(this)) {
    sem_post(&domain_work);
  }
}

void Domain::CallRaw(void (*func)(void*), void* data) {
  ABSL_RAW_CHECK(internal::CleanupEnabled(),
                 "did not call Domain::EnableCleanup");
  CallData cd = {func, data};
  QueueCallback(cd);
  Enqueue();
}

// Very simple, not thread safe rate limiter.
class LeakyBucket {
 public:
  // Attempt to rate-limit something to once per <interval>, with
  // supports for spikes of up to n at a time. Also limit the maximum
  // debt to n (to prevent total shutdown over periods of overuse.)
  LeakyBucket(int64_t n, ::absl::Duration interval)
      : n_(n), reserve_(n), interval_(interval), last_(::absl::Now()) {}

  // I would like to perform a rate-limited action. Wait until doing
  // so not violate our rate.
  void Wait() {
    while (reserve_ <= 0) {
      auto t = ::absl::Now();
      auto d = t - last_;
      if (d >= interval_) {
        reserve_ += d / interval_;
        reserve_ = std::min(n_, reserve_);
        last_ = t;
      } else {
        auto delay = interval_ - d;
        ::absl::SleepFor(delay);
      }
    }
  }
  // I performed <i> actions.  Account for this. (It may put us
  // arbitrarily in arrears. This class is not very careful about hard
  // bounds, just preventing massive overuse over a long time period.
  void Report(size_t i) {
    reserve_ -= i;
    reserve_ = std::max(-n_, reserve_);
  }

 private:
  int64_t n_;
  int64_t reserve_;
  ::absl::Duration interval_;
  ::absl::Time last_;
};

// Add any newly-queued domains to d, returning true if we found any.
bool AddNewDomains(std::vector<Domain*>* domains) {
  LList<Domain> new_domains = to_run.PopAll();
  bool any = false;
  auto i = new_domains.begin();
  while (i != new_domains.end()) {
    any = true;
    Domain* d = &*i;
    i++;
    d->queued_for_run_.store(false, std::memory_order_release);
    domains->push_back(d);
  }

  return any;
}

bool RunDomain(Domain* d) {
  static LeakyBucket* limiter = new LeakyBucket(10, ::absl::Milliseconds(5));
  bool success = false;
  // We may or many not need to Fence() this domain, but
  // it's not unlikely, so guarantee we have the right to do one
  // fence.  The bucket size means this won't impede us under low
  // traffic, and this prevents us from doing too many fences under
  // very high Call traffic.
  limiter->Wait();
  // TODO: Figure out a sane way to amortize fences over
  // multiple domains, if we need to.
  size_t fences;
  if (d->TryRunCallbacks(&fences)) {
    success = true;
    // this is a Release_AtomicIncrement (ENOFUNC)
    d->num_queues_.fetch_sub(1, std::memory_order_seq_cst);
  }
  limiter->Report(fences);

  return success;
}

void* run_domain_callbacks_thread(void*) {
  if (Thread_RegisterExternalThread != nullptr) {
    Thread_RegisterExternalThread("rcu_callback_thread");
  }
  // Changes to std::vector may cause the heap leak checker to incorrectly
  // report that the two below vectors are leaking memory (b/382768893).
  absl::LeakCheckDisabler disabler;
  std::vector<Domain*> domains;
  std::vector<Domain*> unfinished;
  while (true) {
    // We may go long periods of time without interacting with the memory
    // allocator, pinning a thread cache in the meantime.
    tcmalloc::MallocExtension::MarkThreadIdle();

    // Note: this wait succeeding doesn't guarantee we have non-empty
    // work lists, because we may have already picked it up from extra
    // AddNewDomains calls as we looped.  But it blocking *does* mean
    // we've picked up the work from every empty->nonempty transition,
    // and we wouldn't have gotten out of the below loop unless we'd
    // cleared all of them.
    sem_wait(&domain_work);

    // We will interact with the memory allocator during cleanups.
    tcmalloc::MallocExtension::MarkThreadBusy();

    while (!domains.empty() || AddNewDomains(&domains)) {
      unfinished.clear();
      for (auto d : domains) {
        if (!RunDomain(d)) unfinished.push_back(d);
      }
      unfinished.swap(domains);
      // TODO: adapt this backoff some more interesting way.
      ::absl::SleepFor(absl::Milliseconds(1));
    }
  }
}

// TODO: Ideally this thread would unconditionally be start inside of the
// module initializer.  But until <link> is enforced, it'll break
// too many fragile binaries.
ABSL_CONST_INIT static SpinLock cleanup_spinlock(
    absl::base_internal::SCHEDULE_KERNEL_ONLY);

static bool module_initializer_ran_ ABSL_GUARDED_BY(cleanup_spinlock) = false;

static std::atomic<bool> cleanup_enabled_;

namespace internal {

bool CleanupEnabled() {
  return cleanup_enabled_.load(std::memory_order_acquire);
}

}  // namespace internal

void Domain::DoEnableCleanup() {
  SpinLockHolder h(cleanup_spinlock);
  if (internal::CleanupEnabled()) return;
  sem_init(&domain_work, 0, 0);
  // pairs with CleanupEnabled (which must be "before any call to Call")
  // to guarantee visibility of sem.
  cleanup_enabled_.store(true, std::memory_order_release);
  if (!module_initializer_ran_) return;
  pthread_t thr;
  ABSL_RAW_CHECK(
      pthread_create(&thr, nullptr, run_domain_callbacks_thread, nullptr) == 0,
      "Failed to create thread.");
}

void DomainInit() {
  SpinLockHolder h(cleanup_spinlock);
  ABSL_RAW_CHECK(!module_initializer_ran_,
                 "Module initializer can only be called once.");
  module_initializer_ran_ = true;
  if (internal::CleanupEnabled()) {
    pthread_t thr;
    ABSL_RAW_CHECK(pthread_create(&thr, nullptr, run_domain_callbacks_thread,
                                  nullptr) == 0,
                   "Failed to create thread.");
  }
}

}  // namespace rcu
}  // namespace base
