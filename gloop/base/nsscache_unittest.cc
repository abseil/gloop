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

#include "gloop/base/nsscache.h"

#include <grp.h>
#include <pwd.h>

#include <memory>
#include <string>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/gloop_test.h"

TEST(SimpleUser, Root) {
  uid_t uid;
  ASSERT_TRUE(LookupUIDByName("root", &uid));
  EXPECT_EQ(0, uid);

  std::string name;
  ASSERT_TRUE(LookupNameByUID(0, &name));
  EXPECT_EQ("root", name);

  std::shared_ptr<passwd> u = LookupUserByName("root");
  ASSERT_TRUE(u != nullptr);
  EXPECT_EQ(0, u->pw_uid);
  EXPECT_STREQ("root", u->pw_name);

  std::shared_ptr<passwd> ui = LookupUserByUID(0);
  ASSERT_TRUE(ui != nullptr);
  EXPECT_EQ(0, ui->pw_uid);
  EXPECT_STREQ("root", ui->pw_name);
}

TEST(SimpleUser, Nobody) {
  const uid_t kNobodyID = getpwnam("nobody")->pw_uid;
  std::shared_ptr<passwd> u = LookupUserByName("nobody");
  ASSERT_TRUE(u != nullptr);
  EXPECT_STREQ("nobody", u->pw_name);
  EXPECT_EQ(kNobodyID, u->pw_uid);

  std::shared_ptr<passwd> ui = LookupUserByUID(kNobodyID);
  ASSERT_TRUE(ui != nullptr);
  EXPECT_STREQ("nobody", ui->pw_name);
  EXPECT_EQ(kNobodyID, ui->pw_uid);

  uid_t uid;
  ASSERT_TRUE(LookupUIDByName("nobody", &uid));
  EXPECT_EQ(kNobodyID, uid);

  std::string name;
  ASSERT_TRUE(LookupNameByUID(kNobodyID, &name));
  EXPECT_EQ("nobody", name);
}

TEST(SimpleGroup, Root) {
#if defined(__APPLE__)
  constexpr char kExpectedRootGroup[] = "wheel";
#else
  constexpr char kExpectedRootGroup[] = "root";
#endif

  gid_t gid;
  ASSERT_TRUE(LookupGIDByGroupName(kExpectedRootGroup, &gid));
  EXPECT_EQ(0, gid);

  std::string name;
  ASSERT_TRUE(LookupGroupNameByGID(0, &name));
  EXPECT_EQ(kExpectedRootGroup, name);

  std::shared_ptr<group> g = LookupGroupByGroupName(kExpectedRootGroup);
  ASSERT_TRUE(g != nullptr);
  EXPECT_EQ(0, g->gr_gid);
  EXPECT_STREQ(kExpectedRootGroup, g->gr_name);

  std::shared_ptr<group> gi = LookupGroupByGID(0);
  ASSERT_TRUE(gi != nullptr);
  EXPECT_EQ(0, gi->gr_gid);
  EXPECT_STREQ(kExpectedRootGroup, gi->gr_name);
}

TEST(Lookup, Failure) {
  std::string kNonexistentUserName = "zZznonexistent-user";
  uid_t uid;
  ASSERT_FALSE(LookupUIDByName(kNonexistentUserName, &uid));

  std::shared_ptr<passwd> u = LookupUserByName(kNonexistentUserName);
  EXPECT_TRUE(u == nullptr);

#if !defined(__ANDROID__)  // Android returns u42949_a44951 for this???
  std::string name;
  const auto kNonexistentUserID = static_cast<uid_t>(-12345);
  ASSERT_FALSE(LookupNameByUID(kNonexistentUserID, &name)) << name;

  std::shared_ptr<passwd> ui = LookupUserByUID(kNonexistentUserID);
  EXPECT_TRUE(ui == nullptr);
#endif
}

TEST(Cache, Hit) {
  // Flush the cache.
  absl::SleepFor(absl::Seconds(2));
  std::shared_ptr<passwd> u1 = LookupUserByName("nobody");
  EXPECT_TRUE(u1 != nullptr);
  EXPECT_STREQ("nobody", u1->pw_name);
  // If we lookup again we should hit.
  std::shared_ptr<passwd> u2 = LookupUserByName("nobody");
  ASSERT_TRUE(u2 != nullptr);
  EXPECT_EQ(u1.get(), u2.get());
}

TEST(Cache, Stale) {
  // Flush the cache.
  absl::SleepFor(absl::Seconds(2));

  std::shared_ptr<passwd> u1 = LookupUserByName("nobody");
  EXPECT_TRUE(u1 != nullptr);
  EXPECT_STREQ("nobody", u1->pw_name);

  absl::SleepFor(absl::Seconds(2));
  // If we lookup again we should miss, since the entry will be stale.
  std::shared_ptr<passwd> u2 = LookupUserByName("nobody");
  ASSERT_TRUE(u2 != nullptr);
  EXPECT_NE(u1.get(), u2.get());
}

// Measure the in-cache lookup overhead
static void BM_Lookup(benchmark::State& state) {
  EXPECT_TRUE(LookupUserByName("root") != nullptr);
  for (auto _ : state) {
    EXPECT_TRUE(LookupUserByName("root") != nullptr);
  }
}
BENCHMARK(BM_Lookup);
