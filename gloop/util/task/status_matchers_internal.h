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

// IWYU pragma: private, include "util/task/status_matchers.h"
// IWYU pragma: friend util/task/status_matchers.h

#ifndef THIRD_PARTY_GLOOP_UTIL_TASK_STATUS_MATCHERS_INTERNAL_H_
#define THIRD_PARTY_GLOOP_UTIL_TASK_STATUS_MATCHERS_INTERNAL_H_

#include <ostream>  // NOLINT
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/string_view.h"
#include "gloop/util/status/codes.pb.h"
#include "gloop/util/status/error_space.h"
#include "gloop/util/status/status.h"
#include "gloop/util/status/status.pb.h"
#include "gloop/util/status/status_builder.h"
#include "gmock/gmock.h"

namespace util {

// Enable Abseil status matchers for StatusProto.
#if !defined(PORTABLE_STATUS)
inline absl::Status GetStatus(const ::util::StatusProto& status) {
  return ::util::MakeStatusFromProto(status);
}
#endif

namespace status_internal {

using ::absl_testing::status_internal::GetStatus;

// Casts a `std::string` matcher to an `absl::string_view`.  This is necessary
// because there's no implicit cast from string_view to string.
::testing::Matcher<absl::string_view> ConvertStringMatcher(
    ::testing::Matcher<const std::string&> matcher);

////////////////////////////////////////////////////////////
// Implementation of StatusInErrorSpaceIs().

// An internal matcher for matching a const util::ErrorSpace* against
// an expected const util::ErrorSpace*.  It describes the expected
// error space by the symbolic name.
class ErrorSpaceIs {
 public:
  using is_gtest_matcher = void;

  explicit ErrorSpaceIs(const ::util::ErrorSpace* error_space)
      : expected_(error_space) {}

  bool MatchAndExplain(const ::util::ErrorSpace* actual,
                       std::ostream* os) const {
    if (expected_ != actual) {
      if (os) {
        *os << "where the actual error space is ";
        Describe(actual, *os);
      }
      return false;
    }
    return true;
  }

  void DescribeTo(std::ostream* os) const {
    *os << "is ";
    Describe(expected_, *os);
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "isn't ";
    Describe(expected_, *os);
  }

 private:
  static void Describe(const ::util::ErrorSpace* error_space,
                       std::ostream& os) {
    if (error_space) {
      os << "<" << error_space->SpaceName() << ">";
    } else {
      os << "null";
    }
  }
  const ErrorSpace* expected_;
};

// ToErrorSpaceMatcher(m) converts m to a Matcher<const util::ErrorSpace*>.
inline ::testing::Matcher<const ::util::ErrorSpace*> ToErrorSpaceMatcher(
    const ::util::ErrorSpace* space) {
  // Ensure that the expected error space is described by its symbolic name.
  return ErrorSpaceIs(space);
}
inline ::testing::Matcher<const ::util::ErrorSpace*> ToErrorSpaceMatcher(
    const ::testing::Matcher<const ::util::ErrorSpace*>& space_matcher) {
  return space_matcher;
}

// `StatusCode` is implicitly convertible from `int`, `absl::StatusCode`, and
// any enum that is associated with an error space, and explicitly convertible
// to these types as well.
//
// We need this class because absl::StatusCode (as a scoped enum) is not
// implicitly convertible to int. In order to handle use cases like
//   StatusIs(Anyof(absl::StatusCode::kUnknown, absl::StatusCode::kCancelled))
// which uses polymorphic matchers, we need to unify the interfaces into
// Matcher<StatusCode>.
class StatusCode {
 public:
  /*implicit*/ StatusCode(int code) : code_(code), space_(nullptr) {}  // NOLINT
  /*implicit*/ StatusCode(absl::StatusCode code)                       // NOLINT
      : code_(static_cast<int>(code)), space_(::util::CanonicalErrorSpace()) {}
  template <typename T, typename = typename std::enable_if<
                            ::util::EnumHasErrorSpace<T>::value>::type>
  /*implicit*/ StatusCode(T code)  // NOLINT
      : code_(static_cast<int>(code)),
        space_(::util::GetErrorSpaceForEnum(code)) {}

  explicit operator int() const { return code_; }

  friend void PrintTo(const StatusCode& code, std::ostream* os) {
    if (code.space_ != nullptr) {
      *os << code.space_->String(code.code_);
    } else {
      *os << code.code_;
    }
  }

 private:
  int code_;
  const ::util::ErrorSpace* space_;
};

// Relational operators to handle matchers like Eq, Lt, etc..
inline bool operator==(const StatusCode& lhs, const StatusCode& rhs) {
  return static_cast<int>(lhs) == static_cast<int>(rhs);
}

// StatusIs() is a polymorphic matcher.  This class is the common
// implementation of it shared by all types T where StatusIs() can be
// used as a Matcher<T>.
class StatusInErrorSpaceIsMatcherCommonImpl {
 public:
  StatusInErrorSpaceIsMatcherCommonImpl(
      ::testing::Matcher<const ::util::ErrorSpace*> space_matcher,
      ::testing::Matcher<StatusCode> code_matcher,
      ::testing::Matcher<absl::string_view> message_matcher)
      : space_matcher_(std::move(space_matcher)),
        code_matcher_(std::move(code_matcher)),
        message_matcher_(std::move(message_matcher)) {}

  void DescribeTo(std::ostream* os) const;

  void DescribeNegationTo(std::ostream* os) const;

  bool MatchAndExplain(const absl::Status& status,
                       ::testing::MatchResultListener* result_listener) const;

 private:
  const ::testing::Matcher<const ::util::ErrorSpace*> space_matcher_;
  const ::testing::Matcher<StatusCode> code_matcher_;
  const ::testing::Matcher<absl::string_view> message_matcher_;
};

// Monomorphic implementation of matcher StatusIs() for a given type
// T.  T can be Status, StatusOr<>, or a reference to either of them.
template <typename T>
class MonoStatusInErrorSpaceIsMatcherImpl
    : public ::testing::MatcherInterface<T> {
 public:
  using is_gtest_matcher = void;

  explicit MonoStatusInErrorSpaceIsMatcherImpl(
      StatusInErrorSpaceIsMatcherCommonImpl common_impl)
      : common_impl_(std::move(common_impl)) {}

  void DescribeTo(std::ostream* os) const final { common_impl_.DescribeTo(os); }

  void DescribeNegationTo(std::ostream* os) const final {
    common_impl_.DescribeNegationTo(os);
  }

  bool MatchAndExplain(
      T actual_value,
      ::testing::MatchResultListener* result_listener) const override {
    return common_impl_.MatchAndExplain(GetStatus(actual_value),
                                        result_listener);
  }

 private:
  StatusInErrorSpaceIsMatcherCommonImpl common_impl_;
};

class StatusInErrorSpaceIsMatcher {
 public:
  template <typename ErrorSpaceMatcherT, typename StatusCodeMatcherT,
            typename StatusMessageMatcherT>
  StatusInErrorSpaceIsMatcher(ErrorSpaceMatcherT&& space_matcher,
                              StatusCodeMatcherT&& code_matcher,
                              StatusMessageMatcherT&& message_matcher)
      : common_impl_(
            ToErrorSpaceMatcher(
                std::forward<ErrorSpaceMatcherT>(space_matcher)),
            ::testing::MatcherCast<StatusCode>(
                std::forward<StatusCodeMatcherT>(code_matcher)),
            ::testing::MatcherCast<absl::string_view>(
                std::forward<StatusMessageMatcherT>(message_matcher))) {}

  // TODO: Shim to support existing, explicit uses of `std::string`
  // matchers.  Remove once all of these (few) locations are migrated to use
  // `string_view` matchers.
  template <typename ErrorSpaceMatcherT, typename StatusCodeMatcherT>
  StatusInErrorSpaceIsMatcher(
      ErrorSpaceMatcherT&& space_matcher, StatusCodeMatcherT&& code_matcher,
      ::testing::Matcher<const std::string&> message_matcher)
      : common_impl_(ToErrorSpaceMatcher(
                         std::forward<ErrorSpaceMatcherT>(space_matcher)),
                     ::testing::MatcherCast<StatusCode>(
                         std::forward<StatusCodeMatcherT>(code_matcher)),
                     ConvertStringMatcher(std::move(message_matcher))) {}

  // Converts this polymorphic matcher to a monomorphic matcher of the
  // given type.  T can be StatusOr<>, Status, or a reference to
  // either of them.
  template <typename T>
  /*implicit*/ operator ::testing::Matcher<T>() const {  // NOLINT
    return ::testing::MatcherCast<T>(
        MonoStatusInErrorSpaceIsMatcherImpl<const T&>(common_impl_));
  }

 private:
  const StatusInErrorSpaceIsMatcherCommonImpl common_impl_;
};

// CanonicalStatusIs() is a polymorphic matcher.  This class is the common
// implementation of it shared by all types T where CanonicalStatusIs() can be
// used as a Matcher<T>.
class CanonicalStatusIsMatcherCommonImpl {
 public:
  CanonicalStatusIsMatcherCommonImpl(
      ::testing::Matcher<StatusCode> code_matcher,
      ::testing::Matcher<absl::string_view> message_matcher)
      : code_matcher_(std::move(code_matcher)),
        message_matcher_(std::move(message_matcher)) {}

  void DescribeTo(std::ostream* os) const;

  void DescribeNegationTo(std::ostream* os) const;

  bool MatchAndExplain(const absl::Status& status,
                       ::testing::MatchResultListener* result_listener) const;

 private:
  const ::testing::Matcher<StatusCode> code_matcher_;
  const ::testing::Matcher<absl::string_view> message_matcher_;
};

// Monomorphic implementation of matcher CanonicalStatusIs() for a given type
// T.  T can be Status, StatusOr<>, or a reference to either of them.
template <typename T>
class MonoCanonicalStatusIsMatcherImpl : public ::testing::MatcherInterface<T> {
 public:
  using is_gtest_matcher = void;

  explicit MonoCanonicalStatusIsMatcherImpl(
      CanonicalStatusIsMatcherCommonImpl common_impl)
      : common_impl_(std::move(common_impl)) {}

  void DescribeTo(std::ostream* os) const final { common_impl_.DescribeTo(os); }

  void DescribeNegationTo(std::ostream* os) const final {
    common_impl_.DescribeNegationTo(os);
  }

  bool MatchAndExplain(
      T actual_value,
      ::testing::MatchResultListener* result_listener) const override {
    return common_impl_.MatchAndExplain(GetStatus(actual_value),
                                        result_listener);
  }

 private:
  CanonicalStatusIsMatcherCommonImpl common_impl_;
};

// Implements CanonicalStatusIs() as a polymorphic matcher.
class CanonicalStatusIsMatcher {
 public:
  template <typename StatusCodeMatcher, typename StatusMessageMatcher>
  CanonicalStatusIsMatcher(StatusCodeMatcher&& code_matcher,
                           StatusMessageMatcher&& message_matcher)
      : common_impl_(::testing::MatcherCast<StatusCode>(
                         std::forward<StatusCodeMatcher>(code_matcher)),
                     ::testing::MatcherCast<absl::string_view>(
                         std::forward<StatusMessageMatcher>(message_matcher))) {
  }

  // TODO: Shim to support existing, explicit uses of `std::string`
  // matchers.  Remove once all of these (few) locations are migrated to use
  // `string_view` matchers.
  template <typename StatusCodeMatcher>
  CanonicalStatusIsMatcher(
      StatusCodeMatcher&& code_matcher,
      ::testing::Matcher<const std::string&> message_matcher)
      : common_impl_(::testing::MatcherCast<StatusCode>(
                         std::forward<StatusCodeMatcher>(code_matcher)),
                     ConvertStringMatcher(std::move(message_matcher))) {}

  // Converts this polymorphic matcher to a monomorphic matcher of the given
  // type.  T can be StatusOr<>, Status, or a reference to either of them.
  template <typename T>
  operator ::testing::Matcher<T>() const {  // NOLINT
    return ::testing::Matcher<T>(
        new MonoCanonicalStatusIsMatcherImpl<const T&>(common_impl_));
  }

 private:
  const CanonicalStatusIsMatcherCommonImpl common_impl_;
};

void AddFatalFailure(absl::string_view expression,
                     const absl::StatusBuilder& builder);

}  // namespace status_internal
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TASK_STATUS_MATCHERS_INTERNAL_H_
