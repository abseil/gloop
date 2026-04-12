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

#include "gloop/util/tuple/back.h"

#include <tuple>

#include "gloop/gloop_test.h"
#include "gloop/util/tuple/test_util.h"

namespace util {
namespace tuple {
namespace {

class Back : public TestValues {};

TEST_F(Back, NonConst) {
  ::std::tuple<A, B> t(a, b);
  B* p = &back(t);
  EXPECT_EQ(&::std::get<1>(t), p);
}

TEST_F(Back, Const) {
  const ::std::tuple<A, B> t(a, b);
  const B* p = &back(t);
  EXPECT_EQ(&::std::get<1>(t), p);
}

}  // namespace
}  // namespace tuple
}  // namespace util
