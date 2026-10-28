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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_INTERNAL_REFLECTION_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_INTERNAL_REFLECTION_H_

#include <array>
#include <cstddef>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

#if defined(GTL_EXTEND_INTERNAL_TURN_OFF_FIELD_NAMES_FOR_TEST)
#define GTL_EXTEND_PARSE_FIELD_NAMES 0
#elif defined(TARGET_OS_OSX) || defined(__ANDROID__)
// TODO: Implement field names for these.
#define GTL_EXTEND_PARSE_FIELD_NAMES 0
#elif ABSL_HAVE_BUILTIN(__builtin_dump_struct)
#define GTL_EXTEND_PARSE_FIELD_NAMES 1
#else
#define GTL_EXTEND_PARSE_FIELD_NAMES 0
#endif

namespace gtl::internal_extend {

struct ParsingState {
  // The index of the field currently being parsed.
  int index = 0;
  // The number of unmatched '{' characters encountered so far while parsing.
  int brace_count = 0;
};

// Called by `__builtin_dump_struct` for each line of the struct representation.
// Rather than printing the struct representation, we parse `fmt` to extract the
// lines containing struct field names and store them in `T::kFieldNames`.
// Note: `__builtin_dump_struct` provides no guarantees whatsoever on its
// output, so our parsing is entirely best-effort.
//
// `PrintfHijack` is called repeatedly with each `fmt` line and corresponding
// arguments via `__builtin_dump_struct`. While there is no guarantee about
// the `fmt` strings provided, it is typical that lines come in 5 flavors:
// 1. A string containing only "%s", accepting the name of the struct itself.
// 2. A string containing only "%s%s", accepting the name of a base class.
// 3. A string containing only whitespace and an open brace "{" indicating that
//    we are entering a new struct.
// 4. A string with "=" where the value passed into the format specifier which
//    immediately precedes "=" is the name of a struct field.
// 5. A string ending with a "}" indicating the conclusion of a struct.
//
// For example, for a struct
// ```
// struct Bacon {
//   int repeatability;
//   double flavor;
//   double fattiness;
//   struct Metadata {
//     bool is_pork;
//     bool is_expired;
//   } metadata;
// };
// ```
// we would expect the following values for `fmt` to be passed to
// `PrintfHijack`, along with the following trailing arguments:
//     `fmt`           |    trailing arguments
// --------------------|----------------------------------------------
//   "%s"              | {"Bacon"}
//   " {\n"            | {}
//   "%s%s %s = %d\n"  | {"  ", "int", "repeatability", ???}
//   "%s%s %s = %f\n"  | {"  ", "double", "flavor", ???}
//   "%s%s %s = %f\n"  | {"  ", "double", "fattiness", ???}
//   "%s%s %s ="       | {"  ", "Metadata", "metadata"}
//   " {\n"            | {}
//   "%s%s %s = %d\n"  | {"  ", "_Bool", "is_pork", ???}
//   "%s%s %s = %d\n"  | {"  ", "_Bool", "is_expired", ???}
//   "%s}\n"           | {"  "}
//   "}\n"             | {}
//
// `PrintfHijack` inspects each format string and argument set, extracting field
// names for the given struct and writes them to `fields`. If an unexpected
// format string is encountered, `state.index` will be set to -1 indicating a
// failure. Parsing succeeds if after every call to `PrintfHijack`,
// `state.index == fields.size()`.
//
// Defining `PrintfHijack` as `ABSL_ATTRIBUTE_ALWAYS_INLINE` overloads (rather
// than an out-of-line C variadic function) allows the compiler to constant-fold
// the literal `fmt` strings emitted by `__builtin_dump_struct` and
// dead-code-eliminate unused field-value reads and nested struct/base-class
// traversal in optimized builds, without emitting weak symbols in `-O0` builds.
ABSL_ATTRIBUTE_ALWAYS_INLINE inline int PrintfHijack(
    ParsingState& state, absl::Span<absl::string_view> /*fields*/,
    absl::string_view fmt, const char* /*arg1*/ = nullptr,
    const char* /*arg2*/ = nullptr) {
  if (state.index < 0) return 0;

  if (fmt.ends_with("{\n")) {
    ++state.brace_count;
  } else if (fmt.ends_with('}') || fmt.ends_with("}\n")) {
    --state.brace_count;
  } else if (state.brace_count == 1 && fmt != "%s%s") {
    state.index = -1;
  }
  return 0;
}

template <typename... Unused>
ABSL_ATTRIBUTE_ALWAYS_INLINE inline int PrintfHijack(
    ParsingState& state, absl::Span<absl::string_view> fields,
    absl::string_view fmt, const char* /*indent*/, const char* /*type*/,
    const char* field_name, const Unused&...) {
  if (state.index >= 0 && state.brace_count == 1) {
    if (fmt.starts_with("%s%s %s =") &&
        static_cast<size_t>(state.index) < fields.size()) {
      fields[static_cast<size_t>(state.index)] = field_name;
      ++state.index;
    } else {
      state.index = -1;
    }
  }
  return 0;
}

// Forward declaration of test helper.
template <typename T>
struct ReflectionTestingExtension;

// Not nested in ReflectionExtension to reduce template instantiations.
template <size_t N>
struct FieldNameInfo {
  static constexpr size_t kFieldCount = N;

#if GTL_EXTEND_PARSE_FIELD_NAMES
  std::array<absl::string_view, kFieldCount> field_names;
#endif
  // Indicates whether parsing was successful.
  bool success;
};

}  // namespace gtl::internal_extend

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_INTERNAL_REFLECTION_H_
