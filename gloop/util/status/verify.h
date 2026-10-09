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

// Macro-free utilities to verify conditions or relationships between
// expressions. For example:
//
//   // Returns OkStatus if x == y, otherwise returns a status that includes
//   // line location where Verify() is called.
//   absl::Status error = Verify(x == y);
//
//   // Returns OkStatus if x == y, otherwise returns a similar status to the
//   // above but which also includes the values of `x` and `y`.
//   absl::Status error = VerifyEq(x, y);
//
// To use the binary comparators (Eq, Ne, etc.), the arguments `lhs` and `rhs`
// must be types that are printable by absl::StrCat.
//
// This usage pattern is common:
//
//  absl::Status Compute(int x, int y) {
//    RETURN_IF_ERROR(util::VerifyLt(x, y)) << "Invalid parameters.";
//    ...
//    return absl::OkStatus();
//  }

#ifndef THIRD_PARTY_GLOOP_UTIL_STATUS_VERIFY_H_
#define THIRD_PARTY_GLOOP_UTIL_STATUS_VERIFY_H_

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/source_location.h"

namespace util {

ABSL_DEPRECATED("Please call absl::InternalError directly.")
inline absl::Status Fail(
    absl::string_view error_message,
    absl::SourceLocation loc = absl::SourceLocation::current()) {
  return absl::InternalError(absl::StrFormat("Fail(%s:%d): %s", loc.file_name(),
                                             loc.line(), error_message));
}

inline absl::Status Verify(
    const bool cond, absl::string_view error_message = absl::string_view(),
    absl::SourceLocation loc = absl::SourceLocation::current()) {
  if (ABSL_PREDICT_TRUE(cond)) {
    return absl::OkStatus();
  } else {
    return Fail(error_message, loc);
  }
}

template <typename LHS, typename RHS>
inline absl::Status VerifyEq(
    const LHS& lhs, const RHS& rhs,
    absl::string_view error_message = absl::string_view(),
    absl::SourceLocation loc = absl::SourceLocation::current()) {
  if (ABSL_PREDICT_TRUE(lhs == rhs)) {
    return absl::OkStatus();
  }
  return Fail(absl::StrCat(lhs, " == ", rhs, " ", error_message), loc);
}

template <typename LHS, typename RHS>
inline absl::Status VerifyNe(
    const LHS& lhs, const RHS& rhs,
    absl::string_view error_message = absl::string_view(),
    absl::SourceLocation loc = absl::SourceLocation::current()) {
  if (ABSL_PREDICT_TRUE(lhs != rhs)) {
    return absl::OkStatus();
  }
  return Fail(absl::StrCat(lhs, " != ", rhs, " ", error_message), loc);
}

template <typename LHS, typename RHS>
inline absl::Status VerifyLe(
    const LHS& lhs, const RHS& rhs,
    absl::string_view error_message = absl::string_view(),
    absl::SourceLocation loc = absl::SourceLocation::current()) {
  if (ABSL_PREDICT_TRUE(lhs <= rhs)) {
    return absl::OkStatus();
  }
  return Fail(absl::StrCat(lhs, " <= ", rhs, " ", error_message), loc);
}

template <typename LHS, typename RHS>
inline absl::Status VerifyLt(
    const LHS& lhs, const RHS& rhs,
    absl::string_view error_message = absl::string_view(),
    absl::SourceLocation loc = absl::SourceLocation::current()) {
  if (ABSL_PREDICT_TRUE(lhs < rhs)) {
    return absl::OkStatus();
  }
  return Fail(absl::StrCat(lhs, " < ", rhs, " ", error_message), loc);
}

template <typename LHS, typename RHS>
inline absl::Status VerifyGe(
    const LHS& lhs, const RHS& rhs,
    absl::string_view error_message = absl::string_view(),
    absl::SourceLocation loc = absl::SourceLocation::current()) {
  if (ABSL_PREDICT_TRUE(lhs >= rhs)) {
    return absl::OkStatus();
  }
  return Fail(absl::StrCat(lhs, " >= ", rhs, " ", error_message), loc);
}

template <typename LHS, typename RHS>
inline absl::Status VerifyGt(
    const LHS& lhs, const RHS& rhs,
    absl::string_view error_message = absl::string_view(),
    absl::SourceLocation loc = absl::SourceLocation::current()) {
  if (ABSL_PREDICT_TRUE(lhs > rhs)) {
    return absl::OkStatus();
  }
  return Fail(absl::StrCat(lhs, " > ", rhs, " ", error_message), loc);
}

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_STATUS_VERIFY_H_
