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

// Removing the following header is prohibited as it can introduce undefined behavior.
// clang-format off
#include "gloop/enforce_gloop_support.h"
// clang-format on

#include "gloop/util/tuple/components/internal_preprocessor.h"

#include "gloop/gloop_test.h"

namespace util {
namespace tuple {
namespace {

#define EMPTY(...)
#define AS_STRING(x) AS_STRING_INTERNAL(x)
#define AS_STRING_INTERNAL(x) #x

TEST(Preprocessor, IsEmpty) {
  EXPECT_EQ(1, TUPLE_INTERNAL_IS_EMPTY());
  EXPECT_EQ(0, TUPLE_INTERNAL_IS_EMPTY(()));
  EXPECT_EQ(0, TUPLE_INTERNAL_IS_EMPTY(a));
  EXPECT_EQ(0, TUPLE_INTERNAL_IS_EMPTY(a, b));
  EXPECT_EQ(0, TUPLE_INTERNAL_IS_EMPTY((,)));
  EXPECT_EQ(0, TUPLE_INTERNAL_IS_EMPTY(,));
  EXPECT_EQ(1, TUPLE_INTERNAL_IS_EMPTY(EMPTY()));
  EXPECT_EQ(1, TUPLE_INTERNAL_IS_EMPTY(EMPTY(, , , ,)));
}

TEST(Preprocessor, IsParenthesized) {
  EXPECT_EQ(0, TUPLE_INTERNAL_IS_PARENTHESIZED());
  EXPECT_EQ(0, TUPLE_INTERNAL_IS_PARENTHESIZED(a));
  EXPECT_EQ(0, TUPLE_INTERNAL_IS_PARENTHESIZED((a) b));
  EXPECT_EQ(0, TUPLE_INTERNAL_IS_PARENTHESIZED((a)(b)));
  EXPECT_EQ(0, TUPLE_INTERNAL_IS_PARENTHESIZED(a, b));
  EXPECT_EQ(1, TUPLE_INTERNAL_IS_PARENTHESIZED(()));
  EXPECT_EQ(1, TUPLE_INTERNAL_IS_PARENTHESIZED((a)));
  EXPECT_EQ(1, TUPLE_INTERNAL_IS_PARENTHESIZED(((a) b)));
  EXPECT_EQ(1, TUPLE_INTERNAL_IS_PARENTHESIZED(((a)(b))));
  EXPECT_EQ(1, TUPLE_INTERNAL_IS_PARENTHESIZED((a, b)));
}

TEST(Preprocessor, TupleUnparenthesize) {
  EXPECT_EQ("", AS_STRING(TUPLE_INTERNAL_UNPARENTHESIZE()));
  EXPECT_EQ("a", AS_STRING(TUPLE_INTERNAL_UNPARENTHESIZE(a)));
  EXPECT_EQ("(a) b", AS_STRING(TUPLE_INTERNAL_UNPARENTHESIZE((a) b)));
  EXPECT_EQ("(a)(b)", AS_STRING(TUPLE_INTERNAL_UNPARENTHESIZE((a)(b))));
  EXPECT_EQ("a b", AS_STRING(TUPLE_INTERNAL_UNPARENTHESIZE(a b)));
  EXPECT_EQ("", AS_STRING(TUPLE_INTERNAL_UNPARENTHESIZE(())));
  EXPECT_EQ("a", AS_STRING(TUPLE_INTERNAL_UNPARENTHESIZE((a))));
  EXPECT_EQ("(a) b", AS_STRING(TUPLE_INTERNAL_UNPARENTHESIZE(((a) b))));
  EXPECT_EQ("(a)(b)", AS_STRING(TUPLE_INTERNAL_UNPARENTHESIZE(((a)(b)))));
  EXPECT_EQ("a b", AS_STRING(TUPLE_INTERNAL_UNPARENTHESIZE((a b))));
}

TEST(Preprocessor, TupleParenthesize) {
  EXPECT_EQ("()", AS_STRING(TUPLE_INTERNAL_PARENTHESIZE()));
  EXPECT_EQ("(a)", AS_STRING(TUPLE_INTERNAL_PARENTHESIZE(a)));
  EXPECT_EQ("((a) b)", AS_STRING(TUPLE_INTERNAL_PARENTHESIZE((a) b)));
  EXPECT_EQ("((a)(b))", AS_STRING(TUPLE_INTERNAL_PARENTHESIZE((a)(b))));
  EXPECT_EQ("(a b)", AS_STRING(TUPLE_INTERNAL_PARENTHESIZE(a b)));
  EXPECT_EQ("()", AS_STRING(TUPLE_INTERNAL_PARENTHESIZE(())));
  EXPECT_EQ("(a)", AS_STRING(TUPLE_INTERNAL_PARENTHESIZE((a))));
  EXPECT_EQ("((a) b)", AS_STRING(TUPLE_INTERNAL_PARENTHESIZE(((a) b))));
  EXPECT_EQ("((a)(b))", AS_STRING(TUPLE_INTERNAL_PARENTHESIZE(((a)(b)))));
  EXPECT_EQ("(a b)", AS_STRING(TUPLE_INTERNAL_PARENTHESIZE((a b))));
}

#define IDENTITY(DATA, ELEM) (DATA, ELEM)

TEST(Preprocessor, TupleForEach) {
  EXPECT_EQ("(~, a) (~, b) (~, c)",
            AS_STRING(TUPLE_INTERNAL_FOR_EACH(IDENTITY, ~, (a, b, c))));
  EXPECT_EQ("", AS_STRING(TUPLE_INTERNAL_FOR_EACH(IDENTITY, ~, ())));
  EXPECT_EQ("(~, a), (~, b), (~, c)",
            TUPLE_INTERNAL_STRINGIZE(
                TUPLE_INTERNAL_LIST_FOR_EACH(IDENTITY, ~, (a, b, c))));
  EXPECT_EQ("", AS_STRING(TUPLE_INTERNAL_LIST_FOR_EACH(IDENTITY, ~, ())));
}

}  // namespace
}  // namespace tuple
}  // namespace util
