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

#include "gloop/util/gtl/extend/ordering.h"

#include <string>

#include "absl/meta/internal/constexpr_testing.h"
#include "gloop/gloop_test.h"
#include "gloop/util/gtl/extend/extend.h"

namespace {

struct OneField : gtl::Extend<OneField>::With<gtl::OrderingExtension> {
  int num;
};

struct ManyFields : gtl::Extend<ManyFields>::With<gtl::OrderingExtension> {
  int num = 3;
  bool b = true;
  std::string message = "hello";
};

struct Nested : gtl::Extend<Nested>::With<gtl::OrderingExtension> {
  int num = 3;
  ManyFields fields;
};

template <typename T>
struct Template
    : gtl::Extend<Template<T>>::template With<gtl::OrderingExtension> {
  T val;
};

TEST(Ordering, OneField) {
  OneField a{{}, 3};
  OneField b{{}, 4};

  EXPECT_LT(a, b);
  EXPECT_GT(b, a);
  EXPECT_EQ(a, a);
  EXPECT_GE(a, a);
  EXPECT_LE(a, a);
}

TEST(Ordering, ManyFields) {
  ManyFields a{{}, 3, true, "abc"};
  ManyFields b{{}, 3, true, "abd"};

  EXPECT_LT(a, b);
  EXPECT_GT(b, a);
}

TEST(Ordering, Nested) {
  Nested a{{}, 2, {{}, 3, true, "zebra"}};
  Nested b{{}, 3, {{}, 3, true, "hello"}};
  Nested c{{}, 3, {{}, 3, true, "zebra"}};
  EXPECT_LT(a, b);
  EXPECT_LT(b, c);
}

TEST(Ordering, Template) {
  Template<int> a_int{{}, 3};
  Template<int> b_int{{}, 4};
  EXPECT_EQ(a_int, a_int);
  EXPECT_LT(a_int, b_int);

  Template<double> a_double{{}, 3.1};
  Template<double> b_double{{}, 4.1};
  EXPECT_EQ(a_double, a_double);
  EXPECT_LT(a_double, b_double);
}

struct Point {
  int x;
  int y;
};
bool operator==(Point lhs, Point rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y;
}

// This is intentionally not constexpr to make sure that types still compile
// if there's a field with an `operator<` that is not constexpr.
bool operator<(Point lhs, Point rhs) {
  return !(lhs == rhs) && lhs.x <= rhs.x && lhs.y <= rhs.y;
}
struct WeightedPoint
    : gtl::Extend<WeightedPoint>::With<gtl::OrderingExtension> {
  Point p;
  double weight;
};

TEST(Ordering, PartialOrdering) {
  WeightedPoint w1{{}, {0, 1}, 1.0};
  WeightedPoint w2{{}, {1, 0}, 1.0};
  WeightedPoint w3{{}, {1, 1}, 1.0};
  EXPECT_FALSE(w1 < w2);
  EXPECT_FALSE(w2 < w1);
  EXPECT_LT(w1, w3);
  EXPECT_LT(w2, w3);
}

TEST(Ordering, CanBeConstantEvaluated) {
  using absl::meta_internal::HasConstexprEvaluation;
  static constexpr OneField a = {{}, 3}, b = {{}, 2};
  EXPECT_TRUE(HasConstexprEvaluation([] { return a < b; }));
  EXPECT_TRUE(HasConstexprEvaluation([] { return a > b; }));
  EXPECT_TRUE(HasConstexprEvaluation([] { return a <= b; }));
  EXPECT_TRUE(HasConstexprEvaluation([] { return a >= b; }));
  // This should fail to compile since `WeightedPoint` contains Point whose
  // `operator<` is not constexpr.
  static constexpr WeightedPoint p1{}, p2{};
  EXPECT_FALSE(HasConstexprEvaluation([] { return p1 < p2; }));
  EXPECT_FALSE(HasConstexprEvaluation([] { return p1 > p2; }));
  EXPECT_FALSE(HasConstexprEvaluation([] { return p1 <= p2; }));
  EXPECT_FALSE(HasConstexprEvaluation([] { return p1 >= p2; }));
}

}  // namespace
