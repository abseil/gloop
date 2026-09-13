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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_STRINGIFY_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_STRINGIFY_H_

#include <sstream>

#include "gloop/util/gtl/extend/extension.h"
#include "gloop/util/gtl/extend/internal/debug_print_value.h"
#include "gloop/util/gtl/extend/reflection_extension.h"
#include "gloop/util/gtl/generic_printer.h"

namespace gtl {

// StringifyExtension
//
// A gtl::Extend extension that enables AbslStringify() for the
// struct. Each struct field must be printable with gtl::GenericPrinter.
//
// Note that this extension is only to be used for debugging. The precise format
// is unspecified. It is guaranteed that each value will be streamed in the
// order they appear in the struct. ALL OTHER FORMATTING IS SUBJECT TO CHANGE.
// In particular, when possible, we attempt to provide field names for struct
// members, but this is not guaranteed. Field names are not accessible on all
// compilers, and may not be available for all types even within the same
// program.
//
// Example:
//
// struct Point : gtl::Extend<Point>::With<gtl::StringifyExtension> {
//   int x;
//   int y;
// };
//
// Point p1{.x = 3, .y = 4};
// // generates something like "Value: {x = 3, y = 4}" or maybe "Value: {3,  4}"
// std::string stringified = absl::StrFormat("Value: %v", p1);

template <typename T>
struct StringifyExtension : Extension<StringifyExtension, T> {
  using deps = void(ReflectionExtension<T>);
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const T& value) {
    internal_extend::DebugPrintValue(
        [&](const auto& v) { sink.Append(v); },
        [&, ss = std::stringstream()](const auto& v) mutable {
          ss.str("");
          ss << gtl::GenericPrint(v);
          sink.Append(ss.str());
        },
        value);
  }
};

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_STRINGIFY_H_
