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

// IWYU pragma: private, include "util/gtl/extend/debug_printing.h"

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_REFLECTION_EXTENSION_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_REFLECTION_EXTENSION_H_

#include <array>
#include <cstddef>
#include <utility>

#include "absl/base/no_destructor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gloop/util/gtl/extend/extend.h"
#include "gloop/util/gtl/extend/internal/reflection.h"

namespace gtl {
// Forward declaration.
namespace internal_extend {
template <typename Write, typename WriteGeneric, typename T>
void DebugPrintValue(Write&&, WriteGeneric&&, const T&);
}  // namespace internal_extend

// An extension which tries to produce a `FieldNameInfo` with the names of T's
// fields.
template <class T>
struct ReflectionExtension : Extension<ReflectionExtension, T> {
  // Private to prevent structs and non allow-listed extensions from calling
  // GetFieldNames().
 private:
  static const auto& GetFieldNames(const T& value);

  using Extension<ReflectionExtension, T>::Unpack;

  // Allow-listed extensions.
  template <typename Write, typename WriteGeneric, typename U>
  friend void internal_extend::DebugPrintValue(Write&&, WriteGeneric&&,
                                               const U&);
  friend struct ::gtl::internal_extend::ReflectionTestingExtension<T>;
};

// Implementation below.

// static
template <typename T>
const auto& ReflectionExtension<T>::GetFieldNames(const T& value) {
  using FieldNameInfo = internal_extend::FieldNameInfo<std::tuple_size<
      decltype(ReflectionExtension<T>::Unpack(std::declval<T>()))>::value>;

  // The fact that it is marked `static` is important: Field names are
  // parsed as an effect of this initialization. Marking this variable as
  // `static` guarantees that initialization happens exactly once and
  // ensures that it is thread-safe.
  static const absl::NoDestructor<FieldNameInfo> kResult([&value]() {
    auto result = FieldNameInfo();
#if GTL_EXTEND_PARSE_FIELD_NAMES
    // NOTE: The `__builtin_dump_struct` Clang intrinsic is not supported in
    // general, has no guaranteed API or semantics and is subject to
    // breaking without notice. In general, it should *not* be used. We have
    // explicit permission to use it here for this one use case. Do not copy
    // this usage into any other project. If you would like to use
    // `__builtin_dump_struct`, please contact the OWNERS of this project
    // for guidance.
    std::array<absl::string_view, FieldNameInfo::kFieldCount> field_names;
    internal_extend::ParsingState state;

    __builtin_dump_struct(std::addressof(value), internal_extend::PrintfHijack,
                          state, absl::MakeSpan(field_names));
    // If parsing succeeds, `state.index` will refer to one-passed-the-end
    // and therefore be equal to the field count.
    result.field_names = field_names;
    result.success = (state.index == FieldNameInfo::kFieldCount);
#else
    (void)value;  // Avoid unused-capture warning
#endif
    return result;
  }());
  return *kResult;
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_REFLECTION_EXTENSION_H_
