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

#include "gloop/util/tuple/relational.h"

#include <string>
#include <tuple>

#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

std::tuple<int, std::string> Small() { return std::make_tuple(0, "B"); }
std::tuple<int, std::string> Medium() { return std::make_tuple(0, "C"); }
std::tuple<int, std::string> Big() { return std::make_tuple(1, "A"); }

TEST(Relational, Compare) {
  EXPECT_EQ(0, compare(Small(), Small()));
  EXPECT_EQ(-1, compare(Small(), Medium()));
  EXPECT_EQ(1, compare(Medium(), Small()));
  EXPECT_EQ(-1, compare(Medium(), Big()));
  EXPECT_EQ(1, compare(Big(), Medium()));
}

TEST(Relational, Less) {
  EXPECT_FALSE(less(Small(), Small()));
  EXPECT_TRUE(less(Small(), Medium()));
  EXPECT_FALSE(less(Medium(), Small()));
  EXPECT_TRUE(less(Medium(), Big()));
  EXPECT_FALSE(less(Big(), Medium()));
}

TEST(Relational, Greater) {
  EXPECT_FALSE(greater(Small(), Small()));
  EXPECT_FALSE(greater(Small(), Medium()));
  EXPECT_TRUE(greater(Medium(), Small()));
  EXPECT_FALSE(greater(Medium(), Big()));
  EXPECT_TRUE(greater(Big(), Medium()));
}

TEST(Relational, Equivalent) {
  EXPECT_TRUE(equivalent(Small(), Small()));
  EXPECT_FALSE(equivalent(Small(), Medium()));
  EXPECT_FALSE(equivalent(Medium(), Small()));
  EXPECT_FALSE(equivalent(Medium(), Big()));
  EXPECT_FALSE(equivalent(Big(), Medium()));
}

TEST(Relational, NotEquivalent) {
  EXPECT_FALSE(not_equivalent(Small(), Small()));
  EXPECT_TRUE(not_equivalent(Small(), Medium()));
  EXPECT_TRUE(not_equivalent(Medium(), Small()));
  EXPECT_TRUE(not_equivalent(Medium(), Big()));
  EXPECT_TRUE(not_equivalent(Big(), Medium()));
}

TEST(Relational, LessEqual) {
  EXPECT_TRUE(less_equal(Small(), Small()));
  EXPECT_TRUE(less_equal(Small(), Medium()));
  EXPECT_FALSE(less_equal(Medium(), Small()));
  EXPECT_TRUE(less_equal(Medium(), Big()));
  EXPECT_FALSE(less_equal(Big(), Medium()));
}

TEST(Relational, GreaterEqual) {
  EXPECT_TRUE(greater_equal(Small(), Small()));
  EXPECT_FALSE(greater_equal(Small(), Medium()));
  EXPECT_TRUE(greater_equal(Medium(), Small()));
  EXPECT_FALSE(greater_equal(Medium(), Big()));
  EXPECT_TRUE(greater_equal(Big(), Medium()));
}

TEST(Relational, Equal) {
  EXPECT_TRUE(equal(Small(), Small()));
  EXPECT_FALSE(equal(Small(), Medium()));
  EXPECT_FALSE(equal(Medium(), Small()));
  EXPECT_FALSE(equal(Medium(), Big()));
  EXPECT_FALSE(equal(Big(), Medium()));
}

TEST(Relational, NotEqual) {
  EXPECT_FALSE(not_equal(Small(), Small()));
  EXPECT_TRUE(not_equal(Small(), Medium()));
  EXPECT_TRUE(not_equal(Medium(), Small()));
  EXPECT_TRUE(not_equal(Medium(), Big()));
  EXPECT_TRUE(not_equal(Big(), Medium()));
}

TEST(Relational, CompareCrossType) {
  std::tuple<int, std::string> a(0, "A");
  std::tuple<unsigned, const char*> b(0, "B");
  std::tuple<unsigned, const char*> c(0, "A");
  EXPECT_EQ(-1, compare(a, b));
  EXPECT_EQ(1, compare(b, a));
  EXPECT_EQ(0, compare(a, c));
  EXPECT_EQ(0, compare(c, a));
}

TEST(Relational, LessT) {
  EXPECT_FALSE(less_t()(Small(), Small()));
  EXPECT_TRUE(less_t()(Small(), Medium()));
  EXPECT_FALSE(less_t()(Medium(), Small()));
  EXPECT_TRUE(less_t()(Medium(), Big()));
  EXPECT_FALSE(less_t()(Big(), Medium()));
}

TEST(Relational, GreaterT) {
  EXPECT_FALSE(greater_t()(Small(), Small()));
  EXPECT_FALSE(greater_t()(Small(), Medium()));
  EXPECT_TRUE(greater_t()(Medium(), Small()));
  EXPECT_FALSE(greater_t()(Medium(), Big()));
  EXPECT_TRUE(greater_t()(Big(), Medium()));
}

TEST(Relational, EquivalentT) {
  EXPECT_TRUE(equivalent_t()(Small(), Small()));
  EXPECT_FALSE(equivalent_t()(Small(), Medium()));
  EXPECT_FALSE(equivalent_t()(Medium(), Small()));
  EXPECT_FALSE(equivalent_t()(Medium(), Big()));
  EXPECT_FALSE(equivalent_t()(Big(), Medium()));
}

TEST(Relational, NotEquivalentT) {
  EXPECT_FALSE(not_equivalent_t()(Small(), Small()));
  EXPECT_TRUE(not_equivalent_t()(Small(), Medium()));
  EXPECT_TRUE(not_equivalent_t()(Medium(), Small()));
  EXPECT_TRUE(not_equivalent_t()(Medium(), Big()));
  EXPECT_TRUE(not_equivalent_t()(Big(), Medium()));
}

TEST(Relational, LessEqualT) {
  EXPECT_TRUE(less_equal_t()(Small(), Small()));
  EXPECT_TRUE(less_equal_t()(Small(), Medium()));
  EXPECT_FALSE(less_equal_t()(Medium(), Small()));
  EXPECT_TRUE(less_equal_t()(Medium(), Big()));
  EXPECT_FALSE(less_equal_t()(Big(), Medium()));
}

TEST(Relational, GreaterEqualT) {
  EXPECT_TRUE(greater_equal_t()(Small(), Small()));
  EXPECT_FALSE(greater_equal_t()(Small(), Medium()));
  EXPECT_TRUE(greater_equal_t()(Medium(), Small()));
  EXPECT_FALSE(greater_equal_t()(Medium(), Big()));
  EXPECT_TRUE(greater_equal_t()(Big(), Medium()));
}

TEST(Relational, EqualT) {
  EXPECT_TRUE(equal_t()(Small(), Small()));
  EXPECT_FALSE(equal_t()(Small(), Medium()));
  EXPECT_FALSE(equal_t()(Medium(), Small()));
  EXPECT_FALSE(equal_t()(Medium(), Big()));
  EXPECT_FALSE(equal_t()(Big(), Medium()));
}

TEST(Relational, NotEqualT) {
  EXPECT_FALSE(not_equal_t()(Small(), Small()));
  EXPECT_TRUE(not_equal_t()(Small(), Medium()));
  EXPECT_TRUE(not_equal_t()(Medium(), Small()));
  EXPECT_TRUE(not_equal_t()(Medium(), Big()));
  EXPECT_TRUE(not_equal_t()(Big(), Medium()));
}

}  // namespace
}  // namespace tuple
}  // namespace util
