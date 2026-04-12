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

#include "gloop/util/gtl/associative_view_internal.h"

#include <map>
#include <set>
#include <string>

#include "gloop/gloop_test.h"
#include "gloop/util/gtl/set_view.h"

namespace gtl {
namespace internal_associative_view {
namespace {

TEST(AssociateViewTest, MapTypeHasUniqueKeys) {
  EXPECT_TRUE((MapTypeHasUniqueKeys<std::map<std::string, int>>()));
  EXPECT_FALSE((MapTypeHasUniqueKeys<std::multimap<std::string, int>>()));
  EXPECT_FALSE((MapTypeHasUniqueKeys<std::set<std::string>>()));
}

// An int wrapper that keeps count of created instances.
struct TrackedInt {
  explicit TrackedInt(int v) : value(v) { ++num_creations; }
  // Note: needs to be reset between tests to keep cases hermetic.
  inline static int num_creations = 0;
  int value;
};

struct NonTransparentCmp {
  bool operator()(const TrackedInt& x, const TrackedInt& y) const {
    return x.value < y.value;
  }
};

struct TransparentCmp {
  using is_transparent = void;

  bool operator()(const TrackedInt& x, const TrackedInt& y) const {
    return x.value < y.value;
  }
  bool operator()(int x, int y) const { return x < y; }
  bool operator()(const TrackedInt& x, int y) const { return x.value < y; }
  bool operator()(int x, const TrackedInt& y) const { return x < y.value; }
};

TEST(AssociateViewTest, HeterogeneousContainerTransparentLookup) {
  TrackedInt::num_creations = 0;
  std::set<TrackedInt, TransparentCmp> s = {TrackedInt(1), TrackedInt(2),
                                            TrackedInt(3)};
  SetView<TrackedInt, AlsoSupportsLookupWith<int>> v = s;

  {
    // Regular transparent lookup - no objects created in the process.
    EXPECT_EQ(TrackedInt::num_creations, 3);
    ASSERT_TRUE(v.find(123) == v.end());
    EXPECT_EQ(TrackedInt::num_creations, 3);
    ASSERT_FALSE(v.contains(123));
    EXPECT_EQ(TrackedInt::num_creations, 3);
  }
}

TEST(AssociateViewTest, HeterogeneousContainerConstructsKeys) {
  TrackedInt::num_creations = 0;
  std::set<TrackedInt, NonTransparentCmp> s = {TrackedInt(1), TrackedInt(2),
                                               TrackedInt(3)};
  SetView<TrackedInt, AlsoSupportsLookupWith<int>> v = s;

  {
    // To lookup "transparently" an `int`, we make a copy.
    EXPECT_EQ(TrackedInt::num_creations, 3);
    ASSERT_TRUE(v.find(123) == v.end());
    EXPECT_EQ(TrackedInt::num_creations, 4);
    ASSERT_FALSE(v.contains(123));
    EXPECT_EQ(TrackedInt::num_creations, 5);
  }

  {
    // To lookup a `key_type` instance, we don't make a copy.
    TrackedInt key(4);
    ASSERT_EQ(TrackedInt::num_creations, 6);
    ASSERT_TRUE(v.find(key) == v.end());
    ASSERT_EQ(TrackedInt::num_creations, 6);
  }
}

}  // namespace
}  // namespace internal_associative_view
}  // namespace gtl
