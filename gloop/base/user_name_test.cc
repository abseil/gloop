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

#include "gloop/base/user_name.h"

#ifndef _WIN32
#include <unistd.h>
#endif
#include <string>

#include "gtest/gtest.h"

namespace {
TEST(UserNameTest, MyUserName) {
#ifdef _WIN32
  // UserName() is not supported on Windows. Let's just test we get a
  // non-empty string and get the same one on repeated calls.
  const std::string expected = MyUserName();
#else
  const std::string expected = UserName(geteuid());
#endif
  EXPECT_FALSE(expected.empty());
  EXPECT_EQ(MyUserName(), expected);
}

#ifndef _WIN32
TEST(UserNameTest, Root) { EXPECT_EQ(UserName(0), "root"); }
#endif

}  // namespace
