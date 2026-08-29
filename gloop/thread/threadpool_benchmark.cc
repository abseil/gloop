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

// This is a benchmark for Add functionality of thread pool.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/flags/flag.h"
#include "absl/synchronization/barrier.h"
#include "absl/synchronization/blocking_counter.h"
#include "benchmark/benchmark.h"
#include "gloop/base/callback.h"
#include "gloop/base/init_google.h"
#include "gloop/base/log_file_flags.h"
#include "gloop/thread/executor.h"
#include "gloop/thread/threadpool.h"

namespace {

// ScheduleRemaining() is a helper function that tail schedules a new call
// if a given concurrent 'chain' of executions has remaining executions, and
// if not, decrements `counter` indicating the finished execution chain.
void ScheduleRemaining(thread::Executor* ex, absl::BlockingCounter* counter,
                       int remaining) {
  if (remaining > 0) {
    ex->Schedule([=] { ScheduleRemaining(ex, counter, remaining - 1); });
  } else {
    counter->DecrementCount();
  }
}

void RunBenchmark(thread::Executor* ex, int concurrent,
                  benchmark::State& state) {
  // To achieve `concurrent` invocables to run concurrently without needing
  // (expensive) synchronization primitives and counters dominating the cost
  // in benchmarks, we schedule `concurrent` initial invocables that each
  // tail schedule `per_thread` remaining executions on the thread pool.
  int per_thread = state.max_iterations / concurrent;
  while (state.KeepRunningBatch(state.max_iterations)) {
    absl::BlockingCounter counter(concurrent);
    for (int i = 0; i < concurrent; i++) {
      ex->Schedule([&] { ScheduleRemaining(ex, &counter, per_thread - 1); });
    }
    counter.Wait();
  }
  state.SetItemsProcessed(static_cast<int64_t>(concurrent) * per_thread);
}

// Benchmark a thread pool that reuses 1 closure.
void ThreadPoolHelper(int closures, benchmark::State& state, int num_threads) {
  ThreadPool pool(num_threads);
  RunBenchmark(&pool, closures, state);
}

void BM_ThreadPool1(benchmark::State& state) {
  const int num_threads = state.range(0);
  ThreadPoolHelper(1, state, num_threads);
}
BENCHMARK(BM_ThreadPool1)->Arg(1);
BENCHMARK(BM_ThreadPool1)->Arg(4);

void BM_ThreadPool4(benchmark::State& state) {
  const int num_threads = state.range(0);

  ThreadPoolHelper(4, state, num_threads);
}
BENCHMARK(BM_ThreadPool4)->Arg(1);
BENCHMARK(BM_ThreadPool4)->Arg(4);
BENCHMARK(BM_ThreadPool4)->Arg(8);

void BM_ThreadPool8(benchmark::State& state) {
  const int num_threads = state.range(0);

  ThreadPoolHelper(8, state, num_threads);
}
BENCHMARK(BM_ThreadPool8)->Arg(4);
BENCHMARK(BM_ThreadPool8)->Arg(8);
BENCHMARK(BM_ThreadPool8)->Arg(16);

void BM_ThreadPool16(benchmark::State& state) {
  const int num_threads = state.range(0);

  ThreadPoolHelper(16, state, num_threads);
}
BENCHMARK(BM_ThreadPool16)->Arg(8);
BENCHMARK(BM_ThreadPool16)->Arg(16);
BENCHMARK(BM_ThreadPool16)->Arg(32);

void BM_ThreadPool32(benchmark::State& state) {
  const int num_threads = state.range(0);

  ThreadPoolHelper(32, state, num_threads);
}
BENCHMARK(BM_ThreadPool32)->Arg(16);
BENCHMARK(BM_ThreadPool32)->Arg(32);
BENCHMARK(BM_ThreadPool32)->Arg(64);

void BM_ThreadPool128(benchmark::State& state) {
  const int num_threads = state.range(0);

  ThreadPoolHelper(128, state, num_threads);
}
BENCHMARK(BM_ThreadPool128)->Arg(64);
BENCHMARK(BM_ThreadPool128)->Arg(128);
BENCHMARK(BM_ThreadPool128)->Arg(256);

void BM_ThreadPool512(benchmark::State& state) {
  const int num_threads = state.range(0);

  ThreadPoolHelper(512, state, num_threads);
}
BENCHMARK(BM_ThreadPool512)->Arg(256);
BENCHMARK(BM_ThreadPool512)->Arg(512);
BENCHMARK(BM_ThreadPool512)->Arg(1024);

void BM_ThreadPool2048(benchmark::State& state) {
  const int num_threads = state.range(0);

  ThreadPoolHelper(2048, state, num_threads);
}
BENCHMARK(BM_ThreadPool2048)->Arg(1024);
BENCHMARK(BM_ThreadPool2048)->Arg(2048);

// Simulates workloads where many short running callbacks are added to the
// threadpool. The callbacks are not enough to keep all the workers busy
// continuously so the number of workers running changes overtime.
//
// In effect this tests how well the threadpool avoids spurious wakeups.
void BM_SpikyLoad(benchmark::State& state) {
  struct Work {
    void DoSomeWork() {
      // Simulates small but non-trivial amount of work.
      for (int i = 0; i != 1000; ++i) val++;
      counter->DecrementCount();
    }
    char pad[ABSL_CACHELINE_SIZE];
    std::unique_ptr<Closure> cb;
    volatile int val = 0;
    absl::BlockingCounter* counter = nullptr;
  };
  const int num_threads = state.range(0);
  const int kNumSpikes = 1000;
  const int batch_size = 3 * num_threads;
  std::vector<Work> work(batch_size);
  while (state.KeepRunningBatch(kNumSpikes * batch_size)) {
    ThreadPool pool(num_threads);
    for (int i = 0; i != kNumSpikes; ++i) {
      absl::BlockingCounter counter(batch_size);
      for (auto& w : work) {
        w.counter = &counter;
        pool.Schedule([w = &w] { w->DoSomeWork(); });
      }
      counter.Wait();
    }
  }
  state.SetItemsProcessed(state.iterations() * batch_size);
}
BENCHMARK(BM_SpikyLoad)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16);

// Benchmark the creation and destruction of a large number of threads.
void BM_ThreadPoolDuration(benchmark::State& state) {
  constexpr size_t kThreads = 10003;
  for (auto _ : state) {
    absl::Barrier barrier(kThreads);
    {
      ThreadPool pool(kThreads);
      for (int i = 0; i < kThreads; ++i) {
        pool.Schedule([&]() { barrier.Block(); });
      }
    }
  }
}
BENCHMARK(BM_ThreadPoolDuration);

void BM_QueueCountContention(benchmark::State& state) {
  static ThreadPool* pool = new ThreadPool(10);
  for (auto _ : state) {
    int count = pool->queue_count();
    benchmark::DoNotOptimize(count);
  }
}
BENCHMARK(BM_QueueCountContention)->ThreadRange(1, 64);

}  // namespace

int main(int argc, char* argv[]) {
  absl::SetFlag(&FLAGS_logtostderr, true);
  absl::InitializeLog();
  benchmark::RunSpecifiedBenchmarks();
  return 0;
}
