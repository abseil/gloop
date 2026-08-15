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

#include "gloop/util/freelist/fixed_size_object_pool.h"

#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <memory>

#include "absl/container/btree_set.h"
#include "absl/memory/memory.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/base/timer.h"
#include "gloop/thread/thread.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::testing::ContainerEq;
using ::testing::Ge;
using ::testing::IsNull;
using ::testing::Le;
using ::testing::Pointee;

// Implements an object class whose instance count is tracked.
class TrackedObject {
 public:
  TrackedObject() { live_count_ += 1; }
  ~TrackedObject() { live_count_ -= 1; }
  static int live_count_;
};
/*static*/ int TrackedObject::live_count_ = 0;

// Implements a thread that will wait for the object pool free size to reach
// the specified goal.
template <class T>
class NumAvailableWaiter : public Thread {
 public:
  NumAvailableWaiter(FixedSizeObjectPool<T>* pool, int goal) {
    pool_ = pool;
    goal_ = goal;
    done_waiting_ = false;
  }

  // This type is neither copyable nor movable.
  NumAvailableWaiter(const NumAvailableWaiter&) = delete;
  NumAvailableWaiter& operator=(const NumAvailableWaiter&) = delete;

  bool WaitForGet(int ms) {
    absl::MutexLock lock(done_waiting_mutex_);
    done_waiting_mutex_.AwaitWithTimeout(absl::Condition(&done_waiting_),
                                         absl::Milliseconds(ms));
    return done_waiting_;
  }

 protected:
  void Run() override {
    pool_->WaitForNumAvailable(goal_);
    {
      absl::MutexLock lock(done_waiting_mutex_);
      done_waiting_ = true;
    }
  }

 private:
  FixedSizeObjectPool<T>* pool_;
  int goal_;
  absl::Mutex done_waiting_mutex_;
  bool done_waiting_;
};

// Implements a thread that will wait for the next free object from the pool
// (and delete it).
template <class T>
class GetWaiter : public Thread {
 public:
  explicit GetWaiter(FixedSizeObjectPool<T>* pool) {
    pool_ = pool;
    done_waiting_ = false;
  }

  // This type is neither copyable nor movable.
  GetWaiter(const GetWaiter&) = delete;
  GetWaiter& operator=(const GetWaiter&) = delete;

  bool WaitForGet(int ms) {
    absl::MutexLock lock(done_waiting_mutex_);
    done_waiting_mutex_.AwaitWithTimeout(absl::Condition(&done_waiting_),
                                         absl::Milliseconds(ms));
    return done_waiting_;
  }

 protected:
  void Run() override {
    T* object = pool_->Get();
    delete object;
    {
      absl::MutexLock lock(done_waiting_mutex_);
      done_waiting_ = true;
    }
  }

 private:
  FixedSizeObjectPool<T>* pool_;
  absl::Mutex done_waiting_mutex_;
  bool done_waiting_;
};

int* IncIntFactory(int* next) { return new int((*next)++); }

template <class T>
void DiscardObject(FixedSizeObjectPool<T>* pool) {
  T* object = pool->TryGet();
  ASSERT_TRUE(object != nullptr);
  delete object;
}

TEST(FixedSizeObjectPoolTest, ConstructorUsingDefaultFactory) {
  // Test the constructor that constructs objects using their default
  // constructor.
  auto int_pool = std::make_unique<FixedSizeObjectPool<int>>(5, 5);
  EXPECT_EQ(5, int_pool->MaximumSize());
  EXPECT_EQ(5, int_pool->NumAvailable());
  int_pool = std::make_unique<FixedSizeObjectPool<int>>(5, 2);
  EXPECT_EQ(5, int_pool->MaximumSize());
  EXPECT_EQ(5, int_pool->NumAvailable());

  // Make sure the objects are getting deleted when the pool is deleted.
  auto tracked_object_pool =
      std::make_unique<FixedSizeObjectPool<TrackedObject>>(6, 6);
  EXPECT_EQ(6, TrackedObject::live_count_);
  EXPECT_EQ(6, tracked_object_pool->MaximumSize());
  EXPECT_EQ(6, tracked_object_pool->NumAvailable());
  TrackedObject* to = tracked_object_pool->TryGet();
  EXPECT_TRUE(to != nullptr);
  delete to;
  to = tracked_object_pool->TryGet();
  EXPECT_TRUE(to != nullptr);
  delete to;
  EXPECT_EQ(4, tracked_object_pool->NumAvailable());
  tracked_object_pool.reset();
  EXPECT_EQ(0, TrackedObject::live_count_);
}

TEST(FixedSizeObjectPoolTest, ConstructorUsingCallbackFactory) {
  // Test the constructor that takes a factory method.
  int next = 0;
  auto int_pool = std::make_unique<FixedSizeObjectPool<int>>(
      5, 3, [&next] { return absl::WrapUnique(IncIntFactory(&next)); });
  ASSERT_EQ(5, int_pool->NumAvailable());
  EXPECT_EQ(3, next);
  absl::btree_set<int> expected_ints;
  absl::btree_set<int> actual_ints;
  for (int i = 0; i < 5; ++i) {
    expected_ints.insert(i);
    int* obj = int_pool->TryGet();
    ASSERT_TRUE(obj != nullptr);
    actual_ints.insert(*obj);
    delete obj;
  }
  EXPECT_EQ(5, next);
  EXPECT_THAT(actual_ints, ContainerEq(expected_ints));
}

TEST(FixedSizeObjectPoolTest, ConstructorUsingUniquePtrFunction) {
  int next = 0;
  auto int_pool = std::make_unique<FixedSizeObjectPool<int>>(
      5, 3, [&next] { return std::make_unique<int>(next++); });
  ASSERT_EQ(5, int_pool->NumAvailable());
  EXPECT_EQ(3, next);
  absl::btree_set<int> expected_ints;
  absl::btree_set<int> actual_ints;
  for (int i = 0; i < 5; ++i) {
    expected_ints.insert(i);
    int* obj = int_pool->TryGet();
    ASSERT_TRUE(obj != nullptr);
    actual_ints.insert(*obj);
    delete obj;
  }
  EXPECT_EQ(5, next);
  EXPECT_THAT(actual_ints, ContainerEq(expected_ints));
}

TEST(FixedSizeObjectPoolTest, Sizes) {
  auto int_pool = std::make_unique<FixedSizeObjectPool<int>>(5, 3);
  EXPECT_EQ(5, int_pool->MaximumSize());
  EXPECT_EQ(5, int_pool->NumAvailable());
  EXPECT_EQ(0, int_pool->NumGrabbed());

  int* objects[5];
  for (int i = 0; i < 5; ++i) {
    objects[i] = int_pool->Get();
    EXPECT_EQ(5, int_pool->MaximumSize());
    EXPECT_EQ(4 - i, int_pool->NumAvailable());
    EXPECT_EQ(i + 1, int_pool->NumGrabbed());
  }
  for (int i = 0; i < 5; ++i) {
    int_pool->Release(objects[i]);
    EXPECT_EQ(5, int_pool->MaximumSize());
    EXPECT_EQ(1 + i, int_pool->NumAvailable());
    EXPECT_EQ(4 - i, int_pool->NumGrabbed());
  }
}

TEST(FixedSizeObjectPoolTest, Retire) {
  int next = 1;
  auto int_pool = std::make_unique<FixedSizeObjectPool<int>>(
      1, 0, [&next] { return absl::WrapUnique(IncIntFactory(&next)); });
  int* n = int_pool->Get();
  EXPECT_EQ(1, *n);
  EXPECT_EQ(0, int_pool->NumAvailable());
  int_pool->Retire(n);
  EXPECT_EQ(1, int_pool->NumAvailable());
  n = int_pool->Get();
  EXPECT_EQ(2, *n);
  int_pool->Retire(n);
}

// When a test is run under AddressSanitizer (<link>), threads
// start with significant delay, so we have to increase the time limit
// to make sure the waiters get object from a pool if it's available.
#ifdef ADDRESS_SANITIZER
static const size_t kWaiterRunningTimeMs = 1000;
#else
static const size_t kWaiterRunningTimeMs = 200;
#endif

TEST(FixedSizeObjectPoolTest, Get) {
  auto int_pool = std::make_unique<FixedSizeObjectPool<int>>(1, 0);

  // Check that waiting for an object that is free returns immediately.
  auto waiter = std::make_unique<GetWaiter<int>>(int_pool.get());
  waiter->SetJoinable(true);
  waiter->Start();
  EXPECT_TRUE(waiter->WaitForGet(kWaiterRunningTimeMs));
  waiter->Join();

  // Test normal operation.
  waiter = std::make_unique<GetWaiter<int>>(int_pool.get());
  waiter->SetJoinable(true);
  waiter->Start();
  EXPECT_FALSE(waiter->WaitForGet(kWaiterRunningTimeMs));
  int_pool->Release(new int());
  EXPECT_TRUE(waiter->WaitForGet(kWaiterRunningTimeMs));
  waiter->Join();
}

TEST(FixedSizeObjectPoolTest, GetRAII) {
  auto int_pool = std::make_unique<FixedSizeObjectPool<int>>(1, 0);
  {
    FixedSizeObjectPool<int>::UniquePtr my_int = int_pool->GetRAII();
    EXPECT_EQ(0, *my_int);
  }
  EXPECT_EQ(0, int_pool->NumGrabbed());
}

TEST(FixedSizeObjectPoolTest, WaitForNumAvailable) {
  auto int_pool = std::make_unique<FixedSizeObjectPool<int>>(5, 0);

  // Check that waiting for the free size to be what it currently is returns
  // immediately.
  auto waiter = std::make_unique<NumAvailableWaiter<int>>(int_pool.get(), 5);
  waiter->SetJoinable(true);
  waiter->Start();
  EXPECT_TRUE(waiter->WaitForGet(kWaiterRunningTimeMs));
  waiter->Join();

  // Test normal operation.
  waiter = std::make_unique<NumAvailableWaiter<int>>(int_pool.get(), 1);
  waiter->SetJoinable(true);
  waiter->Start();
  EXPECT_FALSE(waiter->WaitForGet(kWaiterRunningTimeMs));
  for (int i = 0; i < 3; ++i) {
    DiscardObject(int_pool.get());
  }
  EXPECT_FALSE(waiter->WaitForGet(kWaiterRunningTimeMs));
  DiscardObject(int_pool.get());
  EXPECT_TRUE(waiter->WaitForGet(kWaiterRunningTimeMs));
  waiter->Join();
}

template <typename DurationType>
DurationType MillisecondsForType(int64_t num_milliseconds);

template <>
absl::Duration MillisecondsForType<absl::Duration>(int64_t num_milliseconds) {
  return absl::Milliseconds(num_milliseconds);
}

template <>
int64_t MillisecondsForType<int64_t>(int64_t num_milliseconds) {
  return num_milliseconds;
}

template <typename DurationType>
void TestGetWithDuration() {
  auto int_pool = std::make_unique<FixedSizeObjectPool<int>>(1, 0);

  CycleTimer timer;
  timer.Start();

  // Get the element, returning right away
  int* succeed =
      int_pool->GetWithTimeout(MillisecondsForType<DurationType>(20));
  EXPECT_TRUE(succeed != nullptr);
  EXPECT_GE(10, absl::ToInt64Milliseconds(timer.GetDuration()));

  timer.Restart();
  int* fail = int_pool->GetWithTimeout(MillisecondsForType<DurationType>(20));
  EXPECT_TRUE(fail == nullptr);
  EXPECT_LE(20, absl::ToInt64Milliseconds(timer.GetDuration()));

  // put the element back
  int_pool->Release(succeed);

  // and get it again
  timer.Restart();
  succeed = int_pool->GetWithTimeout(MillisecondsForType<DurationType>(20));
  EXPECT_TRUE(succeed != nullptr);
  EXPECT_GE(10, absl::ToInt64Milliseconds(timer.GetDuration()));  // right away
  int_pool->Release(succeed);                                     // put it back
}

TEST(FixedSizeObjectPoolTest, GetWithDurationTimeout) {
  TestGetWithDuration<absl::Duration>();
}

TEST(FixedSizeObjectPoolTest, GetWithMillisecondTimeout) {
  TestGetWithDuration<int64_t>();
}

TEST(FixedSizeObjectPoolTest, TryGetRAII) {
  FixedSizeObjectPool<int> int_pool(3, 0);
  auto succeed1 = int_pool.TryGetRAII();
  auto succeed2 = int_pool.TryGetRAII();
  auto succeed3 = int_pool.TryGetRAII();
  // Just 3 objects in the pool.
  auto fail1 = int_pool.TryGetRAII();
  auto fail2 = int_pool.TryGetRAII();
  EXPECT_THAT(succeed1, Pointee(0));
  EXPECT_THAT(succeed2, Pointee(0));
  EXPECT_THAT(succeed3, Pointee(0));
  EXPECT_THAT(fail1, IsNull());
  EXPECT_THAT(fail2, IsNull());
  // Return one object, then we can get it once again.
  succeed2.reset();
  auto succeed4 = int_pool.TryGetRAII();
  EXPECT_THAT(succeed4, Pointee(0));
  auto fail3 = int_pool.TryGetRAII();
  EXPECT_THAT(fail3, IsNull());
}

TEST(FixedSizeObjectPoolTest, GetRAIIWithTimeout) {
  FixedSizeObjectPool<int> int_pool(1, 0);
  const absl::Duration wait_duration = absl::Milliseconds(20);
  CycleTimer timer;
  timer.Start();
  {
    // Get the element, returning right away.
    auto succeed = int_pool.GetRAIIWithTimeout(wait_duration);
    EXPECT_TRUE(succeed != nullptr);
    EXPECT_THAT(succeed, Pointee(0));
    EXPECT_THAT(timer.GetDuration(), Le(wait_duration / 2));  // Right away.

    timer.Restart();
    // Fail to get an element again, returning after the timeout.
    auto fail = int_pool.GetRAIIWithTimeout(wait_duration);
    EXPECT_THAT(fail, IsNull());
    EXPECT_THAT(timer.GetDuration(), Ge(wait_duration));
  }
  // The element is now back in the pool. Get it again.
  timer.Restart();
  auto succeed = int_pool.GetRAIIWithTimeout(wait_duration);
  EXPECT_THAT(succeed, Pointee(0));
  EXPECT_THAT(timer.GetDuration(), Le(wait_duration / 2));  // Right away.
}

}  // namespace
