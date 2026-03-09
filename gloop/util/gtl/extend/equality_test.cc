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

#include "gloop/util/gtl/extend/equality.h"

#include <limits>
#include <string>
#include <type_traits>

#include "absl/hash/hash.h"
#include "absl/hash/hash_testing.h"
#include "absl/meta/internal/constexpr_testing.h"  // NOLINT(build/include)
#include "gloop/util/gtl/extend/extend.h"
#include "gtest/gtest.h"

namespace {

class UnhashableType {
 public:
  explicit UnhashableType(int v = 0) : v_(v) {}

  // This is intentionally not constexpr to make sure that types still compile
  // if there's a field with an `operator<` that is not constexpr.
  bool operator==(const UnhashableType& other) const { return v_ == other.v_; }

 private:
  int v_;
};

static_assert(!std::is_constructible_v<absl::Hash<UnhashableType>>);

struct OneField : gtl::Extend<OneField>::With<gtl::EqualityExtension> {
  int num;
};

struct ManyFields : gtl::Extend<ManyFields>::With<gtl::EqualityExtension> {
  int num = 3;
  bool b = true;
  std::string message = "hello";
};

struct ManyFieldsOneUnhashable
    : gtl::Extend<ManyFieldsOneUnhashable>::With<gtl::EqualityExtension> {
  UnhashableType unhash{3};
  bool b = true;
  std::string message = "hello";
};

struct Nested : gtl::Extend<Nested>::With<gtl::EqualityExtension> {
  int num = 3;
  ManyFields fields;
};

template <typename T>
struct Template
    : gtl::Extend<Template<T>>::template With<gtl::EqualityExtension> {
  T val;
};

TEST(Equality, OneField) {
  OneField a{{}, 3};
  OneField b{{}, 4};
  OneField c{{}, 3};

  EXPECT_EQ(a, c);
  EXPECT_NE(a, b);

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({
      OneField{{}, std::numeric_limits<int>::min()},
      OneField{{}, -1},
      OneField{{}, 0},
      OneField{{}, 1},
      OneField{{}, std::numeric_limits<int>::max()},
  }));
}

TEST(Equality, ManyFields) {
  ManyFields a;
  ManyFields b{{}, 3, true, "some other message"};
  ManyFields c{{}, 3, true, "hello"};

  EXPECT_EQ(a, c);
  EXPECT_NE(a, b);

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({
      ManyFields{{}, 3, true, "abc"},
      ManyFields{{}, 3, true, "abd"},
  }));
}

TEST(Equality, ManyFieldsOneUnhashable) {
  ManyFieldsOneUnhashable a;
  ManyFieldsOneUnhashable b{{}, UnhashableType{3}, true, "some other message"};
  ManyFieldsOneUnhashable c{{}, UnhashableType{3}, true, "hello"};

  EXPECT_EQ(a, c);
  EXPECT_NE(a, b);

  EXPECT_FALSE(std::is_constructible_v<absl::Hash<ManyFieldsOneUnhashable>>);
}

TEST(Equality, Nested) {
  Nested a{{}, 3, {}};
  Nested b{{}, 3, {{}, 3, true}};
  Nested c{{}, 3, {{}, 3, false, "hello"}};
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({
      Nested{{}, 2, {{}, 3, true, "zebra"}},
      Nested{{}, 3, {{}, 3, true, "hello"}},
      Nested{{}, 3, {{}, 3, true, "zebra"}},
  }));
}

TEST(Equality, Template) {
  Template<int> a_int{{}, 3};
  Template<int> b_int{{}, 4};
  EXPECT_EQ(a_int, a_int);
  EXPECT_NE(a_int, b_int);

  Template<double> a_double{{}, 3.1};
  Template<double> b_double{{}, 4.1};
  EXPECT_EQ(a_double, a_double);
  EXPECT_NE(a_double, b_double);

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({
      Template<int>{{}, 3},
      Template<int>{{}, 4},
  }));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({
      Template<double>{{}, 3.1},
      Template<double>{{}, 4.1},
  }));
}

struct HasNonConstexprOps {
  int i;
  bool operator==(const HasNonConstexprOps& other) const {
    return i == other.i;
  }
};

struct HasNonConstexprFields
    : gtl::Extend<HasNonConstexprFields>::With<gtl::EqualityExtension> {
  HasNonConstexprOps ops = {0};
};

TEST(Equality, CanBeConstantEvaluated) {
  using absl::meta_internal::HasConstexprEvaluation;
  static constexpr OneField a = {{}, 1}, b = {{}, 2};
  EXPECT_TRUE(HasConstexprEvaluation([] { return a == b; }));
  EXPECT_TRUE(HasConstexprEvaluation([] { return a != b; }));
  static constexpr HasNonConstexprFields x = {}, y = {};
  EXPECT_FALSE(HasConstexprEvaluation([] { return x == y; }));
  EXPECT_FALSE(HasConstexprEvaluation([] { return x != y; }));
}

}  // namespace
