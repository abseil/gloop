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

//
// Unit test for Demangling functions.

#include "gloop/util/symbolize/demangle.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/strings/str_split.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/threadpool.h"
#include "gloop/util/symbolize/demangled_type_name.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "re2/re2.h"

// Define a simple struct which has only two possible demangled representations:
// with absolute namespace (leading "::") or without.
namespace util_testing {
struct DemangleTestStruct {};
}  // namespace util_testing

namespace {

TEST(Demangle, DemangleTypeName) {
  EXPECT_THAT(util::DemangledTypeName<util_testing::DemangleTestStruct>(),
              testing::AnyOf("util_testing::DemangleTestStruct",
                             "::util_testing::DemangleTestStruct"));
}

}  // namespace
