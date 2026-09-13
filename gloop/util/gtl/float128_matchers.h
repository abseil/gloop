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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_FLOAT128_MATCHERS_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_FLOAT128_MATCHERS_H_

#include "absl/strings/str_cat.h"
#include "gloop/util/gtl/float128.h"
#include "gmock/gmock.h"

namespace gtl {

// Like testing::NanSensitiveDoubleEq, but for gtl::Float128.
MATCHER_P(NanSensitiveFloat128Eq, y,
          absl::StrCat(negation ? "is not " : "is ",
                       isnan(y) ? "nan"
                                : absl::StrCat("equal to ",
                                               testing::PrintToString(y)))) {
  return (isnan(arg) && isnan(y)) || arg == y;
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_FLOAT128_MATCHERS_H_
