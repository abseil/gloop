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

#include "gloop/util/tuple/components/dump_vars.h"

#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/log/log_entry.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/util/tuple/components/streamable.h"
#include "gloop/util/tuple/components/streamable_test.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::StrEq;

namespace util {
namespace tuple {
namespace {

template <class T>
::std::string ToString(const T& t) {
  ::std::ostringstream strm;
  strm << t;
  return strm.str();
}

TEST(DumpVars, Empty) {
  EXPECT_EQ("", ToString(DUMP_VARS()));
  EXPECT_EQ("", DUMP_VARS().str());
}

TEST(DumpVars, Lvalue) {
  ::std::string foo = "hello";
  EXPECT_EQ(R"(foo = "hello")", ToString(DUMP_VARS(foo)));
  EXPECT_EQ(R"(foo = "hello")", DUMP_VARS(foo).str());
  EXPECT_EQ(R"(x = "hello")", ToString(DUMP_VARS(foo).as("x")));
}

TEST(DumpVars, Rvalue) {
  EXPECT_EQ("2 + 2 = 4", ToString(DUMP_VARS(2 + 2)));
  EXPECT_EQ("2 + 2 = 4", DUMP_VARS(2 + 2).str());
  EXPECT_EQ("x = 4", ToString(DUMP_VARS(2 + 2).as("x")));
}

#define FORTY_TWO 42
#define ONE_AND_TWO 1, 2

TEST(DumpVars, Macro) {
  // Macros get evaluated before they are stringized. It's not necessarily good,
  // but we'll have a test for it to serve as a documentation of facts.
  EXPECT_EQ("42 = 42", ToString(DUMP_VARS(FORTY_TWO)));
  EXPECT_EQ("42 = 42", DUMP_VARS(FORTY_TWO).str());

  EXPECT_EQ("1 = 1, 2 = 2", ToString(DUMP_VARS(ONE_AND_TWO)));
  EXPECT_EQ("1 = 1, 2 = 2", DUMP_VARS(ONE_AND_TWO).str());
  EXPECT_EQ("one = 1, two = 2",
            ToString(DUMP_VARS(ONE_AND_TWO).as("one", "two")));
}

template <int A, int B>
int Plus() {
  return A + B;
}

TEST(DumpVars, Parens) {
  EXPECT_EQ("x = 5", ToString(DUMP_VARS(Plus<2, 3>()).as("x")));
  EXPECT_EQ("(Plus<2, 3>()) = 5", ToString(DUMP_VARS((Plus<2, 3>()))));
  EXPECT_EQ("(Plus<2, 3>()) = 5", DUMP_VARS((Plus<2, 3>())).str());
  EXPECT_EQ("((Plus<2, 3>())) = 5", ToString(DUMP_VARS(((Plus<2, 3>())))));
  EXPECT_EQ("((Plus<2, 3>())) = 5", DUMP_VARS(((Plus<2, 3>()))).str());
  EXPECT_EQ("Parens = 5", DUMP_VARS(((Plus<2, 3>()))).as("Parens").str());
}

TEST(DumpVars, Bindings) {
  // Using a unique_ptr to ensure there is no copy.
  std::vector<std::pair<int, std::unique_ptr<std::string>>> v;
  v.push_back({3, std::make_unique<std::string>("hello")});
  const std::string foo = "bar";
  for (const auto& [i, s] : v) {
    EXPECT_EQ("i = 3, *s = \"hello\", foo = \"bar\"",
              ToString(DUMP_VARS(i, *s, foo)));
  }
}

TEST(DumpVars, NamesOverride) {
  EXPECT_EQ("z = 5", ToString(DUMP_VARS(5).as().as("x", "y").as("z")));
}

TEST(DumpVars, TwoValues) {
  int foo = 42;
  int bar = 24;
  EXPECT_EQ("foo = 42, bar = 24", ToString(DUMP_VARS(foo, bar)));
  EXPECT_EQ("foo = 42, bar = 24", DUMP_VARS(foo, bar).str());
  EXPECT_EQ("bar = 42, foo = 24", DUMP_VARS(foo, bar).as("bar", "foo").str());
}

TEST(DumpVars, WeirdCommas) {
  int foo = 1;
  int bar = 2;
  EXPECT_EQ("foo<bar = true, bar>(42) = false",
            ToString(DUMP_VARS(foo<bar, bar>(42))));
  EXPECT_EQ("foo<bar = true, bar>(42) = false",
            DUMP_VARS(foo<bar, bar>(42)).str());
  EXPECT_EQ("true = true, false = false",
            DUMP_VARS(foo<bar, bar>(42)).as("true", "false").str());
}

TEST(DumpVars, LazyEvaluation) {
  {
    int n = 0;
    auto F = [&]() { return ++n; };
    auto vars = DUMP_VARS(F());
    EXPECT_EQ(0, n);
    EXPECT_EQ("F() = 1", ToString(vars));
    EXPECT_EQ(1, n);
    EXPECT_EQ("F() = 2", ToString(vars));
    EXPECT_EQ(2, n);
    EXPECT_EQ("F() = 3", vars.str());
    EXPECT_EQ(3, n);
    EXPECT_EQ("F() = 4", vars.str());
    EXPECT_EQ(4, n);
    EXPECT_EQ("5 = 5", vars.as("5").str());
    EXPECT_EQ(5, n);
  }
  {
    int n = 0;
    auto F = [&]() { return ++n; };
    auto vars = DUMP_VARS(F()).as("x");
    EXPECT_EQ(0, n);
    EXPECT_EQ("x = 1", ToString(vars));
    EXPECT_EQ(1, n);
    EXPECT_EQ("x = 2", ToString(vars));
    EXPECT_EQ(2, n);
    EXPECT_EQ("x = 3", vars.str());
    EXPECT_EQ(3, n);
    EXPECT_EQ("x = 4", vars.str());
    EXPECT_EQ(4, n);
    EXPECT_EQ("y = 5", vars.as("y").str());
    EXPECT_EQ(5, n);
  }
}

TEST(DumpVars, TemporaryLifetime) {
  EXPECT_EQ(R"(absl::string_view(std::string("hello")) = "hello")",
            ToString(DUMP_VARS(absl::string_view(std::string("hello")))));
  auto v = DUMP_VARS(absl::string_view(std::string("hello")));
  EXPECT_EQ(R"(absl::string_view(std::string("hello")) = "hello")",
            ToString(v));
  EXPECT_EQ(R"(temp = "hello")", ToString(v.as("temp")));
}

TEST(DumpVars, IsDumpVars) {
  auto a = DUMP_VARS();
  auto b = DUMP_VARS(42);
  const auto& c = a;
  EXPECT_TRUE(is_dump_vars<decltype(a)>());
  EXPECT_TRUE(is_dump_vars<decltype(b)>());
  EXPECT_TRUE(is_dump_vars<decltype(c)>());
  EXPECT_FALSE(is_dump_vars<int>());
  EXPECT_FALSE(is_dump_vars<decltype(&a)>());
}

TEST(DumpVars, ManyArgs) {
  auto v =
      DUMP_VARS(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
                18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
                34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
                50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63);
  std::string expected;
  for (int i = 0; i != 64; ++i) {
    if (i) absl::StrAppend(&expected, ", ");
    absl::StrAppend(&expected, i, " = ", i);
  }
  EXPECT_EQ(expected, ToString(v));
}

TEST(DumpVars, Separator) {
  EXPECT_EQ("", ToString(DUMP_VARS().sep(".")));
  EXPECT_EQ("0 = 0", ToString(DUMP_VARS(0).sep(".")));
  EXPECT_EQ("0 = 0.1 = 1", ToString(DUMP_VARS(0, 1).sep(".")));
  EXPECT_EQ("0 = 0.1 = 1", ToString(DUMP_VARS(0, 1).sep(",").sep(".")));
  EXPECT_EQ("0 = 0.1 = 1.2 = 2", ToString(DUMP_VARS(0, 1, 2).sep(".")));

  EXPECT_EQ("0 = 01 = 1", ToString(DUMP_VARS(0, 1).sep("")));
  EXPECT_EQ("0 = 0\n1 = 1", ToString(DUMP_VARS(0, 1).sep("\n")));
  EXPECT_EQ("0 = 0abc1 = 1", ToString(DUMP_VARS(0, 1).sep("abc")));

  EXPECT_EQ("x = 0.y = 1", ToString(DUMP_VARS(0, 1).sep(".").as("x", "y")));
  EXPECT_EQ("x = 0.y = 1", ToString(DUMP_VARS(0, 1).as("x", "y").sep(".")));

  EXPECT_EQ("", ToString(DUMP_VARS().sep(".", "-")));
  EXPECT_EQ("0-0", ToString(DUMP_VARS(0).sep(".", "-")));
  EXPECT_EQ("0-0.1-1", ToString(DUMP_VARS(0, 1).sep(".", "-")));
  EXPECT_EQ("0-0.1-1.2-2", ToString(DUMP_VARS(0, 1, 2).sep(".", "-")));

  EXPECT_EQ("0-01-1", ToString(DUMP_VARS(0, 1).sep("", "-")));
  EXPECT_EQ("0-0\n1-1", ToString(DUMP_VARS(0, 1).sep("\n", "-")));
  EXPECT_EQ("0-0\n1-1", ToString(DUMP_VARS(0, 1).sep(",", "|").sep("\n", "-")));
  EXPECT_EQ("0-0abc1-1", ToString(DUMP_VARS(0, 1).sep("abc", "-")));

  EXPECT_EQ("x-0.y-1", ToString(DUMP_VARS(0, 1).sep(".", "-").as("x", "y")));
  EXPECT_EQ("x-0.y-1", ToString(DUMP_VARS(0, 1).as("x", "y").sep(".", "-")));
}

struct CurlyWriter : default_writer_t<CurlyWriter> {
  template <class T>
  void operator()(std::ostream& strm, const T& obj) const {
    strm << "{";
    default_writer_t<CurlyWriter>::operator()(strm, obj);
    strm << "}";
  }
};

TEST(DumpVars, Writer) {
  ::std::string foo = "hello";
  EXPECT_EQ(R"(foo = {"hello"})",
            ToString(DUMP_VARS(foo).set_writer(CurlyWriter())));
  EXPECT_EQ(R"(foo = {"hello"})",
            DUMP_VARS(foo).set_writer(CurlyWriter()).str());

  EXPECT_EQ(R"(x = {"hello"})",
            ToString(DUMP_VARS(foo).set_writer(CurlyWriter()).as("x")));

  EXPECT_EQ(R"(x = {"hello"})",
            ToString(DUMP_VARS(foo).as("x").set_writer(CurlyWriter())));

  std::vector<std::string> bar = {"hello", "world"};
  EXPECT_EQ(R"(bar = {[{"hello"}, {"world"}]})",
            ToString(DUMP_VARS(bar).set_writer(CurlyWriter())));
}

TEST(DumpVars, ProtoEmpty) {
  util::tuple::TestProto obj;
  EXPECT_THAT(ToString(DUMP_VARS(obj)), StrEq("obj = <>"));
}

TEST(DumpVars, RedactProtoShortFormat) {
  // The format of redacted fields is not specified. Here we test that the
  // string contains the relevant information but not the redacted information
  // without testing the format of the string.
  util::tuple::TestProto obj;
  obj.set_foo("hello");
  obj.set_bar(42);
  obj.set_redact("SECRET");
  std::string s = ToString(DUMP_VARS(obj));
  EXPECT_THAT(s, HasSubstr("foo: \"hello\""));
  EXPECT_THAT(s, HasSubstr("bar: 42"));
  EXPECT_THAT(s, Not(HasSubstr("SECRET")));
}

TEST(DumpVars, RedactProtoLongFormat) {
  // The format of redacted fields is not specified. Here we test that the
  // string contains the relevant information but not the redacted information
  // without testing the format of the string.
  util::tuple::TestProto obj;
  obj.set_foo(std::string(200, 'x'));
  obj.set_bar(42);
  obj.set_redact("SECRET");
  std::string s = ToString(DUMP_VARS(obj));
  EXPECT_THAT(s, HasSubstr(std::string(200, 'x')));
  EXPECT_THAT(s, HasSubstr("bar: 42"));
  EXPECT_THAT(s, Not(HasSubstr("SECRET")));
}

// A struct supporting AbslStringify, see https://abseil.io/tips/215.
struct Stringifiable {
  int x = 0;
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const Stringifiable& x) {
    absl::Format(&sink, "Stingified=%d", x.x);
  }
};

TEST(DumpVars, Stringify) {
  Stringifiable foo = {.x = 42};
  EXPECT_EQ(DUMP_VARS(foo).str(), "foo = Stingified=42");
}

}  // namespace
}  // namespace tuple
}  // namespace util
