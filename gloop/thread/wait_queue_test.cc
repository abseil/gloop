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

#include "gloop/thread/wait_queue.h"

#include <algorithm>
#include <cstddef>
#include <deque>
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

// Insert integers between low inclusive and high exclusive into q.
// The odd ones are pushed in FIFO order, while the even ones are pushed
// in LIFO order.
template <class Q>
static void PushRange(Q* q, int low, int high) {
  while (low != high) {
    if (low & 1) {
      q->push(low);
    } else {
      q->push_front(low);
    }
    VLOG(2) << "Pushing " << low;
    ++low;
  }
}

// Push the numbers between 0 and 999 inclusive from several threads in the
// pool.  Try to get the pushes to race and interleave with each other.
template <class Q>
static void PushRanges(Q* q, ThreadPool* pool) {
  VLOG(1) << "Adding 20-36";
  pool->Schedule([q] { PushRange<Q>(q, 20, 36); });
  VLOG(1) << "Adding 7-20";
  pool->Schedule([q] { PushRange<Q>(q, 7, 20); });
  VLOG(1) << "Adding 36-501";
  pool->Schedule([q] { PushRange<Q>(q, 36, 501); });
  VLOG(1) << "Adding 501-1000";
  pool->Schedule([q] { PushRange<Q>(q, 501, 1000); });
  VLOG(1) << "Adding 0-5";
  pool->Schedule([q] { PushRange<Q>(q, 0, 5); });
  VLOG(1) << "Adding 5-7";
  pool->Schedule([q] { PushRange<Q>(q, 5, 7); });
}

// Insert integers between low inclusive and high exclusive into q.
// The integers are inserted in a random order.
// For a PrioritizedWaitQueue<int>, then these will be re-ordered correctly.
template <class Q>
static void PushRangeRandomly(Q* q, int low, int high) {
  std::vector<int> numbers;
  for (int i = low; i < high; ++i) numbers.push_back(i);
  std::shuffle(numbers.begin(), numbers.end(), util_random::SharedBitGen());
  for (std::size_t i = 0; i < numbers.size(); ++i) {
    VLOG(2) << "Pushing " << numbers[i];
    q->push(numbers[i]);
  }
}

template <class Q>
static void PushRangesRandomly(Q* q, ThreadPool* pool) {
  VLOG(1) << "Adding 20-36";
  pool->Schedule([q] { PushRangeRandomly<Q>(q, 20, 36); });
  VLOG(1) << "Adding 7-20";
  pool->Schedule([q] { PushRangeRandomly<Q>(q, 7, 20); });
  VLOG(1) << "Adding 36-501";
  pool->Schedule([q] { PushRangeRandomly<Q>(q, 36, 501); });
  VLOG(1) << "Adding 501-1000";
  pool->Schedule([q] { PushRangeRandomly<Q>(q, 501, 1000); });
  VLOG(1) << "Adding 0-5";
  pool->Schedule([q] { PushRangeRandomly<Q>(q, 0, 5); });
  VLOG(1) << "Adding 5-7";
  pool->Schedule([q] { PushRangeRandomly<Q>(q, 5, 7); });
}

// Pop elements from q.  Make sure that exactly <high> elements were
// present and their values are all integers between 0 and high-1
// inclusive.
template <class Q>
static void PopRange(Q* q, int high) {
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

// Take all the elements from q in one operation by SwapQuque. Make sure that
// exactly <high> elements were present and their values are all integers
// between 0 and high-1 inclusive.
template <class Q>
static void GetAllElements(Q* q, std::size_t high) {
  // Give up if we don't get all the elements back from the queue
  // in 10 seconds.
  int timeout = 10;
  while (q->size() != high) {
    if (!timeout--) LOG(FATAL) << "Can't find all elements in the queue";
    VLOG(1) << "Sleeping for a second until everything is there";
    absl::SleepFor(absl::Seconds(1));
  }
  typename Q::container_type results = q->PopAll();
  CHECK(q->empty());

  std::vector<typename Q::value_type> vector_results(results.begin(),
                                                     results.end());
  std::sort(vector_results.begin(), vector_results.end());
  for (std::size_t i = 0; i != high; ++i) CHECK_EQ(i, vector_results[i]);
}

// Note: TestFront should not be called after PushRanges, but only after
// PushRangesRandomly -- this is because PushRanges can push_front and
// break it.
template <class Q>
static void TestFront(Q* q, int high) {
  std::vector<int> results;
  int timeout = 10;
  int r = 0;
  int trash;
  for (int i = 0; i < high; ++i) {
    if (i & 1) {
      while (!q->Pop(&r)) {
        if (!timeout--) LOG(FATAL) << "Can't find all elements in the queue";
        VLOG(1) << "Sleeping for a second...";
        absl::SleepFor(absl::Seconds(1));
      }
      VLOG(2) << "Popped " << r;
      results.push_back(r);
    } else {
      while (!q->Front(&r)) {
        if (!timeout--) LOG(FATAL) << "Can't find all elements in the queue";
        VLOG(1) << "Sleeping for a second...";
        absl::SleepFor(absl::Seconds(1));
      }
      CHECK(q->Pop(&trash));
      CHECK_EQ(trash, r);
      VLOG(2) << "Fronted and popped " << r;
      results.push_back(r);
    }
  }
  CHECK(q->empty());
  CHECK(!q->Front(&r));
  std::sort(results.begin(), results.end());
  for (int i = 0; i < high; ++i) CHECK_EQ(i, results[i]);
}

// Pop elements from q using the Wait method.  Make sure that exactly <high>
// elements were present and their values are all integers between 0 and
// high-1 inclusive.
template <class Q>
static void WaitRange(Q* q, int high) {
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

// Ensure that items pushed in the front are really in the front and
// ones in the back are really in the back.  This test should be run from
// a single thread, as the order becomes nondeterministic when multiple
// threads are pushing or popping things from the queue.
template <template <typename> class T>
static void TestOrder(T<int>* q) {
  VLOG(1) << "Testing order";
  CHECK(q->empty());
  PushRange(q, 0, 100);
  int r;
  for (int i = 100; i;) {
    i -= 2;
    CHECK(q->Pop(&r));
    CHECK_EQ(r, i);
  }
  for (int i = 1; i != 101; i += 2) {
    CHECK(q->Pop(&r));
    CHECK_EQ(r, i);
  }
  CHECK(q->empty());
}

// Push 0 and 1, then wait five seconds, and then push 2 and 3.
template <class Q>
static void SlowPush(Q* q) {
  q->push(0);
  q->push(1);
  absl::SleepFor(absl::Seconds(5));
  q->push(2);
  q->push(3);
}

// Test WaitWithDeadline.
template <class Q>
static void TestDeadline(Q* q, ThreadPool* pool) {
  VLOG(1) << "Testing Deadline";
  pool->Schedule([q] { SlowPush<Q>(q); });
  bool timed_out;
  int r;
  CHECK(q->WaitWithDeadline(&r, absl::Now() + absl::Milliseconds(3000),
                            &timed_out));
  CHECK(!timed_out);
  CHECK_EQ(r, 0);
  CHECK(q->WaitWithDeadline(&r, absl::Now() + absl::Milliseconds(100),
                            &timed_out));
  CHECK(!timed_out);
  CHECK_EQ(r, 1);
  CHECK(q->WaitWithDeadline(&r, absl::Now() + absl::Milliseconds(3500),
                            &timed_out));
  CHECK(timed_out);
  CHECK(q->WaitWithDeadline(&r, absl::Now() + absl::Milliseconds(3500),
                            &timed_out));
  CHECK(!timed_out);
  CHECK_EQ(r, 2);
  CHECK(q->WaitWithDeadline(&r, absl::Now() + absl::Milliseconds(3000),
                            &timed_out));
  CHECK(!timed_out);
  CHECK_EQ(r, 3);
}

// Test WaitWithTimeout.
template <class Q>
static void TestTimeout(Q* q, ThreadPool* pool) {
  VLOG(1) << "Testing Timeout";
  pool->Schedule([q] { SlowPush<Q>(q); });
  bool timed_out;
  int r;
  CHECK(q->WaitWithTimeout(&r, absl::Milliseconds(3000), &timed_out));
  CHECK(!timed_out);
  CHECK_EQ(r, 0);
  CHECK(q->WaitWithTimeout(&r, absl::Milliseconds(100), &timed_out));
  CHECK(!timed_out);
  CHECK_EQ(r, 1);
  CHECK(q->WaitWithTimeout(&r, absl::Milliseconds(3500), &timed_out));
  CHECK(timed_out);
  CHECK(q->WaitWithTimeout(&r, absl::Milliseconds(3500), &timed_out));
  CHECK(!timed_out);
  CHECK_EQ(r, 2);
  CHECK(q->WaitWithTimeout(&r, absl::Milliseconds(3000), &timed_out));
  CHECK(!timed_out);
  CHECK_EQ(r, 3);
}

// Test StopWaiters.
template <class Q>
static void TestStopWaiters(Q* q) {
  VLOG(1) << "Testing StopWaiters";
  bool timed_out;
  int r;
  q->StopWaiters();
  CHECK(!q->WaitWithDeadline(&r, absl::Now() + absl::Seconds(1), &timed_out));
  CHECK(!q->WaitWithTimeout(&r, absl::Seconds(1), &timed_out));
  CHECK(!q->Wait(&r));
}

// Test that the default max_queue_size_ is correct
// and that set_max_queue_size sets and max_queue_size
// returns the max_queue_size_.
template <class Qtype>
static void TestMaxQueueSizeChange() {
  Qtype wq;
  CHECK_EQ(wq.max_queue_size(), Qtype::kInfiniteQueueSize);
  wq.set_max_queue_size(10);
  CHECK_EQ(wq.max_queue_size(), 10);
}

// Sleep briefly, notify <wait_started>, and Wait() for two elements.
template <class Q>
static void SleepAndRead(Q* wq, absl::Notification* wait_started) {
  absl::SleepFor(absl::Microseconds(50));
  int result;
  wait_started->Notify();
  wq->Wait(&result);
  wq->Wait(&result);
}

// Test that set_max_queue_size makes push block.
template <class Qtype>
static void TestQueueSizeLimit(ThreadPool* pool) {
  // Fill up the queue so that the next push should block.
  Qtype wq;
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
  pool->Schedule(absl::bind_front(SleepAndRead<Qtype>, &wq, &wait_started));
  wq.push(10);
  CHECK(wait_started.HasBeenNotified());
  // This one shouldn't block.
  wq.push(11);
}

// Test interaction between push_[front_]nowait and set_max_queue_size.
template <class Qtype>
static void TestNoWait() {
  // Fill up the queue.
  Qtype wq;
  int result;
  wq.set_max_queue_size(10);
  for (int k = 0; k < 10; ++k) {
    wq.push(k);
  }

  // These should return false since the queue is full.
  CHECK(!wq.push_nowait(0));
  CHECK(!wq.push_front_nowait(0));

  // Make some room.
  CHECK(wq.Pop(&result));
  CHECK_EQ(0, result);
  CHECK(wq.Pop(&result));
  CHECK_EQ(1, result);

  // These should succeed.
  CHECK(wq.push_nowait(10));
  CHECK(wq.push_front_nowait(1));

  // Check queue contents.
  for (int i = 1; i < 11; ++i) {
    CHECK(wq.Pop(&result));
    CHECK_EQ(i, result);
  }
  // Queue should be empty now.
  CHECK(!wq.Pop(&result));
}

template <class Qtype>
static void TestPushAndDrop() {
  // Fill up the queue.
  Qtype wq;
  int result;
  wq.set_max_queue_size(3);
  for (int k = 0; k < 3; ++k) {
    wq.push(k);
  }

  // Drop should drop 0. And the front should now be 1.
  CHECK(wq.Drop());
  CHECK(wq.Pop(&result));
  CHECK_EQ(1, result);

  // Drop should now drop 2, making the queue empty.
  CHECK(wq.Drop());
  CHECK(!wq.Drop());
}

template <class Q>
static void WriteAndBlock(int low, int high, Q* wq,
                          absl::Notification* write_finished) {
  PushRange(wq, low, high);
  write_finished->Notify();
}

template <class Qtype>
static void TestSwapWithQueueSizeLimit(ThreadPool* pool) {
  // Fill up the queue so that the next push should block.
  Qtype wq;
  wq.set_max_queue_size(10);
  for (int k = 0; k < 10; ++k) {
    wq.push(k);
  }
  // Schedule two tasks in two different threads that immediately try to
  // push elements in the queue. Both threads should block. We then consume all
  // the elements in that queue by "PopAll". This should unblock both threads.
  absl::Notification write_finished_1;
  absl::Notification write_finished_2;
  pool->Schedule(
      absl::bind_front(WriteAndBlock<Qtype>, 11, 13, &wq, &write_finished_1));
  pool->Schedule(
      absl::bind_front(WriteAndBlock<Qtype>, 13, 16, &wq, &write_finished_2));
  absl::SleepFor(absl::Seconds(2));  // PushRange should be called by then.

  CHECK(!write_finished_1.HasBeenNotified());
  CHECK(!write_finished_2.HasBeenNotified());
  typename Qtype::container_type c = wq.PopAll();
  absl::SleepFor(
      absl::Seconds(2));  // This should be enough time for unblocking to happen
  CHECK(write_finished_1.HasBeenNotified());
  CHECK(write_finished_2.HasBeenNotified());
}

TEST(WaitQueueTest, TestQueues) {
  ThreadPool pool(4, ThreadPool::Options{.thread_options = thread::Options(),
                                         .queue_capacity = 10});
  VLOG(1) << "Started pool";
  WaitQueue<int> wq;

  PushRanges(&wq, &pool);
  VLOG(1) << "Popping";
  // May not be popped in order because the threads may not have finished
  // pushing when we pop.  Order is tested below
  PopRange(&wq, 1000);

  PushRanges(&wq, &pool);
  GetAllElements(&wq, 1000);

  PushRangesRandomly(&wq, &pool);
  TestFront(&wq, 1000);

  PushRanges(&wq, &pool);
  WaitRange(&wq, 1000);
  TestOrder(&wq);
  TestDeadline(&wq, &pool);
  TestTimeout(&wq, &pool);
  TestStopWaiters(&wq);

  TestMaxQueueSizeChange<WaitQueue<int>>();
  TestQueueSizeLimit<WaitQueue<int>>(&pool);
  TestNoWait<WaitQueue<int>>();
  TestPushAndDrop<WaitQueue<int>>();
  TestSwapWithQueueSizeLimit<WaitQueue<int>>(&pool);
}

TEST(WaitQueueTest, TestWaitQueueCopy) {
  WaitQueue<int> queue;
  queue.push(10);
  queue.push(20);

  std::deque<int> deque;
  queue.CopyTo(&deque);
  EXPECT_EQ(10, deque.at(0));
  EXPECT_EQ(20, deque.at(1));

  // Verify that the WaitQueue retains its original contents.
  EXPECT_EQ(2, queue.size());
  int popped;
  EXPECT_TRUE(queue.Pop(&popped));
  EXPECT_EQ(10, popped);
  EXPECT_TRUE(queue.Pop(&popped));
  EXPECT_EQ(20, popped);
  EXPECT_TRUE(!queue.Pop(&popped));
}

TEST(WaitQueueTest, TestWaitQueueMove) {
  WaitQueue<std::unique_ptr<int>> queue;
  queue.push(std::make_unique<int>(5));

  std::unique_ptr<int> val;
  ASSERT_TRUE(queue.Pop(&val));
  EXPECT_THAT(val, testing::Pointee(5));
}

}  // namespace
