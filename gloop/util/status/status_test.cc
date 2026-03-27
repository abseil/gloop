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

#include "gloop/util/status/status.h"

#include <stdio.h>

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord_test_helpers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/source_location.h"
#include "benchmark/benchmark.h"
#include "gloop/util/status/error_space.h"
#include "gloop/util/status/non_message_set_payload.pb.h"
#include "gloop/util/status/status_internal.h"
#include "gloop/util/status/test_payload.pb.h"
#include "gmock/gmock.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/bridge/message_set.pb.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/wrappers.pb.h"
#include "gtest/gtest.h"

namespace {

using absl_testing::IsOk;
using absl_testing::IsOkAndHolds;
using absl_testing::StatusIs;
using testing::AllOf;
using testing::Eq;
using testing::HasSubstr;
using testing::Not;
using testing::Optional;
using testing::Pair;

MATCHER_P(EqualsProto, proto, "") {
  return proto.DebugString() == arg.DebugString();
}

enum class MyErrorCode {
  kOk = 0,
  kCustomPermissionDenied = 123456,
  kCustomUnavailable = 123457
};

const std::string& ExpectedErrorSpaceName() {
  static const std::string* str = [] {
    // Choose a length large enough to force an allocation for the sake of
    // benchmarks.
    const auto length = std::string().capacity() + 1;
    return new std::string(length, 'x');
  }();
  return *str;
}

class MyErrorSpace : public util::ErrorSpaceImpl<MyErrorSpace> {
 public:
  static absl::string_view space_name() { return ExpectedErrorSpaceName(); }
  static std::string code_to_string(int code) {
    return absl::StrFormat("error(%d)", code);
  }

  static absl::StatusCode canonical_code(int code) {
    switch (code) {
      case static_cast<int>(MyErrorCode::kCustomPermissionDenied):
        return absl::StatusCode::kPermissionDenied;
      case static_cast<int>(MyErrorCode::kCustomUnavailable):
        return absl::StatusCode::kUnavailable;
      default:
        return absl::StatusCode::kUnknown;
    }
  }
};

// Associate the MyErrorCode with MyErrorSpace so Status APIs can find
// it.
inline const ::util::ErrorSpace* GetErrorSpace(
    ::util::ErrorSpaceAdlTag<MyErrorCode>) {
  return MyErrorSpace::Get();
}

#define GET_SOURCE_LOCATION(offset) __builtin_LINE() - offset

}  // namespace

namespace s2 {
enum MyCodes {
  kPermissionDenied = util::error::PERMISSION_DENIED,
};
struct MyErrorSpace2 : util::ErrorSpaceImpl<MyErrorSpace2> {
  static absl::string_view space_name() { return "myerrors2"; }
  static std::string code_to_string(int code) {
    return absl::StrFormat("error(%d)", code);
  }
  static absl::StatusCode canonical_code(int code) {
    switch (code) {
      case MyCodes::kPermissionDenied:
        return absl::StatusCode::kPermissionDenied;
      default:
        return absl::StatusCode::kUnknown;
    }
  }
};
const util::ErrorSpace* GetErrorSpace(util::ErrorSpaceAdlTag<MyCodes>) {
  return MyErrorSpace2::Get();
}
}  // namespace s2

static google::protobuf::bridge::MessageSet MakeTestPayload(
    absl::string_view payload_message) {
  google::protobuf::bridge::MessageSet out;
  util::TestPayload* payload =
      out.MutableExtension(util::TestPayload::message_set_extension);
  payload->set_message(payload_message);
  return out;
}

TEST(MakeTestPayload, Works) {
  EXPECT_THAT(MakeTestPayload("a"), EqualsProto(MakeTestPayload("a")));
  EXPECT_THAT(MakeTestPayload("a"), Not(EqualsProto(MakeTestPayload("b"))));
}

using PayloadsVec = std::vector<std::pair<std::string, absl::Cord>>;

void CheckSourceLocation(
    const absl::Status& status, std::vector<int> lines = {},
    absl::SourceLocation loc = absl::SourceLocation::current()) {
  ASSERT_EQ(status.GetSourceLocations().size(), lines.size())
      << "Size check failed at " << loc.line();
  for (size_t i = 0; i < lines.size(); ++i) {
    EXPECT_EQ(absl::string_view(status.GetSourceLocations()[i].file_name()),
              absl::string_view(loc.file_name()))
        << "File name check failed at " << loc.line();
    EXPECT_EQ(status.GetSourceLocations()[i].line(), lines[i])
        << "Line check failed at " << loc.line();
  }
}

// Check that s has the specified fields.
//
// An empty `payload_message` means the s must not contain a payload,
// otherwise the contents of payload must be equal to that returned by
// `MakeTestPayload(payload_message)`.
//
// Note: most code should not validate Status values this way.  Use
// https://github.com/abseil/abseil-cpp/tree/master/absl/status/status_matchers.h
// instead.
static void CheckStatus(
    const absl::Status& s, const util::ErrorSpace* space, const int error_code,
    const util::error::Code canonical_code, absl::string_view message,
    absl::string_view payload_message, const PayloadsVec& payloads = {},
    std::vector<int> lines = {},
    absl::SourceLocation loc = absl::SourceLocation::current()) {
  SCOPED_TRACE(testing::Message() << "Where s is " << s);
  EXPECT_EQ(error_code, util::RetrieveErrorCode(s));
  EXPECT_EQ(util::ToAbslStatusCode(canonical_code), s.code());
  EXPECT_EQ(canonical_code, s.raw_code());
  EXPECT_EQ(space, util::RetrieveErrorSpace(s));
  EXPECT_TRUE(util::HasErrorSpace(s, space));
  EXPECT_THAT(util::RetrieveErrorSpaceAndCode(s), Pair(space, error_code));
  EXPECT_EQ(message, s.message());

  if (error_code == 0) {
    EXPECT_TRUE(s.ok());
    EXPECT_EQ("OK", s.ToString());
  } else {
    EXPECT_TRUE(!s.ok());
    EXPECT_THAT(s.ToString(),
                HasSubstr(absl::StatusCodeToString(
                    static_cast<absl::StatusCode>(canonical_code))));
    if (space != util::CanonicalErrorSpace()) {
      EXPECT_THAT(s.ToString(), HasSubstr(space->SpaceName()));
      EXPECT_THAT(s.ToString(), HasSubstr(space->String(error_code)));
    }
    EXPECT_THAT(s.ToString(), HasSubstr(message));
    EXPECT_THAT(util::StatusToString(s), HasSubstr(space->SpaceName()));
    EXPECT_THAT(util::StatusToString(s), HasSubstr(space->String(error_code)));
    EXPECT_THAT(util::StatusToString(s), HasSubstr(message));

    EXPECT_THAT(util::ErrorSpaceAndStatusToString(s),
                HasSubstr(space->SpaceName()));
    EXPECT_THAT(util::ErrorSpaceAndStatusToString(s),
                HasSubstr(space->String(error_code)));
  }

  if (payload_message.empty()) {
    EXPECT_FALSE(util::HasPayload(s));
  } else {
    SCOPED_TRACE(testing::Message() << "Expecting payload_message == \""
                                    << payload_message << "\"");
    ASSERT_TRUE(util::HasPayload(s));
    EXPECT_THAT(util::MakePayloadsSet(s),
                EqualsProto(MakeTestPayload(payload_message)));
  }
  for (auto const& p : payloads) {
    EXPECT_THAT(s.GetPayload(p.first), Optional(p.second));
  }

  CheckSourceLocation(s, lines, loc);
}

TEST(ErrorSpace, SpaceName) {
  ASSERT_EQ(std::string("generic"),
            ::util::RetrieveErrorSpace(absl::OkStatus())->SpaceName());
  ASSERT_EQ(ExpectedErrorSpaceName(), MyErrorSpace::Get()->SpaceName());
}

TEST(ErrorSpace, FindKnown) {
  ASSERT_EQ(MyErrorSpace::Get(),
            util::ErrorSpace::Find(ExpectedErrorSpaceName()));
  ASSERT_EQ(s2::MyErrorSpace2::Get(), util::ErrorSpace::Find("myerrors2"));
}

TEST(ErrorSpace, RegistrationWithoutGet) {
  // Make sure nobody ever calls MyErrorSpace3::Get()
  struct MyErrorSpace3 : util::ErrorSpaceImpl<MyErrorSpace3> {
    static absl::string_view space_name() { return "myerrors3"; }
    static std::string code_to_string(int code) { return ""; }
    static absl::StatusCode canonical_code(int code) {
      return absl::StatusCode::kUnknown;
    }
  };
  ASSERT_NE(nullptr, util::ErrorSpace::Find("myerrors3"));
}

TEST(ErrorSpace, FindGeneric) {
  ASSERT_NE(nullptr, util::ErrorSpace::Find("generic"));
}

TEST(ErrorSpace, FindUnknown) {
  ASSERT_EQ(nullptr, util::ErrorSpace::Find("nonexistent_error_space"));
}

TEST(ErrorSpace, GenericCodeNames) {
  const util::ErrorSpace* e = util::CanonicalErrorSpace();
  EXPECT_EQ("OK", e->String(util::error::OK));
  EXPECT_EQ("cancelled", e->String(util::error::CANCELLED));
  EXPECT_EQ("unknown", e->String(util::error::UNKNOWN));
  EXPECT_EQ("aborted", e->String(util::error::ABORTED));
  EXPECT_EQ("1000", e->String(1000));  // Out of range
}

TEST(Status, ConstructDefault) {
  absl::Status status;
  CheckStatus(status, util::CanonicalErrorSpace(), 0, util::error::OK, "", "");
}

TEST(Status, OkStatus) {
  CheckStatus(absl::OkStatus(), util::CanonicalErrorSpace(), 0, util::error::OK,
              "", "");
}

// Test that the many ways of passing an error code of zero always
// produces an OK status.
TEST(Status, ConstructWithZeroCode) {
  EXPECT_EQ(absl::Status(), absl::OkStatus());

  EXPECT_EQ(absl::Status(absl::StatusCode::kOk, "ignored"), absl::OkStatus());
  EXPECT_EQ(util::MakeStatus(MyErrorCode::kOk, "ignored"), absl::OkStatus());

  EXPECT_EQ(util::MakeStatus(util::CanonicalErrorSpace(), 0, "ignored"),
            absl::OkStatus());
  EXPECT_EQ(util::MakeStatus(MyErrorSpace::Get(), 0, "ignored"),
            absl::OkStatus());

  const google::protobuf::bridge::MessageSet payload =
      MakeTestPayload("ignored");
  EXPECT_EQ(
      util::MakeStatus(util::CanonicalErrorSpace(), 0, "ignored", &payload),
      absl::OkStatus());
  EXPECT_EQ(util::MakeStatus(MyErrorSpace::Get(), 0, "ignored", &payload),
            absl::OkStatus());
}

// Test equivalence across the various ways of constructing a Status
// with no message and no payload in the canonical space.  The current
// implementation represents these values differently from others.
TEST(Status, ConstructCanonicalNoMessage) {
  const absl::Status kCancelled = absl::CancelledError();
  CheckStatus(kCancelled, util::CanonicalErrorSpace(), util::error::CANCELLED,
              util::error::CANCELLED, "", "");

  EXPECT_EQ(kCancelled, absl::Status(absl::StatusCode::kCancelled, ""));
  EXPECT_EQ(kCancelled, util::MakeStatus(util::CanonicalErrorSpace(),
                                         util::error::CANCELLED, ""));
  EXPECT_EQ(kCancelled, util::MakeStatus(util::CanonicalErrorSpace(),
                                         util::error::CANCELLED, "", nullptr));
}

// Test equivalence across the various ways of constructing a Status
// with a message, no payload, in the canonical space.  This test is
// in contrast to ConstructCanonical... tests.
TEST(Status, ConstructCanonicalWithMessage) {
  const absl::Status kCancelled(absl::StatusCode::kCancelled, "message");
  CheckStatus(kCancelled, util::CanonicalErrorSpace(), util::error::CANCELLED,
              util::error::CANCELLED, "message", "", {},
              {GET_SOURCE_LOCATION(3)});

  EXPECT_EQ(util::MakeStatus(util::CanonicalErrorSpace(),
                             util::error::CANCELLED, "message"),
            kCancelled);
  EXPECT_EQ(util::MakeStatus(util::CanonicalErrorSpace(),
                             util::error::CANCELLED, "message", nullptr),
            kCancelled);
}

// Test equivalence across the various ways of constructing a Status
// with no message, but a payload, in the canonical space.  This test
// is in contrast to the other ConstructCanonical... tests.
TEST(Status, ConstructCanonicalNoMessageWithPayload) {
  const google::protobuf::bridge::MessageSet payload = MakeTestPayload("foo");
  const absl::Status kCancelled = util::MakeStatus(
      util::CanonicalErrorSpace(), util::error::CANCELLED, "", &payload);
  CheckStatus(kCancelled, util::CanonicalErrorSpace(), util::error::CANCELLED,
              util::error::CANCELLED, "", "foo");
}

// Test equivalence across the various ways of constructing a Status
// with no message, but a payload, in the canonical space.  This test
// is in contrast to the other ConstructCanonical... tests.
TEST(Status, ConstructCanonicalWithMessageWithPayload) {
  const google::protobuf::bridge::MessageSet payload = MakeTestPayload("foo");
  const absl::Status kCancelled = util::MakeStatus(
      util::CanonicalErrorSpace(), util::error::CANCELLED, "message", &payload);
  CheckStatus(kCancelled, util::CanonicalErrorSpace(), util::error::CANCELLED,
              util::error::CANCELLED, "message", "foo", {},
              {GET_SOURCE_LOCATION(4)});
}

TEST(Status, ConstructWithZeroCodeWithPayload) {
  const google::protobuf::bridge::MessageSet payload = MakeTestPayload("foo");
  absl::Status status =
      util::MakeStatus(MyErrorSpace::Get(), 0, "msg", &payload);
  CheckStatus(status, util::CanonicalErrorSpace(), 0, util::error::OK, "", "");
}

TEST(Status, CheckOK) {
  absl::Status status;
  CHECK_OK(status);
  CHECK_OK(status) << "Failed";
  QCHECK_OK(status) << "Failed";
  DCHECK_OK(status) << "Failed";
}

TEST(StatusOr, CheckOK) {
  absl::StatusOr<int> i = 3;
  CHECK_OK(i);
  CHECK_OK(i) << "Failed";
  QCHECK_OK(i) << "Failed";
  DCHECK_OK(i) << "Failed";
}

TEST(StatusDeathTest, CheckOK) {
  absl::Status status;
  status = absl::Status(absl::StatusCode::kCancelled, "Operation Cancelled");
  ASSERT_DEATH_IF_SUPPORTED(CHECK_OK(status), "Check failed.*status");
  ASSERT_DEATH_IF_SUPPORTED(CHECK_OK(status), "Source Location");
  ASSERT_DEATH_IF_SUPPORTED(QCHECK_OK(status), "Check failed.*status");
  ASSERT_DEATH_IF_SUPPORTED(QCHECK_OK(status), "Source Location");
  ASSERT_DEATH_IF_SUPPORTED(QCHECK_OK(status) << "Foo1234", "Check failed");
  ASSERT_DEATH_IF_SUPPORTED(QCHECK_OK(status) << "Foo1234", "Source Location");
  ASSERT_DEATH_IF_SUPPORTED(QCHECK_OK(status) << "Foo1234", "Foo1234");
}

TEST(StatusOrDeathTest, CheckOK) {
  absl::StatusOr<int> bad_i =
      absl::Status(absl::StatusCode::kCancelled, "Operation Cancelled");
  ASSERT_DEATH_IF_SUPPORTED(CHECK_OK(bad_i),
                            "Check failed.*Operation Cancelled");
  ASSERT_DEATH_IF_SUPPORTED(CHECK_OK(bad_i), "Source Location");
  ASSERT_DEATH_IF_SUPPORTED(QCHECK_OK(bad_i),
                            "Check failed.*Operation Cancelled");
  ASSERT_DEATH_IF_SUPPORTED(QCHECK_OK(bad_i), "Source Location");
  ASSERT_DEATH_IF_SUPPORTED(QCHECK_OK(bad_i) << "Foo1234",
                            "Check failed.*Operation Cancelled");
  ASSERT_DEATH_IF_SUPPORTED(QCHECK_OK(bad_i) << "Foo1234", "Source Location");
  ASSERT_DEATH_IF_SUPPORTED(QCHECK_OK(bad_i) << "Foo1234", "Foo1234");
}

TEST(Status, CreateStatusOkNullMessageSet) {
  absl::Status status =
      util::MakeStatus(util::CanonicalErrorSpace(), 0, "", nullptr);
  CheckStatus(status, util::CanonicalErrorSpace(), 0, util::error::OK, "", "");
  CHECK(!::util::HasPayload(status));
}

TEST(Status, CreateStatusNullMessageSet) {
  absl::Status status =
      util::MakeStatus(MyErrorSpace::Get(), 2, "message", nullptr);
  CheckStatus(status, MyErrorSpace::Get(), 2, util::error::UNKNOWN, "message",
              "", {}, {GET_SOURCE_LOCATION(2)});
  CHECK(!::util::HasPayload(status));
}

TEST(Status, Cancelled) {
  ASSERT_THAT(absl::CancelledError().ToString(), HasSubstr("CANCEL"));
  ASSERT_THAT(util::StatusToString(absl::CancelledError()),
              HasSubstr("cancel"));
  ASSERT_THAT(util::ErrorSpaceAndStatusToString(absl::CancelledError()),
              Eq("generic::cancelled"));
}

TEST(Status, Filled) {
  absl::Status status = util::MakeStatus(MyErrorSpace::Get(), 2, "message");
  CheckStatus(status, MyErrorSpace::Get(), 2, util::error::UNKNOWN, "message",
              "", {}, {GET_SOURCE_LOCATION(2)});
}

TEST(Status, FilledNegative) {
  absl::Status status = util::MakeStatus(MyErrorSpace::Get(), -2, "message");
  CheckStatus(status, MyErrorSpace::Get(), -2, util::error::UNKNOWN, "message",
              "", {}, {GET_SOURCE_LOCATION(2)});
}

TEST(Status, FilledWithPayload) {
  const google::protobuf::bridge::MessageSet payload = MakeTestPayload("foo");
  absl::Status status =
      util::MakeStatus(MyErrorSpace::Get(), 2, "message", &payload);
  CheckStatus(status, MyErrorSpace::Get(), 2, util::error::UNKNOWN, "message",
              "foo", {}, {GET_SOURCE_LOCATION(2)});
  ASSERT_TRUE(util::HasPayload(status));
  EXPECT_THAT(util::MakePayloadsSet(status), EqualsProto(payload));
}

TEST(Status, InvalidUtf8Message) {
  // An invalid UTF-8 message generates a WARNING in debug mode.
  // This test does not check anything except exercising the utf-8 check path
  // and ensuring that it does not cause a process crash.
  absl::Status status = util::MakeStatus(MyErrorSpace::Get(), 2, "\x80");
  CheckStatus(status, MyErrorSpace::Get(), 2, util::error::UNKNOWN, "\x80", "",
              {}, {GET_SOURCE_LOCATION(2)});
}

TEST(Status, Set) {
  absl::Status status;
  status = util::MakeStatus(MyErrorSpace::Get(), 2, "message");
  CheckStatus(status, MyErrorSpace::Get(), 2, util::error::UNKNOWN, "message",
              "", {}, {GET_SOURCE_LOCATION(2)});
}

TEST(Status, SetOverlappingMessage) {
  absl::Status status;
  status = util::MakeStatus(MyErrorSpace::Get(), 2, "message");
  CheckStatus(status, MyErrorSpace::Get(), 2, util::error::UNKNOWN, "message",
              "", {}, {GET_SOURCE_LOCATION(2)});

  absl::string_view old_message = status.message();
  status = util::MakeStatus(MyErrorSpace::Get(), 2, old_message);
  CheckStatus(status, MyErrorSpace::Get(), 2, util::error::UNKNOWN, "message",
              "", {}, {GET_SOURCE_LOCATION(2)});

  absl::string_view full_message = status.message();
  absl::string_view part_message = absl::ClippedSubstr(full_message, 1, 3);
  EXPECT_EQ(part_message, "ess");
  status = util::MakeStatus(MyErrorSpace::Get(), 2, part_message);
  CheckStatus(status, MyErrorSpace::Get(), 2, util::error::UNKNOWN, "ess", "",
              {}, {GET_SOURCE_LOCATION(2)});
}

TEST(Status, CreateWithMessageSet) {
  const google::protobuf::bridge::MessageSet payload = MakeTestPayload("foo");
  absl::Status status =
      util::MakeStatus(MyErrorSpace::Get(), 2, "message", &payload);
  CheckStatus(status, MyErrorSpace::Get(), 2, util::error::UNKNOWN, "message",
              "foo", {}, {GET_SOURCE_LOCATION(2)});
}

TEST(Status, ClearWithPayload) {
  const google::protobuf::bridge::MessageSet payload = MakeTestPayload("foo");
  absl::Status status =
      util::MakeStatus(MyErrorSpace::Get(), 2, "message", &payload);
  status = absl::OkStatus();
  CheckStatus(status, util::CanonicalErrorSpace(), 0, util::error::OK, "", "");
}

TEST(Status, Clear) {
  absl::Status status = util::MakeStatus(MyErrorSpace::Get(), 2, "message");
  status = absl::OkStatus();
  CheckStatus(status, util::CanonicalErrorSpace(), 0, util::error::OK, "", "");
}

TEST(Status, Copy) {
  absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 2, "message");
  absl::Status b(a);
  ASSERT_EQ(a, b);
}

TEST(Status, CopyPayload) {
  const google::protobuf::bridge::MessageSet payload = MakeTestPayload("foo");
  absl::Status a =
      util::MakeStatus(MyErrorSpace::Get(), 2, "message", &payload);
  absl::Status b(a);
  int line = GET_SOURCE_LOCATION(2);
  CheckStatus(a, MyErrorSpace::Get(), 2, util::error::UNKNOWN, "message", "foo",
              {}, {line});
  CheckStatus(b, MyErrorSpace::Get(), 2, util::error::UNKNOWN, "message", "foo",
              {}, {line});
  ASSERT_EQ(a, b);
}

TEST(Status, Assign) {
  absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 2, "message");
  absl::Status b;
  b = a;
  ASSERT_EQ(a, b);
}

TEST(Status, Update) {
  absl::Status s;
  s.Update(absl::OkStatus());
  ASSERT_TRUE(s.ok());
  absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 2, "message");
  int line = GET_SOURCE_LOCATION(1);
  s.Update(a);
  ASSERT_EQ(s, a);
  CheckStatus(s, MyErrorSpace::Get(), 2, util::error::UNKNOWN, "message", "",
              {}, {line});

  absl::Status b = util::MakeStatus(MyErrorSpace::Get(), 17, "other message");
  s.Update(b);
  ASSERT_EQ(s, a);
  CheckStatus(s, MyErrorSpace::Get(), 2, util::error::UNKNOWN, "message", "",
              {}, {line});

  s.Update(absl::OkStatus());
  ASSERT_EQ(s, a);
  ASSERT_FALSE(s.ok());
  CheckStatus(s, MyErrorSpace::Get(), 2, util::error::UNKNOWN, "message", "",
              {}, {line});
}

TEST(Status, AssignEmpty) {
  absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 2, "message");
  absl::Status b;
  a = b;
  ASSERT_EQ(std::string("OK"), a.ToString());
  ASSERT_TRUE(b.ok());
  ASSERT_TRUE(a.ok());
}

TEST(Status, AssignPayload) {
  const google::protobuf::bridge::MessageSet payload = MakeTestPayload("foo");
  absl::Status a =
      util::MakeStatus(MyErrorSpace::Get(), 2, "message", &payload);
  int line = GET_SOURCE_LOCATION(1);

  absl::Status b;
  b = a;
  CheckStatus(b, MyErrorSpace::Get(), 2, util::error::UNKNOWN, "message", "foo",
              {}, {line});
  EXPECT_EQ(a, b);
  ASSERT_TRUE(util::HasPayload(a));
  ASSERT_TRUE(util::HasPayload(b));
  EXPECT_THAT(util::MakePayloadsSet(a), EqualsProto(util::MakePayloadsSet(b)));
}

TEST(Status, AssignEmptyPayload) {
  const google::protobuf::bridge::MessageSet payload = MakeTestPayload("foo");
  absl::Status a =
      util::MakeStatus(MyErrorSpace::Get(), 2, "message", &payload);
  absl::Status b;
  a = b;
  ASSERT_EQ(std::string("OK"), a.ToString());
  ASSERT_TRUE(b.ok());
  ASSERT_TRUE(a.ok());
}

TEST(Status, Swap) {
  absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 2, "message");
  absl::Status b = a;
  absl::Status c;
  swap(c, a);
  ASSERT_EQ(c, b);
  ASSERT_EQ(a, absl::OkStatus());
}

TEST(Status, SwapWithPayload) {
  const google::protobuf::bridge::MessageSet payload = MakeTestPayload("foo");
  absl::Status a =
      util::MakeStatus(MyErrorSpace::Get(), 2, "message", &payload);
  absl::Status b = a;
  absl::Status c;
  swap(c, a);
  ASSERT_EQ(c, b);
  ASSERT_EQ(a, absl::OkStatus());
}

TEST(Status, UnknownCode) {
  absl::Status status = util::MakeStatus(MyErrorSpace::Get(), 10, "message");
  ASSERT_TRUE(!status.ok());
  ASSERT_EQ(10, util::RetrieveErrorCode(status));
  ASSERT_EQ(absl::StatusCode::kUnknown, status.code());
  ASSERT_EQ(std::string("message"), status.message());
  ASSERT_EQ(::util::RetrieveErrorSpace(status), MyErrorSpace::Get());
  ASSERT_THAT(status.ToString(), HasSubstr(ExpectedErrorSpaceName()));
  ASSERT_THAT(status.ToString(), HasSubstr("10"));
  ASSERT_THAT(status.ToString(), HasSubstr("message"));
}

TEST(Status, EqualsSame) {
  const absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 1, "message");
  const absl::Status b = util::MakeStatus(MyErrorSpace::Get(), 1, "message");
  ASSERT_EQ(a, b);
}

TEST(Status, EqualsCopy) {
  const absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 1, "message");
  const absl::Status b = a;
  ASSERT_EQ(a, b);
}

TEST(Status, EqualsDifferentCode) {
  const absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 1, "message");
  const absl::Status b = util::MakeStatus(MyErrorSpace::Get(), 2, "message");
  ASSERT_NE(a, b);
}

TEST(Status, EqualsDifferentSpace) {
  const absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 1, "message");
  const absl::Status b =
      util::MakeStatus(s2::MyErrorSpace2::Get(), 1, "message");
  ASSERT_NE(a, b);
}

TEST(Status, EqualsDifferentMessage) {
  const absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 1, "message");
  const absl::Status b = util::MakeStatus(MyErrorSpace::Get(), 1, "another");
  ASSERT_NE(a, b);
}

TEST(Status, EqualsPayload1) {
  const google::protobuf::bridge::MessageSet payload = MakeTestPayload("foo");
  absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 1, "msg", &payload);
  absl::Status b = util::MakeStatus(MyErrorSpace::Get(), 1, "msg");
  ASSERT_NE(a, b);
}

TEST(Status, EqualsPayload2) {
  const google::protobuf::bridge::MessageSet payload = MakeTestPayload("foo");
  absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 1, "msg");
  absl::Status b = util::MakeStatus(MyErrorSpace::Get(), 1, "msg", &payload);
  ASSERT_NE(a, b);
}

TEST(Status, EqualsPayloadSame) {
  const google::protobuf::bridge::MessageSet payload_a = MakeTestPayload("foo");
  absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 1, "msg", &payload_a);
  const google::protobuf::bridge::MessageSet payload_b = MakeTestPayload("foo");
  absl::Status b = util::MakeStatus(MyErrorSpace::Get(), 1, "msg", &payload_b);
  ASSERT_EQ(a, b);
}

TEST(Status, EqualsPayloadMismatch) {
  const google::protobuf::bridge::MessageSet payload_foo =
      MakeTestPayload("foo");
  const google::protobuf::bridge::MessageSet payload_bar =
      MakeTestPayload("bar");
  absl::Status a =
      util::MakeStatus(MyErrorSpace::Get(), 1, "msg", &payload_foo);
  absl::Status b =
      util::MakeStatus(MyErrorSpace::Get(), 1, "msg", &payload_bar);
  ASSERT_NE(a, b);
}

TEST(Status, EqualsCanonicalCodeSame) {
  absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 1234, "message");
  absl::Status b = util::MakeStatus(MyErrorSpace::Get(), 1234, "message");
  ASSERT_EQ(a, b);
  util::SetCanonicalCode(absl::StatusCode::kResourceExhausted, &a);
  util::SetCanonicalCode(absl::StatusCode::kResourceExhausted, &b);
  ASSERT_EQ(a, b);
}

TEST(Status, EqualsCanonicalCodeMismatch) {
  absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 1234, "message");
  absl::Status b = util::MakeStatus(MyErrorSpace::Get(), 1234, "message");
  ASSERT_EQ(a, b);
  util::SetCanonicalCode(absl::StatusCode::kResourceExhausted, &a);
  util::SetCanonicalCode(absl::StatusCode::kUnavailable, &b);
  ASSERT_NE(a, b);
}

TEST(Status, StripMessage) {
  absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 1, "");
  absl::Status b = util::MakeStatus(MyErrorSpace::Get(), 1, "x");
  EXPECT_EQ(a, util::StripMessage(b));
  util::SetCanonicalCode(absl::StatusCode::kPermissionDenied, &a);
  util::SetCanonicalCode(absl::StatusCode::kPermissionDenied, &b);
  EXPECT_EQ(a, util::StripMessage(b));
}

TEST(Status, HasErrorSpace) {
  EXPECT_TRUE(
      util::HasErrorSpace(absl::OkStatus(), util::CanonicalErrorSpace()));
  EXPECT_TRUE(
      util::HasErrorSpace(absl::Status(absl::StatusCode::kCancelled, "msg"),
                          util::CanonicalErrorSpace()));
  EXPECT_TRUE(util::HasErrorSpace(
      util::MakeStatus(MyErrorSpace ::Get(), 1, "msg"), MyErrorSpace ::Get()));
}

TEST(Status, HasErrorCodeOnOkStatus) {
  absl::Status ok = absl::OkStatus();
  EXPECT_TRUE(util::HasErrorCode(ok, absl::StatusCode::kOk));
  EXPECT_TRUE(util::HasErrorCode(ok, util::CanonicalErrorSpace(), 0));
  EXPECT_TRUE(util::HasErrorCode(ok, s2::MyErrorSpace2::Get(), 0));
}

TEST(Status, GetErrorSpaceForEnum) {
  absl::Status adl = util::MakeStatus(s2::kPermissionDenied, "x");
  absl::Status manual =
      util::MakeStatus(s2::MyErrorSpace2::Get(), s2::kPermissionDenied, "x");
  absl::Status canonical =
      absl::Status(absl::StatusCode::kPermissionDenied, "x");
  EXPECT_EQ(adl, manual);
  EXPECT_TRUE(util::HasErrorCode(adl, s2::kPermissionDenied));
  EXPECT_TRUE(util::HasErrorCode(manual, s2::kPermissionDenied));
  EXPECT_FALSE(util::HasErrorCode(manual, absl::StatusCode::kPermissionDenied));
  EXPECT_FALSE(util::HasErrorCode(canonical, s2::kPermissionDenied));
  EXPECT_TRUE(
      util::HasErrorCode(canonical, absl::StatusCode::kPermissionDenied));
}

TEST(Status, StripMessageWithPayload) {
  const google::protobuf::bridge::MessageSet payload = MakeTestPayload("foo");
  absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 1, "");
  absl::Status b = util::MakeStatus(MyErrorSpace::Get(), 1, "x", &payload);
  EXPECT_EQ(a, util::StripMessage(b));
}

static ABSL_ATTRIBUTE_NOINLINE absl::Status FailRoutine() {
  absl::Status failed_status(absl::StatusCode::kInternal, "fail");
  ABSL_BLOCK_TAIL_CALL_OPTIMIZATION();
  return failed_status;
}

constexpr char kUrl[] = "url.payload";
constexpr char kPayload[] = "aaaaa";

PayloadsVec AllVisitedPayloads(const absl::Status& s) {
  PayloadsVec result;

  s.ForEachPayload([&](absl::string_view type_url, const absl::Cord& payload) {
    result.push_back(std::make_pair(std::string(type_url), payload));
  });

  return result;
}

TEST(Status, MessageSetPayload) {
  absl::Status s = FailRoutine();
  util::TestPayload payload;
  payload.set_message("test");
  util::AttachPayload(&s, payload);
  EXPECT_TRUE(util::HasPayloadWithType<util::TestPayload>(s));
  EXPECT_THAT(util::GetPayload<util::TestPayload>(s), EqualsProto(payload));
}

TEST(Status, MoveOps_Global) {
  absl::Status from = absl::CancelledError();
  absl::Status to = std::move(from);

  EXPECT_FALSE(to.ok());
  EXPECT_EQ(util::CanonicalErrorSpace(), ::util::RetrieveErrorSpace(to));
  EXPECT_EQ(util::error::CANCELLED, util::RetrieveErrorCode(to));
  EXPECT_EQ("", to.message());

  // We can't check the actual state of 'from', but the methods with no
  // preconditions should continue to work.
  EXPECT_GE(util::RetrieveErrorCode(from), 0);
  EXPECT_EQ(from.message(), "Status accessed after move.");

  // If I overwrite it, it should have a known state.
  from = absl::OkStatus();
  EXPECT_TRUE(from.ok());
  EXPECT_EQ(util::CanonicalErrorSpace(), ::util::RetrieveErrorSpace(from));
  EXPECT_EQ(util::error::OK, util::RetrieveErrorCode(from));
  EXPECT_EQ("", from.message());

  to = std::move(from);
  EXPECT_TRUE(to.ok());
  EXPECT_EQ(util::CanonicalErrorSpace(), ::util::RetrieveErrorSpace(to));
  EXPECT_EQ(util::error::OK, util::RetrieveErrorCode(to));
  EXPECT_EQ("", to.message());

  // We can't check the actual state of 'from', but the methods with no
  // preconditions should continue to work.
  EXPECT_GE(util::RetrieveErrorCode(from), 0);
  EXPECT_EQ(from.message(), "Status accessed after move.");
}

TEST(Status, MoveOps_NonGlobal) {
  absl::Status from(absl::StatusCode::kInvalidArgument, "Message");
  absl::Status to = std::move(from);

  EXPECT_FALSE(to.ok());
  EXPECT_EQ(util::CanonicalErrorSpace(), ::util::RetrieveErrorSpace(to));
  EXPECT_EQ(util::error::INVALID_ARGUMENT, util::RetrieveErrorCode(to));
  EXPECT_EQ("Message", to.message());

  // We can't check the actual state of 'from', but the methods with no
  // preconditions should continue to work.
  EXPECT_GE(util::RetrieveErrorCode(from), 0);
  EXPECT_EQ(from.message(), "Status accessed after move.");

  // If I overwrite it, it should have a known state.
  from = absl::Status(absl::StatusCode::kFailedPrecondition, "Other message");
  EXPECT_FALSE(from.ok());
  EXPECT_EQ(util::CanonicalErrorSpace(), ::util::RetrieveErrorSpace(from));
  EXPECT_EQ(util::error::FAILED_PRECONDITION, util::RetrieveErrorCode(from));
  EXPECT_EQ("Other message", from.message());

  to = std::move(from);
  EXPECT_FALSE(to.ok());
  EXPECT_EQ(util::CanonicalErrorSpace(), ::util::RetrieveErrorSpace(to));
  EXPECT_EQ(util::error::FAILED_PRECONDITION, util::RetrieveErrorCode(to));
  EXPECT_EQ("Other message", to.message());

  // We can't check the actual state of 'from', but the methods with no
  // preconditions should continue to work.
  EXPECT_GE(util::RetrieveErrorCode(from), 0);
  EXPECT_EQ(from.message(), "Status accessed after move.");
}

TEST(Canonical, CustomMapping) {
  absl::Status s = util::MakeStatus(
      MyErrorSpace::Get(),
      static_cast<int>(MyErrorCode::kCustomPermissionDenied), "message");
  EXPECT_EQ(absl::StatusCode::kPermissionDenied, s.code());
}

TEST(Status, Annotate) {
  using ::absl::Status;

  // Standard status.
  EXPECT_EQ(
      absl::Status(absl::StatusCode::kUnavailable, "message; annotated"),
      ::util::Annotate(absl::Status(absl::StatusCode::kUnavailable, "message"),
                       "annotated"));

  EXPECT_EQ(absl::Status(absl::StatusCode::kUnavailable, "message"),
            ::util::Annotate(
                absl::Status(absl::StatusCode::kUnavailable, "message"), ""));

  EXPECT_EQ(absl::Status(absl::StatusCode::kUnavailable, "annotated"),
            ::util::Annotate(absl::Status(absl::StatusCode::kUnavailable, ""),
                             "annotated"));

  // MessageSet payload.
  const google::protobuf::bridge::MessageSet payload = MakeTestPayload("foo");
  EXPECT_EQ(
      util::MakeStatus(MyErrorSpace::Get(), 1, "m; annotated", &payload),
      ::util::Annotate(util::MakeStatus(MyErrorSpace::Get(), 1, "m", &payload),
                       "annotated"));

  // OK status.
  EXPECT_EQ(absl::OkStatus(),
            ::util::Annotate(absl::OkStatus(), "message ignore for OK status"));

  // Explicitly set canonical code.
  {
    absl::Status s = util::MakeStatus(MyErrorSpace::Get(), 1234, "");
    util::SetCanonicalCode(absl::StatusCode::kResourceExhausted, &s);

    EXPECT_EQ(absl::StatusCode::kResourceExhausted,
              util::Annotate(s, "foo").code());
  }
}

TEST(Canonical, CanonicalCode) {
  absl::Status ok = absl::OkStatus();
  absl::Status cancel = absl::CancelledError();
  absl::Status perm = util::MakeStatus(
      MyErrorSpace::Get(),
      static_cast<int>(MyErrorCode::kCustomPermissionDenied), "message");
  absl::Status other = util::MakeStatus(MyErrorSpace::Get(), -1, "message");
  EXPECT_EQ(ok.code(), absl::StatusCode::kOk);
  EXPECT_EQ(cancel.code(), absl::StatusCode::kCancelled);
  EXPECT_EQ(perm.code(), absl::StatusCode::kPermissionDenied);
  EXPECT_EQ(other.code(), absl::StatusCode::kUnknown);

  // Check handling of a canonical code not known in this address space.
  util::SetCanonicalCode(
      static_cast<absl::StatusCode>(util::error::Code_MAX + 1), &perm);
  EXPECT_EQ(perm.code(), absl::StatusCode::kUnknown);
}

TEST(Canonical, SetCanonicalCode) {
  absl::Status s = util::MakeStatus(MyErrorSpace::Get(), 1234, "message");
  util::SetCanonicalCode(absl::StatusCode::kResourceExhausted, &s);
  EXPECT_EQ(1234, util::RetrieveErrorCode(s));
  EXPECT_EQ(absl::StatusCode::kResourceExhausted, s.code());
}

TEST(Canonical, SetCanonicalCodeIgnoredOnOkStatus) {
  absl::Status s = util::MakeStatus(MyErrorSpace::Get(), 0, "message");
  util::SetCanonicalCode(absl::StatusCode::kResourceExhausted, &s);
  EXPECT_TRUE(s.ok());
  EXPECT_EQ(absl::StatusCode::kOk, s.code());
}

TEST(Canonical, SetCanonicalCodeIgnoredOnCanonicalSpace) {
  absl::Status s(absl::StatusCode::kDeadlineExceeded, "message");
  util::SetCanonicalCode(absl::StatusCode::kResourceExhausted, &s);
  EXPECT_EQ(util::error::DEADLINE_EXCEEDED, util::RetrieveErrorCode(s));
  EXPECT_EQ(absl::StatusCode::kDeadlineExceeded, s.code());
}

TEST(Canonical, SetCanonicalCodeOnSharedStatus) {
  const absl::Status x = util::MakeStatus(MyErrorSpace::Get(), 1234, "message");

  // Set canonical code on a copy.
  absl::Status y = x;
  util::SetCanonicalCode(absl::StatusCode::kResourceExhausted, &y);
  EXPECT_NE(x, y);
  EXPECT_EQ(util::RetrieveErrorSpace(x), util::RetrieveErrorSpace(y));
  EXPECT_EQ(util::RetrieveErrorCode(x), util::RetrieveErrorCode(y));
  EXPECT_EQ(x.message(), y.message());
  EXPECT_EQ(absl::StatusCode::kUnknown, x.code());
  EXPECT_EQ(absl::StatusCode::kResourceExhausted, y.code());

  // Yet another copy, with a different code set.
  absl::Status z = y;
  util::SetCanonicalCode(absl::StatusCode::kDeadlineExceeded, &z);
  EXPECT_NE(y, z);
  EXPECT_EQ(util::RetrieveErrorSpace(x), util::RetrieveErrorSpace(z));
  EXPECT_EQ(util::RetrieveErrorCode(x), util::RetrieveErrorCode(z));
  EXPECT_EQ(x.message(), z.message());
  EXPECT_EQ(absl::StatusCode::kResourceExhausted, y.code());
  EXPECT_EQ(absl::StatusCode::kDeadlineExceeded, z.code());
}

void MaybeAttachPayload(absl::Status* status) {
  util::TestPayload payload;
  payload.set_message("a");
  util::AttachPayload(status, payload);
  status->SetPayload(kUrl, absl::Cord(kPayload));
}

// Check all payloads are equal except for `ErrorSpace` payloads.
void CheckPayloadEqual(const absl::Status& orig,
                       const absl::Status& new_status) {
  EXPECT_EQ(util::HasPayload(orig), util::HasPayload(new_status));
  if (util::HasPayload(orig) && util::HasPayload(new_status)) {
    EXPECT_THAT(util::MakePayloadsSet(orig),
                EqualsProto(util::MakePayloadsSet(new_status)));
  }
  static constexpr char kErrorSpaceUrl[] =
      "type.googleapis.com/util.ErrorSpacePayload";
  orig.ForEachPayload(
      [&](absl::string_view type_url, const absl::Cord& payload) {
        if (type_url == kErrorSpaceUrl) return;
        EXPECT_THAT(new_status.GetPayload(type_url), Optional(payload));
      });
  new_status.ForEachPayload(
      [&](absl::string_view type_url, const absl::Cord& payload) {
        if (type_url == kErrorSpaceUrl) return;
        EXPECT_THAT(orig.GetPayload(type_url), Optional(payload));
      });
}

void CheckSourceLocationsEqual(const absl::Status& orig,
                               const absl::Status& new_status) {
  auto orig_sources = orig.GetSourceLocations();
  auto new_sources = new_status.GetSourceLocations();
  ASSERT_EQ(orig_sources.size(), new_sources.size());

  for (size_t i = 0; i < orig_sources.size(); ++i) {
    EXPECT_EQ(absl::string_view(orig_sources[i].file_name()),
              absl::string_view(new_sources[i].file_name()));
    EXPECT_EQ(orig_sources[i].line(), new_sources[i].line());
  }
}

TEST(StatusMutation, SetMessage) {
  auto CheckEqualityExceptMsg = [](const absl::Status& orig,
                                   const absl::Status& new_status) {
    EXPECT_EQ(orig.code(), new_status.code());
    EXPECT_EQ(util::RetrieveErrorCode(orig),
              util::RetrieveErrorCode(new_status));
    EXPECT_EQ(util::RetrieveErrorSpace(orig),
              util::RetrieveErrorSpace(new_status));
    CheckPayloadEqual(orig, new_status);
    CheckSourceLocationsEqual(orig, new_status);
  };

  static const char kOldMsg[] = "old msg";
  static const char kNewMsg[] = "new msg";
  {
    absl::Status orig = absl::OkStatus();
    absl::Status new_status = util::SetMessage(orig, kNewMsg);
    EXPECT_EQ(orig, new_status);
  }
  {
    absl::Status orig(absl::StatusCode::kUnknown, kOldMsg);
    MaybeAttachPayload(&orig);
    absl::Status new_status = util::SetMessage(orig, kNewMsg);
    CheckEqualityExceptMsg(orig, new_status);
    EXPECT_EQ(new_status.message(), kNewMsg);
  }
  {
    absl::Status orig =
        util::MakeStatus(MyErrorCode::kCustomPermissionDenied, kOldMsg);
    util::SetCanonicalCode(absl::StatusCode::kDataLoss, &orig);
    MaybeAttachPayload(&orig);
    absl::Status new_status = util::SetMessage(orig, kNewMsg);
    CheckEqualityExceptMsg(orig, new_status);
    EXPECT_EQ(new_status.message(), kNewMsg);
  }
}

TEST(StatusMutation, SetSpaceAndCode) {
  auto CheckEqualityExceptSpaceAndCode = [](const absl::Status& orig,
                                            const absl::Status& new_status) {
    EXPECT_EQ(orig.message(), new_status.message());
    CheckPayloadEqual(orig, new_status);
    CheckSourceLocationsEqual(orig, new_status);
  };

  static const char kMsg[] = "message";
  // OK -> CanonicalError
  {
    absl::Status orig;
    absl::Status new_status = util::SetErrorSpaceAndCode(
        orig, util::CanonicalErrorSpace(),
        static_cast<int>(absl::StatusCode::kUnknown));
    CheckEqualityExceptSpaceAndCode(orig, new_status);
    EXPECT_EQ(util::RetrieveErrorSpace(new_status),
              util::CanonicalErrorSpace());
    EXPECT_EQ(new_status.code(), absl::StatusCode::kUnknown);
  }
  // CanonicalError -> CustomError
  {
    absl::Status orig(absl::StatusCode::kUnknown, kMsg);
    MaybeAttachPayload(&orig);
    absl::Status new_status = util::SetErrorSpaceAndCode(
        orig, MyErrorSpace::Get(),
        static_cast<int>(MyErrorCode::kCustomPermissionDenied));
    CheckEqualityExceptSpaceAndCode(orig, new_status);
    EXPECT_EQ(util::RetrieveErrorSpace(new_status), MyErrorSpace::Get());
    EXPECT_EQ(util::RetrieveErrorCode(new_status),
              static_cast<int>(MyErrorCode::kCustomPermissionDenied));
  }
  // CanonicalError -> OK
  {
    absl::Status orig(absl::StatusCode::kUnknown, kMsg);
    MaybeAttachPayload(&orig);
    absl::Status new_status =
        util::SetErrorSpaceAndCode(orig, util::CanonicalErrorSpace(), 0);
    EXPECT_TRUE(new_status.ok());
  }
  // CustomError -> CanonicalError
  {
    absl::Status orig =
        util::MakeStatus(MyErrorCode::kCustomPermissionDenied, kMsg);
    MaybeAttachPayload(&orig);
    absl::Status new_status = util::SetErrorSpaceAndCode(
        orig, util::CanonicalErrorSpace(),
        static_cast<int>(absl::StatusCode::kUnknown));
    CheckEqualityExceptSpaceAndCode(orig, new_status);
    EXPECT_EQ(util::RetrieveErrorSpace(new_status),
              util::CanonicalErrorSpace());
    EXPECT_EQ(new_status.code(), absl::StatusCode::kUnknown);
  }
}

TEST(StatusMutation, SetMessageErrorSpaceAndCode) {
  auto CheckPayloadAndSourceLocationEquality =
      [](const absl::Status& orig, const absl::Status& new_status) {
        CheckPayloadEqual(orig, new_status);
        CheckSourceLocationsEqual(orig, new_status);
      };

  constexpr absl::string_view kMsg = "old msg";
  constexpr absl::string_view kNewMsg = "new msg";
  // OK -> CanonicalError
  {
    absl::Status orig;
    absl::Status new_status = util::SetMessageErrorSpaceAndCode(
        orig, kNewMsg, util::CanonicalErrorSpace(),
        static_cast<int>(absl::StatusCode::kUnknown));
    CheckPayloadAndSourceLocationEquality(orig, new_status);
    EXPECT_EQ(new_status.message(), kNewMsg);
    EXPECT_EQ(util::RetrieveErrorSpace(new_status),
              util::CanonicalErrorSpace());
    EXPECT_EQ(new_status.code(), absl::StatusCode::kUnknown);
  }
  // CanonicalError -> CustomError
  {
    absl::Status orig(absl::StatusCode::kUnknown, kMsg);
    MaybeAttachPayload(&orig);
    absl::Status new_status = util::SetMessageErrorSpaceAndCode(
        orig, kNewMsg, MyErrorSpace::Get(),
        static_cast<int>(MyErrorCode::kCustomPermissionDenied));
    CheckPayloadAndSourceLocationEquality(orig, new_status);
    EXPECT_EQ(new_status.message(), kNewMsg);
    EXPECT_EQ(util::RetrieveErrorSpace(new_status), MyErrorSpace::Get());
    EXPECT_EQ(util::RetrieveErrorCode(new_status),
              static_cast<int>(MyErrorCode::kCustomPermissionDenied));
  }
  // CanonicalError -> OK
  {
    absl::Status orig(absl::StatusCode::kUnknown, kMsg);
    MaybeAttachPayload(&orig);
    absl::Status new_status = util::SetMessageErrorSpaceAndCode(
        orig, kNewMsg, util::CanonicalErrorSpace(), 0);
    EXPECT_TRUE(new_status.ok());
  }
  // CustomError -> CanonicalError
  {
    absl::Status orig =
        util::MakeStatus(MyErrorCode::kCustomPermissionDenied, kMsg);
    MaybeAttachPayload(&orig);
    absl::Status new_status = util::SetMessageErrorSpaceAndCode(
        orig, kNewMsg, util::CanonicalErrorSpace(),
        static_cast<int>(absl::StatusCode::kUnknown));
    CheckPayloadAndSourceLocationEquality(orig, new_status);
    EXPECT_EQ(new_status.message(), kNewMsg);
    EXPECT_EQ(util::RetrieveErrorSpace(new_status),
              util::CanonicalErrorSpace());
    EXPECT_EQ(new_status.code(), absl::StatusCode::kUnknown);
  }
}

TEST(SourceLocation, Annotate) {
  {
    absl::Status original = absl::Status(absl::StatusCode::kUnavailable, "");
    CheckSourceLocation(original);
    absl::Status annotated = ::util::Annotate(original, "foo");
    CheckSourceLocation(annotated);
    annotated = ::util::Annotate(annotated, " bar");
    CheckSourceLocation(annotated);
  }
  {
    absl::Status original = absl::Status(absl::StatusCode::kUnavailable, "foo",
                                         absl::SourceLocation::current());
    int line1 = GET_SOURCE_LOCATION(1);
    CheckSourceLocation(original, {line1});
    absl::Status annotated = ::util::Annotate(original, "bar");
    CheckSourceLocation(annotated, {line1});
  }
}

TEST(SourceLocation, StripMessage) {
  {
    absl::Status original = absl::Status(absl::StatusCode::kUnavailable, "");
    CheckSourceLocation(original);
    absl::Status status = ::util::StripMessage(original);
    CheckSourceLocation(status);
  }
  {
    absl::Status original = absl::Status(absl::StatusCode::kUnavailable, "foo",
                                         absl::SourceLocation::current());
    int line1 = GET_SOURCE_LOCATION(1);
    CheckSourceLocation(original, {line1});
    absl::Status status = ::util::StripMessage(original);
    CheckSourceLocation(status);
  }
}

TEST(SourceLocation, MakeStatusWithErrorSpace) {
  {
    CheckSourceLocation(util::MakeStatus(MyErrorCode::kOk, "ignored"));
    CheckSourceLocation(util::MakeStatus(MyErrorCode::kOk, ""));
    CheckSourceLocation(
        util::MakeStatus(MyErrorCode::kOk, "ignored", absl::SourceLocation()));
    CheckSourceLocation(
        util::MakeStatus(MyErrorCode::kOk, "", absl::SourceLocation()));
  }
  {
    absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 2, "");
    CheckSourceLocation(a);
  }
  {
    absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 2, "foo",
                                      absl::SourceLocation::current());
    CheckSourceLocation(a, {GET_SOURCE_LOCATION(1)});
  }
}

TEST(SourceLocation, SetErrorSpaceAndCode) {
  static const char kMsg[] = "message";
  // OK -> CanonicalError
  {
    absl::Status orig;
    absl::Status new_status = util::SetErrorSpaceAndCode(
        orig, util::CanonicalErrorSpace(),
        static_cast<int>(absl::StatusCode::kUnknown));
    CheckSourceLocation(new_status);
  }
  // CanonicalError -> CustomError
  {
    absl::Status orig(absl::StatusCode::kUnknown, kMsg);
    int line = GET_SOURCE_LOCATION(1);
    MaybeAttachPayload(&orig);
    CheckSourceLocation(orig, {line});
    absl::Status new_status = util::SetErrorSpaceAndCode(
        orig, MyErrorSpace::Get(),
        static_cast<int>(MyErrorCode::kCustomPermissionDenied));
    CheckSourceLocation(new_status, {line});
  }
  // CanonicalError -> OK
  {
    absl::Status orig(absl::StatusCode::kUnknown, kMsg);
    MaybeAttachPayload(&orig);
    CheckSourceLocation(orig, {GET_SOURCE_LOCATION(2)});
    absl::Status new_status =
        util::SetErrorSpaceAndCode(orig, util::CanonicalErrorSpace(), 0);
    CheckSourceLocation(new_status);
  }
  // CustomError -> CanonicalError
  {
    absl::Status orig =
        util::MakeStatus(MyErrorCode::kCustomPermissionDenied, kMsg);
    int line = GET_SOURCE_LOCATION(1);
    MaybeAttachPayload(&orig);
    CheckSourceLocation(orig, {line});
    absl::Status new_status = util::SetErrorSpaceAndCode(
        orig, util::CanonicalErrorSpace(),
        static_cast<int>(absl::StatusCode::kUnknown));
    CheckSourceLocation(new_status, {line});
  }
}

TEST(SourceLocation, SetCanonicalCode) {
  {
    absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 1, "msg");
    int line = GET_SOURCE_LOCATION(1);
    CheckSourceLocation(a, {line});

    util::SetCanonicalCode(absl::StatusCode::kPermissionDenied, &a);
    CheckSourceLocation(a, {line});
  }
  {
    absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 1, "msg");
    int line = GET_SOURCE_LOCATION(1);
    CheckSourceLocation(a, {line});

    util::SetCanonicalCode(absl::StatusCode::kOk, &a);
    // Setting CanonicalCode to kOk does not clear the source location.
    // Do Not Submit: Is this a valid case?
    CheckSourceLocation(a, {line});
  }
  {
    absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 0, "msg");
    CheckSourceLocation(a);
    util::SetCanonicalCode(absl::StatusCode::kPermissionDenied, &a);
    CheckSourceLocation(a);
  }
  {
    absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 0, "msg");
    CheckSourceLocation(a);
    util::SetCanonicalCode(absl::StatusCode::kOk, &a);
    CheckSourceLocation(a);
  }
}

TEST(SourceLocation, ToCanonicalCode) {
  {
    absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 1, "msg",
                                      absl::SourceLocation::current());
    int line = GET_SOURCE_LOCATION(1);
    CheckSourceLocation(a, {line});

    CheckSourceLocation(util::ToCanonical(a), {line});
  }
  {
    absl::Status a = util::MakeStatus(MyErrorSpace::Get(), 0, "msg");
    CheckSourceLocation(a);

    CheckSourceLocation(util::ToCanonical(a));
  }
}

TEST(ToCanonicalTest, OkReturnsUnchanged) {
  const int value = 5;
  const absl::StatusOr<int> status_or(value);
  EXPECT_THAT(util::ToCanonical(status_or), IsOkAndHolds(value));
}

TEST(ToCanonicalTest, CanonicalErrorReturnsUnchanged) {
  const absl::Status canon_error = absl::AbortedError("msg");
  const absl::StatusOr<int> status_or(canon_error);
  EXPECT_THAT(util::ToCanonical(status_or), Eq(status_or));
}

TEST(ToCanonicalTest, NoncanonicalErrorReturnsCanonical) {
  const absl::Status custom_error = MakeStatus(MyErrorSpace::Get(), 1, "msg");

  const absl::StatusOr<int> status_or(custom_error);
  auto canonical = util::ToCanonical(status_or);
  EXPECT_EQ(util::RetrieveErrorSpace(canonical.status()),
            util::CanonicalErrorSpace());
  EXPECT_EQ(canonical.status().code(), MyErrorSpace::Get()->CanonicalCode(1));
  EXPECT_EQ(canonical.status().message(), "msg");
}

TEST(ToCanonicalTest, DoesntCopyValue) {
  absl::StatusOr<std::unique_ptr<int>> status_or(std::make_unique<int>(5));
  EXPECT_THAT(util::ToCanonical(std::move(status_or)), IsOk());
}

TEST(ToCanonicalTest, ConstStatus) {
  const absl::StatusOr<int> status_or = 5;
  EXPECT_THAT(util::ToCanonical(status_or), IsOk());
}

TEST(SourceLocation, SetMessage) {
  static const char kOldMsg[] = "old msg";
  static const char kNewMsg[] = "new msg";
  {
    absl::Status orig = absl::OkStatus();
    absl::Status new_status = util::SetMessage(orig, kNewMsg);
    CheckSourceLocation(new_status);
  }
  {
    absl::Status orig(absl::StatusCode::kUnknown, kOldMsg);
    absl::Status new_status = util::SetMessage(orig, kNewMsg);
    CheckSourceLocation(new_status, {GET_SOURCE_LOCATION(2)});
  }
  {
    absl::Status orig(absl::StatusCode::kUnknown, kOldMsg);
    absl::Status new_status = util::SetMessage(orig, "");
    CheckSourceLocation(new_status);

    new_status = util::SetMessage(new_status, kNewMsg);
    CheckSourceLocation(new_status);
  }
  {
    absl::Status orig =
        util::MakeStatus(MyErrorCode::kCustomPermissionDenied, kOldMsg);
    util::SetCanonicalCode(absl::StatusCode::kDataLoss, &orig);
    absl::Status new_status = util::SetMessage(orig, kNewMsg);
    CheckSourceLocation(new_status, {GET_SOURCE_LOCATION(3)});
  }
}

TEST(StatusCode, StatusCodeToString) {
  // Make sure `StatusCodeToString()` outputs the same results as
  // `util::error::Code_Name()` on all valid `util::error::Code`.
  // We cannot use EnumerateEnumValues here; to be compatible with lite protos
  // we have to avoid relying on descriptors.
  // NOLINTNEXTLINE
  for (int i = util::error::Code_MIN; i <= util::error::Code_MAX; ++i) {
    auto code = static_cast<util::error::Code>(i);
    if (util::error::Code_IsValid(code) &&
        code !=
            util::error::
                DO_NOT_USE_RESERVED_FOR_FUTURE_EXPANSION_USE_DEFAULT_IN_SWITCH_INSTEAD_) {  // NOLINT
      EXPECT_EQ(util::error::Code_Name(code),
                absl::StatusCodeToString(util::ToAbslStatusCode(code)));
    }
  }
}

TEST(PrintStatusPayload, CanPrintKnownProto) {
  google::protobuf::Int32Value payload;
  payload.set_value(123);
  google::protobuf::Any any;
  any.PackFrom(payload);

  absl::Status status = absl::CancelledError("test");
  status.SetPayload(any.type_url(), absl::Cord(any.value()));
  EXPECT_THAT(absl::StrCat(status), HasSubstr("Int32Value"));
  if constexpr (std::is_base_of_v<google::protobuf::Message,
                                  google::protobuf::Any>) {
    // Printing any proto only works with full protos (lite protos do not have
    // the descriptors).
    EXPECT_THAT(absl::StrCat(status), HasSubstr("value: 123"));
  }
}

TEST(PrintStatusPayload, UnknownTypeUrl) {
  absl::Status status = absl::CancelledError("test");
  status.SetPayload("type.googleapis.com/util.UnknownType", absl::Cord(""));
  EXPECT_THAT(absl::StrCat(status), HasSubstr("UnknownType"));
}

TEST(PrintStatusPayload, InvalidTypeUrl) {
  absl::Status status = absl::CancelledError("test");
  status.SetPayload("invalid_type_url", absl::Cord(""));
  EXPECT_THAT(absl::StrCat(status), HasSubstr("invalid_type_url"));
}

TEST(PrintStatusPayload, InvalidData) {
  google::protobuf::Int32Value payload;
  payload.set_value(123);
  google::protobuf::Any any;
  any.PackFrom(payload);

  absl::Status status = absl::CancelledError("test");
  status.SetPayload(any.type_url(), absl::Cord("this will not parse"));
  EXPECT_THAT(absl::StrCat(status), HasSubstr("Int32Value"));
}

TEST(StatusCode, UtilErrorCodeConversion) {
  // Make sure the conversion functions produce the same numeric values.
  // We cannot use EnumerateEnumValues here; to be compatible with lite protos
  // we have to avoid relying on descriptors.
  // NOLINTNEXTLINE
  for (int i = util::error::Code_MIN; i <= util::error::Code_MAX; ++i) {
    auto code = static_cast<util::error::Code>(i);
    if (util::error::Code_IsValid(code) &&
        code !=
            util::error::
                DO_NOT_USE_RESERVED_FOR_FUTURE_EXPANSION_USE_DEFAULT_IN_SWITCH_INSTEAD_) {  // NOLINT
      absl::StatusCode absl_code = util::ToAbslStatusCode(code);
      EXPECT_EQ(static_cast<int>(code), static_cast<int>(absl_code));
      EXPECT_EQ(util::ToUtilErrorCode(absl_code), code);
    }
  }
}

TEST(StatusFuzz, VarintEncodingWithBadLimit) {
  absl::string_view varint_encoding_with_bad_limit = absl::string_view(
      "\022\000\022\022\020\022\021\026M:\314\315\315}"
      "\315\315\010\010\010\010\010\010\010\010\010\010\010\010\010\315\315"
      "\234",
      32);
  absl::Status status = absl::CancelledError();
  status.SetPayload(util::status_internal::kErrorSpaceUrl,
                    absl::Cord(varint_encoding_with_bad_limit));
  int code = util::RetrieveErrorCode(status);
  EXPECT_EQ(code, util::error::CANCELLED);
  const util::ErrorSpace* space = util::RetrieveErrorSpace(status);
  EXPECT_EQ(space, util::CanonicalErrorSpace());
}

static void BM_StatusCreateDestroy(benchmark::State& state) {
  for (auto _ : state) {
    absl::Status status;
    benchmark::DoNotOptimize(status);
  }
}
BENCHMARK(BM_StatusCreateDestroy);

static void BM_StatusCreateDestroy_ErrorSpace(benchmark::State& state) {
  for (auto _ : state) {
    absl::Status status =
        util::MakeStatus(MyErrorCode::kCustomPermissionDenied, "");
    benchmark::DoNotOptimize(status);
  }
}
BENCHMARK(BM_StatusCreateDestroy_ErrorSpace);

const auto& kStatusOk = *new absl::Status();
const auto& kStatusError =
    *new auto(absl::UnknownError("This string is 28 characters"));
const auto& kStatusErrorSpace = *new absl::Status(util::MakeStatus(
    MyErrorCode::kCustomPermissionDenied, "This string is 28 characters"));

static void BM_StatusCopyOk(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(kStatusOk);
    absl::Status dest = kStatusOk;
    benchmark::DoNotOptimize(dest);
  }
}
BENCHMARK(BM_StatusCopyOk)->ThreadRange(1, 8);

static void BM_StatusCopyError(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(kStatusError);
    absl::Status dest = kStatusError;
    benchmark::DoNotOptimize(dest);
  }
}
BENCHMARK(BM_StatusCopyError)->ThreadRange(1, 8);

static void BM_StatusCopyError_ErrorSpace(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(kStatusErrorSpace);
    absl::Status dest = kStatusErrorSpace;
    benchmark::DoNotOptimize(dest);
  }
}
BENCHMARK(BM_StatusCopyError_ErrorSpace)->ThreadRange(1, 8);

static void BM_StatusCopyError_Deep(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(kStatusError);
    absl::Status dest(kStatusError.code(), kStatusError.message());
    benchmark::DoNotOptimize(dest);
  }
}
BENCHMARK(BM_StatusCopyError_Deep)->ThreadRange(1, 8);

static void BM_StatusCopyError_Deep_ErrorSpace(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(kStatusErrorSpace);
    absl::Status dest = util::MakeStatus(
        static_cast<MyErrorCode>(util::RetrieveErrorCode(kStatusErrorSpace)),
        kStatusErrorSpace.message());
    benchmark::DoNotOptimize(dest);
  }
}
BENCHMARK(BM_StatusCopyError_Deep_ErrorSpace)->ThreadRange(1, 8);

static void BM_StatusAssignCopyError(benchmark::State& state) {
  absl::Status status[2] = {absl::UnknownError("This string is 28 characters"),
                            absl::OkStatus()};
  int src = 0;
  benchmark::DoNotOptimize(status[src]);
  for (auto _ : state) {
    const int dest = 1 - src;
    status[dest] = absl::OkStatus();
    status[dest] = status[src];
    benchmark::DoNotOptimize(status[dest]);
    src = dest;
  }
}
BENCHMARK(BM_StatusAssignCopyError);

static void BM_StatusAssignCopyError_ErrorSpace(benchmark::State& state) {
  absl::Status status[2] = {
      util::MakeStatus(MyErrorCode::kCustomPermissionDenied,
                       "This string is 28 characters"),
      absl::OkStatus()};
  int src = 0;
  benchmark::DoNotOptimize(status[src]);
  for (auto _ : state) {
    const int dest = 1 - src;
    status[dest] = absl::OkStatus();
    status[dest] = status[src];
    benchmark::DoNotOptimize(status[dest]);
    src = dest;
  }
}
BENCHMARK(BM_StatusAssignCopyError_ErrorSpace);

static void BM_StatusAssignMoveError(benchmark::State& state) {
  absl::Status status[2] = {absl::UnknownError("This string is 28 characters"),
                            absl::OkStatus()};
  int src = 0;
  benchmark::DoNotOptimize(status[src]);
  for (auto _ : state) {
    const int dest = 1 - src;
    status[dest] = absl::OkStatus();
    status[dest] = std::move(status[src]);
    benchmark::DoNotOptimize(status[dest]);
    src = dest;
  }
}
BENCHMARK(BM_StatusAssignMoveError);

static void BM_StatusAssignMoveError_ErrorSpace(benchmark::State& state) {
  absl::Status status[2] = {
      util::MakeStatus(MyErrorCode::kCustomPermissionDenied,
                       "This string is 28 characters"),
      absl::OkStatus()};
  int src = 0;
  benchmark::DoNotOptimize(status[src]);
  for (auto _ : state) {
    const int dest = 1 - src;
    status[dest] = absl::OkStatus();
    status[dest] = std::move(status[src]);
    benchmark::DoNotOptimize(status[dest]);
    src = dest;
  }
}
BENCHMARK(BM_StatusAssignMoveError_ErrorSpace);

static void BM_StatusUpdateCopyError(benchmark::State& state) {
  absl::Status status[2] = {absl::UnknownError("This string is 28 characters"),
                            absl::OkStatus()};
  int src = 0;
  benchmark::DoNotOptimize(status[src]);
  for (auto _ : state) {
    const int dest = 1 - src;
    status[dest] = absl::OkStatus();
    status[dest].Update(status[src]);
    benchmark::DoNotOptimize(status[dest]);
    src = dest;
  }
}
BENCHMARK(BM_StatusUpdateCopyError);

static void BM_StatusUpdateMoveError(benchmark::State& state) {
  absl::Status status[2] = {absl::UnknownError("This string is 28 characters"),
                            absl::OkStatus()};
  int src = 0;
  benchmark::DoNotOptimize(status[src]);
  for (auto _ : state) {
    const int dest = 1 - src;
    status[dest] = absl::OkStatus();
    status[dest].Update(std::move(status[src]));
    benchmark::DoNotOptimize(status[dest]);
    src = dest;
  }
}
BENCHMARK(BM_StatusUpdateMoveError);

static void BM_StatusClear(benchmark::State& state) {
  absl::Status status;
  for (auto _ : state) {
    benchmark::DoNotOptimize(status);
    status = absl::OkStatus();
    benchmark::DoNotOptimize(status);
  }
}
BENCHMARK(BM_StatusClear);

static void BM_StatusIgnoreError(benchmark::State& state) {
  absl::Status status;
  for (auto _ : state) {
    benchmark::DoNotOptimize(status);
    status.IgnoreError();
    benchmark::DoNotOptimize(status);
  }
}
BENCHMARK(BM_StatusIgnoreError);

// Benchmark for returning OK util::Statuses across multiple function calls.
// This version of the benchmark instantiates a separate const absl::Status for
// every function called *and* returns absl::OkStatus(), with the intent of
// inhibiting NRVO.
absl::Status ReturnOk1() { return absl::OkStatus(); }

absl::Status ReturnOk2() {
  const absl::Status status1 = ReturnOk1();
  if (!status1.ok()) return status1;
  const absl::Status status2 = ReturnOk1();
  if (!status2.ok()) return status2;
  return absl::OkStatus();
}

absl::Status ReturnOk3() {
  const absl::Status status = ReturnOk2();
  if (!status.ok()) return status;
  return absl::OkStatus();
}

absl::Status ReturnOk4() {
  const absl::Status status = ReturnOk3();
  if (!status.ok()) return status;
  return absl::OkStatus();
}

absl::Status ReturnOk5() {
  const absl::Status status = ReturnOk4();
  if (!status.ok()) return status;
  return absl::OkStatus();
}

static void BM_ReturnOkNested(benchmark::State& state) {
  for (auto _ : state) {
    absl::Status status = ReturnOk5();
    benchmark::DoNotOptimize(status);
  }
}
BENCHMARK(BM_ReturnOkNested);

// This benchmark is the same as BM_ReturnOkNested, but uses a pattern that
// should be optimizable using NRVO.
absl::Status ReturnOkRvo1() {
  absl::Status status;
  return status;
}

absl::Status ReturnOkRvo2() {
  absl::Status status;
  status = ReturnOkRvo1();
  if (!status.ok()) return status;
  status = ReturnOkRvo1();
  if (!status.ok()) return status;
  return status;
}

absl::Status ReturnOkRvo3() {
  absl::Status status;
  status = ReturnOkRvo2();
  return status;
}

absl::Status ReturnOkRvo4() {
  absl::Status status;
  status = ReturnOkRvo3();
  return status;
}

absl::Status ReturnOkRvo5() {
  absl::Status status;
  status = ReturnOkRvo4();
  return status;
}

static void BM_ReturnOkRvoNested(benchmark::State& state) {
  for (auto _ : state) {
    absl::Status status = ReturnOkRvo5();
    benchmark::DoNotOptimize(status);
  }
}
BENCHMARK(BM_ReturnOkRvoNested);

// Benchmark for returning error util::Statuses across multiple function calls.
// The pattern used is one that should not trigger NRVO.
absl::Status ReturnError1() {
  return absl::Status(absl::StatusCode::kInvalidArgument,
                      "Baaaaaaaad sheep. Baaaaahaaaaaahaaaad sheep!");
}

// The status variables are intentionally left non-const to allow for implicit
// moves on return.
absl::Status ReturnError2() {
  absl::Status status1 = ReturnError1();
  if (!status1.ok()) return status1;
  absl::Status status2 = ReturnError1();
  if (!status2.ok()) return status2;
  return absl::OkStatus();
}

absl::Status ReturnError3() {
  absl::Status status = ReturnError2();
  if (!status.ok()) return status;
  return absl::OkStatus();
}

absl::Status ReturnError4() {
  absl::Status status = ReturnError3();
  if (!status.ok()) return status;
  return absl::OkStatus();
}

absl::Status ReturnError5() {
  absl::Status status = ReturnError4();
  if (!status.ok()) return status;
  return absl::OkStatus();
}

static void BM_ReturnErrorNested(benchmark::State& state) {
  for (auto _ : state) {
    absl::Status status = ReturnError5();
    benchmark::DoNotOptimize(status);
  }
}
BENCHMARK(BM_ReturnErrorNested);

static void BM_StatusToStringOk(benchmark::State& state) {
  absl::Status status;
  for (auto _ : state) {
    std::string ok_status = status.ToString();
    benchmark::DoNotOptimize(ok_status);
  }
}
BENCHMARK(BM_StatusToStringOk);

// Below are for util::StatusToString rather than status::ToString.

static void BM_Util_StatusToStringOk(benchmark::State& state) {
  absl::Status status;
  for (auto _ : state) {
    std::string status_string = util::StatusToString(status);
    benchmark::DoNotOptimize(status_string);
  }
}
BENCHMARK(BM_Util_StatusToStringOk);

static void BM_Util_StatusToStringNotOk(benchmark::State& state) {
  absl::Status status = ReturnError1();
  for (auto _ : state) {
    std::string status_string = util::StatusToString(status);
    benchmark::DoNotOptimize(status_string);
  }
}
BENCHMARK(BM_Util_StatusToStringNotOk);

static void BM_Util_StatusToString_NonCanonical(benchmark::State& state) {
  absl::Status status =
      util::MakeStatus(MyErrorCode::kCustomPermissionDenied, "");
  for (auto _ : state) {
    std::string status_string = util::StatusToString(status);
    benchmark::DoNotOptimize(status_string);
  }
}
BENCHMARK(BM_Util_StatusToString_NonCanonical);

static void BM_HasErrorCode_Positive_ErrorSpace(benchmark::State& state) {
  absl::Status status =
      util::MakeStatus(MyErrorCode::kCustomPermissionDenied, "");
  for (auto s : state) {
    benchmark::DoNotOptimize(status);
    benchmark::DoNotOptimize(
        util::HasErrorCode(status, MyErrorCode::kCustomPermissionDenied));
  }
}
BENCHMARK(BM_HasErrorCode_Positive_ErrorSpace);

static void BM_HasErrorCode_Negative_ErrorSpace(benchmark::State& state) {
  absl::Status status = util::MakeStatus(MyErrorCode::kCustomUnavailable, "");
  for (auto s : state) {
    benchmark::DoNotOptimize(status);
    benchmark::DoNotOptimize(
        util::HasErrorCode(status, MyErrorCode::kCustomPermissionDenied));
  }
}
BENCHMARK(BM_HasErrorCode_Negative_ErrorSpace);

static void BM_HasErrorCode_Negative_Ok(benchmark::State& state) {
  absl::Status status = absl::OkStatus();
  for (auto s : state) {
    benchmark::DoNotOptimize(status);
    benchmark::DoNotOptimize(
        util::HasErrorCode(status, MyErrorCode::kCustomPermissionDenied));
  }
}
BENCHMARK(BM_HasErrorCode_Negative_Ok);

static void BM_SetMessage_Canonical(benchmark::State& state) {
  absl::Status status = absl::OkStatus();
  for (auto s : state) {
    benchmark::DoNotOptimize(
        util::SetMessage(status, "new message for status"));
  }
}
BENCHMARK(BM_SetMessage_Canonical);

static void BM_SetMessage_NonCanonical(benchmark::State& state) {
  absl::Status status =
      util::MakeStatus(MyErrorCode::kCustomPermissionDenied, "");
  for (auto s : state) {
    benchmark::DoNotOptimize(
        util::SetMessage(status, "new message for status"));
  }
}
BENCHMARK(BM_SetMessage_NonCanonical);

static void BM_StripMessage_Canonical(benchmark::State& state) {
  absl::Status status = absl::OkStatus();

  for (auto s : state) {
    benchmark::DoNotOptimize(util::StripMessage(status));
  }
}
BENCHMARK(BM_StripMessage_Canonical);

static void BM_StripMessage_NonCanonical(benchmark::State& state) {
  absl::Status status =
      util::MakeStatus(MyErrorCode::kCustomPermissionDenied, "");
  for (auto s : state) {
    benchmark::DoNotOptimize(util::StripMessage(status));
  }
}
BENCHMARK(BM_StripMessage_NonCanonical);
