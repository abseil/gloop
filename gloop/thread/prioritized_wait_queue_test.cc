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

#include "gloop/thread/prioritized_wait_queue.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <vector>

#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/threadpool.h"
#include "gloop/util/random/shared_bit_gen.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

// Define a PrioritizedWaitQueue that sorts in ascending order, so that we
// get the same results as with a default std::sort().  That is, the queue item
// with the _lowest_ priority is popped first.
typedef PrioritizedWaitQueue<int, std::greater<int>> AscendingPriorityWaitQueue;

// Returns a vector of integers which is formed by inserting consecutive values
// from low to high, and then randomly shuffling.
static std::vector<int> MakeShuffledRange(int low, int high) {
  std::vector<int> numbers;
  for (int i = low; i < high; ++i) numbers.push_back(i);
  std::shuffle(numbers.begin(), numbers.end(), util_random::SharedBitGen());
  return numbers;
}

// Insert integers between low inclusive and high exclusive into q.
// The integers are inserted in a random order.
// For a PrioritizedWaitQueue<int>, then these will be re-ordered correctly.
static void PushRangeRandomly(AscendingPriorityWaitQueue* q, int low,
                              int high) {
  std::vector<int> numbers = MakeShuffledRange(low, high);
  for (std::size_t i = 0; i < numbers.size(); ++i) {
    VLOG(2) << "Pushing " << numbers[i];
    q->push(numbers[i]);
  }
}

// Push the numbers between 0 and 999 inclusive from several threads in the
// pool.  Try to get the pushes to race and interleave with each other.
static void PushRanges(AscendingPriorityWaitQueue* q, ThreadPool* pool) {
  VLOG(1) << "Adding 20-36";
  pool->Schedule([q] { PushRangeRandomly(q, 20, 36); });
  VLOG(1) << "Adding 7-20";
  pool->Schedule([q] { PushRangeRandomly(q, 7, 20); });
  VLOG(1) << "Adding 36-501";
  pool->Schedule([q] { PushRangeRandomly(q, 36, 501); });
  VLOG(1) << "Adding 501-1000";
  pool->Schedule([q] { PushRangeRandomly(q, 501, 1000); });
  VLOG(1) << "Adding 0-5";
  pool->Schedule([q] { PushRangeRandomly(q, 0, 5); });
  VLOG(1) << "Adding 5-7";
  pool->Schedule([q] { PushRangeRandomly(q, 5, 7); });
}

// Pop elements from q.  Make sure that exactly <high> elements were
// present and their values are all integers between 0 and high-1
// inclusive.
static void PopRange(AscendingPriorityWaitQueue* q, int high) {
  std::vector<int> results;
  // Give up if we don't get all the elements back from the queue
  // in 10 seconds.
  int timeout = 10;
  int r;
  for (int i = 0; i != high; ++i) {
    while (!q->Pop(&r)) {
      if (!timeout--) LOG(FATAL) << "Can't find all elements in the queue";
      VLOG(1) << "Sleeping for a second...";
      absl::SleepFor(absl::Seconds(1));
    }
    VLOG(2) << "Popped " << r;
    results.push_back(r);
  }
  CHECK(q->empty());
  CHECK(!q->Pop(&r));
  std::sort(results.begin(), results.end());
  for (int i = 0; i != high; ++i) CHECK_EQ(i, results[i]);
}

// Since AscendingPriorityWaitQueue is a priority queue, we push everything on
// the queue and wait for the lowest possible element to be at Front().
static void TestPriorityFront(AscendingPriorityWaitQueue* q, int high) {
  int timeout = 10;
  int front = -1;
  for (int i = 0; i < high; ++i) {
    while (true) {
      while (!q->Front(&front)) {
        if (!timeout--) LOG(FATAL) << "Can't find all elements in the queue";
        VLOG(1) << "Sleeping for a second...";
        absl::SleepFor(absl::Seconds(1));
      }
      if (front == i) break;
      if (!timeout--) LOG(FATAL) << "Can't find all elements in the queue";
      VLOG(1) << "Sleeping for a second...";
      absl::SleepFor(absl::Seconds(1));
    }
    CHECK(q->Pop(&front));
    CHECK_EQ(front, i);
  }
}

// Ensure that items pushed in out-of-order are correctly popped in
// sorted order.  (for PrioritizedWaitQueue)
static void TestSortedPop(AscendingPriorityWaitQueue* q) {
  VLOG(1) << "Testing sorted pop";
  CHECK(q->empty());
  PushRangeRandomly(q, 0, 100);
  int r;
  for (int i = 0; i < 100; ++i) {
    CHECK(q->Pop(&r));
    VLOG(2) << "Popped " << r;
    CHECK_EQ(i, r);
  }
  CHECK(q->empty());
}

// Pop elements from q using the Wait method.  Make sure that exactly <high>
// elements were present and their values are all integers between 0 and
// high-1 inclusive.
static void WaitRange(AscendingPriorityWaitQueue* q, int high) {
  VLOG(1) << "Testing Wait";
  std::vector<int> results;
  for (int i = 0; i != high; ++i) {
    int r;
    CHECK(q->Wait(&r));
    VLOG(2) << "Waited and got " << r;
    results.push_back(r);
  }
  CHECK(q->empty());
  std::sort(results.begin(), results.end());
  for (int i = 0; i != high; ++i) CHECK(results[i] == i);
}

// Push 0 and 1, then wait five seconds, and then push 2 and 3.
static void SlowPush(AscendingPriorityWaitQueue* q) {
  q->push(0);
  q->push(1);
  absl::SleepFor(absl::Seconds(5));
  q->push(2);
  q->push(3);
}

// Test the WaitWithTimeout as well as StopWaiters.
static void TestTimeout(AscendingPriorityWaitQueue* q, ThreadPool* pool) {
  VLOG(1) << "Testing Timeout";
  pool->Schedule(absl::bind_front(SlowPush, q));
  bool timed_out;
  int r;
  CHECK(q->WaitWithTimeout(&r, 3000, &timed_out));
  CHECK(!timed_out);
  CHECK_EQ(r, 0);
  CHECK(q->WaitWithTimeout(&r, 100, &timed_out));
  CHECK(!timed_out);
  CHECK_EQ(r, 1);
  CHECK(q->WaitWithTimeout(&r, 3500, &timed_out));
  CHECK(timed_out);
  CHECK(q->WaitWithTimeout(&r, 3500, &timed_out));
  CHECK(!timed_out);
  CHECK_EQ(r, 2);
  CHECK(q->WaitWithTimeout(&r, 3000, &timed_out));
  CHECK(!timed_out);
  CHECK_EQ(r, 3);
  VLOG(1) << "Testing StopWaiters";
  q->StopWaiters();
  CHECK(!q->WaitWithTimeout(&r, absl::Seconds(1), &timed_out));
  CHECK(!q->Wait(&r));
}

// Sleep briefly, notify <wait_started>, and Wait() for two elements.
static void SleepAndRead(AscendingPriorityWaitQueue* wq,
                         absl::Notification* wait_started) {
  absl::SleepFor(absl::Microseconds(50));
  int result;
  wait_started->Notify();
  wq->Wait(&result);
  wq->Wait(&result);
}

// Test that set_max_queue_size makes push block.
static void TestQueueSizeLimit(ThreadPool* pool) {
  // Fill up the queue so that the next push should block.
  AscendingPriorityWaitQueue wq;
  wq.set_max_queue_size(10);
  for (int k = 0; k < 10; ++k) {
    wq.push(k);
  }

  // Schedule a task in another thread and then immediately try to
  // push on another element. This should block. The other thread
  // actually sleeps a bit before notifying wait_started and calling
  // Wait() twice. There's a race condition if the sleep isn't long
  // enough or the notification comes too early, but it shouldn't be
  // too likely, and iit's just a false positive.
  absl::Notification wait_started;
  pool->Schedule(absl::bind_front(SleepAndRead, &wq, &wait_started));
  wq.push(10);
  CHECK(wait_started.HasBeenNotified());
  // This one shouldn't block.
  wq.push(11);
}

template <typename Queue>
std::vector<int> ExtractAll(Queue* q) {
  std::vector<int> result;
  int temp;
  while (!q->empty()) {
    temp = q->top();
    q->pop();
    result.push_back(temp);
  }
  return result;
}

TEST(PrioritizedWaitQueueTest, TestQueues) {
  ThreadPool pool(4, ThreadPool::Options{.thread_options = thread::Options(),
                                         .queue_capacity = 10});
  VLOG(1) << "Started pool";
  AscendingPriorityWaitQueue pwq;

  PushRanges(&pwq, &pool);
  // May not be popped in order because the threads may not have finished
  // pushing when we pop.  Order is tested below
  PopRange(&pwq, 1000);

  PushRanges(&pwq, &pool);
  TestPriorityFront(&pwq, 1000);
  TestSortedPop(&pwq);

  PushRanges(&pwq, &pool);
  WaitRange(&pwq, 1000);
  TestTimeout(&pwq, &pool);

  TestQueueSizeLimit(&pool);
}

TEST(PrioritizedWaitQueueTest, TestPrioritizedQueueCopy) {
  PrioritizedWaitQueue<int> src_queue;
  src_queue.push(5);
  src_queue.push(7);
  src_queue.push(2);

  decltype(src_queue)::container_type dst_queue;
  src_queue.CopyTo(&dst_queue);

  EXPECT_THAT(ExtractAll(&dst_queue), testing::ElementsAre(7, 5, 2));

  int temp;
  // Check the source to make sure the data is still there.
  for (int i : {7, 5, 2}) {
    EXPECT_TRUE(src_queue.Pop(&temp));
    EXPECT_EQ(temp, i);
  }
  EXPECT_TRUE(src_queue.empty());
}

TEST(PrioritizedWaitQueueTest, TestPrioritizedQueueMove) {
  struct IntPtrComp {
    bool operator()(const std::unique_ptr<int>& a,
                    const std::unique_ptr<int>& b) const {
      return *a < *b;
    }
  };

  PrioritizedWaitQueue<std::unique_ptr<int>, IntPtrComp> queue(IntPtrComp{});

  queue.push(std::make_unique<int>(5));
  queue.push(std::make_unique<int>(2));
  queue.push(std::make_unique<int>(8));

  std::unique_ptr<int> val;
  bool timed_out;
  ASSERT_TRUE(queue.Pop(&val));
  EXPECT_THAT(val, testing::Pointee(8));
  ASSERT_TRUE(queue.Wait(&val));
  EXPECT_THAT(val, testing::Pointee(5));
  ASSERT_TRUE(
      queue.WaitWithTimeout(&val, absl::InfiniteDuration(), &timed_out));
  EXPECT_THAT(val, testing::Pointee(2));
  EXPECT_FALSE(timed_out);
}

TEST(PrioritizedWaitQueueTest, TestPrioritizedQueueSwap) {
  PrioritizedWaitQueue<int> src_queue;
  src_queue.push(5);
  src_queue.push(7);
  src_queue.push(2);

  decltype(src_queue)::container_type dst_queue;
  src_queue.SwapEmptyContainer(&dst_queue);

  EXPECT_TRUE(src_queue.empty());
  EXPECT_THAT(ExtractAll(&dst_queue), testing::ElementsAre(7, 5, 2));
  EXPECT_TRUE(dst_queue.empty());
}

TEST(PrioritizedWaitQueueTest, TestPrioritizedQueuePushMany) {
  PrioritizedWaitQueue<int> src_queue;

  std::vector<int> items{1, 7, 5, 4};
  src_queue.push_many(items.begin(), items.end());

  decltype(src_queue)::container_type dst_queue;
  src_queue.SwapEmptyContainer(&dst_queue);

  EXPECT_THAT(ExtractAll(&dst_queue), testing::ElementsAre(7, 5, 4, 1));
}

TEST(PrioritizedWaitQueueTest, TestPrioritizedQueueBoundedPushMany) {
  PrioritizedWaitQueue<int> src_queue;
  src_queue.set_max_queue_size(13);

  // Partially fill the queue.
  src_queue.push(7);
  src_queue.push(5);

  const int items_to_push = 100;

  ThreadPool pool(1, ThreadPool::Options{.thread_options = thread::Options(),
                                         .queue_capacity = 1});

  // Push more items onto the queue via push_many. This will chunk the
  // insertions as capacity becomes available.
  absl::Notification notify;
  pool.Schedule([&] {
    std::vector<int> items = MakeShuffledRange(0, 100);
    src_queue.push_many(items.begin(), items.end());
    notify.Notify();
  });

  // Drain the queue in order to join the fiber.
  int val;
  for (int i = 0; i < items_to_push; ++i) {
    src_queue.Wait(&val);
  }

  notify.WaitForNotification();

  EXPECT_EQ(2, src_queue.size());
}

TEST(PrioritizedWaitQueueTest, TestPrioritizedQueuePushManyByMove) {
  struct IntPtrComp {
    bool operator()(const std::unique_ptr<int>& a,
                    const std::unique_ptr<int>& b) const {
      return *a < *b;
    }
  };
  PrioritizedWaitQueue<std::unique_ptr<int>, IntPtrComp> src_queue;

  std::vector<std::unique_ptr<int>> items;
  items.push_back(std::make_unique<int>(3));
  items.push_back(std::make_unique<int>(10));
  items.push_back(std::make_unique<int>(4));
  items.push_back(std::make_unique<int>(11));

  src_queue.push_many(std::make_move_iterator(items.begin()),
                      std::make_move_iterator(items.end()));

  std::unique_ptr<int> item;

  ASSERT_TRUE(src_queue.Pop(&item));
  EXPECT_EQ(11, *item);

  ASSERT_TRUE(src_queue.Pop(&item));
  EXPECT_EQ(10, *item);

  ASSERT_TRUE(src_queue.Pop(&item));
  EXPECT_EQ(4, *item);

  ASSERT_TRUE(src_queue.Pop(&item));
  EXPECT_EQ(3, *item);

  ASSERT_FALSE(src_queue.Pop(&item));
}

}  // namespace
