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

#include "gloop/util/status/error_space.h"

#include <atomic>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "gloop/util/status/status_internal.h"
#include "google/protobuf/message.h"

namespace util {
namespace {

// Global registry of `ErrorSpace` instances, used by `ErrorSpace::Find`.
// An `ErrorSpace` can be registered eagerly or lazily using the two
// `ErrorSpace::Register` functions.
//  - Eagerly: This is the legacy mechanism.
//  - Lazily: This is the mechanism used for link time registration.  Ideally,
//    it would do something less complicated, but some of the error spaces are
//    backed by protobuf reflection, so we need to defer calls to `SpaceName()`
//    until after protobuf has stood up most of its internals (somewhere before
//    `InitGoogle()` but after link time).
//
// To accomplish this weird set of constraints we have the following invariants:
//  - `space_lock` is held for all complex operations
//  - `static_spaces` is initialized once.  It is immutable after construction.
//    `static_spaces != nullptr` is the indicator that we have switched to
//    using `dynamic_spaces` for all new registrations.
//  - `pending_static_spaces` is used until `static_spaces` is initialized.
//    Thereafter all registration goes into `dynamic_spaces`.
//  - `dynamic_spaces` is always guarded by `space_lock`; however,
//    `HasAnyDynamicSpaces` can be used to avoid the mutex lock in the fast path
//    case where there are *NO* dynamic registration calls.
//  - The first fall to `ErrorSpace::Find` triggers the shift from static to
//    dynamic mode, including building the `static_spaces` table.
using ErrorSpaceTable = absl::flat_hash_map<std::string, const ErrorSpace*>;
using ErrorSpaceFactoryList = std::vector<const ErrorSpace* (*)()>;

ABSL_CONST_INIT ABSL_CACHELINE_ALIGNED std::atomic<const ErrorSpaceTable*>
    static_spaces = nullptr;
ABSL_CONST_INIT ABSL_CACHELINE_ALIGNED absl::Mutex space_lock(absl::kConstInit);
ABSL_CONST_INIT ErrorSpaceFactoryList* pending_static_spaces
    ABSL_GUARDED_BY(space_lock) = nullptr;
ABSL_CONST_INIT std::atomic<ErrorSpaceTable*> dynamic_spaces
    ABSL_GUARDED_BY(space_lock) ABSL_PT_GUARDED_BY(space_lock) = nullptr;

void ProcessStaticRegistrations() {
  absl::MutexLock l(space_lock);
  if (static_spaces.load(std::memory_order_relaxed) != nullptr) {
    // We lost a race to construct this, just use the computed version.
    return;
  }
  auto* s = new ErrorSpaceTable();
  s->reserve(pending_static_spaces->size() - 1);
  for (auto* registration : *pending_static_spaces) {
    const ErrorSpace* space = registration();
    auto name = space->SpaceName();
    if (name != status_internal::kGenericErrorSpaceName) {
      (*s)[name] = space;
    }
  }
  static_spaces.store(s, std::memory_order_release);
  delete pending_static_spaces;
  pending_static_spaces = nullptr;
}

void RegisterStaticSpace(const ErrorSpace* (*factory)())
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(space_lock) {
  if (pending_static_spaces == nullptr) {
    pending_static_spaces = new ErrorSpaceFactoryList();
  }
  pending_static_spaces->push_back(factory);
}

void RegisterDynamicSpace(absl::string_view name, const ErrorSpace* space)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(space_lock) {
  if (name == status_internal::kGenericErrorSpaceName) return;
  auto* s = dynamic_spaces.load(std::memory_order_acquire);
  if (s == nullptr) {
    s = new ErrorSpaceTable;
    dynamic_spaces.store(s, std::memory_order_release);
  }
  (*s)[name] = space;
}

bool HasAnyDynamicSpaces() ABSL_NO_THREAD_SAFETY_ANALYSIS {
  // We can look at null/non-null for the pointer without the mutex, but
  // we require the mutex to actually look at the *contents* of the map.
  return dynamic_spaces.load(std::memory_order_acquire) != nullptr;
}

}  // namespace

// See the comments above for details on registration.
bool ErrorSpace::Register(const ErrorSpace* (*factory)()) {
  absl::MutexLock l(space_lock);
  auto* s = static_spaces.load(std::memory_order_acquire);
  if (s == nullptr) {
    RegisterStaticSpace(factory);
  } else {
    const ErrorSpace* space = factory();
    RegisterDynamicSpace(space->SpaceName(), space);
  }
  return true;
}

void ErrorSpace::Register(absl::string_view name, const ErrorSpace* space) {
  absl::MutexLock l(space_lock);
  RegisterDynamicSpace(name, space);
}

ErrorSpace* ErrorSpace::Find(absl::string_view name) {
  if (name == status_internal::kGenericErrorSpaceName) {
    return const_cast<GenericErrorSpace*>(GenericErrorSpace::Get());
  }

  auto* s = static_spaces.load(std::memory_order_acquire);
  if (ABSL_PREDICT_FALSE(s == nullptr)) {
    ProcessStaticRegistrations();
    s = static_spaces.load(std::memory_order_acquire);
  }
  if (auto it = s->find(name); it != s->end()) {
    return const_cast<ErrorSpace*>(it->second);
  }

  if (!HasAnyDynamicSpaces()) return nullptr;
  absl::ReaderMutexLock l(space_lock);
  s = dynamic_spaces.load(std::memory_order_acquire);
  if (auto it = s->find(name); it != s->end()) {
    return const_cast<ErrorSpace*>(it->second);
  }
  return nullptr;
}

absl::string_view ErrorSpace::SpaceName() const {
  return space_name_func_(this);
}

// Provide default implementations of abstract methods in case
// somehow somebody ends up invoking one of these methods during
// the subclass construction/destruction phase.
std::string ErrorSpace::String(int code) const {
  return code_to_string_func_(this, code);
}

absl::StatusCode ErrorSpace::CanonicalCode(int code) const {
  return canonical_code_func_(this, code);
}

}  // namespace util
