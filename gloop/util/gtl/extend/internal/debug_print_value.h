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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_INTERNAL_DEBUG_PRINT_VALUE_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_INTERNAL_DEBUG_PRINT_VALUE_H_

#if GTL_EXTEND_PARSE_FIELD_NAMES
#include <cstddef>
#endif

#include <cstdint>
#include <type_traits>
#include <utility>

#include "gloop/util/gtl/extend/internal/reflection.h"
#include "gloop/util/gtl/extend/reflection_extension.h"

namespace gtl::internal_extend {

// Base implementation for StringifyExtension and StreamPrintingExtension.
inline bool DebugPrintingJitter() {
  // We use ASLR to get a somewhat randomized pointer.
  static const bool kJitter = []() {
    return reinterpret_cast<uintptr_t>(&kJitter) % 13 > 6;
  }();
  return kJitter;
}

// Generates a human-readable string representation of "value".
// T must be a gtl::Extend type with gtl::ReflectionExtension.
// "write" and "write_generic" are functors used to produce the output. "write"
// must be invocable with a string representing raw pretty-printed text, whereas
// "write_generic" must be invocable with an arbitrary object and pretty-print
// it.
template <typename Write, typename WriteGeneric, typename T>
void DebugPrintValue(Write&& write, WriteGeneric&& write_generic,
                     const T& value) {
  // In order to prevent Hyrum's Law dependency on the precise output of
  // this operator, we randomly use extra whitespace in some cases.
  //
  // Note: for now, the jitter is fixed per struct type per process execution.
  // This is done by defining `kJitter` in `Jitter()`, which is *only*
  // templated on `T`, not on e.g. `Write` and `WriteGeneric`.
  const bool jitter = DebugPrintingJitter();
  write(jitter ? "{" : "{ ");

  if constexpr (!std::is_empty_v<T>) {
#if GTL_EXTEND_PARSE_FIELD_NAMES
    // We can access this because ReflectionExtension is a friend.
    const auto& field_name_info = ReflectionExtension<T>::GetFieldNames(value);
    size_t index = 0;
#endif
    std::apply(
        [&](const auto&... args) {
          const char* sep = "";
          const char* comma = jitter ? ", " : ",  ";
#if GTL_EXTEND_PARSE_FIELD_NAMES
          if (field_name_info.success) {
            ((write(std::exchange(sep, comma)),
              write(field_name_info.field_names[index++]), write(" = "),
              write_generic(args)),
             ...);
            return;
          }
#endif
          ((write(std::exchange(sep, comma)), write_generic(args)), ...);
        },
        ReflectionExtension<T>::Unpack(value));
  }
  write(jitter ? "}" : " }");
}

}  // namespace gtl::internal_extend

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_INTERNAL_DEBUG_PRINT_VALUE_H_
