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

//
// This header defines the `StringRef` and `StringLabel` classes which
// respectively reference or manage string contents, or application defined
// identifier values that have a (human readable) string representation.
//
// `StringRef` and `StringLabel` are aliases to the `StringImpl<bool is_ref>`
// templated implementation, and are identical in both API and ABI with
// the following differences in behavior:
//
//   - `StringRef` is trivial and references existing string values
//
//   - `StringLabel` creates a copy of a string value if it doesn't have
//     proof that the referenced string value has an infinite lifetime.
//
//   - A "moved from" `StringLabel` instance will be reset to an empty value
//     after a move construct or move assign operation. `StringRef` has only
//     copy semantics and will never be "moved from".
//
// The intended use case is tracing and diagnostics where we often need to
// assign identifying and human-readable labels to certain events such as
// "SearchTopics" or "WaitForFinalFiber", and we want to minimize both the
// compute and memory requirements for such string values.
//
// `StringRef` and `StringLabel` have string compatible semantics: assuming
// we have some function `void Bar(StringRef label)`, we can call it as:
//
//   Bar("Hello world");
//   Bar(std::string("Hello world"));
//   Bar(absl::string_view("Hello world"));
//
// Additionally, a `StringRef` or `StringLabel` class can be instantiated
// from a source location value (`TraceSourceLocation`) in which case the
// created instance has a type of `kSourceLocation` with the location being
// returned through the `source_location()` property, or alternatively using
// the `file_name()` and `line()` properties.
//
// The purpose for capturing source locations is to provide a cheap default
// value for functions accepting names for tracing. For example:
//
//   int Select(..., StringRef name = TraceSourceLocation::current());
//
// We allow implicit conversions from `absl::SourceLocation` or any future
// standard such as (a more efficient) `std::source_location`. Specifically,
// we don't want applications to directly reference `TraceSourceLocation`,
// so we could also write the above example as:
//
//   int Select(..., StringRef name = absl::SourceLocation::current());
//
// The class contains optimizations for literal string values where supported by
// the compiler. For example, `StringRef("Hello world")` will record the string
// reference to be a literal value. A `StringLabel` instance created from a
// literal string reference will never create heap allocated storage for string
// literals. This reduces the RAM and CPU cost for the intended use case where
// the majority of values are constants.
//
// Both classes support user defined `Id` mappings which allows applications to
// map any strongly typed 32-bit integral value to a string representation. This
// implementation is inspired by the Tempus `IdOrString` implementation, but
// more flexible and generic.
//
// Applications can provide id to string mappings through a string registry.
// For example, assuming we have some strongly typed `Color` class, we can
// define a string registry that provides lookups for mapped `Id` values:
//
//   enum class Color { kRed, kGreen, kBlue };
//
//   struct Registry {
//     static absl::string_view Lookup(StringRef::Id id) {
//       static constexpr absl::string_view str[] = {"Red", "Green", "Blue"};
//       return id <= 2 ? str[id] : absl::string_view{};
//     }
//   };
//
// Additionally, an application can provide an implicit conversion with the
// predefined name `ToTraceStringRef()` which converts a strongly typed value
// to a `StringRef` identifying value for the provided value:
//
//   constexpr StringRef ToTraceStringRef(Color color) {
//     return StringRef::Create<Registry>(static_cast<StringRef::Id>(color));
//   }
//
// The above implicit conversion function allows direct implicit conversions
// from a `Color` to a `StringRef` or `StringLabel` value, for example:
//
//   void PrintLabel(StringRef label) {
//     std::cout << "The label is " << label << "\n";
//   }
//
//   PrintLabel(Color::kBlue);  // Prints "The label is Blue"
//
// Using identifiers is more efficient as it reduces the total size of any trace
// or diagnostics data we need to record, and avoids potentially costly string
// operations.
//
// String registries can be registered as a 'well known' string registry. For
// example, Tempus string id values are defined in `TempusId` proto files, and
// available for offline lookups. This means that such Tempus `Id` values can
// be persisted 'as is' using their native `id` value.
//
// Such libraries can be added to the `StringRegistry` enum defined inside the
// `string_id.h` header, and added as a predefined constant to our registry
// class, which will record this automatically in the `StringRef` instance:
//
//   struct Registry {
//     static inline constexpr kRegistry = StringRegistry::kColors;
//
//     static absl::string_view Lookup(perftools::tracing::StringRef::Id id) {
//       static constexpr absl::string_view str[] = {"Red", "Green", "Blue"};
//       return id <= 2 ? str[id] : absl::string_view{};
//     }
//   };
//
// There can be any number of registry classes marking themselves as a well
// known string registry provided they try to ensure that the generated
// id values never overlap. Identifier values of well known string registry
// values should never change. In our example, we could add enum values, but
// we should never change the value of the existing enum values once used.
#ifndef THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_STRING_LABEL_H_
#define THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_STRING_LABEL_H_

#include <cstddef>
#include <iosfwd>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "absl/types/source_location.h"
#include "gloop/perftools/tracing/string_base.h"
#include "gloop/perftools/tracing/string_registry.h"
#include "gloop/perftools/tracing/trace_source_location.h"

namespace perftools::tracing {

template <bool is_ref>
class ABSL_ATTRIBUTE_TRIVIAL_ABI StringImpl;

using StringRef = StringImpl<true>;
using StringLabel = StringImpl<false>;

template <bool is_ref>
class ABSL_ATTRIBUTE_TRIVIAL_ABI StringImpl : public StringBase {
 public:
  // StringImpl is movable, copyable and assignable.
  StringImpl() = default;
  StringImpl(StringImpl&& rhs) = default;
  StringImpl(const StringImpl& rhs) = default;
  StringImpl& operator=(StringImpl&& rhs) = default;
  StringImpl& operator=(const StringImpl& rhs) = default;

  // Cross StringRef/StringLabel copy construction.
  template <bool other>
  StringImpl(const StringImpl<other>& rhs) : data_(rhs.data_) {}  // NOLINT

  // Cross StringRef/StringLabel assignment.
  template <bool other>
  StringImpl& operator=(const StringImpl<other>& rhs) {
    data_ = rhs.data_;
    return *this;
  }

#if ABSL_HAVE_ATTRIBUTE(enable_if)

  // Creates a StringImpl from the provided string literal value.
  template <size_t n>
  constexpr StringImpl(const char (&value)[n])  // NOLINT
      __attribute__((enable_if(n > 0 && value[n - 1] == 0, "")))
      : data_(kLiteral, absl::string_view(value, n - 1)) {}

  // Creates a StringImpl from the compile-time constant string_view value.
  constexpr StringImpl(absl::string_view sv)  // NOLINT
      __attribute__((enable_if(sv.data() && (!sv.empty() || sv[0]), "")))
      : data_(kLiteral, sv) {}

#endif  // ABSL_HAVE_ATTRIBUTE(enable_if)

  // Creates a StringImpl from the provided const char*.
  //
  // Note: this function is templated with <typename = void> to avoid it being a
  // 'better' viable function than the constexpr enabled versions above. When
  // picking a function from the list of viable functions, c++ ranks non
  // template functions higher than templated functions.
  template <typename = void>
  constexpr StringImpl(const char* s)  // NOLINT
      : data_(s ? kDynamic : kEmptyContent, s) {}

  // Creates a StringImpl from the provided string_view value, or an empty
  // instance if `s` holds a default `{nullptr, 0}` value.
  template <typename = void>
  constexpr StringImpl(absl::string_view s)  // NOLINT
      : data_(s.data() ? kDynamic : kEmptyContent, s) {}

  // Creates a StringImpl from the provided string_view value.
  StringImpl(const std::string& s) : data_(kDynamic, s) {}  // NOLINT

  // Creates a StringImpl from the provided string_view value, explicitly
  // marking the referenced value as 'immortal'. This is useful for uses where
  // the application can guarantee the string lifetime such as interned strings.
  constexpr StringImpl(Immortal, absl::string_view s) noexcept
      : data_(kImmortal, s) {}

  // Creates a `StringImpl` instance from the given source location.
  constexpr StringImpl(TraceSourceLocation source_location)  // NOLINT
      : data_(source_location) {}
  constexpr StringImpl(absl::SourceLocation source_location)  // NOLINT
      : StringImpl(TraceSourceLocation(source_location)) {}

  // Creates a `StringImpl` instance with the given `id` value
  // for the string registry `Registry`.
  template <typename Registry>
  static constexpr StringImpl Create(Id id) noexcept {
    constexpr auto kRegistry = StringRegistryTraits<Registry>::kRegistry;
    return StringImpl(id, &Registry::Lookup, kRegistry);
  }

  // Creates an instance from the provided (strongly typed) T value, if there is
  // an explicit conversion function 'ToTraceStringRef()' defining an implicit
  // conversion to StringRef. See the class comments for more information.
  template <typename Id,
            typename R = decltype(ToTraceStringRef(std::declval<const Id&>())),
            typename StringRef = StringImpl<true>,
            std::enable_if_t<std::is_convertible_v<R, StringRef>, bool> = true>
  constexpr StringImpl(const Id& id)  // NOLINT
      : StringImpl(ToTraceStringRef(id)) {}

  // Returns the type of this instance.
  constexpr Type type() const { return data_.type(); }

  // Returns true if this instance represents an empty value.
  constexpr bool IsEmpty() const { return type() == kEmpty; }

  // Returns true if this instance contains an `Id` value.
  constexpr bool IsId() const { return type() == kStringId; }

  // Returns true if this instance references or holds a string value.
  constexpr bool IsString() const { return type() == kString; }

  // Returns true if this instance holds a source location.
  constexpr bool IsSourceLocation() const { return type() == kSourceLocation; }

  // Returns true if this instance is both trivial, and any string data
  // it potentially references has an infinite lifespan. Empty and StringId
  // values are always trivial / immortal. String values are immortal if they
  // reference either literal strings, or referencing string values explicitly
  // tagged as 'immortal'.
  constexpr bool IsImmortal() const { return data_.content_type() != kDynamic; }

  // Returns true if the referenced string value is a compile time string
  // literal, which is a special immortal value: it provides a strong
  // guarantee that the string does not contain any PII.
  constexpr bool IsLiteral() const { return data_.content_type() == kLiteral; }

  // Returns the referenced string value if IsString() == true. If this instance
  // contains an `id` value it will invoke the corresponding Lookup function and
  // return the returned string value or `string_view(}` if not found.
  // Returns `string_view{}` for all other types and empty instances.
  absl::string_view str() const ABSL_ATTRIBUTE_LIFETIME_BOUND;

  // Returns the strongly typed id value if IsId() == true, `Id{0}` otherwise.
  constexpr Id id() const { return IsId() ? data_.id() : Id{0}; }

  // Returns the well known string registry for this value or `kNoRegistry`.
  constexpr StringRegistry registry() const { return data_.registry(); }

  // Returns the file name if this instance holds a source location, else
  // returns a pointer to an empty string. Never returns null.
  constexpr const char* absl_nonnull file_name() const {
    return IsSourceLocation() ? data_.file_name() : "";
  }

  // Returns the line number if this instance holds a source location.
  constexpr int line() const { return IsSourceLocation() ? data_.line() : 0; }

  // Returns the source location if this instance holds a source location.
  constexpr TraceSourceLocation source_location() const;

  // Returns a copy of the current instance with infinite lifetime.
  //
  // All values that are not a string type (empty, ID, source location)
  // have infinite lifetime and are returned 'as is'. String type values are
  // inspected for being either a compile time constant or immortal value.
  // This function checks if the current instance already has an infinite
  // lifetime. If not, it creates a copy of the value with an infinite lifetime
  // using an internal global hash table containing all interned string values.
  //
  // IMPORTANT: this function does not guarantee that returned string values are
  // uniquely identified by the memory address of the contained character data.
  // For example, there can be multiple string literals and string values
  // explicitly marked as 'Immortal' that have the same contents stored in
  // different memory locations which are all returned 'as is', as they are
  // already explicitly having an infinite lifetime.
  StringImpl Intern() const {
    switch (data_.content_type()) {
      case ContentType::kEmpty:
      case ContentType::kStringId:
      case ContentType::kSourceLocation:
      case ContentType::kImmortal:
        // Explicit (immortal) value contents: make a raw copy.
        return StringImpl(RawCopy(), *this);
      case ContentType::kLiteral:
        // For now be overly prudent with string literals
        break;
      case ContentType::kDynamic:
        // No guarantee about lifetime of the string contents.
        break;
    }
    return StringImpl(Immortal(), StringBase::InternString(data_.str()));
  }

  // Equality. Allows cross comparing StringRef and StringLabel values.
  // Comparison works as follows:`
  // - values must be of the same basic type (empty, Id, string)
  // - for string types, they must have equal string values.
  // - for id values, they must have the same id value, and either belong
  //   to the same well known registry, or have the same Lookup function.
  template <bool r1, bool r2>
  friend bool operator==(const StringImpl<r1>&, const StringImpl<r2>&);

  friend void swap(StringImpl& lhs, StringImpl& rhs) noexcept {
    swap(lhs.data_, rhs.data_);
  }

  // Formats `label` depending on the contained data. If `label` contains a
  // string or id value, it will render the `str()` value into `sink`. If
  // `label` contains a source location, it will use the stringification for
  // TraceSourceLocation. Else it will leave the sink empty.
  template <typename Sink, bool r>
  friend void AbslStringify(Sink& sink, const StringImpl<r>& label);

 private:
  // Convenience constants to reduce code verbosity.
  static inline constexpr ContentType kEmptyContent = ContentType::kEmpty;
  static inline constexpr ContentType kDynamic = ContentType::kDynamic;
  static inline constexpr ContentType kLiteral = ContentType::kLiteral;
  static inline constexpr ContentType kImmortal = ContentType::kImmortal;

  using RawCopy = StringBase::RawCopy;
  using Data = StringBase::Data<is_ref>;
  friend class StringImpl<!is_ref>;

  // Creates a raw copy of the provided instance
  template <bool r>
  StringImpl(RawCopy, const StringImpl<r>& s) : data_(RawCopy(), s.data_) {}

  constexpr StringImpl(Id id, LookupFn fn, StringRegistry r) noexcept
      : data_(id, fn, r) {}

  absl::string_view id_as_str() const {
    DCHECK_EQ(type(), kStringId);
    DCHECK_NE(data_.lookup_fn(), nullptr);
    return data_.lookup_fn()(data_.id());
  }

  Data data_;
};

template <bool is_ref>
absl::string_view StringImpl<is_ref>::str() const
    ABSL_ATTRIBUTE_LIFETIME_BOUND {
  switch (type()) {
    case kStringId:
      return id_as_str();
    case kString:
      return data_.str();
    default:
      return absl::string_view();
  }
}

template <bool is_ref>
constexpr TraceSourceLocation StringImpl<is_ref>::source_location() const {
  return IsSourceLocation()
             ? TraceSourceLocation(TraceSourceLocation::Access(),
                                   data_.file_name(), data_.line())
             : TraceSourceLocation();
}

template <bool r1, bool r2>
inline bool operator==(const StringImpl<r1>& lhs, const StringImpl<r2>& rhs) {
  if (lhs.type() == rhs.type() && lhs.registry() == rhs.registry()) {
    switch (lhs.type()) {
      case StringBase::kEmpty:
        return true;
      case StringBase::kStringId:
        return lhs.id() == rhs.id() &&
               (lhs.registry() != StringRegistry::kNoRegistry ||
                lhs.data_.lookup_fn() == rhs.data_.lookup_fn());
      case StringBase::kString:
        return lhs.str() == rhs.str();
      case StringBase::kSourceLocation:
        return lhs.file_name() == rhs.file_name() && lhs.line() == rhs.line();
    }
    ABSL_UNREACHABLE();
  }
  return false;
}

template <bool r, typename T>
inline bool operator==(const StringImpl<r>& lhs, const T& rhs) {
  return lhs == StringImpl<true>(rhs);
}

template <bool r, typename T>
inline bool operator!=(const StringImpl<r>& lhs, const T& rhs) {
  return lhs != StringImpl<true>(rhs);
}

template <bool r, typename T>
inline bool operator==(const T& lhs, const StringImpl<r>& rhs) {
  return StringImpl<true>(lhs) == rhs;
}

template <bool r, typename T>
inline bool operator!=(const T& lhs, const StringImpl<r>& rhs) {
  return StringImpl<true>(lhs) != rhs;
}

template <bool r1, bool r2>
inline bool operator!=(const StringImpl<r1>& lhs, const StringImpl<r2>& rhs) {
  return !operator==(lhs, rhs);
}

std::ostream& operator<<(std::ostream& stream, const StringRef& label);
std::ostream& operator<<(std::ostream& stream, const StringLabel& label);

template <typename Sink, bool is_ref>
void AbslStringify(Sink& sink, const StringImpl<is_ref>& label) {
  switch (label.type()) {
    case StringBase::kEmpty:
      break;
    case StringBase::kStringId:
    case StringBase::kString:
      sink.Append(label.str());
      break;
    case StringBase::kSourceLocation:
      AbslStringify(sink, label.source_location());
      break;
  }
}

}  // namespace perftools::tracing

#endif  // THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_STRING_LABEL_H_
