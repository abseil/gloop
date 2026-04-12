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

//
// Tests for static map and static set.

#include "gloop/util/registration/static_map.h"

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "gloop/gloop_test.h"

using testing::ContainerEq;
using testing::ElementsAre;
using testing::Eq;
using testing::IsNull;
using testing::Pointee;
using testing::UnorderedElementsAre;

DEFINE_STATIC_MAP(IntToStringMap, int, std::string);
DEFINE_STATIC_MAP(IntToStringMap2, int, std::string);

SET_STATIC_MAP_VALUE(IntToStringMap, 10, "10");
SET_STATIC_MAP_VALUE(IntToStringMap, 20, "20");
SET_STATIC_MAP_VALUE(IntToStringMap, 30, "30");

SET_STATIC_MAP_VALUE(IntToStringMap2, 10, "100");
SET_STATIC_MAP_VALUE(IntToStringMap2, 20, "200");
SET_STATIC_MAP_VALUE(IntToStringMap2, 30, "300");
SET_STATIC_MAP_DEFAULT_VALUE(IntToStringMap2, "400");

DEFINE_STATIC_FLAT_HASH_MAP(IntToStringHashMap, int, std::string);
SET_STATIC_MAP_VALUE(IntToStringHashMap, 40, "40");

DEFINE_STATIC_MAP(StringToIntMap, std::string, int);
SET_STATIC_MAP_VALUE(StringToIntMap, "10", 10);

struct StringToIntHashMap
    : public util_registration::StaticFlatHashMap<StringToIntHashMap,
                                                  std::string, int> {};
const auto kInsertStringToIntHashMap80 =
    StringToIntHashMap::InsertValue("80", 80);

DEFINE_STATIC_SET(StringSet, std::string);
SET_STATIC_SET_KEY(StringSet, "foo");
SET_STATIC_SET_KEY(StringSet, "bar");

struct StringHashSet
    : public util_registration::StaticFlatHashSet<StringHashSet, std::string> {
};
const auto kInsertStringHashSetBaz = StringHashSet::InsertKey("baz");

TEST(MapSingleton, GetValue) {
  EXPECT_THAT(IntToStringMap::GetValue(25), IsNull());
  EXPECT_THAT(IntToStringMap::GetValue(10), Pointee(Eq("10")));
  EXPECT_THAT(IntToStringMap::GetValue(20), Pointee(Eq("20")));
  EXPECT_THAT(IntToStringMap::GetValue(30), Pointee(Eq("30")));
  // Test absence of default
  EXPECT_THAT(IntToStringMap::GetValue(555), IsNull());

  EXPECT_THAT(IntToStringMap2::GetValue(10), Pointee(Eq("100")));
  EXPECT_THAT(IntToStringMap2::GetValue(20), Pointee(Eq("200")));
  EXPECT_THAT(IntToStringMap2::GetValue(30), Pointee(Eq("300")));
  // Test the default
  EXPECT_THAT(IntToStringMap2::GetValue(555), Pointee(Eq("400")));

  EXPECT_THAT(IntToStringHashMap::GetValue(25), IsNull());
  EXPECT_THAT(IntToStringHashMap::GetValue(40), Pointee(Eq("40")));
}

TEST(MapSingleton, HeterogeneousGetValue) {
  EXPECT_THAT(StringToIntMap::GetValue(absl::string_view("25")), IsNull());
  EXPECT_THAT(StringToIntMap::GetValue(absl::string_view("10")),
              Pointee(Eq(10)));
  EXPECT_THAT(StringToIntHashMap::GetValue(absl::string_view("80")),
              Pointee(Eq(80)));
}

TEST(MapSingleton, GetKeys) {
  std::vector<int> keys;

  IntToStringMap::GetKeys(&keys);
  EXPECT_THAT(keys, ElementsAre(10, 20, 30));

  EXPECT_THAT(IntToStringMap::Keys(), ContainerEq(keys));

  EXPECT_THAT(IntToStringHashMap::Keys(), UnorderedElementsAre(40));
}

TEST(MapSingleton, MultipleDefinitionsDeath) {
  GLOOP_EXPECT_DEATH({ IntToStringMap::InsertValue(10, "10"); }, "");
  GLOOP_EXPECT_DEATH({ IntToStringMap2::SetDefaultValue("10"); }, "");
}

TEST(SetSingleton, ContainsKey) {
  EXPECT_TRUE(StringSet::ContainsKey("foo"));
  EXPECT_TRUE(StringSet::ContainsKey("bar"));
  EXPECT_FALSE(StringSet::ContainsKey("xyz"));

  EXPECT_TRUE(StringHashSet::ContainsKey("baz"));
  EXPECT_FALSE(StringHashSet::ContainsKey("xyz"));
}

TEST(SetSingleton, HeterogeneousContainsKey) {
  EXPECT_TRUE(StringSet::ContainsKey(absl::string_view("foo")));
  EXPECT_TRUE(StringHashSet::ContainsKey(absl::string_view("baz")));
}

TEST(SetSingleton, GetKeys) {
  std::vector<std::string> keys;

  StringSet::GetKeys(&keys);
  EXPECT_THAT(keys, ElementsAre("bar", "foo"));

  EXPECT_THAT(StringSet::Keys(), ContainerEq(keys));

  EXPECT_THAT(StringHashSet::Keys(), UnorderedElementsAre("baz"));
}

TEST(SetSingleton, MultipleDefinitionsDeath) {
  GLOOP_EXPECT_DEATH({ StringSet::InsertKey("foo"); }, "");
}
