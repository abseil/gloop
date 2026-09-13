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

#include "gloop/util/tuple/generate.h"

#include <stddef.h>

#include <array>
#include <cstdint>
#include <utility>

#include "gloop/util/tuple/array.h"
#include "gloop/util/tuple/pair.h"
#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

struct ByIndexGenerator {
  template <::size_t I>
  ::size_t operator()() const {
    return I;
  }
};

struct ByTypeGenerator {
  template <class T>
  ::size_t operator()() const {
    return sizeof(T);
  }
};

struct ByIndexAndTypeGenerator {
  template <::size_t I, class T>
  ::size_t operator()() const {
    return I;
  }
};

TEST(Generate, ByIndex) {
  typedef std::pair<::size_t, ::size_t> P;
  EXPECT_EQ(P(0, 1), generate_index<P>(ByIndexAndTypeGenerator()));
}

TEST(Generate, ByTagAndIndex) {
  typedef std::pair<::size_t, ::size_t> P;
  EXPECT_EQ(P(0, 1), (generate_index<pair_tag, 2>(ByIndexGenerator())));
}

TEST(Generate, ByType) {
  typedef std::pair<int32_t, int64_t> P;
  EXPECT_EQ(P(4, 8), generate<P>(ByTypeGenerator()));
}

TEST(Generate, ArrayByIndex) {
  typedef std::array<::size_t, 2> A;
  EXPECT_EQ((A{{0, 1}}), generate_index<A>(ByIndexAndTypeGenerator()));
}

TEST(Generate, ArrayByTagAndIndex) {
  typedef std::array<::size_t, 2> A;
  EXPECT_EQ((A{{0, 1}}), (generate_index<array_tag, 2>(ByIndexGenerator())));
}

TEST(Generate, ArrayByType) {
  typedef std::array<::size_t, 2> A;
  EXPECT_EQ((A{{8, 8}}), generate<A>(ByTypeGenerator()));
}

}  // namespace
}  // namespace tuple
}  // namespace util
