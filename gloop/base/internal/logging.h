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

#ifndef THIRD_PARTY_GLOOP_BASE_INTERNAL_LOGGING_H_
#define THIRD_PARTY_GLOOP_BASE_INTERNAL_LOGGING_H_

#include "absl/base/nullability.h"
extern "C" {
// The remote debug logging library will override this weak symbol and use it to
// do "early" initialization, which should be as lightweight as possible.
//
// If a non-null pointer is returned, it represents a "late" initialization
// function which will be called at the very end of `InitGoogle()`, after all
// module initializers have run.
void (*absl_nullable InitializeRemoteDebugLogging())();
}  // extern "C"

#endif  // THIRD_PARTY_GLOOP_BASE_INTERNAL_LOGGING_H_
