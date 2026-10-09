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

#include "gloop/util/freelist/freelist.h"

#include "absl/base/config.h"  // IWYU pragma: keep
#include "absl/flags/flag.h"

ABSL_RETIRED_FLAG(
    bool, expensive_freelist_debug_check, false,
    "If this is set, all calls to Delete() on a FreeList in debug "
    "mode will do a check that the delete is permitted, i.e. not a "
    "double-deletion or deletion of an element not allocated from "
    "the list. This check is potentially very expensive, but will "
    "track down the silent memory corruption that otherwise happens "
    "when such an event occurs.");

// Under memory sanitizers - ASAN and MSAN - free lists are disabled by default.
// The intent is to capture "use-after-free" bugs, while still retaining the
// possibility to use free-lists under the sanitizers (via manual flag-flip).
#if defined(ABSL_HAVE_ADDRESS_SANITIZER) || defined(ABSL_HAVE_MEMORY_SANITIZER)
constexpr bool kUseFreeListsDefault = false;
#else
constexpr bool kUseFreeListsDefault = true;
#endif

ABSL_FLAG(bool, use_freelists, kUseFreeListsDefault,
          "If this is unset, all freelist functionality will be disabled. "
          "This is very useful for debugging memory leaks, as freelists "
          "will no longer show up as large allocations.");
