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

#include "gloop/util/gtl/value_or_die.h"

#include <cstddef>
#include <memory>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace gtl {
namespace {

using ::testing::Eq;
using ::testing::Pointee;

class Immovable {
 public:
  Immovable() = default;
  Immovable(const Immovable&) = delete;
  Immovable(const Immovable&&) = delete;
};

TEST(ValueOrDieTest, StatusOr) {
  absl::StatusOr<int> int_value = 1;
  EXPECT_EQ(ValueOrDie(int_value), 1);
  std::unique_ptr<int> moveable_value = ValueOrDie(
      absl::StatusOr<std::unique_ptr<int>>(std::make_unique<int>(1)));
  EXPECT_THAT(moveable_value, Pointee(Eq(1)));
  absl::StatusOr<Immovable> immovable_value;
  immovable_value.emplace();
  EXPECT_EQ(&ValueOrDie(immovable_value), &*immovable_value);
  absl::StatusOr<int> failed = absl::NotFoundError("not found");
  EXPECT_DEATH(ValueOrDie(failed), "ValueOrDie.*not found");
}

TEST(ValueOrDieTest, Optional) {
  std::optional<int> int_value = 1;
  EXPECT_EQ(ValueOrDie(int_value), 1);
  std::unique_ptr<int> moveable_value =
      ValueOrDie(std::optional<std::unique_ptr<int>>(std::make_unique<int>(1)));
  EXPECT_THAT(moveable_value, Pointee(Eq(1)));
  std::optional<Immovable> immovable_value;
  immovable_value.emplace();
  EXPECT_EQ(&ValueOrDie(immovable_value), &*immovable_value);
  EXPECT_DEATH(ValueOrDie(std::optional<int>()), "ValueOrDie");
}

TEST(ValueOrDieTest, Pointer) {
  int int_value = 1;
  const int& int_ref = ValueOrDie(&int_value);
  EXPECT_EQ(int_ref, 1);
  Immovable immovable_value;
  EXPECT_EQ(&ValueOrDie(&immovable_value), &immovable_value);
  EXPECT_DEATH(ValueOrDie(static_cast<int*>(nullptr)), "ValueOrDie");
}

TEST(ValueOrDieTest, SmartPointer) {
  auto int_value = std::make_unique<int>(1);
  EXPECT_EQ(ValueOrDie(int_value), 1);
  auto immovable_value = std::make_unique<Immovable>();
  EXPECT_EQ(&ValueOrDie(immovable_value), immovable_value.get());
  EXPECT_DEATH(ValueOrDie(std::unique_ptr<nullptr_t>()), "ValueOrDie");
}

TEST(ValueOrDieTest, Nested) {
  EXPECT_THAT(ValueOrDie(ValueOrDie(ValueOrDie(
                  absl::StatusOr<std::optional<std::unique_ptr<int>>>(
                      std::make_unique<int>(1))))),
              Eq(1));
}

TEST(ValueOrDieTest, StatusOrRefIsNotLifetimeBound) {
  int i = 0;
  int& r = gtl::ValueOrDie(absl::StatusOr<int&>(i));
  EXPECT_EQ(&r, &i);
}

}  // namespace
}  // namespace gtl
