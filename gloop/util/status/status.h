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

#ifndef THIRD_PARTY_GLOOP_UTIL_STATUS_STATUS_H_
#define THIRD_PARTY_GLOOP_UTIL_STATUS_STATUS_H_

// For documentation see <link>.
// In new code, write `absl::Status` and include
// "absl/status/status.h".

#include <assert.h>

#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/flags/declare.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/source_location.h"
#include "gloop/util/status/codes.pb.h"     // IWYU pragma: export
#include "gloop/util/status/error_space.h"  // IWYU pragma: export
#include "gloop/util/status/non_message_set_payload.pb.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/bridge/message_set.pb.h"
#include "google/rpc/status.pb.h"

ABSL_DECLARE_FLAG(bool, util_status_save_stack_trace);

namespace util {
#ifndef SWIG
namespace status_internal {
struct RegisterStatusPayloadPrinter {
  RegisterStatusPayloadPrinter();
};

extern RegisterStatusPayloadPrinter g_status_payload_printer_register;
}  // namespace status_internal
#endif  // !SWIG

// Even though we are including the header, SWIG needs to see this here.
class ErrorSpace;

namespace status_internal {

struct ErrorSpaceAndCode {
  const ErrorSpace* absl_nonnull GetErrorSpace() const;
  absl::string_view GetErrorSpaceName() const;
  bool MatchErrorSpace(const ErrorSpace*) const;
  std::variant<std::string, const ErrorSpace*> space;
  int code;
};

struct ErrorSpacePayload {
  static ErrorSpaceAndCode Retrieve(const absl::Status& status);
  static void Set(const ErrorSpace* space, int code, absl::Status* status);
};

absl::Status MakeNonOkStatusWithOkCode(absl::string_view message);

}  // namespace status_internal

const ErrorSpace* CanonicalErrorSpace();

// Returns a Status that is identical to `s` except that the error_message()
// has been augmented by adding `msg` to the end of the original error message.
//
// Annotate should be used to add higher-level information to a Status.  E.g.,
//
//   absl::Status s = file::GetContents(...);
//   if (!s.ok()) {
//     return Annotate(s, "loading denylist");
//   }
//
// Annotate() adds the appropriate separators, so callers should not include a
// separator in `msg`. The exact formatting is subject to change, so you should
// not depend on it in your tests.
//
// OK status values have no error message and therefore if `s` is OK, the result
// is unchanged.
absl::Status Annotate(const absl::Status& s, absl::string_view msg);

namespace error {
// Registers `ErrorCode` so it works with `Status::Is` and
// `testing::status::StatusIs`.  See documentation in util/task/error_space.h.
//
// All namespaces must be fully qualified or SWIG becomes unhappy.
inline const ::util::ErrorSpace* GetErrorSpace(
    ::util::ErrorSpaceAdlTag<::util::error::Code>) {
  return ::util::CanonicalErrorSpace();
}
}  // namespace error

// Protocol object that can hold a serialized Status object
class StatusProto;

#ifndef SWIG
// task.swig redefined this routine so that MessageSet conversion to proto
// happens on Python side.
void SaveStatusToProto(const absl::Status& s, util::StatusProto* proto);
#endif
// To be used only from SWIG.
void InternalSaveStatusToProto(const absl::Status& s, util::StatusProto* proto);

absl::Status MakeStatusFromProto(
    const util::StatusProto& proto,
    absl::SourceLocation loc = absl::SourceLocation::current());

// Conversion functions between `google.rpc.Status` and `absl::Status`.
// NOTE: These functions don't handle the corner case where `status` holds a
// custom error space/code which is mapped to `kOk` canonical code, i.e.
// `status.code()` is `absl::StatusCode::kOk` but `status.ok()` is false.
// If you have such `status` object, you should change the mapping of the error
// code to an non-zero canonical code, or use `util.StatusProto`.
//
// ANOTHER NOTE: These functions don't adhere to the gRPC status mapping rules
// specified in
// <link>,
// in particular around handling message set payloads.
//
//   // Serialize
//   google::rpc::Status rpc_status;
//   rpc_status.ParseFromString(grpc::Status(absl_status).error_details());
//
//   // Deserialize (and please add error checks for details)
//   util::StatusProto util_status_proto;
//   rpc_status.details(0).UnpackTo(&util_status_proto);
//   absl::Status absl_status = MakeStatusFromProto(util_status_proto);
google::rpc::Status SaveStatusAsRpcStatus(const absl::Status& status);

// Same as `SaveStatusAsRpcStatus`, but via an output parameter for arena
// support.
void SaveStatusAsRpcStatus(const absl::Status& status,
                           google::rpc::Status& rpc_status);

absl::Status MakeStatusFromRpcStatus(
    const google::rpc::Status& status,
    absl::SourceLocation loc = absl::SourceLocation::current());

// Returns a copy of the status object with error message, payload and source
// locations stripped off. Useful for comparing against expected status when
// error message might vary, e.g.
//     EXPECT_EQ(expected_status, util::StripMessage(real_status));
absl::Status StripMessage(const absl::Status& status);

// Return a combination of the error code name, e.g. "foo_space::BAR" suitable
// for use in monitoring. NOTE: This contains more Google-specific details than
// `Status::ToString()`, such as error space/code. You should not
// programmatically depend on the exact format of the result, since it is
// subject to change.
std::string ErrorSpaceAndStatusToString(const absl::Status& status);

// Return a combination of the error code name and message, and possibly the
// payload and the stack trace.
// NOTE: This contains more Google-specific details than `Status::ToString()`,
// such as error space/code and MessageSet payload. You should not
// programmatically depend on the exact format of the result, since it is
// subject to change.
std::string StatusToString(const absl::Status& status);

// The following helpers are replacing all ErrorSpace-related member functions.
// See <link> for more details.

#ifndef SWIG
// Create a status in the associated error space with the specified code and
// error message.  If `code == util::error::OK`, `msg` is ignored and an
// object identical to an OK status is constructed.
//
// `msg` must be in UTF-8. The implementation may complain (e.g., by printing
// a warning) if it is not.
template <typename Enum,
          typename = typename std::enable_if<
              EnumHasErrorSpace<Enum>::value &&
              !std::is_same<Enum, ::util::error::Code>::value>::type>
absl::Status MakeStatus(
    Enum code, absl::string_view msg,
    absl::SourceLocation loc = absl::SourceLocation::current()) {
  return MakeStatus(GetErrorSpaceForEnum(code), static_cast<int>(code), msg,
                    loc);
}
#endif  // SWIG

// Creates a status in the specified error space with the code and error
// message provided.  If `code == 0` all other arguments are ignored and an
// `ok()` Status is constructed.
//
// New APIs should use the canonical error space and construct the status using
// either absl::OkStatus() or one of the canonical status constructors in
// //depot///gloop/util/task/canonical_errors.h.
//
// REQUIRES: space != nullptr
absl::Status MakeStatus(
    const ErrorSpace* space, int code, absl::string_view msg,
    absl::SourceLocation loc = absl::SourceLocation::current());

// Creates a status in the specified error space with the code, error message,
// and MessageSet (if non-null).  If `code == 0` all other arguments are
// ignored and an `ok()` Status is constructed.
// If `message_set != nullptr`, creates a copy of the message_set (consequently
// does not take ownership).
//
// REQUIRES: `space != nullptr`
absl::Status MakeStatus(
    const ErrorSpace* space, int code, absl::string_view msg,
    const google::protobuf::bridge::MessageSet* message_set,
    absl::SourceLocation loc = absl::SourceLocation::current());

// Return the error space used to create the Status.
inline const ErrorSpace* absl_nonnull RetrieveErrorSpace(
    const absl::Status& status) {
  return status_internal::ErrorSpacePayload::Retrieve(status).GetErrorSpace();
}

// Returns the stored error code.  CAUTION: error codes have no meaning except
// in the context of a particular error space, so it is almost always wrong
// to call this method without also calling `RetrieveErrorSpace()`.  Most code
// should use `code()`, or use `RetrieveErrorCode()` and `RetrieveErrorSpace()`
// together.
//
// NOTE: Prefer `RetrieveErrorSpaceAndCode` over calling `RetrieveErrorSpace`
//       and `RetrieveErrorCode` separately.
inline int RetrieveErrorCode(const absl::Status& status) {
  return status_internal::ErrorSpacePayload::Retrieve(status).code;
}

// Return the error space used to create the Status along with the error code.
inline std::pair<const ErrorSpace*, int> RetrieveErrorSpaceAndCode(
    const absl::Status& status) {
  const status_internal::ErrorSpaceAndCode r =
      status_internal::ErrorSpacePayload::Retrieve(status);
  return {r.GetErrorSpace(), r.code};
}

#ifndef SWIG
// Returns true iff this status has the code and associated error space of
// `code`.  `HasErrorCode(MakeStatus(code, ""), code)` is always true.
// In particular, if the `code` is zero, returns true if `status.ok()`.
// Sample usage:
//
//   absl::Status s = frobber::Frobnicate();
//   if (HasErrorCode(s, frobber::kNoMoreFrobs)) {
//     frobber::OrderMoreFrobs();
//   }
//
// REQUIRES: `code` is in an enum associated with an error space; see the
// `ErrorSpace` class documentation for details.
template <typename Enum,
          typename = typename std::enable_if<
              EnumHasErrorSpace<Enum>::value &&
              !std::is_same<Enum, ::util::error::Code>::value>::type>
ABSL_MUST_USE_RESULT bool HasErrorCode(const absl::Status& status, Enum code) {
  int value = static_cast<int>(code);
  if (value == 0) return status.ok();
  return HasErrorCode(status, GetErrorSpaceForEnum(code), value);
}

// Returns true iff this status has the code and canonical error space.
// `HasErrorCode(absl::Status(code, ""), code)` is always true.
ABSL_MUST_USE_RESULT inline bool HasErrorCode(const absl::Status& status,
                                              absl::StatusCode code) {
  return status.code() == code &&
         RetrieveErrorSpace(status) == CanonicalErrorSpace();
}
#endif  // SWIG

// Returns true iff this status has an error code equal to `code` and an
// error space equal to `space`.
bool HasErrorCode(const absl::Status& status, const ErrorSpace* space,
                  int code);

// Returns true iff this status has an `ErrorSpace` equal to `space`.
// NOTE: This function might be more performant than
// `RetrieveErrorSpace(status) == space` in that it compares the name of
// the ErrorSpace directly instead of using `util::ErrorSpace::Find` (which
// acquires a global lock) to look up the pointer and compare.
bool HasErrorSpace(const absl::Status& status, const ErrorSpace* space);

// Sets the canonical code for a `Status` with a non-canonical error space.
// Does nothing if the status has a code in a custom error space which already
// maps to `canonical_code`.
void SetCanonicalCode(absl::StatusCode canonical_code, absl::Status* status);

// Returns a copy of the status object with the message changed to `msg`, while
// keeping the same error space, error code and payloads.
// NOTE: if `status` is ok, `msg` will be discarded.
absl::Status SetMessage(const absl::Status& status, absl::string_view msg);

// Returns a copy of the status object with the error space and the error code
// changed to `space` and `code` respectively, while keeping the same message
// and payloads.
absl::Status SetErrorSpaceAndCode(const absl::Status& status,
                                  const util::ErrorSpace* space, int code);

// Returns a copy of the status object with the message changed to `msg`, the
// error space changed to `space`, and the code changed to `code`. Payloads and
// source locations are preserved.
absl::Status SetMessageErrorSpaceAndCode(const absl::Status& status,
                                         absl::string_view msg,
                                         const util::ErrorSpace* space,
                                         int code);

// `absl::StatusCode` and `util::error::Code` represent the same idea and are
// guaranteed to match. The following conversion functions are provided to
// avoid writing `static_cast`, which makes the code less readable.
// It's unfortunate that we have two different C++ types because Abseil doesn't
// want a dependency on protobuf.
// When writing C++ code, even if you choose to define your own proto type for
// `Status`, you should still use `absl::Status` and `absl::StatusCode` in most
// of your code, and write helper functions to convert between `absl::Status`
// and your own proto type and call them only when you need to transfer it over
// the wire.
absl::StatusCode ToAbslStatusCode(util::error::Code code);
util::error::Code ToUtilErrorCode(absl::StatusCode code);

#ifdef SWIG
// Returns a copy of the status object in the canonical error space. This will
// remove any custom error space or error code. It will use the canonical code
// from the status protocol buffer (if present) or the result of passing this
// status to the `ErrorSpace::CanonicalCode()` method.
inline absl::Status ToCanonical(const absl::Status& status) {
  return SetErrorSpaceAndCode(status, util::CanonicalErrorSpace(),
                              status.raw_code());
}
template <typename T>
absl::StatusOr<T> ToCanonical(absl::StatusOr<T> status_or) {
  return status_or.ok() ? std::move(status_or)
                        : util::ToCanonical(status_or.status());
}
#else  // SWIG
// A <link> to bypass ADL (argument-dependent lookup) for
// ToCanonical(Status). Unfortunately SWIG doesn't like this pattern, hence the
// duplicate implementations.
struct ToCanonicalAlias {
  inline absl::Status operator()(const absl::Status& status) const {
    return SetErrorSpaceAndCode(status, util::CanonicalErrorSpace(),
                                status.raw_code());
  }

  template <typename T>
  absl::StatusOr<T> operator()(absl::StatusOr<T> status_or) const {
    return status_or.ok() ? std::move(status_or) : (*this)(status_or.status());
  }
};

// Returns a copy of the Status or StatusOr object in the canonical error space.
// This will remove any custom error space or error code. It will use the
// canonical code from the status protocol buffer (if present) or the result of
// passing this status to the `ErrorSpace::CanonicalCode()` method.
inline constexpr auto ToCanonical = ToCanonicalAlias{};
#endif  // SWIG

////////////////////////////////////////////////////////////////////////
// Payload support
////////////////////////////////////////////////////////////////////////

namespace status_internal {

extern absl::string_view kMessageSetUrl;

}  // namespace status_internal

// AttachPayload()
//
// If "s->ok()", does nothing.  Else adds "obj" to the set of protocol
// buffer messages associated with this error.
//
// MessageSetExtension must be a protocol buffer type that can be stored in a
// MessageSet. Usually `MessageSetExtension::message_set_extension` is the
// extension id. If that's not the case, call the overload that takes
// `ExtensionIdentifier` as an argument, e.g. `google::rpc::Status` has a
// non-standard extension id `google::rpc::error_details_ext`.
//
// Example:
//   absl::Status s;
//   util::TestPayload test_payload;
//   test_payload.set_message("test");
//   util::AttachPayload(&s, test_payload);
//
//   google::rpc::Status rpc_status;
//   rpc_status.set_message("message for external");
//   util::AttachPayload(&s, rpc_status, google::rpc::error_details_ext);
template <typename MessageSetExtension, typename ExtensionIdentifier>
void AttachPayload(absl::Status* s, const MessageSetExtension& obj,
                   const ExtensionIdentifier& id) {
  if (s->ok()) return;

  google::protobuf::bridge::MessageSet message_set_obj;
  if (auto message_set_payload =
          s->GetPayload(status_internal::kMessageSetUrl)) {
    (void)message_set_obj.ParsePartialFromString(*message_set_payload);
  }

  message_set_obj.MutableExtension(id)->CopyFrom(obj);

  s->SetPayload(status_internal::kMessageSetUrl,
                message_set_obj.SerializePartialAsCord());
}

template <typename MessageSetExtension>
inline void AttachPayload(absl::Status* s, const MessageSetExtension& obj) {
  util::AttachPayload(s, obj, MessageSetExtension::message_set_extension);
}

// ErasePayload()
//
// If "s->ok()", does nothing. Else finds a message set payload and erases a
// protocol buffer message from it (if found) specified by its extension
// identifier.
//
// MessageSetExtension must be a protocol buffer type that can be stored in a
// MessageSet. Usually `MessageSetExtension::message_set_extension` is the
// extension id. If that's not the case, call the overload that takes
// `ExtensionIdentifier` as an argument.
// Example:
//   absl::Status s(absl::StatusCode::kDeadlineExceeded, "foo");
//   util::TestPayload test_payload;
//   test_payload.set_message("test");
//   util::AttachPayload(&s, test_payload);
//   CHECK(util::HasPayloadWithType<util::TestPayload>(s));
//   util::ErasePayload<util::TestPayload>(&s);
//   CHECK(!util::HasPayloadWithType<util::TestPayload>(s));
template <typename ExtensionIdentifier>
bool ErasePayload(absl::Status* s, const ExtensionIdentifier& id) {
  if (s->ok()) {
    return false;
  }

  auto message_set_payload = s->GetPayload(status_internal::kMessageSetUrl);
  if (!message_set_payload.has_value()) {
    return false;
  }

  google::protobuf::bridge::MessageSet message_set_obj;
  if (!message_set_obj.ParsePartialFromString(*message_set_payload)) {
    return false;
  }

  if (!message_set_obj.HasExtension(id)) {
    return false;
  }

  message_set_obj.ClearExtension(id);
  auto new_payload = message_set_obj.SerializePartialAsCord();
  if (new_payload.empty()) {
    s->ErasePayload(status_internal::kMessageSetUrl);
  } else {
    s->SetPayload(status_internal::kMessageSetUrl, std::move(new_payload));
  }
  return true;
}

template <typename MessageSetExtension>
inline bool ErasePayload(absl::Status* s) {
  return util::ErasePayload(s, MessageSetExtension::message_set_extension);
}

// HasPayload()
//
// Indicates whether an absl::Status object contains any payloads with a type
// extending proto2's `MessageSet`, returning `true` if so. Having a payload
// does not guarantee the presence of a payload with a specific type. Note that
// returning `false` does not necessarily indicate the absence of a payload, but
// only the absence on one which extends `MessageSet`.
bool HasPayload(const absl::Status& s);

// HasPayloadWithType()
//
// Indicates whether an absl::Status object contains a payload with a type
// extending proto2's MessageSet, returning `true` if so. The extension
// identifier is specified as the second argument. This function implicitly
// invokes `HasPayload()`, so you do not need to call it alongside a
// `HasPayloadWithType()` call.
template <typename ExtensionIdentifier>
bool HasPayloadWithType(const absl::Status& s, const ExtensionIdentifier& id) {
  if (auto message_set_payload =
          s.GetPayload(status_internal::kMessageSetUrl)) {
    google::protobuf::bridge::MessageSet message_set_obj;
    if (!message_set_obj.ParsePartialFromString(*message_set_payload)) {
      return false;
    }
    return message_set_obj.HasExtension(id);
  }

  return false;
}

// SWIG compilation fails if the following overload is present.
#ifndef SWIG

template <typename MessageSetExtension, typename ExtensionIdentifier>
ABSL_DEPRECATE_AND_INLINE()
inline bool HasPayloadWithType(const absl::Status& s,
                               const ExtensionIdentifier& id) {
  // Resolves to the single-template-parameter overload above.
  return util::HasPayloadWithType(s, id);
}

#endif  // SWIG

// Indicates whether an absl::Status object contains a payload with a type
// extending proto2's `MessageSet`, returning `true` if so. The extension
// identifier is expected to be accessible as
// `MessageSetExtension::message_set_extension()`.
// This function implicitly invokes `HasPayload()`, so you do not need to call
// it alongside a `HasPayloadWithType()` call.
template <typename MessageSetExtension>
inline bool HasPayloadWithType(const absl::Status& s) {
  return ::util::HasPayloadWithType(s,
                                    MessageSetExtension::message_set_extension);
}

// GetPayload()
//
// Returns a copy of a payload object with type MessageSetExtension. The second
// argument specifies the ExtensionIdentifier. Before calling GetPayload, you
// should check the presence of the payload with this type by invoking
// HasPayloadWithType with the same arguments. Otherwise this call will lead to
// crash in case if payload if absent.
template <typename MessageSetExtension, typename ExtensionIdentifier>
inline MessageSetExtension GetPayload(const absl::Status& s,
                                      const ExtensionIdentifier& id) {
  static_assert(!std::is_reference<MessageSetExtension>::value,
                "MessageSetExtension cannot be a reference");

  auto message_set_payload = s.GetPayload(status_internal::kMessageSetUrl);

  ABSL_INTERNAL_CHECK(
      message_set_payload.has_value(),
      "Call to GetPayload should be guarded by the HasPayloadWithType check");

  google::protobuf::bridge::MessageSet message_set_obj;
  (void)message_set_obj.ParsePartialFromString(*message_set_payload);
  return message_set_obj.GetExtension(id);
}

// Returns a copy of a payload object with type MessageSetExtension. An
// extension id is expected to be accessible as
//   MessageSetExtension::message_set_extension.
//
// Note: before calling `GetPayload()`, you should check for the presence of a
// payload by invoking `HasPayloadWithType()` with the same arguments; not
// performing this check may lead to undefined behavior in cases where the
// payload is absent.
template <typename MessageSetExtension>
inline MessageSetExtension GetPayload(const absl::Status& s) {
  static_assert(!std::is_reference<MessageSetExtension>::value,
                "MessageSetExtension cannot be a reference");
  return ::util::GetPayload<MessageSetExtension>(
      s, MessageSetExtension::message_set_extension);
}

// These symbols conflict with versions declared/defined in task.swig, causing
// swig to get confused (caught by pywrap_task_test). Prevent it from seeing
// them.
#ifndef SWIG

// MakePayloadsSet()
//
// Makes a new `MessageSet` object out of an absl::Status' payloads, attaching
// payloads with `MessageSetExtension` types. Before calling MakePayloadsSet,
// you should check the presence of any such payload by invoking HasPayload with
// the same argument. Otherwise this call will lead to crash.
google::protobuf::bridge::MessageSet MakePayloadsSet(const absl::Status& s);

absl::Cord InternalGetMessageSetPayloadString(const absl::Status& s);

#endif

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_STATUS_STATUS_H_
