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

//
// This is a benchmark for Add functionality of thread pool.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/flags.h"
#include "absl/synchronization/barrier.h"
#include "absl/synchronization/blocking_counter.h"
#include "benchmark/benchmark.h"
#include "gloop/base/callback.h"
#include "gloop/thread/executor.h"
#include "gloop/thread/threadpool.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/functional/to_callback.h"
#include "gloop/util/gtl/stl_util.h"

// A Repeatable closure is used to benchmark "Add" functionality of an
// executor. The reason for using this closure instead of other closures is
// that we want to measure the time of only "Add" functionality of an executor
// and therefore we want to reduce other overhead of clusures (like creation
// and destruction time) as much as possible. A repeatable closure adds itself
// into the executor queue. That's why we use it instead of permanent closure
// which does not add itself. However, this closure does not delete itself even
// after repeating the "num_reuse" times.
class RepeatableClosureForAdd : public Closure {
 public:
  // Create a repeatable closure that will be executed by the executor "ex".
  // "num_reuse" indicates how many times this closure will be reused. After
  // that many reuses, it will use "counter" to indicate its completion.
  RepeatableClosureForAdd(thread::Executor* ex, absl::BlockingCounter* counter,
                          int num_reuse)
      : ex_(ex), counter_(counter), num_reuse_(num_reuse) {
    CHECK(ex != nullptr);
    CHECK(counter != nullptr);
  }

  // Overrides the default implementation of Run.
  void Run() override {
    if (num_reuse_.fetch_sub(1, std::memory_order_relaxed) > 1) {
      ex_->Schedule(util::functional::FromCallback(this));
    } else {
      // We are done reusing this closure
      // So we signal completion of this closure
      counter_->DecrementCount();
      delete this;
    }
  }

 private:
  // Executor that executes this closure.
  thread::Executor* ex_;

  // Counter to indicate completion of this closure.
  absl::BlockingCounter* counter_;

  // How many times this closure will be reused.
  std::atomic<int> num_reuse_;
};

// "iters" is the total number of tasks performed during this benchmark.
// "concurrent_closures" represent how many closures will be active
// simultaneously. So, each closure performs iters/concurrent_closures
// number of tasks.
void BenchmarkWithClosureReuse(thread::Executor* ex, int concurrent_closures,
                               benchmark::State& state) {
  int work_per_closure = state.max_iterations / concurrent_closures;
  state.SetItemsProcessed(static_cast<int64_t>(concurrent_closures) *
                          (work_per_closure + 1));
  while (state.KeepRunningBatch(state.max_iterations)) {
    absl::BlockingCounter counter(concurrent_closures);
    for (int i = 0; i < concurrent_closures; i++) {
      RepeatableClosureForAdd* closure =
          new RepeatableClosureForAdd(ex, &counter, work_per_closure);
      closure->Run();
    }
    counter.Wait();
  }
}

// Benchmark a thread pool that reuses 1 closure.
void ThreadPoolClosureReuseHelper(int concurrent_closures,
                                  benchmark::State& state, int num_threads) {
  ThreadPool pool(num_threads);
  BenchmarkWithClosureReuse(&pool, concurrent_closures, state);
}

void BM_ThreadPool1ClosureReuse(benchmark::State& state) {
  const int num_threads = state.range(0);

  ThreadPoolClosureReuseHelper(1, state, num_threads);
}
BENCHMARK(BM_ThreadPool1ClosureReuse)->Arg(1);
BENCHMARK(BM_ThreadPool1ClosureReuse)->Arg(4);
BENCHMARK(BM_ThreadPool1ClosureReuse)->Arg(8);
BENCHMARK(BM_ThreadPool1ClosureReuse)->Arg(16);
BENCHMARK(BM_ThreadPool1ClosureReuse)->Arg(32);

// Benchmark a thread pool that reuses 4 closure.
void BM_ThreadPool4ClosureReuse(benchmark::State& state) {
  const int num_threads = state.range(0);

  ThreadPoolClosureReuseHelper(4, state, num_threads);
}
BENCHMARK(BM_ThreadPool4ClosureReuse)->Arg(1);
BENCHMARK(BM_ThreadPool4ClosureReuse)->Arg(4);
BENCHMARK(BM_ThreadPool4ClosureReuse)->Arg(8);
BENCHMARK(BM_ThreadPool4ClosureReuse)->Arg(16);
BENCHMARK(BM_ThreadPool4ClosureReuse)->Arg(32);

// Benchmark a thread pool that reuses 8 closure.
void BM_ThreadPool8ClosureReuse(benchmark::State& state) {
  const int num_threads = state.range(0);

  ThreadPoolClosureReuseHelper(8, state, num_threads);
}
BENCHMARK(BM_ThreadPool8ClosureReuse)->Arg(1);
BENCHMARK(BM_ThreadPool8ClosureReuse)->Arg(4);
BENCHMARK(BM_ThreadPool8ClosureReuse)->Arg(8);
BENCHMARK(BM_ThreadPool8ClosureReuse)->Arg(16);
BENCHMARK(BM_ThreadPool8ClosureReuse)->Arg(32);

// Benchmark a thread pool that reuses 16 closure.
void BM_ThreadPool16ClosureReuse(benchmark::State& state) {
  const int num_threads = state.range(0);

  ThreadPoolClosureReuseHelper(16, state, num_threads);
}

// Benchmark a thread pool that reuses 32 closure.
void BM_ThreadPool32ClosureReuse(benchmark::State& state) {
  const int num_threads = state.range(0);

  ThreadPoolClosureReuseHelper(32, state, num_threads);
}

// Benchmark a thread pool that reuses 64 closure.
void BM_ThreadPool64ClosureReuse(benchmark::State& state) {
  const int num_threads = state.range(0);

  ThreadPoolClosureReuseHelper(64, state, num_threads);
}

// Benchmark a thread pool that reuses 128 closure.
void BM_ThreadPool128ClosureReuse(benchmark::State& state) {
  const int num_threads = state.range(0);

  ThreadPoolClosureReuseHelper(128, state, num_threads);
}

// Benchmark a thread pool that reuses 512 closure.
void BM_ThreadPool512ClosureReuse(benchmark::State& state) {
  const int num_threads = state.range(0);

  ThreadPoolClosureReuseHelper(512, state, num_threads);
}

// Benchmark a thread pool that reuses 2048 closure.
void BM_ThreadPool2048ClosureReuse(benchmark::State& state) {
  const int num_threads = state.range(0);

  ThreadPoolClosureReuseHelper(2048, state, num_threads);
}

// This part is for benchmarking a thread pool without
// reusing closures.

// Helper function for "BenchmarkAdd".
// It performs the task. And then it adds another closure if
// it needs to or decrements the counter.
void WorkerFunctionForAdd(thread::Executor* ex, absl::BlockingCounter* counter,
                          int remaining_sequential_iters) {
  if (remaining_sequential_iters > 0)
    ex->Schedule(absl::bind_front(&WorkerFunctionForAdd, ex, counter,
                                  remaining_sequential_iters - 1));
  else
    // We are done adding closures.
    counter->DecrementCount();
}

// This function does the actual benchmarking of the Add functionality.
void BenchmarkAdd(thread::Executor* ex, benchmark::State& state,
                  int num_threads) {
  int concurrent_closures =
      (num_threads < static_cast<int>(state.max_iterations))
          ? num_threads
          : state.max_iterations;

  int remaining_sequential_iters = state.max_iterations / concurrent_closures;

  state.SetItemsProcessed(static_cast<int64_t>(concurrent_closures) *
                          (remaining_sequential_iters + 1));

  while (state.KeepRunningBatch(
      state
          .max_iterations)) {  // Add the closures to the queue of the executor.
    absl::BlockingCounter counter(concurrent_closures);
    for (int i = 0; i < concurrent_closures; i++) {
      ex->Schedule(absl::bind_front(&WorkerFunctionForAdd, ex, &counter,
                                    remaining_sequential_iters));
    }

    // Wait for all the closures to finish.
    counter.Wait();
  }
}

void BM_ThreadPoolNoClosureReuse(benchmark::State& state) {
  const int num_threads = state.range(0);

  ThreadPool* pool = new ThreadPool(num_threads);
  BenchmarkAdd(pool, state, num_threads);
  delete pool;
}

// Simulates workloads where many short running callbacks are added to the
// threadpool. The callbacks are not enough to keep all the workers busy
// continuously so the number of workers running changes overtime.
//
// In effect this tests how well the threadpool avoids spurious wakeups.
void BM_SpikyLoad(benchmark::State& state) {
  struct Work {
    Work() {
      cb.reset(::util::functional::ToPermanentCallback(absl::bind_front(
          +[](Work* w) {
            // Simulate small but non-trivial amount of work.
            for (int i = 0; i != 1000; ++i) {
              int cur = w->val;
              w->val = cur + 1;
            }
            w->counter->DecrementCount();
          },
          this)));
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
  ThreadPool pool(num_threads);
  while (state.KeepRunningBatch(kNumSpikes * batch_size)) {
    for (int i = 0; i != kNumSpikes; ++i) {
      absl::BlockingCounter counter(batch_size);
      for (auto& w : work) {
        w.counter = &counter;
        pool.Schedule(util::functional::FromCallback(w.cb.get()));
      }
      counter.Wait();
    }
  }
  state.SetItemsProcessed(state.iterations() * batch_size);
}
BENCHMARK(BM_SpikyLoad)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16);

int main(int argc, char* argv[]) {
  benchmark::RunSpecifiedBenchmarks();
  return 0;
}
