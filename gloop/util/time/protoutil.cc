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

#include "gloop/util/time/protoutil.h"

#include <cstdint>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "google/protobuf/duration.pb.h"
#include "google/protobuf/timestamp.pb.h"

namespace util_time {

namespace {

// Validation requirements documented at:
// http://<path>
absl::Status Validate(const google::protobuf::Duration& d) {
  const auto sec = d.seconds();
  const auto ns = d.nanos();
  if (sec < -315576000000 || sec > 315576000000) {
    return absl::InvalidArgumentError(absl::StrCat("seconds=", sec));
  }
  if (ns < -999999999 || ns > 999999999) {
    return absl::InvalidArgumentError(absl::StrCat("nanos=", ns));
  }
  if ((sec < 0 && ns > 0) || (sec > 0 && ns < 0)) {
    return absl::InvalidArgumentError("sign mismatch");
  }
  return absl::OkStatus();
}

// Validation requirements documented at:
// http://<path>
absl::Status Validate(const google::protobuf::Timestamp& t) {
  const auto sec = t.seconds();
  const auto ns = t.nanos();
  // sec must be [0001-01-01T00:00:00Z, 9999-12-31T23:59:59.999999999Z]
  if (sec < -62135596800 || sec > 253402300799) {
    return absl::InvalidArgumentError(absl::StrCat("seconds=", sec));
  }
  if (ns < 0 || ns > 999999999) {
    return absl::InvalidArgumentError(absl::StrCat("nanos=", ns));
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<google::protobuf::Duration> EncodeGoogleApiProto(
    absl::Duration d) {
  google::protobuf::Duration proto;
  absl::Status status = EncodeGoogleApiProto(d, &proto);
  if (!status.ok()) return status;
  return proto;
}

absl::Status EncodeGoogleApiProto(absl::Duration d,
                                  google::protobuf::Duration* proto) {
  // s and n may both be negative, per the Duration proto spec.
  const int64_t s = absl::IDivDuration(d, absl::Seconds(1), &d);
  const int64_t n = absl::IDivDuration(d, absl::Nanoseconds(1), &d);
  proto->set_seconds(s);
  proto->set_nanos(n);
  return Validate(*proto);
}

absl::StatusOr<google::protobuf::Timestamp> EncodeGoogleApiProto(absl::Time t) {
  google::protobuf::Timestamp proto;
  absl::Status status = EncodeGoogleApiProto(t, &proto);
  if (!status.ok()) return status;
  return proto;
}

absl::Status EncodeGoogleApiProto(absl::Time t,
                                  google::protobuf::Timestamp* proto) {
  const int64_t s = absl::ToUnixSeconds(t);
  proto->set_seconds(s);
  proto->set_nanos((t - absl::FromUnixSeconds(s)) / absl::Nanoseconds(1));
  return Validate(*proto);
}

absl::StatusOr<absl::Duration> DecodeGoogleApiProto(
    const google::protobuf::Duration& proto) {
  absl::Status status = Validate(proto);
  if (!status.ok()) return status;
  return absl::Seconds(proto.seconds()) + absl::Nanoseconds(proto.nanos());
}

absl::StatusOr<absl::Time> DecodeGoogleApiProto(
    const google::protobuf::Timestamp& proto) {
  absl::Status status = Validate(proto);
  if (!status.ok()) return status;
  return absl::FromUnixSeconds(proto.seconds()) +
         absl::Nanoseconds(proto.nanos());
}

}  // namespace util_time
