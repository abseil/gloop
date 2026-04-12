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

#include "gloop/util/gtl/subspan.h"

#include <stddef.h>

#include "absl/base/macros.h"
#include "absl/types/any_span.h"
#include "gloop/gloop_test.h"

namespace {

using ::testing::ElementsAre;

using AnyIntSpan = absl::AnySpan<int>;

bool IsHardened() {
  bool hardened = false;
  ABSL_HARDENING_ASSERT([&hardened]() {
    hardened = true;
    return true;
  }());
  return hardened;
}

TEST(SubspanTest, Subspan) {
  int arr[] = {0, 1, 2};
  AnyIntSpan span = arr;
  EXPECT_THAT(gtl::Subspan(span, 2, 1), ElementsAre(2));
  EXPECT_THAT(gtl::Subspan(span, 1, AnyIntSpan::npos), ElementsAre(1, 2));
#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    GLOOP_EXPECT_DEATH(gtl::Subspan(span, 0, 4), "");
    GLOOP_EXPECT_DEATH(gtl::Subspan(span, 3, 1), "");
    GLOOP_EXPECT_DEATH(gtl::Subspan(span, 4, AnyIntSpan::npos), "");
    GLOOP_EXPECT_DEATH(gtl::Subspan(span, AnyIntSpan::npos, 0), "");
    GLOOP_EXPECT_DEATH(gtl::Subspan(span, AnyIntSpan::npos, AnyIntSpan::npos),
                       "");
  }
#endif
}

TEST(SubspanTest, SubspanOrTruncate) {
  int arr[] = {0, 1, 2};
  AnyIntSpan span = arr;
  EXPECT_THAT(gtl::SubspanOrTruncate(span, 0, 3), ElementsAre(0, 1, 2));
  EXPECT_THAT(gtl::SubspanOrTruncate(span, 2, AnyIntSpan::npos),
              ElementsAre(2));
#if GTEST_HAS_DEATH_TEST
  if (IsHardened()) {
    GLOOP_EXPECT_DEATH(gtl::SubspanOrTruncate(span, 4, AnyIntSpan::npos), "");
    GLOOP_EXPECT_DEATH(gtl::SubspanOrTruncate(span, AnyIntSpan::npos, 0), "");
    GLOOP_EXPECT_DEATH(
        gtl::SubspanOrTruncate(span, AnyIntSpan::npos, AnyIntSpan::npos), "");
  }
#endif
}

}  // namespace
