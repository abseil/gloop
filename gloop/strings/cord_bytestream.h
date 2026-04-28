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

#ifndef THIRD_PARTY_GLOOP_STRINGS_CORD_BYTESTREAM_H_
#define THIRD_PARTY_GLOOP_STRINGS_CORD_BYTESTREAM_H_

// TODO: b/491805634 - This header currently contains no symbols.
// Remove the following pragma once the code is migrated to this file.
// IWYU pragma: always_keep

#include "absl/strings/cord.h"

// Please include this file if you want to use strings::CordByteSink or
// strings::CordReader. The implementations are moving to this module.

#endif  // THIRD_PARTY_GLOOP_STRINGS_CORD_BYTESTREAM_H_
