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

#include "gloop/util/gtl/internal/num_initializers.h"

#include <stdint.h>

#include <any>
#include <array>
#include <optional>

#include "gloop/util/intops/safe_int.h"
#include "gtest/gtest.h"

namespace gtl::internal_aggregate {
namespace {

TEST(NumInitializers, SimpleStructs) {
  struct A1 {
    int a;
  };
  EXPECT_EQ(NumInitializers<A1>(), 1);

  struct A2 {
    int a;
    int b;
  };
  EXPECT_EQ(NumInitializers<A2>(), 2);

  struct A3 {
    int a;
    const int& b;
    int&& c;
  };
  EXPECT_EQ(NumInitializers<A3>(), 3);
}

TEST(NumInitializers, SimpleStructsWithDefaults) {
  struct A1 {
    int a = 1;
  };
  EXPECT_EQ(NumInitializers<A1>(), 1);

  struct A2 {
    int a = 1;
    int b = 2;
  };
  EXPECT_EQ(NumInitializers<A2>(), 2);

  struct A3 {
    int a = 0;
    const int& b;
    int&& c;
  };
  EXPECT_EQ(NumInitializers<A3>(), 3);
}

TEST(NumInitializers, EmptyBase) {
  struct Base {};

  struct A1 : Base {
    int a;
  };
  EXPECT_EQ(NumInitializers<A1>(), 2);

  struct A2 : Base {
    int a;
    int b;
  };
  EXPECT_EQ(NumInitializers<A2>(), 3);

  struct A3 : Base {
    int a;
    const int& b;
    int&& c;
  };
  EXPECT_EQ(NumInitializers<A3>(), 4);
}

TEST(NumInitializers, Base) {
  struct Base {
    int x;
    int y;
  };

  struct A1 : Base {
    int a;
  };
  EXPECT_EQ(NumInitializers<A1>(), 2);

  struct A2 : Base {
    int a;
    int b;
  };
  EXPECT_EQ(NumInitializers<A2>(), 3);

  struct A3 : Base {
    int a;
    const int& b;
    int&& c;
  };
  EXPECT_EQ(NumInitializers<A3>(), 4);
}

TEST(NumInitializers, MultipleBases) {
  struct Base1 {
    int x1;
    int y1;
  };
  struct Base2 {
    int x2;
    int y2;
  };

  struct A1 : Base1, Base2 {
    int a;
  };
  EXPECT_EQ(NumInitializers<A1>(), 3);

  struct A2 : Base1, Base2 {
    int a;
    int b;
  };
  EXPECT_EQ(NumInitializers<A2>(), 4);

  struct A3 : Base1, Base2 {
    int a;
    const int& b;
    int&& c;
  };
  EXPECT_EQ(NumInitializers<A3>(), 5);
}

TEST(NumInitializers, StdArrayMembers) {
  struct A {
    std::array<int, 3> a;
    int b;
  };
  EXPECT_EQ(NumInitializers<A>(), 2);
}

TEST(NumInitializers, StructMembers) {
  struct A {
    struct Nested {
      int x;
      int y;
    } n;
    int b;
  };
  EXPECT_EQ(NumInitializers<A>(), 2);
}

TEST(NumInitializers, MoveOnlyMembers) {
  class MoveOnly {
   public:
    MoveOnly() = default;
    MoveOnly(MoveOnly&&) = default;
    MoveOnly& operator=(MoveOnly&&) = default;

    MoveOnly(MoveOnly const&) = delete;
    MoveOnly& operator=(MoveOnly const&) = delete;
  };

  struct A {
    MoveOnly a;
    int b;
    MoveOnly c;
    MoveOnly&& d;
    // MoveOnly& e;  // does not compile, until a brave soul implements it.
  };
  EXPECT_EQ(NumInitializers<A>(), 4);
}

// Not supported on older compilers due to mysterious compiler bug(s) that are
// somewhat difficult to track down. (See b/478243383.)
//
// TODO: Enable this unconditionally.
#if !defined(_MSC_VER) || _MSC_VER >= 1939
TEST(NumInitializers, MutableRefConstructibleMembers) {
  class MutableRefConstructible {
   public:
    MutableRefConstructible() = default;

    MutableRefConstructible(MutableRefConstructible&) {
      {
        // Dummy braces to avoid modernize-use-equals-default diagnostic.
        // We avoid `= default` due to compiler bug on _MSC_VER < 1940.
      }
    }

    MutableRefConstructible(const MutableRefConstructible&) = delete;
  };

  struct A {
    MutableRefConstructible a;
    int b;
    MutableRefConstructible c;
    MutableRefConstructible&& d;
  };
  EXPECT_EQ(NumInitializers<A>(), 4);
}

TEST(NumInitializers, ImmovableMembers) {
  struct Immovable {
    Immovable() = default;
    Immovable(Immovable&&) = delete;
  };

  struct A {
    const Immovable& a;
    Immovable&& b;
  };
  EXPECT_EQ(NumInitializers<A>(), 2);
}
#endif  // _MSC_VER >= 1939

TEST(NumInitializers, StdAnyMembers) {
  struct A1 {
    std::any a;  // cover `AnythingExcept`
    int b;
    std::any c;
  };
  EXPECT_EQ(NumInitializers<A1>(), 3);

  struct A2 {
    int a;
    std::any b;  // cover `Anything`
    const std::any& c;
    std::any&& d;
  };
  EXPECT_EQ(NumInitializers<A2>(), 4);
}

TEST(NumInitializers, OptionalStrongInt) {
  DEFINE_SAFE_INT_TYPE(SafeInt, int64_t, util_intops::LogFatalOnError);
  struct OptionalStrongInt {
    std::optional<SafeInt> num;
  };
  EXPECT_EQ(NumInitializers<OptionalStrongInt>(), 1);
}

}  // namespace
}  // namespace gtl::internal_aggregate
