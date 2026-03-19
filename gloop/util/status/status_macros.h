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

// Helper macros and methods to return and propagate errors with `absl::Status`.
//
// See https://abseil.io/tips/121 for guidance on the use of these macros.

#ifndef THIRD_PARTY_GLOOP_UTIL_STATUS_STATUS_MACROS_H_
#define THIRD_PARTY_GLOOP_UTIL_STATUS_STATUS_MACROS_H_

#include <cstddef>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/source_location.h"
#include "gloop/util/status/status.h"
#include "gloop/util/status/status_builder.h"  // IWYU pragma: export

// Evaluates an expression that produces a `absl::Status`. If the status is not
// ok, returns it from the current function.
//
// For example:
//   absl::Status MultiStepFunction() {
//     RETURN_IF_ERROR(Function(args...));
//     RETURN_IF_ERROR(foo.Method(args...));
//     return absl::OkStatus();
//   }
//
// The macro ends with a `util::StatusBuilder` which allows the returned status
// to be extended with more details.  Any chained expressions after the macro
// will not be evaluated unless there is an error.
//
// For example:
//   absl::Status MultiStepFunction() {
//     RETURN_IF_ERROR(Function(args...)) << "in MultiStepFunction";
//     RETURN_IF_ERROR(foo.Method(args...)).Log(base_logging::ERROR)
//         << "while processing query: " << query.DebugString();
//     return absl::OkStatus();
//   }
//
// `util::StatusBuilder` supports adapting the builder chain using a `With`
// method and a functor.  This allows for powerful extensions to the macro.
//
// For example, teams can define local policies to use across their code:
//
//   StatusBuilder TeamPolicy(StatusBuilder builder) {
//     return std::move(builder.Log(base_logging::WARNING).Attach(...));
//   }
//
//   RETURN_IF_ERROR(foo()).With(TeamPolicy);
//   RETURN_IF_ERROR(bar()).With(TeamPolicy);
//
// Changing the return type allows the macro to be used with Task and Rpc
// interfaces.  See `util::TaskReturn` and `rpc::RpcSetStatus` for details.
//
//   void Read(StringPiece name, util::Task* task) {
//     int64 id;
//     RETURN_IF_ERROR(GetIdForName(name, &id)).With(TaskReturn(task));
//     RETURN_IF_ERROR(ReadForId(id)).With(TaskReturn(task));
//     task->Return();
//   }
//
// If using this macro inside a lambda, you need to annotate the return type
// to avoid confusion between a `util::StatusBuilder` and a `absl::Status` type.
// E.g.
//
//   []() -> absl::Status {
//     RETURN_IF_ERROR(Function(args...));
//     RETURN_IF_ERROR(foo.Method(args...));
//     return absl::OkStatus();
//   }
#define RETURN_IF_ERROR(expr) STATUS_MACROS_RETURN_IF_ERROR_IMPL_(return, expr)

// Executes an expression `rexpr` that returns an `absl::StatusOr<T>`. On OK,
// moves its value into the variable defined by `lhs`, otherwise returns
// from the current function. By default the error status is returned
// unchanged, but it may be modified by an `error_expression`. If there is an
// error, `lhs` is not evaluated; thus any side effects that `lhs` may have
// only occur in the success case.
//
// Interface:
//
//   ASSIGN_OR_RETURN(lhs, rexpr)
//   ASSIGN_OR_RETURN(lhs, rexpr, error_expression);
//
// WARNING: if lhs is parenthesized, the parentheses are removed. See examples
// for more details.
//
// WARNING: expands into multiple statements; it cannot be used in a single
// statement (e.g. as the body of an if statement without {})!
//
// Example: Declaring and initializing a new variable (ValueType can be anything
//          that can be initialized with assignment--including a const
//          reference, although that's discouraged by
//          https://abseil.io/tips/107):
//   ASSIGN_OR_RETURN(ValueType value, MaybeGetValue(arg));
//
// Example: Assigning to an existing variable:
//   ValueType value;
//   ASSIGN_OR_RETURN(value, MaybeGetValue(arg));
//
// Example: Assigning to an expression with side effects:
//   MyProto data;
//   ASSIGN_OR_RETURN(*data.mutable_str(), MaybeGetValue(arg));
//   // No field "str" is added on error.
//
// Example: Initializing a `std::unique_ptr`.
//   ASSIGN_OR_RETURN(std::unique_ptr<T> ptr, MaybeGetPtr(arg));
//
// Example: Initializing a map. Because of C++ preprocessor limitations,
// the type used in ASSIGN_OR_RETURN cannot contain commas, so wrap the
// lhs in parentheses:
//   ASSIGN_OR_RETURN((absl::flat_hash_map<Foo, Bar> my_map), GetMap());
// Or use `auto` if the type is obvious enough:
//   ASSIGN_OR_RETURN(auto my_map, GetMap());
//
// Example: Assigning to structured bindings (https://abseil.io/tips/169). The
// same situation with comma as in map, so wrap the statement in parentheses.
//   ASSIGN_OR_RETURN((auto [first, second]), GetPair());
//
// If passed, the `error_expression` is evaluated to produce the return
// value. The expression may reference any variable visible in scope, as
// well as a `util::StatusBuilder` object populated with the error and
// named by a single underscore `_`. The expression typically uses the
// builder to modify the status and is returned directly in manner similar
// to RETURN_IF_ERROR. The expression may, however, evaluate to any type
// returnable by the function, including (void). For example:
//
// Example: Adjusting the error message.
//   ASSIGN_OR_RETURN(ValueType value, MaybeGetValue(query),
//                    _ << "while processing query " << query.DebugString());
//
// Example: Logging the error on failure.
//   ASSIGN_OR_RETURN(ValueType value, MaybeGetValue(query), _.LogError());
//
#define ASSIGN_OR_RETURN(...) \
  STATUS_MACROS_ASSIGN_OR_RETURN_IMPL_(return, __VA_ARGS__)

// =================================================================
// == Implementation details, do not rely on anything below here. ==
// =================================================================

#define STATUS_MACROS_RETURN_IF_ERROR_IMPL_(return_keyword, expr) \
  STATUS_MACROS_IMPL_ELSE_BLOCKER_                                \
  if (auto status_macro_internal_adaptor =                        \
          ::util::status_macro_internal::MacroAdaptor(            \
              (expr), ::absl::SourceLocation::current())) {       \
  } else /* NOLINT */                                             \
    return_keyword status_macro_internal_adaptor.Consume(         \
        ::absl::SourceLocation::current())

#define STATUS_MACROS_ASSIGN_OR_RETURN_IMPL_(return_keyword, ...)            \
  STATUS_MACROS_IMPL_GET_VARIADIC_((return_keyword, __VA_ARGS__,             \
                                    STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_3_,  \
                                    STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_2_)) \
  (return_keyword, __VA_ARGS__)

constexpr bool HasPotentialConditionalOperator(const char* lhs, int size) {
  for (int i = 0; i < size; ++i) {
    if (lhs[i] == '?') {
      return true;
    }
  }
  return false;
}

template <std::size_t N>
constexpr bool IsEnclosedByParentheses(const char (&lhs)[N]) {
  if (N < 2) {
    return false;
  }
  return lhs[0] == '(' && lhs[N - 2] == ')';
}

namespace util::status_macro_internal {

template <typename T, typename EnableIf = void>
struct IsAllowedStatusOrMacroType : std::false_type {};

template <typename T>
struct IsAllowedStatusOrMacroType<
    T, std::enable_if_t<std::is_convertible_v<
           T*, typename absl::StatusOr<typename T::value_type>*>>>
    : std::true_type {};

}  // namespace util::status_macro_internal

// MSVC incorrectly expands variadic macros, splice together a macro call to
// work around the bug.
#define STATUS_MACROS_IMPL_GET_VARIADIC_HELPER_(_1, _2, _3, _4, NAME, ...) NAME
#define STATUS_MACROS_IMPL_GET_VARIADIC_(args)            \
  /* NOLINTNEXTLINE(clang-diagnostic-pre-c++20-compat) */ \
  STATUS_MACROS_IMPL_GET_VARIADIC_HELPER_ args

#define STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_2_(return_keyword, lhs, rexpr)  \
  STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_(                                     \
      STATUS_MACROS_IMPL_CONCAT_(_status_or_value, __LINE__), lhs, rexpr,   \
      return_keyword absl::Status(                                          \
          std::move(STATUS_MACROS_IMPL_CONCAT_(_status_or_value, __LINE__)) \
              .status(),                                                    \
          ::absl::SourceLocation::current()))

#define STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_3_(return_keyword, lhs, rexpr,  \
                                               error_expression)            \
  STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_(                                     \
      STATUS_MACROS_IMPL_CONCAT_(_status_or_value, __LINE__), lhs,          \
      rexpr, /* NOLINTNEXTLINE(misc-const-correctness) */                   \
      ::util::StatusBuilder _(                                              \
          std::move(STATUS_MACROS_IMPL_CONCAT_(_status_or_value, __LINE__)) \
              .status(),                                                    \
          ::absl::SourceLocation::current());                               \
      (void)_; /* error_expression is allowed to not use this variable */   \
      return_keyword(error_expression))

#define STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_(statusor, lhs, rexpr,     \
                                             error_expression)         \
  auto statusor = (rexpr);                                             \
  if (ABSL_PREDICT_FALSE(!statusor.ok())) {                            \
    error_expression;                                                  \
  }                                                                    \
  {                                                                    \
    static_assert(                                                     \
        !IsEnclosedByParentheses(#lhs) ||                              \
            !HasPotentialConditionalOperator(#lhs, sizeof(#lhs) - 2),  \
        "Identified potential conditional operator, consider not "     \
        "using ASSIGN_OR_RETURN");                                     \
  }                                                                    \
  {                                                                    \
    static_assert(                                                     \
        ::util::status_macro_internal::IsAllowedStatusOrMacroType<     \
            typename std::remove_const<decltype(statusor)>::type>(),   \
        "ASSIGN_OR_RETURN should only be used with absl::StatusOr<>"); \
  }                                                                    \
  STATUS_MACROS_IMPL_UNPARENTHESIZE_IF_PARENTHESIZED(lhs) =            \
      (*std::move(statusor))

// Internal helpers for macro expansion.
#define STATUS_MACROS_IMPL_EAT(...)
#define STATUS_MACROS_IMPL_REM(...) __VA_ARGS__
#define STATUS_MACROS_IMPL_EMPTY()

// Internal helpers for emptiness arguments check.
#define STATUS_MACROS_IMPL_IS_EMPTY_INNER(...) \
  STATUS_MACROS_IMPL_IS_EMPTY_INNER_HELPER((__VA_ARGS__, 0, 1))
// MSVC expands variadic macros incorrectly, so we need this extra indirection
// to work around that (b/110959038).
#define STATUS_MACROS_IMPL_IS_EMPTY_INNER_HELPER(args) \
  STATUS_MACROS_IMPL_IS_EMPTY_INNER_I args
#define STATUS_MACROS_IMPL_IS_EMPTY_INNER_I(e0, e1, is_empty, ...) is_empty

#define STATUS_MACROS_IMPL_IS_EMPTY(...) \
  STATUS_MACROS_IMPL_IS_EMPTY_I(__VA_ARGS__)
#define STATUS_MACROS_IMPL_IS_EMPTY_I(...) \
  STATUS_MACROS_IMPL_IS_EMPTY_INNER(_ __VA_OPT__(, )##__VA_ARGS__)

// Internal helpers for if statement.
#define STATUS_MACROS_IMPL_IF_1(_Then, _Else) _Then
#define STATUS_MACROS_IMPL_IF_0(_Then, _Else) _Else
#define STATUS_MACROS_IMPL_IF(_Cond, _Then, _Else) \
  STATUS_MACROS_IMPL_CONCAT_(STATUS_MACROS_IMPL_IF_, _Cond)(_Then, _Else)

// Expands to 1 if the input is parenthesized. Otherwise expands to 0.
#define STATUS_MACROS_IMPL_IS_PARENTHESIZED(...) \
  STATUS_MACROS_IMPL_IS_EMPTY(STATUS_MACROS_IMPL_EAT __VA_ARGS__)

// If the input is parenthesized, removes the parentheses. Otherwise expands to
// the input unchanged.
#define STATUS_MACROS_IMPL_UNPARENTHESIZE_IF_PARENTHESIZED(...)             \
  STATUS_MACROS_IMPL_IF(STATUS_MACROS_IMPL_IS_PARENTHESIZED(__VA_ARGS__),   \
                        STATUS_MACROS_IMPL_REM, STATUS_MACROS_IMPL_EMPTY()) \
  __VA_ARGS__

// Internal helper for concatenating macro values.
#define STATUS_MACROS_IMPL_CONCAT_INNER_(x, y) x##y
#define STATUS_MACROS_IMPL_CONCAT_(x, y) STATUS_MACROS_IMPL_CONCAT_INNER_(x, y)

// The GNU compiler emits a warning for code like:
//
//   if (foo)
//     if (bar) { } else baz;
//
// because it thinks you might want the else to bind to the first if.  This
// leads to problems with code like:
//
//   if (do_expr) RETURN_IF_ERROR(expr) << "Some message";
//
// The "switch (0) case 0:" idiom is used to suppress this.
#define STATUS_MACROS_IMPL_ELSE_BLOCKER_ \
  switch (0)                             \
  case 0:                                \
  default:  // NOLINT

namespace util {
namespace status_macro_internal {

// Provides a conversion to bool so that it can be used inside an if statement
// that declares a variable.
class StatusAdaptorForMacros {
 public:
  StatusAdaptorForMacros();
  ~StatusAdaptorForMacros() = default;

  StatusAdaptorForMacros(const absl::Status& status, absl::SourceLocation loc)
      : builder_(status, loc) {}

  StatusAdaptorForMacros(absl::Status&& status, absl::SourceLocation loc)
      : builder_(std::move(status), loc) {}

  StatusAdaptorForMacros(const StatusBuilder& builder, absl::SourceLocation)
      : builder_(builder) {}

  StatusAdaptorForMacros(StatusBuilder&& builder, absl::SourceLocation)
      : builder_(std::move(builder)) {}

  StatusAdaptorForMacros(const StatusAdaptorForMacros&) = delete;
  StatusAdaptorForMacros& operator=(const StatusAdaptorForMacros&) = delete;

  explicit operator bool() const { return ABSL_PREDICT_TRUE(builder_.ok()); }

  StatusBuilder&& Consume() { return std::move(builder_); }

  // Called by RETURN_IF_ERROR for non-Status arguments.
  // We ignore the argument since it was also passed to the constructor.
  StatusBuilder&& Consume(absl::SourceLocation) { return std::move(builder_); }

 private:
  StatusBuilder builder_;
};

// Special adaptor for use by RETURN_IF_ERROR for absl::Status arguments.
// This one avoids constructing a StatusBuilder on the fast path.
//
// REQUIRES: Only used by RETURN_IF_ERROR implementation.
class ReturnIfErrorAdaptor {
 public:
  explicit ReturnIfErrorAdaptor(const absl::Status& status) : status_(status) {}
  explicit ReturnIfErrorAdaptor(absl::Status&& status)
      : status_(std::move(status)) {}

  ReturnIfErrorAdaptor() = delete;
  ReturnIfErrorAdaptor(const ReturnIfErrorAdaptor&) = delete;
  ReturnIfErrorAdaptor& operator=(const ReturnIfErrorAdaptor&) = delete;

  ~ReturnIfErrorAdaptor() {
    // WARNING! WARNING! WARNING!
    //
    // We play fast and loose here and avoid destroying status_. This should be
    // safe because status_ will never own memory at destruction time. The two
    // cases to consider are:
    //  (1) OK: OkStatus() representation needs no cleanup
    //  (2) Not-OK: we take the else branch in RETURN_IF_ERROR and move
    //      status_ into StatusBuilder which leaves status_ with a MovedFromRep
    //      that needs no cleanup.
    // If the absl::Status implementation changes, leaks should be caught by
    // the various tests we have when run under lsan or debug leak checker.
  }

  explicit operator bool() const { return ABSL_PREDICT_TRUE(status_.ok()); }

  StatusBuilder Consume(absl::SourceLocation loc) {
    return StatusBuilder(std::move(status_), loc);
  }

 private:
  // Place the status inside a union so we can avoid generating unnecessary code
  // to call the Status destructor.
  union {
    absl::Status status_;
    char nothing_[1];
  };
};

// Overloads of MacroAdaptor that pick the right adaptor class
// for each argument type.
inline ReturnIfErrorAdaptor MacroAdaptor(const absl::Status& s,
                                         absl::SourceLocation) {
  return ReturnIfErrorAdaptor(s);
}
inline ReturnIfErrorAdaptor MacroAdaptor(absl::Status&& s,
                                         absl::SourceLocation) {
  return ReturnIfErrorAdaptor(std::move(s));
}
inline StatusAdaptorForMacros MacroAdaptor(const StatusBuilder& s,
                                           absl::SourceLocation loc) {
  return StatusAdaptorForMacros(s, loc);
}
inline StatusAdaptorForMacros MacroAdaptor(
    StatusBuilder&& s, absl::SourceLocation loc = absl::SourceLocation()) {
  return StatusAdaptorForMacros(std::move(s), loc);
}

}  // namespace status_macro_internal
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_STATUS_STATUS_MACROS_H_
