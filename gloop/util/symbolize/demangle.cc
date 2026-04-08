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

#include "gloop/util/symbolize/demangle.h"

#if (defined(__ANDROID__) || defined(ANDROID)) && !defined(OS_ANDROID)
#define OS_ANDROID
#endif

// We support certain compilers only.  See demangle.h for details.
#if defined(OS_ANDROID) && (defined(__i386__) || defined(__x86_64__))
#define HAS_CXA_DEMANGLE 0
#elif (__GNUC__ >= 4 || (__GNUC__ >= 3 && __GNUC_MINOR__ >= 4))
#define HAS_CXA_DEMANGLE 1
#elif defined(__clang__) && !defined(_MSC_VER)
#define HAS_CXA_DEMANGLE 1
#else
#define HAS_CXA_DEMANGLE 0
#endif

#include <stdlib.h>
#include <string.h>

#if HAS_CXA_DEMANGLE
#include <cxxabi.h>

#include <string>
#endif

#include "absl/debugging/internal/demangle_rust.h"

namespace util {
static bool DemangleRustSymbol(const char* mangled, std::string* out) {
  char buf[2048];
  using absl::debugging_internal::DemangleRustSymbolEncoding;
  if (DemangleRustSymbolEncoding(mangled, buf, sizeof(buf))) {
    out->append(buf);
    return true;
  }
  return false;
}

// The API reference of abi::__cxa_demangle() can be found in
// libstdc++'s manual.
// https://gcc.gnu.org/onlinedocs/libstdc++/libstdc++-html-USERS-4.3/a01696.html
void DemangleToString(const char* mangled, std::string* out) {
  // Rust symbols start with "_R"
  // https://doc.rust-lang.org/rustc/symbol-mangling/v0.html#symbol-name
  if (mangled[0] == '_' && mangled[1] == 'R') {
    if (!DemangleRustSymbol(mangled, out)) {
      out->append(mangled);
    }
    return;
  }

  // Check for __alloc_token prefix (Clang alloc-token ABI).
  if (mangled[0] == '_' && mangled[1] == '_' &&
      strncmp(mangled + 2, "alloc_token_", 12) == 0) {
    const char* p = mangled + 14;
    const char* q = p;
    while (*q >= '0' && *q <= '9') {
      q++;
    }
    if (q > p && *q == '_') {
      mangled = q + 1;  // fast ABI: __alloc_token_<id>_<func>
    } else {
      mangled = p;  // default ABI: __alloc_token_<func>
    }
  }

  // ... while mangled C++ symbols are distinct and start with "_Z"
  // https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling
  size_t length = 0;
  int status = 0;
  char* demangled = nullptr;
#if HAS_CXA_DEMANGLE
  demangled = abi::__cxa_demangle(mangled, nullptr, &length, &status);
#endif
  if (status == 0 && demangled != nullptr &&
      length > 1) {                      // Demangling succeeded.
    out->append(demangled, length - 1);  // length counts terminating null.
    free(demangled);
  } else {
    out->append(mangled);
  }
}

std::string Demangle(const char* mangled) {
  std::string demangled;
  DemangleToString(mangled, &demangled);
  return demangled;
}

bool DemanglingIsSupported() { return HAS_CXA_DEMANGLE; }
}  // namespace util
