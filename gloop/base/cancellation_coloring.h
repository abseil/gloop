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

#ifndef THIRD_PARTY_GLOOP_BASE_CANCELLATION_COLORING_H_
#define THIRD_PARTY_GLOOP_BASE_CANCELLATION_COLORING_H_

#include <sys/types.h>

#include <ostream>

#include "absl/base/attributes.h"

namespace base::internal {

// This library contains APIs for annotating a thread's active "cancellation
// color", allowing for runtime checks for coloring violations. For example, it
// allows the program to crash if a coroutine body calls an API that respects
// only fiber cancellation. See <link> for details.

// The cancellation color of a function, following the definitions in
// <link>. This is used with the WithCancellationColor
// class below to annotate the current cancellation disposition of a thread at
// any given time.
enum class CancellationColor {
  // The active cancellation color has not been annotated.
  //
  // This is the default when no API that annotates a cancellation color has
  // been called, including when a thread has just been started.
  kUnknown,

  // The thread is running a function that has no reason or ability to care
  // about cancellation because it's not supposed to wait for external things:
  // it is "pure computation".
  kUncolored,

  // The thread is running a coroutine body, which respects coroutine
  // cancellation, under an active scheduling context. See <link>
  // and <link>.
  kCoroutine,

  // The thread is polling a Rust future, where cancellation is expressed by
  // stopping polling and destroying the future, under an active scheduling
  // context.
  kRustFuture,

  kAsyncTask,

  // The thread is running code that respects thread::Fiber cancellation. A
  // scheduling context must not be active.
  //
  // Note that unlike kCoroutine, this enum member is not exhaustive: it's
  // possible also for such functions to be running under a kUnknown color. This
  // is because fibers are not scoped; there is no choke point for calling such
  // code.
  //
  // For example a new thread can be created without the thread::Fiber
  // constructor, and then call into a fiber cancellation-respecting function.
  // This is in contrast to "closed" ecosystems like coroutines, where the only
  // legal way to enter is through a bridge that annotates the thread color.
  kFibers,

  // A fake color that is distinct from the rest, reserved for unit tests.
  kFake,
};

// Support printing color names, for example in CHECK_EQ statements.
std::ostream& operator<<(std::ostream& os, CancellationColor color);

// We make cancellation coloring checks only in debug builds.
#ifndef NDEBUG

// Return the current active cancellation color for the calling thread.
CancellationColor GetActiveCancellationColor();

// An RAII object that sets the result of GetActiveCancellationColor for the
// thread on which it is created, restoring the previous value when done.
//
// This is a subtle API that should only be used within official bridge
// functions in the API ecosystems enumerated above. It is not for general
// application use.
class WithCancellationColor final {
 public:
  explicit WithCancellationColor(CancellationColor color);

  // REQUIRES: destroyed on the same thread as the constructor ran on.
  ~WithCancellationColor();

 private:
  const CancellationColor new_color_;
  const CancellationColor prev_color_;
  const pid_t creating_thread_;
};

#else  // NDEBUG

inline CancellationColor GetActiveCancellationColor() {
  return CancellationColor::kUnknown;
}

class WithCancellationColor final {
 public:
  explicit WithCancellationColor(CancellationColor) {}
};

#endif  // NDEBUG

}  // namespace base::internal

#endif  // THIRD_PARTY_GLOOP_BASE_CANCELLATION_COLORING_H_
