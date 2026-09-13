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

#ifndef THIRD_PARTY_GLOOP_PERFTOOLS_TRACING__TRACE_SOURCE_LOCATION_H_
#define THIRD_PARTY_GLOOP_PERFTOOLS_TRACING__TRACE_SOURCE_LOCATION_H_

#include <cstdint>
#include <ostream>

#include "absl/base/attributes.h"
#include "absl/strings/str_format.h"
#include "absl/types/source_location.h"

namespace perftools::tracing {

// `TraceSourceLocation` is a plug-in replacement for `std::source_location` as
// well as `absl::SourceLocation`, except that the `function_name` and `column`
// properties are implemented as empty values.
//
// `TraceSourceLocation` can be implicitly converted from `absl::SourceLocation`
// values. Please note that `std::source_location` has not yet been approved as
// per <link>
//
// `TraceSourceLocation` is intended for internal `perftools::tracing` use only,
// and to at most sit at the boundary layers of tracing API to capture source
// location information in the ' = current()` usage pattern. In other words:
// `TraceSourceLocation` should never be spelled in any non tracing source file.
//
// Motivation:
// -----------
// The main motivation for `TraceSourceLocation` is the ability to synthesize
// a value from a known `file_name` / `line` pair of values captured previously
// in the trace recording API. For purposes of tracing, we added `StringRef`
// parameters to core library functions that are instrumented for causality.
// For example, `thread::Select()` has a `StringRef name` last parameter to
// allow applications to name specific blocking waits. To avoid adding both
// `StringRef` and `SourceLocation` overloads to these APIs, we default the
// name value to the current source location, storing the file and line
// information internally in the `StringRef` instance, allowing us to convert
// these values back into a `TraceSourceLocation` in the recording API.
//
// A secondary motivation is that we can potentially optimize this class
// to pre-compute the tracing specific hash value at compile time for the
// source location while also persisting the file_name and line information.
// This provides both compute savings at runtime, as well allows us to retain
// the original file and line information deep into the tracing APIs. This is
// in contrast to EncodedSourceLocation which only persists the hash value.
class ABSL_ATTRIBUTE_TRIVIAL_ABI TraceSourceLocation {
 public:
  // Access token for internal / restricted APIs.
  class Access;

  // TraceSourceLocation is trivially copyable and assignable.
  TraceSourceLocation() = default;
  TraceSourceLocation(const TraceSourceLocation&) = default;
  TraceSourceLocation& operator=(const TraceSourceLocation&) = default;

  // Implicit constructor from absl::SourceLocation.
  constexpr TraceSourceLocation(absl::SourceLocation) noexcept;  // NOLINT

  // Constructs a new TraceLocationSource instance from the current call site.
  static constexpr TraceSourceLocation current(
      TraceSourceLocation location = absl::SourceLocation::current()) noexcept {
    return location;
  }

  // Creates a TraceSourceLocation from the provide file name and line number.
  // Requires `file_name` to be non-null and have an infinite life time.
  constexpr TraceSourceLocation(Access, const char* file_name,
                                std::uint_least32_t line) noexcept;

  // Returns the file name of this source location.
  constexpr const char* file_name() const noexcept { return file_name_; }

  // Returns the line number of this source location.
  constexpr std::uint_least32_t line() const noexcept { return line_; }

  // Not supported std::source_location properties
  constexpr std::uint_least32_t column() const noexcept { return 0; }
  constexpr const char* function_name() const noexcept { return ""; }

  // Formats `location` as "<file>:<line>" or an empty string if empty.
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const TraceSourceLocation& location) {
    if (*location.file_name() != '\0') {
      absl::Format(&sink, "%s:%d", location.file_name(), location.line());
    }
  }

  // Streams the source location as "<file>:<line>" if not empty.
  friend std::ostream& operator<<(std::ostream& os,
                                  const TraceSourceLocation& location) {
    absl::Format(&os, "%v", location);
    return os;
  }

 private:
  constexpr TraceSourceLocation(const char* file_name,
                                std::uint_least32_t line) noexcept;

  const char* file_name_ = "";
  std::uint_least32_t line_ = 0;
};

// Forward declare friends
template <bool>
class StringImpl;
namespace testing {
struct TestOnlyAccess;
};

// Define access + friends
class TraceSourceLocation::Access {
 private:
  // Note: explicit definition to prevent compiler eliding the private ctor
  explicit constexpr Access() noexcept = default;

  template <bool>
  friend class StringImpl;
  friend struct testing::TestOnlyAccess;
};

inline constexpr TraceSourceLocation::TraceSourceLocation(
    const char* file_name, std::uint_least32_t line) noexcept
    : file_name_(file_name), line_(line) {}

inline constexpr TraceSourceLocation::TraceSourceLocation(
    Access, const char* file_name, std::uint_least32_t line) noexcept
    : TraceSourceLocation(file_name, line) {}

inline constexpr TraceSourceLocation::TraceSourceLocation(
    absl::SourceLocation location) noexcept
    : TraceSourceLocation(location.file_name(), location.line()) {}

}  // namespace perftools::tracing

#endif  // THIRD_PARTY_GLOOP_PERFTOOLS_TRACING__TRACE_SOURCE_LOCATION_H_
