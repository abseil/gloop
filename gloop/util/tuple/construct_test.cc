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

#include "gloop/util/tuple/construct.h"

#include <initializer_list>
#include <ostream>
#include <tuple>

#include "gloop/util/tuple/test_util.h"
#include "gtest/gtest.h"

namespace evil_user_ns {
template <class... Args>
void apply(Args...) = delete;
template <class... Args>
void do_apply(Args...) = delete;
template <class... Args>
void apply_impl(Args...) = delete;
template <class... Args>
void direct_initialize(Args...) = delete;
template <class... Args>
void brace_initialize(Args...) = delete;
struct UserType {};
}  // namespace evil_user_ns

namespace {

using ::std::tie;
using ::util::tuple::TestValues;

enum Initialization { kNormal, kInitList };

struct S {
  explicit S() : init(kNormal), num_args(0) {}
  explicit S(TestValues::A a) : init(kNormal), num_args(1) {}
  explicit S(TestValues::A a, TestValues::B b) : init(kNormal), num_args(2) {}
  explicit S(::std::initializer_list<TestValues::A> args)
      : init(kInitList), num_args(args.size()) {}

  Initialization init;
  int num_args;
};

bool operator==(const S& a, const S& b) {
  return a.init == b.init && a.num_args == b.num_args;
}

::std::ostream& operator<<(::std::ostream& strm, const S& s) {
  return strm << "{" << s.init << ", " << s.num_args << "}";
}

TEST_F(TestValues, DirectInitialize) {
  using ::util::tuple::direct_initialize;
  EXPECT_EQ(S(), direct_initialize<S>(tie()));
  EXPECT_EQ(S(a), direct_initialize<S>(tie(a)));
  EXPECT_EQ(S(a, b), direct_initialize<S>(tie(a, b)));
}

TEST_F(TestValues, BraceInitialize) {
  using ::util::tuple::brace_initialize;
  EXPECT_EQ(S{}, brace_initialize<S>(tie()));
  EXPECT_EQ(S{a}, brace_initialize<S>(tie(a)));
  EXPECT_EQ((S{a, b}), brace_initialize<S>(tie(a, b)));
}

TEST_F(TestValues, DirectInitializeT) {
  const ::util::tuple::direct_initialize_t<S> f = {};
  EXPECT_EQ(S(), f(tie()));
  EXPECT_EQ(S(a), f(tie(a)));
  EXPECT_EQ(S(a, b), f(tie(a, b)));
}

TEST_F(TestValues, BraceInitializeT) {
  const ::util::tuple::brace_initialize_t<S> f = {};
  EXPECT_EQ(S{}, f(tie()));
  EXPECT_EQ(S{a}, f(tie(a)));
  EXPECT_EQ((S{a, b}), f(tie(a, b)));
}

// Test that ADL calls are not performed
TEST(ConstructCall, NoADLCalls) {
  using T = ::evil_user_ns::UserType;
  ::util::tuple::brace_initialize<T>(tie());
  ::util::tuple::direct_initialize<T>(tie());
}

}  // namespace
