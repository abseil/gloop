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

#include "gloop/thread/fiber/scheduler-types.h"

#include <atomic>
#include <cstdint>
#include <list>
#include <queue>

#include "absl/base/internal/spinlock.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "gloop/base/callback.h"
#include "gloop/gloop_test.h"
#include "gloop/thread/threadpool.h"
#include "gloop/util/functional/to_callback.h"

namespace {

static bool IsZero(int* x) { return *x == 0; }

TEST(CombinerLockTest, BasicTest) {
  static const int kThreads = 20;
  static const int kReps = 500000;

  struct Helper {
    struct TestData {
      absl::Mutex mu;
      int threads;  // count of threads, under mu
      int reps;
      thread::internal::CombinerLock cmu;
      int x;
    };

    static intptr_t CombinableInc(void* v) {
      TestData* d = static_cast<TestData*>(v);
      d->x++;
      return 0;
    }

    static void ThreadBody(TestData* d) {
      for (int i = 0; i < kReps; i++) {
        d->cmu.ExecuteLocked(CombinableInc, d);
      }
      d->mu.lock();
      d->threads--;
      d->mu.unlock();
    }
  };

  ThreadPool tp(kThreads);
  Helper::TestData d;
  d.threads = 0;
  d.x = 0;
  d.reps = kReps;
  for (int t = 0; t < kThreads; t++) {
    d.mu.lock();
    d.threads++;
    d.mu.unlock();
    tp.Schedule([&d] { Helper::ThreadBody(&d); });
  }
  d.mu.LockWhen(absl::Condition(&IsZero, &d.threads));
  d.mu.unlock();
  EXPECT_EQ(d.x, kReps * kThreads);
}

class SpinLockHelper {
 public:
  void ExecuteLocked(Closure* work) {
    absl::base_internal::SpinLockHolder l(lock_);
    work->Run();
  }

 private:
  absl::base_internal::SpinLock lock_;
};

class MutexHelper {
 public:
  void ExecuteLocked(Closure* work) {
    absl::MutexLock l(mu_);
    work->Run();
  }

 private:
  absl::Mutex mu_;
};

class CombinerHelper {
 public:
  CombinerHelper() = default;
  void ExecuteLocked(Closure* work) {
    struct Helper {
      static void* RunClosure(Closure* work) {
        work->Run();
        return nullptr;
      }
    };
    cl_.ExecuteLocked(Helper::RunClosure, work);
  }

 private:
  thread::internal::CombinerLock cl_;
};

template <typename T>
static void WrapBenchmark(benchmark::State& state, T* synch_helper,
                          Closure* work) {
  for (auto _ : state) {
    synch_helper->ExecuteLocked(work);
    // Do a tiny amount of work between each critical section.
    for (int i = 0; i < 100; i++) {
      CHECK_LE(0, i);
    }
  }

  delete work;
}

template <typename T>
static void BM_TinyCritical(benchmark::State& state) {
  struct Helper {
    static void Critical(int* i) { *i += 1; }
  };

  int i = 0;
  static T synch;
  WrapBenchmark(state, &synch, ::util::functional::ToPermanentCallback([&i] {
                  Helper::Critical(&i);
                }));
}
BENCHMARK_TEMPLATE(BM_TinyCritical, SpinLockHelper)->ThreadRange(1, 128);
BENCHMARK_TEMPLATE(BM_TinyCritical, MutexHelper)->ThreadRange(1, 128);
BENCHMARK_TEMPLATE(BM_TinyCritical, CombinerHelper)->ThreadRange(1, 128);

template <typename T>
static void BM_SmallCritical(benchmark::State& state) {
  struct Helper {
    static void Critical(std::list<int>* list) {
      int sum = 0;
      benchmark::DoNotOptimize(sum);
      for (auto i : *list) {
        sum += i;
      }
    }
  };

  static T synch;
  static std::list<int> list = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  WrapBenchmark(state, &synch, ::util::functional::ToPermanentCallback([] {
                  Helper::Critical(&list);
                }));
}
BENCHMARK_TEMPLATE(BM_SmallCritical, SpinLockHelper)->ThreadRange(1, 128);
BENCHMARK_TEMPLATE(BM_SmallCritical, MutexHelper)->ThreadRange(1, 128);
BENCHMARK_TEMPLATE(BM_SmallCritical, CombinerHelper)->ThreadRange(1, 128);

template <typename T>
static void BM_PrioQueueCritical(benchmark::State& state) {
  struct Helper {
    static void Critical(std::priority_queue<int>* pq, int index) {
      pq->push(index);
      int next = pq->top();
      benchmark::DoNotOptimize(next);
      pq->pop();
      next *= 2;
    }
  };

  static T synch;
  static std::priority_queue<int> pq;
  static std::atomic<int32_t> index{0};
  WrapBenchmark(state, &synch,
                ::util::functional::ToPermanentCallback(absl::bind_front(
                    Helper::Critical, &pq,
                    index.fetch_add(1, std::memory_order_relaxed))));
}
BENCHMARK_TEMPLATE(BM_PrioQueueCritical, SpinLockHelper)->ThreadRange(1, 128);
BENCHMARK_TEMPLATE(BM_PrioQueueCritical, MutexHelper)->ThreadRange(1, 128);
BENCHMARK_TEMPLATE(BM_PrioQueueCritical, CombinerHelper)->ThreadRange(1, 128);

}  // namespace
