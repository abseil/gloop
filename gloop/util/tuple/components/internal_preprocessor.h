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

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_INTERNAL_PREPROCESSOR_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_INTERNAL_PREPROCESSOR_H_

// Returns empty token. Given a variadic number of arguments.
#define TUPLE_INTERNAL_EMPTY(...)

// Returns a comma. Given a variadic number of arguments.
#define TUPLE_INTERNAL_COMMA(...) ,

// Returns the arguments.
#define TUPLE_INTERNAL_IDENTITY(...) __VA_ARGS__

// Evaluates and creates a string with the arguments.
#define TUPLE_INTERNAL_STRINGIZE(...) TUPLE_INTERNAL_STRINGIZE_I(__VA_ARGS__)

// Evaluates both arguments and concatenates the results.
#define TUPLE_INTERNAL_CAT(A, ...) TUPLE_INTERNAL_CAT_I(A, __VA_ARGS__)

// Returns the first argument. Requires at least one argument.
#define TUPLE_INTERNAL_1ST(x, ...) x

// Returns the second argument. Requires at least two arguments.
#define TUPLE_INTERNAL_2ND(_1, x, ...) x

// Returns the number of passed arguments. Requires the less than 64 arguments
// after expansion.
#define TUPLE_INTERNAL_NARG(...)                                               \
  TUPLE_INTERNAL_65TH((__VA_ARGS__, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55,    \
                       54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, \
                       40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, \
                       26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, \
                       12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1))

// Returns 1 if the expansion of arguments has an unprotected comma. Otherwise
// returns 0. Requires that the number of arguments after expansion is less than
// 64.
#define TUPLE_INTERNAL_HAS_COMMA(...)                                         \
  TUPLE_INTERNAL_65TH((__VA_ARGS__, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, \
                       1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  \
                       1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  \
                       1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0))

// If the arguments after expansion have no tokens, evaluates to `1`. Otherwise
// evaluates to `0`.
//
// Requires: * the number of arguments after expansion is at most 64.
//           * If the argument is a macro, it must be callable with one
//             argument.
//
// Implementation details:
//
// There is one case when it generates a compile error: if the argument is macro
// that cannot be called with one argument.
//
//   #define M(a, b)  // it doesn't matter what it expands to
//
//   // Expected: expands to `0`.
//   // Actual: compile error.
//   TUPLE_INTERNAL_IS_EMPTY(M)
//
// There are 4 cases tested:
//
// * __VA_ARGS__ possible expansion has no unparen'd commas. Expected 0.
// * __VA_ARGS__ possible expansion is not enclosed in parenthesis. Expected 0.
// * __VA_ARGS__ possible expansion is not a macro that ()-evaluates to a comma.
//   Expected 0
// * __VA_ARGS__ is empty, or has unparen'd commas, or is enclosed in
//   parenthesis, or is a macro that ()-evaluates to comma. Expected 1.
//
// We trigger detection on '0001', i.e. on empty.
#define TUPLE_INTERNAL_IS_EMPTY(...)                              \
  TUPLE_INTERNAL_IS_EMPTY_I(                                      \
      TUPLE_INTERNAL_HAS_COMMA(__VA_ARGS__),                      \
      TUPLE_INTERNAL_HAS_COMMA(TUPLE_INTERNAL_COMMA __VA_ARGS__), \
      TUPLE_INTERNAL_HAS_COMMA(__VA_ARGS__()),                    \
      TUPLE_INTERNAL_HAS_COMMA(TUPLE_INTERNAL_COMMA __VA_ARGS__()))

// Evaluates to _Then if _Cond is 1 and _Else if _Cond is 0.
#define TUPLE_INTERNAL_IF(_Cond, _Then, _Else) \
  TUPLE_INTERNAL_CAT(TUPLE_INTERNAL_IF_, _Cond)(_Then, _Else)

// Evaluates to the number of arguments after expansion. Identifies 'empty' as
// 0. Requires less than 64 arguments.
#define TUPLE_INTERNAL_NARG0(...)                            \
  TUPLE_INTERNAL_IF(TUPLE_INTERNAL_IS_EMPTY(__VA_ARGS__), 0, \
                    TUPLE_INTERNAL_NARG(__VA_ARGS__))

// Expands to 1 is there is only one argument and it is enclosed in parentheses.
// Requires less than 64 arguments.
#define TUPLE_INTERNAL_IS_PARENTHESIZED(...) \
  TUPLE_INTERNAL_IS_EMPTY(TUPLE_INTERNAL_EMPTY __VA_ARGS__)

// Removes the wrapping parenthesis, if they exist. Requires less than 64
// arguments.
#define TUPLE_INTERNAL_UNPARENTHESIZE(...)                                 \
  TUPLE_INTERNAL_IF(TUPLE_INTERNAL_IS_PARENTHESIZED(__VA_ARGS__),          \
                    TUPLE_INTERNAL_REMOVE_PARENS, TUPLE_INTERNAL_IDENTITY) \
  (__VA_ARGS__)

// Adds parenthesis if they do not exist. Requires less than 64
// arguments.
#define TUPLE_INTERNAL_PARENTHESIZE(...) \
  TUPLE_INTERNAL_ADD_PARENS(TUPLE_INTERNAL_UNPARENTHESIZE(__VA_ARGS__))

// Removes parenthesis. Requires argument enclosed in parenthesis.
#define TUPLE_INTERNAL_REMOVE_PARENS(...) TUPLE_INTERNAL_IDENTITY __VA_ARGS__

// Adds parenthesis.
#define TUPLE_INTERNAL_ADD_PARENS(...) (__VA_ARGS__)

// TUPLE_INTERNAL_FOR_EACH(F, data, (a1, ..., aN)) expands to
// F(data, a1) ... F(data, aN). Requires N to be less than 64.
#define TUPLE_INTERNAL_FOR_EACH(F, data, tuple) \
  TUPLE_INTERNAL_FOR_EACH_IMPL(F, data, TUPLE_INTERNAL_REMOVE_PARENS(tuple))

// TUPLE_INTERNAL_LIST_FOR_EACH(F, data, (a1, ..., aN)) expands to
// F(data, a1), ..., F(data, aN). Notice this includes comma separators. It also
// uses a different macro for repeated expansion, and it is useful for nested
// repeated expansions. Requires N to be less than 64.
#define TUPLE_INTERNAL_LIST_FOR_EACH(F, data, tuple) \
  TUPLE_INTERNAL_LIST_FOR_EACH_IMPL(F, data,         \
                                    TUPLE_INTERNAL_REMOVE_PARENS(tuple))

// Implementation details.
#define TUPLE_INTERNAL_STRINGIZE_I(...) #__VA_ARGS__

#define TUPLE_INTERNAL_CAT_I(A, ...) A##__VA_ARGS__

// Because of MSVC treating a token with a comma in it as a single token when
// passed to another macro, we need to force it to evaluate it as multiple
// tokens. We do that by using a "IDENTITY(MACRO PARENTHESIZED_ARGS)" macro. We
// define one per possible macro that relies on this behavior. Note "_Args" must
// be parenthesized.
#define TUPLE_INTERNAL_65TH_I(                                                 \
    _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16,     \
    _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, \
    _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, \
    _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, _59, _60, _61, \
    _62, _63, _64, x, ...)                                                     \
  x

#define TUPLE_INTERNAL_IDENTITY_VAR(_1) _1

#define TUPLE_INTERNAL_65TH(_Args) \
  TUPLE_INTERNAL_IDENTITY_VAR(TUPLE_INTERNAL_65TH_I _Args)

#define TUPLE_INTERNAL_CAT_5(_1, _2, _3, _4, _5) _1##_2##_3##_4##_5
#define TUPLE_INTERNAL_IS_EMPTY_I(_1, _2, _3, _4) \
  TUPLE_INTERNAL_HAS_COMMA(                       \
      TUPLE_INTERNAL_CAT_5(TUPLE_INTERNAL_IS_EMPTY_CASE_, _1, _2, _3, _4))
#define TUPLE_INTERNAL_IS_EMPTY_CASE_0001 ,
#define TUPLE_INTERNAL_IF_1(_Then, _Else) _Then
#define TUPLE_INTERNAL_IF_0(_Then, _Else) _Else

#define TUPLE_INTERNAL_FOR_EACH_IMPL(F, data, ...)      \
  TUPLE_INTERNAL_CAT(TUPLE_INTERNAL_FOR_EACH_,          \
                     TUPLE_INTERNAL_NARG0(__VA_ARGS__)) \
  (F, data, __VA_ARGS__)

#define TUPLE_INTERNAL_FOR_EACH_0(F, data, ...)
#define TUPLE_INTERNAL_FOR_EACH_1(F, data, X) F(data, X)
#define TUPLE_INTERNAL_FOR_EACH_2(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_1(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_3(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_2(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_4(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_3(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_5(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_4(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_6(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_5(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_7(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_6(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_8(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_7(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_9(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_8(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_10(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_9(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_11(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_10(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_12(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_11(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_13(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_12(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_14(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_13(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_15(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_14(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_16(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_15(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_17(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_16(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_18(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_17(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_19(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_18(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_20(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_19(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_21(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_20(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_22(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_21(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_23(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_22(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_24(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_23(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_25(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_24(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_26(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_25(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_27(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_26(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_28(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_27(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_29(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_28(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_30(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_29(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_31(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_30(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_32(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_31(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_33(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_32(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_34(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_33(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_35(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_34(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_36(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_35(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_37(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_36(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_38(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_37(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_39(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_38(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_40(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_39(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_41(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_40(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_42(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_41(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_43(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_42(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_44(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_43(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_45(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_44(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_46(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_45(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_47(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_46(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_48(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_47(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_49(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_48(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_50(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_49(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_51(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_50(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_52(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_51(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_53(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_52(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_54(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_53(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_55(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_54(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_56(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_55(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_57(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_56(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_58(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_57(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_59(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_58(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_60(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_59(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_61(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_60(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_62(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_61(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_63(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_62(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_FOR_EACH_64(F, data, X, ...) \
  F(data, X) TUPLE_INTERNAL_FOR_EACH_63(F, data, __VA_ARGS__)

#define TUPLE_INTERNAL_LIST_FOR_EACH_IMPL(F, data, ...) \
  TUPLE_INTERNAL_CAT(TUPLE_INTERNAL_LIST_FOR_EACH_,     \
                     TUPLE_INTERNAL_NARG0(__VA_ARGS__)) \
  (F, data, __VA_ARGS__)

#define TUPLE_INTERNAL_LIST_FOR_EACH_0(F, data, ...)
#define TUPLE_INTERNAL_LIST_FOR_EACH_1(F, data, X) F(data, X)
#define TUPLE_INTERNAL_LIST_FOR_EACH_2(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_1(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_3(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_2(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_4(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_3(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_5(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_4(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_6(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_5(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_7(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_6(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_8(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_7(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_9(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_8(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_10(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_9(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_11(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_10(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_12(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_11(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_13(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_12(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_14(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_13(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_15(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_14(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_16(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_15(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_17(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_16(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_18(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_17(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_19(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_18(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_20(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_19(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_21(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_20(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_22(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_21(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_23(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_22(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_24(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_23(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_25(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_24(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_26(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_25(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_27(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_26(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_28(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_27(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_29(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_28(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_30(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_29(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_31(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_30(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_32(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_31(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_33(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_32(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_34(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_33(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_35(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_34(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_36(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_35(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_37(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_36(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_38(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_37(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_39(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_38(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_40(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_39(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_41(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_40(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_42(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_41(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_43(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_42(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_44(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_43(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_45(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_44(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_46(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_45(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_47(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_46(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_48(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_47(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_49(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_48(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_50(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_49(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_51(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_50(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_52(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_51(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_53(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_52(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_54(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_53(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_55(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_54(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_56(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_55(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_57(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_56(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_58(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_57(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_59(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_58(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_60(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_59(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_61(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_60(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_62(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_61(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_63(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_62(F, data, __VA_ARGS__)
#define TUPLE_INTERNAL_LIST_FOR_EACH_64(F, data, X, ...) \
  F(data, X), TUPLE_INTERNAL_LIST_FOR_EACH_63(F, data, __VA_ARGS__)

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_INTERNAL_PREPROCESSOR_H_
