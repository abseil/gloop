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
#include "gloop/util/math/mathutil.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

ABSL_FLAG(bool, dumptables, false,
          "When true, this program dumps the definitions for fastmath.cc"
          "to stdout and runs no unit tests.");

namespace {

using ::testing::ElementsAreArray;
using ::testing::FloatEq;
using ::testing::Pointwise;

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

TEST(DumpTables, Test) {
  if (absl::GetFlag(FLAGS_dumptables)) {
    std::cout << "#if 1\n";
    GenerateTableFloat(std::cout);
    GenerateTableDouble(std::cout);
    std::cout << "#endif\n";
  }
}

// Google Benchmark implementations for fastmath functions

void BM_StdLog2f(benchmark::State& state) {
  float i = 1.0f;
  const float step_size = 0.000001f;
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::log2(i * step_size));
    i += 1.0f;
    if (i > 10000000.0f) i = 1.0f;
  }
}
BENCHMARK(BM_StdLog2f);

void BM_Flog2(benchmark::State& state) {
  float i = 1.0f;
  const float step_size = 0.000001f;
  for (auto _ : state) {
    benchmark::DoNotOptimize(flog2(i * step_size));
    i += 1.0f;
    if (i > 10000000.0f) i = 1.0f;
  }
}
BENCHMARK(BM_Flog2);

void BM_Vflog2(benchmark::State& state) {
  float i = 1.0f;
  const float step_size = 0.000001f;
  for (auto _ : state) {
    benchmark::DoNotOptimize(vflog2(i * step_size));
    i += 1.0f;
    if (i > 10000000.0f) i = 1.0f;
  }
}
BENCHMARK(BM_Vflog2);

void BM_StdExp2f(benchmark::State& state) {
  float i = 1.0f;
  const float step_size = 0.000001f;
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::exp2(i * step_size));
    i += 1.0f;
    if (i > 10000000.0f) i = 1.0f;
  }
}
BENCHMARK(BM_StdExp2f);

void BM_Fexp2(benchmark::State& state) {
  float i = 1.0f;
  const float step_size = 0.000001f;
  for (auto _ : state) {
    benchmark::DoNotOptimize(fexp2(i * step_size));
    i += 1.0f;
    if (i > 10000000.0f) i = 1.0f;
  }
}
BENCHMARK(BM_Fexp2);

void BM_Vfexp2(benchmark::State& state) {
  float i = 1.0f;
  const float step_size = 0.000001f;
  for (auto _ : state) {
    benchmark::DoNotOptimize(vfexp2(i * step_size));
    i += 1.0f;
    if (i > 10000000.0f) i = 1.0f;
  }
}
BENCHMARK(BM_Vfexp2);

void BM_StdLogf(benchmark::State& state) {
  float i = 1.0f;
  const float step_size = 0.000001f;
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::log(i * step_size));
    i += 1.0f;
    if (i > 10000000.0f) i = 1.0f;
  }
}
BENCHMARK(BM_StdLogf);

void BM_Flog(benchmark::State& state) {
  float i = 1.0f;
  const float step_size = 0.000001f;
  for (auto _ : state) {
    benchmark::DoNotOptimize(flog(i * step_size));
    i += 1.0f;
    if (i > 10000000.0f) i = 1.0f;
  }
}
BENCHMARK(BM_Flog);

void BM_Vflog(benchmark::State& state) {
  float i = 1.0f;
  const float step_size = 0.000001f;
  for (auto _ : state) {
    benchmark::DoNotOptimize(vflog(i * step_size));
    i += 1.0f;
    if (i > 10000000.0f) i = 1.0f;
  }
}
BENCHMARK(BM_Vflog);

void BM_StdExpf(benchmark::State& state) {
  float i = 1.0f;
  const float step_size = 0.000001f;
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::exp(i * step_size));
    i += 1.0f;
    if (i > 10000000.0f) i = 1.0f;
  }
}
BENCHMARK(BM_StdExpf);

void BM_Fexp(benchmark::State& state) {
  float i = 1.0f;
  const float step_size = 0.000001f;
  for (auto _ : state) {
    benchmark::DoNotOptimize(fexp(i * step_size));
    i += 1.0f;
    if (i > 10000000.0f) i = 1.0f;
  }
}
BENCHMARK(BM_Fexp);

void BM_Vfexp(benchmark::State& state) {
  float i = 1.0f;
  const float step_size = 0.000001f;
  for (auto _ : state) {
    benchmark::DoNotOptimize(vfexp(i * step_size));
    i += 1.0f;
    if (i > 10000000.0f) i = 1.0f;
  }
}
BENCHMARK(BM_Vfexp);

void BM_StdLog2d(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 0.000001;
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::log2(i * step_size));
    i += 1.0;
    if (i > 10000000.0) i = 1.0;
  }
}
BENCHMARK(BM_StdLog2d);

void BM_Flog2d(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 0.000001;
  for (auto _ : state) {
    benchmark::DoNotOptimize(flog2d(i * step_size));
    i += 1.0;
    if (i > 10000000.0) i = 1.0;
  }
}
BENCHMARK(BM_Flog2d);

void BM_Vflog2d(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 0.000001;
  for (auto _ : state) {
    benchmark::DoNotOptimize(vflog2d(i * step_size));
    i += 1.0;
    if (i > 10000000.0) i = 1.0;
  }
}
BENCHMARK(BM_Vflog2d);

void BM_StdExp2d(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 0.000001;
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::exp2(i * step_size));
    i += 1.0;
    if (i > 10000000.0) i = 1.0;
  }
}
BENCHMARK(BM_StdExp2d);

void BM_Fexp2d(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 0.000001;
  for (auto _ : state) {
    benchmark::DoNotOptimize(fexp2d(i * step_size));
    i += 1.0;
    if (i > 10000000.0) i = 1.0;
  }
}
BENCHMARK(BM_Fexp2d);

void BM_Vfexp2d(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 0.000001;
  for (auto _ : state) {
    benchmark::DoNotOptimize(vfexp2d(i * step_size));
    i += 1.0;
    if (i > 10000000.0) i = 1.0;
  }
}
BENCHMARK(BM_Vfexp2d);

void BM_StdPow(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 0.000001;
  for (auto _ : state) {
    double total = 0.0;
    double arg = i * step_size;
    total += std::pow(2.37, arg);
    total += std::pow(0.5, arg);
    total += std::pow(1.0 + arg, arg);
    benchmark::DoNotOptimize(total);
    i += 1.0;
    if (i > 1000000.0) i = 1.0;
  }
}
BENCHMARK(BM_StdPow);

void BM_Fpowd(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 0.000001;
  for (auto _ : state) {
    double total = 0.0;
    double arg = i * step_size;
    total += fpowd(2.37, arg);
    total += fpowd(0.5, arg);
    total += fpowd(1.0 + arg, arg);
    benchmark::DoNotOptimize(total);
    i += 1.0;
    if (i > 1000000.0) i = 1.0;
  }
}
BENCHMARK(BM_Fpowd);

void BM_Vfpowd(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 0.000001;
  for (auto _ : state) {
    double total = 0.0;
    double arg = i * step_size;
    total += vfpowd(2.37, arg);
    total += vfpowd(0.5, arg);
    total += vfpowd(1.0 + arg, arg);
    benchmark::DoNotOptimize(total);
    i += 1.0;
    if (i > 1000000.0) i = 1.0;
  }
}
BENCHMARK(BM_Vfpowd);

void BM_StdLogOdds2Prob(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 10.0 / 10000001.0;
  for (auto _ : state) {
    double arg = i * step_size - 5.0;
    benchmark::DoNotOptimize(LogOdds2Prob(arg));
    i += 1.0;
    if (i > 10000000.0) i = 1.0;
  }
}
BENCHMARK(BM_StdLogOdds2Prob);

void BM_FLogOdds2Prob(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 10.0 / 10000001.0;
  for (auto _ : state) {
    double arg = i * step_size - 5.0;
    benchmark::DoNotOptimize(fLogOdds2Prob(arg));
    i += 1.0;
    if (i > 10000000.0) i = 1.0;
  }
}
BENCHMARK(BM_FLogOdds2Prob);

void BM_StdProb2LogOdds(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 1.0 / 10000001.0;
  for (auto _ : state) {
    double arg = i * step_size;
    benchmark::DoNotOptimize(Prob2LogOdds(arg));
    i += 1.0;
    if (i > 10000000.0) i = 1.0;
  }
}
BENCHMARK(BM_StdProb2LogOdds);

void BM_FProb2LogOdds(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 1.0 / 10000001.0;
  for (auto _ : state) {
    double arg = i * step_size;
    benchmark::DoNotOptimize(fProb2LogOdds(arg));
    i += 1.0;
    if (i > 10000000.0) i = 1.0;
  }
}
BENCHMARK(BM_FProb2LogOdds);

void BM_StdLogd(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 0.000001;
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::log(i * step_size));
    i += 1.0;
    if (i > 10000000.0) i = 1.0;
  }
}
BENCHMARK(BM_StdLogd);

void BM_Flogd(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 0.000001;
  for (auto _ : state) {
    benchmark::DoNotOptimize(flogd(i * step_size));
    i += 1.0;
    if (i > 10000000.0) i = 1.0;
  }
}
BENCHMARK(BM_Flogd);

void BM_Vflogd(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 0.000001;
  for (auto _ : state) {
    benchmark::DoNotOptimize(vflogd(i * step_size));
    i += 1.0;
    if (i > 10000000.0) i = 1.0;
  }
}
BENCHMARK(BM_Vflogd);

void BM_StdExpd(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 0.000001;
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::exp(i * step_size));
    i += 1.0;
    if (i > 10000000.0) i = 1.0;
  }
}
BENCHMARK(BM_StdExpd);

void BM_Fexpd(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 0.000001;
  for (auto _ : state) {
    benchmark::DoNotOptimize(fexpd(i * step_size));
    i += 1.0;
    if (i > 10000000.0) i = 1.0;
  }
}
BENCHMARK(BM_Fexpd);

void BM_Vfexpd(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 0.000001;
  for (auto _ : state) {
    benchmark::DoNotOptimize(vfexpd(i * step_size));
    i += 1.0;
    if (i > 10000000.0) i = 1.0;
  }
}
BENCHMARK(BM_Vfexpd);

void BM_StdSqrt(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 0.000001;
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::sqrt(i * step_size));
    i += 1.0;
    if (i > 10000000.0) i = 1.0;
  }
}
BENCHMARK(BM_StdSqrt);

void BM_Fpowd05(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 0.000001;
  for (auto _ : state) {
    benchmark::DoNotOptimize(fpowd(i * step_size, 0.5));
    i += 1.0;
    if (i > 10000000.0) i = 1.0;
  }
}
BENCHMARK(BM_Fpowd05);

void BM_Vfpowd05(benchmark::State& state) {
  double i = 1.0;
  const double step_size = 0.000001;
  for (auto _ : state) {
    benchmark::DoNotOptimize(vfpowd(i * step_size, 0.5));
    i += 1.0;
    if (i > 10000000.0) i = 1.0;
  }
}
BENCHMARK(BM_Vfpowd05);

}  // namespace
