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

// Macros for non-fatal assertions.  The `RET_CHECK` family of macros mirrors
// the `CHECK` family from "absl/log/check.h", but instead of
// aborting the process on failure, these return a absl::Status with code
// `util::error::INTERNAL` from the current method.
//
//   RET_CHECK(ptr != nullptr);
//   RET_CHECK_GT(value, 0) << "Optional additional message";
//   RET_CHECK_FAIL() << "Always fails";
//   RET_CHECK_OK(status) << "If status is not OK, return an internal error";
//   RET_QCHECK(!flag.empty()) << "--flag is required";
//
// These macros can only be used in functions that return `absl::Status` or
// `absl::StatusOr`. The generated `absl::Status` will contain the string
// "RET_CHECK failure". The RET_CHECK_{EQ,GT,...} comparison macros impose the
// same printability requirements as CHECK_EQ and friends: their arguments must
// be writable to std::ostream or support <link>.
//
// On failure, the `RET_CHECK*` macros log the stack trace to `ERROR`,
// while the `RET_QCHECK*` macros do not.
//
// These macros end with a `util::StatusBuilder` in their tail
// position and can be customized like calls to `ABSL_RETURN_IF_ERROR` from
// "https://github.com/abseil/gloop/tree/main/gloop/util/status/status_macros.h".
//
// Be careful with the usage of `RET_CHECK_*` for checking user-sensitive data
// since it logs the underlying input values on failure. `RET_CHECK` is a safer
// way for this situation because it just logs the input condition as a string
// literal on failure.
//
// Flag --ret_check_abort_on_failure can be used to overwrite default behavior
// and abort the process on failure by logging to `FATAL` instead of `ERROR`.
// Setting this flag can be useful in some test cases, but not recommended in
// production.

#ifndef THIRD_PARTY_GLOOP_UTIL_STATUS_RET_CHECK_H_
#define THIRD_PARTY_GLOOP_UTIL_STATUS_RET_CHECK_H_

#include <cstddef>
#include <ostream>
#include <sstream>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/flags/declare.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/has_absl_stringify.h"
#include "absl/strings/has_ostream_operator.h"
#include "absl/types/source_location.h"
#include "gloop/util/status/status_builder.h"
#include "gloop/util/status/status_macros.h"

ABSL_DECLARE_FLAG(bool, ret_check_abort_on_failure);

namespace util {
namespace internal_status_macros_ret_check {

// Returns a StatusBuilder that corresponds to a `RET_CHECK` failure.
StatusBuilder RetCheckFailSlowPath(absl::SourceLocation location);
StatusBuilder RetCheckFailSlowPath(absl::SourceLocation location,
                                   const char* condition);
StatusBuilder RetCheckFailSlowPath(absl::SourceLocation location,
                                   const char* condition,
                                   const absl::Status& s);

// Takes ownership of `condition`.  This API is a little quirky because it is
// designed to make use of the `::Check_*Impl` methods that implement `CHECK_*`
// and `DCHECK_*`.
StatusBuilder RetCheckFailSlowPath(absl::SourceLocation location,
                                   std::string* condition);

inline const absl::Status& AsStatus(const absl::Status& status) {
  return status;
}

template <typename T>
inline const absl::Status& AsStatus(const absl::StatusOr<T>& status_or) {
  return status_or.status();
}

// A helper class for formatting `expr (V1 vs. V2)` in a `RET_CHECK_XX`
// statement.  See `MakeCheckOpString` for sample usage.
class CheckOpMessageBuilder {
 public:
  // Inserts `exprtext` and ` (` to the stream.
  explicit CheckOpMessageBuilder(const char* exprtext);
  // Deletes `stream_`.
  ~CheckOpMessageBuilder();
  // For inserting the first variable.
  std::ostream* ForVar1() { return stream_; }
  // For inserting the second variable (adds an intermediate ` vs. `).
  std::ostream* ForVar2();
  // Get the result (inserts the closing `)`).
  std::string* NewString();

 private:
  std::ostringstream* stream_;
};

// Formats a value for a failing `RET_CHECK_XX` statement.  Excepting a few
// special-case overloads below, behavior attempts to mimic ABSL logging.
// The stream operator is used if supported; else absl::Stringify() support
// is checked.
template <typename T>
inline void MakeCheckOpValueString(std::ostream* os, const T& v) {
  // Keeping to C++17, since this gets compiled under --config=darwin_arm64.
  if constexpr (absl::HasOstreamOperator<T>::value) {
    (*os) << v;
  } else {
    static_assert(absl::HasAbslStringify<T>::value,
                  "Type must support the stream operator or absl::Stringify");
    absl::Format(os, "%v", v);
  }
}

// Overrides for char types provide readable values for unprintable characters.
void MakeCheckOpValueString(std::ostream* os, char v);
void MakeCheckOpValueString(std::ostream* os, signed char v);
void MakeCheckOpValueString(std::ostream* os, unsigned char v);

// We need an explicit specialization for `std::nullptr_t`.
void MakeCheckOpValueString(std::ostream* os, std::nullptr_t v);
void MakeCheckOpValueString(std::ostream* os, const char* v);
void MakeCheckOpValueString(std::ostream* os, const signed char* v);
void MakeCheckOpValueString(std::ostream* os, const unsigned char* v);
void MakeCheckOpValueString(std::ostream* os, char* v);
void MakeCheckOpValueString(std::ostream* os, signed char* v);
void MakeCheckOpValueString(std::ostream* os, unsigned char* v);

// Build the error message string.  Specify no inlining for code size.
template <typename T1, typename T2>
std::string* MakeCheckOpString(const T1& v1, const T2& v2,
                               const char* exprtext) ABSL_ATTRIBUTE_NOINLINE;

template <typename T1, typename T2>
std::string* MakeCheckOpString(const T1& v1, const T2& v2,
                               const char* exprtext) {
  CheckOpMessageBuilder comb(exprtext);
  util::internal_status_macros_ret_check::MakeCheckOpValueString(comb.ForVar1(),
                                                                 v1);
  util::internal_status_macros_ret_check::MakeCheckOpValueString(comb.ForVar2(),
                                                                 v2);
  return comb.NewString();
}

// Helper functions for `UTIL_TASK_CONTRIB_STATUS_MACROS_INTERNAL_RET_CHECK_OP`
// macro.  The `(int, int)` specialization works around the issue that the
// compiler will not instantiate the template version of the function on values
// of unnamed enum type - see comment below.
#define UTIL_TASK_CONTRIB_STATUS_MACROS_INTERNAL_DEFINE_CHECK_OP_IMPL(name, \
                                                                      op)   \
  template <typename T1, typename T2>                                       \
  inline std::string* name##Impl(const T1& v1, const T2& v2,                \
                                 const char* exprtext) {                    \
    if (ABSL_PREDICT_TRUE(v1 op v2))                                        \
      return nullptr;                                                       \
    else                                                                    \
      return ::util::internal_status_macros_ret_check::MakeCheckOpString(   \
          v1, v2, exprtext);                                                \
  }                                                                         \
  inline std::string* name##Impl(int v1, int v2, const char* exprtext) {    \
    return ::util::internal_status_macros_ret_check::name##Impl<int, int>(  \
        v1, v2, exprtext);                                                  \
  }

UTIL_TASK_CONTRIB_STATUS_MACROS_INTERNAL_DEFINE_CHECK_OP_IMPL(Check_EQ, ==)
UTIL_TASK_CONTRIB_STATUS_MACROS_INTERNAL_DEFINE_CHECK_OP_IMPL(Check_NE, !=)
UTIL_TASK_CONTRIB_STATUS_MACROS_INTERNAL_DEFINE_CHECK_OP_IMPL(Check_LE, <=)
UTIL_TASK_CONTRIB_STATUS_MACROS_INTERNAL_DEFINE_CHECK_OP_IMPL(Check_LT, <)
UTIL_TASK_CONTRIB_STATUS_MACROS_INTERNAL_DEFINE_CHECK_OP_IMPL(Check_GE, >=)
UTIL_TASK_CONTRIB_STATUS_MACROS_INTERNAL_DEFINE_CHECK_OP_IMPL(Check_GT, >)
#undef UTIL_TASK_CONTRIB_STATUS_MACROS_INTERNAL_DEFINE_CHECK_OP_IMPL

// `RET_CHECK_EQ` and friends want to pass their arguments by reference, however
// this winds up exposing lots of cases where people have defined and
// initialized static const data members but never declared them (i.e. in a .cc
// file), meaning they are not referenceable.  This function avoids that problem
// for integers (the most common cases) by overloading for every primitive
// integer type, even the ones we discourage, and returning them by value.
template <typename T>
inline const T& GetReferenceableValue(const T& t) {
  return t;
}
inline char GetReferenceableValue(char t) { return t; }
inline unsigned char GetReferenceableValue(unsigned char t) { return t; }
inline signed char GetReferenceableValue(signed char t) { return t; }
inline short GetReferenceableValue(short t) { return t; }
inline unsigned short GetReferenceableValue(unsigned short t) { return t; }
inline int GetReferenceableValue(int t) { return t; }
inline unsigned int GetReferenceableValue(unsigned int t) { return t; }
inline long GetReferenceableValue(long t) { return t; }
inline unsigned long GetReferenceableValue(unsigned long t) { return t; }
inline long long GetReferenceableValue(long long t) { return t; }
inline unsigned long long GetReferenceableValue(unsigned long long t) {
  return t;
}

}  // namespace internal_status_macros_ret_check
}  // namespace util

#define RET_CHECK(cond)                                                  \
  while (ABSL_PREDICT_FALSE(!(cond)))                                    \
  return ::util::internal_status_macros_ret_check::RetCheckFailSlowPath( \
      ::absl::SourceLocation::current(), #cond)
#define RET_QCHECK(condition) RET_CHECK(condition).SetNoLogging()

#define RET_CHECK_FAIL()                                                 \
  return ::util::internal_status_macros_ret_check::RetCheckFailSlowPath( \
      ::absl::SourceLocation::current())
#define RET_QCHECK_FAIL() RET_CHECK_FAIL().SetNoLogging()

// Takes an expression returning absl::Status and asserts that the status is
// `ok()`.  If not, it returns a `util::error::INTERNAL` error.
//
// This is similar to `ABSL_RETURN_IF_ERROR` in that it propagates errors.  The
// difference is that it follows the behavior of `RET_CHECK`, returning an
// internal error (wrapping the original error text), including the filename and
// line number, and logging a stack trace.
//
// This is appropriate to use to write an assertion that a function that returns
// `absl::Status` cannot fail, particularly when the error code itself should
// not be surfaced.
#define RET_CHECK_OK(status)                                               \
  ABSL_INTERNAL_STATUS_MACROS_IMPL_ELSE_BLOCKER_                           \
  if (const absl::Status ret_check_macro_status =                          \
          ::util::internal_status_macros_ret_check::AsStatus(status);      \
      ret_check_macro_status.ok()) {                                       \
  } else /* NOLINT */                                                      \
    return ::util::internal_status_macros_ret_check::RetCheckFailSlowPath( \
        ::absl::SourceLocation::current(), #status, ret_check_macro_status)
#define RET_QCHECK_OK(status) RET_CHECK_OK(status).SetNoLogging()

#if defined(STATIC_ANALYSIS) || defined(PORTABLE_STATUS)
#define UTIL_TASK_CONTRIB_STATUS_MACROS_INTERNAL_RET_CHECK_OP(name, op, lhs, \
                                                              rhs)           \
  RET_CHECK((lhs)op(rhs))
#else
#define UTIL_TASK_CONTRIB_STATUS_MACROS_INTERNAL_RET_CHECK_OP(name, op, lhs,   \
                                                              rhs)             \
  while (                                                                      \
      std::string* _result =                                                   \
          ::util::internal_status_macros_ret_check::Check_##name##Impl(        \
              ::util::internal_status_macros_ret_check::GetReferenceableValue( \
                  lhs),                                                        \
              ::util::internal_status_macros_ret_check::GetReferenceableValue( \
                  rhs),                                                        \
              #lhs " " #op " " #rhs))                                          \
  return ::util::internal_status_macros_ret_check::RetCheckFailSlowPath(       \
      ::absl::SourceLocation::current(), _result)
#endif

#define RET_CHECK_EQ(lhs, rhs) \
  UTIL_TASK_CONTRIB_STATUS_MACROS_INTERNAL_RET_CHECK_OP(EQ, ==, lhs, rhs)
#define RET_CHECK_NE(lhs, rhs) \
  UTIL_TASK_CONTRIB_STATUS_MACROS_INTERNAL_RET_CHECK_OP(NE, !=, lhs, rhs)
#define RET_CHECK_LE(lhs, rhs) \
  UTIL_TASK_CONTRIB_STATUS_MACROS_INTERNAL_RET_CHECK_OP(LE, <=, lhs, rhs)
#define RET_CHECK_LT(lhs, rhs) \
  UTIL_TASK_CONTRIB_STATUS_MACROS_INTERNAL_RET_CHECK_OP(LT, <, lhs, rhs)
#define RET_CHECK_GE(lhs, rhs) \
  UTIL_TASK_CONTRIB_STATUS_MACROS_INTERNAL_RET_CHECK_OP(GE, >=, lhs, rhs)
#define RET_CHECK_GT(lhs, rhs) \
  UTIL_TASK_CONTRIB_STATUS_MACROS_INTERNAL_RET_CHECK_OP(GT, >, lhs, rhs)

#define RET_QCHECK_EQ(lhs, rhs) RET_CHECK_EQ(lhs, rhs).SetNoLogging()
#define RET_QCHECK_NE(lhs, rhs) RET_CHECK_NE(lhs, rhs).SetNoLogging()
#define RET_QCHECK_LE(lhs, rhs) RET_CHECK_LE(lhs, rhs).SetNoLogging()
#define RET_QCHECK_LT(lhs, rhs) RET_CHECK_LT(lhs, rhs).SetNoLogging()
#define RET_QCHECK_GE(lhs, rhs) RET_CHECK_GE(lhs, rhs).SetNoLogging()
#define RET_QCHECK_GT(lhs, rhs) RET_CHECK_GT(lhs, rhs).SetNoLogging()

#endif  // THIRD_PARTY_GLOOP_UTIL_STATUS_RET_CHECK_H_
