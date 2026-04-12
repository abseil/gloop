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

#include "gloop/thread/timedcall.h"

#include <unistd.h>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/log_severity.h"
#include "absl/functional/bind_front.h"
#include "absl/log/globals.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/walltime.h"
#include "gloop/gloop_test.h"

ABSL_CONST_INIT absl::Mutex mutex(absl::kConstInit);

static bool ReadBoolUnderMutex(const bool* b) {
  absl::MutexLock lock(mutex);
  return *b;
}

static void SetBool(bool* v) {
  LOG(INFO) << "in SetBool(" << v << ")";
  absl::MutexLock lock(mutex);
  *v = true;
}

static void Wait2Sec(void* v) {
  LOG(INFO) << "in Wait2Sec";
  sleep(2);
  if (v != nullptr) {
    LOG(INFO) << "Wait2Sec attempting to cancel self";
    TimedCall* t = reinterpret_cast<TimedCall*>(v);
    t->Set(TimedCall::Stop, nullptr);  // cancel ourselves
    ASSERT_TRUE(t->deadline() == TimedCall::Running);
  }
  LOG(INFO) << "leaving Wait2Sec";
}

struct RepeatContext {
  TimedCall* t;
  int countdown;
};

static void RepeatingCall(void* v) {
  RepeatContext* c = reinterpret_cast<RepeatContext*>(v);
  LOG(INFO) << "in RepeatingCall " << c->countdown;
  c->countdown--;
  if (c->countdown >= 0) {
    c->t->Set(base::ToWallTime(absl::Now() + absl::Milliseconds(500)),
              absl::bind_front(RepeatingCall, v));
  }
}

static void DeleteTimedCall(void* v) {
  TimedCall* d = reinterpret_cast<TimedCall*>(v);
  delete d;
}

static void ClearAndDeleteTimedCall(void* v) {
  TimedCall* d = reinterpret_cast<TimedCall*>(v);
  d->Set(TimedCall::Stop, absl::bind_front(&ClearAndDeleteTimedCall, nullptr),
         TimedCall::kNoWait);
  d->Set(TimedCall::Stop, absl::bind_front(&ClearAndDeleteTimedCall, nullptr),
         0);
  delete d;
}

static void SetTwice(void* v) {
  TimedCall* t = reinterpret_cast<TimedCall*>(v);
  t->Set(base::ToWallTime(absl::Now() + absl::Seconds(1)),
         absl::bind_front(&SetTwice, v));
  t->Set(base::ToWallTime(absl::Now() + absl::Seconds(2)),
         absl::bind_front(&SetTwice, v));
}

static void TestStopWait() {
  static const int kRepeatCount = 10000;
  LOG(INFO) << "in TestStopWait";
  TimedCall t;
  for (int i = 0; i < kRepeatCount; ++i) {
    t.Set(base::ToWallTime(absl::Now()), absl::bind_front(&SetTwice, &t));
    sched_yield();
    t.Set(TimedCall::Stop, nullptr);
    WallTime d = t.deadline();
    ASSERT_TRUE(d == TimedCall::Stop || d == TimedCall::Expired);
  }
  LOG(INFO) << "leaving TestStopWait";
}

static void InfiniteLoop(void* v) {
  TimedCall* t = reinterpret_cast<TimedCall*>(v);
  t->Set(base::ToWallTime(absl::Now() + absl::Milliseconds(10)),
         absl::bind_front(&InfiniteLoop, v));
}

static void TestDeleteRace() {
  static const int kRepeatCount = 10000;
  LOG(INFO) << "in TestDeleteRace";
  for (int i = 0; i < kRepeatCount; ++i) {
    TimedCall* t = new TimedCall();
    t->Set(base::ToWallTime(absl::Now()), absl::bind_front(&InfiniteLoop, t));
    sched_yield();
    t->Set(TimedCall::Stop, nullptr);
    delete t;
  }
  LOG(INFO) << "leaving TestDeleteRace";
}

static void TestMultipleExpirationIndependent() {
  LOG(INFO) << "in TestMultipleExpirationIndependent";
  struct Helper {
    static void DependsOn(void* arg) {
      absl::Mutex* mu = static_cast<absl::Mutex*>(arg);
      absl::SleepFor(absl::Milliseconds(50));
      mu->lock();
      mu->unlock();
    }
  };

  absl::Mutex mu1, mu2;
  TimedCall t1, t2;
  absl::Time deadline = absl::Now() + absl::Milliseconds(2);

  t1.Set(base::ToWallTime(deadline), absl::bind_front(Helper::DependsOn, &mu1));
  t2.Set(base::ToWallTime(deadline + absl::Milliseconds(1)),
         absl::bind_front(Helper::DependsOn, &mu2));

  absl::SleepFor(absl::Milliseconds(20));
  // Set()'s invariants require callers to hold no resources on which the
  // associated callback may depend.
  //
  // There was previously a bug in which the callbacks from multiple TimedCall
  // objects could be executed sequentially, before a previous waiter on Set()
  // had been released.  This resulted in a deadlock if that waiter held
  // resources depended on by another TimedCall's callback.
  //
  // The order of operations below is:
  //   t1, t2 are programmed, depending on m1, m2 respectively
  //   t1's callback expires, it delays arbitrarily
  //   main() gives t1 time to start, takes m2, then attempts to cancel t1
  //   -> which waits for the stalling t1 above
  //
  //   If t2's callback is now allowed to execute before Set() has returned, it
  //   will deadlock as main() still holds m2.
  mu2.lock();
  t1.Set(TimedCall::Stop, nullptr);
  mu2.unlock();
  LOG(INFO) << "leaving TestMultipleExpirationIndependent";
}
TEST(TimedCall, TestNumScheduled) {
  auto quickprint = []() { LOG(INFO) << "hello world"; };

  TimedCall t1(base::ToWallTime(absl::Now() + absl::Hours(1)), quickprint);

  TimedCall t2(base::ToWallTime(absl::Now() + absl::Hours(2)), quickprint);

  TimedCall t3(base::ToWallTime(absl::Now() + absl::Hours(3)), quickprint);

  ASSERT_EQ(TimedCall::NumScheduled(), 3);
  t1.Set(TimedCall::Stop, nullptr);
  ASSERT_EQ(TimedCall::NumScheduled(), 2);
  t2.Set(TimedCall::Stop, nullptr);
  ASSERT_EQ(TimedCall::NumScheduled(), 1);
}

TEST(TimedCall, Test) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  TimedCall t;
  TimedCall t2;
  TimedCall t3;
  RepeatContext c;
  bool x;
  bool y;
  WallTime delay = 0.5;
  WallTime error = 0;
  int n = 10;

  LOG(INFO) << "&x=" << &x << " &y=" << &y;

  c.t = &t3;
  c.countdown = 3;
  c.t->Set(base::ToWallTime(absl::Now() + absl::Seconds(1)),
           absl::bind_front(&RepeatingCall, &c));

  y = false;
  t2.Set(base::ToWallTime(absl::Now() + absl::Seconds(delay * n * 2) +
                          absl::Seconds(3)),
         absl::bind_front(&SetBool, &y));

  for (int i = 0; i != n; i++) {
    x = false;
    absl::Time before = absl::Now();
    t.Set(base::ToWallTime(before + absl::Seconds(delay)),
          absl::bind_front(&SetBool, &x));
    while (!ReadBoolUnderMutex(&x)) {
      sched_yield();
    }
    absl::Time after = absl::Now();
    ASSERT_GE(base::ToWallTime(after),
              base::ToWallTime(before + absl::Seconds(delay)));
    error += base::ToWallTime(after) -
             (base::ToWallTime(before + absl::Seconds(delay)));
  }
  ASSERT_LT(error, n * 0.2);

  ASSERT_TRUE(!ReadBoolUnderMutex(&y));

  // Set up to crash in 3 seconds
  t.Set(base::ToWallTime(absl::Now() + absl::Seconds(3)),
        absl::bind_front(&SetBool, nullptr));
  sleep(1);
  t.Set(TimedCall::Stop, nullptr);               // cancel
  ASSERT_TRUE(t.deadline() == TimedCall::Stop);  // should succeed
  sleep(3);

  // set up to call something that blocks 2 seconds
  t.Set(base::ToWallTime(absl::Now()), absl::bind_front(&Wait2Sec, nullptr));
  sleep(1);
  t.Set(TimedCall::Stop, nullptr);              // cancel
  ASSERT_EQ(t.deadline(), TimedCall::Expired);  // should fail to stop
                                                // timer going off as call will
                                                // already be running.
                                                // Should not see Running.

  // set up to call something that blocks 2 seconds
  t.Set(base::ToWallTime(absl::Now()), absl::bind_front(&Wait2Sec, nullptr));
  sleep(1);
  t.Set(TimedCall::Stop, nullptr, TimedCall::kNoWait);  // cancel
  ASSERT_TRUE(t.deadline() == TimedCall::Running);      // should fail to stop
                                                    // timer going off as call
                                                    // will already be running
                                                    // Should see Running,
                                                    // because we didn't wait
                                                    // for call to finish.

  // set up to call something that blocks 2 seconds but tries to
  // cancel itself while running
  t.Set(base::ToWallTime(absl::Now()), absl::bind_front(&Wait2Sec, &t));
  sleep(1);
  t.Set(TimedCall::Stop, nullptr);                  // cancel
  ASSERT_TRUE(t.deadline() == TimedCall::Expired);  // should fail to stop
                                                    // timer going off as call
                                                    // will already be running.
                                                    // Should not see Running.

  // set up TimedCalls which will delete themselves while running
  TimedCall* d = new TimedCall();
  t.Set(base::ToWallTime(absl::Now()), absl::bind_front(&DeleteTimedCall, d));
  sleep(1);
  d = new TimedCall();
  t.Set(base::ToWallTime(absl::Now()),
        absl::bind_front(&ClearAndDeleteTimedCall, d));
  sleep(1);
  // heap checker will report leak(s) if either of these fail

  while (!ReadBoolUnderMutex(&y)) {
    sched_yield();
  }

  TestStopWait();
  TestDeleteRace();

  // Check RunAt
  y = false;
  TimedCall::RunAt(base::ToWallTime(absl::Now() + absl::Seconds(0.2)),
                   absl::bind_front(&SetBool, &y));
  sleep(1);
  ASSERT_TRUE(ReadBoolUnderMutex(&y));

  TestMultipleExpirationIndependent();
}

static void DoLoopOnAlarm(TimedCall* t) {
  t->Set(base::ToWallTime(absl::Now() + absl::Nanoseconds(1)),
         [t] { DoLoopOnAlarm(t); });
}

// Historically, there was a bug where TimedCall would update the current
// processing time every loop, preventing cancellation from ever occurring if
// absl::Now() returned a value greater than the alarm time after every
// execution. This test confirms that that bug has been fixed.
TEST(TimedCall, LoopingTimedCallCanBeCancelled) {
  TimedCall t;
  t.Set(base::ToWallTime(absl::Now() + absl::Nanoseconds(1)),
        [&] { DoLoopOnAlarm(&t); });
  absl::SleepFor(absl::Milliseconds(100));
  t.Set(TimedCall::Stop, nullptr);
}
