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

// A test for CancellableClosure

#include "gloop/util/callback/cancellable_closure.h"

#include <limits.h>
#include <stdio.h>
#include <unistd.h>

#include <cstdint>
#include <string>
#include <vector>

#include "absl/base/macros.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/callback.h"
#include "gloop/base/init_google.h"
#include "gloop/thread/executor.h"
#include "gloop/thread/threadpool.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/functional/to_callback.h"

// It may be useful to set -parallel=false when debugging.
ABSL_FLAG(bool, parallel, true, "run the many WaitFor/Canceltests in parallel");

// Set *x to n.
static void SetToNWithDelayMS(int n, int* x, int delay_ms) {
  if (delay_ms > 0) {
    absl::SleepFor(absl::Milliseconds(delay_ms));
  }
  *x = n;
}

static void TestSimpleRun() {
  LOG(INFO) << "=== TestSimpleRun";
  util::callback::CancellableClosure* cc;
  int x;

  // Use Run() before Unref()
  x = 0;
  cc = util::callback::CancellableClosure::New(
      ::util::functional::ToCallback([&x] { SetToNWithDelayMS(1, &x, 0); }));
  cc->Run();
  cc->Unref();
  CHECK_EQ(x, 1);

  // Use Unref() before Run()
  x = 0;
  cc = util::callback::CancellableClosure::New(
      ::util::functional::ToCallback([&x] { SetToNWithDelayMS(1, &x, 0); }));
  cc->Unref();  // should be able to call Run after the last Unref().
  cc->Run();
  CHECK_EQ(x, 1);
}

static void TestRefUnref() {
  LOG(INFO) << "=== TestRefUnref";
  util::callback::CancellableClosure* cc;
  int x;
  static const int kReferences = 100;

  // Ensure that it takes one more Unref() than we've done Ref()s
  // to keep the heap checker happy.
  x = 0;
  cc = util::callback::CancellableClosure::New(
      ::util::functional::ToCallback([&x] { SetToNWithDelayMS(1, &x, 0); }));
  cc->Run();
  CHECK_EQ(x, 1);
  for (int i = 0; i != kReferences; i++) {
    cc->Ref();
  }
  for (int i = 0; i != kReferences + 1; i++) {
    cc->Unref();
  }
}

// Parameters for a test run with WaitUntil or Cancel
struct Parameters {
  int run_delay_ms;   // delay before closure is Run() by Executor
  int run_time_ms;    // time delay in Run()
  int wait_delay_ms;  // delay before Cancel() or delay passed to WaitUntil()
  int flags;          // flags passed to WaitUntil()
};

static const int kRunInCaller =  // shorten the name
    util::callback::CancellableClosure::kRunInCaller;

// Run (*test_func)() for every combinary of parameters in the arrays
// run_delay_ms, run_time_ms, wait_delay_ms, flags.  The sizes of these in
// given by the *_count parameters.
// Create and later Executors for the tests, and wait for them all to complete.
// This routine is shared between the WaitUntil and Cancel tests
static void RunTest(void (*test_func)(thread::Executor*, Parameters,
                                      absl::Notification*),
                    const int run_delay_ms[], int run_delay_ms_count,
                    const int run_time_ms[], int run_time_ms_count,
                    const int wait_delay_ms[], int wait_delay_ms_count,
                    const int flags[], int flags_count) {
  std::vector<absl::Notification*> wait_for;
  ThreadPool* tp0 = nullptr;
  ThreadPool* tp1 = new ThreadPool(40);
  tp1->StartWorkers();
  Parameters params;
  for (int rdi = 0; rdi != run_delay_ms_count; rdi++) {
    params.run_delay_ms = run_delay_ms[rdi];
    for (int rti = 0; rti != run_time_ms_count; rti++) {
      params.run_time_ms = run_time_ms[rti];
      for (int wdi = 0; wdi != wait_delay_ms_count; wdi++) {
        params.wait_delay_ms = wait_delay_ms[wdi];
        for (int fi = 0; fi != flags_count; fi++) {
          params.flags = flags[fi];
          wait_for.push_back(new absl::Notification);
          if (absl::GetFlag(FLAGS_parallel)) {
            if (tp0 == nullptr) {
              tp0 = new ThreadPool(40);
              tp0->StartWorkers();
            }
            thread::Executor* exec = tp1;
            tp0->Schedule(
                absl::bind_front(test_func, exec, params, wait_for.back()));
          } else {
            (test_func)(tp1, params, wait_for.back());
          }
        }
      }
    }
  }
  for (int i = 0; i != wait_for.size(); i++) {
    wait_for[i]->WaitForNotification();
    delete wait_for[i];
  }
  // in case some closures delayed with AddAfter() are not yet
  // queued.
  absl::SleepFor(absl::Seconds(1));
  delete tp0;
  delete tp1;
}

// Check a single use of WaitUntil() with a given delay, wait time, and flags.
// Run the closures on *exec, and notify *done when finished.
static void TestWaitUntilSingle(thread::Executor* exec, Parameters params,
                                absl::Notification* done) {
  std::string description(
      absl::StrFormat("TestWaitUntilSingle run_delay_ms=%d "
                      "run_time_ms=%d wait_delay_ms=%d flags=%x\n",
                      params.run_delay_ms, params.run_time_ms,
                      params.wait_delay_ms, params.flags));
  // Set up the closure
  int x = 0;
  util::callback::CancellableClosure* cc =
      util::callback::CancellableClosure::New(::util::functional::ToCallback(
          absl::bind_front(&SetToNWithDelayMS, 1, &x, params.run_time_ms)));
  exec->ScheduleAfterForMigration(absl::Milliseconds(params.run_delay_ms),
                                  ::util::functional::FromCallback(cc));

  // Use WaitUntil()
  int64_t before_ms = absl::ToUnixMillis(absl::Now());
  int64_t timeout_ms = (params.wait_delay_ms == INT_MAX
                            ? util::callback::CancellableClosure::kForever
                            : params.wait_delay_ms + before_ms);
  bool result = cc->WaitUntil(timeout_ms, params.flags);
  int64_t after_ms = absl::ToUnixMillis(absl::Now());
  int64_t interval_ms = after_ms - before_ms;

  // Compute what we expect to have seen and how long it should have taken.
  bool expected_result = false;
  if ((params.flags & kRunInCaller) != 0) {
    // expect closure to have run, and delay determined by run time
    // because WaitUntil() will run the closure.
    expected_result = true;
    CHECK_LT(interval_ms, params.run_time_ms + 140);
    CHECK_LE(params.run_time_ms - 50, interval_ms);
  } else if (params.run_delay_ms < params.wait_delay_ms) {
    // expect closure to have run, and delay to be determined by
    // Executor delay plus run time
    expected_result = true;
    CHECK_LT((params.run_delay_ms + params.run_time_ms) - 50, interval_ms);
    CHECK_LT(interval_ms, (params.run_delay_ms + params.run_time_ms) + 140);
  } else {
    // expect closure not to have run, and delay determined by wait
    // argument
    CHECK_LT(params.wait_delay_ms - 50, interval_ms);
    CHECK_LT(interval_ms, params.wait_delay_ms + 140);
  }
  CHECK_EQ(result, expected_result);
  CHECK_EQ(x, result ? 1 : 0);
  CHECK(cc->WaitUntil(util::callback::CancellableClosure::kForever, 0));
  CHECK_EQ(x, 1);
  cc->Unref();
  done->Notify();
}

// Test many combinations of delays and parameters for WaitUntil.
static void TestWaitUntilAll() {
  LOG(INFO) << "=== TestWaitUntil";
  static const int run_delay_ms[] = {100, 500, 900};  // ms before Run()
  static const int run_time_ms[] = {0, 100};  // Run() takes this many ms
  static const int wait_delay_ms[] = {350, 750, INT_MAX /*infinity*/};
  // WaitUtil wait time
  static const int flags[] = {0, kRunInCaller};  // WaitUntil flags
  RunTest(&TestWaitUntilSingle, run_delay_ms, ABSL_ARRAYSIZE(run_delay_ms),
          run_time_ms, ABSL_ARRAYSIZE(run_time_ms), wait_delay_ms,
          ABSL_ARRAYSIZE(wait_delay_ms), flags, ABSL_ARRAYSIZE(flags));
}

// Check Cancel() of a closure delayed for run_delay_ms that runs for
// run_time_ms, cancelling after wait_delay_ms, and trying again after
// WaitUntil() with flags.
// Run the closures on *exec, and notify *done when finished.
static void TestCancelSingle(thread::Executor* exec, Parameters params,
                             absl::Notification* done) {
  std::string description(
      absl::StrFormat("TestCancelSingle run_delay_ms=%d "
                      "run_time_ms=%d wait_delay_ms=%d flags=%x\n",
                      params.run_delay_ms, params.run_time_ms,
                      params.wait_delay_ms, params.flags));
  // set up the closure
  int x = 0;
  util::callback::CancellableClosure* cc =
      util::callback::CancellableClosure::New(::util::functional::ToCallback(
          absl::bind_front(&SetToNWithDelayMS, 1, &x, params.run_time_ms)));
  exec->ScheduleAfterForMigration(absl::Milliseconds(params.run_delay_ms),
                                  ::util::functional::FromCallback(cc));

  absl::SleepFor(
      absl::Milliseconds(params.wait_delay_ms));  // first we sleep for a while.
  Closure* cancelled_cl;

  // try cancelling....
  util::callback::CancellableClosure::CancelResult result =
      cc->Cancel(&cancelled_cl);

  // then wait until the closure has either run on been cancelled
  CHECK(
      cc->WaitUntil(util::callback::CancellableClosure::kForever, params.flags))
      << description;

  // then try cancelling again.
  Closure* cancelled_cl2;
  util::callback::CancellableClosure::CancelResult result2 =
      cc->Cancel(&cancelled_cl2);

  // Now we compute the result we should expect.
  util::callback::CancellableClosure::CancelResult expected_result;
  util::callback::CancellableClosure::CancelResult expected_result2;
  if (params.wait_delay_ms < params.run_delay_ms) {
    // expect to have cancelled on the first try
    expected_result = util::callback::CancellableClosure::CANCELLED;
    expected_result2 = util::callback::CancellableClosure::ALREADY_CANCELLED;
  } else if (params.wait_delay_ms < params.run_delay_ms + params.run_time_ms) {
    // expect to see the closure while it's running on first try, and cancelled
    // on second try.
    expected_result = util::callback::CancellableClosure::RUNNING;
    expected_result2 = util::callback::CancellableClosure::FINISHED;
  } else {
    // expect to see the closure finished
    expected_result = util::callback::CancellableClosure::FINISHED;
    expected_result2 = util::callback::CancellableClosure::FINISHED;
  }
  CHECK_EQ(result, expected_result) << description;
  CHECK_EQ(result2, expected_result2) << description;
  if (result == util::callback::CancellableClosure::CANCELLED) {
    CHECK(cancelled_cl != nullptr) << description;
    CHECK_EQ(x, 0) << description;
    cancelled_cl->Run();
    CHECK_EQ(x, 1) << description;
  } else {
    CHECK_EQ(x, 1) << description;
    CHECK(cancelled_cl == nullptr) << description;
  }
  if (result2 == util::callback::CancellableClosure::CANCELLED) {
    CHECK(cancelled_cl2 != nullptr) << description;
    CHECK_EQ(x, 0) << description;
    cancelled_cl2->Run();
    CHECK_EQ(x, 1) << description;
  } else {
    CHECK_EQ(x, 1) << description;
    CHECK(cancelled_cl2 == nullptr) << description;
  }
  cc->Unref();
  done->Notify();
}

// Test many combinations of delays and parameters for Cancel.
static void TestCancelAll() {
  LOG(INFO) << "=== TestCancel";
  static const int run_delay_ms[] = {0, 300};  // ms before Run()
  static const int run_time_ms[] = {0, 300};   // Run() takes this many ms
  static const int wait_delay_ms[] = {150, 450, 750};  // wait before Cancel()
  static const int flags[] = {0, kRunInCaller};        // WaitUntil flags
  RunTest(&TestCancelSingle, run_delay_ms, ABSL_ARRAYSIZE(run_delay_ms),
          run_time_ms, ABSL_ARRAYSIZE(run_time_ms), wait_delay_ms,
          ABSL_ARRAYSIZE(wait_delay_ms), flags, ABSL_ARRAYSIZE(flags));
}

int main(int argc, char* argv[]) {
  InitGoogle(argv[0], &argc, &argv, true);

  TestSimpleRun();
  TestRefUnref();
  TestWaitUntilAll();
  TestCancelAll();

  printf("PASS\n");
  return 0;
}
