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

#include "gloop/thread/threadlocal.h"

#include <stdio.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/container/fixed_array.h"
#include "absl/functional/bind_front.h"
#include "absl/synchronization/notification.h"
#include "benchmark/benchmark.h"
#include "gloop/concurrent/barrier/barrier.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/threadpool.h"
#include "gloop/util/functional/to_callback.h"
#include "gtest/gtest.h"

static std::atomic<intptr_t> constructions;
static std::atomic<intptr_t> destructions;

// Special type that counts constructions/destructions
class Type {
 private:
  int value_;

 public:
  Type() {
    value_ = 0;
    constructions.fetch_add(1, std::memory_order_relaxed);
  }

  Type(const Type& t) {
    value_ = t.value_;
    constructions.fetch_add(1, std::memory_order_relaxed);
  }

  ~Type() { destructions.fetch_add(1, std::memory_order_relaxed); }

  Type(int v) : value_(v) {
    constructions.fetch_add(1, std::memory_order_relaxed);
  }
  int value() const { return value_; }
  void set_value(int value) { value_ = value; }
};

static void ResetCounters() {
  constructions.store(0, std::memory_order_relaxed);
  destructions.store(0, std::memory_order_relaxed);
}

TEST(ThreadLocalTest, ScalarTypeConstructions) {
  {
    ThreadLocal<int> t;
    ASSERT_EQ(t.get(), 0);
  }
  {
    ThreadLocal<void*> t;
    ASSERT_EQ(t.get(), nullptr);
  }
}

TEST(ThreadLocalTest, ZeroArgumentConstruction) {
  ResetCounters();
  {
    ThreadLocal<Type> t;
    ASSERT_EQ(constructions.load(std::memory_order_relaxed),
              0);  // no default value
    ASSERT_EQ(t.get().value(), 0);
    ASSERT_EQ(constructions.load(std::memory_order_relaxed), 1);
  }
  ASSERT_EQ(destructions.load(std::memory_order_relaxed), 1);
}

TEST(ThreadLocalTest, SingleArgumentConstruction) {
  ResetCounters();
  {
    ThreadLocal<Type> t(Type(10));
    ASSERT_EQ(constructions.load(std::memory_order_relaxed),
              2);  // For Type(10) and its copy
    ASSERT_EQ(t.get().value(), 10);
    ASSERT_EQ(constructions.load(std::memory_order_relaxed),
              3);  // Another copy for this thread
  }
  ASSERT_EQ(destructions.load(std::memory_order_relaxed),
            3);  // Type(10), startup copy, copy for this thread
}

TEST(ThreadLocalTest, GetIsConst) {
  ResetCounters();
  {
    const ThreadLocal<Type> t(Type(10));
    ASSERT_EQ(constructions.load(std::memory_order_relaxed),
              2);  // For Type(10) and its copy
    ASSERT_EQ(t.get().value(), 10);
    ASSERT_EQ(constructions.load(std::memory_order_relaxed),
              3);  // Another copy for this thread
  }
  ASSERT_EQ(destructions.load(std::memory_order_relaxed),
            3);  // Type(10), startup copy, copy for this thread
}

TEST(ThreadLocalTest, UniquePtr) {
  ResetCounters();
  {
    ThreadLocal<std::unique_ptr<Type>> t;
    ASSERT_EQ(constructions.load(std::memory_order_relaxed), 0);  // none yet
    Type* p = t.get().get();
    ASSERT_EQ(p, nullptr);
    t.pointer()->reset(new Type(10));
    ASSERT_EQ(constructions.load(std::memory_order_relaxed),
              1);  // the one above
    p = t.get().get();
    ASSERT_NE(p, nullptr);
    ASSERT_EQ(constructions.load(std::memory_order_relaxed),
              1);  // stil the same
    ASSERT_EQ(t.get()->value(), 10);
    t.pointer()->reset(new Type(5));
    ASSERT_EQ(constructions.load(std::memory_order_relaxed),
              2);  // plus the above one
    ASSERT_EQ(destructions.load(std::memory_order_relaxed),
              1);  // old value in t is gone
    ASSERT_EQ(t.get()->value(), 5);
  }
  ASSERT_EQ(destructions.load(std::memory_order_relaxed),
            2);  // Type(10), Type(5)
}

TEST(ThreadLocalTest, DestroyLargeNumber) {
  ResetCounters();
  {
    ThreadLocal<Type>* array = new ThreadLocal<Type>[10];
    ASSERT_EQ(constructions.load(std::memory_order_relaxed),
              0);  // no default values
    for (int i = 0; i < 10; ++i) array[i].pointer();
    ASSERT_EQ(constructions.load(std::memory_order_relaxed),
              10);  // default values for this thread created
    delete[] array;
    ASSERT_EQ(destructions.load(std::memory_order_relaxed), 10);
  }
}

namespace {
class NoDefaultConstructor {
 public:
  explicit NoDefaultConstructor(int arg) : arg_(arg) {}
  int arg_;

 private:
  NoDefaultConstructor();
};
}  // namespace

TEST(ThreadLocal, TypeWithoutDefaultConstructor) {
  NoDefaultConstructor prototype(10);
  ThreadLocal<NoDefaultConstructor> t(prototype);
  EXPECT_EQ(10, t.get().arg_);
}

namespace {
class NoCopyConstructor {
 public:
  NoCopyConstructor() : arg_(-1) {}

  // This type is neither copyable nor movable.
  NoCopyConstructor(const NoCopyConstructor&) = delete;
  NoCopyConstructor& operator=(const NoCopyConstructor&) = delete;
  int arg_;
};
}  // namespace

TEST(ThreadLocal, TypeWithoutCopyConstructor) {
  ThreadLocal<NoCopyConstructor> t;
  EXPECT_EQ(-1, t.get().arg_);
}

static void BM_ThreadLocal(benchmark::State& state) {
  ThreadLocal<int> t;
  for (auto _ : state) {
    t.set(t.get() + 1);
    benchmark::DoNotOptimize(t);
  }
}
BENCHMARK(BM_ThreadLocal);

#ifdef ABSL_HAVE_THREAD_LOCAL
static void BM_NativeThreadLocal(benchmark::State& state) {
  thread_local int t;
  for (auto _ : state) {
    ++t;
    benchmark::DoNotOptimize(t);
  }
}
BENCHMARK(BM_NativeThreadLocal);
#endif

static void InitWorker(ThreadLocal<int>* x) { x->set(100); }

static void BM_ThreadLocalInit(benchmark::State& state) {
  const int threads = state.range(0);
  ThreadPool* pool = nullptr;
  if (threads > 0) {
    // Create a dummy thread-local to reserve a small id
    ThreadLocal<int> dummy;

    pool = new ThreadPool(threads);

    // Force thread-local access in all threads
    absl::Notification all_done;
    absl::FixedArray<absl::Notification, 0> inited(threads);
    for (int i = 0; i < threads; i++) {
      pool->Schedule([&] {
        InitWorker(&dummy);
        inited[i].Notify();
        all_done.WaitForNotification();
      });
    }

    // Wait until all threads have done their initializations
    for (int i = 0; i < threads; i++) {
      inited[i].WaitForNotification();
    }

    // Let all closures exit
    all_done.Notify();
  }

  for (auto _ : state) {
    ThreadLocal<int> t;
    t.set(10);
  }
  delete pool;
}
BENCHMARK(BM_ThreadLocalInit)->Range(1, 1000);

static void BM_ThreadLocalMany(benchmark::State& state) {
  for (auto _ : state) {
    // Create a brand new thread to get fresh ThreadLocal state.
    ThreadPool pool(1);
    pool.Schedule([&state] {
      std::vector<ThreadLocal<int>> locals(state.range(0));
      for (auto& t : locals) {
        ++(*t.pointer());
      }
    });
  }
}
BENCHMARK(BM_ThreadLocalMany)->Arg(100000);
