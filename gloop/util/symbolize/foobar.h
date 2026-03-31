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

#ifndef THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_FOOBAR_H_
#define THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_FOOBAR_H_

#include <string>

// The following functions alternately and recursively call each other as many
// times as specified. The final call retrieves the stack trace and returns it
// as a string.
std::string Foo(int depth);
std::string Bar(int depth);

#endif  // THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_FOOBAR_H_
