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

#include "gloop/util/task/status_matchers_internal.h"

#include <ostream>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/util/status/status.h"
#include "gmock/gmock.h"

namespace util::status_internal {

::testing::Matcher<absl::string_view> ConvertStringMatcher(
    ::testing::Matcher<const std::string&> matcher) {
  return ::testing::ResultOf(
      [](const absl::string_view view) -> std::string {
        return std::string(view);
      },
      std::move(matcher));
}

void StatusInErrorSpaceIsMatcherCommonImpl::DescribeTo(std::ostream* os) const {
  *os << "is in an error space that ";
  space_matcher_.DescribeTo(os);
  *os << ", has a status code that ";
  code_matcher_.DescribeTo(os);
  *os << ", and has an error message that ";
  message_matcher_.DescribeTo(os);
}

void StatusInErrorSpaceIsMatcherCommonImpl::DescribeNegationTo(
    std::ostream* os) const {
  *os << "is in an error space that ";
  space_matcher_.DescribeNegationTo(os);
  *os << ", or has a status code that ";
  code_matcher_.DescribeNegationTo(os);
  *os << ", or has an error message that ";
  message_matcher_.DescribeNegationTo(os);
}

bool StatusInErrorSpaceIsMatcherCommonImpl::MatchAndExplain(
    const ::absl::Status& status,
    ::testing::MatchResultListener* result_listener) const {
  ::testing::StringMatchResultListener inner_listener;
  const ::util::ErrorSpace* error_space = ::util::RetrieveErrorSpace(status);
  if (!space_matcher_.MatchAndExplain(error_space, &inner_listener)) {
    *result_listener << (inner_listener.str().empty()
                             ? ::absl::StrCat("whose error space is wrongly '",
                                              error_space->SpaceName(), "'")
                             : "which is in an error space " +
                                   inner_listener.str());
    return false;
  }

  inner_listener.Clear();
  if (!code_matcher_.MatchAndExplain(::util::RetrieveErrorCode(status),
                                     &inner_listener)) {
    *result_listener << (inner_listener.str().empty()
                             ? "whose status code is wrong"
                             : "which has a status code " +
                                   inner_listener.str());
    return false;
  }

  if (!message_matcher_.Matches(status.message())) {
    *result_listener << "whose error message is wrong";
    return false;
  }

  return true;
}

void CanonicalStatusIsMatcherCommonImpl::DescribeTo(std::ostream* os) const {
  *os << "has a canonical status code that ";
  code_matcher_.DescribeTo(os);
  *os << " and has an error message that ";
  message_matcher_.DescribeTo(os);
}

void CanonicalStatusIsMatcherCommonImpl::DescribeNegationTo(
    std::ostream* os) const {
  *os << "has a canonical status code that ";
  code_matcher_.DescribeNegationTo(os);
  *os << " or has an error message that ";
  message_matcher_.DescribeNegationTo(os);
}

bool CanonicalStatusIsMatcherCommonImpl::MatchAndExplain(
    const ::absl::Status& status,
    ::testing::MatchResultListener* result_listener) const {
  ::testing::StringMatchResultListener inner_listener;
  if (!code_matcher_.MatchAndExplain(
          static_cast<::util::error::Code>(status.code()), &inner_listener)) {
    *result_listener << (inner_listener.str().empty()
                             ? "whose canonical status code is wrong"
                             : "which has a canonical status code " +
                                   inner_listener.str());
    return false;
  }

  if (!message_matcher_.Matches(status.message())) {
    *result_listener << "whose error message is wrong";
    return false;
  }

  return true;
}

void AddFatalFailure(absl::string_view expression,
                     const absl::StatusBuilder& builder) {
  GTEST_MESSAGE_AT_(
      builder.source_location().file_name(), builder.source_location().line(),
      ::absl::StrCat(expression, " returned error: ",
                     ::absl::Status(builder).ToString(
                         absl::StatusToStringMode::kWithEverything))
          .c_str(),
      ::testing::TestPartResult::kFatalFailure);
}

}  // namespace util::status_internal
