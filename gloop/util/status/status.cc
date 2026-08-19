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

#include <stdint.h>
#include <stdio.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "absl/base/attributes.h"
#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/container/fixed_array.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_payload_printer.h"
#include "absl/strings/ascii.h"
#include "absl/strings/cord.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/source_location.h"
#include "gloop/util/coding/varint.h"
#include "gloop/util/status/error_space.h"
#include "gloop/util/status/status.pb.h"  // TO BE DELETED
#include "gloop/util/status/status_internal.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/bridge/message_set.pb.h"  // TO BE DELETED
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "google/protobuf/message_lite.h"
#include "google/protobuf/text_format.h"

namespace {

// Constructs a Status via its 3-arg c'tor.  Unlike the actual constructor,
// this does not provide a default value for `loc`.
absl::Status MakeStatusInternal(absl::StatusCode code, absl::string_view msg,
                                absl::SourceLocation loc) {
  return absl::Status(code, msg, loc);
}

// This is actually a LegacyUnredactedShortDebugString (b/339110033).
// TODO: b/284176655 - remove this
static std::string LegacyUnredactedShortDebugString(
    google::protobuf::Message& message) {
  std::string debug_string;
  static const absl::NoDestructor<google::protobuf::TextFormat::Printer>
      printer([] {
        google::protobuf::TextFormat::Printer p;
        p.SetSingleLineMode(true);
        p.SetExpandAny(true);
        return p;
      }());
  (void)printer->PrintToString(message, &debug_string);
  if (!debug_string.empty() && debug_string.back() == ' ') {
    debug_string.pop_back();
  }
  return debug_string;
}

[[maybe_unused]] static std::string LegacyUnredactedShortDebugString(
    google::protobuf::MessageLite& message) {
  return message.ShortDebugString();
}

}  // namespace

class absl::status_internal::StatusPrivateAccessor {
 public:
  static absl::Status MakeNonOkStatusWithOkCode(absl::string_view message) {
    return absl::Status::MakeNonOkStatusWithOkCode(message);
  }

  static absl::Status SetMessageWithoutPayloadsOrSourceLocations(
      const absl::Status& status, absl::string_view message) {
    if (status.ok()) {
      return status;
    }

    if (message.empty()) {
      return absl::Status(status.code(), message, absl::SourceLocation());
    }

    using StatusRep =
        std::remove_cv_t<std::remove_pointer_t<decltype(Status::RepToPointer(
            std::declval<uintptr_t>()))>>;
    StatusRep* rep;
    if (Status::IsInlined(status.rep_)) {
      rep = new StatusRep(Status::InlinedRepToCode(status.rep_), message,
                          nullptr);
    } else {
      rep = Status::RepToPointer(status.rep_)->Clone(message, false, false);
    }
    return absl::Status(Status::PointerToRep(rep));
  }
};

namespace util {

namespace error {

inline absl::string_view CodeEnumToString(error::Code code) {
  switch (code) {
    case OK:
      return "OK";
    case CANCELLED:
      return "CANCELLED";
    case UNKNOWN:
      return "UNKNOWN";
    case INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case DEADLINE_EXCEEDED:
      return "DEADLINE_EXCEEDED";
    case NOT_FOUND:
      return "NOT_FOUND";
    case ALREADY_EXISTS:
      return "ALREADY_EXISTS";
    case PERMISSION_DENIED:
      return "PERMISSION_DENIED";
    case UNAUTHENTICATED:
      return "UNAUTHENTICATED";
    case RESOURCE_EXHAUSTED:
      return "RESOURCE_EXHAUSTED";
    case FAILED_PRECONDITION:
      return "FAILED_PRECONDITION";
    case ABORTED:
      return "ABORTED";
    case OUT_OF_RANGE:
      return "OUT_OF_RANGE";
    case UNIMPLEMENTED:
      return "UNIMPLEMENTED";
    case INTERNAL:
      return "INTERNAL";
    case UNAVAILABLE:
      return "UNAVAILABLE";
    case DATA_LOSS:
      return "DATA_LOSS";
    case  //
        DO_NOT_USE_RESERVED_FOR_FUTURE_EXPANSION_USE_DEFAULT_IN_SWITCH_INSTEAD_:
      // We are not adding a default clause here, to explicitly make clang
      // detect the missing codes. This conversion method must stay in sync
      // with codes.proto.
      return "UNKNOWN";
  }

  // No default clause, clang will abort if a code is missing from
  // above switch.
  return "UNKNOWN";
}

}  // namespace error.

absl::string_view GenericErrorSpace::space_name() {
  return util::status_internal::kGenericErrorSpaceName;
}

std::string GenericErrorSpace::code_to_string(int code) {
  std::string status;
  if (code == 0) {
    status = "OK";
  } else if (error::Code_IsValid(code)) {
    // Lower-case the protocol-compiler assigned name for compatibility
    // with old behavior.
    status = absl::AsciiStrToLower(
        error::CodeEnumToString(static_cast<error::Code>(code)));
  } else {
    status = absl::StrCat(code);
  }
  return status;
}

absl::StatusCode GenericErrorSpace::canonical_code(int code) {
  return absl::status_internal::MapToLocalCode(code);
}

std::string ErrorSpaceAndStatusToString(const absl::Status& status) {
  if (status.ok()) {
    return "OK";
  }
  const auto [space, code] = util::RetrieveErrorSpaceAndCode(status);
  return absl::StrCat(space->SpaceName(), "::", space->String(code));
}

std::string StatusToString(const absl::Status& status) {
  if (status.ok()) return "OK";
  std::string result;
  const auto [space, code] = util::RetrieveErrorSpaceAndCode(status);
  absl::StrAppend(&result, space->SpaceName(), "::", space->String(code), ": ",
                  status.message());
  // If we are using lite protos then we skip stringifying the MessageSet since
  // we will not be able to produce a meaningful human-readable output.
  if constexpr (std::is_base_of_v<google::protobuf::Message,
                                  google::protobuf::bridge::MessageSet>) {
    if (util::HasPayload(status)) {
      auto message_set = util::MakePayloadsSet(status);
      absl::StrAppend(&result, " ",
                      LegacyUnredactedShortDebugString(message_set));
    }
  }

  status.ForEachPayload([&](absl::string_view type_url, absl::Cord payload) {
    if (type_url == util::status_internal::kErrorSpaceUrl ||
        type_url == util::status_internal::kMessageSetUrl) {
      return;
    } else {
      absl::StrAppend(&result, " [", type_url, "='",
                      absl::CHexEscape(std::string(payload)), "']");
    }
  });
  return result;
}

absl::Status Annotate(const absl::Status& s, absl::string_view msg) {
  if (s.ok() || msg.empty()) return s;

  std::string annotated;
  if (!s.message().empty()) {
    absl::StrAppend(&annotated, s.message(), "; ", msg);
    msg = annotated;
  }
  return ::util::SetMessage(s, msg);
}

namespace {

// Helper to create a status with a specified canonical code. If canonical_code
// is nullopt, the canonical code will be inferred by the errorspace and code.
//
// Loc is either absl::SourceLocation, or NoSourceLocation.  The latter is used
// on platforms where no SourceLocation is provided, and no
// SourceLocation::current() fallback is available.
template <typename Loc>
absl::Status MakeStatusWithCanonicalCode(
    const ErrorSpace* space, int code, absl::string_view msg,
    std::optional<absl::StatusCode> canonical_code, Loc loc) {
  DCHECK(space != nullptr);
  if (code == 0 || space == GenericErrorSpace::Get()) {
    return MakeStatusInternal(static_cast<absl::StatusCode>(code), msg, loc);
  }

  absl::StatusCode inferred_canonical_code =
      canonical_code.value_or(space->CanonicalCode(code));
  // Some `ErrorSpace` maps its error code to `kOk`, the resulting `Status`
  // is not `ok()` but `code() == kOk`.
  absl::Status ret =
      inferred_canonical_code == absl::StatusCode::kOk
          ? status_internal::MakeNonOkStatusWithOkCode(msg)
          : MakeStatusInternal(inferred_canonical_code, msg, loc);
  status_internal::ErrorSpacePayload::Set(space, code, &ret);
  return ret;
}

// Copies over source locations from `src` to `*dst`.
static void CopySourceLocations(absl::Status* dst, const absl::Status& src) {
  for (const absl::SourceLocation& loc : src.GetSourceLocations()) {
    dst->AddSourceLocation(loc);
  }
}

}  // namespace

absl::Status StripMessage(const absl::Status& status) {
  const auto [error_space, error_code] =
      util::RetrieveErrorSpaceAndCode(status);
  absl::Status ret = util::MakeStatusWithCanonicalCode(
      error_space, error_code,
      /*msg=*/absl::string_view(), status.code(), absl::SourceLocation());
  return ret;
}

absl::Status MakeStatus(const ErrorSpace* space, int code,
                        absl::string_view msg, absl::SourceLocation loc) {
  // Infer the canonical code using the error space and code.
  return MakeStatusWithCanonicalCode(space, code, msg,
                                     /*canonical_code=*/std::nullopt, loc);
}

absl::Status MakeStatus(const ErrorSpace* space, int code,
                        absl::string_view msg,
                        const google::protobuf::bridge::MessageSet* message_set,
                        absl::SourceLocation loc) {
  absl::Status ret = util::MakeStatus(space, code, msg, loc);

  if (message_set != nullptr) {
    ret.SetPayload(status_internal::kMessageSetUrl,
                   message_set->SerializePartialAsCord());
  }

  return ret;
}

// Return the canonical error space.
const ErrorSpace* CanonicalErrorSpace() { return GenericErrorSpace::Get(); }

// Returns true iff this status has an error code equal to `code` and an
// error space equal to `space`.
bool HasErrorCode(const absl::Status& status, const ErrorSpace* space,
                  int code) {
  if (code == 0) return status.ok();
  status_internal::ErrorSpaceAndCode space_and_code =
      status_internal::ErrorSpacePayload::Retrieve(status);
  return space_and_code.code == code && space_and_code.MatchErrorSpace(space);
}

// Returns true iff this status has an `ErrorSpace` equal to `space`.
bool HasErrorSpace(const absl::Status& status, const ErrorSpace* space) {
  return status_internal::ErrorSpacePayload::Retrieve(status).MatchErrorSpace(
      space);
}

void SetCanonicalCode(absl::StatusCode canonical_code, absl::Status* status) {
  if (status->code() == canonical_code) return;
  status_internal::ErrorSpaceAndCode space_and_code =
      status_internal::ErrorSpacePayload::Retrieve(*status);
  if (space_and_code.MatchErrorSpace(util::CanonicalErrorSpace())) return;
  // The zero/OK canonical code used to mean "calculate it later". However,
  // since we removed the lazy calculation, we need to always eagerly
  // calculate the canonical code.
  if (canonical_code == absl::StatusCode::kOk) {
    canonical_code = static_cast<absl::StatusCode>(
        space_and_code.GetErrorSpace()->CanonicalCode(space_and_code.code));
  }
  absl::Status new_status(canonical_code, status->message(),
                          absl::SourceLocation());
  // Copy all payloads over, including normal payloads, ErrorSpace, MessageSet,
  // StackTrace.
  status->ForEachPayload(
      [&](absl::string_view type_url, const absl::Cord& payload) {
        new_status.SetPayload(type_url, payload);
      });
  CopySourceLocations(&new_status, *status);
  *status = std::move(new_status);
}

// Copies over payloads from `src` to `*dst` except for the `ErrorSpace`
// payload.
static void CopyPayloads(absl::Status* dst, const absl::Status& src) {
  src.ForEachPayload(
      [&](absl::string_view type_url, const absl::Cord& payload) {
        if (type_url == status_internal::kErrorSpaceUrl) return;
        dst->SetPayload(type_url, payload);
      });
}

// Create a copy of `status` with message changed to `msg`.
absl::Status SetMessage(const absl::Status& status, absl::string_view msg) {
  const auto [error_space, error_code] =
      util::RetrieveErrorSpaceAndCode(status);
  absl::Status ret = util::MakeStatusWithCanonicalCode(
      error_space, error_code, msg, status.code(), absl::SourceLocation());
  CopyPayloads(&ret, status);
  CopySourceLocations(&ret, status);
  return ret;
}

// Create a copy of `status` with error space and code changed to `space` and
// `code` respectively.
absl::Status SetErrorSpaceAndCode(const absl::Status& status,
                                  const util::ErrorSpace* space, int code) {
  absl::Status ret =
      util::MakeStatus(space, code, status.message(), absl::SourceLocation());
  CopyPayloads(&ret, status);
  CopySourceLocations(&ret, status);
  return ret;
}

// Returns a copy of the status object with the message changed to `msg`, the
// error space changed to `space`, and the code changed to `code`. Payloads and
// source locations are preserved.
absl::Status SetMessageErrorSpaceAndCode(const absl::Status& status,
                                         absl::string_view msg,
                                         const util::ErrorSpace* space,
                                         int code) {
  absl::Status ret = util::MakeStatus(space, code, msg, absl::SourceLocation());
  CopyPayloads(&ret, status);
  CopySourceLocations(&ret, status);
  return ret;
}

absl::StatusCode ToAbslStatusCode(util::error::Code code) {
  return static_cast<absl::StatusCode>(code);
}

util::error::Code ToUtilErrorCode(absl::StatusCode code) {
  return static_cast<util::error::Code>(code);
}

namespace status_internal {

namespace {
// Used as a fail-safe when the actual ErrorSpace is not linked into the binary.
class UnknownErrorSpace : public ErrorSpaceImpl<UnknownErrorSpace> {
 public:
  static absl::string_view space_name() { return "UnknownErrorSpace"; }
  static std::string code_to_string(int code) {
    if (code == 0) {
      return "OK";
    }
    return absl::StrCat("UNKNOWN (code=", code, ")");
  }
  static absl::StatusCode canonical_code(int code) {
    return code == 0 ? absl::StatusCode::kOk : absl::StatusCode::kUnknown;
  }
};
}  // namespace

const ErrorSpace* absl_nonnull ErrorSpaceAndCode::GetErrorSpace() const {
  const ErrorSpace* value = nullptr;
  if (std::holds_alternative<std::string>(space)) {
    value = ErrorSpace::Find(std::get<std::string>(space));
  } else {
    value = std::get<const ErrorSpace*>(space);
  }
  if (value == nullptr) {
    return UnknownErrorSpace::Get();
  }
  return value;
}

absl::string_view ErrorSpaceAndCode::GetErrorSpaceName() const {
  if (std::holds_alternative<std::string>(space)) {
    return std::get<std::string>(space);
  }
  const ErrorSpace* value = std::get<const ErrorSpace*>(space);
  if (value == nullptr) {
    value = UnknownErrorSpace::Get();
  }
  return value->SpaceName();
}

bool ErrorSpaceAndCode::MatchErrorSpace(const ErrorSpace* space_ptr) const {
  if (std::holds_alternative<std::string>(space)) {
    return std::get<std::string>(space) == space_ptr->SpaceName();
  } else if (std::holds_alternative<const ErrorSpace*>(space)) {
    return std::get<const ErrorSpace*>(space) == space_ptr;
  }
  return false;
}

// The protobuf tag is encoded as `FIELD_NUMBER << 3 | WIRE_TYPE`
static constexpr char kTagCode = 1 << 3 | 0;   // WIRETYPE_VARINT = 0
static constexpr char kTagSpace = 2 << 3 | 2;  // WIRETYPE_LENGTH_DELIMITED = 2

static int CalculateSizeOfPayload(const ErrorSpace* space, int code) {
  int space_name_size = space->SpaceName().size();
  constexpr int kTagSize = 1;
  return 2 * kTagSize + Varint::Length32(code) +
         Varint::Length32(space_name_size) + space_name_size;
}

void ErrorSpacePayload::Set(const ErrorSpace* space, int code,
                            absl::Status* status) {
  DCHECK_NE(code, 0);
  if (space == GenericErrorSpace::Get()) return;

  int buffer_size = CalculateSizeOfPayload(space, code);
  absl::FixedArray<char, 128> buf(buffer_size);
  char* cur = buf.data();
  *cur++ = kTagCode;
  cur = Varint::Encode32(cur, code);
  *cur++ = kTagSpace;
  absl::string_view space_name = space->SpaceName();
  cur = Varint::Encode32(cur, space_name.size());
  memcpy(cur, space_name.data(), space_name.size());

  status->SetPayload(kErrorSpaceUrl,
                     absl::Cord(absl::string_view(buf.data(), buffer_size)));
}

static std::optional<uint32_t> ParseVarint32(absl::string_view& str) {
  uint32_t v;
  const char* end =
      Varint::Parse32WithLimit(str.data(), str.data() + str.size(), &v);
  if (end == nullptr) return std::nullopt;
  str.remove_prefix(end - str.data());
  return v;
}

static std::optional<ErrorSpaceAndCode> ParseErrorSpacePayload(
    absl::Cord payload) {
  absl::string_view buf = payload.Flatten();
  std::optional<int> code;
  std::optional<std::string> space_name;
  while (!buf.empty()) {
    char tag = buf[0];
    buf.remove_prefix(1);
    switch (tag) {
      case kTagCode: {
        auto c = ParseVarint32(buf);
        if (!c.has_value()) return std::nullopt;
        code.emplace(*c);
        break;
      }
      case kTagSpace: {
        auto length = ParseVarint32(buf);
        if (!length.has_value() || *length > buf.size()) {
          return std::nullopt;
        }
        space_name.emplace(buf.data(), *length);
        buf.remove_prefix(*length);
        break;
      }
      default:
        return std::nullopt;
    }
  }

  if (!code.has_value() || !space_name.has_value()) {
    return std::nullopt;
  }
  return ErrorSpaceAndCode{*std::move(space_name), *code};
}

ErrorSpaceAndCode ErrorSpacePayload::Retrieve(const absl::Status& status) {
  std::optional<absl::Cord> serialized = status.GetPayload(kErrorSpaceUrl);
  if (serialized.has_value()) {
    if (auto parsed_space_and_code =
            ParseErrorSpacePayload(std::move(*serialized))) {
      return std::move(*parsed_space_and_code);
    }
  }
  return {util::CanonicalErrorSpace(), status.raw_code()};
}

absl::Status MakeNonOkStatusWithOkCode(absl::string_view message) {
  return absl::status_internal::StatusPrivateAccessor::
      MakeNonOkStatusWithOkCode(message);
}

static std::optional<std::string> PrintStatusPayload(
    absl::string_view type_url, const absl::Cord& payload) {
  if (type_url == util::status_internal::kErrorSpaceUrl) {
    auto space_and_code =
        util::status_internal::ParseErrorSpacePayload(payload);
    if (!space_and_code.has_value()) return std::nullopt;
    const util::ErrorSpace* const space = space_and_code->GetErrorSpace();
    if (space == nullptr) return std::nullopt;
    return absl::StrCat(space->SpaceName(),
                        "::", space->String(space_and_code->code));
  } else if (type_url == util::status_internal::kMessageSetUrl) {
    if constexpr (std::is_base_of_v<google::protobuf::Message,
                                    google::protobuf::bridge::MessageSet>) {
      google::protobuf::bridge::MessageSet message_set_obj;
      if (!message_set_obj.ParsePartialFromString(payload)) {
        return std::nullopt;
      }
      return LegacyUnredactedShortDebugString(message_set_obj);
    } else {
      return std::nullopt;
    }
  } else if constexpr (std::is_base_of_v<google::protobuf::Message,
                                         google::protobuf::Any>) {
    std::string type_name;
    if (!google::protobuf::Any::ParseAnyTypeUrl(type_url, &type_name)) {
      return std::nullopt;
    }
    const google::protobuf::DescriptorPool* pool =
        google::protobuf::DescriptorPool::generated_pool();
    const google::protobuf::Descriptor* desc =
        pool->FindMessageTypeByName(type_name);
    if (desc == nullptr) {
      return std::nullopt;
    }
    const google::protobuf::Message* prototype =
        google::protobuf::MessageFactory::generated_factory()->GetPrototype(
            desc);
    if (prototype == nullptr) {
      return std::nullopt;
    }
    std::unique_ptr<google::protobuf::Message> any_message;
    any_message.reset(prototype->New());
    if (!any_message->ParsePartialFromString(payload)) {
      return std::nullopt;
    }
    return LegacyUnredactedShortDebugString(*any_message);
  } else {
    return std::nullopt;
  }
}

RegisterStatusPayloadPrinter::RegisterStatusPayloadPrinter() {
  absl::status_internal::SetStatusPayloadPrinter(PrintStatusPayload);
}

RegisterStatusPayloadPrinter g_status_payload_printer_register;

}  // namespace status_internal

void SaveStatusAsRpcStatus(const absl::Status& status,
                           google::rpc::Status& rpc_status) {
  rpc_status.set_code(static_cast<int>(status.code()));
  rpc_status.set_message(status.message());
  status.ForEachPayload(
      [&](absl::string_view type_url, const absl::Cord& payload) {
        google::protobuf::Any* any = rpc_status.add_details();
        any->set_type_url(type_url);
        any->set_value(std::string(payload));
      });
}

google::rpc::Status SaveStatusAsRpcStatus(const absl::Status& status) {
  google::rpc::Status rpc_status;
  SaveStatusAsRpcStatus(status, rpc_status);
  return rpc_status;
}

absl::Status MakeStatusFromRpcStatus(const google::rpc::Status& status,
                                     absl::SourceLocation loc) {
  if (status.code() == 0) return absl::OkStatus();
  absl::Status ret(static_cast<absl::StatusCode>(status.code()),
                   status.message(), loc);
  for (const google::protobuf::Any& detail : status.details()) {
    if (status_internal::IsStackTracePayloadUrl(detail.type_url())) {
      continue;
    }
    ret.SetPayload(detail.type_url(), absl::Cord(detail.value()));
  }
  return ret;
}

////////////////////////////////////////////////////////////////////////
// Payload support
////////////////////////////////////////////////////////////////////////

namespace status_internal {

ABSL_CONST_INIT absl::string_view kMessageSetUrl =
    "type.googleapis.com/util.MessageSetPayload";

}  // namespace status_internal

bool HasPayload(const absl::Status& s) {
  return s.GetPayload(status_internal::kMessageSetUrl).has_value();
}

google::protobuf::bridge::MessageSet MakePayloadsSet(const absl::Status& s) {
  google::protobuf::bridge::MessageSet message_set_obj;
  if (auto message_set_payload =
          s.GetPayload(status_internal::kMessageSetUrl)) {
    (void)message_set_obj.ParsePartialFromString(*message_set_payload);
  }

  return message_set_obj;
}

absl::Cord InternalGetMessageSetPayloadString(const absl::Status& s) {
  return s.GetPayload(status_internal::kMessageSetUrl).value_or(absl::Cord());
}

}  // namespace util
