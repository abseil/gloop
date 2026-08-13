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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_GENERIC_PRINTER_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_GENERIC_PRINTER_H_

#include "absl/strings/internal/generic_printer.h"

// Helpers for printing objects in generic code.
//
// These functions help with streaming in generic code. It may be desirable, for
// example, to log values from generic functions; however, operator<< may not be
// overloaded for all types.
//
// The helpers in this library use, in order of precedence:
//
//  1. std::string and absl::string_view are quoted and escaped. (The specific
//     format is not guaranteed to be stable.)
//  2. A defined AbslStringify() method.
//  3. gtl::LogContainer, if the object is a STL container.
//  4. For std::tuple, std::pair, and std::optional, recursively calls
//     gtl::GenericPrint for each element.
//  5. Floating point values are printed with enough precision for round-trip.
//  6. char values are quoted, with non-printable values escaped, and the
//     char's numeric value is included.
//  7. A defined operator<< overload (which should be found by ADL).
//  8. A defined DebugString() const method.
//  9. A fallback value with basic information otherwise.
//
// Note that the fallback value means that if no formatting conversion is
// defined, you will not see a compile-time error. This also means that
// gtl::GenericPrint() can safely be used in generic template contexts, and can
// format any types needed (even though the output will vary).
//
// Example usage:
//
//   // All values after GenericPrint() are formatted:
//   LOG(INFO) << gtl::GenericPrint()
//             << int_var         // <- printed normally
//             << float_var       // <- sufficient precision for round-trip
//             << " unchanged literal text ";
//
//   // Just one value is formatted:
//   LOG(INFO) << gtl::GenericPrint(string("this is quoted and escaped\t\n"))
//             << gtl::GenericPrint("but not this, ");
//             << string("and not this.");
//
// To make a type loggable with GenericPrint, prefer defining operator<< as a
// friend function. For example:
//
//   class TypeToLog {
//    public:
//     string ToString() const;  // Many types already implement this.
//                               // Define out-of-line if it is complex.
//     friend std::ostream& operator<<(std::ostream& os, const TypeToLog& v) {
//       return os << v.ToString();  // OK to define in-line instead, if simple.
//     }
//   };
//
// (Defining operator<< as an inline friend free function allows it to be found
// by Argument-Dependent Lookup, or ADL, which is the mechanism typically used
// for operator overload resolution. An inline friend function is the tightest
// scope possible for overloading the left-hand side of an operator.)
//
// See also //gloop/util/tuple/streamable.h. The streamable() utility has
// substantially more dependencies, and does not use a default operator<<. The
// generic helpers below only depend on STL headers and container_logging.h, and
// do not require types to implement interfaces beyond an operator<< overload
// (which would also be used, e.g., for LOG(INFO)).

namespace gtl {

// Helper for logging values in generic code.
//
// Example usage:
//
//   template <typename T>
//   void GenericFunction() {
//     T v1, v2;
//     VLOG(1) << gtl::GenericPrint() << v1 << v2;  // GenericPrint everything
//     VLOG(1) << gtl::GenericPrint(v1) << v2;  // GenericPrint just v1
//   }
//
using absl::strings_internal::GenericPrint;

// Generic printer type: this class can be used, for example, as a template
// argument to allow users to provide alternative printing strategies.
//
// For example, to allow callers to provide a custom strategy:
//
//   template <typename T, typename PrinterT = gtl::GenericPrinter<T>>
//   void GenericFunction() {
//     T value;
//     VLOG(1) << PrinterT{value};
//   }
//
using absl::strings_internal::GenericPrinter;

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_GENERIC_PRINTER_H_
