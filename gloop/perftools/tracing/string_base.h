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

#ifndef THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_STRING_BASE_H_
#define THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_STRING_BASE_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iosfwd>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "gloop/perftools/tracing/string_registry.h"
#include "gloop/perftools/tracing/trace_source_location.h"

namespace perftools::tracing {

// StringBase is the non-virtual base class for the templated StringImpl<>
// implementation, providing the types and definitions used in the string
// implementations and free helper functions for creating, copying and deleting
// dynamically allocated data.
class ABSL_ATTRIBUTE_TRIVIAL_ABI StringBase {
 public:
  // `Type` defines the logical contents type managed by a `StringRef` instance.
  enum Type : uint16_t {
    kEmpty,           // An empty instance
    kStringId,        // An `Id` value which can be accessed through `id()`
    kSourceLocation,  // A source location value which can be accessed
                      // through `file_name()` and `line()`
    kString           // A string value which can be accessed through `str()`
  };

  // String identifiers are 32 bit numerical values.
  using Id = uint32_t;

  // Function signature for the `Registry::Lookup()` function.
  using LookupFn = absl::string_view (*)(Id);

  // Immortal is a tag that can be used to indicate a provided string value's
  // lifetime is infinite. For example, a value referencing an interned string.
  //
  //   absl::string_view sv = InternStringValue(input.method());
  //   Region region(StringRef::Immortal(), sv);
  struct Immortal {};

  // RawCopy is a tag that can be used to force a data value to be created as a
  // a raw, bitwise copy of an existing value. This is used in cases where we
  // already have proof of such a bitwise copy being correct.
  struct RawCopy {};

  // The concrete content type managed by a `StringRef` instance.
  enum class ContentType : uint16_t {
    kEmpty,           // An empty instance
    kStringId,        // An `Id` value which can be accessed through `id()`
    kSourceLocation,  // A source location value which can be accessed
                      // through `file_name()` and `line()`
    kDynamic,         // A heap allocated string value
    kImmortal,        // A string value with an infinite lifetime.
    kLiteral          // A compile-time string literal value.
  };

  // Creates an infinite lifetime copy of the provided value by looking up the
  // provided value in an internal hash map of values, inserting it as needed.
  // This function is thread-safe, and can be called concurrently by any number
  // of threads. The returned value is guaranteed to be immutable and has an
  // infinite lifetime: i.e., the value remains valid until the process exits.
  static absl::string_view InternString(absl::string_view s);

 private:
  // `Metadata` holds metadata for the contents depending on the value of `type`
  // - type `kEmpty':
  //   `registry`     := `kNoRegistry`
  //   `length_or_id` := 0
  // - type `kStringId':
  //   `registry`     := well known registry or `kNoRegistry`
  //   `length_or_id` := <application mapped `Id` value>
  // - type `kDynamic|kImmortal|kLiteral` (string):
  //   `registry`     := `kNoRegistry`
  //   `length_or_id` := length of the contained string (reference)
  // - type `kSourceLocation':
  //   `registry`     := `kNoRegistry`
  //   `length_or_id` := line number
  struct Metadata {
    constexpr Metadata() = default;
    constexpr Metadata(const Metadata&) = default;
    constexpr Metadata& operator=(const Metadata&) = default;

    constexpr Metadata(StringRegistry registry, Id id) noexcept
        : type(ContentType::kStringId), registry(registry), length_or_id(id) {}
    constexpr Metadata(ContentType type, size_t length) noexcept
        : type(type), length_or_id(static_cast<uint32_t>(length)) {}
    explicit constexpr Metadata(TraceSourceLocation source_location) noexcept
        : type(ContentType::kSourceLocation),
          length_or_id(source_location.line()) {}

    ContentType type{ContentType::kEmpty};
    StringRegistry registry{StringRegistry::kNoRegistry};
    uint32_t length_or_id{0};
  };

  // `Value` holds a pointer to the string data (string types), the `Lookup`
  // function of the registry for an id value, the `file_name` value for a
  // source location, or `str = nullptr` if empty.
  struct Value {
    constexpr Value() noexcept : str(nullptr) {}
    constexpr Value(const Value&) = default;
    constexpr Value& operator=(const Value&) = default;

    explicit constexpr Value(const char* str) : str(str) {}
    explicit constexpr Value(LookupFn lookup_fn) : lookup_fn(lookup_fn) {}
    explicit constexpr Value(TraceSourceLocation source_location)
        : file_name(source_location.file_name()) {}

    union {
      const char* str;
      LookupFn lookup_fn;
      const char* absl_nonnull file_name;
    };
  };

  // `Raw` is the trivial aggregate class containing  `Value` and `Metadata`.
  //
  // Both `Value` and `Metadata` are 64-bit values, allowing the `Data` class,
  // and by extension the `StringImpl` class, to be passed into functions as a
  // trivial two register value on both x86 and ARM.
  //
  // The static 'CreateValue()`, `CopyValue()` and `DeleteValue` methods are
  // deliberately free functions instead of non static member functions. Out of
  // line member functions require a `this` pointer, forcing a value that could
  // otherwise be kept in registers to be spilled on the stack. On top of that,
  // such out of line member calls 'leak' the `this` pointer, so the compiler
  // assumes all member values to be clobbered. Free functions allow all data
  // to be passed in and returned as register values without side effects.
  struct Raw {
    Value value;
    Metadata metadata;

    // Creates a heap allocated value of type `kDynamic` holding a copy of `s`
    static Raw CreateValue(absl::string_view s);

    // Creates a heap allocated string value holding a copy of `raw`
    // Requires that `raw` contains a dynamic string value.
    static Raw CopyValue(Raw raw);

    // Deletes the heap allocated string value in `raw`
    // Requires that `raw` contains a dynamic string value.
    static void DeleteValue(Raw raw) noexcept;

    friend void swap(Raw& lhs, Raw& rhs) noexcept {
      // Compilers are pesky little children: http://gcc.godbolt.org/z/MPWGWKqTT
      auto tmp1 = lhs.value;
      auto tmp2 = lhs.metadata;
      auto tmp3 = rhs.value;
      auto tmp4 = rhs.metadata;
      rhs.value = tmp1;
      rhs.metadata = tmp2;
      lhs.value = tmp3;
      lhs.metadata = tmp4;
    }
  };

 protected:
  // `Data` manages the `Raw` string contents. The class provides all value
  // semantics: constructors and assignment operators including copy and
  // assignment operations across `StringRef` and `StringLabel` values.
  // c++ is quite pesky about destructors being defined. Even if you define them
  // as ` = default` using template specialization, c++ does not consider such
  // classes trivially destructible. To be portable to IOS and other platform
  // not being on c++20, we can't use a constexpr destructor, so we use full
  // class template specialization. The default implementation is the raw / ref
  // version defaulting all constructors and assignment.
  template <bool is_ref>
  class ABSL_ATTRIBUTE_TRIVIAL_ABI Data {
   public:
    using Other = Data<!is_ref>;

    // Default, copy and move construct and assign
    constexpr Data() = default;
    constexpr Data(Data&& rhs) = default;
    constexpr Data(const Data& rhs) = default;
    constexpr Data& operator=(Data&& rhs) = default;
    constexpr Data& operator=(const Data& rhs) = default;

    // Implicit conversion constructor from ref / dynamic value.
    constexpr Data(const Other& rhs) noexcept : raw_(rhs.raw_) {}  // NOLINT

    // Explicit constructor for string value.
    constexpr Data(ContentType type, absl::string_view s) noexcept
        : raw_{Value(s.data()), Metadata(type, s.size())} {}

    // Explicit constructor for string id types.
    constexpr Data(Id id, LookupFn fn, StringRegistry registry) noexcept
        : raw_{Value(fn), Metadata(registry, id)} {}

    // Explicit constructor for TraceSourceLocation.
    explicit constexpr Data(TraceSourceLocation source_location) noexcept
        : raw_{Value(source_location), Metadata(source_location)} {}

    // Explicit constructor to create a raw, bitwise copy.
    // Requires the content to not be dynamic.
    Data(RawCopy, const Data& rhs) : raw_(rhs.raw_) {
      DCHECK_NE(rhs.raw_.metadata.type, ContentType::kDynamic);
    }

    // Implicit conversion assignment from ref / dynamic value.
    constexpr Data& operator=(const Other& rhs) noexcept {
      raw_ = rhs.raw_;
      return *this;
    }

    // public properties
    constexpr ContentType content_type() const { return raw_.metadata.type; }
    constexpr LookupFn lookup_fn() const { return raw_.value.lookup_fn; }
    constexpr Id id() const { return raw_.metadata.length_or_id; }
    constexpr StringRegistry registry() const { return raw_.metadata.registry; }
    constexpr const char* data() const { return raw_.value.str; }
    constexpr size_t size() const { return raw_.metadata.length_or_id; }
    constexpr absl::string_view str() const { return {data(), size()}; }
    constexpr const char* absl_nonnull file_name() const {
      return raw_.value.file_name;
    }
    constexpr int line() const {
      return static_cast<int>(raw_.metadata.length_or_id);
    }

    // Logical type.
    constexpr Type type() const {
      switch (content_type()) {
        case ContentType::kEmpty:
          return kEmpty;
        case ContentType::kStringId:
          return kStringId;
        case ContentType::kSourceLocation:
          return kSourceLocation;
        case ContentType::kDynamic:
        case ContentType::kImmortal:
        case ContentType::kLiteral:
          return kString;
      }
      ABSL_UNREACHABLE();
      return Type::kEmpty;
    }

    friend void swap(Data& rhs, Data& lhs) noexcept {
      using std::swap;
      swap(lhs.raw_, rhs.raw_);
    }

   private:
    friend class Data<!is_ref>;

    explicit constexpr Data(Raw raw) noexcept : raw_(raw) {}

    Raw raw_;
  };
};

template <>
class ABSL_ATTRIBUTE_TRIVIAL_ABI StringBase::Data<false> : public Data<true> {
 public:
  using Base = Data<true>;

  ~Data() noexcept { Destroy(); }
  constexpr Data() = default;
  constexpr Data(Data&& rhs) noexcept : Base(rhs.raw_) { rhs.raw_ = {}; }
  constexpr Data(const Data& rhs) : Base(Copy(rhs.raw_)) {}
  constexpr Data(const Base& rhs) : Base(Copy(rhs.raw_)) {}  // NOLINT

  constexpr Data(ContentType type, absl::string_view s)
      : Base(Create(type, s)) {}

  constexpr Data(Id id, LookupFn fn, StringRegistry registry) noexcept
      : Base(id, fn, registry) {}

  constexpr explicit Data(TraceSourceLocation source_location) noexcept
      : Base(source_location) {}

  Data(RawCopy, const Data& rhs) : Base(RawCopy(), rhs) {}

  constexpr Data& operator=(const Data& rhs) {
    Destroy();
    raw_ = Copy(rhs.raw_);
    return *this;
  }

  constexpr Data& operator=(Data&& rhs) noexcept {
    Destroy();
    raw_ = rhs.raw_;
    rhs.raw_ = {};
    return *this;
  }

  constexpr Data& operator=(const Base& rhs) {
    Destroy();
    raw_ = Copy(rhs.raw_);
    return *this;
  }

  friend void swap(Data& rhs, Data& lhs) noexcept {
    using std::swap;
    swap(lhs.raw_, rhs.raw_);
  }

 private:
  friend class Data<true>;

  // Creates a heap allocated copy of `s` if `type` is `kDynamic`.
  static constexpr Raw Create(ContentType type, absl::string_view s) {
    Raw raw{Value(s.data()), Metadata(type, s.size())};
    return type == ContentType::kDynamic ? Raw::CreateValue(s) : raw;
  }

  // Returns a heap allocated copy of `raw` if `raw` has type `kDynamic`.
  static constexpr Raw Copy(Raw raw) {
    ContentType type = raw.metadata.type;
    return type == ContentType::kDynamic ? Raw::CopyValue(raw) : raw;
  }

  // Deletes any dynamic allocated value.
  constexpr void Destroy() noexcept {
    ContentType type = raw_.metadata.type;
    if (type == ContentType::kDynamic) Raw::DeleteValue(raw_);
  }
};

std::ostream& operator<<(std::ostream&, StringBase::Type);
std::ostream& operator<<(std::ostream& stream, StringBase::ContentType type);

}  // namespace perftools::tracing

#endif  // THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_STRING_BASE_H_
