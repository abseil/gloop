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

#include "gloop/util/symbolize/symbolize.h"
#include "gtest/gtest.h"

// This test verifies that SymbolMap correctly detects that this test's main
// binary is stripped. (It needs to be in a different binary from the main
// symbolize unit test, since that is built unstripped, like most tests.)
TEST(SymbolMap, StrippedBinary) {
  EXPECT_TRUE(util::SymbolMap::GetCached().binary_is_stripped());
}
