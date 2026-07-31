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

#include "gloop/util/tuple/string_hash_map.h"

#include <type_traits>

#include "absl/strings/string_view.h"
#include "gloop/util/gtl/string_hash_map.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

using N = typename ::gtl::string_hash_map<int>::value_type;

struct M : N {
  using N::N;
};

TEST(Node, Tag) {
  EXPECT_TRUE((::std::is_same<tag<N>::type, string_hash_map_node_tag>::value));
  EXPECT_TRUE((::std::is_same<tag<M>::type, string_hash_map_node_tag>::value));
}

TEST(Node, Assemble) {
  EXPECT_TRUE((::std::is_same<
               assemble<string_hash_map_node_tag, absl::string_view, int>::type,
               N>::value));
}

TEST(Node, Element) {
  EXPECT_TRUE((::std::is_same<element<0, N>::type, absl::string_view>::value));
  EXPECT_TRUE((::std::is_same<element<1, N>::type, int>::value));
  EXPECT_TRUE((::std::is_same<element<0, M>::type, absl::string_view>::value));
  EXPECT_TRUE((::std::is_same<element<1, M>::type, int>::value));
}

TEST(Node, Size) {
  EXPECT_EQ(2, size<N>::value);
  EXPECT_EQ(2, size<M>::value);
}

TEST(Node, GetImpl) {
  ::gtl::string_hash_map<int> m = {{"foo", 42}};
  auto& n = *m.find("foo");

  // Assignment to fields.
  // Assigning to a temporary string_view doesn't actually change the node.
  tuple::get<0>(n) = "bar";
  tuple::get<1>(n) = 43;

  // Non-const getter.
  EXPECT_EQ("foo", tuple::get<0>(n));
  EXPECT_EQ(43, tuple::get<1>(n));

  // Const getter.
  const auto& cn = n;
  EXPECT_EQ("foo", tuple::get<0>(cn));
  EXPECT_EQ(43, tuple::get<1>(cn));
}

TEST(Node, Name) {
  EXPECT_EQ(absl::string_view("key"), (name<0, N>()));
  EXPECT_EQ(absl::string_view("value"), (name<1, N>()));
  EXPECT_EQ(absl::string_view("key"), (name<0, M>()));
  EXPECT_EQ(absl::string_view("value"), (name<1, M>()));
}

TEST(Node, HasAllElements) {
  EXPECT_TRUE(has_all_elements<N>::value);
  EXPECT_TRUE(has_all_elements<M>::value);
}

}  // namespace
}  // namespace tuple
}  // namespace util
