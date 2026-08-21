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

#include "gloop/util/functional/to_shared_function.h"

#include <functional>
#include <memory>
#include <utility>

#include "gtest/gtest.h"

namespace util::functional {
namespace {

struct CustomFunctor {
  bool operator()() { return true; }
};

TEST(ToSharedFunctionTest, UnderlyingConstOperator) {
  EXPECT_TRUE(ToSharedFunction(CustomFunctor{})());
  EXPECT_TRUE(ToSharedFunction(std::make_unique<CustomFunctor>())());
  EXPECT_TRUE(ToSharedFunction(std::make_shared<CustomFunctor>())());
}

TEST(ToSharedFunctionTest, LambdaWithBoundMoveOnly) {
  auto int_ptr = std::make_unique<int>(3);
  EXPECT_EQ(ToSharedFunction(
                [int_ptr = std::move(int_ptr)]() { return *int_ptr + 1; })(),
            4);
}

TEST(ToSharedFunctionTest, MutableLambdaWithBoundMoveOnly) {
  auto int_ptr = std::make_unique<int>(3);
  EXPECT_EQ(ToSharedFunction([int_ptr = std::move(int_ptr)]() mutable {
              return *std::exchange(int_ptr, nullptr) + 1;
            })(),
            4);
}

TEST(ToSharedFunctionTest, ThroughStdFunctionBoundMoveOnly) {
  auto int_ptr = std::make_unique<int>(3);
  std::function<int()> std_function = ToSharedFunction(
      [int_ptr = std::move(int_ptr)]() -> int { return *int_ptr + 1; });
  int result = std_function();
  EXPECT_EQ(result, 4);
}

struct NonCopyableNonMovable {
  NonCopyableNonMovable() = default;
  ~NonCopyableNonMovable() = default;
  NonCopyableNonMovable(const NonCopyableNonMovable&) = delete;
  NonCopyableNonMovable operator=(const NonCopyableNonMovable&) = delete;

  int element = 3;
};

TEST(ToSharedFunctionTest, ForwardsArguments) {
  auto int_ptr = std::make_unique<int>(3);
  std::function<int(NonCopyableNonMovable&&)> std_function = ToSharedFunction(
      [int_ptr = std::move(int_ptr)](NonCopyableNonMovable&& forwarded) {
        return *int_ptr + 1;
      });
  EXPECT_EQ(std_function(NonCopyableNonMovable{}), 4);
}

}  // namespace
}  // namespace util::functional
