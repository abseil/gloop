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
#include <string>

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
  EXPECT_DEATH(ValueOrDie(std::unique_ptr<std::nullptr_t>()), "ValueOrDie");
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

TEST(BottomValueOrDieTest, UnwrappedValue) {
  int val = 42;
  EXPECT_EQ(BottomValueOrDie(val), 42);
  EXPECT_EQ(BottomValueOrDie(42), 42);
  std::string s = "hello";
  EXPECT_EQ(BottomValueOrDie(s), "hello");
}

TEST(BottomValueOrDieTest, SingleLevel) {
  absl::StatusOr<int> statusor_val = 10;
  EXPECT_EQ(BottomValueOrDie(statusor_val), 10);

  std::optional<int> opt_val = 20;
  EXPECT_EQ(BottomValueOrDie(opt_val), 20);

  int raw_val = 30;
  EXPECT_EQ(BottomValueOrDie(&raw_val), 30);

  auto ptr_val = std::make_unique<int>(40);
  EXPECT_EQ(BottomValueOrDie(ptr_val), 40);
}

TEST(BottomValueOrDieTest, NestedLayers) {
  // Variant 1: StatusOr < optional < shared_ptr < int > > >
  absl::StatusOr<std::optional<std::shared_ptr<int>>> nested1 =
      std::make_optional(std::make_shared<int>(100));
  EXPECT_EQ(BottomValueOrDie(nested1), 100);

  // Variant 2: unique_ptr < StatusOr < optional < int > > >
  auto nested2 = std::make_unique<absl::StatusOr<std::optional<int>>>(
      absl::StatusOr<std::optional<int>>(std::make_optional(200)));
  EXPECT_EQ(BottomValueOrDie(nested2), 200);

  // Variant 3: StatusOr < unique_ptr < optional < int* > > >
  int target = 300;
  absl::StatusOr<std::unique_ptr<std::optional<int*>>> nested3 =
      std::make_unique<std::optional<int*>>(&target);
  EXPECT_EQ(BottomValueOrDie(nested3), 300);
}

TEST(BottomValueOrDieTest, CrashesOnEmptyLayers) {
  // 3-level chain: StatusOr < optional < shared_ptr < int > > >

  // Top layer fails: StatusOr is not ok
  absl::StatusOr<std::optional<std::shared_ptr<int>>> nested_failed_status =
      absl::InternalError("failed");
  EXPECT_DEATH(BottomValueOrDie(nested_failed_status), "failed");

  // Middle layer fails: optional is empty
  absl::StatusOr<std::optional<std::shared_ptr<int>>> nested_empty_opt =
      std::nullopt;
  EXPECT_DEATH(BottomValueOrDie(nested_empty_opt), "ValueOrDie");

  // Bottom layer fails: shared_ptr is null
  absl::StatusOr<std::optional<std::shared_ptr<int>>> nested_null_ptr =
      std::make_optional(std::shared_ptr<int>(nullptr));
  EXPECT_DEATH(BottomValueOrDie(nested_null_ptr), "ValueOrDie");

  // 4-level chain: optional < StatusOr < unique_ptr < int* > > >

  // Level 1 fails: optional is empty
  std::optional<absl::StatusOr<std::unique_ptr<int*>>> chain_empty_opt =
      std::nullopt;
  EXPECT_DEATH(BottomValueOrDie(chain_empty_opt), "ValueOrDie");

  // Level 2 fails: StatusOr is not ok
  std::optional<absl::StatusOr<std::unique_ptr<int*>>> chain_failed_status =
      absl::StatusOr<std::unique_ptr<int*>>(
          absl::InvalidArgumentError("badarg"));
  EXPECT_DEATH(BottomValueOrDie(chain_failed_status), "badarg");

  // Level 3 fails: unique_ptr is null
  std::optional<absl::StatusOr<std::unique_ptr<int*>>> chain_null_uniq =
      absl::StatusOr<std::unique_ptr<int*>>(std::unique_ptr<int*>(nullptr));
  EXPECT_DEATH(BottomValueOrDie(chain_null_uniq), "ValueOrDie");

  // Level 4 fails: raw pointer is null
  std::optional<absl::StatusOr<std::unique_ptr<int*>>> chain_null_raw =
      absl::StatusOr<std::unique_ptr<int*>>(std::make_unique<int*>(nullptr));
  EXPECT_DEATH(BottomValueOrDie(chain_null_raw), "ValueOrDie");
}

}  // namespace
}  // namespace gtl
