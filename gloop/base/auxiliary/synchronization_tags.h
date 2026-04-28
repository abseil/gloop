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

#ifndef THIRD_PARTY_GLOOP_BASE_AUXILIARY_SYNCHRONIZATION_TAGS_H_
#define THIRD_PARTY_GLOOP_BASE_AUXILIARY_SYNCHRONIZATION_TAGS_H_

namespace base {
// Define a set of tags to ensure consistency in where data is recorded.

// These tags indicate where the weight and offset are in the tags array
// for contention profiling, and indicate where the tags with Context
// information start.
enum class ContentionzIndexOffsets {
  kMicrosIndex,      // Index of contention time in microseconds.
  kCountIndex,       // Index of number of observed contentions.
  kHasContextIndex,  // Index of bool indicating whether Context present.
  kNumTags           // Number of tags needed to hold recorded data. Add new
                     // data before this constant.
};
}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_AUXILIARY_SYNCHRONIZATION_TAGS_H_
