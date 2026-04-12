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

#include "gloop/util/tuple/swap.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include "gloop/gloop_test.h"

namespace util {
namespace tuple {
namespace {

struct Swappable {
  int value = 0;

  Swappable() {}
  Swappable(const Swappable&) = delete;
  Swappable& operator=(const Swappable&) = delete;

  friend void swap(Swappable& lhs, Swappable& rhs) {
    ::std::swap(lhs.value, rhs.value);
  }
};

TEST(Swap, BuiltIn) {
  auto t = ::std::make_tuple(42);
  auto q = ::std::make_tuple(24);
  tuple::swap(t, q);
  EXPECT_EQ(24, get<0>(t));
  EXPECT_EQ(42, get<0>(q));
}

TEST(Swap, NonCopyable) {
  ::std::tuple<Swappable> t;
  get<0>(t).value = 42;
  ::std::tuple<Swappable> q;
  get<0>(q).value = 24;
  tuple::swap(t, q);
  EXPECT_EQ(24, get<0>(t).value);
  EXPECT_EQ(42, get<0>(q).value);
}

TEST(Swap, TwoElements) {
  auto t = ::std::make_tuple(42, 0.5);
  auto q = ::std::make_tuple(24, 1.5);
  tuple::swap(t, q);
  EXPECT_EQ(::std::make_tuple(24, 1.5), t);
  EXPECT_EQ(::std::make_tuple(42, 0.5), q);
}

}  // namespace
}  // namespace tuple
}  // namespace util
