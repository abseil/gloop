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

#include "gloop/util/gtl/string_hash_set.h"

#include <iostream>
#include <ostream>
#include <string>
#include <utility>

#include "absl/container/internal/hash_policy_testing.h"
#include "absl/container/internal/unordered_set_constructor_test.h"
#include "absl/container/internal/unordered_set_lookup_test.h"
#include "absl/container/internal/unordered_set_modifiers_test.h"
#include "absl/strings/string_view.h"
#include "gloop/gloop_test.h"

// Note that we are opening absl::container_internal namespace here
// as a workaround because INSTANTIATE_TYPED_TEST_CASE_P assumes local
// namespacing
namespace absl::container_internal {
namespace {

using SetTypes = ::testing::Types<::gtl::string_hash_set<
    StatefulTestingHash, StatefulTestingEqual, Alloc<absl::string_view>>>;

INSTANTIATE_TYPED_TEST_SUITE_P(StringHashSet, ConstructorTest, SetTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(StringHashSet, LookupTest, SetTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(StringHashSet, ModifiersTest, SetTypes);

}  // namespace
}  // namespace absl::container_internal

namespace gtl {
namespace {

using ::testing::Eq;
using ::testing::UnorderedElementsAre;

class WeirdString : public absl::string_view {
 public:
  explicit WeirdString(absl::string_view s) : s_(s) { Fix(); }
  WeirdString(const WeirdString& other) : s_(other.s_) { Fix(); }
  WeirdString& operator=(const WeirdString& other) {
    s_ = other.s_;
    Fix();
    return *this;
  }

 private:
  void Fix() { absl::string_view::operator=(s_); }

  std::string s_;
};

struct ConvertibleToWeirdString {
  operator WeirdString() const { return WeirdString(s); }
  std::string s;
};

TEST(ConvertibleToWeirdString, Insert) {
  string_hash_set<> m;
  ConvertibleToWeirdString s = {"0123456789012345"};
  m.insert(s);
  EXPECT_TRUE(m.count(s.s));
}

TEST(ConvertibleToWeirdString, Emplace) {
  string_hash_set<> m;
  ConvertibleToWeirdString s = {"0123456789012345"};
  m.emplace(s);
  EXPECT_TRUE(m.count(s.s));
}

TEST(NodeType, IsPrintable) {
  string_hash_set<> m;
  for (const auto& node : m) {
    std::cout << node << std::endl;
  }
}

TEST(ValueType, Compare) {
  string_hash_set<> m = {"abc"};
  auto it = m.begin();
  EXPECT_EQ(*it, "abc");
  EXPECT_NE(*it, "a");
  EXPECT_LT(*it, "ccc");
  EXPECT_GT(*it, "aaa");
  EXPECT_LE(*it, "ccc");
  EXPECT_LE(*it, "abc");
  EXPECT_GE(*it, "aaa");
  EXPECT_GE(*it, "abc");
}

TEST(StringHashSet, MergeExtractInsert) {
  gtl::string_hash_set<> set1 = {"A", "B"}, set2 = {"A", "C"};

  EXPECT_THAT(set1, UnorderedElementsAre(Eq("A"), Eq("B")));
  EXPECT_THAT(set2, UnorderedElementsAre(Eq("A"), Eq("C")));

  set1.merge(set2);

  EXPECT_THAT(set1, UnorderedElementsAre(Eq("A"), Eq("B"), Eq("C")));
  EXPECT_THAT(set2, UnorderedElementsAre(Eq("A")));

  auto node = set1.extract("A");
  EXPECT_TRUE(node);
  EXPECT_THAT(node.value(), Eq("A"));
  EXPECT_THAT(set1, UnorderedElementsAre(Eq("B"), Eq("C")));

  auto insert_result = set2.insert(std::move(node));
  EXPECT_FALSE(node);
  EXPECT_FALSE(insert_result.inserted);
  EXPECT_TRUE(insert_result.node);
  EXPECT_THAT(insert_result.node.value(), Eq("A"));
  EXPECT_EQ(*insert_result.position, "A");
  EXPECT_THAT(set2, UnorderedElementsAre(Eq("A")));

  node = set1.extract("B");
  EXPECT_TRUE(node);
  EXPECT_THAT(node.value(), Eq("B"));
  EXPECT_THAT(set1, UnorderedElementsAre(Eq("C")));

  // Note: We can't modify value() on a string_hash_set!

  insert_result = set2.insert(std::move(node));
  EXPECT_FALSE(node);
  EXPECT_TRUE(insert_result.inserted);
  EXPECT_FALSE(insert_result.node);
  EXPECT_EQ(*insert_result.position, "B");
  EXPECT_THAT(set2, UnorderedElementsAre(Eq("A"), Eq("B")));
}

}  // namespace
}  // namespace gtl
