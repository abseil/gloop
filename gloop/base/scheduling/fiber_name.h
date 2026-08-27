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

// Internal interfaces for propagating fiber names to the thread runtime
// for stack trace collection and debugging.
//
// These interfaces are strictly internal to the threading subsystem
// (//gloop/thread) and fiber runtime
// (//gloop/thread/fiber). Application code MUST NOT use these
// interfaces directly.

#ifndef THIRD_PARTY_GLOOP_BASE_SCHEDULING_FIBER_NAME_H_
#define THIRD_PARTY_GLOOP_BASE_SCHEDULING_FIBER_NAME_H_

#include <cstddef>

#include "absl/base/nullability.h"
#include "absl/strings/string_view.h"

namespace thread {
namespace internal {

// Strong type wrapper around an encoded strings::ArenaString pointer (or
// nullptr). Enforces type safety at call sites to prevent passing raw
// unencoded strings or string_views to InternalSetCurrentFiberName.
//
// Callers MUST only pass pointers returned from
// strings::ArenaString::data() or nullptr. Passing unencoded pointers will
// cause out-of-bounds reads or crashes during Decode().
class EncodedFiberName {
 public:
  constexpr EncodedFiberName() = default;
  constexpr explicit EncodedFiberName(std::nullptr_t) : rep_(nullptr) {}

  // Explicit factory for pre-encoded ArenaString pointers.
  [[nodiscard]] static constexpr EncodedFiberName FromEncoded(
      const char* arena_data) {
    EncodedFiberName name;
    name.rep_ = arena_data;
    return name;
  }

  [[nodiscard]] static constexpr EncodedFiberName None() {
    return EncodedFiberName(nullptr);
  }

  constexpr const char* encoded_rep() const { return rep_; }
  constexpr bool empty() const { return rep_ == nullptr; }

 private:
  const char* absl_nullable rep_ = nullptr;
};

}  // namespace internal

void InternalSetCurrentFiberName(internal::EncodedFiberName encoded_fiber_name);

// Returns the current fiber name for the calling thread, or an empty
// string_view if no fiber is active.
//
// ASYNC-SIGNAL-SAFE: May be safely called from signal handlers (e.g. stack
// trace extraction via FillStackTrace()).
absl::string_view InternalGetCurrentFiberName();

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_BASE_SCHEDULING_FIBER_NAME_H_
