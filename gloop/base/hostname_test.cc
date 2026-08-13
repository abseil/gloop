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

#include "gloop/base/hostname.h"

#include <string>
#include <vector>

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {
using ::testing::_;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::NotNull;

TEST(BaseHostname, ReturnsAHostname) {
  const absl::string_view hostname = base::Hostname();
  EXPECT_THAT(hostname, Not(IsEmpty()));
}

TEST(Hostname, ReturnsAHostname) {
  const char* const hostname_ptr = ::Hostname();
  ASSERT_THAT(hostname_ptr, NotNull());
  const absl::string_view hostname(hostname_ptr);
  EXPECT_THAT(hostname, Not(IsEmpty()));
}

}  // namespace
