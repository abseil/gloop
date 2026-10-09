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

#include "gloop/util/gtl/string_hash_map.h"

#include <any>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/container/internal/hash_policy_testing.h"
#include "absl/container/internal/unordered_map_constructor_test.h"
#include "absl/container/internal/unordered_map_lookup_test.h"
#include "absl/container/internal/unordered_map_modifiers_test.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

// Note that we are opening absl::container_internal namespace here
// as a workaround because INSTANTIATE_TYPED_TEST_CASE_P assumes local
// namespacing
namespace absl::container_internal {
namespace {

using MapTypes = ::testing::Types<
    ::gtl::string_hash_map<int, StatefulTestingHash, StatefulTestingEqual,
                           Alloc<std::pair<const absl::string_view, int>>>>;

INSTANTIATE_TYPED_TEST_SUITE_P(StringHashMap, ConstructorTest, MapTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(StringHashMap, LookupTest, MapTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(StringHashMap, ModifiersTest, MapTypes);

}  // namespace
}  // namespace absl::container_internal

namespace gtl {
namespace {

using ::testing::Contains;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::Pair;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

TEST(Insert, BraceInit) {
  string_hash_map<int> m;
  m.insert({"a", 1});
  EXPECT_THAT(m, UnorderedElementsAre(Pair("a", 1)));
}

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
  string_hash_map<int> m;
  ConvertibleToWeirdString s = {"0123456789012345"};
  m.insert(std::make_pair(s, 0));
  EXPECT_EQ(m.count(s.s), 1);
}

TEST(ConvertibleToWeirdString, Emplace) {
  string_hash_map<int> m;
  ConvertibleToWeirdString s = {"0123456789012345"};
  m.emplace(s, 0);
  EXPECT_EQ(m.count(s.s), 1);
}

TEST(ConvertibleToWeirdString, Piecewise) {
  string_hash_map<int> m;
  ConvertibleToWeirdString s = {"0123456789012345"};
  m.emplace(std::piecewise_construct, std::tie(s), std::tie());
  EXPECT_EQ(m.count(s.s), 1);
}

TEST(ExplicitConversion, Emplace) {
  string_hash_map<std::vector<std::string>> m;
  // Compiles, following the C++17 rules.
  m.emplace("abc", 42);
  EXPECT_THAT(m, Contains(Pair("abc", SizeIs(42))));
}

TEST(ValueType, InsertEmplace) {
  string_hash_map<int> m = {{"hello", 42}};
  EXPECT_FALSE(m.insert(*m.begin()).second);
  EXPECT_FALSE(m.insert(std::move(*m.begin())).second);
  EXPECT_FALSE(m.emplace(*m.begin()).second);
  EXPECT_FALSE(m.emplace(std::move(*m.begin())).second);
}

template <typename Expected, typename Actual>
::testing::AssertionResult IsSame(Expected&& expected, Actual&& actual) {
  static_assert(std::is_same_v<Expected, Actual>);
  if (&expected == &actual) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure()
         << "Address " << static_cast<const void*>(&actual) << " != expected "
         << static_cast<const void*>(&expected);
}

TEST(ValueType, Get) {
  using std::get;
  string_hash_map<int> m = {{"hello", 42}};
  auto& elem = *m.begin();
  const auto& const_elem = elem;
  int& value = elem.value();
  const int& const_value = value;

  EXPECT_TRUE(IsSame(value, elem.value()));
  EXPECT_TRUE(IsSame(const_value, const_elem.value()));
  EXPECT_TRUE(IsSame(std::move(value), std::move(elem).value()));
  EXPECT_TRUE(IsSame(std::move(const_value), std::move(const_elem).value()));

  EXPECT_TRUE(IsSame(value, get<1>(elem)));
  EXPECT_TRUE(IsSame(const_value, get<1>(const_elem)));
  EXPECT_TRUE(IsSame(std::move(value), get<1>(std::move(elem))));
  EXPECT_TRUE(IsSame(std::move(const_value), get<1>(std::move(const_elem))));

  EXPECT_EQ(get<0>(elem), "hello");
  EXPECT_EQ(get<1>(elem), 42);

  static_assert(std::tuple_size_v<string_hash_map<int>::value_type> == 2,
                "std::tuple_size is not specialized");
  static_assert(
      std::is_same_v<std::tuple_element_t<0, string_hash_map<int>::value_type>,
                     absl::string_view>,
      "std::tuple_element<0> is not specialized");
  static_assert(
      std::is_same_v<std::tuple_element_t<1, string_hash_map<int>::value_type>,
                     int>,
      "std::tuple_element<1> is not specialized");
  static_assert(
      std::is_same_v<
          std::tuple_element_t<1, string_hash_map<double>::value_type>, double>,
      "std::tuple_element<1> is not specialized");
}

TEST(ValueType, Compare) {
  string_hash_map<int> m = {{"abc", 42}};
  auto it = m.begin();
  EXPECT_EQ(*it, std::make_pair("abc", 42));
  EXPECT_NE(*it, std::make_pair("abc", 43));
  EXPECT_NE(*it, std::make_pair("aaa", 42));
  EXPECT_LT(*it, std::make_pair("abc", 43));
  EXPECT_LT(*it, std::make_pair("ccc", 41));
  EXPECT_GT(*it, std::make_pair("abc", 41));
  EXPECT_GT(*it, std::make_pair("aaa", 43));
  EXPECT_LE(*it, std::make_pair("ccc", 41));
  EXPECT_LE(*it, std::make_pair("abc", 42));
  EXPECT_GE(*it, std::make_pair("abc", 41));
  EXPECT_GE(*it, std::make_pair("abc", 42));
}

TEST(StringHashMap, OfAny) {
  string_hash_map<std::any> m;
  m["a"] = std::any();
  ASSERT_EQ(m.count("a"), 1);
  EXPECT_FALSE(m.at("a").has_value());
}

// TEST(StringHashMap, NotCompile) {
//   string_hash_map<std::vector<string>> m;
//   m.insert(make_pair("abc", 42));
// }

// TODO: Fix gMock to support C++11 features in MSVC and Android. Once
// that is done, we can enable this test back.
#if !defined(_MSC_VER) && !defined(__ANDROID__)

TEST(StringHashMap, Matchers) {
  string_hash_map<int> m = {{"abc", 42}, {"GOOOOGLE", 1998}};
  EXPECT_THAT(m, UnorderedElementsAre(Pair("abc", 42),
                                      Pair(HasSubstr("OOO"), Gt(1900))));
}

#endif  // !defined(_MSC_VER) && !defined(__ANDROID__)

TEST(StringHashMap, MergeExtractInsert) {
  string_hash_map<int> set1 = {{"A", 1}, {"B", 2}};
  string_hash_map<int> set2 = {{"A", -1}, {"C", -3}};

  EXPECT_THAT(set1, UnorderedElementsAre(Pair("A", 1), Pair("B", 2)));
  EXPECT_THAT(set2, UnorderedElementsAre(Pair("A", -1), Pair("C", -3)));

  set1.merge(set2);

  EXPECT_THAT(set1,
              UnorderedElementsAre(Pair("A", 1), Pair("B", 2), Pair("C", -3)));
  EXPECT_THAT(set2, UnorderedElementsAre(Pair("A", -1)));

  auto node = set1.extract("A");
  EXPECT_TRUE(node);
  EXPECT_EQ(node.key(), "A");
  EXPECT_EQ(node.mapped(), 1);
  EXPECT_THAT(set1, UnorderedElementsAre(Pair("B", 2), Pair("C", -3)));

  auto insert_result = set2.insert(std::move(node));
  EXPECT_FALSE(node);  // NOLINT(bugprone-use-after-move)
  EXPECT_FALSE(insert_result.inserted);
  EXPECT_TRUE(insert_result.node);
  EXPECT_EQ(insert_result.node.key(), "A");
  EXPECT_EQ(insert_result.node.mapped(), 1);
  EXPECT_THAT(*insert_result.position, Pair("A", -1));
  EXPECT_THAT(set2, UnorderedElementsAre(Pair("A", -1)));

  node = set1.extract("B");
  EXPECT_TRUE(node);
  EXPECT_EQ(node.key(), "B");
  EXPECT_EQ(node.mapped(), 2);
  EXPECT_THAT(set1, UnorderedElementsAre(Pair("C", -3)));

  node.mapped() = 17;

  insert_result = set2.insert(std::move(node));
  EXPECT_FALSE(node);  // NOLINT(bugprone-use-after-move)
  EXPECT_TRUE(insert_result.inserted);
  EXPECT_FALSE(insert_result.node);
  EXPECT_THAT(*insert_result.position, Pair("B", 17));
  EXPECT_THAT(set2, UnorderedElementsAre(Pair("A", -1), Pair("B", 17)));
}

}  // namespace
}  // namespace gtl
