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

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stddef.h>

#include <string>

#include "gloop/base/nsscache.h"
#include "gtest/gtest.h"

static int getgrnam_r_calls = 0;

// Override the real getgrnam_r with a version that always fails.
extern "C" int getgrnam_r(const char*, struct group*, char*, size_t,
                          struct group** result) {
  ++getgrnam_r_calls;
  *result = nullptr;
  return ERANGE;
}

TEST(Cache, OomOverflow) {
  getgrnam_r_calls = 0;
  gid_t gid = 0;
  EXPECT_FALSE(LookupGIDByGroupName("test-trigger-oom", &gid));
  EXPECT_GT(getgrnam_r_calls, 0);
}
