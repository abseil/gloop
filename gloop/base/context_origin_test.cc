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

#include "gloop/base/context_origin.h"

#include <optional>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace base {
namespace {

using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Optional;

TEST(ContextOriginTest, Empty) {
  ContextOrigin origin;
  ASSERT_THAT(origin.stack_trace(), Eq(std::nullopt));
}

TEST(ContextOriginTest, HoldsStackTrace) {
  std::vector<void*> stack{reinterpret_cast<void*>(1)};
  ContextOrigin origin{stack};
  ASSERT_THAT(origin.stack_trace(), Optional(ElementsAreArray(stack)));
}

TEST(ContextOriginTest, CopyConstructor) {
  std::vector<void*> stack{reinterpret_cast<void*>(1)};
  ContextOrigin o1{stack};
  ContextOrigin o2(o1);
  ASSERT_THAT(o1.stack_trace(), Optional(ElementsAreArray(stack)));
  ASSERT_THAT(o2.stack_trace(), Optional(ElementsAreArray(stack)));
}

TEST(ContextOriginTest, MoveConstructor) {
  std::vector<void*> stack{reinterpret_cast<void*>(1)};
  ContextOrigin o1{stack};
  ContextOrigin o2(std::move(o1));
  ASSERT_THAT(o2.stack_trace(), Optional(ElementsAreArray(stack)));
}

}  // namespace
}  // namespace base
