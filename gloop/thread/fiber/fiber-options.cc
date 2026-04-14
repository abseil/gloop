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

#include "gloop/thread/fiber/fiber-options.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/hash/hash.h"
#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "gloop/base/arena.h"
#include "gloop/base/context.h"
#include "gloop/strings/arena-string.h"
#include "gloop/thread/fiber/internal/fiber-thread-options.h"
#include "gloop/thread/thread.h"

#ifdef FIBER_OPTIONS_COUNTING_MUTEX
#include "gloop/concurrent/percpu/counting_mutex.h"
#else
#include "absl/synchronization/mutex.h"
#endif

namespace thread {

namespace {

// An intern table for managing a set of strings for which each identical
// string appears in memory exactly once. Unlike `strings::InternTable`, this
// exposes a `strings::ArenaString` output which is only a single pointer,
// instead of string_view (pointer + length). This is helpful to minimize the
// size of the common FiberOptions struct.
//
// This class is thread safe. The returned ArenaString will remain valid for
// the lifetime of the InternTable.
class InternTable {
 public:
  explicit InternTable(size_t block_size) : arena_(block_size) {}

// TODO: b/288322795 - Workaround for CountingMutex build issues.
#ifdef FIBER_OPTIONS_COUNTING_MUTEX
  using MutexImpl = concurrent::CountingMutex;
  using MutexImplReaderLock = concurrent::CountingMutexReaderLock;
  using MutexImplLock = concurrent::CountingMutexLock;
#else
  using MutexImpl = absl::Mutex;
  using MutexImplReaderLock = absl::ReaderMutexLock;
  using MutexImplLock = absl::MutexLock;
#endif

  strings::ArenaString Intern(absl::string_view s) {
    {
      // Since the number of Fiber names should be bounded (due them existing
      // for the lifetime of the process), do a cheaper lookup first before
      // trying to insert.

      MutexImplReaderLock lock(mu_);
      if (auto iter = values_.find(s); iter != values_.end()) {
        return *iter;
      }
    }

    MutexImplLock lock(mu_);
    return *values_.lazy_emplace(s, [s, this](const auto& ctor) {
      ctor(strings::ArenaString(s, &arena_));
    });
  }

 private:
  struct StringHash {
    using is_transparent = void;
    size_t operator()(absl::string_view v) const {
      return absl::Hash<absl::string_view>{}(v);
    }
    size_t operator()(strings::ArenaString v) const {
      return absl::Hash<absl::string_view>{}(v.str());
    }
  };
  struct StringEq {
    using is_transparent = void;
    bool operator()(strings::ArenaString lhs, strings::ArenaString rhs) const {
      return lhs.str() == rhs.str();
    }
    bool operator()(strings::ArenaString lhs, absl::string_view rhs) const {
      return lhs.str() == rhs;
    }
  };
  mutable MutexImpl mu_;
  UnsafeArena arena_ ABSL_GUARDED_BY(mu_);
  absl::flat_hash_set<strings::ArenaString, StringHash, StringEq> values_
      ABSL_GUARDED_BY(mu_);
};

InternTable& fiber_names_table() {
  static auto* const fiber_names_table = new InternTable(4096);
  return *fiber_names_table;
}

}  // namespace

// Any  default tree context may need to be adjusted to accommodate
// <link>, so we allow the adjustment via a
// weak function. This is called in NewTree and Detach when the base::Context is
// consumed using `TreeOptions::consume_context()`, and the context is not
// explicitly set in the TreeOptions. The real implementation is in
// <path>; see there for details.
ABSL_ATTRIBUTE_WEAK void AdjustDefaultTreeContext(base::Context&, const void*) {
}

base::Context TreeOptions::copy_context(const void* fiber) const {
  base::Context context(context_.has_value() ? *context_ : base::Context());
  if (!context_.has_value()) {
    AdjustDefaultTreeContext(context, fiber);
  }
  return context;
}

base::Context TreeOptions::consume_context(const void* fiber) {
  base::Context context(context_.has_value() ? std::move(*context_)
                                             : base::Context());
  if (!context_.has_value()) {
    AdjustDefaultTreeContext(context, fiber);
  }
  context_ = std::nullopt;
  return context;
}

FiberOptions& FiberOptions::SetStackSize(size_t stack_size) {
  // Silently round up stack_size if it's less than our minimum, unless it's
  // zero which we treat specially as an indication that the default stack size
  // specified by the default_fiber_stack_size command line flag should be
  // used.
  if (stack_size != 0) {
    stack_size =
        std::max<size_t>(stack_size, 1u << internal::kMinStackSizeLog2);
  }
  CHECK_LE(stack_size, (1u << internal::kMaxStackSizeLog2))
      << "Stack size must be less than or equal to 2^"
      << internal::kMaxStackSizeLog2 << " bytes";
  stack_size_ = stack_size;
  return *this;
}

size_t FiberOptions::GetStackSize() const { return stack_size_; }

FiberOptions& FiberOptions::SetInternedName(absl::string_view name) {
  if (name.empty()) {
    name_ = nullptr;
    return *this;
  }

  DCHECK(IsValidThreadNamePrefix(name))
      << "Fiber name \"" << name << "\" contains a disallowed character.";

  // Stored as `const char*` (and not `strings::ArenaString`) because including
  // strings/arena-string.h in the header caused compilation bugs in dependent
  // projects. See the comments in CL 393147480 for more context.
  name_ = fiber_names_table().Intern(name).data();
  return *this;
}

absl::string_view FiberOptions::name() const {
  return strings::ArenaString::Decode(name_);
}

}  // namespace thread
