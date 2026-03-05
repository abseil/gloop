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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_EXTENSION_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_EXTENSION_H_

namespace gtl {
// Forward declaration.
namespace internal_extend {
template <typename Write, typename WriteGeneric, typename T>
void DebugPrintValue(Write&&, WriteGeneric&&, const T&);
}  // namespace internal_extend

// A base class for extensions that add methods to the extended class that
// allows accessing the type it's extending as a tuple.
template <template <typename> typename Ext, typename T>
class Extension {
 private:
  // Downcast this into the type it's extending.
  constexpr const T& AsExtendedType() const {
    return *static_cast<const T*>(this);
  }
  constexpr T& AsExtendedType() { return *static_cast<T*>(this); }

  // Unpack this object itself as a tuple.  Used as `this->UnpackThis()`.
  constexpr auto UnpackThis() const { return AsExtendedType().Unpack(); }
  constexpr auto UnpackThis() { return AsExtendedType().Unpack(); }

  // Unpack other objects of type `T`. For example, when writing `MyExtension`
  // and given `T& t`, it can be unpacked with `MyExtension::Unpack(t)`.
  constexpr static auto Unpack(const T& t) { return t.Unpack(); }
  constexpr static auto Unpack(T& t) { return t.Unpack(); }

  friend Ext<T>;
  template <typename Write, typename WriteGeneric, typename U>
  friend void internal_extend::DebugPrintValue(Write&&, WriteGeneric&&,
                                               const U&);
};

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_EXTENSION_H_
