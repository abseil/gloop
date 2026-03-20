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

#include "gloop/util/gtl/extend/tuple.h"

#include <tuple>
#include <type_traits>
#include <vector>

#include "absl/meta/internal/constexpr_testing.h"
#include "gloop/util/gtl/extend/extend.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

struct CanBeTuple : public gtl::Extend<CanBeTuple>::With<gtl::TupleExtension> {
  int x = 0;
  int y = 0;
  float d = 0.0;
};

static_assert(std::is_same_v<gtl::AsTupleType<CanBeTuple>,
                             std::tuple<const int&, const int&, const float&>>,
              "Incorrect tuple type deduced.");

TEST(TupleExtensionTest, CanMakeTuple) {
  CanBeTuple can_be = {{}, 1, 2, 2.71f};
  std::tuple<const int&, const int&, const float&> tup = can_be.AsTuple();
  static_assert(
      std::is_const_v<
          std::remove_reference_t<decltype(std::get<0>(can_be.AsTuple()))>>);
  EXPECT_THAT(std::get<0>(tup), 1);
  EXPECT_THAT(&std::get<0>(tup), &can_be.x);
  static_assert(
      std::is_const_v<
          std::remove_reference_t<decltype(std::get<0>(can_be.AsTuple()))>>);
  EXPECT_THAT(std::get<1>(tup), 2);
  EXPECT_THAT(&std::get<1>(tup), &can_be.y);
  static_assert(
      std::is_const_v<
          std::remove_reference_t<decltype(std::get<0>(can_be.AsTuple()))>>);
  EXPECT_THAT(std::get<2>(tup), 2.71);
  EXPECT_THAT(&std::get<2>(tup), &can_be.d);
}

TEST(TupleExtensionTest, TupleTypes) {
  EXPECT_TRUE(
      (std::is_same_v<gtl::AsTupleType<CanBeTuple>,
                      std::tuple<const int&, const int&, const float&>>));
  EXPECT_TRUE((std::is_same_v<gtl::AsTupleOfRawTypes<CanBeTuple>,
                              std::tuple<int, int, float>>));
}

struct CanMutateTuple
    : public gtl::Extend<CanMutateTuple>::With<gtl::MutableTupleExtension> {
  int num2 = 0;
  std::vector<int> vals;
};

struct MutableTuple
    : public gtl::Extend<MutableTuple>::With<gtl::MutableTupleExtension> {
  int x = 0;
  int y = 0;
  float d = 0.0;
};

static_assert(std::is_same_v<gtl::AsTupleType<CanMutateTuple>,
                             std::tuple<const int&, const std::vector<int>&>>,
              "Incorrect tuple type deduced.");
static_assert(std::is_same_v<gtl::AsMutableTupleType<CanMutateTuple>,
                             std::tuple<int&, std::vector<int>&>>,
              "Incorrect mutable tuple type deduced.");

TEST(TupleExtensionTest, CanMutateTuple) {
  using testing::ElementsAre;
  using testing::Pair;

  CanMutateTuple can_mutate{};
  // Check the dependency is pulled in.
  static_assert(std::is_const_v<std::remove_reference_t<decltype(std::get<0>(
                    can_mutate.AsTuple()))>>);

  // Mutate the object via the tuple.
  std::tuple<int&, std::vector<int>&> tup = can_mutate.AsMutableTuple();
  std::get<0>(tup) += 1;
  std::get<1>(tup).push_back(2);
  EXPECT_THAT(can_mutate.num2, 1);
  EXPECT_THAT(can_mutate.vals, ElementsAre(2));
}

TEST(TupleExtensionTest, CanAssignFromTuple) {
  using testing::ElementsAre;
  using testing::Pair;

  CanMutateTuple can_mutate;
  can_mutate.AssignTupleToFields(std::make_tuple(1, std::vector<int>{2}));
  EXPECT_EQ(can_mutate.num2, 1);
  EXPECT_THAT(can_mutate.vals, ElementsAre(2));

  // Copy assign from an assigned-to temporary.
  can_mutate = CanMutateTuple{}.AssignTupleToFields(
      std::make_tuple(3, std::vector<int>{4}));
  EXPECT_EQ(can_mutate.num2, 3);
  EXPECT_THAT(can_mutate.vals, ElementsAre(4));
}

TEST(TupleExtension, CanTupleBeConstantEvaluated) {
  using absl::meta_internal::HasConstexprEvaluation;
  static constexpr CanBeTuple t = {{}, 2, 2, 10.0f / 6.0f};
  EXPECT_TRUE(HasConstexprEvaluation([] { return t.AsTuple(); }));
}

TEST(TupleExtension, CanMutableTupleBeConstantEvaluated) {
  using absl::meta_internal::HasConstexprEvaluation;
  EXPECT_TRUE(HasConstexprEvaluation([] {
    MutableTuple t = {{}, 2, 2, 10.0f / 6.0f};
    return t.AsMutableTuple();
  }));
}

struct NotTuple {};

struct NotExtendedCanBeTuple {
  std::tuple<const int&, const NotTuple&, const double&> AsTuple() const;
};

struct NotExtendedCanBeMutableTuple : NotExtendedCanBeTuple {
  std::tuple<int&, NotTuple&, const double&> AsMutableTuple();
};

TEST(TupleExtension, TypeTraits) {
  EXPECT_TRUE(gtl::HasAsTuple<CanBeTuple>);
  EXPECT_FALSE(gtl::HasAsMutableTuple<CanBeTuple>);
  EXPECT_TRUE(gtl::HasAsTuple<CanMutateTuple>);
  EXPECT_TRUE(gtl::HasAsMutableTuple<CanMutateTuple>);
  EXPECT_FALSE(gtl::HasAsTuple<NotTuple>);
  EXPECT_FALSE(gtl::HasAsMutableTuple<NotTuple>);
  EXPECT_TRUE(gtl::HasAsTuple<NotExtendedCanBeTuple>);
  EXPECT_FALSE(gtl::HasAsMutableTuple<NotExtendedCanBeTuple>);
  EXPECT_TRUE(gtl::HasAsTuple<NotExtendedCanBeMutableTuple>);
  EXPECT_TRUE(gtl::HasAsMutableTuple<NotExtendedCanBeMutableTuple>);
}

}  // namespace
