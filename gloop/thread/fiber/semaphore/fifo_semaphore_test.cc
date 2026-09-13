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

// Tests for fiber-friendly FIFO semaphore.

#include "gloop/thread/fiber/semaphore/fifo_semaphore.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "gloop/thread/fiber/bundle.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/thread/fiber/semaphore/ordered_semaphore.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace thread {

using absl_testing::IsOk;
using absl_testing::StatusIs;

class FifoSemaphoreTest : public ::testing::Test {
 public:
  typedef std::vector<std::unique_ptr<Fiber>> FiberVec;
  typedef std::vector<std::unique_ptr<FifoSemaphore>> SemaphoreVec;

 protected:
  FifoSemaphoreTest() {}

  ~FifoSemaphoreTest() override { Cleanup(); }

  // Cleans up fibers at the end of a test.  Sometimes used prior to
  // test destruction, to ensure that fibers no longer hold references to
  // test-local data.
  void Cleanup() {
    for (const auto& f : fibers_) {
      f->Cancel();
      f->Join();
    }
    fibers_.clear();
  }

  static const internal::OrderedSemaphore* GetOrderedSem(
      const FifoSemaphore& sem) {
    return &sem.sem_;
  }

  // Acquires a random amount from a semaphore (within the bounds of its
  // capacity), then releases it in one or more chunks, pausing briefly
  // in-between.
  void AcquireAndPiecemealRelease(FifoSemaphore* sem, absl::BitGen& local_rng) {
    // Acquire some random (non-zero) amount from the semaphore.
    int32_t amount = absl::Uniform<int32_t>(
                         local_rng, 0, GetOrderedSem(*sem)->capacity() - 1) +
                     1;
    if (Select({thread::OnCancel(), sem->OnAcquire(amount)}) == 0) {
      return;
    }

    int32_t released = 0;
    while (released < amount) {
      if (Fiber::Current()->Cancelled()) {
        // Bail out after releasing everything we acquired.
        sem->Release(amount - released);
        break;
      }

      // Sleep briefly, to add some timing variation.
      absl::SleepFor(
          absl::Microseconds(absl::Uniform<int32_t>(local_rng, 0, 500)));
      // Release some random amount (but don't exceed what we acquired!)
      int32_t to_release = absl::Uniform<int32_t>(local_rng, 0, amount) + 1;
      to_release = std::min(to_release, amount - released);
      sem->Release(to_release);
      released += to_release;
    }
  }

  // Continually picks a random semaphore, then acquires and releases some
  // portion of its value.
  void RepeatedAcquireAndPiecemealRelease(const SemaphoreVec* sems) {
    CHECK_GT(sems->size(), 0);
    absl::BitGen local_rng;
    while (!Fiber::Current()->Cancelled()) {
      // Pick a semaphore at random to operate on.
      FifoSemaphore* s =
          sems->at(absl::Uniform<int32_t>(local_rng, 0, sems->size())).get();
      AcquireAndPiecemealRelease(s, local_rng);
    }
  }

  // Spawns a fiber that will acquire and release semaphore resources until
  // it's cancelled.
  void AddAcquireAndReleaseFiber(const SemaphoreVec& sems) {
    fibers_.emplace_back(new Fiber(
        [this, &sems] { RepeatedAcquireAndPiecemealRelease(&sems); }));
  }

  // Spawns a fiber that does a single blocking Acquire() on a semaphore.
  Fiber* AddWaitingFiber(FifoSemaphore* sem, uintptr_t amount) {
    fibers_.emplace_back(
        new Fiber([sem, amount]() { WaitForResources(sem, amount); }));
    return fibers_.back().get();
  }

  // Tries to acquire resources from a semaphore, blocking until it succeeds
  // or is cancelled.
  static void WaitForResources(FifoSemaphore* sem, uintptr_t amount) {
    Select({sem->OnAcquire(amount), thread::OnCancel()});
  }

  // Waits for the specified number of fibers to block acquiring 'sem'.
  static void WaitForBlockedFibers(FifoSemaphore* sem, int nblocked) {
    GetOrderedSem(*sem)->WaitForBlockedAcquirers(nblocked);
  }

  // Fibers used during the course of a test.
  FiberVec fibers_;
  absl::BitGen rng_;
};

TEST_F(FifoSemaphoreTest, BasicFifoSemaphoreLock) {
  FifoSemaphore s(20);
  {
    FifoSemaphoreLock lock(s, 5);
    EXPECT_EQ(15, GetOrderedSem(s)->current_value());
  }
  EXPECT_EQ(20, GetOrderedSem(s)->current_value());
}

TEST_F(FifoSemaphoreTest, FifoSemaphoreLockMoves) {
  FifoSemaphore s(20);

  // Acquire outside, move to inner scope.
  {
    FifoSemaphoreLock lock(s, 5);
    {
      FifoSemaphoreLock lock2;
      lock2 = std::move(lock);
      EXPECT_EQ(15, GetOrderedSem(s)->current_value());
    }
    EXPECT_EQ(20, GetOrderedSem(s)->current_value());
  }
  EXPECT_EQ(20, GetOrderedSem(s)->current_value());

  // Acquire inside, move to outer scope to outlive the acquiring lock.
  {
    FifoSemaphoreLock lock;
    {
      FifoSemaphoreLock lock2(s, 5);
      EXPECT_EQ(15, GetOrderedSem(s)->current_value());
      std::swap(lock, lock2);
    }
    EXPECT_EQ(15, GetOrderedSem(s)->current_value());
  }
  EXPECT_EQ(20, GetOrderedSem(s)->current_value());

  // Inner and outer acquire different amounts, and we clobber one with the
  // other instead of swapping.
  {
    FifoSemaphoreLock lock(s, 3);
    {
      FifoSemaphoreLock lock2(s, 5);
      EXPECT_EQ(12, GetOrderedSem(s)->current_value());
      lock = std::move(lock2);
      EXPECT_EQ(15, GetOrderedSem(s)->current_value());
    }
    EXPECT_EQ(15, GetOrderedSem(s)->current_value());
  }
  EXPECT_EQ(20, GetOrderedSem(s)->current_value());
}

TEST_F(FifoSemaphoreTest, FifoSemaphoreLockFactoryWorks) {
  FifoSemaphore s(20);
  {
    absl::StatusOr<FifoSemaphoreLock> l =
        FifoSemaphoreLock::MakeFifoSemaphoreLockWithDeadline(
            &s, 10, absl::InfiniteFuture());

    ASSERT_THAT(l, IsOk());

    EXPECT_EQ(10, GetOrderedSem(s)->current_value());
  }
  EXPECT_EQ(20, GetOrderedSem(s)->current_value());
}

TEST_F(FifoSemaphoreTest, FifoSemaphoreLockFactoryInvalidArgument) {
  FifoSemaphore s(20);
  EXPECT_THAT(FifoSemaphoreLock::MakeFifoSemaphoreLockWithDeadline(
                  nullptr, 10, absl::InfiniteFuture()),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(FifoSemaphoreLock::MakeFifoSemaphoreLockWithDeadline(
                  &s, 0, absl::InfiniteFuture()),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(FifoSemaphoreTest, FifoSemaphoreLockFactoryDeadlineWorks) {
  FifoSemaphore s(10);
  s.Acquire(10);
  EXPECT_THAT(FifoSemaphoreLock::MakeFifoSemaphoreLockWithDeadline(
                  &s, 10, absl::Now() + absl::Microseconds(10)),
              StatusIs(absl::StatusCode::kDeadlineExceeded));
  s.Release(10);
}

TEST_F(FifoSemaphoreTest, FifoSemaphoreLockFactoryCancelWorks) {
  FifoSemaphore s(10);
  s.Acquire(10);

  thread::Fiber f([&s] {
    EXPECT_THAT(FifoSemaphoreLock::MakeFifoSemaphoreLockWithDeadline(
                    &s, 10, absl::InfiniteFuture()),
                StatusIs(absl::StatusCode::kCancelled));
  });
  absl::SleepFor(absl::Microseconds(10));
  f.Cancel();
  f.Join();
  s.Release(10);
}

TEST_F(FifoSemaphoreTest, FifoSemaphoreLockFactoryWithPastDeadlineWorks) {
  FifoSemaphore s(10);
  {
    // Should prioritize acquisition case over deadline case.
    absl::StatusOr<FifoSemaphoreLock> l =
        FifoSemaphoreLock::MakeFifoSemaphoreLockWithDeadline(
            &s, 10, absl::InfinitePast());

    ASSERT_THAT(l, IsOk());

    EXPECT_EQ(0, GetOrderedSem(s)->current_value());
  }

  // No capacity, but should return due to deadline in the past.
  s.Acquire(10);
  EXPECT_THAT(FifoSemaphoreLock::MakeFifoSemaphoreLockWithDeadline(
                  &s, 10, absl::InfinitePast()),
              StatusIs(absl::StatusCode::kDeadlineExceeded));
  s.Release(10);
}

TEST_F(FifoSemaphoreTest, BasicFifoSemaphoreMutexLock) {
  FifoSemaphore s(20);
  {
    FifoSemaphoreMutexLock l(s);
    EXPECT_EQ(0, GetOrderedSem(s)->current_value());
  }
  EXPECT_EQ(20, GetOrderedSem(s)->current_value());
}

TEST_F(FifoSemaphoreTest, BasicAcquireAndRelease) {
  FifoSemaphore s(20);
  EXPECT_EQ(20, GetOrderedSem(s)->current_value());
  s.Acquire(5);
  s.Acquire(5);
  EXPECT_EQ(10, GetOrderedSem(s)->current_value());
  s.Release(5);
  Select({s.OnAcquire(3)});
  s.Release(3);
  EXPECT_EQ(15, GetOrderedSem(s)->current_value());
  s.Release(5);
}

TEST_F(FifoSemaphoreTest, BlockingAcquireWorks) {
  FifoSemaphore s(10);
  s.Acquire(5);

  Fiber* f = AddWaitingFiber(&s, 8);

  // The fiber should block due to insufficient resources.
  WaitForBlockedFibers(&s, 1);

  // Release some resources, but not quite enough -- fiber should stay blocked.
  s.Release(1);
  EXPECT_EQ(6, GetOrderedSem(s)->current_value());
  WaitForBlockedFibers(&s, 1);

  // Now release enough to unblock it.
  s.Release(2);
  WaitForBlockedFibers(&s, 0);
  f->Join();
  EXPECT_EQ(0, GetOrderedSem(s)->current_value());

  s.Release(10);
}

TEST_F(FifoSemaphoreTest, AcquireIsFifo) {
  FifoSemaphore s(10);
  s.Acquire(5);

  // Fiber 1: blocked due to insufficient resources.
  Fiber* f1 = AddWaitingFiber(&s, 6);
  WaitForBlockedFibers(&s, 1);

  // Fiber 2: would succeed, but blocked behind fiber 1.
  Fiber* f2 = AddWaitingFiber(&s, 2);
  WaitForBlockedFibers(&s, 2);

  // Fiber 3: would also succeed, but blocked behind 1 & 2.
  Fiber* f3 = AddWaitingFiber(&s, 2);
  WaitForBlockedFibers(&s, 3);

  EXPECT_EQ(5, GetOrderedSem(s)->current_value());
  // Enough to unblock 1 & 2, but not 3.
  s.Release(4);
  f1->Join();
  f2->Join();
  EXPECT_EQ(1, GetOrderedSem(s)->current_value());
  EXPECT_EQ(1, GetOrderedSem(s)->WaiterCount());

  // Now unblock 3.
  s.Release(1);
  WaitForBlockedFibers(&s, 0);
  f3->Join();
  EXPECT_EQ(0, GetOrderedSem(s)->current_value());

  s.Release(10);
}

TEST_F(FifoSemaphoreTest, UnselectedAcquisitionIsNoOp) {
  FifoSemaphore s(10);
  s.Acquire(5);

  // Spawn a fiber that blocks due to insufficient resources.
  Fiber* f = AddWaitingFiber(&s, 8);
  WaitForBlockedFibers(&s, 1);

  // Cancel the fiber; the semaphore should be unchanged.
  f->Cancel();
  WaitForBlockedFibers(&s, 0);
  f->Join();
  EXPECT_EQ(5, GetOrderedSem(s)->current_value());

  s.Release(5);
}

TEST_F(FifoSemaphoreTest, AcquireWithUnselectedBlocker) {
  FifoSemaphore s(10);
  s.Acquire(5);

  // Fiber 1: blocked due to insufficient resources.
  Fiber* f1 = AddWaitingFiber(&s, 6);
  WaitForBlockedFibers(&s, 1);

  // Fiber 2: would succeed, but blocked behind fiber 1.
  Fiber* f2 = AddWaitingFiber(&s, 2);
  WaitForBlockedFibers(&s, 2);

  // Cancel fiber 1; fiber 2 should now proceed.
  f1->Cancel();
  WaitForBlockedFibers(&s, 0);
  f1->Join();
  f2->Join();
  EXPECT_EQ(3, GetOrderedSem(s)->current_value());

  s.Release(7);
}

TEST_F(FifoSemaphoreTest, TryAcquireSucceedsWhenUncontended) {
  FifoSemaphore s(10);
  EXPECT_TRUE(s.TryAcquire(4));
  EXPECT_EQ(6, GetOrderedSem(s)->current_value());
  EXPECT_TRUE(s.TryAcquire(6));
  EXPECT_EQ(0, GetOrderedSem(s)->current_value());
  s.Release(10);
}

TEST_F(FifoSemaphoreTest, TryAcquireZeroAlwaysSucceeds) {
  FifoSemaphore s(10);
  EXPECT_TRUE(s.TryAcquire(0));
  EXPECT_EQ(10, GetOrderedSem(s)->current_value());

  // Even when the semaphore is fully drained, TryAcquire(0) succeeds because
  // available_ >= 0 always holds and there are no waiters.
  s.Acquire(10);
  EXPECT_TRUE(s.TryAcquire(0));
  EXPECT_EQ(0, GetOrderedSem(s)->current_value());
  s.Release(10);
}

TEST_F(FifoSemaphoreTest, TryAcquireFailsWhenInsufficient) {
  FifoSemaphore s(10);
  s.Acquire(8);
  EXPECT_EQ(2, GetOrderedSem(s)->current_value());

  // Not enough available -- TryAcquire must fail and leave the semaphore
  // unchanged.
  EXPECT_FALSE(s.TryAcquire(3));
  EXPECT_EQ(2, GetOrderedSem(s)->current_value());

  // Exactly the available amount works.
  EXPECT_TRUE(s.TryAcquire(2));
  EXPECT_EQ(0, GetOrderedSem(s)->current_value());

  s.Release(10);
}

TEST_F(FifoSemaphoreTest, TryAcquireRespectsFifoOrder) {
  FifoSemaphore s(10);
  s.Acquire(5);

  // Fiber blocks waiting for 8 (only 5 available).
  Fiber* f = AddWaitingFiber(&s, 8);
  WaitForBlockedFibers(&s, 1);
  EXPECT_EQ(5, GetOrderedSem(s)->current_value());

  // Even though we'd fit (need 1, have 5), TryAcquire must refuse so that the
  // waiter ahead of us is not starved.
  EXPECT_FALSE(s.TryAcquire(1));
  EXPECT_EQ(5, GetOrderedSem(s)->current_value());
  EXPECT_EQ(1, GetOrderedSem(s)->WaiterCount());

  // Release enough to unblock the queued waiter.
  s.Release(3);
  f->Join();
  EXPECT_EQ(0, GetOrderedSem(s)->current_value());

  // With the queue drained, TryAcquire works again.
  s.Release(2);
  EXPECT_TRUE(s.TryAcquire(2));
  EXPECT_EQ(0, GetOrderedSem(s)->current_value());

  s.Release(10);
}

TEST_F(FifoSemaphoreTest, TryAcquireDoesNotEnqueue) {
  FifoSemaphore s(10);
  s.Acquire(10);

  // A failed TryAcquire must not leave the caller queued.
  EXPECT_FALSE(s.TryAcquire(1));
  EXPECT_EQ(0, GetOrderedSem(s)->WaiterCount());

  s.Release(10);
}

TEST_F(FifoSemaphoreTest, StressTest) {
  const uintptr_t base_sem_capacity = 1000000;

  SemaphoreVec sems;
  for (int i = 0; i < 20; ++i) {
    sems.emplace_back(new FifoSemaphore(base_sem_capacity * (i + 1)));
  }

  // Start fibers acquiring & releasing various resources.
  for (int i = 0; i < 1000; ++i) {
    AddAcquireAndReleaseFiber(sems);
  }

  // Give the test some time to run, then clean up.
  absl::SleepFor(absl::Seconds(10));

  Cleanup();
}

TEST_F(FifoSemaphoreTest, LargeBitValuesWorkAsExpected) {
  const uintptr_t capacity = std::numeric_limits<uintptr_t>::max() - 10;
  FifoSemaphore s(capacity);

  // This succeeds, leaving 90 in the semaphore...
  s.Acquire(std::numeric_limits<uintptr_t>::max() - 100);

  // ...therefore this blocks:
  Fiber* f = AddWaitingFiber(&s, 100);
  WaitForBlockedFibers(&s, 1);

  // Now release enough to unblock it.
  s.Release(std::numeric_limits<uintptr_t>::max() - 100);
  WaitForBlockedFibers(&s, 0);
  f->Join();

  EXPECT_EQ(std::numeric_limits<uintptr_t>::max() - 110,
            GetOrderedSem(s)->current_value());
  s.Release(100);
}

TEST_F(FifoSemaphoreTest, WaitsUntilAllResourcesReleased) {
  FifoSemaphore s(10);
  s.Acquire(7);

  // This should block since semaphore has only 3 left.
  Fiber* f1 = AddWaitingFiber(&s, 5);
  WaitForBlockedFibers(&s, 1);

  // Call to wait for all the previously acquired resources to be released. This
  // includes waiting for the f1 fiber to acquire and release its resources.
  Fiber f2([&s]() { s.WaitUntilAllResourcesReleased(); });
  WaitForBlockedFibers(&s, 2);

  // Now release the resources, making the semaphore have its full capacity
  // again. This should trigger the second acquire, but should not unblock the
  // WaitUntilAllResourcesReleased.
  s.Release(7);
  f1->Join();
  WaitForBlockedFibers(&s, 1);

  // Finally release the resources acquired by the f1 fiber. This should unblock
  // WaitUntilAllResourcesReleased.
  s.Release(5);
  f2.Join();
  EXPECT_EQ(10, GetOrderedSem(s)->current_value());
}

// <link>
typedef FifoSemaphoreTest FifoSemaphoreDeathTest;

TEST_F(FifoSemaphoreDeathTest, DestructionWithWaiters) {
  std::unique_ptr<FifoSemaphore> s(new FifoSemaphore(10));
  s->Acquire(5);

  // Add a fiber that's blocked due to insufficient resources.
  Fiber* f = AddWaitingFiber(s.get(), 6);
  WaitForBlockedFibers(s.get(), 1);

  // Destroying the semaphore while f is still waiting should CHECK-fail.
  ASSERT_DEATH({ s.reset(); }, "");

  // Cleanup for the normal case: cancel fiber prior to semaphore destruction.
  f->Cancel();
  f->Join();
  s->Release(5);
}

TEST_F(FifoSemaphoreDeathTest, AcquireAndReleaseOutOfBounds) {
  FifoSemaphore s(10);

  ASSERT_DEATH({ s.Acquire(11); }, "");

  s.Acquire(5);
  ASSERT_DEATH({ s.Release(6); }, "");

  s.Acquire(5);
  ASSERT_DEATH({ s.Release(11); }, "");

  s.Release(10);
}

// Tests asymmetric semaphore actions: a single Release() that allows numerous
// Acquire()s to proceed, or numerous Release()s that allow a single Acquire()
// to proceed.  There are a variable number of worker fibers, but they perform
// concurrent actions on the same semaphore.
static void AsymmetricSemaphoreTest(benchmark::State& state, bool direction) {
  struct WorkerInfo {
    // The semaphore being tested.
    FifoSemaphore* sem;
    // Number of times to acquire/release 'sem' per iteration.
    int nrepetitions;
    // Number of acquire/release iterations to perform.
    int iters;
    // Direction of test: acquire vs. release 'sem'.
    bool acquire;
    // Used to instruct the workers to start an iteration.  This prevents
    // races between different iterations of different fibers.
    FifoSemaphore* start;
    // Used to indicate that the worker has completed an iteration.
    FifoSemaphore* finished;
  };

  struct Worker {
    // For each work item read from the channel, performs the requested
    // semaphore actions (acquire/release) repeatedly, then signals the main
    // fiber when complete.
    static void RepeatedlyDoSemActions(const WorkerInfo* work) {
      for (int i = 0; i < work->iters; ++i) {
        // Wait until it's OK to proceed.  Note that this fiber may "get
        // ahead" of others, but since it consumes one of these entries,
        // the total number of repetitions will be correct.
        work->start->Acquire(1);

        // Perform successive acquisitions on the semaphore under test.
        for (int j = 0; j < work->nrepetitions; ++j) {
          if (work->acquire) {
            work->sem->Acquire(1);
          } else {
            work->sem->Release(1);
          }
        }
        // Let the main fiber know that we're done with this execution.
        work->finished->Release(work->nrepetitions);
      }
    }
  };

  int iters = state.max_iterations;
  int capacity = state.range(0);
  int nfibers = state.range(1);
  const int reps_per_worker = capacity / nfibers;
  CHECK_EQ(0, capacity % nfibers);

  state.SetItemsProcessed(static_cast<intptr_t>(capacity) * iters);

  // The semaphore being benchmarked.
  FifoSemaphore test_sem(capacity);
  test_sem.Acquire(capacity);
  // Used to start each chunk of work.
  FifoSemaphore start_sem(nfibers);
  start_sem.Acquire(nfibers);
  // Used to signal the completion of each chunk of work.
  FifoSemaphore finished(capacity);
  finished.Acquire(capacity);

  // Initialize worker info (identical for all worker fibers).
  WorkerInfo work;
  work.sem = &test_sem;
  work.start = &start_sem;
  work.finished = &finished;
  work.acquire = direction;
  work.nrepetitions = reps_per_worker;
  work.iters = iters;

  // Initialization: spawn child fibers (which will all be blocked at first).
  // TODO: use a non-default scheduler to get truly concurrent
  // hardware execution
  auto fiber_cb = absl::bind_front(&Worker::RepeatedlyDoSemActions, &work);
  Bundle fibers;
  for (int i = 0; i < nfibers; ++i) {
    fibers.Add(fiber_cb);
  }

  // Then repeatedly:
  for (auto s : state) {
    // Allow worker fibers to proceed...
    start_sem.Release(nfibers);

    // Give them work to do...
    if (direction) {
      test_sem.Release(capacity);
    } else {
      test_sem.Acquire(capacity);
    }

    // And wait until it's all finished.
    finished.Acquire(capacity);
  }

  finished.Release(capacity);
  test_sem.Release(capacity);
  start_sem.Release(nfibers);

  // Wait for children to terminate.
  fibers.JoinAll();
}

// Many acquisitions are triggered by a single release.
static void BM_AsymmetricAcquire(benchmark::State& state) {
  AsymmetricSemaphoreTest(state, true);
}
BENCHMARK(BM_AsymmetricAcquire)
    ->ArgPair(1 << 10, 1)
    ->ArgPair(1 << 10, 8)
    ->ArgPair(1 << 10, 1 << 10)
    ->ArgPair(1 << 14, 1)
    ->ArgPair(1 << 14, 8)
    ->ArgPair(1 << 18, 1)
    ->ArgPair(1 << 18, 8);

// Many releases trigger a single acquisition.
static void BM_AsymmetricRelease(benchmark::State& state) {
  AsymmetricSemaphoreTest(state, false);
}
BENCHMARK(BM_AsymmetricRelease)
    ->ArgPair(1 << 10, 1)
    ->ArgPair(1 << 10, 8)
    ->ArgPair(1 << 10, 1 << 10)
    ->ArgPair(1 << 14, 1)
    ->ArgPair(1 << 14, 8)
    ->ArgPair(1 << 18, 1)
    ->ArgPair(1 << 18, 8);

}  // namespace thread
