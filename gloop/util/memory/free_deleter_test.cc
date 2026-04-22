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

#include "gloop/util/memory/free_deleter.h"

#include <cstdlib>
#include <memory>
#include <utility>

#include "gtest/gtest.h"

namespace {

TEST(FreeDeleterTest, FreeDeleterCompiles) {
  std::unique_ptr<char, util::memory::FreeDeleter> ptr1;
  EXPECT_EQ(ptr1, nullptr);
  char* chars1 = reinterpret_cast<char*>(malloc(10));
  ptr1.reset(chars1);

  EXPECT_NE(ptr1, nullptr);
  EXPECT_EQ(ptr1.get(), chars1);
  char* chars2 = reinterpret_cast<char*>(malloc(20));
  std::unique_ptr<char, util::memory::FreeDeleter> ptr2;
  ptr2.reset(chars2);
  using std::swap;
  swap(ptr1, ptr2);

  EXPECT_EQ(ptr2.get(), chars1);
  EXPECT_EQ(ptr1.get(), chars2);
  EXPECT_EQ(ptr2.get(), chars1);
  EXPECT_EQ(chars2, ptr1.get());
  EXPECT_NE(ptr1.get(), chars1);
  EXPECT_NE(chars2, ptr2.get());
}

}  // namespace
