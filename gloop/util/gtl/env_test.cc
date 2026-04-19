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

#include "gloop/util/gtl/env.h"

#include <memory>
#include <optional>
#include <string>

#include "absl/strings/numbers.h"
#include "gloop/thread/fiber/bundle.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fiber.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace gtl {
namespace {

using ::testing::Contains;

TEST(EnvTest, SetEnv) {
  ASSERT_TRUE(SetEnv("ENV_NAME", "ENV_VALUE"));
  EXPECT_EQ(GetEnv("ENV_NAME"), "ENV_VALUE");
}

TEST(EnvTest, SetEnvIfUnset) {
  ASSERT_TRUE(SetEnvIfUnset("IF_UNSET", "ONE"));
  EXPECT_EQ(GetEnv("IF_UNSET"), "ONE");
  ASSERT_TRUE(SetEnvIfUnset("IF_UNSET", "TWO"));
  EXPECT_EQ(GetEnv("IF_UNSET"), "ONE");
}

TEST(EnvTest, UnsetEnv) {
  ASSERT_TRUE(SetEnv("ENV_NAME", "ENV_VALUE"));
  EXPECT_EQ(GetEnv("ENV_NAME"), "ENV_VALUE");
  ASSERT_TRUE(UnsetEnv("ENV_NAME"));
  EXPECT_TRUE(GetEnv("ENV_NAME") == std::nullopt);
}

TEST(EnvTest, InvalidName) {
  EXPECT_FALSE(SetEnv("INVALID=NAME", "value"));
  EXPECT_FALSE(SetEnvIfUnset("INVALID=NAME", "value"));
  EXPECT_EQ(GetEnv("INVALID=NAME"), std::nullopt);
  EXPECT_FALSE(UnsetEnv("INVALID=NAME"));
}

TEST(EnvTest, Environ) {
  ASSERT_TRUE(SetEnv("ENV_NAME", "SOME_VALUE"));
  EXPECT_GT(Environ().size(), 10);
  EXPECT_THAT(Environ(), Contains("ENV_NAME=SOME_VALUE"));
}

// Normal getenv that isn't thread-safe would fail this tsan test.
TEST(EnvTest, ThreadSafeSetEnvTsan) {
  SetEnv("COUNT", "0");

  thread::TreeOptions options;
  options.set_max_cpu_slots(10);
  std::unique_ptr<thread::Fiber> tree = thread::NewTree(options, [&] {
    thread::Bundle bundle;
    for (int i = 0; i != 1000; ++i) {
      bundle.Add([&] {
        int val;
        EXPECT_TRUE(absl::SimpleAtoi(GetEnv("COUNT").value_or(""), &val));
        SetEnv("COUNT", std::to_string(val + 1));
      });
    }
    bundle.JoinAll();
  });
  tree->Join();
}

}  // namespace
}  // namespace gtl
