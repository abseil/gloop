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

// An ErrorSpace is a collection of related numeric error codes.  For example,
// all Posix errno values may be placed in the same ErrorSpace, etc.
//
// We recommend that new APIs use the canonical error space instead of creating
// a new error space per API (<link>).
//
// Clients should use the provided `ErrorSpaceImpl` helper to manage creation
// and registration of their space.
//
// Clients should access their space with `MyErrorSpace::Get()`.
//
// Clients wishing to use a proto enum to define an `ErrorSpace` should use
// `ProtoEnumErrorSpace<T>` from util/task/contrib/proto_status/proto_status.h.
//
// Clients wishing to associate an `ErrorSpace*` with an enum must define the
// function `GetErrorSpace(ErrorSpaceAdlTag<Enum>)` which returns a pointer to
// their custom `ErrorSpace` in the same file and namespace as their Enum.  For
// example:
//
//   namespace cobbler {
//
//   // Convenience enum to make writing code involving this error space more
//   // readable; however, the error space must be prepared to handle error
//   // codes outside this range as an individual status can come from different
//   // binaries (e.g. serialized across RPC calls).
//   //
//   // Error spaces should avoid having a zero enum value.  An error code of
//   // zero will always construct an OK status in the canonical space (i.e.
//   // the error space and message are ignored).
//   enum ShoeErrors {
//     kNoSole = 1,
//     kBrokenTongue = 2,
//   };
//   struct ShoeErrorSpace : util::ErrorSpaceImpl<ShoeErrorSpace> {
//     // Returns a globally unique name.  No other restrictions are made on the
//     // structure of the name.  The name will be used when serializing errors
//     // so changes to it have the same requirements as breaking changes to
//     // protocol buffers.
//     static string space_name();
//
//     // Returns a string representation of the error code.  The result is
//     // intended for debugging and does not need to repeat the error space's
//     // name.  `code` is guaranteed to be non-zero but might be outside the
//     // range of any associated enum (it can come from a different binary).
//     static string code_to_string(int code);
//
//     // Returns the `absl::StatusCode` corresponding to the error code
//     // associated with this ErrorSpace. The input error code should always
//     // be non-zero but might be outside the range of the associated enum
//     // (it can come from a different binary ). Implementations should avoid
//     // returning `absl::StatusCode::kOk`.
//     static absl::StatusCode canonical_code(int code);
//   };
//
//   // Registers a mapping between `ShoeErrors` and `ShoeErrorSpace`, allowing
//   // users to construct and test `Status` values.  For example:
//   // - `return util::MakeStatus(cobbler::kNoSole, "rubber melted");`
//   // - `if (util::HasErrorCode(status, cobbler::kBrokenTongue)) { ... }`
//   inline const util::ErrorSpace* GetErrorSpace(
//       util::ErrorSpaceAdlTag<ShoeErrors>) {
//     return ShoeErrorSpace::Get();
//   }
//   }  // namespace cobbler
//

#ifndef THIRD_PARTY_GLOOP_UTIL_STATUS_ERROR_SPACE_H_
#define THIRD_PARTY_GLOOP_UTIL_STATUS_ERROR_SPACE_H_

#include <string>
#include <type_traits>

#include "absl/meta/type_traits.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

// Forward declare `rpc::status::UnknownErrorSpace` so we can friend it and have
// it call `ErrorSpace::Register`.
namespace rpc {
namespace status {
class UnknownErrorSpace;
}  // namespace status
}  // namespace rpc

namespace util {
class ErrorSpaceTestPeer;

template <typename T>
class ErrorSpaceImpl;

// Base for all error spaces.  An error space is a collection of related error
// codes.  All error codes must be non-zero.  Zero always means success.
class ErrorSpace {
 public:
#ifndef SWIG
  ErrorSpace(const ErrorSpace&) = delete;
  void operator=(const ErrorSpace&) = delete;
#endif  // SWIG

  // Returns the name of this error space
  absl::string_view SpaceName() const;

  // Returns a string corresponding to the specified error code.
  std::string String(int code) const;

  // Returns the equivalent canonical code for the given error code associated
  // with this ErrorSpace. ErrorSpace implementations should override this
  // method to provide a custom mapping, and return UNKNOWN for unrecognized
  // values.
  //
  // NOTE: This is an implementation detail of Status and should otherwise
  // be called rarely.  Prefer `Status::code()` or `util::ToCanonical()`.
  // These honor any canonical code assigned to the particular status,
  // set either by way of `util::SetCanonicalCode()` or
  // `util::MakeStatusFromProto()`.
  absl::StatusCode CanonicalCode(int code) const;

  // Finds the error-space with the specified name.  Return the
  // space object, or `nullptr` if not found.
  //
  // NOTE: Do not call Find() until after InitGoogle() returns.
  // Otherwise, some module intializers that register error spaces may not
  // have executed and Find() might not locate the error space of
  // interest.
  // TODO: Find() always returns null in Microsoft Visual Studio
  // builds due to compiler bugs.
  // TODO: Mark ErrorSpace const. Requires call side cleanups.
  static ErrorSpace* Find(absl::string_view name);

 protected:
  // typedef instead of using statements for SWIG compatibility.
  typedef absl::string_view (*SpaceNameFunc)(const ErrorSpace* space);
  typedef std::string (*CodeToStringFunc)(const ErrorSpace* space, int code);
  typedef absl::StatusCode (*CanonicalCodeFunc)(const ErrorSpace* space,
                                                int code);

  constexpr ErrorSpace(SpaceNameFunc space_name_func,
                       CodeToStringFunc code_to_string_func,
                       CanonicalCodeFunc canonical_code_func)
      : space_name_func_(space_name_func),
        code_to_string_func_(code_to_string_func),
        canonical_code_func_(canonical_code_func) {}

  template <bool* addr>
  struct OdrUse {
    constexpr OdrUse() : b(*addr) {}
    bool& b;
  };
  template <typename T>
  struct Registerer {
#ifndef SWIG
    static bool register_token;
    // By using the address of register_token, the compiler is forced to run
    // its initializer.  Without this a compiler can optimize out the token and
    // registration.
    static constexpr OdrUse<&register_token> kRegisterTokenUse{};
#endif  // SWIG
  };

 private:
  // `UnknownErrorSpace` need access to `Register` to register the unknown
  // error spaces so `Find` can find them.
  friend rpc::status::UnknownErrorSpace;
  friend ErrorSpaceTestPeer;

  // Function pointers don't handle covariant return types, so we use the base
  // form during registration.
  template <typename T>
  static const ErrorSpace* GetBase() {
    return T::Get();
  }

  // Registers an error space in the global map of all spaces.  Returns true for
  // easy use in global initialization.  `space` will be called once on first
  // use of `ErrorSpace::Find` which is after `GoogleInit`.
  static bool Register(const ErrorSpace* (*space)());

  // NOTE: This `Register` is introduced only to be used by
  // `rpc::status::UnknownErrorSpace::FindOrCreate`.
  // Registers an error space in the global map of all spaces. `space` will be
  // inserted into the global map.
  static void Register(absl::string_view name, const ErrorSpace* space);

  static ErrorSpace* FindFast_Locked(const std::string& name);

  const SpaceNameFunc space_name_func_;
  const CodeToStringFunc code_to_string_func_;
  const CanonicalCodeFunc canonical_code_func_;
};

template <typename T>
bool ErrorSpace::Registerer<T>::register_token =
    Register(&ErrorSpace::GetBase<T>);

// Manages creation and registration of error space subclasses.  See file
// comments above for detailed documentation.
template <typename T>
class ErrorSpaceImpl : public ErrorSpace {
 public:
#ifndef SWIG
  constexpr ErrorSpaceImpl()
      : ErrorSpace(&ErrorSpaceImpl::SpaceNameImpl,
                   &ErrorSpaceImpl::CodeToStringImpl,
                   &ErrorSpaceImpl::CanonicalCodeImpl) {}

  // Returns the canonical instance of the `T` error space.
  static constexpr const T* Get();
#endif  // SWIG

 private:
  // These functions adapt the stateful implementation that takes a space
  // pointer to stateless static methods, so that clients of ErrorSpaceImpl are
  // safe to have constexpr global instances.
  static absl::string_view SpaceNameImpl(const ErrorSpace* /*space*/) {
    return T::space_name();
  }

  static std::string CodeToStringImpl(const ErrorSpace* /*space*/, int code) {
    return T::code_to_string(code);
  }

#ifndef SWIG
  static absl::StatusCode CanonicalCodeImpl(const ErrorSpace*, int code) {
    return T::canonical_code(code);
  }

  static constexpr Registerer<ErrorSpaceImpl> kRegisterer{};
#endif  // SWIG
};

#ifndef SWIG

// Marks the owner of the ADL extension point explicitly in implementations.
// See the class documentation on `ErrorSpace` for details.
template <typename E>
struct ErrorSpaceAdlTag {};

namespace internal_status {

template <typename E, typename = void>
struct EnumHasErrorSpace : std::false_type {};

template <typename E>
struct EnumHasErrorSpace<
    E, std::void_t<decltype(GetErrorSpace(ErrorSpaceAdlTag<E>{})),
                   typename std::enable_if<std::is_enum<E>::value>::type>>
    : std::true_type {};

}  // namespace internal_status

// Trait type for detecting if calls to `GetErrorSpaceForEnum(E{})` is well
// formed.  Do not specialize this trait.
template <typename E>
struct EnumHasErrorSpace : internal_status::EnumHasErrorSpace<E> {};

// Gets the space associated with an enum using an Argument Dependent Lookup
// extension point (ADL).  See the file documentation for details.
template <typename E,
          typename = typename std::enable_if<EnumHasErrorSpace<E>::value>::type>
const ErrorSpace* GetErrorSpaceForEnum(E) {
  return GetErrorSpace(ErrorSpaceAdlTag<E>{});
}

// -----------------------------------------------------------------
// Implementation details follow

namespace internal_status {

// Provides a global constexpr instance of the error space `T`.
// We need the indirection because ErrorSpaceImpl can't declare constexpr
// instances of T since it is not yet fully declared.
template <typename T>
struct ErrorSpaceInstance {
  static constexpr T value = {};
};

template <typename T>
constexpr T ErrorSpaceInstance<T>::value;

}  // namespace internal_status

template <typename T>
constexpr const T* ErrorSpaceImpl<T>::Get() {
  return &internal_status::ErrorSpaceInstance<T>::value;
}

// The following macros are used to explicitly instantiate `ErrorSpaceInstance`
// for a particular `ErrorSpace`. By putting the `DECLARE` macro in a .h file
// and the `DEFINE` in a .cc file, we ensure that the explicit instantiation
// definition is only in one .cc file and thus work around the linker issue in
// Python (the linker we use for Python doesn't deduplicate symbols).
// More details: <link>.
// NOTE: These macros should ONLY be applied to ErrorSpace that are used in
// Python to work around the above issue.
#define DECLARE_ERROR_SPACE_INSTANTIATION_ONLY_FOR_PYTHON_WORKAROUND(x) \
  extern template struct util::internal_status::ErrorSpaceInstance<x>

#define DEFINE_ERROR_SPACE_INSTANTIATION_ONLY_FOR_PYTHON_WORKAROUND(x) \
  template struct util::internal_status::ErrorSpaceInstance<x>

#endif  // SWIG

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_STATUS_ERROR_SPACE_H_
