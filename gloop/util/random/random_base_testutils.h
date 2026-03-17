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

// Source code authored by Bennet Yee in secure_random_unittest.cc. It
// was moved to this file so it can be utilized across multiple
// unit test files.
//

#ifndef UTIL_RANDOM_RANDOM_BASE_TESTUTILS_H__
#define UTIL_RANDOM_RANDOM_BASE_TESTUTILS_H__

#include "absl/base/attributes.h"
#include "absl/flags/declare.h"
#include "gloop/util/random/random_base.h"

// See comments in secure_random_testutils.cc for what these flags do.

ABSL_DECLARE_FLAG(bool, random_debug);

ABSL_DECLARE_FLAG(int32_t, start_index);

ABSL_DECLARE_FLAG(int32_t, degree_of_freedom_increment);

ABSL_DECLARE_FLAG(int32_t, rand8_degree_of_freedom_increment);

ABSL_DECLARE_FLAG(int32_t, chi_square_samples);

// We compute chi-square statistics at various degrees of freedom and
// check that the value is below the maximum threshold.  The things to
// tweak in the test code are: the ChiThresh table (tbl99, tbl999,
// etc.) for various threshold probabilities, and increment to adjust
// the total runtime for the unit test.
//
// If a generator is deterministically seeded by data from the unit
// test and the generator passes in a manual run of the unit test, all
// we are testing is that the generator code hasn't changed
// catastrophically.

// If a unit test creates a generator that has been deterministically
// seeded then we expect that it passes on the first attempt.
bool Statistics(const char* name, RandomBase* gen) ABSL_MUST_USE_RESULT;

// If a unit test wishes to create a generator is non-determinstically
// seeded, this function will repeatedly create new generators and retry
// statistical tests until either they pass or an upper limit on test
// retries is reached.
//
// If a generator is seeded using random seed material (e.g., from
// /dev/random or /dev/urandom), then the chi-squared test may, depending
// on the seed value used, encounter a chi-squared failure and so return
// false.  To work around this flakiness, this function will try the
// Statistics() test multiple times and return true iff the test passes
// at least required_passes out of num_attempts times.
//
// Choosing ChiThresh to be a high probability threshold will lower the
// number of retries needed.  The important thing is to monitor the retry
// count -- if it goes up, then there is something wrong with the code.
//
// Yes, arguably this might belong in a regression test instead, but
// it is good to run this more frequently (as w/ unit tests) to ensure
// that generators do not generate bad outputs.

ABSL_DECLARE_FLAG(int32_t, clone_count);

// CloneTest: ensure that the RandomBase->Clone() interface
// generates an object that matches the new sequence.
void CloneTest(const char* name, RandomBase* gen);

// NoCloneTest: ensure that the RandomBase->Clone() will always return
// NULL.
void NoCloneTest(const char* name, RandomBase* gen);

ABSL_DECLARE_FLAG(int32_t, float_count);

// FloatTest:
void FloatTest(const char* name, RandomBase* gen);

ABSL_DECLARE_FLAG(int32_t, difference_test_count);

// DifferenceTest: Ensure that two objects generate two distinct
// sequences of numbers.
void DifferenceTest(const char* names, class RandomBase* gen1,
                    class RandomBase* gen2);

ABSL_DECLARE_FLAG(int32_t, max_string_len);
ABSL_DECLARE_FLAG(int32_t, string_test_count);

// StringTest: just tests that output strings are of the appropriate length.
// The distribution of random bytes is handled by the Chi-square test for
// Rand8().

void StringTest(const char* name, class RandomBase* gen);

#endif  // UTIL_RANDOM_RANDOM_BASE_TESTUTILS_H__
