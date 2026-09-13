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

// This is the file that should be included by any file which declares
// direct-access command line flags.  For more information, see
//    <link>

#ifndef THIRD_PARTY_GLOOP_BASE_COMMANDLINEFLAGS_DECLARE_H_
#define THIRD_PARTY_GLOOP_BASE_COMMANDLINEFLAGS_DECLARE_H_

#include <string>  // IWYU pragma: keep

#include "absl/flags/declare.h"  // IWYU pragma: keep

#define GFLAGS_DLL_DECLARE_FLAG /* rewritten to be non-empty in windows dir */

#define DECLARE_VARIABLE(type, shorttype, name)                \
  /* We always want to import declared variables, dll or no */ \
  namespace fL##shorttype {                                    \
    extern GFLAGS_DLL_DECLARE_FLAG type FLAGS_##name;          \
  }                                                            \
  using fL##shorttype::FLAGS_##name

#define DECLARE_bool(name) DECLARE_VARIABLE(bool, B, name)

#define DECLARE_int32(name) DECLARE_VARIABLE(int32_t, I, name)

#define DECLARE_int64(name) DECLARE_VARIABLE(int64_t, I64, name)

#define DECLARE_uint64(name) DECLARE_VARIABLE(uint64_t, U64, name)

#define DECLARE_double(name) DECLARE_VARIABLE(double, D, name)

#define DECLARE_string(name)                                \
  namespace fLS {                                           \
  extern GFLAGS_DLL_DECLARE_FLAG std::string& FLAGS_##name; \
  }                                                         \
  using fLS::FLAGS_##name

#endif  // THIRD_PARTY_GLOOP_BASE_COMMANDLINEFLAGS_DECLARE_H_
