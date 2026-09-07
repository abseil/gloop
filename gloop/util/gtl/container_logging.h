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

// Utilities for container logging.
//

//
// The typical use looks like this:
//
//   LOG(INFO) << gtl::LogContainer(container);
//
// By default, LogContainer() uses the LogShortUpTo100 policy: comma-space
// separation, no newlines, and with limit of 100 items.
//
// Policies can be specified:
//
//   LOG(INFO) << gtl::LogContainer(container, gtl::LogMultiline());
//
// The above example will print the container using newlines between
// elements, enclosed in [] braces.
//
// See below for further details on policies.

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_CONTAINER_LOGGING_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_CONTAINER_LOGGING_H_

#include "absl/log/internal/container.h"

namespace gtl {

// Several policy classes below determine how LogRangeToStream will
// format a range of items.  A Policy class should have these methods:
//
// Called to print an individual container element.
//   void Log(ostream &out, const ElementT &element) const;
//
// Called before printing the set of elements:
//   void LogOpening(ostream &out) const;
//
// Called after printing the set of elements:
//   void LogClosing(ostream &out) const;
//
// Called before printing the first element:
//   void LogFirstSeparator(ostream &out) const;
//
// Called before printing the remaining elements:
//   void LogSeparator(ostream &out) const;
//
// Returns the maximum number of elements to print:
//   int64 MaxElements() const;
//
// Called to print an indication that MaximumElements() was reached:
//   void LogEllipsis(ostream &out) const;

// LogShort uses [] braces and separates items with comma-spaces.  For
// example "[1, 2, 3]".
using LogShort = absl::log_internal::LogShort;

// LogShortUpToN(max_elements) formats the same as LogShort but prints no more
// than the max_elements elements.
using LogShortUpToN = absl::log_internal::LogShortUpToN;

// LogShortUpTo100 formats the same as LogShort but prints no more
// than 100 elements.
using LogShortUpTo100 = absl::log_internal::LogShortUpTo100;

// LogMultiline uses [] braces and separates items with
// newlines.  For example "[
// 1
// 2
// 3
// ]".
using LogMultiline = absl::log_internal::LogMultiline;

// LogMultilineUpToN(max_elements) formats the same as LogMultiline but
// prints no more than max_elements elements.
using LogMultilineUpToN = absl::log_internal::LogMultilineUpToN;

// LogMultilineUpTo100 formats the same as LogMultiline but
// prints no more than 100 elements.
using LogMultilineUpTo100 = absl::log_internal::LogMultilineUpTo100;

// The legacy behavior of LogSequence() does not use braces and
// separates items with spaces.  For example "1 2 3".
using LogLegacyUpTo100 = absl::log_internal::LogLegacyUpTo100;
using LogLegacy = absl::log_internal::LogLegacy;

// The default policy for new code.
using LogDefault = absl::log_internal::LogDefault;

// LogRangeToStream should be used to define operator<< for
// STL and STL-like containers.  For example, see stl_logging.h.
using absl::log_internal::LogRangeToStream;

// Log a range using "policy".  For example:
//
//   LOG(INFO) << gtl::LogRange(start_pos, end_pos, gtl::LogMultiline());
//
// The above example will print the range using newlines between
// elements, enclosed in [] braces.
//
// By default, Range() uses the LogShortUpTo100 policy: comma-space
// separation, no newlines, and with limit of 100 items.
using absl::log_internal::LogRange;

// Log a container using "policy".  For example:
//
//   LOG(INFO) << gtl::LogContainer(container, gtl::LogMultiline());
//
// The above example will print the container using newlines between
// elements, enclosed in [] braces.
//
// By default, Container() uses the LogShortUpTo100 policy: comma-space
// separation, no newlines, and with limit of 100 items.
using absl::log_internal::LogContainer;

// Log a (possibly scoped) enum.  For example:
//
//   enum class Color { kRed, kGreen, kBlue };
//   LOG(INFO) << gtl::LogEnum(kRed);
using absl::log_internal::LogEnum;

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_CONTAINER_LOGGING_H_
