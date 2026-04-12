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

// A set of synchronization benchmarks intended to test performance of common
// cases, while being able to be run under benchy in a reasonable period of
// time.

#include <atomic>

#include "absl/base/const_init.h"
#include "absl/base/no_destructor.h"
#include "absl/base/optimization.h"
#include "absl/synchronization/internal/create_thread_identity.h"
#include "absl/synchronization/internal/kernel_timeout.h"
#include "absl/synchronization/internal/per_thread_sem.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/thread-identity.h"
#include "gloop/gloop_test.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"

namespace gloop_do_not_use {
struct SynchronizationBenchmarkPeer {
  static bool WaitUntil(absl::Time t) {
    return absl::synchronization_internal::PerThreadSem::Wait(
        absl::synchronization_internal::KernelTimeout{t});
  }

  static void Post(absl::base_internal::ThreadIdentity* id) {
    absl::synchronization_internal::PerThreadSem::Post(id);
  }
};
}  // namespace gloop_do_not_use

namespace thread {

using ::gloop_do_not_use::SynchronizationBenchmarkPeer;

namespace {

struct ABSL_CACHELINE_ALIGNED SharedMutexState {
  absl::Mutex mu{absl::kConstInit};
};

absl::NoDestructor<SharedMutexState> shared_mutex_state{};

absl::Mutex& SharedMutex() { return shared_mutex_state->mu; }

void BM_LockUnlock(benchmark::State& state) {
  auto& mutex = SharedMutex();
  for (auto s : state) {
    mutex.lock();
    mutex.unlock();
  }
}

void BM_AwaitTimeout(benchmark::State& state) {
  auto& mutex = SharedMutex();
  bool cond = false;
  for (auto s : state) {
    mutex.LockWhenWithTimeout(absl::Condition(&cond), absl::Microseconds(100));
    mutex.unlock();
  }
}

void BM_AwaitHandOff(benchmark::State& state) {
  auto& mutex = SharedMutex();
  bool cond = false;
  std::atomic_flag done{false};
  ::ClosureThread switcher{thread::Options().set_joinable(true), "switcher",
                           [&] {
                             while (!done.test(std::memory_order_relaxed)) {
                               {
                                 absl::MutexLock l(mutex);
                                 cond = true;
                               }
                               {
                                 absl::MutexLock l(mutex);
                                 cond = false;
                               }
                               absl::SleepFor(absl::Microseconds(100));
                             }
                           }};
  switcher.Start();
  for (auto s : state) {
    mutex.LockWhen(absl::Condition(&cond));
    mutex.unlock();
  }
  done.test_and_set();
  switcher.Join();
}

void BM_PerThreadWaitTimeout(benchmark::State& state) {
  for (auto s : state) {
    SynchronizationBenchmarkPeer::WaitUntil(absl::Now() +
                                            absl::Microseconds(100));
  }
}

void BM_PerThreadWaitHandOff(benchmark::State& state) {
  std::atomic_flag done{false};
  absl::base_internal::ThreadIdentity* id =
      absl::synchronization_internal::GetOrCreateCurrentThreadIdentity();
  ::ClosureThread switcher{thread::Options().set_joinable(true), "switcher",
                           [&] {
                             while (!done.test(std::memory_order_relaxed)) {
                               SynchronizationBenchmarkPeer::Post(id);
                               absl::SleepFor(absl::Microseconds(100));
                             }
                           }};
  switcher.Start();
  for (auto s : state) {
    SynchronizationBenchmarkPeer::WaitUntil(absl::InfiniteFuture());
  }
  done.test_and_set();
  switcher.Join();
}

BENCHMARK(BM_LockUnlock)->ThreadRange(1, 256);
BENCHMARK(BM_AwaitTimeout)->ThreadRange(1, 256);
BENCHMARK(BM_AwaitHandOff)->ThreadRange(1, 256);
BENCHMARK(BM_PerThreadWaitTimeout)->ThreadRange(1, 256);
BENCHMARK(BM_PerThreadWaitHandOff)->ThreadRange(1, 256);

}  // namespace
}  // namespace thread
