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

// Source code was authored by Bennet Yee in secure_random_unittest.cc. It
// was moved to this file so it can be utilized across multiple
// unit test files.

#include "gloop/util/random/random_base_testutils.h"

#include <cstdint>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "gloop/util/random/random_base.h"

ABSL_FLAG(bool, random_debug, false, "Emit debugging output for unit test");

ABSL_FLAG(int32_t, start_index, 0, "Starting index in degree-of-freedom table");

// degree_of_freedom_increment -- This is the primary knob for making
// the unit test run faster to fit in the 60 second unit test
// execution time limit.  The same tests can run as a regression test
// by modifying the increment.

ABSL_FLAG(int32_t, degree_of_freedom_increment, 27,
          "Step size through degree-of-freedom table");

ABSL_FLAG(int32_t, rand8_degree_of_freedom_increment, 1,
          "Step size through degree-of-freedom table for Rand8");

ABSL_FLAG(int32_t, chi_square_samples, 2 << 20,
          "Number of samples to use in the Chi-squared test");

ABSL_FLAG(int32_t, clone_count, 100,
          "Number of output to compare in CloneTest");

ABSL_FLAG(int32_t, float_count, 1 << 13, "Number of samples for FloatTest");

ABSL_FLAG(int32_t, difference_test_count, 100,
          "Number of outputs to compare in DifferenceTest");

ABSL_FLAG(int32_t, max_string_len, 256 * 1024, "Maximum random string length");

ABSL_FLAG(int32_t, string_test_count, 100,
          "Number of iterations of RandString");

// Precomputed threshold values.  Each table gives the upper critical
// values for various degrees of freedom.  With the probability
// (associated with the table), the computed chi-square score at the
// given degree of freedom should not exceed the upper critical
// values.  See the ChiSquare object for how the chi-square score is
// computed and what it measures.
//
// see http://www.itl.nist.gov/div898/handbook/eda/section3/eda3674.htm

struct ChiThresh {
  int deg_of_freedom;
  double expected_chi_square;
};

#if 0
// clang-format off
// this table gives threshold values for 95%, upper critical values.
const ChiThresh tbl95[] = {
  {1,     3.841, },
  {2,     5.991, },
  {3,     7.815, },
  {4,     9.488, },
  {5,    11.070, },
  {6,    12.592, },
  {7,    14.067, },
  {8,    15.507, },
  {9,    16.919, },
  {10,    18.307, },
  {11,    19.675, },
  {12,    21.026, },
  {13,    22.362, },
  {14,    23.685, },
  {15,    24.996, },
  {16,    26.296, },
  {17,    27.587, },
  {18,    28.869, },
  {19,    30.144, },
  {20,    31.410, },
  {21,    32.671, },
  {22,    33.924, },
  {23,    35.172, },
  {24,    36.415, },
  {25,    37.652, },
  {26,    38.885, },
  {27,    40.113, },
  {28,    41.337, },
  {29,    42.557, },
  {30,    43.773, },
  {31,    44.985, },
  {32,    46.194, },
  {33,    47.400, },
  {34,    48.602, },
  {35,    49.802, },
  {36,    50.998, },
  {37,    52.192, },
  {38,    53.384, },
  {39,    54.572, },
  {40,    55.758, },
  {41,    56.942, },
  {42,    58.124, },
  {43,    59.304, },
  {44,    60.481, },
  {45,    61.656, },
  {46,    62.830, },
  {47,    64.001, },
  {48,    65.171, },
  {49,    66.339, },
  {50,    67.505, },
  {51,    68.669, },
  {52,    69.832, },
  {53,    70.993, },
  {54,    72.153, },
  {55,    73.311, },
  {56,    74.468, },
  {57,    75.624, },
  {58,    76.778, },
  {59,    77.931, },
  {60,    79.082, },
  {61,    80.232, },
  {62,    81.381, },
  {63,    82.529, },
  {64,    83.675, },
  {65,    84.821, },
  {66,    85.965, },
  {67,    87.108, },
  {68,    88.250, },
  {69,    89.391, },
  {70,    90.531, },
  {71,    91.670, },
  {72,    92.808, },
  {73,    93.945, },
  {74,    95.081, },
  {75,    96.217, },
  {76,    97.351, },
  {77,    98.484, },
  {78,    99.617, },
  {79,   100.749, },
  {80,   101.879, },
  {81,   103.010, },
  {82,   104.139, },
  {83,   105.267, },
  {84,   106.395, },
  {85,   107.522, },
  {86,   108.648, },
  {87,   109.773, },
  {88,   110.898, },
  {89,   112.022, },
  {90,   113.145, },
  {91,   114.268, },
  {92,   115.390, },
  {93,   116.511, },
  {94,   117.632, },
  {95,   118.752, },
  {96,   119.871, },
  {97,   120.990, },
  {98,   122.108, },
  {99,   123.225, },
  {100,   124.342, },
};

// this table gives threshold values for 99%, upper critical values.
const ChiThresh tbl99[] = {
  {1,     6.635, },
  {2,     9.210, },
  {3,    11.345, },
  {4,    13.277, },
  {5,    15.086, },
  {6,    16.812, },
  {7,    18.475, },
  {8,    20.090, },
  {9,    21.666, },
  {10,    23.209, },
  {11,    24.725, },
  {12,    26.217, },
  {13,    27.688, },
  {14,    29.141, },
  {15,    30.578, },
  {16,    32.000, },
  {17,    33.409, },
  {18,    34.805, },
  {19,    36.191, },
  {20,    37.566, },
  {21,    38.932, },
  {22,    40.289, },
  {23,    41.638, },
  {24,    42.980, },
  {25,    44.314, },
  {26,    45.642, },
  {27,    46.963, },
  {28,    48.278, },
  {29,    49.588, },
  {30,    50.892, },
  {31,    52.191, },
  {32,    53.486, },
  {33,    54.776, },
  {34,    56.061, },
  {35,    57.342, },
  {36,    58.619, },
  {37,    59.893, },
  {38,    61.162, },
  {39,    62.428, },
  {40,    63.691, },
  {41,    64.950, },
  {42,    66.206, },
  {43,    67.459, },
  {44,    68.710, },
  {45,    69.957, },
  {46,    71.201, },
  {47,    72.443, },
  {48,    73.683, },
  {49,    74.919, },
  {50,    76.154, },
  {51,    77.386, },
  {52,    78.616, },
  {53,    79.843, },
  {54,    81.069, },
  {55,    82.292, },
  {56,    83.513, },
  {57,    84.733, },
  {58,    85.950, },
  {59,    87.166, },
  {60,    88.379, },
  {61,    89.591, },
  {62,    90.802, },
  {63,    92.010, },
  {64,    93.217, },
  {65,    94.422, },
  {66,    95.626, },
  {67,    96.828, },
  {68,    98.028, },
  {69,    99.228, },
  {70,   100.425, },
  {71,   101.621, },
  {72,   102.816, },
  {73,   104.010, },
  {74,   105.202, },
  {75,   106.393, },
  {76,   107.583, },
  {77,   108.771, },
  {78,   109.958, },
  {79,   111.144, },
  {80,   112.329, },
  {81,   113.512, },
  {82,   114.695, },
  {83,   115.876, },
  {84,   117.057, },
  {85,   118.236, },
  {86,   119.414, },
  {87,   120.591, },
  {88,   121.767, },
  {89,   122.942, },
  {90,   124.116, },
  {91,   125.289, },
  {92,   126.462, },
  {93,   127.633, },
  {94,   128.803, },
  {95,   129.973, },
  {96,   131.141, },
  {97,   132.309, },
  {98,   133.476, },
  {99,   134.642, },
  {100,   135.807, },
};
#endif

// this table gives threshold values for 99.9%, upper critical values.
const ChiThresh tbl999[] = {
  {1,    10.828, },
  {2,    13.816, },
  {3,    16.266, },
  {4,    18.467, },
  {5,    20.515, },
  {6,    22.458, },
  {7,    24.322, },
  {8,    26.125, },
  {9,    27.877, },
  {10,    29.588, },
  {11,    31.264, },
  {12,    32.910, },
  {13,    34.528, },
  {14,    36.123, },
  {15,    37.697, },
  {16,    39.252, },
  {17,    40.790, },
  {18,    42.312, },
  {19,    43.820, },
  {20,    45.315, },
  {21,    46.797, },
  {22,    48.268, },
  {23,    49.728, },
  {24,    51.179, },
  {25,    52.620, },
  {26,    54.052, },
  {27,    55.476, },
  {28,    56.892, },
  {29,    58.301, },
  {30,    59.703, },
  {31,    61.098, },
  {32,    62.487, },
  {33,    63.870, },
  {34,    65.247, },
  {35,    66.619, },
  {36,    67.985, },
  {37,    69.347, },
  {38,    70.703, },
  {39,    72.055, },
  {40,    73.402, },
  {41,    74.745, },
  {42,    76.084, },
  {43,    77.419, },
  {44,    78.750, },
  {45,    80.077, },
  {46,    81.400, },
  {47,    82.720, },
  {48,    84.037, },
  {49,    85.351, },
  {50,    86.661, },
  {51,    87.968, },
  {52,    89.272, },
  {53,    90.573, },
  {54,    91.872, },
  {55,    93.168, },
  {56,    94.461, },
  {57,    95.751, },
  {58,    97.039, },
  {59,    98.324, },
  {60,    99.607, },
  {61,   100.888, },
  {62,   102.166, },
  {63,   103.442, },
  {64,   104.716, },
  {65,   105.988, },
  {66,   107.258, },
  {67,   108.526, },
  {68,   109.791, },
  {69,   111.055, },
  {70,   112.317, },
  {71,   113.577, },
  {72,   114.835, },
  {73,   116.092, },
  {74,   117.346, },
  {75,   118.599, },
  {76,   119.850, },
  {77,   121.100, },
  {78,   122.348, },
  {79,   123.594, },
  {80,   124.839, },
  {81,   126.083, },
  {82,   127.324, },
  {83,   128.565, },
  {84,   129.804, },
  {85,   131.041, },
  {86,   132.277, },
  {87,   133.512, },
  {88,   134.746, },
  {89,   135.978, },
  {90,   137.208, },
  {91,   138.438, },
  {92,   139.666, },
  {93,   140.893, },
  {94,   142.119, },
  {95,   143.344, },
  {96,   144.567, },
  {97,   145.789, },
  {98,   147.010, },
  {99,   148.230, },
  {100,   149.449, },
};
// clang-format on

// Abstracted random number generator classes used in template
// instantiation for a generalized chi-square test.

template <typename T>
class TypedRng {};

template <>
class TypedRng<uint8_t> {
 public:
  explicit TypedRng<uint8_t>(RandomBase* gen) : gen_(gen) { ; }
  void sample(uint8_t* val) { *val = gen_->Rand8(); }

 private:
  RandomBase* gen_;
};

template <>
class TypedRng<uint16_t> {
 public:
  explicit TypedRng<uint16_t>(RandomBase* gen) : gen_(gen) { ; }
  void sample(uint16_t* val) { *val = gen_->Rand16(); }

 private:
  RandomBase* gen_;
};

template <>
class TypedRng<uint32_t> {
 public:
  explicit TypedRng<uint32_t>(RandomBase* gen) : gen_(gen) { ; }
  void sample(uint32_t* val) { *val = gen_->Rand32(); }

 private:
  RandomBase* gen_;
};

template <>
class TypedRng<uint64_t> {
 public:
  explicit TypedRng<uint64_t>(RandomBase* gen) : gen_(gen) { ; }
  void sample(uint64_t* val) { *val = gen_->Rand64(); }

 private:
  RandomBase* gen_;
};

template <>
class TypedRng<float> {
 public:
  explicit TypedRng<float>(RandomBase* gen) : gen_(gen) { ; }
  void sample(float* val) { *val = gen_->RandFloat(); }

 private:
  RandomBase* gen_;
};

template <>
class TypedRng<double> {
 public:
  explicit TypedRng<double>(RandomBase* gen) : gen_(gen) { ; }
  void sample(double* val) { *val = gen_->RandDouble(); }

 private:
  RandomBase* gen_;
};

// Classifiers.  These classify the random value uniformly into buckets.

template <typename T>
class IntegralClassifier {
 public:
  explicit IntegralClassifier(int number_buckets)
      : number_buckets_(number_buckets) {}
  // classify_value returns an integer [0, number_buckets_).
  int classify_value(T value) { return value % number_buckets_; }

 private:
  int number_buckets_;
};

template <typename FT>
class FloatingPointClassifier {
 public:
  explicit FloatingPointClassifier<FT>(int number_buckets)
      : number_buckets_(number_buckets) {}
  // classify_value returns an integer [0, number_buckets_).
  int classify_value(FT value) {
    // value in [0.0, 1.0)
    int bucket_index(static_cast<int>(value * number_buckets_));
    CHECK_LE(0, bucket_index);
    CHECK_LT(bucket_index, number_buckets_);
    return bucket_index;
  }

 private:
  int number_buckets_;
};

// ChiSquare -- performs the chi-square test to measure how far from
// uniformly random a pseudo-random number generator is.  This test
// has a hardwired-in number of samples to draw from the generator,
// enough for all the tests that it will do.  This is done primarily
// to avoid invoking the pseudo-random number generator too often,
// since some (future, as-yet-to-be-implemented) generators can be
// expensive and we want to examine the output at several different
// degrees of freedom.  After initializing the table of samples, the
// ChiSquare object maybe be queried via chi_square to compute the
// chi-square test score at various degrees of freedom.
//
// The chi square value is defined as follows.  Classify the samples
// into N=(degree-of-freedom plus 1) different classes.  The chi square
// test score is defined as
//
// \chi^2 = sum_{i=1}^N (O_i - E_i)^2 / E_i
//
// where O_i is the observed number of samples in class i, and E_i is
// the expected number of samples in class i.
//
// Here we divide the samples into equal size buckets, so E_i is the
// same, kNChisqSamples / N if the distribution is truly uniform.
// This means that we can simplify the equation slightly:
//
// \chi^2 = \frac{1}{E} sum_{i=1}^N (O_i - E)^2

template <typename T, class Classifier>
class ChiSquare {
 public:
  explicit ChiSquare(RandomBase* gen) {
    TypedRng<T> rng(gen);
    r_ = std::unique_ptr<T[]>(new T[absl::GetFlag(FLAGS_chi_square_samples)]);
    for (int i = 0; i < absl::GetFlag(FLAGS_chi_square_samples); ++i) {
      rng.sample(&r_[i]);
    }
  }

  double ComputeChiSquareAt(int degrees_of_freedom) {
    int n = degrees_of_freedom + 1;
    std::unique_ptr<uint32_t[]> freq(Buckets(n));  // uint32 freq[n];

    double expected_count =
        absl::GetFlag(FLAGS_chi_square_samples) / static_cast<double>(n);
    if (absl::GetFlag(FLAGS_random_debug)) {
      LOG(INFO) << absl::StrFormat("Expected flat distribution count is %9.5f",
                                   expected_count);
    }
    double chisum = 0.0;
    for (int i = 0; i < n; ++i) {
      double diff = freq[i] - expected_count;
      chisum += diff * diff;
    }
    return chisum / expected_count;
  }

  void DumpValues(int degrees_of_freedom) {
    int n = degrees_of_freedom + 1;
    std::unique_ptr<uint32_t[]> freq(Buckets(n));
    for (int i = 0; i < n; ++i) {
      LOG(INFO) << absl::StrFormat("bucket %3d: %5d", i,
                                   static_cast<int>(freq[i]));
    }
  }

 private:
  uint32_t* Buckets(int num_buckets) {
    uint32_t* freq = new uint32_t[num_buckets];
    Classifier which_bucket(num_buckets);

    for (int i = 0; i < num_buckets; ++i) freq[i] = 0;
    for (int i = 0; i < absl::GetFlag(FLAGS_chi_square_samples); ++i) {
      ++freq[which_bucket.classify_value(r_[i])];
    }
    return freq;
  }

  std::unique_ptr<T[]> r_;  // random samples
};

typedef ChiSquare<uint8_t, IntegralClassifier<uint8_t> > ChiSquareUint8;
typedef std::unique_ptr<ChiSquareUint8> ChiSquareUint8Data;

typedef ChiSquare<uint16_t, IntegralClassifier<uint16_t> > ChiSquareUint16;
typedef std::unique_ptr<ChiSquareUint16> ChiSquareUint16Data;

typedef ChiSquare<uint32_t, IntegralClassifier<uint32_t> > ChiSquareUint32;
typedef std::unique_ptr<ChiSquareUint32> ChiSquareUint32Data;

typedef ChiSquare<uint64_t, IntegralClassifier<uint64_t> > ChiSquareUint64;
typedef std::unique_ptr<ChiSquareUint64> ChiSquareUint64Data;

typedef ChiSquare<float, FloatingPointClassifier<float> > ChiSquareFloat;
typedef std::unique_ptr<ChiSquareFloat> ChiSquareFloatData;

typedef ChiSquare<double, FloatingPointClassifier<double> > ChiSquareDouble;
typedef std::unique_ptr<ChiSquareDouble> ChiSquareDoubleData;

// Generic helper template function for StatisticsTest below

namespace {

template <class T>
bool DoStatistics(T* data, int start_index, const ChiThresh* tbl, int tbl_size,
                  int increment) {
  bool status = true;
  for (int i = start_index; i < tbl_size; i += increment) {
    double chisq = data->ComputeChiSquareAt(tbl[i].deg_of_freedom);
    if (absl::GetFlag(FLAGS_random_debug)) {
      LOG(INFO) << absl::StrFormat(
          "Chi square w/ %3d deg-freedom: %9.5f; max: %9.5f",
          tbl[i].deg_of_freedom, chisq, tbl[i].expected_chi_square);
    }

    if (absl::GetFlag(FLAGS_random_debug) &&
        !(chisq < tbl[i].expected_chi_square)) {
      // dump out values
      data->DumpValues(tbl[i].deg_of_freedom);
    }
    if (!(chisq < tbl[i].expected_chi_square)) {
      LOG(ERROR) << "Expectation error: chisq < tbl[i].expected_chi_square"
                 << " at " << tbl[i].deg_of_freedom << " degrees of freedom";
      status = false;
    }
  }
  return status;
}

// Specialize for uint8 since Rand8 output range is too small, so
// the DoF must be one less than a power of 2.

template <>
bool DoStatistics<ChiSquareUint8>(ChiSquareUint8* data, int start_index,
                                  const ChiThresh* tbl, int tbl_size,
                                  int increment) {
  bool status = true;
  for (int i = start_index; i < tbl_size;
       i += absl::GetFlag(FLAGS_rand8_degree_of_freedom_increment)) {
    int check_degree = tbl[i].deg_of_freedom;
    // if check_degree+1 is a power of two, then check_degree in binary
    // will look like:
    //   00...00111..111
    // and check_degree+1 will look like
    //   00...01000..000
    // and the bitwise AND will be zero.  In all other cases, their
    // bitwise AND will be non-zero.
    if (check_degree & (check_degree + 1)) {
      if (absl::GetFlag(FLAGS_random_debug)) {
        LOG(INFO) << absl::StrFormat("Skipping Rand8 DoF %d", check_degree);
      }
      continue;
    }
    double chisq = data->ComputeChiSquareAt(tbl[i].deg_of_freedom);
    if (absl::GetFlag(FLAGS_random_debug)) {
      LOG(INFO) << absl::StrFormat(
          "Chi square w/ %3d deg-freedom: %9.5f; max: %9.5f",
          tbl[i].deg_of_freedom, chisq, tbl[i].expected_chi_square);
    }

    if (absl::GetFlag(FLAGS_random_debug) &&
        !(chisq < tbl[i].expected_chi_square)) {
      // dump out values
      data->DumpValues(tbl[i].deg_of_freedom);
    }
    if (!(chisq < tbl[i].expected_chi_square)) {
      LOG(ERROR) << "Expectation error: chisq < tbl[i].expected_chi_square"
                 << " at " << tbl[i].deg_of_freedom << " degrees of freedom";
      status = false;
    }
  }
  return status;
}

// We compute chi-square statistics at various degrees of freedom and
// check that the value is below the maximum threshold.

bool ChiSquaredStatisticsTest(const char* name, RandomBase* gen) {
  // run chi square test and compare results from upper critical thresholds
  bool status = true;

  // decide what threshold to use by selecting appropriate table here
  const ChiThresh* tbl = tbl999;
  int tbl_size = sizeof tbl999 / sizeof tbl999[0];

  int start_index = absl::GetFlag(FLAGS_start_index);

  // for unittests that must finish in a minute of cpu time, we skip
  // some degrees_of_freedom
  int increment = absl::GetFlag(FLAGS_degree_of_freedom_increment);

  CHECK_GE(start_index, 0);
  CHECK_GT(increment, 0);

  LOG(INFO) << "Chi-squared statistics, using " << name;
  if (absl::GetFlag(FLAGS_random_debug)) {
    LOG(INFO) << "Step size " << increment;
  }

  LOG(INFO) << "Rand8 distribution";

  ChiSquareUint8Data uint8data(new ChiSquareUint8(gen));
  status &=
      DoStatistics(uint8data.get(), start_index, tbl, tbl_size, increment);
  uint8data.reset();

  LOG(INFO) << "OK";

  LOG(INFO) << "Rand16 distribution";

  ChiSquareUint16Data uint16data(new ChiSquareUint16(gen));
  status &=
      DoStatistics(uint16data.get(), start_index, tbl, tbl_size, increment);
  uint16data.reset();

  LOG(INFO) << "OK";

  LOG(INFO) << "Rand32 distribution";

  ChiSquareUint32Data uint32data(new ChiSquareUint32(gen));
  status &=
      DoStatistics(uint32data.get(), start_index, tbl, tbl_size, increment);
  uint32data.reset();

  LOG(INFO) << "OK";

  LOG(INFO) << "Rand64 distribution";

  ChiSquareUint64Data uint64data(new ChiSquareUint64(gen));
  status &=
      DoStatistics(uint64data.get(), start_index, tbl, tbl_size, increment);
  uint64data.reset();

  LOG(INFO) << "OK";

  LOG(INFO) << "RandFloat distribution";

  ChiSquareFloatData fdata(new ChiSquareFloat(gen));
  status &= DoStatistics(fdata.get(), start_index, tbl, tbl_size, increment);
  fdata.reset();

  LOG(INFO) << "OK";

  LOG(INFO) << "RandDouble distribution";

  ChiSquareDoubleData ddata(new ChiSquareDouble(gen));
  status &= DoStatistics(ddata.get(), start_index, tbl, tbl_size, increment);
  ddata.reset();

  LOG(INFO) << "OK";

  // LOG(INFO) << "DEPRECATED_RndDouble distribution";
  // ...
  // LOG(INFO) << "OK";

  return status;
}
}  // namespace

bool Statistics(const char* name, RandomBase* gen) {
  return ChiSquaredStatisticsTest(name, gen);
}

void CloneTest(const char* name, RandomBase* gen) {
  LOG(INFO) << "CloneTest: " << name;

  std::unique_ptr<RandomBase> clone(gen->Clone());
  CHECK_NE(clone.get(), static_cast<RandomBase*>(nullptr))
      << "CloneTest:  Clone method returned NULL";
  std::unique_ptr<uint32_t[]> buf(
      new uint32_t[absl::GetFlag(FLAGS_clone_count)]);

  LOG(INFO) << "CloneTest";
  for (int i = 0; i < absl::GetFlag(FLAGS_clone_count); ++i) {
    buf[i] = gen->Rand32();
  }
  for (int i = 0; i < absl::GetFlag(FLAGS_clone_count); ++i) {
    CHECK(clone->Rand32() == buf[i])
        << " cloned stream differs after " << i << " outputs";
  }
  LOG(INFO) << "OK";
}

void NoCloneTest(const char* name, RandomBase* gen) {
  LOG(INFO) << "NoCloneTest: " << name;
  std::unique_ptr<RandomBase> clone(gen->Clone());
  CHECK_EQ(clone.get(), static_cast<RandomBase*>(nullptr))
      << ": should not be Clone()-able.";
}

void FloatTest(const char* name, RandomBase* gen) {
  LOG(INFO) << "FloatTest: " << name;
  for (int i = 0; i < absl::GetFlag(FLAGS_float_count); ++i) {
    float f = gen->RandFloat();
    CHECK(0.0 <= f && f < 1.0) << ": RandFloat output outside of [0,1)\n"
                               << absl::StrFormat("bad value: %15.13g", f);
  }

  for (int i = 0; i < absl::GetFlag(FLAGS_float_count); ++i) {
    double d = gen->RandDouble();
    CHECK(0.0 <= d && d < 1.0) << ": RandDouble output outside of [0,1)\n"
                               << absl::StrFormat("bad value: %15.13g", d);
  }
  LOG(INFO) << "OK";
}

void DifferenceTest(const char* names, class RandomBase* gen1,
                    class RandomBase* gen2) {
  bool different = false;

  LOG(INFO) << "Paranoid difference output test, " << names;

  for (int i = 0; i < absl::GetFlag(FLAGS_difference_test_count); ++i) {
    if (gen1->Rand32() != gen2->Rand32()) {
      different = true;
      break;
    }
  }
  CHECK(different) << "different seeds but same output!";

  LOG(INFO) << "OK";
}

void StringTest(const char* name, class RandomBase* gen) {
  CHECK_GT(absl::GetFlag(FLAGS_max_string_len), 0);
  for (int i = 0; i < absl::GetFlag(FLAGS_string_test_count); ++i) {
    int len = gen->Rand32() % absl::GetFlag(FLAGS_max_string_len);
    std::string s = gen->RandString(len);

    CHECK(s.length() == len);
  }
}
