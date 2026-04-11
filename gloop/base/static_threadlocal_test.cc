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

#include "gloop/base/static_threadlocal.h"

#include <stdio.h>

#include <iostream>
#include <memory>
#include <thread>  // NOLINT(build/c++11)
#include <vector>

#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/util/random/shared_bit_gen.h"
#include "gtest/gtest.h"

#if GTEST_GOOGLE3_MODE_
#include "benchmark/benchmark.h"
#include "gloop/thread/threadlocal.h"
#endif  // GTEST_GOOGLE3_MODE_

STATIC_THREAD_LOCAL(int, value);
STATIC_THREAD_LOCAL_POD(int, value2);
STATIC_THREAD_LOCAL_WITH_CONSTRUCTOR_ARGS(int, value3, (3));

class Foo1Class {
 public:
  Foo1Class() {
    x_ = new int(1);
    VLOG(0) << "Foo1";
  }
  ~Foo1Class() {
    delete x_;
    VLOG(0) << "~Foo1";
  }
  int value() const { return *x_; }

 private:
  int* x_;
};

STATIC_THREAD_LOCAL(Foo1Class, foo1);

class Foo2Class {
 public:
  Foo2Class() {
    x_ = new int(2);
    VLOG(0) << "Foo2";
  }
  ~Foo2Class() {
    delete x_;
    EXPECT_EQ(foo1.get().value(), 1);
    VLOG(0) << "~Foo2";
  }
  int value() const { return *x_; }

 private:
  int* x_;
};

STATIC_THREAD_LOCAL(Foo2Class, foo2);

// Create "num_threads" threads and let them run "callback".
static void RunThreads(int num_threads, void (*callback)(int i)) {
  std::vector<std::thread> threads;
  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back(callback, i);
  }
  for (int i = 0; i < num_threads; ++i) {
    threads[i].join();
  }
}

// Access a static threadlocal in the destructor of another threadlocal.

static void NestedAccessThreadBody(int i) {
  EXPECT_EQ(foo1.get().value(), 1);
  EXPECT_EQ(foo2.get().value(), 2);
}

TEST(StaticThreadLocal, NestedAccess) { RunThreads(1, NestedAccessThreadBody); }

static void Test1ThreadBody(int val) {
  *value.pointer() = val;
  *value2.pointer() = val + 100;
  util_random::SharedBitGen gen;
  for (int i = 0; i < 10; ++i) {
    absl::SleepFor(absl::Milliseconds(absl::Uniform(gen, 0, 300)));
    EXPECT_EQ(value.get(), val);
    EXPECT_EQ(*value.pointer(), val);

    EXPECT_EQ(value2.get(), val + 100);
    EXPECT_EQ(*value2.pointer(), val + 100);
  }
}

//
// Check if each thread has accessses its local copy of the tls variable.
//
TEST(StaticThreadLocal, Test1) {
  if (value.is_native_tls()) {
    printf("Using compiler-supported TLS.\n");
  } else {
    printf("Using pthread_getspecific-based TLS.\n");
  }

  // If we read without setting, we should get the default value for
  // the type (for int, it's zero).
  EXPECT_EQ(value.get(), 0);
  EXPECT_EQ(value2.get(), 0);
  EXPECT_EQ(value3.get(), 3);

  RunThreads(10, Test1ThreadBody);
}

static void LeakThreadBody() {
  unsigned v = absl::Uniform<unsigned>(util_random::SharedBitGen());
  *value.pointer() = v;
  *value2.pointer() = v;
  EXPECT_EQ(value.get(), v);
  EXPECT_EQ(value2.get(), v);
}

//
// Create and delete threads rapidly, and make sure that there's no
// memory leak.
//
TEST(StaticThreadLocal, Leak) {
  std::cout << "Running smoke test for 10 seconds." << std::endl;
  absl::Time end_time = absl::Now() + absl::Seconds(10);
  int n = 0;
  while (absl::Now() < end_time) {
    std::thread thread(LeakThreadBody);
    thread.join();
    ++n;
  }

  std::cout << "Created " << n << " threads";
}

// Test that the initialization of non-POD STATIC_THREAD_LOCAL objects is safe
// with respect to global constructor ordering.  When run under ASAN this will
// generate initialization-order-fiasco errors if uninitialized state is
// referenced.
STATIC_THREAD_LOCAL_WITH_CONSTRUCTOR_ARGS(std::unique_ptr<int>,
                                          static_thread_local, (new int(1)));

int PeekAtRemoteStaticThreadLocal1() {
#if GTEST_GOOGLE3_MODE_
  benchmark::DoNotOptimize(static_thread_local.safe_pointer());
  benchmark::DoNotOptimize(static_thread_local.pointer());
#endif

  return *static_thread_local.get();
}

extern int PeekAtRemoteStaticThreadLocal2();
static int unordered_global_constructors = PeekAtRemoteStaticThreadLocal2();

#if GTEST_GOOGLE3_MODE_
static ThreadLocal<int> value_threadlocal;

// Compare the performance between StaticThreadLocal and ThreadLocal.
static void BM_StaticThreadLocal(benchmark::State& state) {
  *value.pointer() = 100;
  for (auto _ : state) {
    int* p = value.pointer();
    benchmark::DoNotOptimize(*p);
  }
}
BENCHMARK(BM_StaticThreadLocal)->Threads(1)->ThreadPerCpu();

static void BM_StaticThreadLocalPOD(benchmark::State& state) {
  *value2.pointer() = 100;
  for (auto _ : state) {
    int* p = value2.pointer();
    benchmark::DoNotOptimize(*p);
  }
}
BENCHMARK(BM_StaticThreadLocalPOD)->Threads(1)->ThreadPerCpu();

int GetExternThreadLocal() { return value.get(); }

int GetExternThreadLocalPOD() { return value2.get(); }

// Benchmark the case where a ThreadLocal is used by more than
// one file.
static void BM_ExternThreadLocal(benchmark::State& state) {
  *value.pointer() = 100;
  for (auto _ : state) {
    benchmark::DoNotOptimize(GetExternThreadLocal());
  }
}
BENCHMARK(BM_ExternThreadLocal)->Threads(1)->ThreadPerCpu();

static void BM_ExternThreadLocalPOD(benchmark::State& state) {
  *value2.pointer() = 100;
  for (auto _ : state) {
    benchmark::DoNotOptimize(GetExternThreadLocalPOD());
  }
}
BENCHMARK(BM_ExternThreadLocalPOD)->Threads(1)->ThreadPerCpu();

static void BM_SystemThreadLocalPOD(benchmark::State& state) {
  static int __thread value = 100;
  for (auto _ : state) {
    int* p = &value;
    benchmark::DoNotOptimize(*p);
  }
}
BENCHMARK(BM_SystemThreadLocalPOD)->Threads(1)->ThreadPerCpu();

static void BM_ThreadLocal(benchmark::State& state) {
  value_threadlocal.set(100);
  for (auto _ : state) {
    benchmark::DoNotOptimize(value_threadlocal.get());
  }
}
BENCHMARK(BM_ThreadLocal)->Threads(1)->ThreadPerCpu();

// C++11's thread_local
static void BM_Cpp11ThreadLocal(benchmark::State& state) {
  thread_local static int value = 100;
  for (auto _ : state) {
    int* p = &value;
    benchmark::DoNotOptimize(*p);
  }
}
BENCHMARK(BM_Cpp11ThreadLocal)->Threads(1)->ThreadPerCpu();

static int* Cpp11ThreadLocal() {
  thread_local static int value = 100;
  return &value;
}

// Use a C++11 thread_local that is only referred to from a function.
// This avoids global construction problems.
static void BM_Cpp11ThreadLocalViaFn(benchmark::State& state) {
  for (auto _ : state) {
    int* p = Cpp11ThreadLocal();
    benchmark::DoNotOptimize(*p);
  }
}
BENCHMARK(BM_Cpp11ThreadLocalViaFn)->Threads(1)->ThreadPerCpu();

#endif  // GTEST_GOOGLE3_MODE_
