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

#include "gloop/util/status/status_builder.h"

#include <cerrno>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/log/flags.h"
#include "absl/log/globals.h"
#include "absl/log/log.h"
#include "absl/log/log_entry.h"
#include "absl/log/log_sink.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/status/status.h"
#include "absl/status/status_builder.h"
#include "absl/status/status_macros.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/source_location.h"
#include "benchmark/benchmark.h"
#include "gloop/util/status/codes.pb.h"
#include "gloop/util/status/posixerrorspace.h"
#include "gloop/util/status/status.h"
#include "gloop/util/status/status.pb.h"
#include "gloop/util/status/status_macros.h"
#include "gmock/gmock.h"
#include "google/protobuf/bridge/message_set.pb.h"
#include "gtest/gtest.h"

namespace testing {
MATCHER_P(EqualsProto, msg, "") {
  return msg.DebugString() == arg.DebugString();
}
}  // namespace testing

namespace util {
// We use `#line` to produce some `source_location` values pointing at various
// different (fake) files to test e.g. `VLog`, but we use it at the end of this
// file so as not to mess up the source location data for the whole file.
// Making them static data members lets us forward-declare them and define them
// at the end.
struct Locs {
  static const absl::SourceLocation kSecret;
  static const absl::SourceLocation kLevel0;
  static const absl::SourceLocation kLevel1;
  static const absl::SourceLocation kLevel2;
  static const absl::SourceLocation kFoo;
  static const absl::SourceLocation kBar;
};

enum TestCodes {
  kCustomOK = 0,
  kZomg = 1,
};
struct TestSpace : util::ErrorSpaceImpl<TestSpace> {
  static absl::string_view space_name() { return "StatusBuilderTestSpace"; }
  static std::string code_to_string(int code) { return absl::StrCat(code); }
  static absl::StatusCode canonical_code(int code) {
    return absl::StatusCode::kUnknown;
  }
};
const util::ErrorSpace* GetErrorSpace(util::ErrorSpaceAdlTag<TestCodes>) {
  return TestSpace::Get();
}

class StringSink : public absl::LogSink {
 public:
  StringSink() = default;

  void Send(const absl::LogEntry& entry) override {
    absl::StrAppend(&message_, entry.source_basename(), ":",
                    entry.source_line(), " - ", entry.text_message());
  }

  const std::string& ToString() { return message_; }

 private:
  std::string message_;
};

using ::absl_testing::StatusIs;
using ::testing::_;
using ::testing::AllOf;
using ::testing::AnyOf;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Property;

// Converts a StatusBuilder to a Status.
absl::Status ToStatus(const StatusBuilder& s) { return s; }

// Converts a StatusBuilder to a Status and then ignores it.
void ConvertToStatusAndIgnore(const StatusBuilder& s) {
  absl::Status status = s;
  (void)status;
}

// Converts a StatusBuilder to a StatusOr<T>.
template <typename T>
absl::StatusOr<T> ToStatusOr(const StatusBuilder& s) {
  return s;
}

// Makes a StatusProto with a Status.
StatusProto MakeStatusProto(const absl::Status& s) {
  StatusProto proto;
  ::util::SaveStatusToProto(s, &proto);
  return proto;
}

// Makes a payload proto suitable for attaching to a status.
StatusProto MakePayloadProto(absl::string_view str) {
  StatusProto proto;
  proto.set_message(str);
  return proto;
}

// Makes a message set contains a payload proto, suitable for passing to the
// constructor of `absl::Status`.
google::protobuf::bridge::MessageSet MakePayloadMessageSet(
    absl::string_view str) {
  google::protobuf::bridge::MessageSet out;
  *out.MutableExtension(StatusProto::message_set_extension) =
      MakePayloadProto(str);
  return out;
}

void CheckSourceLocation(
    const absl::Status& status, std::vector<int> lines = {},
    absl::SourceLocation loc = absl::SourceLocation::current()) {
  ASSERT_EQ(status.GetSourceLocations().size(), lines.size())
      << "Size check failed at " << loc.line();
  for (int i = 0; i < lines.size(); ++i) {
    EXPECT_EQ(absl::string_view(status.GetSourceLocations()[i].file_name()),
              absl::string_view(loc.file_name()))
        << "File name check failed at " << loc.line();
    EXPECT_EQ(status.GetSourceLocations()[i].line(), lines[i])
        << "Line check failed at " << loc.line();
  }
}

class StatusBuilderTest : public ::testing::Test {};

TEST_F(StatusBuilderTest, Ctors) {
  EXPECT_EQ(ToStatus(MakeStatusBuilder(kZomg) << "zomg"),
            ::util::MakeStatus(TestSpace::Get(), kZomg, "zomg"));
  EXPECT_EQ(ToStatus(MakeStatusBuilder(TestSpace::Get(), kZomg) << "zomg"),
            ::util::MakeStatus(TestSpace::Get(), kZomg, "zomg"));
  EXPECT_EQ(ToStatus(MakeStatusBuilder(PosixErrorSpace(), ENOSYS) << "nope"),
            PosixErrorToStatus(ENOSYS, "nope"));
}

TEST_F(StatusBuilderTest, RepCopyCtor) {
  using Rep = std::decay_t<
      // NOLINTNEXTLINE(abseil-no-internal-dependencies)
      decltype(*absl::status_internal::StatusBuilderPrivateAccessor::GetRep(
          std::declval<absl::StatusBuilder>()))>;
  Rep r1(absl::OkStatus());
  EXPECT_FALSE(r1.stream.has_value());
  r1.InitStream();
  EXPECT_TRUE(r1.stream.has_value());
  Rep r2(r1);
  EXPECT_TRUE(r2.stream.has_value());
}

TEST_F(StatusBuilderTest, IdentityWithStatusProto) {
  google::protobuf::bridge::MessageSet payload_message_set =
      MakePayloadMessageSet("zomg");

  const std::vector<StatusProto> status_protos = {
      MakeStatusProto(absl::OkStatus()),
      MakeStatusProto(absl::CancelledError()),
      MakeStatusProto(absl::InvalidArgumentError("yup")),
      MakeStatusProto(::util::MakeStatus(CanonicalErrorSpace(), error::UNKNOWN,
                                         "ow", &payload_message_set)),
      MakeStatusProto(PosixErrorToStatus(ENOSYS, "enosys")),
  };

  for (const StatusProto& proto : status_protos) {
    absl::Status base;
    base = ::util::MakeStatusFromProto(proto);

    EXPECT_THAT(ToStatus(StatusBuilder(
                    util::MakeStatusFromProto(proto, absl::SourceLocation()))),
                Eq(base));
    EXPECT_EQ(
        StatusBuilder(util::MakeStatusFromProto(proto, absl::SourceLocation()))
            .ok(),
        base.ok());
    if (!base.ok()) {
      EXPECT_THAT(ToStatusOr<int>(StatusBuilder(util::MakeStatusFromProto(
                                      proto, absl::SourceLocation())))
                      .status(),
                  Eq(base));
    }
  }
}

testing::Matcher<absl::SourceLocation> SourceLocationIs(
    absl::SourceLocation loc) {
  return AnyOf(
      AllOf(Property(&absl::SourceLocation::file_name, Eq(loc.file_name())),
            Property(&absl::SourceLocation::line, Eq(loc.line()))),
      // Fallback for platforms that don't support source locations.
      AllOf(Property(&absl::SourceLocation::file_name, Eq("<source_location>")),
            Property(&absl::SourceLocation::line, Eq(1))));
}

TEST_F(StatusBuilderTest, ErrorCode) {
  // OK
  {
    const StatusBuilder builder(absl::OkStatus());
    EXPECT_TRUE(builder.ok());
    EXPECT_THAT(util::GetCanonicalCode(builder), Eq(error::OK));
    EXPECT_TRUE(util::HasErrorCode(builder, error::OK));
    EXPECT_TRUE(util::HasErrorCode(builder, absl::StatusCode::kOk));
    EXPECT_TRUE(util::HasErrorCode(builder, kCustomOK));
    EXPECT_TRUE(util::HasErrorCode(builder, PosixErrorSpace(), 0));
    EXPECT_FALSE(util::HasErrorCode(builder, kZomg));
    EXPECT_TRUE(util::HasErrorSpace(builder, CanonicalErrorSpace()));
    EXPECT_FALSE(util::HasErrorSpace(builder, TestSpace::Get()));
    EXPECT_FALSE(util::HasErrorSpace(builder, PosixErrorSpace()));
  }

  // Custom OK immediately gets converted into a canonical OK, but it still
  // "Is" the custom OK as well.
  {
    const StatusBuilder builder = MakeStatusBuilder(kCustomOK);
    EXPECT_TRUE(builder.ok());
    EXPECT_THAT(util::GetCanonicalCode(builder), Eq(error::OK));
    EXPECT_THAT(builder.code(), Eq(absl::StatusCode::kOk));
    EXPECT_TRUE(util::HasErrorCode(builder, error::OK));
    EXPECT_TRUE(util::HasErrorCode(builder, absl::StatusCode::kOk));
    EXPECT_TRUE(util::HasErrorCode(builder, kCustomOK));
    EXPECT_TRUE(util::HasErrorCode(builder, PosixErrorSpace(), 0));
    EXPECT_FALSE(util::HasErrorCode(builder, kZomg));
    EXPECT_TRUE(util::HasErrorSpace(builder, CanonicalErrorSpace()));
    EXPECT_FALSE(util::HasErrorSpace(builder, TestSpace::Get()));
    EXPECT_FALSE(util::HasErrorSpace(builder, PosixErrorSpace()));
  }

  // Non-OK canonical code
  {
    const StatusBuilder builder = MakeStatusBuilder(error::INVALID_ARGUMENT);
    EXPECT_FALSE(builder.ok());
    EXPECT_THAT(util::GetCanonicalCode(builder), Eq(error::INVALID_ARGUMENT));
    EXPECT_THAT(builder.code(), Eq(absl::StatusCode::kInvalidArgument));
    EXPECT_TRUE(util::HasErrorCode(builder, error::INVALID_ARGUMENT));
    EXPECT_TRUE(
        util::HasErrorCode(builder, absl::StatusCode::kInvalidArgument));
    EXPECT_FALSE(util::HasErrorCode(builder, kCustomOK));
    EXPECT_FALSE(util::HasErrorCode(builder, kZomg));
    EXPECT_FALSE(util::HasErrorCode(builder, PosixErrorSpace(), 0));
    EXPECT_FALSE(util::HasErrorCode(builder, PosixErrorSpace(),
                                    static_cast<int>(error::INVALID_ARGUMENT)));
    EXPECT_TRUE(util::HasErrorSpace(builder, CanonicalErrorSpace()));
    EXPECT_FALSE(util::HasErrorSpace(builder, TestSpace::Get()));
    EXPECT_FALSE(util::HasErrorSpace(builder, PosixErrorSpace()));
    // Is() is not allowed to be called on a canonical code, so we cannot do a
    // positive test for Is() here.
  }

  // Custom error space
  {
    const StatusBuilder builder = MakeStatusBuilder(kZomg);
    EXPECT_FALSE(builder.ok());
    EXPECT_THAT(util::GetCanonicalCode(builder), Eq(error::UNKNOWN));
    EXPECT_FALSE(util::HasErrorCode(builder, error::UNKNOWN));
    EXPECT_FALSE(util::HasErrorCode(builder, absl::StatusCode::kUnknown));
    EXPECT_FALSE(util::HasErrorCode(builder, kCustomOK));
    EXPECT_TRUE(util::HasErrorCode(builder, kZomg));
    EXPECT_FALSE(util::HasErrorSpace(builder, CanonicalErrorSpace()));
    EXPECT_TRUE(util::HasErrorSpace(builder, TestSpace::Get()));
    EXPECT_FALSE(util::HasErrorSpace(builder, PosixErrorSpace()));
  }
  // Custom error space without ADL
  {
    const StatusBuilder builder = MakeStatusBuilder(PosixErrorSpace(), EINVAL);
    EXPECT_FALSE(builder.ok());
    EXPECT_FALSE(util::HasErrorCode(builder, kCustomOK));
    EXPECT_FALSE(util::HasErrorCode(builder, kZomg));
    EXPECT_TRUE(util::HasErrorCode(builder, PosixErrorSpace(), EINVAL));
    EXPECT_FALSE(util::HasErrorSpace(builder, CanonicalErrorSpace()));
    EXPECT_FALSE(util::HasErrorSpace(builder, TestSpace::Get()));
    EXPECT_TRUE(util::HasErrorSpace(builder, PosixErrorSpace()));
  }
}

TEST_F(StatusBuilderTest, SetErrorCodeLvalue) {
  {
    StatusBuilder builder(absl::CancelledError(), absl::SourceLocation());
    const auto mset = MakePayloadMessageSet("oops");
    EXPECT_THAT(ToStatus(builder.AttachPayload(MakePayloadProto("oops"))
                             .SetErrorCode(error::FAILED_PRECONDITION)),
                Eq(util::MakeStatus(CanonicalErrorSpace(),
                                    error::FAILED_PRECONDITION, "", &mset)));
  }
  {
    StatusBuilder builder(absl::CancelledError("monkey"),
                          absl::SourceLocation());
    EXPECT_THAT(
        ToStatus(builder.SetErrorCode(error::FAILED_PRECONDITION) << "nuts"),
        Eq(::util::MakeStatus(CanonicalErrorSpace(), error::FAILED_PRECONDITION,
                              "monkey; nuts")));
  }
}

TEST_F(StatusBuilderTest, SetErrorCodeRvalue) {
  const auto mset = MakePayloadMessageSet("oops");
  EXPECT_THAT(ToStatus(std::move(StatusBuilder(absl::CancelledError(),
                                               absl::SourceLocation()))
                           .AttachPayload(MakePayloadProto("oops"))
                           .SetErrorCode(error::FAILED_PRECONDITION)),
              Eq(util::MakeStatus(CanonicalErrorSpace(),
                                  error::FAILED_PRECONDITION, "", &mset)));
  EXPECT_THAT(
      ToStatus(
          StatusBuilder(absl::CancelledError("monkey"), absl::SourceLocation())
              .SetErrorCode(error::FAILED_PRECONDITION)
          << "nuts"),
      Eq(::util::MakeStatus(CanonicalErrorSpace(), error::FAILED_PRECONDITION,
                            "monkey; nuts")));
}

TEST_F(StatusBuilderTest, SetCodeLvalue) {
  {
    StatusBuilder builder(absl::CancelledError(), absl::SourceLocation());
    auto attach = MakePayloadMessageSet("paperclip");
    EXPECT_THAT(
        ToStatus(builder.AttachPayload(MakePayloadProto("paperclip"))
                     .SetCode(absl::StatusCode::kFailedPrecondition)),
        Eq(::util::MakeStatus(CanonicalErrorSpace(), error::FAILED_PRECONDITION,
                              "", &attach)));
  }
  {
    StatusBuilder builder(absl::CancelledError("nope"), absl::SourceLocation());
    EXPECT_THAT(
        ToStatus(builder.SetCode(absl::StatusCode::kFailedPrecondition)
                 << "nuh uh"),
        Eq(::util::MakeStatus(CanonicalErrorSpace(), error::FAILED_PRECONDITION,
                              "nope; nuh uh")));
  }
}

TEST_F(StatusBuilderTest, SetCodeRvalue) {
  auto attach = MakePayloadMessageSet("paperclip");
  EXPECT_THAT(ToStatus(std::move(StatusBuilder(absl::CancelledError(),
                                               absl::SourceLocation()))
                           .AttachPayload(MakePayloadProto("paperclip"))
                           .SetCode(absl::StatusCode::kFailedPrecondition)),
              Eq(::util::MakeStatus(CanonicalErrorSpace(),
                                    error::FAILED_PRECONDITION, "", &attach)));
  EXPECT_THAT(
      ToStatus(
          StatusBuilder(absl::CancelledError("nope"), absl::SourceLocation())
              .SetCode(absl::StatusCode::kFailedPrecondition)
          << "nuh uh"),
      Eq(::util::MakeStatus(CanonicalErrorSpace(), error::FAILED_PRECONDITION,
                            "nope; nuh uh")));
}

TEST_F(StatusBuilderTest, AttachLvalue) {
  {
    const auto mset = MakePayloadMessageSet("oops");
    StatusBuilder builder(absl::CancelledError(), absl::SourceLocation());
    EXPECT_THAT(ToStatus(builder.AttachPayload(MakePayloadProto("oops"))),
                Eq(::util::MakeStatus(CanonicalErrorSpace(), error::CANCELLED,
                                      "", &mset)));
  }
  {
    const auto mset = MakePayloadMessageSet("boom");
    StatusBuilder builder(absl::CancelledError(), absl::SourceLocation());
    EXPECT_THAT(
        ToStatus(builder.AttachPayload(MakePayloadProto("boom")) << "stick"),
        Eq(::util::MakeStatus(CanonicalErrorSpace(), error::CANCELLED, "stick",
                              &mset)));
  }
  {
    StatusBuilder builder(PosixErrorToStatus(ENOSYS, "so"),
                          absl::SourceLocation());
    const auto mset = MakePayloadMessageSet("this");
    EXPECT_THAT(
        ToStatus(builder.AttachPayload(MakePayloadProto("this")) << "happened"),
        Eq(::util::MakeStatus(PosixErrorSpace(), ENOSYS, "so; happened",
                              &mset)));
  }
}

TEST_F(StatusBuilderTest, AttachPayloadLvalue) {
  {
    const auto mset = MakePayloadMessageSet("oops");
    StatusBuilder builder(absl::CancelledError(), absl::SourceLocation());
    EXPECT_THAT(ToStatus(builder.AttachPayload(MakePayloadProto("oops"))),
                Eq(::util::MakeStatus(CanonicalErrorSpace(), error::CANCELLED,
                                      "", &mset)));
  }
  {
    const auto mset = MakePayloadMessageSet("boom");
    StatusBuilder builder(absl::CancelledError(), absl::SourceLocation());
    EXPECT_THAT(
        ToStatus(builder.AttachPayload(MakePayloadProto("boom")) << "stick"),
        Eq(::util::MakeStatus(CanonicalErrorSpace(), error::CANCELLED, "stick",
                              &mset)));
  }
  {
    StatusBuilder builder(PosixErrorToStatus(ENOSYS, "so"),
                          absl::SourceLocation());
    const auto mset = MakePayloadMessageSet("this");
    EXPECT_THAT(
        ToStatus(builder.AttachPayload(MakePayloadProto("this")) << "happened"),
        Eq(::util::MakeStatus(PosixErrorSpace(), ENOSYS, "so; happened",
                              &mset)));
  }
}

TEST_F(StatusBuilderTest, AttachRvalue) {
  auto mset = MakePayloadMessageSet("oops");
  EXPECT_THAT(ToStatus(std::move(StatusBuilder(absl::CancelledError(),
                                               absl::SourceLocation()))
                           .AttachPayload(MakePayloadProto("oops"))),
              Eq(::util::MakeStatus(CanonicalErrorSpace(), error::CANCELLED, "",
                                    &mset)));
  mset = MakePayloadMessageSet("boom");
  EXPECT_THAT(ToStatus(std::move(StatusBuilder(absl::CancelledError(),
                                               absl::SourceLocation()))
                           .AttachPayload(MakePayloadProto("boom"))
                       << "stick"),
              Eq(::util::MakeStatus(CanonicalErrorSpace(), error::CANCELLED,
                                    "stick", &mset)));
  mset = MakePayloadMessageSet("this");
  EXPECT_THAT(
      ToStatus(std::move(StatusBuilder(PosixErrorToStatus(ENOSYS, "so"),
                                       absl::SourceLocation()))
                   .AttachPayload(MakePayloadProto("this"))
               << "happened"),
      Eq(::util::MakeStatus(PosixErrorSpace(), ENOSYS, "so; happened", &mset)));
}

TEST_F(StatusBuilderTest, AttachPayloadRvalue) {
  auto mset = MakePayloadMessageSet("oops");
  EXPECT_THAT(
      ToStatus(StatusBuilder(absl::CancelledError(), absl::SourceLocation())
                   .AttachPayload(MakePayloadProto("oops"))),
      Eq(::util::MakeStatus(CanonicalErrorSpace(), error::CANCELLED, "",
                            &mset)));
  mset = MakePayloadMessageSet("boom");
  EXPECT_THAT(
      ToStatus(StatusBuilder(absl::CancelledError(), absl::SourceLocation())
                   .AttachPayload(MakePayloadProto("boom"))
               << "stick"),
      Eq(::util::MakeStatus(CanonicalErrorSpace(), error::CANCELLED, "stick",
                            &mset)));
  mset = MakePayloadMessageSet("this");
  EXPECT_THAT(
      ToStatus(StatusBuilder(PosixErrorToStatus(ENOSYS, "so"),
                             absl::SourceLocation())
                   .AttachPayload(MakePayloadProto("this"))
               << "happened"),
      Eq(::util::MakeStatus(PosixErrorSpace(), ENOSYS, "so; happened", &mset)));
}

TEST_F(StatusBuilderTest, AttachPayloadStandard) {
  auto mset = MakePayloadMessageSet("oops");
  StatusBuilder builder(absl::CancelledError(), absl::SourceLocation());
  builder.AttachPayload(MakePayloadProto("oops"));
  EXPECT_THAT(ToStatus(builder),
              Eq(::util::MakeStatus(CanonicalErrorSpace(), error::CANCELLED, "",
                                    &mset)));
}

TEST_F(StatusBuilderTest, AttachPayloadMethodStandard) {
  auto mset = MakePayloadMessageSet("oops");
  EXPECT_THAT(
      ToStatus(StatusBuilder(absl::CancelledError(), absl::SourceLocation())
                   .AttachPayload(MakePayloadProto("oops"))),
      Eq(::util::MakeStatus(CanonicalErrorSpace(), error::CANCELLED, "",
                            &mset)));
}

TEST_F(StatusBuilderTest, MessageSetPayloadHelpers) {
  static const char* kPayloadMsg = "payload";
  StatusBuilder builder(absl::CancelledError(), absl::SourceLocation());
  EXPECT_FALSE(util::HasPayload(builder));
  builder.AttachPayload(MakePayloadProto(kPayloadMsg));
  EXPECT_TRUE(util::HasPayload(builder));
  EXPECT_TRUE(util::HasPayloadWithType<util::StatusProto>(builder));
  EXPECT_THAT(util::GetPayload<util::StatusProto>(builder),
              testing::EqualsProto(MakePayloadProto(kPayloadMsg)));
}

TEST_F(StatusBuilderTest, MessageSetPayloadMethods) {
  static const char* kPayloadMsg = "payload";
  StatusBuilder builder(absl::CancelledError(), absl::SourceLocation());
  EXPECT_FALSE(util::HasPayload(builder));
  builder.AttachPayload(MakePayloadProto(kPayloadMsg));
  EXPECT_TRUE(util::HasPayload(builder));
  EXPECT_TRUE(util::HasPayloadWithType<util::StatusProto>(builder));
  EXPECT_TRUE(util::HasPayloadWithType<util::StatusProto>(
      builder, util::StatusProto::message_set_extension));
  EXPECT_THAT(util::GetPayload<util::StatusProto>(builder),
              testing::EqualsProto(MakePayloadProto(kPayloadMsg)));
  EXPECT_THAT(util::GetPayload<util::StatusProto>(
                  builder, util::StatusProto::message_set_extension),
              testing::EqualsProto(MakePayloadProto(kPayloadMsg)));
}

TEST_F(StatusBuilderTest, MessageSetPayloadMethodsOnOkStatus) {
  static const char* kPayloadMsg = "payload";
  StatusBuilder builder(absl::OkStatus(), absl::SourceLocation());
  EXPECT_FALSE(util::HasPayload(builder));
  EXPECT_FALSE(util::HasPayloadWithType<util::StatusProto>(builder));
  builder.AttachPayload(MakePayloadProto(kPayloadMsg));
  EXPECT_FALSE(util::HasPayload(builder));
  EXPECT_FALSE(util::HasPayloadWithType<util::StatusProto>(builder));
}

TEST_F(StatusBuilderTest, SetPayloadLvalue) {
  StatusBuilder builder(absl::CancelledError(), absl::SourceLocation());
  builder.SetPayload("test_url", absl::Cord("oops"));
  EXPECT_EQ(builder.GetPayload("test_url"), "oops");
  auto expected = absl::CancelledError("");
  expected.SetPayload("test_url", absl::Cord("oops"));
  EXPECT_THAT(ToStatus(builder), Eq(expected));
}

TEST_F(StatusBuilderTest, SetPayloadRvalue) {
  auto expected = absl::CancelledError("");
  expected.SetPayload("test_url", absl::Cord("oops"));
  EXPECT_THAT(
      absl::Status(StatusBuilder(absl::CancelledError(), absl::SourceLocation())
                       .SetPayload("test_url", absl::Cord("oops"))),
      Eq(expected));
}

TEST_F(StatusBuilderTest, WithVoidTypeAndSideEffects) {
  int code = 0;
  auto policy = [&code](absl::Status status) {
    code = ::util::RetrieveErrorCode(status);
  };
  StatusBuilder(absl::CancelledError(), absl::SourceLocation()).With(policy);
  EXPECT_EQ(::util::error::CANCELLED, code);
  StatusBuilder(absl::OkStatus(), absl::SourceLocation()).With(policy);
  EXPECT_EQ(::util::error::OK, code);
}

struct MoveOnlyAdaptor {
  std::unique_ptr<int> value;
  std::unique_ptr<int> operator()(const absl::Status&) && {
    return std::move(value);
  }
};

template <typename T>
std::string ToStringViaStream(const T& x) {
  std::ostringstream os;
  os << ::util::StatusToString(x);
  return os.str();
}

TEST_F(StatusBuilderTest, StreamInsertionOperator) {
  {
    absl::Status status = absl::AbortedError("zomg");
    StatusBuilder builder(status, absl::SourceLocation());
    EXPECT_EQ(ToStringViaStream(status), ToStringViaStream(builder));
    EXPECT_EQ(ToStringViaStream(status),
              ToStringViaStream(StatusBuilder(status, absl::SourceLocation())));
  }
  {
    absl::Status status = util::MakeStatus(TestSpace::Get(), kZomg, "zomg");
    auto builder = MakeStatusBuilder(TestSpace::Get(), kZomg) << "zomg";
    EXPECT_EQ(ToStringViaStream(status), ToStringViaStream(builder));
    EXPECT_EQ(ToStringViaStream(status),
              ToStringViaStream(MakeStatusBuilder(TestSpace::Get(), kZomg)
                                << "zomg"));
  }
  {
    absl::Status status = PosixErrorToStatus(ENOSYS, "nope");
    auto builder = MakeStatusBuilder(PosixErrorSpace(), ENOSYS) << "nope";
    EXPECT_EQ(ToStringViaStream(status),
              ToStringViaStream(MakeStatusBuilder(PosixErrorSpace(), ENOSYS)
                                << "nope"));
  }
}

struct StringifiableType {
  absl::string_view message;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const StringifiableType& o) {
    sink.Append(o.message);
  }
};
TEST_F(StatusBuilderTest, AbslStringify) {
  absl::Status status = util::MakeStatus(TestSpace::Get(), kZomg, "zomg");
  auto builder = MakeStatusBuilder(TestSpace::Get(), kZomg)
                 << StringifiableType{"zomg"};
  EXPECT_EQ(ToStringViaStream(status), ToStringViaStream(builder));
  EXPECT_EQ(ToStringViaStream(status),
            ToStringViaStream(MakeStatusBuilder(TestSpace::Get(), kZomg)
                              << StringifiableType{"zomg"}));
}

TEST_F(StatusBuilderTest, ToStringDefaultStatusBuilder) {
  EXPECT_EQ(util::StatusBuilder().ToString(),
            ToStringViaStream(util::StatusBuilder()));
}

TEST_F(StatusBuilderTest, ToStringWithStreamInsertionOperator) {
  {
    absl::Status status = absl::AbortedError("zomg");
    StatusBuilder builder(status, absl::SourceLocation());
    EXPECT_EQ(ToStringViaStream(status), builder.ToString());
    EXPECT_EQ(ToStringViaStream(status),
              StatusBuilder(status, absl::SourceLocation()).ToString());
  }
  {
    absl::Status status = util::MakeStatus(TestSpace::Get(), kZomg, "zomg");
    auto builder = MakeStatusBuilder(TestSpace::Get(), kZomg) << "zomg";
    EXPECT_EQ(ToStringViaStream(status), ToStringViaStream(builder));
    EXPECT_EQ(
        ToStringViaStream(status),
        (MakeStatusBuilder(TestSpace::Get(), kZomg) << "zomg").ToString());
  }
  {
    absl::Status status = PosixErrorToStatus(ENOSYS, "nope");
    auto builder = MakeStatusBuilder(PosixErrorSpace(), ENOSYS) << "nope";
    EXPECT_EQ(
        ToStringViaStream(status),
        (MakeStatusBuilder(PosixErrorSpace(), ENOSYS) << "nope").ToString());
  }
}

TEST_F(StatusBuilderTest, ToStringWithPayloads) {
  auto mset = MakePayloadMessageSet("oops");
  absl::Status status =
      ::util::MakeStatus(CanonicalErrorSpace(), error::CANCELLED, "", &mset);
  StatusBuilder builder(absl::CancelledError(), absl::SourceLocation());
  builder.AttachPayload(MakePayloadProto("oops"));
  EXPECT_EQ(ToStringViaStream(status), builder.ToString());
}

class MockLogSink : public absl::LogSink {
 public:
  MOCK_METHOD(void, Send, (const absl::LogEntry&), (override));
};

TEST_F(StatusBuilderTest, ToStringDoesntHaveSideEffects) {
  testing::MockFunction<void()> checkpoint;
  testing::StrictMock<MockLogSink> log_sink;
  {
    testing::InSequence s;
    EXPECT_CALL(log_sink, Send).Times(0);
    EXPECT_CALL(checkpoint, Call);
    EXPECT_CALL(log_sink, Send);
  }

  StatusBuilder builder(absl::CancelledError(), absl::SourceLocation());
  builder.AttachPayload(MakePayloadProto("oops"));
  builder << "hello world!";
  builder.LogError();
  builder.AlsoOutputToSink(&log_sink);

  absl::Status status = absl::CancelledError("hello world!");
  util::AttachPayload(&status, MakePayloadProto("oops"));

  EXPECT_EQ(
      ToStringViaStream(status),
      // This does not have side effects, and so won't send to the log sink.
      builder.ToString());
  checkpoint.Call();
  EXPECT_EQ(ToStringViaStream(status),
            // This converts to Status, so it produces side effects and sends
            // to the log sink.
            ToStringViaStream(builder));
}

TEST(WithExtraMessagePolicyTest, ExtraMessageMoveConstructor) {
  auto policy = util::ExtraMessage() << "doing foo";

  EXPECT_THAT(StatusBuilder(absl::AbortedError("taco")).With(std::move(policy)),
              StatusIs(absl::StatusCode::kAborted, "taco; doing foo"));
}

TEST(CanonicalErrorsTest, CreateAndClassify) {
  struct CanonicalErrorTest {
    error::Code code;
    StatusBuilder builder;
  };
  absl::SourceLocation loc = absl::SourceLocation::current();
  CanonicalErrorTest canonical_errors[] = {
      // implicit location
      {error::ABORTED, AbortedErrorBuilder()},
      {error::ALREADY_EXISTS, AlreadyExistsErrorBuilder()},
      {error::CANCELLED, CancelledErrorBuilder()},
      {error::DATA_LOSS, DataLossErrorBuilder()},
      {error::DEADLINE_EXCEEDED, DeadlineExceededErrorBuilder()},
      {error::FAILED_PRECONDITION, FailedPreconditionErrorBuilder()},
      {error::INTERNAL, InternalErrorBuilder()},
      {error::INVALID_ARGUMENT, InvalidArgumentErrorBuilder()},
      {error::NOT_FOUND, NotFoundErrorBuilder()},
      {error::OUT_OF_RANGE, OutOfRangeErrorBuilder()},
      {error::PERMISSION_DENIED, PermissionDeniedErrorBuilder()},
      {error::UNAUTHENTICATED, UnauthenticatedErrorBuilder()},
      {error::RESOURCE_EXHAUSTED, ResourceExhaustedErrorBuilder()},
      {error::UNAVAILABLE, UnavailableErrorBuilder()},
      {error::UNIMPLEMENTED, UnimplementedErrorBuilder()},
      {error::UNKNOWN, UnknownErrorBuilder()},

      // explicit location
      {error::ABORTED, AbortedErrorBuilder(loc)},
      {error::ALREADY_EXISTS, AlreadyExistsErrorBuilder(loc)},
      {error::CANCELLED, CancelledErrorBuilder(loc)},
      {error::DATA_LOSS, DataLossErrorBuilder(loc)},
      {error::DEADLINE_EXCEEDED, DeadlineExceededErrorBuilder(loc)},
      {error::FAILED_PRECONDITION, FailedPreconditionErrorBuilder(loc)},
      {error::INTERNAL, InternalErrorBuilder(loc)},
      {error::INVALID_ARGUMENT, InvalidArgumentErrorBuilder(loc)},
      {error::NOT_FOUND, NotFoundErrorBuilder(loc)},
      {error::OUT_OF_RANGE, OutOfRangeErrorBuilder(loc)},
      {error::PERMISSION_DENIED, PermissionDeniedErrorBuilder(loc)},
      {error::UNAUTHENTICATED, UnauthenticatedErrorBuilder(loc)},
      {error::RESOURCE_EXHAUSTED, ResourceExhaustedErrorBuilder(loc)},
      {error::UNAVAILABLE, UnavailableErrorBuilder(loc)},
      {error::UNIMPLEMENTED, UnimplementedErrorBuilder(loc)},
      {error::UNKNOWN, UnknownErrorBuilder(loc)},
  };

  for (const auto& test : canonical_errors) {
    SCOPED_TRACE(absl::StrCat("error::", Code_Name(test.code)));

    // Ensure that the creator does, in fact, create status objects in the
    // canonical space, with the expected error code and message.
    std::string message =
        absl::StrCat("error code ", test.code, " test message");
    absl::Status status = StatusBuilder(test.builder) << message;
    EXPECT_EQ(CanonicalErrorSpace(), ::util::RetrieveErrorSpace(status));
    EXPECT_EQ(test.code, ::util::RetrieveErrorCode(status));
    EXPECT_EQ(message, status.message());
    EXPECT_EQ(static_cast<::util::error::Code>(status.code()), test.code);
  }
}

volatile bool force_failure = false;

ABSL_ATTRIBUTE_NOINLINE bool BoolMaybeFail() { return !force_failure; }

static void BM_BoolCheck(benchmark::State& state) {
  force_failure = (state.range(0) != 0);
  for (auto _ : state) {
    if (!BoolMaybeFail()) {
      VLOG(1) << "Failure";
    }
  }
}
BENCHMARK(BM_BoolCheck)->Arg(0)->Arg(1);

ABSL_ATTRIBUTE_NOINLINE absl::Status MaybeFail() {
  return force_failure ? absl::InternalError("test") : absl::OkStatus();
}

static void BM_StatusCheck(benchmark::State& state) {
  force_failure = (state.range(0) != 0);
  for (auto _ : state) {
    if (auto s = MaybeFail(); !s.ok()) {
      VLOG(1) << "Failure";
    }
  }
}
BENCHMARK(BM_StatusCheck)->Arg(0)->Arg(1);

static void BM_StatusBuilder(benchmark::State& state) {
  force_failure = (state.range(0) != 0);
  const absl::SourceLocation kLocation = absl::SourceLocation::current();
  for (auto _ : state) {
    const StatusBuilder builder(MaybeFail(), kLocation);
    benchmark::DoNotOptimize(builder);
  }
}
BENCHMARK(BM_StatusBuilder)->Arg(0)->Arg(1);

absl::Status ABSL_ATTRIBUTE_NOINLINE ReturnIfErrorIter() {
  ABSL_RETURN_IF_ERROR(MaybeFail());
  return absl::OkStatus();
}

static void BM_ReturnIfError(benchmark::State& state) {
  force_failure = (state.range(0) != 0);
  for (auto _ : state) {
    ReturnIfErrorIter().IgnoreError();
  }
}
BENCHMARK(BM_ReturnIfError)->Arg(0)->Arg(1);

ABSL_ATTRIBUTE_NOINLINE absl::StatusOr<std::unique_ptr<int>>
MaybeFailWithPtr() {
  if (force_failure) {
    return absl::InternalError("test");
  } else {
    return std::make_unique<int>(10);
  }
}

ABSL_ATTRIBUTE_NOINLINE absl::Status AssignOrReturnIter() {
  ABSL_ASSIGN_OR_RETURN(auto x, MaybeFailWithPtr());
  return absl::OkStatus();
}

static void BM_AssignOrReturn(benchmark::State& state) {
  force_failure = (state.range(0) != 0);
  for (auto _ : state) {
    AssignOrReturnIter().IgnoreError();
  }
}
BENCHMARK(BM_AssignOrReturn)->Arg(0)->Arg(1);

void BM_StatusOrInt_CtorStatusBuilder(benchmark::State& state) {
  for (auto _ : state) {
    absl::StatusOr<int> status(
        util::UnknownErrorBuilder(absl::SourceLocation::current())
        << "This string is 28 characters");
    benchmark::DoNotOptimize(status);
  }
}
BENCHMARK(BM_StatusOrInt_CtorStatusBuilder);

void BM_StatusOrString_CtorStatusBuilder(benchmark::State& state) {
  for (auto _ : state) {
    absl::StatusOr<std::string> status(
        util::UnknownErrorBuilder(absl::SourceLocation::current())
        << "This string is 28 characters");
    benchmark::DoNotOptimize(status);
  }
}
BENCHMARK(BM_StatusOrString_CtorStatusBuilder);

// Place #line overrides at the end so that profiles of earlier benchmark code
// reference real line numbers.

#line 1337 "/foo/secret.cc"
const absl::SourceLocation Locs::kSecret = absl::SourceLocation::current();
#line 1234 "/tmp/level0.cc"
const absl::SourceLocation Locs::kLevel0 = absl::SourceLocation::current();
#line 1234 "/tmp/level1.cc"
const absl::SourceLocation Locs::kLevel1 = absl::SourceLocation::current();
#line 1234 "/tmp/level2.cc"
const absl::SourceLocation Locs::kLevel2 = absl::SourceLocation::current();
#line 1337 "/foo/foo.cc"
const absl::SourceLocation Locs::kFoo = absl::SourceLocation::current();
#line 1337 "/bar/baz.cc"
const absl::SourceLocation Locs::kBar = absl::SourceLocation::current();

}  // namespace util
