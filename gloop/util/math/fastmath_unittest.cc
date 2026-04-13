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

// Tests fast math functions for exp2, log2 to verify they do not
// introduce too large an error.  Also tests that they run faster
// than the standard <cmath> versions.

#include "gloop/util/math/fastmath.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <ostream>

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "benchmark/benchmark.h"
#include "gloop/base/timer.h"
#include "gloop/util/math/mathutil.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#if defined(NDEBUG) && !ADDRESS_SANITIZER && !MEMORY_SANITIZER &&              \
    !THREAD_SANITIZER && /* Speed tests are flaky on WASM, see b/200956356. */ \
    !defined(__wasm__) && !defined(__EMSCRIPTEN__)
#define SHOULD_RUN_SPEED_TEST 1
#else
#define SHOULD_RUN_SPEED_TEST 0
#endif

// This is purposely externally visible so the optimizer can't get rid of it.
double toret = 0;

ABSL_FLAG(bool, dumptables, false,
          "When true, this program dumps the definitions for fastmath.cc"
          "to stdout and runs no unit tests.");

namespace {

using ::benchmark::DoNotOptimize;
using ::testing::ElementsAreArray;
using ::testing::FloatEq;
using ::testing::Pointwise;

// Constants used in timing tests:

// Increase kIterations for more accurate results.
// Decrease kIterations for a quicker test.
static const int kIterations = 10000000;

// Timing tests inputs from 0 to kStepSize[f]*kIterations
static const double kStepSize = 10. / kIterations;
static const float kStepSizef = 10. / kIterations;

// Multiply WallTimer times by kTimerToNS to get ns of each iteration.
static const double kTimerToNS = 1000000000. / kIterations;

// Custom two-argument integer matcher so that we can use it with
// Pointwise.
MATCHER(IntegerWithinOneOf, "") {
  return MathUtil::WithinMargin(std::get<0>(arg), std::get<1>(arg), 1);
}

TEST(FastMathTest, Constants) {
  EXPECT_EQ(static_cast<float>(std::log(2.0)), FASTMATH_LOG_2_F);
  EXPECT_EQ(static_cast<double>(std::log(2.0)), FASTMATH_LOG_2_D);
  EXPECT_EQ(static_cast<float>(1.0 / std::log(2.0)), FASTMATH_INV_LOG_2_F);
  EXPECT_EQ(static_cast<double>(1.0 / std::log(2.0)), FASTMATH_INV_LOG_2_D);
}

TEST(FastMathTest, VerifyTableFloat) {
  auto a = FastMathClass::InternalTestAccess::actual();
  auto b = FastMathClass::InternalTestAccess::expected();
  EXPECT_THAT(a.log, ElementsAreArray(b.log));
  EXPECT_THAT(a.log_diff, ElementsAreArray(b.log_diff));
  EXPECT_THAT(a.exp1, Pointwise(IntegerWithinOneOf(), b.exp1));
  EXPECT_THAT(a.exp2, Pointwise(FloatEq(), b.exp2));
}

// Double comparions not as precise on WASM.
#if !defined(__wasm__) && !defined(__EMSCRIPTEN__)
TEST(FastMathTest, VerifyTableDouble) {
  auto a = FastMathDClass::InternalTestAccess::actual();
  auto b = FastMathDClass::InternalTestAccess::expected();

  EXPECT_THAT(a.log_diff1, ElementsAreArray(b.log_diff1));
  EXPECT_THAT(a.log_diff2, ElementsAreArray(b.log_diff2));
  EXPECT_THAT(a.exp1, ElementsAreArray(b.exp1));
  EXPECT_THAT(a.exp2, ElementsAreArray(b.exp2));
  EXPECT_THAT(a.magic, b.magic);
}
#endif

// FloatError
//   Check for correctness of single prescision fast math functions.

TEST(FastMathTest, FloatError) {
  double fe_max_err = 0;
  double fl_max_err = 0;
  double vfe_max_err = 0;
  double vfl_max_err = 0;
  double deriv_max_err = 0;

  LOG(INFO) << "float correctness test";

  for (double d = -125.9; d < 125.9; d += .13124235) {
    double e = std::exp2(d);
    double fe = fexp2(d);
    double fl = flog2(fe);
    double vfe = vfexp2(d);
    double vfl = vflog2(vfe);
    fe_max_err = std::max(fe_max_err, std::fabs((fe / e) - 1));
    fl_max_err =
        std::max(fl_max_err, std::fabs(fl - std::log(fe) / std::log(2.0)));
    vfe_max_err = std::max(vfe_max_err, std::fabs((vfe / e) - 1));
    vfl_max_err = std::max(vfl_max_err, std::fabs(vfl - d));
    deriv_max_err = std::max(
        deriv_max_err,
        std::fabs(((flog2(fexp2(d + 0.01)) - flog2(fexp2(d))) - .01) / .01));
#if 0
    EXPECT_LE(fl_max_err, 1e-5) <<
      " d=" << d << " e=" << e << " fe=" << fe
        << " le=" << log(fe)/log(2) << " fl=" << fl
        << " fl_max_err=" << fl_max_err;
#endif
  }
  LOG(INFO) << " fe_max_err: " << fe_max_err;
  LOG(INFO) << " fl_max_err: " << fl_max_err;
  LOG(INFO) << " vfe_max_err: " << vfe_max_err;
  LOG(INFO) << " vfl_max_err: " << vfl_max_err;
  LOG(INFO) << " deriv_max_err: " << deriv_max_err;
  EXPECT_LT(fe_max_err, 2e-5);
  EXPECT_LT(fl_max_err, 2e-5);
  EXPECT_LT(vfe_max_err, 2e-2);
  EXPECT_LT(vfl_max_err, 2e-2);
  EXPECT_LT(deriv_max_err, 2e-2);
  EXPECT_LT(std::fabs(vfexp(10) / std::exp(10) - 1.0), 0.01);
  EXPECT_LT(std::fabs(fexp(10) / std::exp(10) - 1.0), 0.01);
  EXPECT_LT(std::fabs(vflog(10) / std::log(10) - 1.0), 0.01);
  EXPECT_LT(std::fabs(flog(10) / std::log(10) - 1.0), 0.01);
}

// DoubleError
//   Check for correctness of double prescision fast math functions.

TEST(FastMathTest, DoubleError) {
  double fe_max_err = 0;
  double fl_max_err = 0;
  double vfe_max_err = 0;
  double vfl_max_err = 0;
  double deriv_max_err = 0;

  LOG(INFO) << "double correctness test";

  for (double d = -1021.9; d < 1021.9; d += .13124235) {
    double e = std::exp2(d);
    double fe = fexp2d(d);
    double fl = flog2d(fe);
    double vfe = vfexp2d(d);
    double vfl = vflog2d(vfe);
    fe_max_err = std::max(fe_max_err, std::fabs((fe / e) - 1));
    fl_max_err = std::max(fl_max_err, std::fabs(fl - d));
    vfe_max_err = std::max(vfe_max_err, std::fabs((vfe / e) - 1));
    vfl_max_err = std::max(vfl_max_err, std::fabs(vfl - d));
    deriv_max_err = std::max(
        deriv_max_err,
        std::fabs(((flog2d(fexp2d(d + 0.01)) - flog2d(fexp2d(d))) - .01) /
                  .01));
    EXPECT_TRUE((fe_max_err <= 2e-5) && (fl_max_err <= 2e-5))
        << " d=" << d << " e=" << e << " fe=" << fe
        << " le=" << std::log(fe) / std::log(2.0) << " fl=" << fl
        << " fe_max_err=" << fe_max_err;
  }
  LOG(INFO) << " fe_max_err: " << fe_max_err;
  LOG(INFO) << " fl_max_err: " << fl_max_err;
  LOG(INFO) << " vfe_max_err: " << vfe_max_err;
  LOG(INFO) << " vfl_max_err: " << vfl_max_err;
  LOG(INFO) << " deriv_max_err: " << deriv_max_err;
  EXPECT_LT(fe_max_err, 2e-5);
  EXPECT_LT(fl_max_err, 2e-5);
  EXPECT_LT(vfe_max_err, 2e-2);
  EXPECT_LT(vfl_max_err, 2e-2);
  EXPECT_LT(deriv_max_err, 2e-2);
  EXPECT_LT(std::fabs(vfexpd(10) / std::exp(10) - 1.0), 0.01);
  EXPECT_LT(std::fabs(fexpd(10) / std::exp(10) - 1.0), 0.01);
  EXPECT_LT(std::fabs(vflogd(10) / std::log(10) - 1.0), 0.01);
  EXPECT_LT(std::fabs(flogd(10) / std::log(10) - 1.0), 0.01);
}

// FloatSpeed
//   Times the single-precision versions.  The fast versions should be
//   at least twice as fast as the <cmath> versions, and the very fast
//   versions should be faster still.

TEST(FastMathTest, FloatSpeed) {
  float total = 0;

  WallTimer timer;

  LOG(INFO) << "float time test";

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += std::log2(static_cast<float>(i * kStepSizef)));
  timer.Stop();
  double stdlog_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " <cmath> log2 time (ns): " << stdlog_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += flog2(static_cast<float>(i * kStepSizef)));
  timer.Stop();
  double fl2_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " FastLog2 time (ns): " << fl2_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += vflog2(static_cast<float>(i * kStepSizef)));
  timer.Stop();
  double vfl2_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " VeryFastLog2 time (ns): " << vfl2_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++) {
    DoNotOptimize(total += std::exp2(static_cast<float>(i * kStepSizef)));
  }
  timer.Stop();
  double stdexp_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " <cmath> exp2 time (ns): " << stdexp_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += fexp2(static_cast<float>(i * kStepSizef)));
  timer.Stop();
  double fe2_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " FastExp2 time (ns): " << fe2_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += vfexp2(static_cast<float>(i * kStepSizef)));
  timer.Stop();
  double vfe2_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " VeryFastExp2 time (ns): " << vfe2_time << " Total=" << total;
  timer.Reset();

#if SHOULD_RUN_SPEED_TEST
  EXPECT_LT(vfl2_time, stdlog_time);
  EXPECT_LT(vfe2_time, stdexp_time);
#else
  LOG(INFO) << "debug build, no performance checks";
#endif

  toret += total;
}

TEST(EFastMathTest, FloatSpeed) {
  float total = 0;

  WallTimer timer;

  LOG(INFO) << "base-e float time test";

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += std::log(static_cast<float>(i * kStepSizef)));
  timer.Stop();
  double stdlog_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " <cmath> log time (ns): " << stdlog_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += flog(static_cast<float>(i * kStepSizef)));
  timer.Stop();
  double fl_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " FastLog time (ns): " << fl_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += vflog(static_cast<float>(i * kStepSizef)));
  timer.Stop();
  double vfl_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " VeryFastLog time (ns): " << vfl_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++) {
    DoNotOptimize(total += std::exp(static_cast<float>(i * kStepSizef)));
  }
  timer.Stop();
  double stdexp_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " <cmath> exp time (ns): " << stdexp_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += fexp(static_cast<float>(i * kStepSizef)));
  timer.Stop();
  double fe_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " FastExp time (ns): " << fe_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += vfexp(static_cast<float>(i * kStepSizef)));
  timer.Stop();
  double vfe_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " VeryFastExp time (ns): " << vfe_time << " Total=" << total;
  timer.Reset();

#if SHOULD_RUN_SPEED_TEST
  EXPECT_LT(fl_time, stdlog_time);
  EXPECT_LT(vfl_time, stdlog_time);
  EXPECT_LT(fe_time, stdexp_time);
  EXPECT_LT(vfe_time, stdexp_time);
#else
  LOG(INFO) << "debug build, no performance checks";
#endif

  toret += total;
}

// DoubleSpeed
//   Times the double-precision versions.  The fast versions should be
//   at least twice as fast as the <cmath> versions, and the very fast
//   versions should be faster still.

TEST(FastMathTest, DoubleSpeed) {
  double total = 0;

  WallTimer timer;

  LOG(INFO) << "double time test";

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += std::log2(i * kStepSize));
  timer.Stop();
  double stdlog_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " <cmath> log2 time (ns): " << stdlog_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += flog2d(i * kStepSize));
  timer.Stop();
  double fl2_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " FastLog2 time (ns): " << fl2_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += vflog2d(i * kStepSize));
  timer.Stop();
  double vfl2_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " VeryFastLog2 time (ns): " << vfl2_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++) {
    DoNotOptimize(total += std::exp2(i * kStepSize));
  }
  timer.Stop();
  double stdexp_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " <cmath> exp2 time (ns): " << stdexp_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += fexp2d(i * kStepSize));
  timer.Stop();
  double fe2_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " FastExp2 time (ns): " << fe2_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += vfexp2d(i * kStepSize));
  timer.Stop();
  double vfe2_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " VeryFastExp2 time (ns): " << vfe2_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations / 3; i++) {
    total += std::pow(2.37, i * kStepSize);
    total += std::pow(0.5, i * kStepSize);
    total += std::pow(1.0 + i * kStepSize, i * kStepSize);
    DoNotOptimize(total);
  }
  timer.Stop();
  double stdpow_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " <cmath> pow time (ns): " << stdpow_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations / 3; i++) {
    total += fpowd(2.37, i * kStepSize);
    total += fpowd(0.5, i * kStepSize);
    total += fpowd(1.0 + i * kStepSize, i * kStepSize);
    DoNotOptimize(total);
  }
  timer.Stop();
  double fp_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " FastPow time (ns): " << fp_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations / 3; i++) {
    total += vfpowd(2.37, i * kStepSize);
    total += vfpowd(0.5, i * kStepSize);
    total += vfpowd(1.0 + i * kStepSize, i * kStepSize);
    DoNotOptimize(total);
  }
  timer.Stop();
  double vfp_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " VeryFastPow time (ns): " << vfp_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++) {
    auto arg = i * (10.0 / (kIterations + 1)) - 5.0;
    DoNotOptimize(total += LogOdds2Prob(arg));
  }
  timer.Stop();
  double stdlo2p_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " <cmath> lo2p time (ns): " << stdlo2p_time
            << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++) {
    auto arg = i * (10.0 / (kIterations + 1)) - 5.0;
    DoNotOptimize(total += fLogOdds2Prob(arg));
  }
  timer.Stop();
  double flo2p_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " FastLogOdds2Prob time (ns): " << flo2p_time
            << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++) {
    auto arg = i * (1.0 / (kIterations + 1));
    DoNotOptimize(total += Prob2LogOdds(arg));
  }
  timer.Stop();
  double stdp2lo_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " <cmath> p2lo time (ns): " << stdp2lo_time
            << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++) {
    auto arg = i * (1.0 / (kIterations + 1));
    DoNotOptimize(total += fProb2LogOdds(arg));
  }
  timer.Stop();
  double fp2lo_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " FastProb2LogOdds time (ns): " << fp2lo_time
            << " Total=" << total;
  timer.Reset();

#if SHOULD_RUN_SPEED_TEST
  EXPECT_LT(fl2_time, stdlog_time);
  EXPECT_LT(vfl2_time, stdlog_time);
  EXPECT_LT(fe2_time, stdexp_time);
  EXPECT_LT(vfe2_time, stdexp_time);
  EXPECT_LT(fp_time, stdpow_time);
  EXPECT_LT(vfp_time, stdpow_time);
  EXPECT_LT(flo2p_time, stdlo2p_time);
  EXPECT_LT(fp2lo_time, stdp2lo_time);
#else
  LOG(INFO) << "debug build, no performance checks";
#endif

  toret += total;
}

TEST(EFastMathTest, DoubleSpeed) {
  double total = 0;

  WallTimer timer;

  LOG(INFO) << "double time test";

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += std::log(i * kStepSize));
  timer.Stop();
  double stdlog_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " <cmath> log time (ns): " << stdlog_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += flogd(i * kStepSize));
  timer.Stop();
  double fl_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " FastLog time (ns): " << fl_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += vflogd(i * kStepSize));
  timer.Stop();
  double vfl_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " VeryFastLog time (ns): " << vfl_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++) {
    DoNotOptimize(total += std::exp(i * kStepSize));
  }
  timer.Stop();
  double stdexp_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " <cmath> exp time (ns): " << stdexp_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += fexpd(i * kStepSize));
  timer.Stop();
  double fe_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " FastExp time (ns): " << fe_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += vfexpd(i * kStepSize));
  timer.Stop();
  double vfe_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " VeryFastExp time (ns): " << vfe_time << " Total=" << total;
  timer.Reset();

#if SHOULD_RUN_SPEED_TEST
  EXPECT_LT(fl_time, stdlog_time);
  EXPECT_LT(vfl_time, stdlog_time);
  EXPECT_LT(fe_time, stdexp_time);
  EXPECT_LT(vfe_time, stdexp_time);
#else
  LOG(INFO) << "debug build, no performance checks";
#endif

  toret += total;
}

TEST(FastMathTest, LogOdds2Prob) {
  EXPECT_DOUBLE_EQ(0.0, LogOdds2Prob(-10000.0));
  EXPECT_DOUBLE_EQ(0.0, LogOdds2Prob(-std::numeric_limits<double>::infinity()));
  EXPECT_DOUBLE_EQ(0.5, LogOdds2Prob(0.0));
  EXPECT_DOUBLE_EQ(M_E / (1.0 + M_E), LogOdds2Prob(1.0));
  EXPECT_DOUBLE_EQ(1.0, LogOdds2Prob(10000.0));
  EXPECT_DOUBLE_EQ(1.0, LogOdds2Prob(std::numeric_limits<double>::infinity()));
  // 710 is the smallest integer such that exp(710) = Infinity for double.
  EXPECT_FLOAT_EQ(1.0, fLogOdds2Prob(710));
  EXPECT_FLOAT_EQ(0.0, fLogOdds2Prob(-710));
}

TEST(FastMathTest, fLogOdds2Prob) {
  EXPECT_FLOAT_EQ(0.0, fLogOdds2Prob(-10000.0));
  EXPECT_FLOAT_EQ(0.0, fLogOdds2Prob(-std::numeric_limits<float>::infinity()));
  EXPECT_FLOAT_EQ(0.5, fLogOdds2Prob(0.0));
  // fLogOdds2Prob uses fexp which has limited accuracy.
  // The error bound of 1e-6 for fLogOdds2Prob(1.0) is the tightest power of
  // 10 allowed by the implementation at the time this was written.
  EXPECT_NEAR(M_E / (1.0 + M_E), fLogOdds2Prob(1.0), 1e-6);
  EXPECT_FLOAT_EQ(1.0, fLogOdds2Prob(10000.0));
  EXPECT_FLOAT_EQ(1.0, fLogOdds2Prob(std::numeric_limits<float>::infinity()));
  // fLogOdds2Prob calls fexp which is only valid on the range
  // [-126*ln(2)..126*ln(2)].
  // 88 and -88 exercises fLogOdds2Prob outside this range.
  EXPECT_FLOAT_EQ(1.0, fLogOdds2Prob(88));
  EXPECT_FLOAT_EQ(0.0, fLogOdds2Prob(-88));
}

namespace print_precise_internal {

template <typename Float>
int MaxDigits10() {
  typedef std::numeric_limits<Float> Lim;
  return std::floor(Lim::digits * std::log10(Lim::radix) + 2);
}

void Eval(std::ostream& os, int x) { os << absl::StrCat(x); }
void Eval(std::ostream& os, double x) {
  os << absl::StrFormat("%.*e", MaxDigits10<double>(), x);
}
void Eval(std::ostream& os, float x) {
  os << absl::StrFormat("%.*ef", MaxDigits10<float>(), x);
}
template <typename T, size_t N>
void Eval(std::ostream& os, const T (&x)[N]) {
  const char* sep = "";
  // Nothing fancy. Rely on a beautifier like clang-format post-processing
  // the output.
  os << "{ ";
  for (const auto& e : x) {
    os << sep;
    Eval(os, e);
    sep = ", ";
  }
  os << "}";
}

}  // namespace print_precise_internal

template <typename T>
struct PrecisePrinter {
  const T& x;
  friend std::ostream& operator<<(std::ostream& os, const PrecisePrinter& v) {
    print_precise_internal::Eval(os, v.x);
    return os;
  }
};

template <typename T>
PrecisePrinter<T> PrintPrecise(const T& x) {
  return {x};
}

void GenerateTableFloat(std::ostream& os) {
  auto b = FastMathClass::InternalTestAccess::expected();
  os << "const FastMathClass::Table FastMathClass::cache_ = {\n"
     << PrintPrecise(b.log) << ",\n"
     << PrintPrecise(b.log_diff) << ",\n"
     << PrintPrecise(b.exp1) << ",\n"
     << PrintPrecise(b.exp2) << "};\n";
}

void GenerateTableDouble(std::ostream& os) {
  auto b = FastMathDClass::InternalTestAccess::expected();
  os << "const FastMathDClass::Table FastMathDClass::cache_ = {\n"
     << PrintPrecise(b.log) << ",\n"
     << PrintPrecise(b.log_diff1) << ",\n"
     << PrintPrecise(b.log_diff2) << ",\n"
     << PrintPrecise(b.exp1) << ",\n"
     << PrintPrecise(b.exp2) << ",\n"
     << PrintPrecise(b.magic) << "};\n";
}

TEST(FastMathTest, SqrtSpeed) {
  double total = 0;

  WallTimer timer;

  LOG(INFO) << "squareroot time test";

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += sqrt(i * kStepSize));
  timer.Stop();
  double stdlog_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " <cmath> sqrt time (ns): " << stdlog_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += fpowd(i * kStepSize, 0.5));
  timer.Stop();
  double fl2_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " FastPowD 0.5 time (ns): " << fl2_time << " Total=" << total;
  timer.Reset();

  toret += total;
  total = 0;
  timer.Start();
  for (int i = 1; i < kIterations; i++)
    DoNotOptimize(total += vfpowd(i * kStepSize, 0.5));
  timer.Stop();
  double vfl2_time = timer.Get() * kTimerToNS;
  LOG(INFO) << " VFastPowD 0.5 time (ns): " << vfl2_time << " Total=" << total;
  timer.Reset();
}  // SqrtSpeed

}  // namespace

TEST(DumpTables, Test) {
  if (absl::GetFlag(FLAGS_dumptables)) {
    std::cout << "#if 1\n";
    GenerateTableFloat(std::cout);
    GenerateTableDouble(std::cout);
    std::cout << "#endif\n";
  }
}
