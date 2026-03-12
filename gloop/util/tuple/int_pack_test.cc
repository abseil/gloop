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

// Copyright 2013 Google Inc. All Rights Reserved.

#include "gloop/util/tuple/int_pack.h"

#include <cstddef>
#include <tuple>
#include <type_traits>

#include "gtest/gtest.h"

namespace util {
namespace tuple {
namespace {

using ::std::is_convertible;
using ::std::is_same;

TEST(MakeIntPack, Metafunction) {
  EXPECT_TRUE((is_same<make_int_pack<0, 0>::type, int_pack<>>::value));
  EXPECT_TRUE((is_same<make_int_pack<0, 1>::type, int_pack<0>>::value));
  EXPECT_TRUE((is_same<make_int_pack<0, 2>::type, int_pack<0, 1>>::value));
  EXPECT_TRUE((is_same<make_int_pack<5, 5>::type, int_pack<>>::value));
  EXPECT_TRUE((is_same<make_int_pack<5, 6>::type, int_pack<5>>::value));
  EXPECT_TRUE((is_same<make_int_pack<5, 7>::type, int_pack<5, 6>>::value));
}

TEST(MakeIntPack, Inheritance) {
  EXPECT_TRUE((is_convertible<make_int_pack<0, 0>*, int_pack<>*>::value));
  EXPECT_TRUE((is_convertible<make_int_pack<0, 1>*, int_pack<0>*>::value));
  EXPECT_TRUE((is_convertible<make_int_pack<0, 2>*, int_pack<0, 1>*>::value));
  EXPECT_TRUE((is_convertible<make_int_pack<5, 5>*, int_pack<>*>::value));
  EXPECT_TRUE((is_convertible<make_int_pack<5, 6>*, int_pack<5>*>::value));
  EXPECT_TRUE((is_convertible<make_int_pack<5, 7>*, int_pack<5, 6>*>::value));
}

template <size_t... Is>
void CountAll(int_pack<Is...>) {
  using T [[maybe_unused]] = std::tuple<typename make_int_pack<0, Is>::type...>;
}

// This test verifies that MakeIndexSequence can handle large arguments without
// blowing up template instantiation stack, going OOM or taking forever to
// compile (there is hard 15 minutes limit imposed by forge). As of cl/117915234
// it takes ~15 seconds to compile.
TEST(MakeIntPack, Performance) {
  // O(log N) template instantiations.
  using P [[maybe_unused]] = make_int_pack<0, (1 << 20) - 1>::type;
  // O(N) template instantiations.
  CountAll(make_int_pack<0, (1 << 10) - 1>());
}

}  // namespace
}  // namespace tuple
}  // namespace util
