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

#ifndef THIRD_PARTY_GLOOP_UTIL_TASK_STATUS_MATCHERS_H_
#define THIRD_PARTY_GLOOP_UTIL_TASK_STATUS_MATCHERS_H_
// In most cases, you should be using the Abseil status matchers.  See:
// "absl/status/status_matchers.h"
//
// Open source (i.e. Abseil) does not support:
// - Error spaces
// - Status macros
//
// Error spaces will not be open sourced, and if you're working in a space where
// error spaces may be non-canonical, use the matchers here.
//
// /!\ IMPORTANT /!\
// `absl::StatusIs` no longer automatically deduces your error space.  If you're
// operating in a non-canonical error space, you must use
// `StatusInErrorSpaceIs`.  It is suggested to locally define your own matchers
// based on this one.
//
// E.g., assume you have your own error space `FancyErrorSpace`.  Then, define
// your own versions (presumably in some local test header):
// ```
// template<typename StatusMatcherT, typename MessageMatcherT>
// auto FancyStatusIs(StatusMatcherT status_matcher,
//                    MessageMatcherT message_matcher) {
//   return util::StatusInErrorSpaceIs(
//       FancyErrorSpace::Get(),
//       std::forward<StatusMatcherT>(status_matcher),
//       std::forward<MessageMatcherT>(message_matcher));
// }
//
// template<typename StatusMatcherT>
// auto FancyStatusIs(StatusMatcherT status_matcher) {
//   return FancySpaceIs(std::forward<StatusMatcherT>(status_matcher),
//                       ::testing::_);
// }
// ```
//
// Then use this like `StatusIs` as normal, e.g.:
// ```
// EXPECT_THAT(FancyFun(42), absl::IsOk());
// EXPECT_THAT(FancyFun(43), FancyStatusIs(FancyErrorSpace::kFancyError));
// EXPECT_THAT(FancyFun(43),
//             FancyStatusIs(FancyErrorSpace::kFancyError,
//                           HasSubstr("43")));
// ```
//
// Note that `absl::IsOk()` works with non-canonical error spaces.
#include "gloop/util/status/status_macros.h"  // IWYU pragma: export
#include "gloop/util/task/status_matchers_internal.h"
#include "gmock/gmock.h"

namespace util {

// Executes an expression that returns an absl::StatusOr, and assigns the
// contained variable to lhs if the error code is OK.
// If the Status is non-OK, generates a test failure and returns from the
// current function, which must have a void return type.
//
// Example: Declaring and initializing a new value
//   ABSL_ASSERT_OK_AND_ASSIGN(const ValueType& value, MaybeGetValue(arg));
//
// Example: Assigning to an existing value
//   ValueType value;
//   ABSL_ASSERT_OK_AND_ASSIGN(value, MaybeGetValue(arg));
//
// The value assignment example would expand into something like:
//   absl::StatusOr<ValueType> new_value = MaybeGetValue(arg);
//   ASSERT_THAT(new_value, ::absl_testing::IsOk());
//   value = *std::move(new_value);
//
// WARNING: Like ABSL_ASSIGN_OR_RETURN, ABSL_ASSERT_OK_AND_ASSIGN expands into
// multiple statements; it cannot be used in a single statement (e.g. as the
// body of an if statement without {})!
#define ABSL_ASSERT_OK_AND_ASSIGN(lhs, rexpr)                 \
  ABSL_ASSIGN_OR_RETURN(/* NOLINT(clang-diagnostic-shadow) */ \
                        lhs, rexpr,                           \
                        ::util::status_internal::AddFatalFailure(#rexpr, _))
#define ASSERT_OK_AND_ASSIGN(lhs, rexpr) ABSL_ASSERT_OK_AND_ASSIGN(lhs, rexpr)

// This gMock matcher matches a Status or StatusOr<T> or StatusProto value if
// all of the following are true:
//
//   - the status' error_space() matches error_space_matcher,
//   - the status' error_code() matches status_code_matcher, and
//   - the status' error_message() matches error_message_matcher.
//
// Example:
// ```
// enum FooErrorCode {
//   ...
//   kServerError
// };
// class FooErrorSpace : public util::ErrorSpace {
//  public:
//   static const FooErrorSpace* Get();
//   /* Rest of the error space definition... */
// };
//
// using ::testing::HasSubstr;
// using ::testing::MatchesRegex;
// using ::testing::Ne;
// using ::testing::status::StatusIs;
// using ::testing::_;
// absl::StatusOr<string> GetName(int id);
//
// // The error space must be FooErrorSpace; the status code must be
// // kServerError; the error message can be anything.
// EXPECT_THAT(GetName(42),
//             StatusInErrorSpaceIs(FooErrorSpace::Get(), kServerError));
// // The error space must be FooErrorSpace; the status code can be
// // anything; the error message must match the regex.
// EXPECT_THAT(GetName(43),
//             StatusInErrorSpaceIs(FooErrorSpace::Get(), _,
//                                  MatchesRegex("server.*time-out")));
//
// // The error space must be FooErrorSpace; the status code
// // should not be kServerError; the error message can be
// // anything with "client" in it.
// EXPECT_CALL(mock_env, HandleStatus(
//     StatusInErrorSpaceIs(FooErrorSpace::Get(), Ne(kServerError),
//                          HasSubstr("client"))));
// ```
template <typename ErrorSpaceMatcherT, typename StatusCodeMatcherT,
          typename StatusMessageMatcherT>
auto StatusInErrorSpaceIs(ErrorSpaceMatcherT&& space_matcher,
                          StatusCodeMatcherT&& code_matcher,
                          StatusMessageMatcherT&& message_matcher) {
  return status_internal::StatusInErrorSpaceIsMatcher(
      std::forward<ErrorSpaceMatcherT>(space_matcher),
      std::forward<StatusCodeMatcherT>(code_matcher),
      std::forward<StatusMessageMatcherT>(message_matcher));
}

// This gMock matcher matches a Status or StatusOr<T> or StatusProto value if
// all of the following are true:
//
//   - the status' error_space() matches error_space_matcher,
//   - the status' error_code() matches status_code_matcher, and
//
// This is equivalent to the above, but matches any message, i.e.
// StatusInErrorSpace(space_matcher, code_matcher, _);
template <typename ErrorSpaceMatcherT, typename StatusCodeMatcherT>
auto StatusInErrorSpaceIs(ErrorSpaceMatcherT&& space_matcher,
                          StatusCodeMatcherT&& code_matcher) {
  return status_internal::StatusInErrorSpaceIsMatcher(
      std::forward<ErrorSpaceMatcherT>(space_matcher),
      std::forward<StatusCodeMatcherT>(code_matcher), ::testing::_);
}

// This gMock matcher matches a Status or StatusOr<T> or StatusProto value if
// the underlying status matches the provided error space.  See above for how
// to match an error space.
template <typename ErrorSpaceMatcherT>
auto ErrorSpaceIs(ErrorSpaceMatcherT&& space_matcher) {
  return status_internal::StatusInErrorSpaceIsMatcher(
      std::forward<ErrorSpaceMatcherT>(space_matcher), ::testing::_,
      ::testing::_);
}

// This gMock matcher matches a Status or StatusOr<T> or StatusProto value if
// both of the following are true:
//
//   - the status' CanonicalCode() matches canonical_code_matcher and
//   - the status' error_message() matches error_message_matcher.
//
// This differs from the absl::StatusIs() matchers in that it will match a
// status with any error_space(), as long as that error space maps the
// error_code() to the expected canonical code.
//
// This can be useful to match a status without being overly sensitive to the
// specific error space being used.  For example, a test may not care to
// distinguish between a canonical vs rpc vs custom deadline exceeded error.
template <typename StatusCodeMatcherT, typename StatusMessageMatcherT>
status_internal::CanonicalStatusIsMatcher CanonicalStatusIs(
    StatusCodeMatcherT&& code_matcher,
    StatusMessageMatcherT&& message_matcher) {
  return status_internal::CanonicalStatusIsMatcher(
      std::forward<StatusCodeMatcherT>(code_matcher),
      std::forward<StatusMessageMatcherT>(message_matcher));
}

// Returns a gMock matcher that matches a Status or StatusOr<> whose canonical
// status code (i.e., Status::CanonicalCode) matches code_matcher.
template <typename StatusCodeMatcherT>
status_internal::CanonicalStatusIsMatcher CanonicalStatusIs(
    StatusCodeMatcherT&& code_matcher) {
  return CanonicalStatusIs(std::forward<StatusCodeMatcherT>(code_matcher),
                           ::testing::_);
}

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TASK_STATUS_MATCHERS_H_
