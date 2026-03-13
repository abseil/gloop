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

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/source_location.h"
#include "gloop/util/task/error_space.h"
#include "gloop/util/task/status.h"
#include "gtest/gtest.h"

namespace util {
namespace {

// A map from error codes in the "test" space to those in the canonical
// space. This keeps test codes in the same numeric space as canonical
// codes, which avoids unexpected "UNKNOWN" codes when converting from the
// test space to the canonical space.
std::map<int, error::Code> BuildTestErrorMap() {
  // Build a list of the canonical error codes that actually represent
  // errors (i.e. everything but OK).
  std::vector<int> error_codes;
  for (int i = 1; i <= error::Code_MAX; ++i) {
    if (error::Code_IsValid(i)) {
      error_codes.push_back(i);
    }
  }

  std::map<int, error::Code> canonical_codes;
  // Build a map from test codes to canonical codes. The canonical code for
  // a given test code is ((test_code + 1) % MAX_CODE).
  for (int i = 0; i < error_codes.size(); ++i) {
    int test_code = error_codes[i];
    int canonical_code = error_codes[(i + 1) % error_codes.size()];
    canonical_codes[test_code] = static_cast<error::Code>(canonical_code);
  }
  return canonical_codes;
}

// Given a canonical error code, returns the equivalent code from the "test"
// error space.
int GetTestCodeForCanonical(error::Code code) {
  for (const auto& value : BuildTestErrorMap()) {
    if (value.second == code) {
      return value.first;
    }
  }
  LOG(FATAL) << "Unable to find test code for canonical code: " << code;
}

// An error space used only by these unit tests.
class TestErrorSpace : public ErrorSpaceImpl<TestErrorSpace> {
 public:
  static absl::string_view space_name() { return "test"; }

  static std::string code_to_string(int code) {
    return absl::StrCat("TEST_CODE_", code);
  }

  static absl::StatusCode canonical_code(int code) {
    auto canonical_codes = BuildTestErrorMap();
    auto found = canonical_codes.find(code);
    if (found != canonical_codes.end()) {
      return static_cast<absl::StatusCode>(found->second);
    }
    return absl::StatusCode::kUnknown;
  }
};

// This structure holds the details for testing a single canonical error code,
// its creator, and its classifier.
struct CanonicalErrorTest {
  error::Code code;
  absl::Status (*creator)(absl::string_view, absl::SourceLocation);
  bool (*classifier)(const absl::Status&);
};

constexpr CanonicalErrorTest kCanonicalErrorTests[]{
    {error::ABORTED, absl::AbortedError, absl::IsAborted},
    {error::ALREADY_EXISTS, absl::AlreadyExistsError, absl::IsAlreadyExists},
    {error::CANCELLED, absl::CancelledError, absl::IsCancelled},
    {error::DATA_LOSS, absl::DataLossError, absl::IsDataLoss},
    {error::DEADLINE_EXCEEDED, absl::DeadlineExceededError,
     absl::IsDeadlineExceeded},
    {error::FAILED_PRECONDITION, absl::FailedPreconditionError,
     absl::IsFailedPrecondition},
    {error::INTERNAL, absl::InternalError, absl::IsInternal},
    {error::INVALID_ARGUMENT, absl::InvalidArgumentError,
     absl::IsInvalidArgument},
    {error::NOT_FOUND, absl::NotFoundError, absl::IsNotFound},
    {error::OUT_OF_RANGE, absl::OutOfRangeError, absl::IsOutOfRange},
    {error::PERMISSION_DENIED, absl::PermissionDeniedError,
     absl::IsPermissionDenied},
    {error::UNAUTHENTICATED, absl::UnauthenticatedError,
     absl::IsUnauthenticated},
    {error::RESOURCE_EXHAUSTED, absl::ResourceExhaustedError,
     absl::IsResourceExhausted},
    {error::UNAVAILABLE, absl::UnavailableError, absl::IsUnavailable},
    {error::UNIMPLEMENTED, absl::UnimplementedError, absl::IsUnimplemented},
    {error::UNKNOWN, absl::UnknownError, absl::IsUnknown},
};

TEST(CanonicalErrorsTest, CreateAndClassify) {
  for (const auto& test : kCanonicalErrorTests) {
    // Ensure that the creator does, in fact, create status objects in the
    // canonical space, with the expected error code and message.
    std::string message =
        absl::StrCat("error code ", test.code, " test message");
    absl::Status status =
        test.creator(message, absl::SourceLocation::current());
    EXPECT_EQ(::util::CanonicalErrorSpace(),
              ::util::RetrieveErrorSpace(status));
    EXPECT_EQ(test.code, ::util::RetrieveErrorCode(status));
    EXPECT_EQ(message, status.message());

    // Ensure that the classifier returns true for a status produced by the
    // creator.
    EXPECT_TRUE(test.classifier(status));

    // Ensure that the classifier returns false for a canonical status
    // with a different code.
    status = absl::Status(static_cast<absl::StatusCode>(test.code + 1), "");
    EXPECT_FALSE(test.classifier(status));

    // Ensure that the classifier returns false for a status with the expected
    // code, but from another error space.
    status = ::util::MakeStatus(TestErrorSpace::Get(), test.code, "");
    EXPECT_FALSE(test.classifier(status));

    // Ensure that the classifier returns true for a status from another space
    // whose code maps to the canonical code.
    status = ::util::MakeStatus(TestErrorSpace::Get(),
                                GetTestCodeForCanonical(test.code), "");
    EXPECT_TRUE(test.classifier(status));
  }
}

TEST(CanonicalErrorsTest, NoMessageOverloads) {
  EXPECT_EQ(absl::CancelledError(), absl::CancelledError(""));
}

}  // namespace
}  // namespace util
