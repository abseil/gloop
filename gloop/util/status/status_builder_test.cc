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
#include "gloop/util/status/status.h"
#include "gmock/gmock.h"
#include "google/protobuf/bridge/message_set.pb.h"
#include "gtest/gtest.h"
#include "util/task/posixerrorspace.h"
#include "util/task/status_macros.h"

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
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Pointee;
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

class StatusBuilderTest : public ::testing::Test {
 protected:
  void TestRepCopyCtor() {
    StatusBuilder::Rep r1(absl::OkStatus());
    EXPECT_FALSE(r1.stream.has_value());
    r1.InitStream();
    EXPECT_TRUE(r1.stream.has_value());
    StatusBuilder::Rep r2(r1);
    EXPECT_TRUE(r2.stream.has_value());
  }
};

TEST_F(StatusBuilderTest, Size) {
  EXPECT_LE(sizeof(StatusBuilder), 40)
      << "Relax this test with caution and thorough testing. If StatusBuilder "
         "is too large it can potentially blow stacks, especially in debug "
         "builds. See the comments for StatusBuilder::Rep.";
}

TEST_F(StatusBuilderTest, Ctors) {
  EXPECT_EQ(ToStatus(StatusBuilder(kZomg) << "zomg"),
            ::util::MakeStatus(TestSpace::Get(), kZomg, "zomg"));
  EXPECT_EQ(ToStatus(StatusBuilder(TestSpace::Get(), kZomg) << "zomg"),
            ::util::MakeStatus(TestSpace::Get(), kZomg, "zomg"));
  EXPECT_EQ(ToStatus(StatusBuilder(PosixErrorSpace(), ENOSYS) << "nope"),
            PosixErrorToStatus(ENOSYS, "nope"));
}

TEST_F(StatusBuilderTest, RepCopyCtor) { TestRepCopyCtor(); }

TEST_F(StatusBuilderTest, ExplicitSourceLocation) {
  const absl::SourceLocation kLocation = absl::SourceLocation::current();

  {
    const StatusBuilder builder(absl::OkStatus(), kLocation);
    EXPECT_THAT(builder.source_location().file_name(),
                Eq(kLocation.file_name()));
    EXPECT_THAT(builder.source_location().line(), Eq(kLocation.line()));
  }
}

TEST_F(StatusBuilderTest, ImplicitSourceLocation) {
  const StatusBuilder builder(absl::OkStatus());
  auto loc = absl::SourceLocation::current();
  EXPECT_THAT(builder.source_location().file_name(),
              AnyOf(Eq(loc.file_name()), Eq("<source_location>")));
  EXPECT_THAT(builder.source_location().line(),
              AnyOf(Eq(1), Eq(loc.line() - 1)));
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

TEST_F(StatusBuilderTest, GetPreviousSourceLocations) {
  const absl::SourceLocation loc0 = absl::SourceLocation::current();
  absl::Status status = absl::InvalidArgumentError("hi", loc0);
  const absl::SourceLocation loc1 = absl::SourceLocation::current();
  status.AddSourceLocation(loc1);
  const absl::SourceLocation loc2 = absl::SourceLocation::current();
  status.AddSourceLocation(loc2);

  // The builder's location is not included.
  const StatusBuilder builder(status);
  EXPECT_THAT(builder.GetPreviousSourceLocations(),
              ElementsAre(SourceLocationIs(loc0), SourceLocationIs(loc1),
                          SourceLocationIs(loc2)));
}

TEST_F(StatusBuilderTest, EmptyGetPreviousSourceLocationsForNewFromStatusCode) {
  const StatusBuilder builder = InvalidArgumentErrorBuilder();
  EXPECT_THAT(builder.GetPreviousSourceLocations(), IsEmpty());
}

TEST_F(StatusBuilderTest, ErrorCode) {
  // OK
  {
    const StatusBuilder builder(absl::OkStatus());
    EXPECT_TRUE(builder.ok());
    EXPECT_THAT(builder.CanonicalCode(), Eq(error::OK));
    EXPECT_TRUE(builder.Is(error::OK));
    EXPECT_TRUE(builder.Is(absl::StatusCode::kOk));
    EXPECT_TRUE(builder.Is(kCustomOK));
    EXPECT_TRUE(builder.Is(PosixErrorSpace(), 0));
    EXPECT_FALSE(builder.Is(kZomg));
    EXPECT_TRUE(builder.Is(CanonicalErrorSpace()));
    EXPECT_FALSE(builder.Is(TestSpace::Get()));
    EXPECT_FALSE(builder.Is(PosixErrorSpace()));
  }

  // Custom OK immediately gets converted into a canonical OK, but it still
  // "Is" the custom OK as well.
  {
    const StatusBuilder builder(kCustomOK);
    EXPECT_TRUE(builder.ok());
    EXPECT_THAT(builder.CanonicalCode(), Eq(error::OK));
    EXPECT_THAT(builder.code(), Eq(absl::StatusCode::kOk));
    EXPECT_TRUE(builder.Is(error::OK));
    EXPECT_TRUE(builder.Is(absl::StatusCode::kOk));
    EXPECT_TRUE(builder.Is(kCustomOK));
    EXPECT_TRUE(builder.Is(PosixErrorSpace(), 0));
    EXPECT_FALSE(builder.Is(kZomg));
    EXPECT_TRUE(builder.Is(CanonicalErrorSpace()));
    EXPECT_FALSE(builder.Is(TestSpace::Get()));
    EXPECT_FALSE(builder.Is(PosixErrorSpace()));
  }

  // Non-OK canonical code
  {
    const StatusBuilder builder(error::INVALID_ARGUMENT);
    EXPECT_FALSE(builder.ok());
    EXPECT_THAT(builder.CanonicalCode(), Eq(error::INVALID_ARGUMENT));
    EXPECT_THAT(builder.code(), Eq(absl::StatusCode::kInvalidArgument));
    EXPECT_TRUE(builder.Is(error::INVALID_ARGUMENT));
    EXPECT_TRUE(builder.Is(absl::StatusCode::kInvalidArgument));
    EXPECT_FALSE(builder.Is(kCustomOK));
    EXPECT_FALSE(builder.Is(kZomg));
    EXPECT_FALSE(builder.Is(PosixErrorSpace(), 0));
    EXPECT_FALSE(builder.Is(PosixErrorSpace(),
                            static_cast<int>(error::INVALID_ARGUMENT)));
    EXPECT_TRUE(builder.Is(CanonicalErrorSpace()));
    EXPECT_FALSE(builder.Is(TestSpace::Get()));
    EXPECT_FALSE(builder.Is(PosixErrorSpace()));
    // Is() is not allowed to be called on a canonical code, so we cannot do a
    // positive test for Is() here.
  }

  // Custom error space
  {
    const StatusBuilder builder(kZomg);
    EXPECT_FALSE(builder.ok());
    EXPECT_THAT(builder.CanonicalCode(), Eq(error::UNKNOWN));
    EXPECT_FALSE(builder.Is(error::UNKNOWN));
    EXPECT_FALSE(builder.Is(absl::StatusCode::kUnknown));
    EXPECT_FALSE(builder.Is(kCustomOK));
    EXPECT_TRUE(builder.Is(kZomg));
    EXPECT_FALSE(builder.Is(CanonicalErrorSpace()));
    EXPECT_TRUE(builder.Is(TestSpace::Get()));
    EXPECT_FALSE(builder.Is(PosixErrorSpace()));
  }
  // Custom error space without ADL
  {
    const StatusBuilder builder(PosixErrorSpace(), EINVAL);
    EXPECT_FALSE(builder.ok());
    EXPECT_FALSE(builder.Is(kCustomOK));
    EXPECT_FALSE(builder.Is(kZomg));
    EXPECT_TRUE(builder.Is(PosixErrorSpace(), EINVAL));
    EXPECT_FALSE(builder.Is(CanonicalErrorSpace()));
    EXPECT_FALSE(builder.Is(TestSpace::Get()));
    EXPECT_TRUE(builder.Is(PosixErrorSpace()));
  }
}

TEST_F(StatusBuilderTest, StatusCode) {
  // OK
  {
    const StatusBuilder builder(absl::StatusCode::kOk);
    EXPECT_TRUE(builder.ok());
    EXPECT_THAT(builder.code(), Eq(absl::StatusCode::kOk));
    EXPECT_TRUE(builder.Is(error::OK));
    EXPECT_TRUE(builder.Is(absl::StatusCode::kOk));
    EXPECT_TRUE(builder.Is(kCustomOK));
    EXPECT_TRUE(builder.Is(PosixErrorSpace(), 0));
    EXPECT_FALSE(builder.Is(kZomg));
  }
  // Non-OK code
  {
    const StatusBuilder builder(absl::StatusCode::kInvalidArgument);
    EXPECT_FALSE(builder.ok());
    EXPECT_THAT(builder.code(), Eq(absl::StatusCode::kInvalidArgument));
    EXPECT_TRUE(builder.Is(error::INVALID_ARGUMENT));
    EXPECT_TRUE(builder.Is(absl::StatusCode::kInvalidArgument));
    EXPECT_FALSE(builder.Is(kCustomOK));
    EXPECT_FALSE(builder.Is(kZomg));
    EXPECT_FALSE(builder.Is(PosixErrorSpace(), 0));
    EXPECT_FALSE(builder.Is(PosixErrorSpace(),
                            static_cast<int>(error::INVALID_ARGUMENT)));
  }
}

TEST_F(StatusBuilderTest, OkIgnoresStuff) {
  EXPECT_THAT(ToStatus(StatusBuilder(absl::OkStatus(), absl::SourceLocation())
                       << "booyah"),
              Eq(absl::OkStatus()));
  EXPECT_THAT(ToStatus(StatusBuilder(absl::OkStatus(), absl::SourceLocation())
                           .SetPayload("url", absl::Cord("payload"))
                       << "aliens"),
              Eq(absl::OkStatus()));
}

TEST_F(StatusBuilderTest, Streaming) {
  EXPECT_THAT(
      ToStatus(StatusBuilder(absl::CancelledError(), absl::SourceLocation())
               << "booyah"),
      Eq(absl::CancelledError("booyah")));
  EXPECT_THAT(
      ToStatus(
          StatusBuilder(absl::AbortedError("hello"), absl::SourceLocation())
          << "world"),
      Eq(absl::AbortedError("hello; world")));
  EXPECT_THAT(ToStatus(StatusBuilder(PosixErrorToStatus(ENOSYS, "enosys"),
                                     absl::SourceLocation())
                       << "punk!"),
              Eq(PosixErrorToStatus(ENOSYS, "enosys; punk!")));

  // Explicitly set canonical codes should be preserved.
  {
    absl::Status original_status = PosixErrorToStatus(ENOSYS, "enosys");
    SetCanonicalCode(absl::StatusCode::kPermissionDenied, &original_status);

    const absl::Status transformed =
        (StatusBuilder(original_status, absl::SourceLocation()) << "foo");

    EXPECT_EQ(absl::StatusCode::kPermissionDenied, transformed.code());
  }
}

TEST_F(StatusBuilderTest, PrependLvalue) {
  {
    StatusBuilder builder(absl::CancelledError(), absl::SourceLocation());
    EXPECT_THAT(ToStatus(builder.SetPrepend() << "booyah"),
                Eq(absl::CancelledError("booyah")));
  }
  {
    StatusBuilder builder(absl::AbortedError(" hello"), absl::SourceLocation());
    EXPECT_THAT(ToStatus(builder.SetPrepend() << "world"),
                Eq(absl::AbortedError("world hello")));
  }

  // Explicitly set canonical codes should be preserved.
  {
    absl::Status original_status = PosixErrorToStatus(ENOSYS, "enosys");
    SetCanonicalCode(absl::StatusCode::kPermissionDenied, &original_status);

    StatusBuilder builder(original_status, absl::SourceLocation());
    const absl::Status transformed = builder.SetPrepend() << "foo";

    EXPECT_EQ(absl::StatusCode::kPermissionDenied, transformed.code());
  }
}

TEST_F(StatusBuilderTest, PrependRvalue) {
  EXPECT_THAT(
      ToStatus(StatusBuilder(absl::CancelledError(), absl::SourceLocation())
                   .SetPrepend()
               << "booyah"),
      Eq(absl::CancelledError("booyah")));
  EXPECT_THAT(ToStatus(StatusBuilder(absl::AbortedError(" hello"),
                                     absl::SourceLocation())
                           .SetPrepend()
                       << "world"),
              Eq(absl::AbortedError("world hello")));

  // Explicitly set canonical codes should be preserved.
  {
    absl::Status original_status = PosixErrorToStatus(ENOSYS, "enosys");
    SetCanonicalCode(absl::StatusCode::kPermissionDenied, &original_status);

    const absl::Status transformed =
        (StatusBuilder(original_status, absl::SourceLocation()).SetPrepend()
         << "foo");

    EXPECT_EQ(absl::StatusCode::kPermissionDenied, transformed.code());
  }
}

TEST_F(StatusBuilderTest, AppendLvalue) {
  {
    StatusBuilder builder(absl::CancelledError(), absl::SourceLocation());
    EXPECT_THAT(ToStatus(builder.SetAppend() << "booyah"),
                Eq(absl::CancelledError("booyah")));
  }
  {
    StatusBuilder builder(absl::AbortedError("hello"), absl::SourceLocation());
    EXPECT_THAT(ToStatus(builder.SetAppend() << " world"),
                Eq(absl::AbortedError("hello world")));
  }

  // Explicitly set canonical codes should be preserved.
  {
    absl::Status original_status = PosixErrorToStatus(ENOSYS, "enosys");
    SetCanonicalCode(absl::StatusCode::kPermissionDenied, &original_status);

    StatusBuilder builder(original_status, absl::SourceLocation());
    const absl::Status transformed = builder.SetAppend() << "foo";

    EXPECT_EQ(absl::StatusCode::kPermissionDenied, transformed.code());
  }
}

TEST_F(StatusBuilderTest, AppendRvalue) {
  EXPECT_THAT(
      ToStatus(StatusBuilder(absl::CancelledError(), absl::SourceLocation())
                   .SetAppend()
               << "booyah"),
      Eq(absl::CancelledError("booyah")));
  EXPECT_THAT(ToStatus(StatusBuilder(absl::AbortedError("hello"),
                                     absl::SourceLocation())
                           .SetAppend()
                       << " world"),
              Eq(absl::AbortedError("hello world")));

  // Explicitly set canonical codes should be preserved.
  {
    absl::Status original_status = PosixErrorToStatus(ENOSYS, "enosys");
    SetCanonicalCode(absl::StatusCode::kPermissionDenied, &original_status);

    const absl::Status transformed =
        (StatusBuilder(original_status, absl::SourceLocation()).SetAppend()
         << "foo");

    EXPECT_EQ(absl::StatusCode::kPermissionDenied, transformed.code());
  }
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

TEST_F(StatusBuilderTest, WithRvalueRef) {
  auto policy = [](StatusBuilder sb) { return sb << "policy"; };
  EXPECT_THAT(ToStatus(StatusBuilder(absl::AbortedError("hello"),
                                     absl::SourceLocation())
                           .With(policy)),
              Eq(absl::AbortedError("hello; policy")));
}

TEST_F(StatusBuilderTest, WithRef) {
  auto policy = [](StatusBuilder sb) { return sb << "policy"; };
  StatusBuilder sb(absl::AbortedError("zomg"), absl::SourceLocation());
  EXPECT_THAT(ToStatus(sb.With(policy)),
              Eq(absl::AbortedError("zomg; policy")));
}

TEST_F(StatusBuilderTest, WithTypeChange) {
  auto policy = [](StatusBuilder sb) -> std::string {
    return sb.ok() ? "true" : "false";
  };
  EXPECT_EQ(StatusBuilder(absl::CancelledError(), absl::SourceLocation())
                .With(policy),
            "false");
  EXPECT_EQ(
      StatusBuilder(absl::OkStatus(), absl::SourceLocation()).With(policy),
      "true");
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

TEST_F(StatusBuilderTest, WithMoveOnlyAdaptor) {
  StatusBuilder sb(absl::AbortedError("zomg"), absl::SourceLocation());
  EXPECT_THAT(sb.With(MoveOnlyAdaptor{std::make_unique<int>(100)}),
              Pointee(100));
  EXPECT_THAT(StatusBuilder(absl::AbortedError("zomg"), absl::SourceLocation())
                  .With(MoveOnlyAdaptor{std::make_unique<int>(100)}),
              Pointee(100));
}

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
    auto builder = StatusBuilder(TestSpace::Get(), kZomg) << "zomg";
    EXPECT_EQ(ToStringViaStream(status), ToStringViaStream(builder));
    EXPECT_EQ(
        ToStringViaStream(status),
        ToStringViaStream(StatusBuilder(TestSpace::Get(), kZomg) << "zomg"));
  }
  {
    absl::Status status = PosixErrorToStatus(ENOSYS, "nope");
    auto builder = StatusBuilder(PosixErrorSpace(), ENOSYS) << "nope";
    EXPECT_EQ(
        ToStringViaStream(status),
        ToStringViaStream(StatusBuilder(PosixErrorSpace(), ENOSYS) << "nope"));
  }
}

struct HasAbslStringify {
  absl::string_view message;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const HasAbslStringify& o) {
    sink.Append(o.message);
  }
};
TEST_F(StatusBuilderTest, AbslStringify) {
  absl::Status status = util::MakeStatus(TestSpace::Get(), kZomg, "zomg");
  auto builder = StatusBuilder(TestSpace::Get(), kZomg)
                 << HasAbslStringify{"zomg"};
  EXPECT_EQ(ToStringViaStream(status), ToStringViaStream(builder));
  EXPECT_EQ(ToStringViaStream(status),
            ToStringViaStream(StatusBuilder(TestSpace::Get(), kZomg)
                              << HasAbslStringify{"zomg"}));
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
    auto builder = StatusBuilder(TestSpace::Get(), kZomg) << "zomg";
    EXPECT_EQ(ToStringViaStream(status), ToStringViaStream(builder));
    EXPECT_EQ(ToStringViaStream(status),
              (StatusBuilder(TestSpace::Get(), kZomg) << "zomg").ToString());
  }
  {
    absl::Status status = PosixErrorToStatus(ENOSYS, "nope");
    auto builder = StatusBuilder(PosixErrorSpace(), ENOSYS) << "nope";
    EXPECT_EQ(ToStringViaStream(status),
              (StatusBuilder(PosixErrorSpace(), ENOSYS) << "nope").ToString());
  }
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
  builder << "hello world!";
  builder.LogError();
  builder.AlsoOutputToSink(&log_sink);

  absl::Status status = absl::CancelledError("hello world!");

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

TEST(WithExtraMessagePolicyTest, AppendsToExtraMessage) {
  // The policy simply calls operator<< on the builder; the following examples
  // demonstrate that, without duplicating all of the above tests.
  EXPECT_THAT(ToStatus(StatusBuilder(absl::AbortedError("hello"),
                                     absl::SourceLocation())
                           .With(ExtraMessage("world"))),
              Eq(absl::AbortedError("hello; world")));
  EXPECT_THAT(ToStatus(StatusBuilder(absl::AbortedError("hello"),
                                     absl::SourceLocation())
                           .With(ExtraMessage() << "world")),
              Eq(absl::AbortedError("hello; world")));
  EXPECT_THAT(ToStatus(StatusBuilder(absl::AbortedError("hello"),
                                     absl::SourceLocation())
                           .With(ExtraMessage("world"))
                           .With(ExtraMessage("!"))),
              Eq(absl::AbortedError("hello; world!")));
  EXPECT_THAT(ToStatus(StatusBuilder(absl::AbortedError("hello"),
                                     absl::SourceLocation())
                           .With(ExtraMessage("world, "))
                           .SetPrepend()),
              Eq(absl::AbortedError("world, hello")));
  EXPECT_THAT(ToStatus(StatusBuilder(absl::AbortedError("hello"),
                                     absl::SourceLocation())
                           .With(ExtraMessage() << HasAbslStringify{"world"})),
              Eq(absl::AbortedError("hello; world")));

  // The above examples use temporary StatusBuilder rvalues; verify things also
  // work fine when StatusBuilder is an lvalue.
  StatusBuilder builder(absl::AbortedError("hello"), absl::SourceLocation());
  EXPECT_THAT(
      ToStatus(builder.With(ExtraMessage("world")).With(ExtraMessage("!"))),
      Eq(absl::AbortedError("hello; world!")));
}

TEST(WithExtraMessagePolicyTest, ExtraMessageMoveConstructor) {
  auto policy = util::ExtraMessage() << "doing foo";

  EXPECT_THAT(StatusBuilder(absl::AbortedError("taco")).With(std::move(policy)),
              StatusIs(absl::StatusCode::kAborted, "taco; doing foo"));
}

TEST(WithExtraMessagePolicyTest,
     ExtraMessageStreamOperatorPreservesRvalueness) {
  static_assert(
      std::is_same_v<ExtraMessage&&, decltype(ExtraMessage() << "foo")>);
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

TEST_F(StatusBuilderTest, StatusSourceLocationChaining) {
  {
    absl::Status src = absl::OkStatus();
    CheckSourceLocation(src);
    CheckSourceLocation(ToStatus(StatusBuilder(src, absl::SourceLocation())));
    CheckSourceLocation(
        ToStatus(StatusBuilder(src, absl::SourceLocation::current())));
    CheckSourceLocation(
        ToStatus(StatusBuilder(src, absl::SourceLocation::current()) << "hmm"));
  }
  {
    absl::Status src = absl::Status(absl::StatusCode::kCancelled, "");
    CheckSourceLocation(src);
    CheckSourceLocation(ToStatus(StatusBuilder(src, absl::SourceLocation())));
    CheckSourceLocation(
        ToStatus(StatusBuilder(src, absl::SourceLocation::current())));
    CheckSourceLocation(
        ToStatus(StatusBuilder(src, absl::SourceLocation::current()) << ""));
    CheckSourceLocation(
        ToStatus(StatusBuilder(src, absl::SourceLocation::current()) << "hmm"),
        {__builtin_LINE() - 1});
  }
  {
    absl::Status src = absl::Status(absl::StatusCode::kCancelled, "msg",
                                    absl::SourceLocation());
    CheckSourceLocation(src);
    CheckSourceLocation(ToStatus(StatusBuilder(src, absl::SourceLocation())));
    CheckSourceLocation(
        ToStatus(StatusBuilder(src, absl::SourceLocation::current())),
        {__builtin_LINE() - 1});
    CheckSourceLocation(
        ToStatus(StatusBuilder(src, absl::SourceLocation::current()) << "hmm"),
        {__builtin_LINE() - 1});
  }
  {
    absl::Status src = absl::Status(absl::StatusCode::kCancelled, "msg");
    int src_line = __builtin_LINE() - 1;
    CheckSourceLocation(src, {src_line});
    CheckSourceLocation(ToStatus(StatusBuilder(src, absl::SourceLocation())),
                        {src_line});
    CheckSourceLocation(
        ToStatus(StatusBuilder(src, absl::SourceLocation::current())),
        {src_line, __builtin_LINE() - 1});
    CheckSourceLocation(
        ToStatus(StatusBuilder(src, absl::SourceLocation::current()) << "hmm"),
        {src_line, __builtin_LINE() - 1});
  }
}

TEST_F(StatusBuilderTest, SetErrorCode) {
  StatusBuilder builder;
  builder.SetCode(absl::StatusCode::kResourceExhausted);
  LOG(INFO) << "Builder code: " << builder;
  EXPECT_FALSE(builder.ok());
  EXPECT_EQ(builder.code(), absl::StatusCode::kResourceExhausted);
}

TEST_F(StatusBuilderTest, BuilderToStatusOrStatusShouldGiveErrorStatusOr) {
  absl::StatusOr<absl::Status> value = StatusBuilder(absl::CancelledError());
  ASSERT_FALSE(value.ok());
  EXPECT_THAT(value.status(), StatusIs(absl::StatusCode::kCancelled));
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
  RETURN_IF_ERROR(MaybeFail());
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
  ASSIGN_OR_RETURN(auto x, MaybeFailWithPtr());
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
