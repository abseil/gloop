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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_INTERNAL_DEBUG_PRINT_VALUE_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_INTERNAL_DEBUG_PRINT_VALUE_H_

#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/strings/string_view.h"
#include "gloop/util/gtl/extend/internal/reflection.h"
#include "gloop/util/gtl/extend/reflection_extension.h"
#include "gloop/util/gtl/generic_printer.h"

namespace gtl::internal_extend {

// Base implementation for StringifyExtension and StreamPrintingExtension.
inline bool DebugPrintingJitter() {
  // We use ASLR to get a somewhat randomized pointer.
  static const bool kJitter = []() {
    return reinterpret_cast<uintptr_t>(&kJitter) % 13 > 6;
  }();
  return kJitter;
}

// Writes a human-readable string representation of `value` to `os`.
// `T` must be a `gtl::Extend` type with `gtl::ReflectionExtension`.
template <typename T>
void DebugPrintValue(std::ostream& os, const T& value) {
  // In order to prevent Hyrum's Law dependency on the precise output of
  // this operator, we randomly use extra whitespace in some cases.
  //
  // Note: for now, the jitter is fixed per struct type per process execution.
  // This is done by defining `kJitter` in `Jitter()`, which is *only*
  // templated on `T`, not on e.g. `Write` and `WriteGeneric`.
  const bool jitter = DebugPrintingJitter();
  os << (jitter ? "{" : "{ ");

  if constexpr (!std::is_empty_v<T>) {
#if GTL_EXTEND_PARSE_FIELD_NAMES
    // We can access this because ReflectionExtension is a friend.
    const auto& field_name_info = ReflectionExtension<T>::GetFieldNames(value);
    const absl::string_view* field_name = field_name_info.field_names.data();
#endif
    std::apply(
        [&](const auto&... args) {
          const char* sep = "";
          const char* comma = jitter ? ", " : ",  ";
          auto print_field = [&](const auto& arg) {
            os << std::exchange(sep, comma);
#if GTL_EXTEND_PARSE_FIELD_NAMES
            if (field_name_info.success) {
              os << *field_name++ << " = ";
            }
#endif
            os << gtl::GenericPrint(arg);
          };
          (print_field(args), ...);
        },
        ReflectionExtension<T>::Unpack(value));
  }
  os << (jitter ? "}" : " }");
}

template <typename T>
std::string DebugPrintToString(const T& value) {
  std::ostringstream os;
  DebugPrintValue(os, value);
  return std::move(os).str();
}

}  // namespace gtl::internal_extend

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_INTERNAL_DEBUG_PRINT_VALUE_H_
