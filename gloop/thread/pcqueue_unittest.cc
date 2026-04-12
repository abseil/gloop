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

#include <cstdint>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#undef NDEBUG

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include "gloop/gloop_test.h"
#include "gloop/thread/pcqueue.h"
#include "gloop/thread/thread.h"

namespace thread {

static pthread_mutex_t counter_lock = PTHREAD_MUTEX_INITIALIZER;
static int counter = 0;
static void increment_counter() {
  pthread_mutex_lock(&counter_lock);
  counter++;
  if ((counter % 10000) == 0) {
    fprintf(stderr, "Finished %d\n", counter);
  }
  pthread_mutex_unlock(&counter_lock);
}

// Item of work to be done
struct WorkItem {
  int index;
  bool done;
  bool special;

  WorkItem() {
    index = 0;
    done = false;
    special = false;
  }
};

// Producer thread
class ProducerThread : public Thread {
 private:
  ProducerConsumerQueue<void*>* queue;
  WorkItem* work;
  int start_;
  int length;

 public:
  ProducerThread(ProducerConsumerQueue<void*>* q, WorkItem* w, int s, int l) {
    queue = q;
    work = w;
    start_ = s;
    length = l;
  }

 protected:
  virtual void Run() {
    for (int i = 0; i < length; i++) {
      queue->Put(&work[start_ + i]);
      // System.err.println("Sent " + work[start+i].index);
    }
    // System.err.println("Finished producer " + start);
  }
};

// Consumer thread: gets work to do and does it
class ConsumerThread : public Thread {
 protected:
  ProducerConsumerQueue<void*>* queue;
  int me;

 public:
  ConsumerThread(ProducerConsumerQueue<void*>* q, int m) {
    queue = q;
    me = m;
  }

 protected:
  virtual void Run() {
    WorkItem* item;
    while ((item = (WorkItem*)queue->Get()) != nullptr) {
      // System.err.println("Recv " + item.index);
      ASSERT_TRUE(!item->done);
      item->done = true;
      increment_counter();
    }
    // System.err.println("Finished consumer " + me);
  }
};

TEST(ProducerConsumerQueueTest, TestHugeQueue) {
  // ASSERT_TRUE that a huge producer-consumer queue can be created without
  // using too much memory.
  ProducerConsumerQueue<void*>* large_queue = new ProducerConsumerQueue<void*>(
      ProducerConsumerQueue<void*>::kUnbounded);
  for (intptr_t i = 0; i < 1000000; i++) {
    large_queue->Put(reinterpret_cast<char*>(i));
  }
  for (intptr_t i = 0; i < 1000000; i++) {
    ASSERT_EQ(i, reinterpret_cast<intptr_t>(large_queue->Get()));
  }
  delete large_queue;
}

TEST(ProducerConsumerQueueTest, TestRunningQueue) {
  // Initialize data
  const int num_producers = 10;
  const int num_consumers = 20;
  const int N = 100000 / num_producers;
  const int num_items = num_producers * N;

  ProducerThread** producers = new ProducerThread*[num_producers];
  ConsumerThread** consumers = new ConsumerThread*[num_consumers];
  WorkItem* items = new WorkItem[num_items];
  for (int i = 0; i < N; i++) {
    items[i].special = true;
  }
  ProducerConsumerQueue<void*>* queue = new ProducerConsumerQueue<void*>(10);
  for (int i = 0; i < num_producers; i++) {
    producers[i] = new ProducerThread(queue, items, i * N, N);
    producers[i]->SetJoinable(true);
  }
  for (int i = 0; i < num_consumers; i++) {
    consumers[i] = new ConsumerThread(queue, i);
    consumers[i]->SetJoinable(true);
  }
  for (int i = 0; i < num_items; i++) {
    items[i].index = i;
  }

  // Fork threads
  fprintf(stderr, "Forking producers\n");
  for (int i = 0; i < num_producers; i++) {
    producers[i]->Start();
  }
  fprintf(stderr, "Forking consumers\n");
  for (int i = 0; i < num_consumers; i++) {
    consumers[i]->Start();
  }

  // Wait for producers to be done
  fprintf(stderr, "Waiting for producers to finish\n");
  for (int i = 0; i < num_producers; i++) {
    producers[i]->Join();
  }

  // Send termination values to consumers and wait for them to finish
  fprintf(stderr, "Terminating consumers\n");
  for (int i = 0; i < num_consumers; i++) {
    queue->Put(nullptr);  // "null" means stop
  }
  fprintf(stderr, "Waiting for consumers to finish\n");
  for (int i = 0; i < num_consumers; i++) {
    consumers[i]->Join();
  }

  // Ensure that all work is done
  fprintf(stderr, "ASSERTing that all work got done\n");
  for (int i = 0; i < num_items; i++) {
    ASSERT_TRUE(items[i].done);
  }
  fprintf(stderr, "All work done\n");

  delete queue;
  delete[] items;
  for (int i = 0; i < num_producers; i++) {
    delete producers[i];
  }
  for (int i = 0; i < num_consumers; i++) {
    delete consumers[i];
  }
  delete[] producers;
  delete[] consumers;
}

TEST(ProducerConsumerQueueTest, TestPutIfReadyToRun) {
  ProducerConsumerQueue<void*>* queue = new ProducerConsumerQueue<void*>(10);

  WorkItem w;

  ASSERT_FALSE(queue->PutIfReadyToRun(&w));

  ConsumerThread* s = new ConsumerThread(queue, 0);
  s->SetJoinable(true);
  s->Start();

  sleep(5);

  ASSERT_TRUE(queue->PutIfReadyToRun(&w));

  queue->Put(nullptr);
  s->Join();

  delete s;
  delete queue;
}

TEST(ProducerConsumerQueueTest, TestForcePut) {
  ProducerConsumerQueue<void*>* queue = new ProducerConsumerQueue<void*>(2);

  WorkItem w;
  queue->Put(&w);
  queue->Put(&w);

  ASSERT_TRUE(!queue->TryPut(&w));  // because queue is full now.
  queue->ForcePut(&w);              // ignores queue full, shouldn't block

  ASSERT_EQ(queue->count(), 3);
  delete queue;
}

TEST(ProducerConsumerQueueTest, TestTimeout) {
  ProducerConsumerQueue<void*> queue(1);
  void* item;
  auto start = absl::Now();
  EXPECT_FALSE(queue.GetWithTimeout(&item, 100));
  auto elapsed = absl::Now() - start;
  EXPECT_GE(elapsed, absl::Milliseconds(100));

  queue.Put(&item);
  start = absl::Now();
  EXPECT_TRUE(queue.GetWithTimeout(&item, 1000));
  elapsed = absl::Now() - start;
  EXPECT_LT(elapsed, absl::Milliseconds(1000));
  EXPECT_EQ(item, &item);
}

}  // namespace thread

static void BM_PutGet(benchmark::State& state) {
  thread::ProducerConsumerQueue<void*> q(
      thread::ProducerConsumerQueue<void*>::kUnbounded);
  for (auto _ : state) {
    q.Put(nullptr);
    q.Get();
  }
}
BENCHMARK(BM_PutGet);
