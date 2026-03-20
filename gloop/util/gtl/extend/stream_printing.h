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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_STREAM_PRINTING_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_STREAM_PRINTING_H_

#include <iostream>
#include <ostream>

#include "gloop/util/gtl/extend/extension.h"
#include "gloop/util/gtl/extend/internal/debug_print_value.h"
#include "gloop/util/gtl/extend/reflection_extension.h"
#include "gloop/util/gtl/generic_printer.h"

namespace gtl {

// StreamPrintingExtension
//
// A gtl::Extend extension that enables operator<< for the
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
// struct Point : gtl::Extend<Point>::With<gtl::StreamPrintingExtension> {
//   int x;
//   int y;
// };
//
// Point p1{.x = 3, .y = 4};
// LOG(INFO) << p1;  // logs something like "{x = 3, y = 4}" or maybe "{3,  4}"

template <typename T>
struct StreamPrintingExtension : Extension<StreamPrintingExtension, T> {
 public:
  using deps = void(ReflectionExtension<T>);

  friend std::ostream& operator<<(std::ostream& os, const T& value) {
    internal_extend::DebugPrintValue(
        [&](const auto& v) { os << v; },
        [&](const auto& v) { os << gtl::GenericPrint(v); }, value);
    return os;
  }
};

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_STREAM_PRINTING_H_
