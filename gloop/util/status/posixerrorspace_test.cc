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

#include "gloop/util/status/posixerrorspace.h"

#include <errno.h>
#include <stddef.h>

#include <string>

#include "absl/status/status.h"
#include "absl/types/source_location.h"
#include "absl/types/span.h"
#include "gloop/util/status/status.h"
#include "gtest/gtest.h"

namespace util {

TEST(PosixErrorSpaceTest, TESTSingleton) {
  const ErrorSpace* error_space = PosixErrorSpace();
  EXPECT_TRUE(error_space != nullptr);
  EXPECT_EQ(error_space, ErrorSpace::Find(error_space->SpaceName()));
}

TEST(PosixErrorSpaceTest, TESTSetSpaceName) {
  const ErrorSpace* error_space = PosixErrorSpace();
  EXPECT_TRUE(error_space != nullptr);
  EXPECT_STREQ(error_space->SpaceName().data(), "util::PosixErrorSpace");
}

TEST(PosixErrorSpaceTest, TESTString) {
  const ErrorSpace* space = PosixErrorSpace();
  EXPECT_STREQ("Success", space->String(0).c_str());
  EXPECT_STREQ("Interrupted system call", space->String(EINTR).c_str());
  EXPECT_STREQ("Unknown error 41", space->String(41).c_str());
}

TEST(PosixErrorSpaceTest, TESTMakeStatus) {
  absl::Status status = PosixErrorToStatus(0, "");
  EXPECT_EQ(0, ::util::RetrieveErrorCode(status));
  EXPECT_EQ("", status.message());
  EXPECT_STREQ(::util::StatusToString(status).c_str(), "OK");

  status = PosixErrorToStatus(0, "Message");
  EXPECT_EQ(0, ::util::RetrieveErrorCode(status));
  EXPECT_EQ("", status.message());
  EXPECT_STREQ("OK", ::util::StatusToString(status).c_str());

  status = PosixErrorToStatus(EINTR, "");
  EXPECT_EQ(EINTR, ::util::RetrieveErrorCode(status));
  EXPECT_EQ("", status.message());
  EXPECT_STREQ("util::PosixErrorSpace",
               ::util::RetrieveErrorSpace(status)->SpaceName().data());
  EXPECT_STREQ("util::PosixErrorSpace::Interrupted system call: ",
               ::util::StatusToString(status).c_str());

  status = PosixErrorToStatus(EINTR, "Message");
  EXPECT_EQ(EINTR, ::util::RetrieveErrorCode(status));
  EXPECT_EQ("Message", status.message());
  EXPECT_STREQ("util::PosixErrorSpace",
               ::util::RetrieveErrorSpace(status)->SpaceName().data());
  EXPECT_STREQ("util::PosixErrorSpace::Interrupted system call: Message",
               ::util::StatusToString(status).c_str());

  // errno value of 41 is known on linux to not be defined.
  status = PosixErrorToStatus(41, "");
  EXPECT_EQ(41, ::util::RetrieveErrorCode(status));
  EXPECT_EQ("", status.message());
  EXPECT_STREQ("util::PosixErrorSpace",
               ::util::RetrieveErrorSpace(status)->SpaceName().data());
  EXPECT_STREQ("util::PosixErrorSpace::Unknown error 41: ",
               ::util::StatusToString(status).c_str());

  status = PosixErrorToStatus(41, "Message");
  EXPECT_EQ(41, ::util::RetrieveErrorCode(status));
  EXPECT_EQ("Message", status.message());
  EXPECT_STREQ("util::PosixErrorSpace",
               ::util::RetrieveErrorSpace(status)->SpaceName().data());
  EXPECT_STREQ("util::PosixErrorSpace::Unknown error 41: Message",
               ::util::StatusToString(status).c_str());
}

TEST(PosixErrorSpaceTest, TESTToCanonical) {
  // All OKs are equal.
  absl::Status status = PosixErrorToStatus(0, "OK");
  EXPECT_EQ(absl::OkStatus(), ::util::ToCanonical(status));

  // Do one conversion with a message embedded.
  status = PosixErrorToStatus(EINVAL, "Canned message");
  EXPECT_EQ(absl::Status(absl::StatusCode::kInvalidArgument, "Canned message"),
            ::util::ToCanonical(status));

  // And now we check only the error codes, using one
  // easy / (relatively) obvious mapping for each output
  // code, mostly just for coverage and as a sanity check.
  status = PosixErrorToStatus(EINVAL, "");
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            ::util::ToCanonical(status).code());

  status = PosixErrorToStatus(ETIMEDOUT, "");
  EXPECT_EQ(absl::StatusCode::kDeadlineExceeded,
            ::util::ToCanonical(status).code());

  status = PosixErrorToStatus(ENOENT, "");
  EXPECT_EQ(absl::StatusCode::kNotFound, ::util::ToCanonical(status).code());

  status = PosixErrorToStatus(EEXIST, "");
  EXPECT_EQ(absl::StatusCode::kAlreadyExists,
            ::util::ToCanonical(status).code());

  status = PosixErrorToStatus(EPERM, "");
  EXPECT_EQ(absl::StatusCode::kPermissionDenied,
            ::util::ToCanonical(status).code());

  status = PosixErrorToStatus(ENOTEMPTY, "");
  EXPECT_EQ(absl::StatusCode::kFailedPrecondition,
            ::util::ToCanonical(status).code());

  status = PosixErrorToStatus(ENOSPC, "");
  EXPECT_EQ(absl::StatusCode::kResourceExhausted,
            ::util::ToCanonical(status).code());

  status = PosixErrorToStatus(EOVERFLOW, "");
  EXPECT_EQ(absl::StatusCode::kOutOfRange, ::util::ToCanonical(status).code());

  status = PosixErrorToStatus(EPROTONOSUPPORT, "");
  EXPECT_EQ(absl::StatusCode::kUnimplemented,
            ::util::ToCanonical(status).code());

  status = PosixErrorToStatus(EAGAIN, "");
  EXPECT_EQ(absl::StatusCode::kUnavailable, ::util::ToCanonical(status).code());

  status = PosixErrorToStatus(EDEADLK, "");
  EXPECT_EQ(absl::StatusCode::kAborted, ::util::ToCanonical(status).code());

  status = PosixErrorToStatus(ECANCELED, "");
  EXPECT_EQ(absl::StatusCode::kCancelled, ::util::ToCanonical(status).code());

  status = PosixErrorToStatus(EL2HLT, "");
  EXPECT_EQ(absl::StatusCode::kUnknown, ::util::ToCanonical(status).code());
}

}  // namespace util
