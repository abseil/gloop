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

#include "absl/log/log.h"
#include "gloop/util/status/status.h"
#include "gtest/gtest.h"

namespace {

// Check that the canonical() ErrorSpace with space name "generic" is
// created and registered. Must be a standalone test to isolate it from
// other code which might call util::CanonicalErrorSpace(). That
// would interfere by creating the generic ErrorSpace explicitly. We
// want to make sure that it gets created spontaneously and without any
// help. See b/7566402 for details.
TEST(CanonicalInitTest, Init) {
  const util::ErrorSpace* space = util::ErrorSpace::Find("generic");
  LOG(INFO) << "space: " << space;
  ASSERT_NE(space, nullptr);
  ASSERT_EQ(space->SpaceName(), "generic");
}

}  // namespace
