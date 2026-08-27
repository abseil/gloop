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

#include "gloop/base/global_symbol_compliance.h"

#include "absl/base/attributes.h"

namespace base {

#if ABSL_HAVE_ATTRIBUTE_WEAK

#if !defined(__APPLE__)
// DynamicallyLinksFirstPartyCodeInternal() is the actual weak symbol used by
// the DynamicallyLinksFirstPartyCode() method. It may be undefined.
ABSL_ATTRIBUTE_WEAK bool HasDuplicateGlobalSymbolsInternal();
#else
// Darwin (macOS and iOS) require a weak definition
ABSL_ATTRIBUTE_WEAK bool HasDuplicateGlobalSymbolsInternal() { return false; }
#endif  // __APPLE__

bool HasDuplicateGlobalSymbols() {
  return HasDuplicateGlobalSymbolsInternal != nullptr &&
         HasDuplicateGlobalSymbolsInternal();
}

#else  // ABSL_HAVE_ATTRIBUTE_WEAK

// We assume compilers not supporting weak symbols to also have poor support for
// features requiring unique global symbols. RSEQ (not supported on windows
// platforms) and Cordz (TLS issues under windows builds with DLLs) are examples
// of features that are already disabled or unavailable under MSVC and lexan.
bool HasDuplicateGlobalSymbols() { return true; }

#endif  // ABSL_HAVE_ATTRIBUTE_WEAK

}  // namespace base
