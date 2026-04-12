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

#include "gloop/thread/periodicclosure.h"

#include <functional>
#include <vector>

#include "absl/functional/bind_front.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/simulated_clock.h"
#include "absl/time/time.h"
#include "gloop/base/callback.h"
#include "gloop/gloop_test.h"
#include "gloop/util/callback/blocking_callback.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/functional/to_callback.h"

namespace thread {
namespace {

bool HasBeenCalled(BlockingClosure* closure) {
  const int immediate_timeout = 0;
  return closure->WaitForNumCalled(1, immediate_timeout);
}

void BlockingFunction(absl::Mutex* mu, bool* running) {
  {
    absl::MutexLock lock(*mu);
    *running = true;
  }

  absl::SleepFor(absl::Milliseconds(50));
}

TEST(PeriodicClosureTest, ObeyInterval) {
  const absl::Duration kPeriod = absl::Milliseconds(100);
  const int kCalls = 10;
  const absl::Duration timeout = (kPeriod * kCalls);

  int actual_calls = 0;
  PeriodicClosure pc(
      [&actual_calls]() {
        ++actual_calls;
        absl::SleepFor(absl::Milliseconds(10));
      },
      kPeriod);

  pc.Start();
  absl::SleepFor(timeout);
  pc.Stop();

  // The closure could get called up to kCalls+1 times: once at time 0, once
  // at time kPeriod, once at time kPeriod*2, up to once at time
  // kPeriod*kCalls.  It could be called fewer times if, say, the machine is
  // overloaded, so let's check that:
  //   (kCalls - 5) <= actual_calls <= (kCalls + 1).
  EXPECT_LE(kCalls - 5, actual_calls);
  EXPECT_LE(actual_calls, kCalls + 1);
}

TEST(PeriodicClosureTest, SetInterval) {
  const absl::Duration kInitialPeriod = absl::Milliseconds(100);

  absl::Mutex mu;
  int initial_calls = 0;
  int calls = 0;

  PeriodicClosure pc(
      [&mu, &calls]() {
        absl::MutexLock lock(mu);
        ++calls;
      },
      kInitialPeriod);

  EXPECT_EQ(pc.Interval(), kInitialPeriod);

  pc.Start();

  // Check that firing on multiple intervals works.
  for (const absl::Duration interval :
       std::vector<absl::Duration>{kInitialPeriod, 2 * kInitialPeriod}) {
    pc.SetInterval(interval);
    EXPECT_EQ(pc.Interval(), interval);
    absl::SleepFor(interval * 5);
    absl::MutexLock lock(mu);
    EXPECT_GE(calls - initial_calls, 4);  // Room for some timing inconsistency.
    initial_calls = calls;
  }
  pc.Stop();
}

// Test for startup delay
TEST(PeriodicClosureTest, ObeyStartupDelay) {
  const absl::Duration kDelay = absl::Seconds(1);
  const absl::Duration kTimeout = kDelay / 2;
  const absl::Duration kPeriod = kDelay / 10;

  BlockingClosure* closure = new BlockingClosure;
  PeriodicClosureOptions pco;
  pco.set_startup_delay(kDelay);
  PeriodicClosure pc(util::functional::FromCallbackWithOwnership(closure),
                     kPeriod, pco);

  pc.Start();
  // Give some time for the thread to start up.
  absl::SleepFor(kTimeout);
  // Closure shouldn't have been called yet.
  EXPECT_FALSE(HasBeenCalled(closure));
  // Give enough time for startup delay to expire.
  absl::SleepFor(kDelay);
  pc.Stop();

  // Closure should have been called at least once.
  EXPECT_TRUE(HasBeenCalled(closure));
}

// Test for deadlock by calling RunNow() while the closure is already running
TEST(PeriodicClosureTest, RunNow) {
  absl::Mutex mu;
  bool running = false;

  Closure* c = ::util::functional::ToPermanentCallback(
      absl::bind_front(BlockingFunction, &mu, &running));
  PeriodicClosure pc(util::functional::FromCallbackWithOwnership(c),
                     absl::Milliseconds(10));

  pc.Start();

  {
    absl::MutexLock lock(mu);
    mu.Await(absl::Condition(&running));  // wait for the function to start
  }

  // trigger a forced run while we know the closure is running
  pc.RunNow();

  pc.Stop();
}

TEST(PeriodicClosureTest, Restart) {
  BlockingClosure* listener = new BlockingClosure;
  PeriodicClosure pc(util::functional::FromCallbackWithOwnership(listener),
                     absl::Milliseconds(100));

  pc.Start();
  ASSERT_TRUE(listener->WaitForNumCalled(1, 200 /* ms */));
  pc.Stop();

  listener->Reset();

  // re-Start() the PeriodicClosure to make sure all is still ok.
  pc.Start();
  ASSERT_TRUE(listener->WaitForNumCalled(1, 200 /* ms */));
  pc.Stop();
}

// If this test hangs forever, its probably a deadlock caused by setting the
// PeriodicClosure's interval to 0ms.
TEST(PeriodicClosureTest, MinInterval) {
  PeriodicClosure pc([] { absl::SleepFor(absl::Milliseconds(20)); },
                     absl::ZeroDuration());

  pc.Start();
  pc.Stop();  // we should be able to Stop()

  pc.Start();
  pc.RunNow();  // we should be able to call non-trivial methods
  pc.Stop();
}

TEST(PeriodicClosureTest, ThreadOptionsJoinable) {
  PeriodicClosureOptions pco;
  pco.mutable_thread_options()->set_joinable(false);
  pco.set_name_prefix("not_joinable_thread_options");
  PeriodicClosure pc([] {}, absl::ZeroDuration(), pco);

  pc.Start();
  pc.Stop();  // we should be able to Stop()
}

// Block until *run is true.  *run is protected by *mu.
static void ProceedWhenTrue(absl::Mutex* mu, bool* run) {
  mu->LockWhen(absl::Condition(run));
  mu->unlock();
}

TEST(PeriodicClosureTest, RunSoonDoesNotBlock) {
  absl::Mutex mu;    // Protects run.
  bool run = false;  // Controlling (blocking) the closure
  // run=false initially because we want the closure to hang
  PeriodicClosure pc(absl::bind_front(&ProceedWhenTrue, &mu, &run),
                     absl::ZeroDuration());
  pc.Start();
  pc.RunSoon();  // Must not block
  mu.lock();
  run = true;  // allow the closure to proceed
  mu.unlock();
  pc.Stop();  // we should be able to Stop()
}

class PeriodicClosureWithSimulatedClockTest : public testing::Test {
 protected:
  PeriodicClosureWithSimulatedClockTest()
      : counter_(0),
        pc_(absl::bind_front(Inc, &counter_mu_, &counter_),
            absl::Milliseconds(50), GetPeriodicClosureOptions()) {}

  PeriodicClosureOptions GetPeriodicClosureOptions() {
    return PeriodicClosureOptions().set_name_prefix("ignore").set_clock(
        &clock_);
  }

  virtual void SetUp() {
    pc_.Start();
    // Note: counter_ gets initially incremented at time 0.
    ASSERT_TRUE(AwaitCountWithTimeout(1));
  }

  virtual void TearDown() { pc_.Stop(); }

  static void Inc(absl::Mutex* mu, int* i) {
    absl::MutexLock lock(*mu);
    ++(*i);
  }

  static bool AreEqual(int* counter, int expected_counter) {
    return *counter == expected_counter;
  }

  // The SimulatedClock tests below advance simulated time and then expect the
  // PeriodicClosure thread to run its closure.  This method helps the tests
  // wait for a not-unbounded amount of time for the PeriodicClosure thread to
  // execute.  The timeout is large to give the PeriodicClosure thread enough
  // time to get scheduled even on a heavily loaded machine.
  bool AwaitCountWithTimeout(int expected_counter) {
    absl::MutexLock lock(counter_mu_);
    std::function<bool()> are_equal =
        absl::bind_front(AreEqual, &counter_, expected_counter);
    const absl::Duration kMaxWaitForOtherThread = absl::Milliseconds(1000);
    return counter_mu_.AwaitWithTimeout(absl::Condition(&are_equal),
                                        kMaxWaitForOtherThread);
  }

  absl::SimulatedClock clock_;
  absl::Mutex counter_mu_;
  int counter_;
  PeriodicClosure pc_;
};

TEST_F(PeriodicClosureWithSimulatedClockTest, FasterThanRealTime) {
  clock_.AdvanceTime(absl::Milliseconds(25));
  for (int i = 2; i < 7; ++i) {
    clock_.AdvanceTime(absl::Milliseconds(50));  // advance past a tick
    EXPECT_TRUE(AwaitCountWithTimeout(i));
  }
}

TEST_F(PeriodicClosureWithSimulatedClockTest, SlowerThanRealTime) {
  absl::SleepFor(absl::Milliseconds(125));  // wait for any unexpected breakage
  EXPECT_EQ(1, counter_);
}

TEST_F(PeriodicClosureWithSimulatedClockTest, RunNow) {
  pc_.RunNow();
  EXPECT_TRUE(AwaitCountWithTimeout(2));
}

TEST_F(PeriodicClosureWithSimulatedClockTest, RunSoon) {
  pc_.RunSoon();
  EXPECT_TRUE(AwaitCountWithTimeout(2));
}

#if GTEST_HAS_DEATH_TEST
TEST(PeriodicClosureDeathTest, BadInterval) {
  GLOOP_EXPECT_DEATH(PeriodicClosure pc([] {}, absl::Milliseconds(-1)),
                     ".* should be >= 0");

  GLOOP_EXPECT_DEATH(PeriodicClosure pc([] {}, absl::Milliseconds(-1),
                                        PeriodicClosureOptions()),
                     ".* should be >= 0");
}

TEST(PeriodicClosureDeathTest, NotStopped) {
  PeriodicClosure* pc = new PeriodicClosure([] {}, absl::Milliseconds(10));

  pc->Start();
  GLOOP_ASSERT_DEATH(delete pc, ".* before destructed");

  pc->Stop();
  delete pc;
}

TEST(PeriodicClosureDeathTest, DoubleStart) {
  PeriodicClosure pc([] {}, absl::Milliseconds(10));

  pc.Start();
  GLOOP_ASSERT_DEATH(pc.Start(), ".* already running");

  pc.Stop();
}

TEST(PeriodicClosureDeathTest, DoubleStop) {
  PeriodicClosure pc([] {}, absl::Milliseconds(10));

  pc.Start();

  pc.Stop();
  GLOOP_ASSERT_DEATH(pc.Stop(), ".* not running");
}
#endif  // GTEST_HAS_DEATH_TEST

}  // namespace
}  // namespace thread
