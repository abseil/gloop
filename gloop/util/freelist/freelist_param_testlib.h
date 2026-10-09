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

#ifndef THIRD_PARTY_GLOOP_UTIL_FREELIST_FREELIST_PARAM_TESTLIB_H_
#define THIRD_PARTY_GLOOP_UTIL_FREELIST_FREELIST_PARAM_TESTLIB_H_

#include <functional>
#include <limits>
#include <memory>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/flags/flag.h"
#include "absl/flags/reflection.h"
#include "gloop/util/freelist/freelist.h"
#include "gtest/gtest.h"

namespace freelist {

class FreeListBaseTest : public testing::Test {
 public:
  class EnsureDeleted {
   public:
    EnsureDeleted() { ++live_; }
    ~EnsureDeleted() { --live_; }
    static int live() { return live_; }

    void set_val(int v) { val_ = v; }
    int val() const { return val_; }

   private:
    int val_ = std::numeric_limits<int>::min();
  };

 protected:
  FreeListBaseTest() {
    absl::SetFlag(&FLAGS_use_freelists, true);
    live_ = 0;
  }
  ~FreeListBaseTest() override {
    EXPECT_EQ(EnsureDeleted::live(), 0);
    // As a courtesy and strictness, reset the static variable after the test.
    live_ = 0;
  }

 private:
  absl::FlagSaver flag_saver_;
  inline static int live_ = 0;
};

struct FreeListTestParams {
  // A name added to the test name. See GetTestName(). If empty, the default
  // name is used.
  std::string test_name;
  std::function<std::unique_ptr<AbstractFreeList<
      FreeListBaseTest::EnsureDeleted>> absl_nonnull()> absl_nonnull
  create_freelist;
  int size ABSL_REQUIRE_EXPLICIT_INIT;
  // Some freelists maintain strict accounting and are guaranteed to delete any
  // items that are pushed beyond the max capacity and to never malloc a new
  // item if one is available in the freelist.  For efficiency reasons, other
  // freelists are permitted to be lazy and won't necessarily have those
  // properties.  If that is the case, this should be called with can_be_lazy
  // set to false.  Lazy lists should not leak memory, but won't necessarily
  // call malloc and delete when expected.
  bool can_be_lazy = false;
};

std::string GetTestName(const testing::TestParamInfo<FreeListTestParams>& info);

class FreeListConformanceTest
    : public FreeListBaseTest,
      public testing::WithParamInterface<FreeListTestParams> {};

}  // namespace freelist

#endif  // THIRD_PARTY_GLOOP_UTIL_FREELIST_FREELIST_PARAM_TESTLIB_H_
