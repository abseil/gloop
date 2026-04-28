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

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/debugging/symbolize.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/auxiliary/synchronization_profile.h"
#include "gloop/base/auxiliary/synchronization_profiling.h"
#include "gloop/base/auxiliary/synchronization_profiling_test_util.h"
#include "gloop/strings/strip.h"
#include "gloop/thread/fiber/bundle.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/threadpool.h"
#include "gtest/gtest.h"

namespace lock_profiling_test {
namespace {

// Aggregated contention data.
struct ContentionStats {
  absl::Duration sample_delay = absl::ZeroDuration();
  int64_t total_samples = 0;
  absl::Duration evicted_delay = absl::ZeroDuration();
  int64_t evicted_samples = 0;
  bool recognized_method = false;  // Whether target method was found or not.
  // Track amount of contention time contributed by locks when
  // Context information is present.
  absl::Duration with_context_delay = absl::ZeroDuration();
  absl::Duration with_context_evicted_delay = absl::ZeroDuration();
  std::string expected_method_name;
};

struct DeltaContentionStats {
  ContentionStats locking_result;
  ContentionStats unlocking_result;
};

// Verifier class to handle reading the contention data and then to
// determine whether the results of the running the test match the
// expected values.
class ContentionVerifier {
 public:
  explicit ContentionVerifier(int number_of_threads_tested)
      : number_of_threads_(number_of_threads_tested) {}

  // Validate the observed amount of contention.
  void ValidateLockingDelayPercentage(double locking_delay_percentage,
                                      const LockHolder& holder);

  // Validate the reported results.
  void ValidateResults(const ContentionStats& results,
                       absl::Duration profiling_duration, bool expect_context,
                       const LockHolder& holder);

  // Extract the contention points from the protobuf output format.
  static DeltaContentionStats GetContentionFromProfile(
      const base::DeltaContentionProfile& profile, const LockHolder& holder);

 private:
  static constexpr int kSymbolNameLength = 256;

  // We do some testing with high thread counts. With high thread counts we
  // generate multiple different call stacks so that we can test eviction of
  // samples from the table. High thread counts also introduce more variablity
  // in the results, so we have a more lenient way of evaluating success.
  static constexpr int kHighThreadCount = 100;

  // An adjustment percent to the lower bound for the contention percentage.
  // Due to timing issues, the actual contention observed may be off slightly
  // from what theoretically should be the contention.
  static constexpr double kContentionAdjustmentLowerBound = 0.5;

  // An adjustment percent for the upper bound.  The adjustment for the upper
  // bound is much larger than the lower bound as the maximum contention
  // profiled may be much larger than the theoretical maximum due to some
  // threads contending before the start of the measurement (will happen on a
  // busy system).  If some threads start to contend much sooner than others,
  // the profiled contention time (numerator for the percentage) will
  // increase, but the test run time (denominator), may not change much, so
  // the overall percentage will be higher than predicted.
  static constexpr double kContentionAdjustmentUpperBound = 0.5;

  static void PopulateContentionFromProfile(
      const base::DeltaContentionProfile& profile,
      absl::string_view method_name, bool locking, ContentionStats& results);

  int number_of_threads_;
};

void ContentionVerifier::PopulateContentionFromProfile(
    const base::DeltaContentionProfile& profile, absl::string_view method_name,
    bool locking, ContentionStats& results) {
  profile.Iterate(
      [&](const base::DeltaContentionProfile::ValuesAndLabels& item) {
        int64_t count =
            locking ? item.locking_contentions : item.unlocking_contentions;
        absl::Duration delay =
            locking ? item.locking_delay : item.unlocking_delay;

        if (count == 0 && delay == absl::ZeroDuration()) {
          return;
        }

        for (int i = 0; i < item.depth; ++i) {
          char symbol[ContentionVerifier::kSymbolNameLength];
          if (!absl::Symbolize(item.stack[i], symbol,
                               ContentionVerifier::kSymbolNameLength - 1)) {
            continue;
          }

          std::string symbol_string = symbol;
          strings::TrimStringRight(&symbol_string, "()");
          if (absl::StrContains(symbol_string, method_name)) {
            results.recognized_method = true;
            results.total_samples += count;
            results.sample_delay += delay;
            if (item.has_context) {
              results.with_context_delay += delay;
            }
            return;
          }
          if (absl::StrContains(symbol_string, "EvictedContentionData")) {
            results.evicted_samples += count;
            results.evicted_delay += delay;
            if (item.has_context) {
              results.with_context_evicted_delay += delay;
            }
            return;
          }
        }
      });
}

DeltaContentionStats ContentionVerifier::GetContentionFromProfile(
    const base::DeltaContentionProfile& profile, const LockHolder& holder) {
  DeltaContentionStats stats;
  stats.locking_result.expected_method_name = holder.ExpectedLockMethodName();
  stats.unlocking_result.expected_method_name =
      holder.ExpectedUnlockMethodName();

  PopulateContentionFromProfile(profile, holder.ExpectedLockMethodName(),
                                /*locking=*/true, stats.locking_result);
  PopulateContentionFromProfile(profile, holder.ExpectedUnlockMethodName(),
                                /*locking=*/false, stats.unlocking_result);
  return stats;
}

// A test class to verify the contentionz metrics are calculated correctly
// for Mutex and Spinlock lock types.
class DeltaContentionzTest : public ::testing::Test {
 protected:
  DeltaContentionzTest() : callstack_(nullptr), lock_holder_(nullptr) {
    absl::SetFlag(&FLAGS_synch_profile_period, kProfilePeriod);
  }

  // Create appropriate Callstack and Lock objects.
  void Create(enum TestLockType lock_type, enum CallstackType callstack_type) {
    lock_holder_ = std::make_unique<LockHolder>(lock_type);
    callstack_.reset(CallstackInterface::New(callstack_type));
  }

  // Test profile generation for a specified number of threads and lock type
  // potentially testing for evictions from the cache.
  void TestGenericProfileGeneration(int number_of_threads_to_test,
                                    enum TestLockType lock_type,
                                    enum CallstackType callstack_type);

  // Executes num_fibers concurrently and returns the contention time as sampled
  // by the instrumentation and also the one measured directly in the function.
  std::pair<absl::Duration, absl::Duration> ExecuteFibers(
      int num_fibers, int num_loops, absl::Duration sleep_duration);

  // Execute num_threads concurrently and return the contention percentage.
  std::unique_ptr<base::DeltaContentionProfile> ExecuteConcurrentThreads(
      int num_threads);

  DeltaContentionStats GetStatsFromProfile(
      std::unique_ptr<base::DeltaContentionProfile> profile) const;

  void ValidateStats(const DeltaContentionStats& result,
                     absl::Duration profiling_duration, int num_threads,
                     enum TestLockType lock_type,
                     enum CallstackType callstack_type) const;

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

  // Setting the profile period to 1 would sample every operation.  While this
  // would be better from measurement perspective.  But in production we sample
  // at a significantly lower rate.  We set the profile period to a moderate
  // value to bring the effect of sampling in our tests, while letting the tests
  // run a short enough time.
  static constexpr int kProfilePeriod = 3;
  static constexpr int kExpectedSamples = 20;
  static constexpr int kTestRuns = kProfilePeriod * kExpectedSamples;
  std::unique_ptr<CallstackInterface> callstack_;
  std::unique_ptr<LockHolder> lock_holder_;
};

void ContentionVerifier::ValidateResults(const ContentionStats& results,
                                         absl::Duration profiling_duration,
                                         bool expect_context,
                                         const LockHolder& holder) {
  if (number_of_threads_ == 1) {
    // A single thread means no contention can be generated.  Therefore, no
    // cycles or samples should be found.
    EXPECT_EQ(results.sample_delay, absl::ZeroDuration());
    EXPECT_EQ(results.total_samples, 0);
    return;
  }

  // The time_since_reset should be within the range:
  // [kHoldTime + kShortHoldTime * number_of_threads_tested,
  //  runtime_ in milliseconds]
  // However, values can be off slightly, so the actual values tested
  // are adjusted very slightly from above to expand the range and prevent
  // false test failures.
  EXPECT_LE(DeltaContentionzTest::kLongHoldTime +
                DeltaContentionzTest::kShortHoldTime *
                    (number_of_threads_ - 1) * 0.99,
            profiling_duration);
  EXPECT_LE(holder.GetRuntime(), profiling_duration);
  EXPECT_GE(holder.GetRuntime() * 1.2, profiling_duration);

  // Check that we saw the expected method in at least one stack trace.
  EXPECT_TRUE(results.recognized_method);

  // Should have seen some contention, so make sure the amount of contention
  // makes sense as well as the stack trace.
  EXPECT_LE(number_of_threads_ - 1,
            results.total_samples + results.evicted_samples);
  double measured_contention_percentage =
      100 * absl::FDivDuration(results.sample_delay + results.evicted_delay,
                               profiling_duration);
  ValidateLockingDelayPercentage(measured_contention_percentage, holder);

  // This is only tested when not running under memory sanitizers since
  // they can reduce or eliminate contention.
#if !defined(MEMORY_SANITIZER) && !defined(ADDRESS_SANITIZER)
  // The test generates multiple different call stacks at high thread
  // counts, this ensures some samples are evicted from the table.
  if (number_of_threads_ > kHighThreadCount) {
    EXPECT_GT(results.evicted_samples, 0);
    EXPECT_GT(results.evicted_delay, absl::ZeroDuration());
  }
#endif

  if (expect_context) {
    // If there is a Context, expect to see > 50% of samples with Context.
    // This limit avoids the risk that there's some contended background
    // thread without context info.
    EXPECT_GT(results.with_context_delay, results.sample_delay * 0.5);
  } else {
    // If there is no Context information, expect to see < 50% of samples
    // with Context. This is to avoid the chance that there's some contended
    // background thread with Context info.
    EXPECT_LT(results.with_context_delay, results.sample_delay * 0.5);
  }
}

void ContentionVerifier::ValidateLockingDelayPercentage(
    double locking_delay_percentage, const LockHolder& holder) {
  // The expected maximum wait percentage is a function of the number of
  // threads simultaneously accessing the Mutex.
  //
  // Total locking delay = (n - 1) * kLongHoldTime + SUM(n - 1) * kShortHoldTime
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
  double expected_max_locking_delay_percentage =
      absl::FDivDuration(
          100 * (number_of_threads_ - 1) *
              (DeltaContentionzTest::kLongHoldTime +
               number_of_threads_ * DeltaContentionzTest::kShortHoldTime / 2),
          (DeltaContentionzTest::kLongHoldTime +
           (number_of_threads_ - 1) * DeltaContentionzTest::kShortHoldTime *
               1.0)) +
      0.5;
  double measured_locking_delay_percentage =
      100.0 * (absl::FDivDuration(holder.GetHoldAndWaitTimeAccumulation() -
                                      holder.GetHoldTimeAccumulation(),
                                  holder.GetRuntime()));

  // The profiled wait percentage should always be positive.
  ASSERT_LE(0.0, locking_delay_percentage);

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
  EXPECT_LE(measured_locking_delay_percentage *
                (1.0 - kContentionAdjustmentLowerBound),
            locking_delay_percentage);

  // The profiled wait percentage should be less than the maximum of either
  // the expected maximum or the measured percentage.  The actual profile
  // percentage is then compared vs an adjustment to the maximum to account
  // for busy systems where threads may have their start time greatly
  // delayed.  A larger upper bound was preferred over no upper bound test
  // at all to catch cases where the profiling code may greatly over compute
  // the contention.
  expected_max_locking_delay_percentage = std::max(
      expected_max_locking_delay_percentage, measured_locking_delay_percentage);
  // Only apply this test if there is more than one thread. We can get
  // contention with a single thread because of background threads. If
  // that happens we get bogus numbers.
  if (number_of_threads_ > 1) {
    EXPECT_GE(expected_max_locking_delay_percentage *
                  (1.0 + kContentionAdjustmentUpperBound),
              locking_delay_percentage);
  }
  EXPECT_EQ(number_of_threads_, holder.GetLockInvocationCount());
}

void DeltaContentionzTest::RunThreadPool(int num_threads) {
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

std::unique_ptr<base::DeltaContentionProfile>
DeltaContentionzTest::ExecuteConcurrentThreads(int num_threads) {
  base::DeltaContentionToken token =
      base::DeltaContentionToken::StartProfiling();
  if (!token.IsValid()) {
    LOG(FATAL) << "Failed to enable delta contentionz.";
  }
  lock_holder_->StartRuntime();
  for (int run = 0; run < kTestRuns; ++run) {
    lock_holder_->Reset();
    RunThreadPool(num_threads);
  }
  // Stop the runtime counter.
  lock_holder_->StopRuntime();
  return std::move(token).StopProfiling();
}

DeltaContentionStats DeltaContentionzTest::GetStatsFromProfile(
    std::unique_ptr<base::DeltaContentionProfile> profile) const {
  DeltaContentionStats result =
      ContentionVerifier::GetContentionFromProfile(*profile, *lock_holder_);
  return result;
}

void DeltaContentionzTest::ValidateStats(
    const DeltaContentionStats& result, absl::Duration profiling_duration,
    int num_threads, enum TestLockType lock_type,
    enum CallstackType callstack_type) const {
  ContentionVerifier verifier(num_threads);
  // We do not collect locking callstacks for spinlocks.  So the next validation
  // is only done for mutexes.
  if (lock_type == kMutexLock) {
    verifier.ValidateResults(result.locking_result, profiling_duration,
                             callstack_type == kContextCallstack,
                             *lock_holder_);
  }
  verifier.ValidateResults(result.unlocking_result, profiling_duration,
                           callstack_type == kContextCallstack, *lock_holder_);
}

std::pair<absl::Duration, absl::Duration> DeltaContentionzTest::ExecuteFibers(
    int num_fibers, int num_loops, absl::Duration sleep_duration) {
  base::DeltaContentionToken token =
      base::DeltaContentionToken::StartProfiling();
  if (!token.IsValid()) {
    LOG(FATAL) << "Failed to enable delta contentionz";
  }
  absl::Duration total_contention_time = absl::ZeroDuration();
  for (int run = 0; run < kTestRuns; ++run) {
    thread::Bundle b;
    absl::Mutex mu;
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
  }
  auto profile = std::move(token).StopProfiling();
  absl::Duration locking_delay = profile->TotalLockingDelayForTesting();
  return {locking_delay, total_contention_time};
}

// Test that the profile collected from number_of_threads_to_test threads
// contains the expected_method at the top of the stack after accounting for
// the excluded methods as well as appropriate values for the contention.
void DeltaContentionzTest::TestGenericProfileGeneration(
    int number_of_threads_to_test, enum TestLockType lock_type,
    enum CallstackType callstack_type) {
  Create(lock_type, callstack_type);
  // Startup threads that will contend.
  std::unique_ptr<base::DeltaContentionProfile> profile =
      ExecuteConcurrentThreads(number_of_threads_to_test);
  absl::Duration profile_duration = profile->duration();
  auto result = GetStatsFromProfile(std::move(profile));
  ValidateStats(result, profile_duration, number_of_threads_to_test, lock_type,
                callstack_type);
}

TEST_F(DeltaContentionzTest, TestZeroContentionAfterInit) {
  base::DeltaContentionToken token =
      base::DeltaContentionToken::StartProfiling();
  ASSERT_TRUE(token.IsValid());
  auto profile = std::move(token).StopProfiling();
  ASSERT_TRUE(profile.get() != nullptr);
  int items = 0;
  profile->Iterate(
      [&](const base::DeltaContentionProfile::ValuesAndLabels& item) {
        items++;
      });
  EXPECT_EQ(items, 0);
}

class DeltaContentionzParameterizedTestWithLockTypeAndCallstackType
    : public DeltaContentionzTest,
      public testing::WithParamInterface<
          std::tuple<TestLockType, CallstackType>> {};

TEST_P(DeltaContentionzParameterizedTestWithLockTypeAndCallstackType,
       TestDeltaContentionzCalculation) {
  auto [lock_type, callstack_type] = GetParam();
  Create(lock_type, callstack_type);
  std::vector<int> num_threads = {1, 2, 5, 10};
  double prev_delay_percentage = 0.0;
  // The first phase runs a threaded loop that sleeps for a single thread.
  // This should have minimal to no contention.
  // The following iterations run a threaded loop that sleeps while holding a
  // mutex.  One thread holds the mutex, while others contend for it.  We expect
  // the contention to increase with the number of threads.
  for (int num_thread : num_threads) {
    auto profile = ExecuteConcurrentThreads(num_thread);
    absl::Duration profile_duration = profile->duration();
    const DeltaContentionStats result = GetStatsFromProfile(std::move(profile));
    ValidateStats(result, profile_duration, num_thread, lock_type,
                  callstack_type);
    const double current_delay_percentage =
        100 * absl::FDivDuration(result.unlocking_result.sample_delay,
                                 profile_duration);
    if (num_thread != 1)
      EXPECT_GT(current_delay_percentage, prev_delay_percentage * 0.95);
    prev_delay_percentage = current_delay_percentage;
  }
}

INSTANTIATE_TEST_SUITE_P(
    DeltaContentionzParameterizedTestWithLockTypeAndCallstackTypeGroup,
    DeltaContentionzParameterizedTestWithLockTypeAndCallstackType,
    testing::Combine(testing::Values(kMutexLock, kSpinLock),
                     testing::Values(kSimpleCallstack, kComplexCallstack,
                                     kContextCallstack)));

TEST_F(DeltaContentionzTest, TestContentionStatsWithFibers) {
  std::vector<int> num_fibers = {2, 10};
  for (int n : num_fibers) {
    auto [sampled_time, measured_time] = ExecuteFibers(
        n, /*num_loops=*/10, /*sleep_duration=*/absl::Milliseconds(10));
    EXPECT_GE(sampled_time, 0.5 * measured_time);
    EXPECT_LE(sampled_time, 1.5 * measured_time);
  }
}

TEST_F(DeltaContentionzTest, TestContendingHighThreadCount) {
#if !defined(ABSL_HAVE_ADDRESS_SANITIZER) && \
    !defined(ABSL_HAVE_LEAK_SANITIZER) &&    \
    !defined(ABSL_HAVE_MEMORY_SANITIZER) &&  \
    !defined(ABSL_HAVE_THREAD_SANITIZER)
  TestGenericProfileGeneration(150, kMutexLock, kComplexCallstack);
#endif
}

TEST_F(DeltaContentionzTest, StartDeltaContentionProfiling) {
  std::vector<base::DeltaContentionToken> tokens;
  for (int i = 0; i < base::internal::kMaxInflightDeltaProfiles; ++i) {
    base::DeltaContentionToken token =
        base::DeltaContentionToken::StartProfiling();
    ASSERT_TRUE(token.IsValid());
    tokens.push_back(std::move(token));
  }
  base::DeltaContentionToken token =
      base::DeltaContentionToken::StartProfiling();
  ASSERT_FALSE(token.IsValid());
  for (int i = 0; i < base::internal::kMaxInflightDeltaProfiles; ++i) {
    std::move(tokens[i]).StopProfiling();
  }
}

class DeltaContentionzParameterizedTestWithNumThreadsLockTypeAndCallstackType
    : public DeltaContentionzTest,
      public testing::WithParamInterface<
          std::tuple<int, TestLockType, CallstackType>> {};

TEST_P(DeltaContentionzParameterizedTestWithNumThreadsLockTypeAndCallstackType,
       TestProfileGeneration) {
  std::tuple param = GetParam();
  TestGenericProfileGeneration(std::get<0>(param), std::get<1>(param),
                               std::get<2>(param));
}

INSTANTIATE_TEST_SUITE_P(
    DeltaContentionzParameterizedTestWithNumThreadsLockTypeAndCallstackTypeGroup,
    DeltaContentionzParameterizedTestWithNumThreadsLockTypeAndCallstackType,
    testing::Combine(testing::Values(1, 2, 5),
                     testing::Values(kMutexLock, kSpinLock),
                     testing::Values(kSimpleCallstack, kComplexCallstack,
                                     kContextCallstack)));

class TestThread : public Thread {
 public:
  TestThread(const thread::Options& options, absl::string_view prefix,
             absl::Duration sleep, absl::Mutex* mu)
      : Thread(options, prefix), sleep_(sleep), mu_(mu) {}

 protected:
  void Run() override {
    for (int i = 0; i < 10; ++i) {
      mu_->lock();
      absl::SleepFor(sleep_);
      mu_->unlock();
    }
  }

 private:
  absl::Duration sleep_;
  absl::Mutex* mu_;
};

std::unique_ptr<base::DeltaContentionProfile> GetDeltaContentionProfile(
    absl::Duration sleep) {
  base::DeltaContentionToken token =
      base::DeltaContentionToken::StartProfiling();
  CHECK(token.IsValid());
  constexpr int kRuns = 100;
  for (int run = 0; run < kRuns; ++run) {
    constexpr int kBatch = 20;
    std::array<absl::Mutex, kBatch> mu;
    thread::Options options;
    options.set_joinable(true);
    const int kNumThreads = kBatch * kBatch;
    std::vector<std::unique_ptr<TestThread>> threads;
    for (int j = 0; j < kNumThreads; ++j) {
      auto t = std::make_unique<TestThread>(options, absl::StrCat("thread", j),
                                            sleep, &mu[j % kBatch]);
      t->Start();
      threads.push_back(std::move(t));
    }
    for (int j = 0; j < kNumThreads; j++) {
      threads[j]->Join();
    }
  }
  auto profile = std::move(token).StopProfiling();
  CHECK(profile != nullptr);
  return profile;
}

// Test that the profiler fails to acquire the spinlock protecting the profiling
// data some times and stores data in the histogram for tracking the lost
// traces.
TEST_F(DeltaContentionzTest, TestLostDataIsRecorded) {
  auto profile = GetDeltaContentionProfile(absl::Milliseconds(1));
  EXPECT_GT(profile->LockingNotRecordedCyclesForTesting() +
                profile->UnlockingNotRecordedCyclesForTesting(),
            0);
}

// Tests whether the final bucket of the histogram is used.  The tests runs for
// a long time.  So it is marked as disabled.
TEST_F(DeltaContentionzTest, DISABLED_TestHistogramBuckets) {
  auto profile = GetDeltaContentionProfile(absl::Milliseconds(1000));
  bool found_final_bucket = false;
  profile->Iterate(
      [&](const base::DeltaContentionProfile::ValuesAndLabels& item) {
        if (item.histogram_bucket_min == absl::Seconds(100)) {
          found_final_bucket = true;
        }
      });
  EXPECT_TRUE(found_final_bucket);
}

TEST(ContentionTest, DeltaContentionData_StackTraceHolds64Frames) {
  constexpr int kStackTraceSize = 64;
  base::internal::DeltaContentionData::StackTrace stack0 = {kStackTraceSize};
  for (int i = 0; i < kStackTraceSize; ++i) {
    // This is the actual important part of this test; it confirms that
    // storing 64 stack frames won't cause a std::array assertion failure.
    stack0.stack[i] = reinterpret_cast<void*>(i);
  }

  base::internal::DeltaContentionData cd(/*g_not_recorded_cycles=*/0);
  constexpr uintptr_t hash0 = 10;
  const absl::Duration kOb0 =
      base::internal::kMutexHistogramLimits[0] - absl::Microseconds(100);
  cd.InsertOrUpdateTrace(hash0, {.tags = {}, .stack = stack0},
                         /*has_context=*/false, kOb0);

  int found = 0;
  for (int i = 0; i < base::internal::kMutexProfileHashTableSize; ++i) {
    for (int j = 0; j < base::internal::kMutexProfileAssociativity; ++j) {
      const base::internal::DeltaContentionData::StackTraceAndStats& e =
          cd.traces[i][j];
      if (e.stack_and_tags.stack == stack0) {
        found = 1;
        EXPECT_EQ(e.histograms[0].total_count, 1);
        EXPECT_EQ(e.histograms[1].total_count, 0);
        EXPECT_EQ(e.histograms[0].total_duration, kOb0);
        EXPECT_EQ(e.histograms[1].total_duration, absl::ZeroDuration());
        EXPECT_EQ(e.histograms[0].counts[0], 1);
        EXPECT_EQ(e.histograms[0].duration[0], kOb0);
      }
    }
  }
  EXPECT_EQ(found, 1);
}

}  // namespace
}  // namespace lock_profiling_test
