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

#include <memory>

#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "gloop/thread/threadpool.h"
#include "gtest/gtest.h"

void ThreadFunction(void* thread_local_int) {
  // Force to do some work.
  volatile int dummy_sum = 0;
  for (volatile int k = 0; k <= 100; ++k) {
    dummy_sum += k;
  }
  CHECK_EQ(5050, dummy_sum);
  ThreadLocal<int>* my_int = static_cast<ThreadLocal<int>*>(thread_local_int);
  my_int->set(static_cast<int>(dummy_sum));
}

TEST(ThreadLocalTest, IntConstructorWorks) {
  ThreadLocal<int> t;
  EXPECT_EQ(0, t.get());
  t.set(12);
  EXPECT_EQ(12, *t.pointer());
}

TEST(ThreadLocalTest, VoidPointerConstructorWorks) {
  ThreadLocal<void*> t;
  EXPECT_TRUE(t.get() == nullptr);
  void* test_ptr = reinterpret_cast<void*>(0x12);
  t.set(test_ptr);
  EXPECT_EQ(test_ptr, *t.pointer());
}

TEST(ThreadLocalTest, SingleArgConstructorWorks) {
  struct Type {
    Type() = default;
    explicit Type(int n) : n(n) {}
    int value() const { return n; }
    int n = 0;
  };
  ThreadLocal<Type> t(Type(10));
  EXPECT_EQ(10, t.get().value());
  t.set(Type());
  EXPECT_EQ(0, t.pointer()->value());
}

TEST(ThreadLocalTest, CopyConstructableUseWorks) {
  struct Type {
    explicit Type(int n) : n(n) {}
    Type(const Type& t) : n(t.n) {}
    Type() = delete;
    Type& operator=(const Type&) = default;

    int value() const { return n; }
    int n = 0;
  };

  ThreadLocal<Type> t(Type(10));
  EXPECT_EQ(10, t.get().value());
  t.set(Type(12));
  EXPECT_EQ(12, t.pointer()->value());
}

TEST(ThreadLocalTest, ThreadsGetTheirOwnObjects) {
  std::unique_ptr<ThreadPool> thread_pool(new ThreadPool(5));
  ThreadLocal<int> sum;

  for (int i = 0; i < 1000; ++i) {
    thread_pool->Schedule(absl::bind_front(ThreadFunction, &sum));  // NOLINT
  }

  // Join all threads, before ThreadLocal goes out of scope.
  thread_pool.reset(nullptr);
}

TEST(ThreadLocalTest, ThreadLocalWithDefaultValue) {
  std::unique_ptr<ThreadPool> thread_pool(new ThreadPool(5));
  ThreadLocal<int> value(42);
  ThreadLocal<bool> set_to_one;

  for (int i = 0; i < 50; ++i) {
    thread_pool->Schedule([&value, &set_to_one]() -> void {
      // Each thread works on its own copy. Check initialization on first
      // call, otherwise set to one and check against one on subsequent calls.
      if (set_to_one.get()) {
        CHECK_EQ(1, value.get());
      } else {
        CHECK_EQ(42, value.get());  // Original value.
        value.set(1);               // Set to one and flag.
        set_to_one.set(true);
      }
    });
  }

  // Join all threads, before ThreadLocals go out of scope.
  thread_pool.reset(nullptr);
}

TEST(ThreadLocalTest, ThreadLocalUniquePtr) {
  // Test type with default constructor that is only moveable.
  ThreadLocal<std::unique_ptr<int>> unique;
  auto* ptr = unique.pointer();
  if (*ptr == nullptr) {
    ptr->reset(new int(12));
  }
}
