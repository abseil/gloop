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

#include <string>
#include <type_traits>

#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/types/source_location.h"
#include "gloop/util/status/status.h"

// This file is separate from status.cc, because it depends on
// NonMessageSetPayload::message_set_extension, which turns out to be expensive
// in terms of code size for some builds (see b/175215333). Keeping this code
// separate allows the linker to include the more commonly used functionality
// from status.cc without necessarily pulling in the code here.

#include "gloop/util/status/error_space.h"
#include "gloop/util/status/status_internal.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/bridge/message_set.pb.h"
