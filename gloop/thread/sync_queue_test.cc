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

#include "gloop/thread/sync_queue.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/gloop_test.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/threadpool.h"
#include "gloop/util/random/shared_bit_gen.h"

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
  typename Q::container_type results;
  q->SwapEmptyContainer(&results);
  CHECK(q->empty());

  std::vector<typename Q::value_type> vector_results(results.begin(),
                                                     results.end());
  std::sort(vector_results.begin(), vector_results.end());
  for (std::size_t i = 0; i != high; ++i) CHECK_EQ(i, vector_results[i]);
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

TEST(ThreadSafeQueueTest, TestQueues) {
  ThreadPool pool(4, ThreadPool::Options{.thread_options = thread::Options(),
                                         .queue_capacity = 10});
  VLOG(1) << "Started pool";
  SyncQueue<int> sq;

  TestOrder(&sq);

  PushRanges(&sq, &pool);
  VLOG(1) << "Popping";
  // May not be popped in order because the threads may not have finished
  // pushing when we pop.  Order is tested below
  PopRange(&sq, 1000);

  PushRanges(&sq, &pool);
  GetAllElements(&sq, 1000);

  PushRangesRandomly(&sq, &pool);
  TestFront(&sq, 1000);
}

class CopyMoveCounter {
 public:
  CopyMoveCounter(int* copies, int* moves) : copies_(copies), moves_(moves) {}
  ~CopyMoveCounter() = default;
  CopyMoveCounter(const CopyMoveCounter& other)
      : copies_(other.copies_), moves_(other.moves_) {
    ++(*copies_);
  }
  CopyMoveCounter& operator=(const CopyMoveCounter& other) {
    copies_ = other.copies_;
    moves_ = other.moves_;
    ++(*copies_);
    return *this;
  }
  CopyMoveCounter(CopyMoveCounter&& other)
      : copies_(other.copies_), moves_(other.moves_) {
    ++(*moves_);
  }
  CopyMoveCounter& operator=(CopyMoveCounter&& other) {
    copies_ = other.copies_;
    moves_ = other.moves_;
    ++(*moves_);
    return *this;
  }

 private:
  int* copies_;
  int* moves_;
};

TEST(ThreadSafeQueueTest, SyncQueueMove) {
  SyncQueue<CopyMoveCounter> q;
  {
    int copies = 0;
    int moves = 0;
    CopyMoveCounter counter(&copies, &moves);
    q.push(counter);
    EXPECT_EQ(copies, 1);
    EXPECT_EQ(moves, 0);
  }
  {
    int copies = 0;
    int moves = 0;
    q.push(CopyMoveCounter(&copies, &moves));
    EXPECT_EQ(copies, 0);
    EXPECT_EQ(moves, 1);
  }
  {
    int copies = 0;
    int moves = 0;
    CopyMoveCounter counter(&copies, &moves);
    q.push_front(counter);
    EXPECT_EQ(copies, 1);
    EXPECT_EQ(moves, 0);
  }
  {
    int copies = 0;
    int moves = 0;
    q.push_front(CopyMoveCounter(&copies, &moves));
    EXPECT_EQ(copies, 0);
    EXPECT_EQ(moves, 1);
  }
}

}  // namespace
