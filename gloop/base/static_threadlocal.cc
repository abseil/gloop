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

#include "gloop/base/static_threadlocal.h"

#include <atomic>

#include "absl/base/attributes.h"
#include "absl/debugging/leak_check.h"
#include "gloop/base/per_thread.h"

namespace base {
namespace internal {

#if BASE_INTERNAL_PER_THREAD_TLS

// The global per_thread_key for list of non-POD types to destruct in TLS mode.
ABSL_CONST_INIT PerThread::Key g_per_thread_key{PerThread::kInvalid};

// Register a StaticThreadLocalList variable.
//
// This is the common code required to register one of these variables. It
// connects it to the linked list for this thread, sets up the destructor, and
// tells the leak checker about the pointer.
void StaticThreadLocalRegister(StaticThreadLocalList* variable,
                               void (*destructor)(void*)) {
  PerThread::Allocate(&g_per_thread_key, &StaticThreadLocalDestructor);
  void** pt = PerThread::Data(base::internal::g_per_thread_key);
  variable->next = static_cast<base::internal::StaticThreadLocalList*>(*pt);
  variable->destructor = destructor;
  *pt = variable;
  absl::IgnoreLeak(variable->value.load(std::memory_order_relaxed));
}

// The destructor for g_per_thread_key.  Called once per thread at exit (unless
// the thread touches static_threadlocal variables during exit).  This
// destructor calls the type-specific destructor-function (Destructor()) of
// each non-POD static_threadlocal variable in TLS mode.
void StaticThreadLocalDestructor(void* const p) {
  // Loop until we've run out of StaticThreadLocalList structs to clean up.
  StaticThreadLocalList* list = static_cast<StaticThreadLocalList*>(p);
  do {
    void* const value = list->value.load(std::memory_order_relaxed);
    StaticThreadLocalList* const next = list->next;

    // Unpublish the value and then destroy it.
    //
    // We need to do these two operations in this order so that if we're
    // interrupted by a signal handler somewhere in the process that handler
    // will see see a null pointer rather than a pointer to a
    // partially-destroyed object.
    //
    // What we want here is for the compiler to emit the "store null pointer"
    // instruction before the call instruction. There's nothing in the standard
    // that actually ensures it will do this, nor is there any fence that we can
    // use to cause this, because the call to the destructor isn't e.g.
    // consuming an atomic write from the signal handler. This is a weird
    // situation: unlike with initialization there is no analog to it when
    // synchronizing with other threads, since it's never safe to destroy an
    // object after unpublishing its pointer if another thread might
    // concurrently access it. We can do so here only because a signal handler
    // entirely pauses us if it interrupts us; it doesn't run concurrently.
    //
    // As of 2024-05, however, this seems to work in practice. The compiler
    // would have a hard time proving that it's allowed to re-order these, It
    // would need to prove that the destructor doesn't also modify list->value,
    // but it presumably can't see through the destructor function pointer.
    list->value.store(nullptr, std::memory_order_relaxed);
    (*list->destructor)(value);

    // Move on to the next item in the list.
    list = next;
  } while (list != nullptr);
}

#endif  // BASE_INTERNAL_PER_THREAD_TLS

}  // namespace internal
}  // namespace base
