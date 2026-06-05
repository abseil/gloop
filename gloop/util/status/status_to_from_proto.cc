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

#include <string>
#include <type_traits>

#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/types/source_location.h"
#include "gloop/util/status/status.h"

// This file is separate from status.cc, because it depends on
// NonMessageSetPayload::message_set_extension, which turns out to be expensive
// in terms of code size for some builds (see b/175215333). Keeping this code
// separate allows the linker to include the more commonly used functionality
// from status.cc without necessarily pulling in the code here.

#include "gloop/util/status/error_space.h"
#include "gloop/util/status/non_message_set_payload.pb.h"
#include "gloop/util/status/status.pb.h"
#include "gloop/util/status/status_internal.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/bridge/message_set.pb.h"

namespace {

// Constructs a Status via its 3-arg c'tor.  Unlike the actual constructor,
// this does not provide a default value for `loc`.
absl::Status MakeStatusInternal(absl::StatusCode code, absl::string_view msg,
                                absl::SourceLocation loc) {
  return absl::Status(code, msg, loc);
}

}  // namespace

namespace util {

void SaveStatusToProto(const absl::Status& s, util::StatusProto* proto) {
  InternalSaveStatusToProto(s, proto);

  if (HasPayload(s)) {
    proto->mutable_message_set()->MergeFrom(MakePayloadsSet(s));
  }
}

void InternalSaveStatusToProto(const absl::Status& s,
                               util::StatusProto* proto) {
  proto->Clear();
  const util::status_internal::ErrorSpaceAndCode space_and_code =
      util::status_internal::ErrorSpacePayload::Retrieve(s);
  int code = space_and_code.code;
  if (code == 0) return;

  absl::string_view space_name = space_and_code.GetErrorSpaceName();

  proto->set_code(code);
  proto->set_space(space_name);

  if (!space_and_code.MatchErrorSpace(GenericErrorSpace::Get())) {
    proto->set_canonical_code(s.raw_code());
  }

  absl::string_view msg = s.message();
  if (!msg.empty()) {
    proto->set_message(msg);
  }

  // Store non-MessageSet payloads as `google.protobuf.Any` in a unique entry
  // of MessageSet.
  s.ForEachPayload([&](absl::string_view type_url, const absl::Cord& payload) {
    // Skip `ErrorSpace` payload because it is already in `StatusProto`.
    if (type_url == status_internal::kErrorSpaceUrl) return;

    // Skip `MessageSet` payload as well.
    if (type_url == status_internal::kMessageSetUrl) return;

    google::protobuf::Any* any =
        proto->mutable_message_set()
            ->MutableExtension(
                util::NonMessageSetPayload::message_set_extension)
            ->add_payloads();
    any->set_type_url(type_url);
    any->set_value(std::string(payload));
  });
}

namespace {

template <typename T, typename Loc>
absl::Status MakeStatusFromProtoHelper(const T& proto, Loc loc) {
  if (proto.code() == 0) return absl::OkStatus();

  std::string message_buf;
  absl::string_view msg = proto.message();

  const ErrorSpace* space = ErrorSpace::Find(proto.space());

  absl::StatusCode canonical_code;
  if (space != GenericErrorSpace::Get() && proto.has_canonical_code()) {
    canonical_code = static_cast<absl::StatusCode>(proto.canonical_code());
  } else if (space == GenericErrorSpace::Get()) {
    canonical_code = static_cast<absl::StatusCode>(proto.code());
  } else if (space != nullptr) {
    canonical_code = space->CanonicalCode(proto.code());
  } else {
    canonical_code = absl::StatusCode::kUnknown;
  }

  if (space == nullptr) {
    space = GenericErrorSpace::Get();

    // An unknown space, use the generic error space for this status.
    message_buf = absl::StrCat("invalid status ", proto.space(),
                               "::", proto.code(), ": ", proto.message());
    msg = message_buf;

    if (canonical_code == absl::StatusCode::kOk) {
      canonical_code = absl::StatusCode::kUnknown;
    }
  }

  absl::Status ret;
  if (space == GenericErrorSpace::Get()) {
    ret = MakeStatusInternal(canonical_code, msg, loc);
  } else {
    ret = canonical_code == absl::StatusCode::kOk
              ? status_internal::MakeNonOkStatusWithOkCode(msg)
              : MakeStatusInternal(canonical_code, msg, loc);
    status_internal::ErrorSpacePayload::Set(space, proto.code(), &ret);
  }

  if (!proto.has_message_set()) return ret;

  constexpr auto& kNonMessageSetPayloadId =
      NonMessageSetPayload::message_set_extension;
  const auto& message_set = proto.message_set();

  bool has_non_message_set_payload =
      message_set.HasExtension(kNonMessageSetPayloadId);
  // Handle non MessageSet payloads first
  if (has_non_message_set_payload) {
    for (const google::protobuf::Any& p :
         message_set.GetExtension(kNonMessageSetPayloadId).payloads()) {
      if (status_internal::IsStackTracePayloadUrl(p.type_url())) {
        continue;
      }
      ret.SetPayload(p.type_url(), absl::Cord(p.value()));
    }
  }

  google::protobuf::bridge::MessageSet message_set_copy = message_set;
  message_set_copy.ClearExtension(kNonMessageSetPayloadId);
  if (message_set_copy.ByteSizeLong() > 0) {
    ret.SetPayload(status_internal::kMessageSetUrl,
                   message_set_copy.SerializePartialAsCord());
  }

  return ret;
}

}  // namespace

absl::Status MakeStatusFromProto(const util::StatusProto& proto,
                                 absl::SourceLocation loc) {
  // Delegate to a templated helper function so that we can take advantage of
  // constexpr if.
  return MakeStatusFromProtoHelper(proto, loc);
}

}  // namespace util
