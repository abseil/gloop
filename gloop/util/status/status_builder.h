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

#ifndef THIRD_PARTY_GLOOP_UTIL_STATUS_STATUS_BUILDER_H_
#define THIRD_PARTY_GLOOP_UTIL_STATUS_STATUS_BUILDER_H_

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/log_severity.h"
#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/log/log_sink.h"
#include "absl/status/status.h"
#include "absl/status/status_builder.h"
#include "absl/strings/cord.h"
#include "absl/strings/internal/ostringstream.h"
#include "absl/strings/internal/stringify_stream.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/source_location.h"
#include "gloop/base/log_severity.h"
#include "gloop/util/status/status.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

class status_internal::StatusBuilderPrivateAccessor {
 public:
  static absl::SourceLocation GetLoc(const StatusBuilder& builder) {
    return builder.loc_;
  }

  static StatusBuilder::Rep* GetRep(const StatusBuilder& builder) {
    return builder.rep_.get();
  }

  static void SetErrorCode(StatusBuilder& builder,
                           const ::util::ErrorSpace* space, int code_int) {
    if (builder.rep_ == nullptr) {
      builder.rep_ = std::make_unique<StatusBuilder::Rep>(
          ::util::SetErrorSpaceAndCode(absl::Status(), space, code_int));
    } else {
      builder.rep_->status =
          ::util::SetErrorSpaceAndCode(builder.rep_->status, space, code_int);
    }
  }
};

// ADL extension point for setting error codes from non-canonical error spaces.
// Argument-dependent lookup extension point for setting error codes from
// non-canonical error spaces.
template <typename Enum>
auto AbslInternalSetErrorCode(
    StatusBuilder& builder ABSL_ATTRIBUTE_LIFETIME_BOUND, Enum code)
    -> decltype(std::conditional_t<
                false, Enum, status_internal::StatusBuilderPrivateAccessor>::
                    SetErrorCode(builder,
                                 std::declval<const ::util::ErrorSpace*>(),
                                 static_cast<int>(code))) {
  return absl::status_internal::StatusBuilderPrivateAccessor::SetErrorCode(
      builder, util::GetErrorSpaceForEnum(code), static_cast<int>(code));
}

// Argument-dependent lookup extension point for attaching payloads (e.g.,
// protocol buffers) to the builder.
//
// Exists for the same reason as AbslInternalSetErrorCode() (see documentation).
template <typename MessageSetExtension, typename ExtensionIdentifier>
void AbslInternalAttachPayload(StatusBuilder& builder,
                               const MessageSetExtension& obj,
                               const ExtensionIdentifier& id) {
  auto* rep =
      absl::status_internal::StatusBuilderPrivateAccessor::GetRep(builder);
  if (rep != nullptr) {
    util::AttachPayload(&rep->status, obj, id);
  }
}

template <typename MessageSetExtension>
void AbslInternalAttachPayload(StatusBuilder& builder,
                               const MessageSetExtension& obj) {
  return AbslInternalAttachPayload(builder, obj,
                                   MessageSetExtension::message_set_extension);
}

ABSL_NAMESPACE_END
}  // namespace absl

namespace util {

using StatusBuilder ABSL_DEPRECATE_AND_INLINE() = absl::StatusBuilder;

// Creates a new status based on an old one by joining the message from the
// original to an additional message.
absl::Status JoinMessageToStatus(absl::Status s, absl::string_view msg,
                                 absl::MessageJoinStyle style);

// Each of the functions below creates StatusBuilder with a canonical error.
// The error code of the StatusBuilder matches the name of the function.
StatusBuilder AbortedErrorBuilder(
    absl::SourceLocation location = absl::SourceLocation::current());
StatusBuilder AlreadyExistsErrorBuilder(
    absl::SourceLocation location = absl::SourceLocation::current());
StatusBuilder CancelledErrorBuilder(
    absl::SourceLocation location = absl::SourceLocation::current());
StatusBuilder DataLossErrorBuilder(
    absl::SourceLocation location = absl::SourceLocation::current());
StatusBuilder DeadlineExceededErrorBuilder(
    absl::SourceLocation location = absl::SourceLocation::current());
StatusBuilder FailedPreconditionErrorBuilder(
    absl::SourceLocation location = absl::SourceLocation::current());
StatusBuilder InternalErrorBuilder(
    absl::SourceLocation location = absl::SourceLocation::current());
StatusBuilder InvalidArgumentErrorBuilder(
    absl::SourceLocation location = absl::SourceLocation::current());
StatusBuilder NotFoundErrorBuilder(
    absl::SourceLocation location = absl::SourceLocation::current());
StatusBuilder OutOfRangeErrorBuilder(
    absl::SourceLocation location = absl::SourceLocation::current());
StatusBuilder PermissionDeniedErrorBuilder(
    absl::SourceLocation location = absl::SourceLocation::current());
StatusBuilder UnauthenticatedErrorBuilder(
    absl::SourceLocation location = absl::SourceLocation::current());
StatusBuilder ResourceExhaustedErrorBuilder(
    absl::SourceLocation location = absl::SourceLocation::current());
StatusBuilder UnavailableErrorBuilder(
    absl::SourceLocation location = absl::SourceLocation::current());
StatusBuilder UnimplementedErrorBuilder(
    absl::SourceLocation location = absl::SourceLocation::current());
StatusBuilder UnknownErrorBuilder(
    absl::SourceLocation location = absl::SourceLocation::current());

// StatusBuilder policy to append an extra message to the original status.
//
// This is most useful with adaptors such as util::TaskReturn that otherwise
// would prevent use of operator<<.  For example:
//
//   ABSL_RETURN_IF_ERROR(foo(val))
//       .With(util::ExtraMessage("when calling foo()"))
//       .With(util::TaskReturn(task));
//
// or
//
//   ABSL_RETURN_IF_ERROR(foo(val))
//       .With(util::ExtraMessage() << "val: " << val)
//       .With(util::TaskReturn(task));
//
// Note in the above example, the ABSL_RETURN_IF_ERROR macro ensures the
// ExtraMessage expression is evaluated only in the error case, so efficiency of
// constructing the message is not a concern in the success case.
class ExtraMessage {
 public:
  ExtraMessage() : ExtraMessage(std::string()) {}
  explicit ExtraMessage(std::string msg)
      : msg_(std::move(msg)), stream_(msg_) {}

  ExtraMessage(
      ExtraMessage&& other) noexcept  // strings::OStringStream is stateless
                                      // so we can simply move over the message.
      : ExtraMessage(std::move(other.msg_)) {}

  // Appends to the extra message that will be added to the original status.  By
  // default, the extra message is added to the original message as if by
  // `util::Annotate`, which includes a convenience separator between the
  // original message and the enriched one.
  template <typename T>
  ExtraMessage& operator<<(const T& value) & {
    stream_ << value;
    return *this;
  }

  // As above, preserving the rvalue-ness of the ExtraMessage object.
  template <typename T>
  ExtraMessage&& operator<<(const T& value) && {
    *this << value;
    return std::move(*this);
  }

  // Appends to the extra message that will be added to the original status.  By
  // default, the extra message is added to the original message as if by
  // `util::Annotate`, which includes a convenience separator between the
  // original message and the enriched one.
  StatusBuilder operator()(StatusBuilder builder) const {
    builder << msg_;
    return builder;
  }

 private:
  std::string msg_;
  absl::status_internal::Stream stream_;
};

// Creates a `StatusBuilder` from an enum code and its associated `ErrorSpace`.
// If logging is enabled, it will use `location` as the location from which the
// log message occurs.  A typical user will not specify `location`, allowing it
// to default to the current location. Note: `Enum` must not be
// `util::error::Code`.
template <typename Enum>
inline std::enable_if_t<EnumHasErrorSpace<Enum>::value, StatusBuilder>
MakeStatusBuilder(Enum code, absl::SourceLocation location =
                                 absl::SourceLocation::current()) {
  return StatusBuilder(::util::MakeStatus(code, ""), location);
}

// This constructor exists for backward compatibility and its usage will be
// migrated to the `absl::StatusCode` constructor.
[[deprecated("Use the overload that takes absl::StatusCode.")]]
inline StatusBuilder MakeStatusBuilder(
    util::error::Code code,
    absl::SourceLocation location = absl::SourceLocation::current()) {
  return StatusBuilder(static_cast<absl::StatusCode>(code), location);
}

// Creates a `StatusBuilder` from an `ErrorSpace` and a code.  If logging is
// enabled, it will use `location` as the location from which the log message
// occurs.  A typical user will not specify `location`, allowing it to default
// to the current location.
inline StatusBuilder MakeStatusBuilder(
    const ErrorSpace* space, int code,
    absl::SourceLocation location = absl::SourceLocation::current()) {
  return StatusBuilder(::util::MakeStatus(space, code, ""), location);
}

// Implementation details follow; clients should ignore.

inline util::error::Code GetCanonicalCode(const StatusBuilder& builder) {
  return static_cast<error::Code>(builder.code());
}

// Returns true iff the status created by the builder will have the code and
// associated error space of `code`. Intended to be called with enumerators from
// non-canonical error spaces.
// `util::HasErrorCode(StatusBuilder(Status(code, "")), code)` is always true.
// In particular, if the `code` is zero, returns true if `status_builder.ok()`.
// Sample usage:
//
//   StatusBuilder TeamPolicy(StatusBuilder builder) {
//     if (util::HasErrorCode(builder, frobber::kNoMoreFrobs)) {
//       builder.Log(absl::LogSeverity::kWarning);
//     }
//     return std::move(builder);
//   }
//
// REQUIRES: `code` is in an enum associated with an error space; see the
// `ErrorSpace` class documentation for details. Also, `code` must not be a
// `util::error::Code`.
template <typename Enum>
[[nodiscard]] decltype(::util::HasErrorCode(std::declval<const absl::Status&>(),
                                            std::declval<Enum>()))
HasErrorCode(const StatusBuilder& builder, Enum code) {
  auto* rep =
      absl::status_internal::StatusBuilderPrivateAccessor::GetRep(builder);
  return ::util::HasErrorCode(rep == nullptr ? absl::OkStatus() : rep->status,
                              code);
}
inline bool HasErrorCode(const StatusBuilder& builder,
                         ::util::error::Code code) {
  auto* rep =
      absl::status_internal::StatusBuilderPrivateAccessor::GetRep(builder);
  return ::util::HasErrorCode(rep == nullptr ? absl::OkStatus() : rep->status,
                              static_cast<absl::StatusCode>(code));
}

// Returns true iff the status created by this builder will have an error code
// equal to `code` and an error space equal to `space`.
// NOTE: Most error spaces can and should use the two-args `HasErrorCode()`
// function taking an enum. This overload is provided for error spaces such as
// util::PosixErrorSpace() which cannot use that template because they lack an
// associated Enum type.
inline bool HasErrorCode(const StatusBuilder& builder, const ErrorSpace* space,
                         int code) {
  auto* rep =
      absl::status_internal::StatusBuilderPrivateAccessor::GetRep(builder);
  return ::util::HasErrorCode(rep == nullptr ? absl::OkStatus() : rep->status,
                              space, code);
}

// Returns true iff the status created by this builder will have an error
// space equal to `space`.
inline bool HasErrorSpace(const StatusBuilder& builder,
                          const ErrorSpace* space) {
  auto* rep =
      absl::status_internal::StatusBuilderPrivateAccessor::GetRep(builder);
  return ::util::RetrieveErrorSpace(rep == nullptr ? absl::OkStatus()
                                                   : rep->status) == space;
}

// HasPayload()
//
// Indicates whether the Status object that will be returned by the
// StatusBuilder contains any payloads with a type extending proto2's
// `MessageSet`, returning `true` if so. Having a payload does not guarantee the
// presence of a payload with a specific type. Note that returning `false` does
// not necessarily indicate the absence of a payload, but only the absence on
// one which extends `MessageSet`.
inline bool HasPayload(const util::StatusBuilder& builder) {
  auto* rep =
      absl::status_internal::StatusBuilderPrivateAccessor::GetRep(builder);
  return rep != nullptr && ::util::HasPayload(rep->status);
}

// HasPayloadWithType()
//
// Indicates whether the Status object that will be returned by the
// StatusBuilder contains a payload with a type extending proto2's MessageSet,
// returning `true` if so. The extension identifier is specified as the second
// argument. This function implicitly invokes `HasPayload()`, so you do not need
// to call it alongside a `HasPayloadWithType()` call.
template <typename MessageSetExtension, typename ExtensionIdentifier>
inline bool HasPayloadWithType(const util::StatusBuilder& builder,
                               const ExtensionIdentifier& id) {
  auto* rep =
      absl::status_internal::StatusBuilderPrivateAccessor::GetRep(builder);
  return rep != nullptr &&
         util::HasPayloadWithType<MessageSetExtension>(rep->status, id);
}

// Indicates whether the Status object that will be returned by the
// StatusBuilder contains a payload with a type extending proto2's `MessageSet`,
// returning `true` if so. The extension identifier is expected to be accessible
// as `MessageSetExtension::message_set_extension()`. This function implicitly
// invokes `HasPayload()`, so you do not need to call it alongside a
// `HasPayloadWithType()` call.
template <typename MessageSetExtension>
inline bool HasPayloadWithType(const util::StatusBuilder& builder) {
  return util::HasPayloadWithType<MessageSetExtension>(
      builder, MessageSetExtension::message_set_extension);
}

// GetPayload()
//
// Returns a copy of a payload object with type MessageSetExtension. The second
// argument specifies the ExtensionIdentifier. Before calling GetPayload, you
// should check the presence of the payload with this type by invoking
// HasPayloadWithType with the same arguments. Otherwise this call will lead to
// crash in case if payload if absent.
template <typename MessageSetExtension, typename ExtensionIdentifier>
inline MessageSetExtension GetPayload(const util::StatusBuilder& builder,
                                      const ExtensionIdentifier& id) {
  auto* rep =
      absl::status_internal::StatusBuilderPrivateAccessor::GetRep(builder);
  ABSL_INTERNAL_CHECK(
      rep != nullptr,
      "Call to GetPayload should be guarded by the HasPayloadWithType check");

  return util::GetPayload<MessageSetExtension>(rep->status, id);
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
inline MessageSetExtension GetPayload(const util::StatusBuilder& builder) {
  return util::GetPayload<MessageSetExtension>(
      builder, MessageSetExtension::message_set_extension);
}

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_STATUS_STATUS_BUILDER_H_
