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

// Test case for b/28089523.

#include <dlfcn.h>

#include <cstdint>
#include <string>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "gloop/util/symbolize/symbolize.h"
#include "gtest/gtest.h"

ABSL_FLAG(int32_t, offset_a_kb, 0, "offset to helper_A.so within combined.so");
ABSL_FLAG(int32_t, offset_b_kb, 0, "offset to helper_A.so within combined.so");

#if GRTE_DLOPEN
#define DLOPEN_FN __google_dlopen_with_offset
#elif STANDALONE_DLOPEN
#include "util/elf/google_dlopen_with_offset.h"  // IWYU pragma: keep, b/300560485

// Use a wrapper to bypass the build-time selection mechanism and ensure that
// this is testing the version that it intends to.

void* dlopen_standalone_wrapper(const char* path, int64_t offset, int flags) {
  auto handle = dlopen_with_offset::DlOpen(path, offset, flags);
  if (!handle.ok()) {
    return nullptr;
  }
  return *handle;
}
#define DLOPEN_FN dlopen_standalone_wrapper
#else
#pragma error "Unknown dlopen function"
#endif

using util::SymbolMap;

TEST(SymbolizeDlopenWithOffset, Basic) {
  CHECK_NE(0, absl::GetFlag(FLAGS_offset_a_kb))
      << "offset_a_kb should not be 0";
  CHECK_NE(0, absl::GetFlag(FLAGS_offset_b_kb))
      << "offset_b_kb should not be 0";

  const std::string helper_so =
      ::testing::SrcDir() +
      "/_main/gloop/util/symbolize/"
      "symbolize_dlopen_with_offset_test_helper_combined.so";
  const char* const fname = helper_so.c_str();

  // offset must match skip= arguments in BUILD of helper_combined.so
  void* h1 = __google_dlopen_with_offset(
      fname, absl::GetFlag(FLAGS_offset_a_kb) * 1024, RTLD_LAZY);
  ASSERT_NE(h1, nullptr) << dlerror();
  void* fn_A = dlsym(h1, "symbolize_dlopen_with_offset_test_helper_A");
  EXPECT_NE(fn_A, nullptr);

  // offset must match skip= arguments in BUILD of helper_combined.so
  void* h2 = __google_dlopen_with_offset(
      fname, absl::GetFlag(FLAGS_offset_b_kb) * 1024, RTLD_LAZY);
  ASSERT_NE(h2, nullptr) << dlerror();
  ASSERT_NE(h1, h2) << "Unexpectedly the same";
  void* fn_B = dlsym(h2, "symbolize_dlopen_with_offset_test_helper_B");
  EXPECT_NE(fn_B, nullptr);

  // And now try to symbolize them.
  const auto& symbol_map = util::SymbolMap::GetCached();

  const char* name = nullptr;
  uint64_t start = 0, size = 0;
  ASSERT_TRUE(symbol_map.GetSymbolInfoAtPosition(
      reinterpret_cast<uint64_t>(fn_A), &name, &start, &size));
  EXPECT_EQ(start, reinterpret_cast<uint64_t>(fn_A));
  EXPECT_STREQ(name, "symbolize_dlopen_with_offset_test_helper_A");

  ASSERT_TRUE(symbol_map.GetSymbolInfoAtPosition(
      reinterpret_cast<uint64_t>(fn_B), &name, &start, &size));
  EXPECT_EQ(start, reinterpret_cast<uint64_t>(fn_B));
  EXPECT_STREQ(name, "symbolize_dlopen_with_offset_test_helper_B");
}
