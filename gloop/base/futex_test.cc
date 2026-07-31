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

#include "gloop/base/futex.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <memory>
#include <vector>

#include "absl/log/check.h"
#include "absl/synchronization/internal/kernel_timeout.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "gloop/thread/thread.h"
#include "gtest/gtest.h"

using absl::Duration;
using absl::synchronization_internal::KernelTimeout;
using base::Futex;

namespace {

TEST(FutexTest, RelTimeout) {
  std::atomic<int32_t> futex(0);
  const Duration kDur = absl::Milliseconds(25);
  const struct timespec timeout = absl::ToTimespec(kDur);
  absl::Time start = absl::Now();
  for (int i = 0; i < 5; ++i) {
    absl::Time before = absl::Now();
    ASSERT_EQ(-ETIMEDOUT, Futex::WaitRelativeTimeout(&futex, 0, &timeout));
    EXPECT_GE(absl::Now(), before + kDur);
  }
  absl::Time stop = absl::Now();
  // allow 10x slowdown from forge. We're using trivial CPU here.
  EXPECT_GE(50 * kDur, stop - start);
}

TEST(FutexTest, AbsTimeout) {
  std::atomic<int32_t> futex(0);
  const Duration kDur = absl::Milliseconds(25);
  absl::Time start = absl::Now();
  for (int i = 0; i < 5; ++i) {
    absl::Time before = absl::Now();
    absl::Time limit = before + kDur;
    const struct timespec timeout = absl::ToTimespec(limit);

    ASSERT_EQ(-ETIMEDOUT, Futex::WaitAbsoluteTimeout(&futex, 0, &timeout));
    EXPECT_GE(absl::Now(), limit);
  }
  absl::Time stop = absl::Now();
  // allow 10x slowdown from forge. We're using trivial CPU here.
  EXPECT_GE(50 * kDur, stop - start);
}

TEST(FutexTest, NegativeTimeout) {
  std::atomic<int32_t> futex(0);
  const KernelTimeout t(absl::UnixEpoch() - absl::Hours(100) -
                        absl::Seconds(1));
  EXPECT_EQ(-ETIMEDOUT, Futex::WaitUntil(&futex, 0, t));
}

TEST(FutexTest, Swap) {
  std::atomic<int32_t> futex_main{0};
  std::atomic<int32_t> futex_other{0};

  std::atomic<int> steps{0};

  ClosureThread other([&]() {
    // Start, wait for the main thread to swap into this thread.
    CHECK_EQ(0, steps.load());
    steps.store(1);
    Futex::Wait(&futex_other, 0);
    CHECK_EQ(1, futex_other.load());
    CHECK_EQ(2, steps.load());

    // Wake the main thread.
    steps.store(3);
    futex_main.store(1);
    Futex::Wake(&futex_main, 1);
  });

  other.SetJoinable(true);
  other.Start();

  // Wait for the other thread to start.
  while (1 != steps.load()) {
  }

  // Swap into the other thread.
  steps.store(2);
  futex_other.store(1);
  Futex::Swap(&futex_main, 0, nullptr, &futex_other);

  // Validate that the other thread woke and woke the main thread.
  CHECK_EQ(3, steps.load());
  CHECK_EQ(1, futex_main.load());

  other.Join();
}

// Time a context-switch using Futex::Swap. See BM_SwitchToSwitch
// in switchto_test.cc for comparison/inspiration.
void BM_FutexSwap(benchmark::State& state) {
  // We want a single context switch timed; as there are two context
  // switches per iteration, we halve state.max_iterations.
  const int iters = state.max_iterations / 2;

  constexpr int32_t kWait = 0;
  constexpr int32_t kWake = 1;

  std::atomic<int32_t> futex_main{kWait};
  std::atomic<int32_t> futex_other{kWait};

  ClosureThread other([&]() {
    Futex::Wait(&futex_other, kWait);

    for (int i = 0; i < iters; ++i) {
      futex_main.store(kWake, std::memory_order_relaxed);
      futex_other.store(kWait, std::memory_order_relaxed);
      Futex::Swap(&futex_other, kWait, nullptr, &futex_main);
    }
  });
  other.SetJoinable(true);
  other.Start();

  // Start benchmark timing, run until next KeepRunningBatch() call.
  state.KeepRunningBatch(state.max_iterations);

  for (int i = 0; i < iters; ++i) {
    futex_other.store(kWake, std::memory_order_relaxed);
    futex_main.store(kWait, std::memory_order_relaxed);
    Futex::Swap(&futex_main, kWait, nullptr, &futex_other);
  }

  CHECK(!state.KeepRunningBatch(state.max_iterations));

  futex_other.store(kWake, std::memory_order_relaxed);
  Futex::Wake(&futex_other, 1);
  other.Join();
}
BENCHMARK(BM_FutexSwap);

// See: <link>
// The following 2 benchmarks intend to expose the slowness associated with
// scaling futexes. There are two dimesnions in which futexes can be scaled --
// size of the waitqueue on a futex, and number of futexes in existences on a
// system. Internally, futexes are stored in a global hashtable, meaning that
// lookup time may increase as the number of futexes increase. Each futex has an
// associated list of threads waiting on. Obviously, one has to walk the entire
// list of threads waiting if one intends to wake them all up, but this should *
// ideally * only result in a linear runtime increase, which it does not. Ditto
// for scaling in terms of number of futexes in existence.

// Helpers for the following 2 benchmarks
struct wait_on_futex_args {
  std::atomic<int>* futex_word;
  std::atomic<int>* started_threads;
};

void* wait_on_futex(void* arg) {
  wait_on_futex_args* args = reinterpret_cast<wait_on_futex_args*>(arg);

  // We do not care about the order of these adds.
  args->started_threads->fetch_add(1, std::memory_order_relaxed);
  Futex::Wait(args->futex_word, 0);
  return nullptr;
}

std::unique_ptr<pthread_t> MakePthread(wait_on_futex_args* args) {
  auto pthread = std::make_unique<pthread_t>();
  pthread_create(pthread.get(), nullptr, wait_on_futex, args);
  return pthread;
}

void StartPthread(pthread_t* pthread) {
  // pthreads are started automatically
}

void JoinPthread(pthread_t* pthread) { pthread_join(*pthread, nullptr); }

std::unique_ptr<ClosureThread> MakeClosureThread(wait_on_futex_args* args) {
  return std::make_unique<ClosureThread>([args] { wait_on_futex(args); });
}

void StartClosureThread(ClosureThread* thread) {
  thread->SetJoinable(true);
  thread->Start();
}

void JoinClosureThread(ClosureThread* thread) { thread->Join(); }

// This benchmarks tracks performance of the futex wake syscall as the number of
// waitees increase. A list must be walked for each wake, so presumably there
// should be a linear relationship. The data shows that as N gets into the
// 1000s, and 10,000s, we see performance regress from this linear relationship.

// Using google3 style threads instead of just plain pthreads, show a much more
// drasitic drop in performance. Within google3 threads, we see a huge drop from
// ~300k wakeups/s to ~5k wakeups/s, which differs from the pthread case which
// steadies off at 150k wakeups/s. If one attaches strace, to each benchmark,
// one can see that the google3 style thread benchmarks creates many more
// futexes than the pthread benchmark, likely meaning that our layers atop
// pthreads are at least partially responsible for the delta.
template <class T, std::unique_ptr<T>(MakeFunc)(wait_on_futex_args*),
          void(StartFunc)(T*), void(JoinFunc)(T*)>
void BM_FutexWakeOneFutexNThreads(benchmark::State& state) {
  const int num_threads = state.range(0);
  int threads_woken_up = 0;

  for (auto s : state) {
    state.PauseTiming();
    std::vector<std::unique_ptr<T>> threads;

    std::atomic<int> started_threads{0};
    std::atomic<int> futex_word{0};

    wait_on_futex_args t_args;
    t_args.started_threads = &started_threads;
    t_args.futex_word = &futex_word;

    for (int i = 0; i < num_threads; i++) {
      threads.push_back(MakeFunc(&t_args));
      StartFunc(threads[i].get());
    }

    while (started_threads.load(std::memory_order_relaxed) != num_threads) {
      absl::SleepFor(absl::Seconds(1));
    }

    // Avoid the race where wake is called before wait.
    absl::SleepFor(absl::Seconds(1));

    state.ResumeTiming();
    futex_word.store(1, std::memory_order_relaxed);
    Futex::Wake(&futex_word, num_threads);
    state.PauseTiming();

    // Cleanup.
    for (int i = 0; i < num_threads; i++) {
      JoinFunc(threads[i].get());
    }
    threads_woken_up += num_threads;
    threads.clear();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(threads_woken_up);
}
BENCHMARK(BM_FutexWakeOneFutexNThreads<pthread_t, MakePthread, StartPthread,
                                       JoinPthread>)
    ->Arg(1)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000);
BENCHMARK(BM_FutexWakeOneFutexNThreads<ClosureThread, MakeClosureThread,
                                       StartClosureThread, JoinClosureThread>)
    ->Arg(1)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000);

// This benchmark scales the number of futexes we have. Futexes are hashed into
// bucket, so presumably scaling the number of futexes will cause more
// collisions. This benchmark intends to capture that behavior.
// Additionally, fibers have a futex per thread in the case of the futex domain.
// This benchmark should emulate the behavior on a machine with many threads who
// are interacting with futexes. We time how long it takes to do a single
// wake-up, while N other threads are waiting on N other unique futexes. The
// takeaway from this benchmark is as we have more futexes in existence, the
// cost to wakeup any one thread waiting on a futex becomes increasingly more
// expensive. More or less, the number of threads we can wake up per second
// grinds to a halt as we have more and more unrelated futexes.

// When using google3 threads instead of plain pthreads, we have a get basically
// comparable results. In the worst case, plain pthreads can wakeup almost 2x as
// many threads per second, but usually the delta is withi 10%.
template <class T, std::unique_ptr<T>(MakeFunc)(wait_on_futex_args*),
          void(StartFunc)(T*), void(JoinFunc)(T*)>
void BM_FutexWakeNFutexesNThreadsSingleWake(benchmark::State& state) {
  const int num_threads = state.range(0);
  int threads_woken_up = 0;

  for (auto s : state) {
    state.PauseTiming();

    std::vector<std::unique_ptr<T>> threads;
    std::atomic<int> started_threads{0};
    std::atomic<int>* futex_words = new std::atomic<int>[num_threads];

    // We can't create this wait_on_futex_args in the for loop below because
    // it'll get destructed too soon.
    wait_on_futex_args* args = new wait_on_futex_args[num_threads];

    for (int i = 0; i < num_threads; i++) {
      futex_words[i] = 0;
      args[i].started_threads = &started_threads;
      args[i].futex_word = &futex_words[i];
      threads.push_back(MakeFunc(&args[i]));
    }

    for (int i = 0; i < num_threads; i++) {
      StartFunc(threads[i].get());
    }

    while (started_threads.load(std::memory_order_relaxed) != num_threads) {
      absl::SleepFor(absl::Seconds(1));
    }

    // Avoid the race where wake is called before wait.
    absl::SleepFor(absl::Seconds(1));

    // We want to extract how long it takes to wake up a single futex -- the
    // 0th.
    state.ResumeTiming();
    futex_words[0].fetch_add(1, std::memory_order_relaxed);
    Futex::Wake(&futex_words[0], 1);
    state.PauseTiming();
    JoinFunc(threads[0].get());

    // Cleanup.
    for (int i = 1; i < num_threads; i++) {
      futex_words[i].fetch_add(1, std::memory_order_relaxed);
      Futex::Wake(&futex_words[i], 1);
      JoinFunc(threads[i].get());
    }

    threads_woken_up += 1;
    free(futex_words);
    threads.clear();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(threads_woken_up);
}
BENCHMARK(BM_FutexWakeNFutexesNThreadsSingleWake<pthread_t, MakePthread,
                                                 StartPthread, JoinPthread>)
    ->Arg(1)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000);
BENCHMARK(BM_FutexWakeNFutexesNThreadsSingleWake<
              ClosureThread, MakeClosureThread, StartClosureThread,
              JoinClosureThread>)
    ->Arg(1)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000);
}  // namespace
