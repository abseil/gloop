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

// Copyright 2013 Google Inc. All Rights Reserved.

#include "gloop/util/tuple/apply.h"

#include <memory>
#include <string>
#include <tuple>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace evil_user_ns {
template <class... Args>
void apply(Args...) = delete;
template <class... Args>
void do_apply(Args...) = delete;
template <class... Args>
void apply_impl(Args...) = delete;

struct UserType {};
}  // namespace evil_user_ns

namespace {

using ::testing::Eq;
using ::testing::Pointee;

int Minus(int a, int b) { return a - b; }

void IncrementBy(int& a, int b) { a += b; }

struct MutableMinus {
  int operator()(int a, int b) { return a - b; }
};

struct Sink {
  template <class... Args>
  bool operator()(Args...) const {
    return true;
  }
};

TEST(Apply, NoArgs) {
  util::tuple::apply(::std::make_tuple([] {}));
  util::tuple::apply([] {}, ::std::make_tuple());
}

TEST(Apply, ConstArgs) {
  EXPECT_EQ(1, util::tuple::apply(::std::make_tuple(Minus, 3, 2)));
  EXPECT_EQ(1, util::tuple::apply(Minus, ::std::make_tuple(3, 2)));
  EXPECT_EQ(1, util::tuple::apply(Minus, 3, ::std::make_tuple(2)));
  EXPECT_EQ(1, util::tuple::apply(Minus, 3, 2, ::std::make_tuple()));
}

TEST(Apply, NonConstArgs) {
  int n = 2;
  int m = 3;
  util::tuple::apply(::std::tie(IncrementBy, n, m));
  EXPECT_EQ(5, n);
  util::tuple::apply(IncrementBy, ::std::tie(n, m));
  EXPECT_EQ(8, n);
  util::tuple::apply(IncrementBy, n, ::std::tie(m));
  EXPECT_EQ(11, n);
  util::tuple::apply(IncrementBy, n, m, ::std::tie());
  EXPECT_EQ(14, n);
}

TEST(Apply, ImplicitConversion) {
  EXPECT_EQ(1, util::tuple::apply(Minus, ::std::make_tuple(3.5, 2)));
}

TEST(Apply, MutableFunctor) {
  MutableMinus f;
  EXPECT_EQ(1, util::tuple::apply(::std::make_tuple(f, 3, 2)));
  EXPECT_EQ(1, util::tuple::apply(f, ::std::make_tuple(3, 2)));
}

TEST(Apply, MemberFunction) {
  struct F {
    explicit F(int a) : a(a) {}

    // This type is neither copyable nor movable.
    F(const F&) = delete;
    F& operator=(const F&) = delete;
    int Plus(int x) const { return x + a; }
    int a;
  };
  F f{2};
  EXPECT_EQ(5, util::tuple::apply(::std::forward_as_tuple(&F::Plus, f, 3)));
  EXPECT_EQ(5, util::tuple::apply(&F::Plus, ::std::forward_as_tuple(f, 3)));
  EXPECT_EQ(5, util::tuple::apply(&F::Plus, f, ::std::forward_as_tuple(3)));
  EXPECT_EQ(5, util::tuple::apply(&F::Plus, f, 3, ::std::forward_as_tuple()));

  EXPECT_EQ(5, util::tuple::apply(::std::forward_as_tuple(&F::Plus, &f, 3)));
  EXPECT_EQ(5, util::tuple::apply(&F::Plus, ::std::forward_as_tuple(&f, 3)));
  EXPECT_EQ(5, util::tuple::apply(&F::Plus, &f, ::std::forward_as_tuple(3)));
  EXPECT_EQ(5, util::tuple::apply(&F::Plus, &f, 3, ::std::forward_as_tuple()));

  ::std::unique_ptr<F> p(new F{2});
  EXPECT_EQ(5, util::tuple::apply(::std::forward_as_tuple(&F::Plus, p, 3)));
  EXPECT_EQ(5, util::tuple::apply(&F::Plus, ::std::forward_as_tuple(p, 3)));
  EXPECT_EQ(5, util::tuple::apply(&F::Plus, p, ::std::forward_as_tuple(3)));
  EXPECT_EQ(5, util::tuple::apply(&F::Plus, p, 3, ::std::forward_as_tuple()));
}

TEST(Apply, RValue) {
  struct F {
    ::std::unique_ptr<int> operator()(::std::unique_ptr<int> p) && { return p; }
  };
  auto make_rvalue = []() { return std::make_unique<int>(123); };
  EXPECT_THAT(util::tuple::apply(::std::forward_as_tuple(F(), make_rvalue())),
              Pointee(Eq(123)));
  EXPECT_THAT(util::tuple::apply(F(), ::std::forward_as_tuple(make_rvalue())),
              Pointee(Eq(123)));
  EXPECT_THAT(util::tuple::apply(F(), make_rvalue(), ::std::forward_as_tuple()),
              Pointee(Eq(123)));
}

TEST(Apply, ADLGuards) {
  using T = ::evil_user_ns::UserType;
  ::util::tuple::apply(Sink{}, ::std::forward_as_tuple(T{}));
}

TEST(IsApplicable, _) {
  using Functor = decltype(Minus);
  {
    const bool r = util::tuple::is_applicable<Functor, std::tuple<>>();
    EXPECT_FALSE(r);
  }
  {
    const bool r = util::tuple::is_applicable<Functor, std::tuple<int>>();
    EXPECT_FALSE(r);
  }
  {
    const bool r = util::tuple::is_applicable<Functor, std::tuple<int, int>>();
    EXPECT_TRUE(r);
  }
  {
    const bool r =
        util::tuple::is_applicable<Functor, std::tuple<int, int, int>>();
    EXPECT_FALSE(r);
  }
  {
    const bool r =
        util::tuple::is_applicable<Functor, std::tuple<float, float>>();
    EXPECT_TRUE(r);
  }
  {
    const bool r =
        util::tuple::is_applicable<Functor,
                                   std::tuple<std::string, std::string>>();
    EXPECT_FALSE(r);
  }
}

}  // namespace
