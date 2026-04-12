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

#ifndef THIRD_PARTY_GLOOP_DEATH_TEST_H_
#define THIRD_PARTY_GLOOP_DEATH_TEST_H_

// IWYU pragma: begin_exports
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
// IWYU pragma: end_exports

#include "absl/base/config.h"  // IWYU pragma: keep

#if defined(ABSL_HAVE_THREAD_SANITIZER) ||  \
    defined(ABSL_HAVE_ADDRESS_SANITIZER) || \
    defined(ABSL_HAVE_MEMORY_SANITIZER)
#define GLOOP_EXPECT_DEATH(statement, regex) EXPECT_DEATH(statement, regex)
#define GLOOP_EXPECT_DEBUG_DEATH(statement, regex) \
  EXPECT_DEBUG_DEATH(statement, regex)
#define GLOOP_EXPECT_DEATH_IF_SUPPORTED(statement, regex) \
  EXPECT_DEATH_IF_SUPPORTED(statement, regex)
#define GLOOP_ASSERT_DEATH(statement, regex) ASSERT_DEATH(statement, regex)
#define GLOOP_ASSERT_DEBUG_DEATH(statement, regex) \
  ASSERT_DEBUG_DEATH(statement, regex)
#define GLOOP_ASSERT_DEATH_IF_SUPPORTED(statement, regex) \
  ASSERT_DEATH_IF_SUPPORTED(statement, regex)
#else
#define GLOOP_EXPECT_DEATH(statement, regex) \
  GTEST_UNSUPPORTED_DEATH_TEST(statement, regex, )
#define GLOOP_EXPECT_DEBUG_DEATH(statement, regex) \
  GTEST_UNSUPPORTED_DEATH_TEST(statement, regex, )
#define GLOOP_EXPECT_DEATH_IF_SUPPORTED(statement, regex) \
  GTEST_UNSUPPORTED_DEATH_TEST(statement, regex, )
#define GLOOP_ASSERT_DEATH(statement, regex) \
  GTEST_UNSUPPORTED_DEATH_TEST(statement, regex, return)
#define GLOOP_ASSERT_DEBUG_DEATH(statement, regex) \
  GTEST_UNSUPPORTED_DEATH_TEST(statement, regex, return)
#define GLOOP_ASSERT_DEATH_IF_SUPPORTED(statement, regex) \
  GTEST_UNSUPPORTED_DEATH_TEST(statement, regex, return)
#endif

#endif  // THIRD_PARTY_GLOOP_DEATH_TEST_H_
