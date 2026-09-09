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

#ifndef THIRD_PARTY_GLOOP_BASE_GLOBAL_SYMBOL_COMPLIANCE_H_
#define THIRD_PARTY_GLOOP_BASE_GLOBAL_SYMBOL_COMPLIANCE_H_

namespace base {

// Returns true if the current binary contains possible duplicate definitions of
// global symbols of first party code (base / absl, etc). This allows library
// code to disable specific features that requires process-wide unique global
// symbols, or leaks such symbols into other APIs / binaries or kernel
// functions. The most common example is RSEQ based percpu logic which may
// result in failures and/or undefined behavior in the presence of duplicate
// definitions of global and thread local data.
// See <link> for more details and background.
//
// IMPORTANT: this logic is only provided to, where possible, reduce the risk of
// code exhibiting ODR violations from using non-supported dynamic loading of
// shared objects. Such code remains at all time unsupported, and owners should
// make their applications ODR / dlopen compliant. This band-aid is targeted to
// be removed somewhere around the end of Q2 2022
bool HasDuplicateGlobalSymbols();

}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_GLOBAL_SYMBOL_COMPLIANCE_H_
