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

#include "gloop/base/scheduling/domain-test.h"

#include <time.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/base/internal/raw_logging.h"
#include "absl/base/optimization.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/memory/memory.h"
#include "absl/synchronization/internal/kernel_timeout.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "gloop/base/callback.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/low-level-support.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/base/sysinfo.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/fifolifo-schedulers.h"
#include "gloop/util/functional/to_callback.h"
#include "gtest/gtest.h"

namespace base {
namespace scheduling {

using ::absl::synchronization_internal::KernelTimeout;

// Completions allow execution to be ordered between threads.
// Completions have three states { unstarted, started, finished }, they are
// always initialized to "unstarted".
class AtomicCompletion {
 public:
  AtomicCompletion() { word_.store(0, std::memory_order_relaxed); }
  ~AtomicCompletion() {
    ABSL_RAW_CHECK(word_.load(std::memory_order_relaxed) == 2, "");
  }

  // Enter "started" state.
  // REQUIRES: previous state == "unstarted"
  void Start() { word_.store(1, std::memory_order_release); }

  // Enter "finished" state.
  // REQUIRES: previous state == "started"
  void Finish() { word_.store(2, std::memory_order_release); }

  // Wait for this completion to enter "started" state.
  void WaitUntilStarted() const {
    while (word_.load(std::memory_order_acquire) < 1) {
    }
  }

  // Wait for this completion to enter "finished" state.
  void WaitUntilFinished() const {
    while (word_.load(std::memory_order_acquire) != 2) {
    }
  }

 private:
  std::atomic<intptr_t> word_;
};

//------------------------------------------------------------------------------
// DomainTestScheduler
//------------------------------------------------------------------------------
// DomainTestScheduler is a nop-scheduler.  It's only functionality is:
//  a) Acting as a root scheduler for Domain implementations under test.
//  b) Mocking out that items we explicitly run in the Domain were in fact
//     scheduled.
//  c) Asserting that we only see expected wakeups.  All wake-ups must be
//     explicitly annotated using ExpectWakeup(), prior to their occurrence.
//  d) Tracking of allocated schedulables, to ensure all terminate and are
//     released by the Domain.
//
// Beyond this a DomainTestScheduler will never return additional work on a
// wake-up or rescheduling operation.  This guarantees that we are allowed
// manual control over what runs within the domain.
//
// DomainTestScheduler objects are reference counted, in addition to the single
// base reference held by Scheduler's constructor we hold an additional
// reference for each outstanding managed schedulable.
class DomainTestScheduler : public base::scheduling::Scheduler {
 public:
  explicit DomainTestScheduler(Domain* domain);

  // Creates a new schedulable, increments num_schedulables_.
  // Unlike Scheduler::NewManagedSchedulable(), schedulables are always
  // returned in an already running state.  Takes a single additional reference.
  Schedulable* NewManagedSchedulable(Schedulable::Type type) override;

  // Express that we expect to see a single future wakeup for "schedulable".
  // REQUIRES: Only a single wake-up may be expected.
  static void ExpectWakeup(Schedulable* schedulable);

  // Returns "true" if the last wakeup previously anticipated by ExpectWakeup
  // occurred.
  static bool WakeupObserved(Schedulable* schedulable);

 private:
  // REQUIRES: num_schedulables_ == 0.  Must be called by Release().
  virtual ~DomainTestScheduler();

  // REQUIRES: ExpectWakeup(schedulable) previously called.
  Slot Wake(Schedulable* schedulable) override;

  // Decrements num_schedulables_.  Releases a reference.
  void DeleteManagedSchedulable(Schedulable* schedulable) override;

  // We never want to schedule new work; this is already being explicitly
  // managed by our test and not Downcalls.
  Schedulable* ScheduleManaged(Slot managing_slot, Schedulable* prev,
                               bool runnable) override {
    return nullptr;  // Well, that was easy.
  }

  // REQUIRES: Should never be called.
  bool StopRunning(Slot managing_slot, Schedulable* current,
                   bool runnable) override;

  Slot root_;
  std::atomic<intptr_t> num_schedulables_;
};

// DomainTestlets are small functions expressing actions against a Domain.
// They are composed to form test-cases.
class DomainTestlets {
 public:
  // Basic operation testlets.
  static void Block(Domain* domain) {
    EXPECT_TRUE(domain->BlockCurrent(Domain::CurrentThreadSchedulable(),
                                     KernelTimeout::Never()));
  }

  static void Swap(Domain* domain, Schedulable** next) {
    EXPECT_TRUE(domain->SwapCurrent(Domain::CurrentThreadSchedulable(), *next,
                                    KernelTimeout::Never()));
  }

  static void Resume(Domain* domain, Schedulable** next) {
    domain->ResumeAdditionalSchedulable(*next);
  }

  // Operations supporting timeouts.  In these, the domain is also expected to
  // call DomainObservedTimeout() which should synchronize with our scheduler.
  // We annotate this expectation.
  static void BlockTimeout(Domain* domain, absl::Time t) {
    DomainTestScheduler::ExpectWakeup(Domain::CurrentThreadSchedulable());
    // Implementation should return false when timing out.
    EXPECT_FALSE(domain->BlockCurrent(Domain::CurrentThreadSchedulable(),
                                      KernelTimeout(t)));
  }

  // Block with timeout, but don't check if the timeout has expired.
  static bool BlockTimeoutRacy(Domain* domain, absl::Time t) {
    return domain->BlockCurrent(Domain::CurrentThreadSchedulable(),
                                KernelTimeout(t));
  }

  static void SwapTimeout(Domain* domain, Schedulable** next, absl::Time t) {
    // The domain is expected to call DomainObservedTimeout, which will re-wake
    // us within Downcalls.  Annotate this expectation.
    DomainTestScheduler::ExpectWakeup(Domain::CurrentThreadSchedulable());
    // Implementation should return false when timing out.
    EXPECT_FALSE(domain->SwapCurrent(Domain::CurrentThreadSchedulable(), *next,
                                     KernelTimeout(t)));
  }
};

// TODO: This type-punning is undefined behavior. We'll get away
// with it on platforms we care about, and this is "only" test code. It can't be
// fixed yet, as other interfaces in google3 pun the manager_int1 member to
// other types.
std::atomic<int>* GetManagerInt1AsAtomic(Schedulable* schedulable) {
  return reinterpret_cast<std::atomic<int>*>(&schedulable->manager_int1);
}

// root_ has a 1:many relationship with all execution.  Stricter external
// validation could one day require this to be more robust.
DomainTestScheduler::DomainTestScheduler(Domain* domain)
    : Scheduler(domain), root_(NewManagingSlot()) {
  num_schedulables_.store(0, std::memory_order_relaxed);
}

DomainTestScheduler::~DomainTestScheduler() {
  CHECK_EQ(0, num_schedulables_.load(std::memory_order_relaxed));
  DeleteManagingSlot(root_);
}

Schedulable* DomainTestScheduler::NewManagedSchedulable(
    Schedulable::Type type) {
  Ref();
  Schedulable* result = new Schedulable(this, type);
#if BASE_SCHEDULABLE_IS_CACHELINE_ALIGNED
  ABSL_RAW_DCHECK(
      reinterpret_cast<uintptr_t>(result) % ABSL_CACHELINE_SIZE == 0,
      "Misaligned Schedulable");
#endif

  // We allocate schedulables such that they appear to have been scheduled.
  // This allows us to inject their execution into the domain directly.
  result->managing_slot = root_;
  result->manager_int1 = 0;
  result->runnable_count.store(1, std::memory_order_release);
  num_schedulables_.fetch_add(1, std::memory_order_relaxed);
  return result;
}

void DomainTestScheduler::DeleteManagedSchedulable(Schedulable* schedulable) {
  num_schedulables_.fetch_sub(1, std::memory_order_relaxed);
  delete schedulable;
  Unref();
}

void DomainTestScheduler::ExpectWakeup(Schedulable* schedulable) {
  schedulable->runnable_count.store(-1, std::memory_order_release);
  ABSL_RAW_CHECK(
      __atomic_load_n(&schedulable->manager_int1, __ATOMIC_RELAXED) != 1,
      "unmatched ExpectWakeup() vs Wake()");
  __atomic_store_n(&schedulable->manager_int1, 1, __ATOMIC_RELEASE);
}

bool DomainTestScheduler::WakeupObserved(Schedulable* schedulable) {
  return __atomic_load_n(&schedulable->manager_int1, __ATOMIC_RELAXED) == -1;
}

Slot DomainTestScheduler::Wake(Schedulable* schedulable) {
  int old_v = GetManagerInt1AsAtomic(schedulable)
                  ->fetch_sub(2, std::memory_order_relaxed);
  ABSL_RAW_CHECK(old_v - 2 == -1, "unexpected wakeup vs schedulable");
  return Slot::NullSlot();
}

bool DomainTestScheduler::StopRunning(Slot managing_slot, Schedulable* current,
                                      bool runnable) {
  ABSL_RAW_LOG(FATAL, "unexpected StopRunning()\n");  // Should never be called.
  return false;
}

//------------------------------------------------------------------------------
// DomainTest
//------------------------------------------------------------------------------

// Note: we limit concurrency to 32 because "ProvidesMaxConcurrency" test
// below is flaky when run under forge constraints on large machines.
DomainTest::DomainTest()
    : domain_(GetNewDomainFunction()("test_domain", std::min(NumCPUs(), 32))) {
  // DomainTestScheduler is not a real scheduler, so we disable rescheduling
  // for the whole domain.
  domain_->DomainTestDisableRescheduling();
  new DomainTestScheduler(domain_);
}

DomainTest::~DomainTest() {
  base::scheduling::Scheduler* scheduler = domain_->root_scheduler();
  // Ensure we can delete the domain.
  delete domain_;
  // ~DomainTestScheduler() asserts we did not leak any schedulables.
  scheduler->Orphan();
}

class SynchronizedDomainWork {
 public:
  SynchronizedDomainWork(Closure* closure, AtomicCompletion* completion)
      : closure_(closure), completion_(completion) {}

  static void Invoke(void* sync_work_ptr) {
    std::unique_ptr<SynchronizedDomainWork> sync_work = absl::WrapUnique(
        reinterpret_cast<SynchronizedDomainWork*>(sync_work_ptr));
    sync_work->completion_->Start();
    sync_work->closure_->Run();
    sync_work->completion_->Finish();
  }

 private:
  Closure* closure_;
  AtomicCompletion* completion_;
};

void DomainTest::RunClosure(Schedulable** schedulable, Closure* work,
                            AtomicCompletion* completion) {
  SynchronizedDomainWork* sync_work =
      new SynchronizedDomainWork(work, completion);
  // We need *schedulable to become visible before completion.
  *schedulable = domain_->CreateExecutableSchedulable(
      domain_->root_scheduler(), SynchronizedDomainWork::Invoke, sync_work);

  DomainTestlets::Resume(domain_, schedulable);
  // Domains are not required to handle concurrent Resumes; so we wait for it
  // to get kicked off.
  completion->WaitUntilStarted();
}

void DomainTest::CompletionHelper(AtomicCompletion* completion, Closure* work,
                                  bool sync_on_start) {
  if (sync_on_start) {
    completion->WaitUntilStarted();
  } else {
    completion->WaitUntilFinished();
  }
  work->Run();
}

Closure* DomainTest::WhenStarted(AtomicCompletion* when_started,
                                 Closure* work) {
  return ::util::functional::ToCallback(
      absl::bind_front(CompletionHelper, when_started, work, true));
}

Closure* DomainTest::WhenFinished(AtomicCompletion* when_finished,
                                  Closure* work) {
  return ::util::functional::ToCallback(
      absl::bind_front(CompletionHelper, when_finished, work, false));
}

void DomainTest::WaitUntilAllFinished(absl::Span<const AtomicCompletion> done) {
  for (int i = 0; i < done.size(); i++) {
    done[i].WaitUntilFinished();
  }
}

//------------------------------------------------------------------------------
// Actual tests follow.
//------------------------------------------------------------------------------

// First test that our interfaces are commutative.

// Resume after Block
TEST_P(DomainTest, BlockResume) {
  Schedulable* s[2];
  std::vector<AtomicCompletion> done(2);

  RunClosure(&s[0],
             ::util::functional::ToCallback(
                 absl::bind_front(DomainTestlets::Block, domain())),
             &done[0]);
  RunClosure(
      &s[1],
      WhenStarted(&done[0], ::util::functional::ToCallback(absl::bind_front(
                                DomainTestlets::Resume, domain(), &s[0]))),
      &done[1]);
  WaitUntilAllFinished(done);
}

// Resume before Block
TEST_P(DomainTest, ResumeBlock) {
  Schedulable* s[2];
  std::vector<AtomicCompletion> done(2);

  RunClosure(
      &s[0],
      WhenFinished(&done[1], ::util::functional::ToCallback(absl::bind_front(
                                 DomainTestlets::Block, domain()))),
      &done[0]);
  RunClosure(&s[1],
             ::util::functional::ToCallback(
                 absl::bind_front(DomainTestlets::Resume, domain(), &s[0])),
             &done[1]);
  WaitUntilAllFinished(done);
}

// Simultaneously, A swap to B, B swap To A
TEST_P(DomainTest, SwapSwap) {
  Schedulable* s[2];
  std::vector<AtomicCompletion> done(2);

  RunClosure(
      &s[0],
      WhenStarted(&done[1], ::util::functional::ToCallback(absl::bind_front(
                                DomainTestlets::Swap, domain(), &s[1]))),
      &done[0]);
  RunClosure(
      &s[1],
      WhenStarted(&done[0], ::util::functional::ToCallback(absl::bind_front(
                                DomainTestlets::Swap, domain(), &s[0]))),
      &done[1]);
  WaitUntilAllFinished(done);
}

// In order: A blocks, B swaps to A, C resumes B.
// [ This is: Swap after Block, Resume after Swap ]
TEST_P(DomainTest, BlockSwapResume) {
  Schedulable* s[3];
  std::vector<AtomicCompletion> done(3);

  RunClosure(&s[0],
             ::util::functional::ToCallback(
                 absl::bind_front(DomainTestlets::Block, domain())),
             &done[0]);
  RunClosure(
      &s[1],
      WhenStarted(&done[0], ::util::functional::ToCallback(absl::bind_front(
                                DomainTestlets::Swap, domain(), &s[0]))),
      &done[1]);
  RunClosure(
      &s[2],
      WhenFinished(&done[0], ::util::functional::ToCallback(absl::bind_front(
                                 DomainTestlets::Resume, domain(), &s[1]))),
      &done[2]);
  WaitUntilAllFinished(done);
}

// In order: A resumes B, B swaps to C, C blocks
// [ This is: Resume before Swap, Swap before Block ]
TEST_P(DomainTest, ResumeSwapBlock) {
  Schedulable* s[3];
  std::vector<AtomicCompletion> done(3);

  RunClosure(
      &s[0],
      WhenStarted(&done[1], ::util::functional::ToCallback(absl::bind_front(
                                DomainTestlets::Resume, domain(), &s[1]))),
      &done[0]);
  RunClosure(
      &s[1],
      WhenFinished(
          &done[0],
          WhenStarted(&done[2], ::util::functional::ToCallback(absl::bind_front(
                                    DomainTestlets::Swap, domain(), &s[2])))),
      &done[1]);
  RunClosure(
      &s[2],
      WhenFinished(&done[1], ::util::functional::ToCallback(absl::bind_front(
                                 DomainTestlets::Block, domain()))),
      &done[2]);
  WaitUntilAllFinished(done);
}

// Test operations supporting a timeout expire correctly.

// An expiring Block.
TEST_P(DomainTest, BlockTimeout) {
  Schedulable* s[2];
  std::vector<AtomicCompletion> done(2);

  static const absl::Duration kDelay = absl::Milliseconds(50);
  absl::Time start = absl::Now();

  RunClosure(&s[0],
             ::util::functional::ToCallback(absl::bind_front(
                 DomainTestlets::BlockTimeout, domain(), start + kDelay)),
             &done[0]);

  // Wait for wake-up to be delivered.
  while (!DomainTestScheduler::WakeupObserved(s[0])) {
  }
  EXPECT_LE(kDelay, absl::Now() - start);

  // s[0] needs a final resume; kick off another testlet.
  RunClosure(&s[1],
             ::util::functional::ToCallback(
                 absl::bind_front(DomainTestlets::Resume, domain(), &s[0])),
             &done[1]);
  WaitUntilAllFinished(done);
}

// A Swap into a Block.  With Swap's timeout subsequently expiring.
TEST_P(DomainTest, BlockSwapTimeout) {
  Schedulable* s[2];
  std::vector<AtomicCompletion> done(2);

  static const absl::Duration kDelay = absl::Milliseconds(50);
  absl::Time start = absl::Now();

  RunClosure(&s[0],
             ::util::functional::ToCallback(
                 absl::bind_front(DomainTestlets::Block, domain())),
             &done[0]);
  RunClosure(&s[1],
             ::util::functional::ToCallback(absl::bind_front(
                 DomainTestlets::SwapTimeout, domain(), &s[0], start + kDelay)),
             &done[1]);

  // Wait for wake-up to be delivered.
  while (!DomainTestScheduler::WakeupObserved(s[1])) {
  }
  EXPECT_LE(kDelay, absl::Now() - start);

  // s[1] needs a final resume; kick off another testlet.
  DomainTestlets::Resume(domain(), &s[1]);
  WaitUntilAllFinished(done);
}

// Swap into an expiring Block.
TEST_P(DomainTest, BlockTimeoutSwap) {
  Schedulable* s[2];
  std::vector<AtomicCompletion> done(2);

  std::atomic<bool> may_exit = false;

  static const absl::Duration kDelay = absl::Milliseconds(10);
  absl::Time start = absl::Now();

  // Block with a timeout.
  RunClosure(&s[0], ::util::functional::ToCallback([&]() {
    DomainTestlets::BlockTimeoutRacy(domain(), start + kDelay);
    while (!may_exit.load()) {
    }
  }),
             &done[0]);

  // Swap into the blocked schedulable, racing with the timeout.
  RunClosure(&s[1], WhenStarted(&done[0], ::util::functional::ToCallback([&]() {
    usleep(10000);  // NO_LINT
    DomainTestlets::Swap(domain(), &s[0]);
    may_exit.store(true);
  })),
             &done[1]);

  // Kick s[1].
  DomainTestlets::Resume(domain(), &s[1]);

  WaitUntilAllFinished(done);
}

// Test that execution is truly concurrent up to max_concurrency().
TEST_P(DomainTest, ProvidesMaxConcurrency) {
  struct Helper {
    static void RunUntilNumConcurrent(std::atomic<intptr_t>* value,
                                      int required) {
      value->fetch_add(1, std::memory_order_relaxed);

      // Spin until we reach the desired target.  If the domain short-changes us
      // on concurrency then the last threads to increment will not be admitted
      // and we will time out.
      while (value->load(std::memory_order_relaxed) != required) {
      }
    }
  };

  const int num_schedulables = domain()->max_concurrency();
  std::vector<Schedulable*> schedulables(num_schedulables);
  std::vector<AtomicCompletion> done(num_schedulables);

  std::atomic<intptr_t> count{0};
  for (int i = 0; i < num_schedulables; i++) {
    RunClosure(&schedulables[i],
               ::util::functional::ToCallback(absl::bind_front(
                   Helper::RunUntilNumConcurrent, &count, num_schedulables)),
               &done[i]);
  }
  WaitUntilAllFinished(done);
  EXPECT_EQ(num_schedulables, count.load(std::memory_order_relaxed));
}

// Finally, a stress test.  Given a chain of schedulables (each continuing their
// neighbour's execution), complete N cycles around the ring.  Uses Swaps 2/3rds
// of the time and {Resume, Block} pairs the rest.  This is technically out of
// spec[*], but compatible with current implementations.
//
// [*] It's possible that when we {Resume, Block} there are more threads
// active within the Domains than a correct root_scheduler() would allow.
TEST_P(DomainTest, StressLoop) {
  struct Helper {
    static void StressLoop(Domain* domain, Schedulable** next, bool tail,
                           int n) {
      DomainTestlets::Block(domain);
      while (n-- > 0) {
        if (n % 3 == 0) {
          DomainTestlets::Resume(domain, next);
          DomainTestlets::Block(domain);
        } else {
          DomainTestlets::Swap(domain, next);
        }
      }
      DomainTestlets::Resume(domain, next);
      if (tail) {
        DomainTestlets::Block(domain);
      }
    }
  };

  static int kNumChains = domain()->max_concurrency();
  static int kChainSize = 10;       // Avoid creating too many threads.
  static int kRepsPerChain = 2500;  // pthreads are too slow w/ larger values.

  std::vector<std::vector<Schedulable*>> chains(kNumChains);
  std::vector<AtomicCompletion> done(kNumChains * kChainSize);

  // Create kNumChains parallel cycles, each of length kChainSize.
  for (int i = 0; i < kNumChains; i++) {
    chains[i].resize(kChainSize);
    for (int j = 0; j < kChainSize; j++) {
      bool tail = j == kChainSize - 1;
      Schedulable** next_ptr = &chains[i][(j + 1) % kChainSize];
      // Final visit to tail node is terminal.
      int reps = tail ? kRepsPerChain - 1 : kRepsPerChain;
      RunClosure(&chains[i][j],
                 ::util::functional::ToCallback(absl::bind_front(
                     Helper::StressLoop, domain(), next_ptr, tail, reps)),
                 &done[(i * kChainSize) + j]);
    }
  }

  // Wait for everyone to start up.  There's a chance we have too many threads
  // running in the domain while they all spin up here, but no current
  // implementations mind this.
  for (int i = 0; i < done.size(); i++) {
    done[i].WaitUntilStarted();
  }

  // Kick off each chain's head.
  for (int i = 0; i < kNumChains; i++) {
    DomainTestlets::Resume(domain(), &chains[i][0]);
  }

  // Let things run their course.
  WaitUntilAllFinished(done);
}

}  // namespace scheduling
}  // namespace base
