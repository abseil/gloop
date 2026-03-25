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

// Defines two functions Foo(int depth) and Bar(int depth) which alternately
// call each other until depth==1, and return the stack trace when this happens.
// Please note that the code for these functions is intentionally unoptimized
// for testing purposes.
#include "gloop/util/symbolize/foobar.h"

#include <cstdlib>
#include <string>

#include "absl/base/attributes.h"
#include "gloop/util/symbolize/symbolized_stacktrace.h"

ABSL_ATTRIBUTE_NOINLINE
ABSL_ATTRIBUTE_NO_TAIL_CALL
std::string Foo(int depth) {
  if (depth < 1) exit(-1);
  return depth == 1 ? util::CurrentStackTrace() : Bar(depth - 1);
}

ABSL_ATTRIBUTE_NOINLINE
ABSL_ATTRIBUTE_NO_TAIL_CALL
std::string Bar(int depth) {
  if (depth < 1) exit(-1);
  return depth == 1 ? util::CurrentStackTrace() : Foo(depth - 1);
}
