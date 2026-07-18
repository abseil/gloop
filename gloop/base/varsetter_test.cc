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

#include "gloop/base/varsetter.h"

#include <atomic>
#include <cstdint>
#include <string>

#include "absl/flags/flag.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

ABSL_FLAG(int32_t, new_int_flag, 10, "a new-style int flag");
ABSL_FLAG(std::string, new_string_flag, "default", "a new-style string flag");

namespace {
using testing::Eq;

TEST(VarSetterTest, IntObject) {
  int v = 100;
  {
    VarSetter setter(&v, 200);
    EXPECT_THAT(v, Eq(200));
    v = 300;
    EXPECT_THAT(v, Eq(300));
  }
  EXPECT_THAT(v, Eq(100));
}

TEST(VarSetterTest, StringObject) {
  std::string v = "one";
  {
    VarSetter setter(&v, "two");
    EXPECT_THAT(v, Eq("two"));
    v = "three";
    EXPECT_THAT(v, Eq("three"));
  }
  EXPECT_THAT(v, Eq("one"));
}

// We use a template method here to emphasize that the code for handling both
// old- and new-style flags is identical, so VarSetter is safe to use across
// changes from old- to new-style flags without modification. (As long as the
// callsites use CTAD to avoid specifying the template argument type.)
template <typename FlagType>
void TestIntFlag(FlagType& flag) {
  absl::SetFlag(&flag, 100);
  {
    VarSetter setter(&flag, 200);
    EXPECT_THAT(absl::GetFlag(flag), Eq(200));
    absl::SetFlag(&flag, 300);
    EXPECT_THAT(absl::GetFlag(flag), Eq(300));
  }
  EXPECT_THAT(absl::GetFlag(flag), Eq(100));
}

// Same logic for the template method here as above.
template <typename FlagType>
void TestStringFlag(FlagType& flag) {
  absl::SetFlag(&flag, "one");
  {
    VarSetter setter(&flag, "two");
    EXPECT_THAT(absl::GetFlag(flag), Eq("two"));
    absl::SetFlag(&flag, "three");
    EXPECT_THAT(absl::GetFlag(flag), Eq("three"));
  }
  EXPECT_THAT(absl::GetFlag(flag), Eq("one"));
}
TEST(VarSetterTest, NewIntFlag) { TestIntFlag(FLAGS_new_int_flag); }
TEST(VarSetterTest, NewStringFlag) { TestStringFlag(FLAGS_new_string_flag); }

TEST(VarSetterTest, MoveOnlyType) {
  class MoveOnly {
   public:
    explicit MoveOnly(int value) : value_(value) {}
    MoveOnly(MoveOnly&& other) = default;
    MoveOnly& operator=(MoveOnly&& other) = default;
    int value() const { return value_; }

   private:
    int value_;
  } v(100);
  {
    VarSetter setter(&v, MoveOnly(200));
    EXPECT_THAT(v.value(), Eq(200));
    v = MoveOnly(300);
    EXPECT_THAT(v.value(), Eq(300));
  }
  EXPECT_THAT(v.value(), Eq(100));
}

TEST(VarSetterTest, CTAD) {
  int v = 100;
  {
    VarSetter setter(&v, 200);
    EXPECT_EQ(v, 200);
  }
  EXPECT_EQ(v, 100);
}

TEST(VarSetterTest, Atomic) {
  std::atomic<int> megaton = 100;
  {
    VarSetter setter(&megaton, 200);
    EXPECT_EQ(megaton, 200);
    megaton.store(300);
    EXPECT_EQ(megaton, 300);
  }
  EXPECT_EQ(megaton, 100);
}

}  // namespace
