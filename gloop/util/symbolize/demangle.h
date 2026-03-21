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

//
// Functions for demangling symbol names.  This works by calling a
// demangler API defined by C++ ABI if available.
//
// Reference:
//
//  http://www.codesourcery.com/cxx-abi/
//  http://www.codesourcery.com/cxx-abi/abi.html#demangler

#ifndef THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_DEMANGLE_H__
#define THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_DEMANGLE_H__

#include <string>

namespace util {

// Demangle a mangled symbol name and return the demangled name.
// If 'mangled' isn't mangled in the first place, this function
// simply returns 'mangled' as is.
//
// For types known at compile time, consider using util::DemangledTypeName()
// instead to avoid the run time overhead.
//
// This function is used for demangling mangled symbol names such as
// '_Z3bazifdPv'.  It uses abi::__cxa_demangle() if your compiler has
// the API.  Otherwise, this function simply returns 'mangled' as is.
//
// Currently, we support only GCC 3.4.x or later for the following
// reasons.
//
// - GCC 2.95.3 doesn't have cxxabi.h
// - GCC 3.3.5 and ICC 9.0 have a bug.  Their abi::__cxa_demangle()
//   returns junk values for non-mangled symbol names (ex. function
//   names in C linkage).  For example,
//     abi::__cxa_demangle("main", 0,  0, &status)
//   returns "unsigned long" and the status code is 0 (successful).
//
// Also, Android x86 is not supported because STLs don't define __cxa_demangle.
//
std::string Demangle(const char* mangled);

// CLIF-friendly version
inline std::string Demangle(const std::string& s) {
  return Demangle(s.c_str());
}

// Same function as above but appends result to 'out'.
void DemangleToString(const char* mangled, std::string* out);

// Return true if demangling is supported.
bool DemanglingIsSupported();

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_DEMANGLE_H__
