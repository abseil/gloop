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

#include "gloop/util/task/status_matchers.h"

#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gloop/util/status/error_space.h"
#include "gloop/util/status/status.h"
#include "gloop/util/status/status.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"

namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::_;
using ::testing::Eq;

TEST(StatusMatchersTest, AssertOkAndAssign) {
  absl::StatusOr<int> status_or = 42;
  ABSL_ASSERT_OK_AND_ASSIGN(int result, status_or);
  EXPECT_THAT(result, Eq(42));
}

TEST(StatusMatchersTest, AssertOkAndAssignFail) {
  EXPECT_FATAL_FAILURE(
      {
        absl::StatusOr<int> status_or = absl::UnknownError("inconnu");
        ABSL_ASSERT_OK_AND_ASSIGN(int b, status_or);
        (void)b;
      },
      "inconnu");
}

// Canonical Status tests.
TEST(StatusMatcherTest, ErrorSpaceIsCanonical) {
  absl::Status status = absl::OkStatus();
  EXPECT_THAT(status, util::StatusInErrorSpaceIs(_, _, _));
  EXPECT_THAT(status, util::ErrorSpaceIs(util::CanonicalErrorSpace()));
  EXPECT_THAT(status,
              util::StatusInErrorSpaceIs(util::CanonicalErrorSpace(), 0));
  EXPECT_THAT(status,
              util::StatusInErrorSpaceIs(util::CanonicalErrorSpace(), 0, _));
}

TEST(StatusMatcherTest, ErrorSpaceIsCanonicalError) {
  absl::Status status = absl::UnknownError("inconnu");
  EXPECT_THAT(status, util::ErrorSpaceIs(util::CanonicalErrorSpace()));
  EXPECT_THAT(status, util::StatusInErrorSpaceIs(
                          util::CanonicalErrorSpace(),
                          static_cast<int>(absl::StatusCode::kUnknown)));
  EXPECT_THAT(status,
              util::StatusInErrorSpaceIs(
                  util::CanonicalErrorSpace(),
                  static_cast<int>(absl::StatusCode::kUnknown), Eq("inconnu")));
}

TEST(StatusMatcherTest, CanonicalStatusIsCanonical) {
  absl::Status status = absl::OkStatus();
  EXPECT_THAT(status, util::CanonicalStatusIs(absl::StatusCode::kOk));
}

TEST(StatusMatcherTest, CanonicalStatusIsCanonicalError) {
  absl::Status status = absl::InvalidArgumentError("ungueltig");
  EXPECT_THAT(status,
              util::CanonicalStatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(status, util::CanonicalStatusIs(
                          absl::StatusCode::kInvalidArgument, "ungueltig"));
}

// Canonical StatusOr tests.
TEST(StatusMatcherTest, ErrorSpaceIsCanonicalOr) {
  absl::StatusOr<int> status = 42;
  EXPECT_THAT(status, util::StatusInErrorSpaceIs(_, _, _));
  EXPECT_THAT(status, util::ErrorSpaceIs(util::CanonicalErrorSpace()));
  EXPECT_THAT(status,
              util::StatusInErrorSpaceIs(util::CanonicalErrorSpace(), 0));
  EXPECT_THAT(status,
              util::StatusInErrorSpaceIs(util::CanonicalErrorSpace(), 0, _));
}

TEST(StatusMatcherTest, ErrorSpaceIsCanonicalErrorOr) {
  absl::StatusOr<std::string> status = absl::UnknownError("inconnu");
  EXPECT_THAT(status, util::ErrorSpaceIs(util::CanonicalErrorSpace()));
  EXPECT_THAT(status, util::StatusInErrorSpaceIs(
                          util::CanonicalErrorSpace(),
                          static_cast<int>(absl::StatusCode::kUnknown)));
  EXPECT_THAT(status,
              util::StatusInErrorSpaceIs(
                  util::CanonicalErrorSpace(),
                  static_cast<int>(absl::StatusCode::kUnknown), Eq("inconnu")));
}

TEST(StatusMatcherTest, CanonicalStatusOrIsCanonical) {
  absl::StatusOr<std::string> status = "OK !";
  EXPECT_THAT(status, util::CanonicalStatusIs(absl::StatusCode::kOk));
}

TEST(StatusMatcherTest, CanonicalStatusOrIsCanonicalError) {
  absl::StatusOr<int> status = absl::InvalidArgumentError("ungueltig");
  EXPECT_THAT(status,
              util::CanonicalStatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(status, util::CanonicalStatusIs(
                          absl::StatusCode::kInvalidArgument, "ungueltig"));
}

// Implement custom error space for tests.
enum class TestError : int {
  kAbort = 1,
  kCancel = 4,
};

struct TestErrorSpace : util::ErrorSpaceImpl<TestErrorSpace> {
  static absl::string_view space_name() { return "test-error-space"; }

  static std::string code_to_string(int code) {
    switch (static_cast<TestError>(code)) {
      case TestError::kAbort:
        return "test";
      case TestError::kCancel:
        return "other";
      default:
        ADD_FAILURE() << "Unrecognized TestError code: " << code;
        return "fail";
    }
  }

  static absl::StatusCode canonical_code(int code) {
    switch (static_cast<TestError>(code)) {
      case TestError::kAbort:
        return absl::StatusCode::kAborted;
      case TestError::kCancel:
        return absl::StatusCode::kCancelled;
      default:
        ADD_FAILURE() << "Unrecognized TestError code: " << code;
        return absl::StatusCode::kUnknown;
    }
  }
};

inline const util::ErrorSpace* GetErrorSpace(
    util::ErrorSpaceAdlTag<TestError>) {
  return TestErrorSpace::Get();
}

// Example of suggested matcher implementation per the docs.
template <typename StatusMatcherT, typename MessageMatcherT>
auto TestStatusIs(StatusMatcherT status_matcher,
                  MessageMatcherT message_matcher) {
  return util::StatusInErrorSpaceIs(
      TestErrorSpace::Get(), std::forward<StatusMatcherT>(status_matcher),
      std::forward<MessageMatcherT>(message_matcher));
}

template <typename StatusMatcherT>
auto TestStatusIs(StatusMatcherT status_matcher) {
  return TestStatusIs(std::forward<StatusMatcherT>(status_matcher),
                      ::testing::_);
}

// Custom error space Status tests.
TEST(StatusMatcherTest, ErrorSpaceIsCanonicalFailure) {
  absl::StatusOr<int> status_or = 123;
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status_or, util::ErrorSpaceIs(TestErrorSpace::Get())),
      "error space");
  EXPECT_NONFATAL_FAILURE(EXPECT_THAT(status_or, util::StatusInErrorSpaceIs(
                                                     TestErrorSpace::Get(), _)),
                          "error space");
  EXPECT_NONFATAL_FAILURE(EXPECT_THAT(status_or, TestStatusIs(_)),
                          "error space");
}

TEST(StatusMatcherTest, ErrorSpaceIsCustom) {
  absl::Status status = util::MakeStatus(TestErrorSpace::Get(), 0, "");
  EXPECT_THAT(status, util::StatusInErrorSpaceIs(_, _, _));
  EXPECT_THAT(status, util::ErrorSpaceIs(util::CanonicalErrorSpace()));
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status, util::ErrorSpaceIs(TestErrorSpace::Get())),
      "error space");
  EXPECT_THAT(status,
              util::StatusInErrorSpaceIs(util::CanonicalErrorSpace(), 0));
  EXPECT_THAT(status,
              util::StatusInErrorSpaceIs(util::CanonicalErrorSpace(), 0, _));
}

TEST(StatusMatcherTest, ErrorSpaceIsCustomError) {
  absl::Status status = util::MakeStatus(TestError::kCancel, "annule");
  EXPECT_THAT(status, util::ErrorSpaceIs(TestErrorSpace::Get()));
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status, util::ErrorSpaceIs(util::CanonicalErrorSpace())),
      "error space");
  EXPECT_THAT(status, util::StatusInErrorSpaceIs(TestErrorSpace::Get(), _, _));
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status, util::StatusInErrorSpaceIs(
                              util::CanonicalErrorSpace(), _, _)),
      "error space");
}

TEST(StatusMatcherTest, ErrorSpaceIsCustomErrorSuccess) {
  absl::Status status = util::MakeStatus(TestError::kCancel, "annule");
  EXPECT_THAT(status, TestStatusIs(TestError::kCancel));
  EXPECT_THAT(status, util::StatusInErrorSpaceIs(TestErrorSpace::Get(),
                                                 TestError::kCancel));
  EXPECT_THAT(status,
              util::StatusInErrorSpaceIs(TestErrorSpace::Get(),
                                         static_cast<int>(TestError::kCancel)));
  EXPECT_THAT(status, TestStatusIs(TestError::kCancel, Eq("annule")));
  EXPECT_THAT(status,
              util::StatusInErrorSpaceIs(TestErrorSpace::Get(),
                                         TestError::kCancel, Eq("annule")));
  EXPECT_THAT(status, util::StatusInErrorSpaceIs(
                          TestErrorSpace::Get(),
                          static_cast<int>(TestError::kCancel), Eq("annule")));
}

TEST(StatusMatcherTest, ErrorSpaceIsCustomErrorFailure) {
  absl::Status status = util::MakeStatus(TestError::kAbort, "abbrechen");
  EXPECT_NONFATAL_FAILURE(EXPECT_THAT(status, TestStatusIs(TestError::kCancel)),
                          "ABORT");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status, util::StatusInErrorSpaceIs(TestErrorSpace::Get(),
                                                     TestError::kCancel)),
      "ABORT");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status, util::StatusInErrorSpaceIs(
                              TestErrorSpace::Get(),
                              static_cast<int>(TestError::kCancel))),
      "ABORT");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status, TestStatusIs(TestError::kCancel, _)), "ABORT");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status, util::StatusInErrorSpaceIs(TestErrorSpace::Get(),
                                                     TestError::kCancel, _)),
      "ABORT");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status, util::StatusInErrorSpaceIs(
                              TestErrorSpace::Get(),
                              static_cast<int>(TestError::kCancel), _)),
      "ABORT");
}

TEST(StatusMatcherTest, ErrorSpaceIsCustomErrorMessageFailure) {
  absl::Status status = util::MakeStatus(TestError::kCancel, "loeschen");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status, TestStatusIs(TestError::kCancel, Eq("annule"))),
      "loeschen");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status,
                  util::StatusInErrorSpaceIs(TestErrorSpace::Get(),
                                             TestError::kCancel, Eq("annule"))),
      "loeschen");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status,
                  util::StatusInErrorSpaceIs(
                      TestErrorSpace::Get(),
                      static_cast<int>(TestError::kCancel), Eq("annule"))),
      "loeschen");
}

TEST(StatusMatcherTest, CanonicalStatusIsCustom) {
  absl::Status status = util::MakeStatus(TestErrorSpace::Get(), 0, "");
  EXPECT_THAT(status, util::CanonicalStatusIs(absl::StatusCode::kOk));
}

TEST(StatusMatcherTest, CanonicalStatusIsCustomError) {
  absl::Status status = util::MakeStatus(TestError::kCancel, "annule");
  EXPECT_THAT(status, util::CanonicalStatusIs(absl::StatusCode::kCancelled));
  EXPECT_THAT(status,
              util::CanonicalStatusIs(absl::StatusCode::kCancelled, "annule"));
  status = util::MakeStatus(TestError::kAbort, "stop");
  EXPECT_THAT(status, util::CanonicalStatusIs(absl::StatusCode::kAborted));
}

// Custom error space StatusOr tests.
TEST(StatusMatcherTest, ErrorSpaceIsCanonicalOrFailure) {
  absl::StatusOr<int> status_or = 123;
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status_or, util::ErrorSpaceIs(TestErrorSpace::Get())),
      "error space");
  EXPECT_NONFATAL_FAILURE(EXPECT_THAT(status_or, util::StatusInErrorSpaceIs(
                                                     TestErrorSpace::Get(), _)),
                          "error space");
  EXPECT_NONFATAL_FAILURE(EXPECT_THAT(status_or, TestStatusIs(_)),
                          "error space");
}

// It is impossible to create an Ok absl::StatusOr object with a non-canonical
// error space.

TEST(StatusMatcherTest, ErrorSpaceIsCustomErrorOr) {
  absl::StatusOr<std::string> status_or =
      util::MakeStatus(TestError::kCancel, "annule");
  EXPECT_THAT(status_or, util::ErrorSpaceIs(TestErrorSpace::Get()));
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status_or, util::ErrorSpaceIs(util::CanonicalErrorSpace())),
      "error space");
  EXPECT_THAT(status_or, util::StatusInErrorSpaceIs(TestErrorSpace::Get(), _));
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status_or,
                  util::StatusInErrorSpaceIs(util::CanonicalErrorSpace(), _)),
      "error space");
}

TEST(StatusMatcherTest, ErrorSpaceIsCustomErrorOrSuccess) {
  absl::StatusOr<std::string> status_or =
      util::MakeStatus(TestError::kCancel, "annule");
  EXPECT_THAT(status_or, TestStatusIs(TestError::kCancel));
  EXPECT_THAT(status_or, util::StatusInErrorSpaceIs(TestErrorSpace::Get(),
                                                    TestError::kCancel));
  EXPECT_THAT(status_or,
              util::StatusInErrorSpaceIs(TestErrorSpace::Get(),
                                         static_cast<int>(TestError::kCancel)));
  EXPECT_THAT(status_or, TestStatusIs(TestError::kCancel, Eq("annule")));
  EXPECT_THAT(status_or,
              util::StatusInErrorSpaceIs(TestErrorSpace::Get(),
                                         TestError::kCancel, Eq("annule")));
  EXPECT_THAT(status_or,
              util::StatusInErrorSpaceIs(TestErrorSpace::Get(),
                                         static_cast<int>(TestError::kCancel),
                                         Eq("annule")));
}

TEST(StatusMatcherTest, ErrorSpaceIsCustomErrorOrFailure) {
  absl::StatusOr<std::string> status_or =
      util::MakeStatus(TestError::kAbort, "abbrechen");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status_or, TestStatusIs(TestError::kCancel)), "ABORT");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status_or, util::StatusInErrorSpaceIs(TestErrorSpace::Get(),
                                                        TestError::kCancel)),
      "ABORT");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status_or, util::StatusInErrorSpaceIs(
                                 TestErrorSpace::Get(),
                                 static_cast<int>(TestError::kCancel))),
      "ABORT");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status_or, TestStatusIs(TestError::kCancel, _)), "ABORT");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status_or, util::StatusInErrorSpaceIs(TestErrorSpace::Get(),
                                                        TestError::kCancel, _)),
      "ABORT");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status_or, util::StatusInErrorSpaceIs(
                                 TestErrorSpace::Get(),
                                 static_cast<int>(TestError::kCancel), _)),
      "ABORT");
}

TEST(StatusMatcherTest, ErrorSpaceIsCustomErrorOrMessageFailure) {
  absl::StatusOr<std::string> status_or =
      util::MakeStatus(TestError::kCancel, "loeschen");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status_or, TestStatusIs(TestError::kCancel, Eq("annule"))),
      "loeschen");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status_or,
                  util::StatusInErrorSpaceIs(TestErrorSpace::Get(),
                                             TestError::kCancel, Eq("annule"))),
      "loeschen");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(status_or,
                  util::StatusInErrorSpaceIs(
                      TestErrorSpace::Get(),
                      static_cast<int>(TestError::kCancel), Eq("annule"))),
      "loeschen");
}

TEST(StatusMatcherTest, StringMatcher) {
  testing::Matcher<std::string> message_matcher = Eq("message");
  EXPECT_THAT(absl::UnknownError("message"),
              util::StatusInErrorSpaceIs(_, _, message_matcher));
}

TEST(StatusMatcherTest, ConstStringRefMatcher) {
  testing::Matcher<const std::string&> message_matcher = Eq("message");
  EXPECT_THAT(absl::UnknownError("message"),
              util::StatusInErrorSpaceIs(_, _, message_matcher));
}

TEST(StatusMatcherTest, CanonicalStatusOrIsCustom) {
  absl::StatusOr<std::string> status_or = "OK !";
  EXPECT_THAT(status_or, util::CanonicalStatusIs(absl::StatusCode::kOk));
}

TEST(StatusMatcherTest, CanonicalStatusOrIsCustomError) {
  absl::StatusOr<int> status_or =
      util::MakeStatus(TestError::kCancel, "annule");
  EXPECT_THAT(status_or, util::CanonicalStatusIs(absl::StatusCode::kCancelled));
  EXPECT_THAT(status_or,
              util::CanonicalStatusIs(absl::StatusCode::kCancelled, "annule"));
  status_or = util::MakeStatus(TestError::kAbort, "stop");
  EXPECT_THAT(status_or, util::CanonicalStatusIs(absl::StatusCode::kAborted));
}

TEST(StatusMatcherTest, CanonicalStatusOrIsString) {
  testing::Matcher<std::string> message_matcher = Eq("message");
  EXPECT_THAT(util::MakeStatus(TestError::kCancel, "message"),
              util::CanonicalStatusIs(_, message_matcher));
}

TEST(StatusMatcherTest, CanonicalStatusOrIsConstStringRef) {
  testing::Matcher<const std::string&> message_matcher = Eq("message");
  EXPECT_THAT(util::MakeStatus(TestError::kCancel, "message"),
              util::CanonicalStatusIs(_, message_matcher));
}

// Status proto tests.
TEST(StatusProtoTest, IsOk) {
  util::StatusProto proto;
  proto.set_code(0);
  EXPECT_THAT(proto, IsOk());

  util::SaveStatusToProto(absl::UnknownError("inconnu"), &proto);
  EXPECT_NONFATAL_FAILURE(EXPECT_THAT(proto, IsOk()), "inconnu");
}

TEST(StatusProtoTest, StatusIs) {
  util::StatusProto proto;
  proto.set_code(0);
  EXPECT_THAT(proto, StatusIs(absl::StatusCode::kOk));

  util::SaveStatusToProto(absl::UnknownError("inconnu"), &proto);
  EXPECT_THAT(proto, StatusIs(absl::StatusCode::kUnknown));
  EXPECT_THAT(proto, StatusIs(absl::StatusCode::kUnknown, "inconnu"));
}

TEST(StatusProtoTest, MacroSuccess) {
  util::StatusProto proto;
  proto.set_code(0);
  EXPECT_THAT(proto, IsOk());
}

TEST(StatusProtoTest, MacroAssertFailure) {
  util::StatusProto proto;
  util::SaveStatusToProto(absl::UnknownError("inconnu"), &proto);
  EXPECT_NONFATAL_FAILURE(EXPECT_THAT(proto, IsOk()), "inconnu");
}

}  // namespace
