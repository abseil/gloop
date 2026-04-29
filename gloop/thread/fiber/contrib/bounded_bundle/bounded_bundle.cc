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

#include "gloop/thread/fiber/contrib/bounded_bundle/bounded_bundle.h"

#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/functional/any_invocable.h"
#include "gloop/thread/fiber/select.h"

namespace thread {

void BoundedBundle::Add(absl::AnyInvocable<void() &&> fn) {
  sem_.Acquire(1);
  bundle_.Add([fn = std::move(fn), this]() mutable {
    auto cleanup = absl::MakeCleanup([this]() { sem_.Release(1); });
    std::move(fn)();
  });
}

void BoundedBundle::CancelAll() { bundle_.CancelAll(); }

bool BoundedBundle::Cancelled() const { return bundle_.Cancelled(); }

void BoundedBundle::JoinAll() { bundle_.JoinAll(); }

thread::Case BoundedBundle::OnJoinable() { return bundle_.OnJoinable(); }

}  // namespace thread
