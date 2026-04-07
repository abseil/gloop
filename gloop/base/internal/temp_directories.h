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

#ifndef THIRD_PARTY_GLOOP_BASE_INTERNAL_TEMP_DIRECTORIES_H_
#define THIRD_PARTY_GLOOP_BASE_INTERNAL_TEMP_DIRECTORIES_H_

#include <string>
#include <vector>

namespace base {
namespace internal {

// Returns a list of plausible temporary directories ordered from most preferred
// to least. They have not been checked for existence or accessibility.
std::vector<std::string> TempDirectories();

// Returns a set of existing temporary directories, which will be a subset of
// the directories returned by `TempDirectories`.
std::vector<std::string> ExistingTempDirectories();

}  // namespace internal
}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_INTERNAL_TEMP_DIRECTORIES_H_
