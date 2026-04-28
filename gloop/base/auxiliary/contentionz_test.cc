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

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/base/dynamic_annotations.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/strings/match.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/auxiliary/synchronization_profiling.h"
#include "gloop/base/auxiliary/synchronization_profiling_test_util.h"
#include "gloop/base/auxiliary/synchronization_tags.h"
#include "gloop/base/profile.h"
#include "gloop/strings/strip.h"
#include "gloop/thread/fiber/bundle.h"
#include "gloop/thread/threadpool.h"
#include "gtest/gtest.h"

namespace lock_profiling_test {

// Verifier class to handle reading the contention data and then to
// determine whether the results of the running the test match the
// expected values.
class ContentionVerifier {
 public:
  explicit ContentionVerifier(int number_of_threads_tested)
      : number_of_threads_(number_of_threads_tested) {}

  // Check the profiling output to verify the data was all collected
  // correctly. The profile contains Context information if expect_context
  // is true.
  void CheckProfileOutput(const LockHolder& holder, bool expect_context);

  // Validate the observed amount of contention.
  void ValidateContentionPercentage(double wait_percentage,
                                    const LockHolder& holder);

 private:
  static constexpr int kSymbolNameLength = 256;

  // Embedded class for aggregated contention data.
  struct ContentionResult {
    absl::Duration contention_time = absl::ZeroDuration();
    int64_t total_samples = 0;
    absl::Duration eviction_time = absl::ZeroDuration();
    int64_t evicted_samples = 0;
    // Track amount of contention time contributed by locks when
    // Context information is present.
    absl::Duration contention_context_time = absl::ZeroDuration();
    absl::Duration evicted_contention_context_time = absl::ZeroDuration();
    bool recognized_method = false;  // Whether target method was found or not.
    std::string expected_method_name;
  };

  // We do some testing with high thread counts. With high thread counts we
  // generate multiple different call stacks so that we can test eviction of
  // samples from the table. High thread counts also introduce more variability
  // in the results, so we have a more lenient way of evaluating success.
  static constexpr int kHighThreadCount = 100;

  // An adjustment percent to the lower bound for the contention percentage. Due
  // to timing issues, the actual contention observed may be off slightly
  // from what theoretically should be the contention.
  static constexpr double kContentionAdjustmentLowerBound = 0.05;

  // An adjustment percent for the upper bound.  The adjustment for the upper
  // bound is much larger than the lower bound as the maximum contention
  // profiled may be much larger than the theoretical maximum due to some
  // threads contending before the start of the measurement (will happen on a
  // busy system).  If some threads start to contend much sooner than others,
  // the profiled contention time (numerator for the percentage) will increase,
  // but the test run time (denominator), may not change much, so the overall
  // percentage will be higher than predicted.
  static constexpr double kContentionAdjustmentUpperBound = 0.50;

  // Parse the reported contended stacks and report contention time and samples.
  // Using the profile API.
  ContentionResult GetContentionFromProfile(const LockHolder& holder,
                                            absl::Duration* duration,
                                            int64_t* sample_period);

  // Validate the reported results.
  void ValidateResults(const ContentionResult& results,
                       absl::Duration time_since_reset, int64_t sample_period,
                       bool expect_context, const LockHolder& holder);

  int number_of_threads_;
};

ContentionVerifier::ContentionResult
ContentionVerifier::GetContentionFromProfile(const LockHolder& holder,
                                             absl::Duration* duration,
                                             int64_t* sample_period) {
  ContentionResult results;
  results.expected_method_name = holder.ExpectedUnlockMethodName();

  std::unique_ptr<base::Profile> profile =
      base::GetMutexProfile(sample_period, duration);

  // Sample period is set to 1 to capture every event. Check that the expected
  // value is returned.
  EXPECT_EQ(absl::GetFlag(FLAGS_synch_profile_period), *sample_period);
  EXPECT_EQ(1, *sample_period);

  profile->Iterate(&results, [](void* arg, const base::Profile::Entry& e) {
    auto results = reinterpret_cast<ContentionResult*>(arg);
    // Check for the expected number of tags to support Context info.
    CHECK_GE(e.ntags,
             static_cast<int>(base::ContentionzIndexOffsets::kNumTags));
    bool has_context = e.tags[static_cast<int>(
        base::ContentionzIndexOffsets::kHasContextIndex)];

    for (int s = 0; s < e.depth; s++) {
      // Convert address in stack to symbol.
      char symbol[kSymbolNameLength];
      std::string symbol_string;
      absl::Symbolize(reinterpret_cast<void*>(e.stack[s]), symbol,
                      kSymbolNameLength - 1);
      symbol_string = symbol;
      strings::TrimStringRight(&symbol_string, "()");
      // Break out of loop if the target lock function is found.
      if (absl::StrContains(symbol_string, results->expected_method_name)) {
        results->recognized_method = true;
        results->contention_time += absl::Microseconds(e.tags[static_cast<int>(
            base::ContentionzIndexOffsets::kMicrosIndex)]);
        results->total_samples += e.tags[static_cast<int>(
            base::ContentionzIndexOffsets::kCountIndex)];
        if (has_context) {
          results->contention_context_time +=
              absl::Microseconds(e.tags[static_cast<int>(
                  base::ContentionzIndexOffsets::kMicrosIndex)]);
        }
        break;
      }
      // Break out of the loop if this is evicted contention data.
      if (absl::StrContains(symbol_string, "LostContentionData")) {
        results->eviction_time += absl::Microseconds(e.tags[static_cast<int>(
            base::ContentionzIndexOffsets::kMicrosIndex)]);
        results->evicted_samples += e.tags[static_cast<int>(
            base::ContentionzIndexOffsets::kCountIndex)];
        if (has_context) {
          results->evicted_contention_context_time +=
              absl::Microseconds(e.tags[static_cast<int>(
                  base::ContentionzIndexOffsets::kMicrosIndex)]);
        }
        break;
      }
    }
  });
  return results;
}

void ContentionVerifier::CheckProfileOutput(const LockHolder& holder,
                                            bool expect_context) {
  absl::Duration time_since_reset;
  int64_t sample_period;

  ContentionResult profile_result =
      GetContentionFromProfile(holder, &time_since_reset, &sample_period);

  // Validate results from profile.
  ValidateResults(profile_result, time_since_reset, sample_period,
                  expect_context, holder);
}

// A test class to verify the contentionz metrics are calculated correctly
// for Mutex and Spinlock lock types.
class ContentionzTest : public ::testing::Test {
 protected:
  ContentionzTest() : callstack_(nullptr), lock_holder_(nullptr) {
    // Need to set the profile period to 1 to make sure all samples are
    // collected to allow for better checking accuracy.
    absl::SetFlag(&FLAGS_synch_profile_period, 1);
  }
  ~ContentionzTest() override { absl::SetFlag(&FLAGS_synch_profile_period, 0); }

  // Create appropriate Callstack and Lock objects.
  void Create(enum TestLockType lock_type, enum CallstackType callstack_type) {
    lock_holder_ = std::make_unique<LockHolder>(lock_type);
    callstack_.reset(CallstackInterface::New(callstack_type));
  }

  // Run all of the contention tests.
  void RunAll();

  // Test profile generation for a specified number of threads and lock type
  // potentially testing for evictions from the cache.
  void TestGenericProfileGeneration(int number_of_threads_to_test,
                                    enum TestLockType lock_type,
                                    enum CallstackType callstack_type);

  // Test stack caching.
  void TestStackCaching(int num_threads);

  // Executes num_fibers concurrently and returns the contention time measured
  // directly in the function.
  absl::Duration ExecuteFibers(int num_fibers, int num_loops,
                               absl::Duration sleep_duration);

 public:
  // A long hold time for holding the contended lock. Making this value large
  // helps to greatly reduce false positives in testing as there is less chance
  // of a thread not contending for the lock for the entire duration of the
  // test.
  static constexpr absl::Duration kLongHoldTime = absl::Milliseconds(50);

  // A short hold time makes the test run faster.
  static constexpr absl::Duration kShortHoldTime = absl::Milliseconds(3);

 private:
  // Entry point to call into the lock holder.
  void CallLockHolder(int num_threads, absl::Duration hold_time) {
    callstack_->CallLockHolder(num_threads, hold_time, lock_holder_.get());
  }

  // Sets up and runs threadpool over workload. Note that no thread returns
  // from this function until all the threads have completed the contention
  // test. Hence time it takes to complete the test can be measured over
  // this function call.
  void RunThreadPool(int num_threads);

  // Execute num_threads concurrently and return the contention percentage.
  double ExecuteConcurrentThreads(int num_threads,
                                  enum CallstackType callstack_type);

  std::unique_ptr<CallstackInterface> callstack_;
  std::unique_ptr<LockHolder> lock_holder_;
};

void ContentionVerifier::ValidateResults(const ContentionResult& results,
                                         absl::Duration time_since_reset,
                                         int64_t sample_period,
                                         bool expect_context,
                                         const LockHolder& holder) {
  if (number_of_threads_ == 1) {
    // A single thread means no contention can be generated.  Therefore, no
    // cycles or samples should be found.
    EXPECT_EQ(results.contention_time, absl::ZeroDuration());
    EXPECT_EQ(results.total_samples, 0);
    return;
  }

  // The time_since_reset should be within the range:
  // [kHoldTime + kShortHoldTime * number_of_threads_tested,
  //  runtime_ in milliseconds]
  // However, values can be off slightly, so the actual values tested
  // are adjusted very slightly from above to expand the range and prevent
  // false test failures.
  EXPECT_LE(ContentionzTest::kLongHoldTime + ContentionzTest::kShortHoldTime *
                                                 (number_of_threads_ - 1) *
                                                 0.99,
            time_since_reset);
  EXPECT_GE(holder.GetRuntime() + absl::Milliseconds(2), time_since_reset);

  // Check that we saw the expected method in at least one stack trace.
  EXPECT_TRUE(results.recognized_method);

  // Should have been some contention, so make sure the amount of contention
  // makes sense as well as the stack trace.
  // When there are large numbers of threads running it is possible to get some
  // evicted contention from the background threads, so the total could
  // be larger than the number of threads - 1, so do a more relaxed test.
  if (number_of_threads_ <= kHighThreadCount) {
    EXPECT_EQ(number_of_threads_ - 1, results.total_samples);
  } else {
    // At high numbers of threads some contention points will be dropped,
    // so include dropped samples in the comparison. There may also be
    // some samples from dropped contention elsewhere in the test. Hence
    // there may get more dropped samples than the number of threads would
    // indicate.
    EXPECT_LE(number_of_threads_ - 1,
              results.total_samples + results.evicted_samples);
  }

  double measured_contention_percentage =
      100 * absl::FDivDuration(results.contention_time + results.eviction_time,
                               time_since_reset);
  ValidateContentionPercentage(measured_contention_percentage, holder);

  // This is only tested when not running under memory sanitizers since
  // they can reduce or eliminate contention.
#if !defined(MEMORY_SANITIZER) && !defined(ADDRESS_SANITIZER)
  // The test generates multiple different call stacks at high thread
  // counts, this ensures some samples are evicted from the table.
  if (number_of_threads_ > kHighThreadCount) {
    EXPECT_GT(results.evicted_samples, 0);
    EXPECT_GT(results.eviction_time, absl::ZeroDuration());
  }
#endif

  if (expect_context) {
    // If there is a Context, expect to see > 50% of samples with Context.
    // This limit avoids the risk that there's some contended background thread
    // without context info.
    EXPECT_GT(results.contention_context_time, results.contention_time * 0.5);
  } else {
    // If there is no Context information, expect to see < 50% of samples
    // with Context. This is to avoid the chance that there's some contended
    // background thread with Context info.
    EXPECT_LT(results.contention_context_time, results.contention_time * 0.5);
  }
}

void ContentionVerifier::ValidateContentionPercentage(
    double wait_percentage, const LockHolder& holder) {
  // The expected maximum wait percentage is a function of the number of threads
  // simultaneously accessing the Mutex.
  //
  // Total wait = (n - 1) * kLongHoldTime + SUM(n - 1) * kShortHoldTime
  //            = (n - 1) * kLongHoldTime + (n - 1) * n * kShortHoldTime / 2
  //            = (n - 1) * (kLongHoldTime + n * kShortHoldTime / 2)
  // Total runtime = kLongHoldTime + (n - 1) * kShortHoldTime
  //
  // Percentage = 100 * Total wait / Total runtime
  //
  // An additional 1/2 percent is added in to account for the case of an
  // expected_wait_percentage of 0.0.  If this wasn't done, the max check
  // would be for exactly 0.0 in the case of 1 thread which is too tight
  // for testing as some small contention could occur due to the test
  // harness structure.  Adding the 1/2 percent is simpler than special
  // casing the num_threads == 1 case.
  double expected_max_wait_percentage =
      absl::FDivDuration(
          100 * (number_of_threads_ - 1) *
              (ContentionzTest::kLongHoldTime +
               number_of_threads_ * ContentionzTest::kShortHoldTime / 2),
          (ContentionzTest::kLongHoldTime +
           (number_of_threads_ - 1) * ContentionzTest::kShortHoldTime * 1.0)) +
      0.5;
  double measured_wait_percentage =
      100.0 * (absl::FDivDuration(holder.GetHoldAndWaitTimeAccumulation(),
                                  holder.GetRuntime()));

  // The profiled wait percentage should always be positive.
  EXPECT_LE(0.0, wait_percentage);

  // The profiled wait percentage should be greater than or equal to what was
  // actually measured during the test by the test harness, taking into
  // account some delta.
  // NOTE:  Once in a blue moon (1 out of 10,000 runs or more), this test
  // will fail.  Usually, it is for the case of only two threads contending.
  // In the failed case, the wait_percentage will be close to 0.
  // What happens is one thread (B) sleeps or is interrupted between bumping
  // attempted_lock_acquisitions_ and actually blocking on the lock.  The
  // other thread (A) then executes its entire hold time before the B thread
  // blocks.  The result is no observed contention time.  So, unless you see
  // consistent failures for this check, your changes are most likely not the
  // result of any failure for this check.
  EXPECT_LE(measured_wait_percentage * (1.0 - kContentionAdjustmentLowerBound),
            wait_percentage);

  // The profiled wait percentage should be less than the maximum of either
  // the expected maximum or the measured percentage.  The actual profile
  // percentage is then compared vs an adjustment to the maximum to account
  // for busy systems where threads may have their start time greatly
  // delayed.  A larger upper bound was preferred over no upper bound test
  // at all to catch cases where the profiling code may greatly over compute the
  // contention.
  expected_max_wait_percentage =
      std::max(expected_max_wait_percentage, measured_wait_percentage);
  // Only apply this test if there is more than one thread. We can get
  // contention with a single thread because of background threads. If
  // that happens we get bogus numbers.
  if (number_of_threads_ > 1) {
    EXPECT_GE(
        expected_max_wait_percentage * (1.0 + kContentionAdjustmentUpperBound),
        wait_percentage);
  }
  EXPECT_EQ(number_of_threads_, holder.GetLockInvocationCount());
}

void ContentionzTest::RunThreadPool(int num_threads) {
  // Create a thread pool and start the workers.
  // It is necessary to wait until after the thread pool has been deleted
  // before collecting runtime information.
  ThreadPool thread_pool(num_threads);

  // Run threaded loop with or without contention in the body.
  for (int i = 0; i < num_threads; i++) {
    absl::Duration hold_time = (i == 0) ? kLongHoldTime : kShortHoldTime;
    thread_pool.Schedule([this, num_threads, hold_time] {
      CallLockHolder(num_threads, hold_time);
    });
  }
}

double ContentionzTest::ExecuteConcurrentThreads(
    int num_threads, enum CallstackType callstack_type) {
  absl::Duration btime = base::GetMutexWaitTime();
  lock_holder_->StartRuntime();
  RunThreadPool(num_threads);

  // Stop the runtime counter.
  lock_holder_->StopRuntime();
  absl::Duration etime = base::GetMutexWaitTime();
  // Grab the calculated contention percentage and validate.
  double wait_percentage = 0.0;
  absl::Duration runtime = lock_holder_->GetRuntime();
  if (runtime != absl::ZeroDuration()) {
    wait_percentage = absl::FDivDuration(etime - btime, runtime) * 100.0;
  }

  ContentionVerifier verifier(num_threads);
  // Need to also validate whether there is Context information for the
  // cases where this is generated.
  verifier.CheckProfileOutput(*lock_holder_,
                              callstack_type == kContextCallstack);

  verifier.ValidateContentionPercentage(wait_percentage, *lock_holder_);
  return wait_percentage;
}

absl::Duration ContentionzTest::ExecuteFibers(int num_fibers, int num_loops,
                                              absl::Duration sleep_duration) {
  base::ResetMutexProfileData();
  thread::Bundle b;
  absl::Mutex mu;
  absl::Duration total_contention_time = absl::ZeroDuration();
  for (int i = 0; i < num_fibers; i++) {
    b.Add([&] {
      for (int j = 0; j < num_loops; j++) {
        absl::Time start = absl::Now();
        mu.lock();
        total_contention_time += (absl::Now() - start);
        absl::SleepFor(sleep_duration);
        mu.unlock();
      }
    });
  }
  b.JoinAll();
  return total_contention_time;
}

void ContentionzTest::RunAll() {
  std::vector<int> num_threads = {1, 2, 5, 10};
  double prev_wait = 0.0;
  // The first iteration runs a threaded loop that sleeps for a single thread.
  // This should have minimal to no contention.
  // The following iterations run a threaded loop that sleeps while holding a
  // mutex.  One thread holds the mutex, while other contend for it.  We expect
  // the contention to increase with the number of threads.
  for (int num_thread : num_threads) {
    base::ResetMutexProfileData();
    double current_wait =
        ExecuteConcurrentThreads(num_thread, kSimpleCallstack);
    if (num_thread != 1) EXPECT_GT(current_wait, prev_wait * 0.95);
    prev_wait = current_wait;
  }
}

// Test that the profile collected from number_of_threads_to_test threads
// contains the expected_method at the top of the stack after accounting for
// the excluded methods as well as appropriate values for the contention.
void ContentionzTest::TestGenericProfileGeneration(
    int number_of_threads_to_test, enum TestLockType lock_type,
    enum CallstackType callstack_type) {
  if (RunningOnValgrind()) {
    return;
  }
  Create(lock_type, callstack_type);
  // Initializing mutex profile data should reset the waiting time to zero.
  base::ResetMutexProfileData();
  // Startup threads that will contend.
  (void)ExecuteConcurrentThreads(number_of_threads_to_test, callstack_type);
}

void ContentionzTest::TestStackCaching(int num_threads) {
  // Set the flags specific to stack caching.
  absl::SetFlag(&FLAGS_synch_contend_trace, 0);
  absl::SetFlag(&FLAGS_synch_use_stack_pointer_for_compression, true);

  RunThreadPool(num_threads);

  // Reset the flags specific to stack caching.
  absl::SetFlag(&FLAGS_synch_contend_trace, 0);
  absl::SetFlag(&FLAGS_synch_use_stack_pointer_for_compression, false);

  // Check that the cache invariants are maintained.
  EXPECT_TRUE(CheckStackCacheInvariants());
}

// NOTE:  Most of these tests fail under Valgrind as Valgrind doesn't do
// well with time management.  For instance, a sleep of 100ms may actually
// only sleep for 99ms under Valgrind.  Therefore, the majority of the tests
// are not run when running under Valgrind.

TEST_F(ContentionzTest, TestProperMutexContentionzCalculation) {
  if (!RunningOnValgrind()) {
    Create(kMutexLock, kSimpleCallstack);
    RunAll();
  }
}

TEST_F(ContentionzTest, TestProperSpinLockContentionzCalculation) {
  if (!RunningOnValgrind()) {
    Create(kSpinLock, kSimpleCallstack);
    RunAll();
  }
}

TEST_F(ContentionzTest, TestGetMutexWaitSecondsReporting) {
  if (!RunningOnValgrind()) {
    Create(kMutexLock, kSimpleCallstack);

    // Verify that waiting for mutexes actually causes the reported wait time
    // to increase.
    absl::Duration wait_time_1 = base::GetMutexWaitTime();
    RunAll();
    absl::Duration wait_time_2 = base::GetMutexWaitTime();
    EXPECT_GT(wait_time_2, wait_time_1);

    // Resetting profile data shouldn't reset total wait time.
    base::ResetMutexProfileData();
    EXPECT_GE(base::GetMutexWaitTime(), wait_time_2);
  }
}

TEST_F(ContentionzTest, TestGetMutexWaitSecondsWithFibers) {
  if (!RunningOnValgrind()) {
    std::vector<int> num_fibers = {2, 10};
    for (int n : num_fibers) {
      absl::Duration before = base::GetMutexWaitTime();
      auto measured_time = ExecuteFibers(
          n, /*num_loops=*/10, /*sleep_duration=*/absl::Milliseconds(100));
      absl::Duration after = base::GetMutexWaitTime();
      absl::Duration sampled_time = after - before;
      EXPECT_GE(sampled_time, 0.95 * measured_time);
      EXPECT_LE(sampled_time, 1.05 * measured_time);
    }
  }
}

TEST_F(ContentionzTest, TestMutexProfileGenerationNoContention) {
  TestGenericProfileGeneration(1, kMutexLock, kSimpleCallstack);
}

TEST_F(ContentionzTest, TestMutexProfileGenerationContend2Threads) {
  TestGenericProfileGeneration(2, kMutexLock, kSimpleCallstack);
}

TEST_F(ContentionzTest, TestMutexProfileGenerationContend5Threads) {
  TestGenericProfileGeneration(5, kMutexLock, kSimpleCallstack);
}

TEST_F(ContentionzTest, TestSpinLockProfileGenerationNoContention) {
  TestGenericProfileGeneration(1, kSpinLock, kSimpleCallstack);
}

TEST_F(ContentionzTest, TestSpinLockProfileGenerationContend2Threads) {
  TestGenericProfileGeneration(2, kSpinLock, kSimpleCallstack);
}

TEST_F(ContentionzTest, TestSpinLockProfileGenerationContend5Threads) {
  TestGenericProfileGeneration(5, kSpinLock, kSimpleCallstack);
}

TEST_F(ContentionzTest, TestSpinlockStackCaching) {
  Create(kSpinLock, kSimpleCallstack);
  TestStackCaching(2);
}

TEST_F(ContentionzTest, TestMutexStackCaching) {
  Create(kMutexLock, kSimpleCallstack);
  TestStackCaching(2);
}

// Test pair of threads with context information.
TEST_F(ContentionzTest, TestMutexProfileGenerationContext2Threads) {
  TestGenericProfileGeneration(2, kMutexLock, kContextCallstack);
}

TEST_F(ContentionzTest, TestContendingHighThreadCount) {
  TestGenericProfileGeneration(1000, kMutexLock, kComplexCallstack);
}

}  // namespace lock_profiling_test
