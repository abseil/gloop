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

#include "gloop/util/status/return.h"

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "gloop/util/status/status_macros.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::testing::Pointee;

absl::Status IfFiveReturnError(int x) {
  return (x == 5) ? absl::InvalidArgumentError("x is 5") : absl::OkStatus();
}

TEST(ReturnTest, ReturnsBool) {
  auto if_five_return_false = [](int x) -> bool {
    RETURN_IF_ERROR(IfFiveReturnError(x)).With(::status_macros::Return(false));
    return true;
  };
  EXPECT_EQ(if_five_return_false(5), false);
  EXPECT_EQ(if_five_return_false(8), true);
}

TEST(ReturnTest, ReturnsInt) {
  auto if_five_return_404 = [](int x) -> int {
    RETURN_IF_ERROR(IfFiveReturnError(x)).With(::status_macros::Return(404));
    return 200;
  };
  EXPECT_EQ(if_five_return_404(5), 404);
  EXPECT_EQ(if_five_return_404(2), 200);
}

TEST(ReturnTest, ReturnsConstRef) {
  auto if_five_return_foo = [](int x) -> std::string {
    const std::string foo = "foo";
    RETURN_IF_ERROR(IfFiveReturnError(x)).With(::status_macros::Return(foo));
    return "bar";
  };
  EXPECT_EQ(if_five_return_foo(5), "foo");
  EXPECT_EQ(if_five_return_foo(9), "bar");
}

TEST(ReturnTest, ReturnsConstRefAdaptor) {
  auto if_five_return_foo = [](int x) -> std::string {
    const auto return_foo = ::status_macros::Return("foo");
    RETURN_IF_ERROR(IfFiveReturnError(x)).With(return_foo);
    return "bar";
  };
  EXPECT_EQ(if_five_return_foo(5), "foo");
  EXPECT_EQ(if_five_return_foo(9), "bar");
}

TEST(ReturnTest, ReturnsNullptr) {
  auto if_five_return_null = [](int x) -> std::unique_ptr<std::string> {
    RETURN_IF_ERROR(IfFiveReturnError(x))
        .With(::status_macros::Return(nullptr));
    return std::make_unique<std::string>("ok");
  };
  EXPECT_EQ(if_five_return_null(5), nullptr);
  EXPECT_THAT(if_five_return_null(8), Pointee<std::string>("ok"));
}

TEST(ReturnTest, ReturnsUniquePtr) {
  auto if_five_return_404 = [](int x) -> std::unique_ptr<int> {
    RETURN_IF_ERROR(IfFiveReturnError(x))
        .With(::status_macros::Return(std::make_unique<int>(404)));
    return std::make_unique<int>(200);
  };
  EXPECT_THAT(if_five_return_404(5), Pointee<int>(404));
  EXPECT_THAT(if_five_return_404(8), Pointee<int>(200));
}

TEST(ReturnTest, ReturnsVoid) {
  bool success = false;
  auto if_five_then_fail = [&success](int x) -> void {
    success = false;
    RETURN_IF_ERROR(IfFiveReturnError(x)).With(::status_macros::ReturnVoid());
    success = true;
  };

  if_five_then_fail(5);
  EXPECT_EQ(success, false);

  if_five_then_fail(2);
  EXPECT_EQ(success, true);
}

}  // namespace
