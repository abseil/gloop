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

#include "gloop/util/refcount/reference_counted.h"

#include <string>

#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gloop/util/refcount/test_common.h"
#include "gtest/gtest.h"

static int constructed = 0;
static int destroyed = 0;

class MyRef : public util::ReferenceCounted {
 public:
  MyRef() { constructed++; }
  MyRef(util::ReferenceCountedType type, const void* owner)
      : util::ReferenceCounted(type, owner) {
    constructed++;
  }
  ~MyRef() override { destroyed++; }
};

class RefTest : public testing::Test {
 public:
  RefTest() {
    constructed = 0;
    destroyed = 0;
  }

  // Return "OK" if sub is found in str.  Else return str.
  std::string Contains(absl::string_view sub, absl::string_view str) {
    if (str.find(sub) != std::string::npos) {
      return "OK";
    } else {
      return std::string(str);
    }
  }
};

TEST_F(RefTest, New) {
  MyRef* ref = new MyRef;
  ASSERT_EQ(1, constructed);
  ASSERT_EQ(0, destroyed);
  ref->Unref();
  ASSERT_EQ(1, constructed);
  ASSERT_EQ(1, destroyed);
}

TEST_F(RefTest, RefUnref) {
  MyRef* ref = new MyRef;
  ASSERT_EQ(1, constructed);
  ASSERT_EQ(0, destroyed);
  ref->Ref();
  ASSERT_EQ(0, destroyed);
  ref->Unref();
  ASSERT_EQ(0, destroyed);
  ref->Unref();
  ASSERT_EQ(1, destroyed);
}

TEST_F(RefTest, ConstRefUnref) {
  const MyRef* cref = new MyRef;
  ASSERT_EQ(1, constructed);
  ASSERT_EQ(0, destroyed);
  cref->Ref();
  ASSERT_EQ(0, destroyed);
  cref->Unref();
  ASSERT_EQ(0, destroyed);
  cref->Unref();
  ASSERT_EQ(1, destroyed);
}

TEST_F(RefTest, Tracked) {
  char initial_owner;
  MyRef* ref = new MyRef(util::TRACKED, &initial_owner);
  ASSERT_EQ(1, constructed);
  ASSERT_EQ(0, destroyed);
  ref->RefFor(&ref);
  ASSERT_EQ(0, destroyed);
  ref->UnrefFor(&initial_owner);
  ASSERT_EQ(0, destroyed);
  ref->UnrefFor(&ref);
  ASSERT_EQ(1, destroyed);
}

TEST_F(RefTest, TrackedStrict) {
  char initial_owner;
  MyRef* ref = new MyRef(util::TRACKED_STRICT, &initial_owner);
  ASSERT_EQ(1, constructed);
  ASSERT_EQ(0, destroyed);
  ref->RefFor(this);
  ASSERT_EQ(0, destroyed);
  ref->UnrefFor(this);
  ASSERT_EQ(0, destroyed);
  ref->UnrefFor(&initial_owner);
  ASSERT_EQ(1, destroyed);
}

TEST_F(RefTest, MixedUse) {
  char initial_owner;
  MyRef* ref = new MyRef(util::TRACKED, &initial_owner);
  ref->RefFor(&ref);
  ref->Ref();
  ASSERT_EQ(0, destroyed);
  ref->UnrefFor(&initial_owner);
  ASSERT_EQ(0, destroyed);
  ref->UnrefFor(&ref);
  ASSERT_EQ(0, destroyed);
  ref->Unref();
  ASSERT_EQ(1, destroyed);
}

TEST_F(RefTest, ListOwnersNoTracking) {
  MyRef* ref = new MyRef;
  ASSERT_EQ("OK", Contains("held 1 times by untracked", ref->ListOwners()));
  ref->Ref();
  ASSERT_EQ("OK", Contains("held 2 times by untracked", ref->ListOwners()));
  ref->Unref();
  ASSERT_EQ("OK", Contains("held 1 times by untracked", ref->ListOwners()));
  ref->Unref();
  ASSERT_EQ(1, destroyed);
}

TEST_F(RefTest, ListOwnersWithTracking) {
  char initial_owner;
  MyRef* ref = new MyRef(util::TRACKED, &initial_owner);
  ASSERT_EQ("OK", Contains(absl::StrFormat("== owner %p", &initial_owner),
                           ref->ListOwners()));
  ASSERT_NE("OK", Contains("by untracked", ref->ListOwners()));

  // Add a tracked ref
  ref->RefFor(this);
  ASSERT_EQ("OK", Contains(absl::StrFormat("== owner %p", &initial_owner),
                           ref->ListOwners()));
  ASSERT_EQ("OK",
            Contains(absl::StrFormat("== owner %p", this), ref->ListOwners()));
  ASSERT_NE("OK", Contains("by untracked", ref->ListOwners()));

  // Add tracked ref with duplicate owner
  ref->RefFor(this);
  ASSERT_EQ("OK", Contains("ambiguous", ref->ListOwners()));
  ASSERT_EQ("OK", Contains("currently held 2 times", ref->ListOwners()));
  ref->UnrefFor(this);
  ASSERT_EQ("OK", Contains("ambiguous", ref->ListOwners()));
  ASSERT_NE("OK", Contains("currently held 2 times", ref->ListOwners()));

  // Add untracked ref
  ref->Ref();
  ASSERT_EQ("OK", Contains("held 1 times by untracked", ref->ListOwners()));
  ref->Unref();

  ref->UnrefFor(&initial_owner);
  ref->UnrefFor(this);
}

TEST_F(RefTest, RefIsUnique) {
  MyRef* ref = new MyRef;
  EXPECT_TRUE(ref->RefIsUnique());
  ref->Ref();
  EXPECT_FALSE(ref->RefIsUnique());
  ref->Unref();
  EXPECT_TRUE(ref->RefIsUnique());
  ref->Unref();
}

TEST_F(RefTest, GetReferenceCount) {
  MyRef* ref = new MyRef;
  EXPECT_EQ(1, ref->Test_GetReferenceCount());
  ref->Ref();
  EXPECT_EQ(2, ref->Test_GetReferenceCount());
  ref->Unref();
  ref->Unref();
}

TEST_F(RefTest, UnrefIfNonNull) {
  MyRef* ref = nullptr;
  RefIfNonNull(ref);
  UnrefIfNonNull(ref);
  ASSERT_EQ(0, constructed);
  ASSERT_EQ(0, destroyed);

  ref = new MyRef();
  ASSERT_EQ(1, constructed);
  ASSERT_EQ(0, destroyed);

  RefIfNonNull(ref);
  UnrefIfNonNull(ref);
  ASSERT_EQ(1, constructed);
  ASSERT_EQ(0, destroyed);

  UnrefIfNonNull(ref);
  ASSERT_EQ(1, constructed);
  ASSERT_EQ(1, destroyed);
}

TEST_F(RefTest, ScopedReference) {
  {
    MyRef* ref = new MyRef;
    util::ScopedReference scoped(ref);
    ref->Unref();  // ScopedReference should still hold a reference

    EXPECT_EQ(1, constructed);
    ASSERT_EQ(0, destroyed);
  }

  EXPECT_EQ(1, constructed);
  EXPECT_EQ(1, destroyed);
}

namespace refcount {

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(ReferenceCountedTest);

BENCHMARK(BenchmarkCopy<util::ReferenceCounted>);
BENCHMARK(BenchmarkCtorDtor<util::ReferenceCounted>);
}  // namespace refcount
