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

// A BoundedBundle is a wrapper of thread::Bundle
// which limits the number of fibers that will be alive at any
// given time. This is useful for flow control: if you have N tasks to execute
// in parallel, and N is unbounded or known to be much larger than the number of
// CPUs available, you can use a BoundedBundle to limit the number of fibers
// that will be alive at any given time.

// All operations on the bundle are run in fibers that are
// descendants of the fiber that created the bundle and will therefore
// observe cancellation.
//
//
// Example:
//     thread::BoundedBundle bundle(42);
//     for (const auto& element : some_vec) {
//       bundle.Add([&element]() {
//               -- do something with element
//
//               -- some_vec.size() could be much more than 42, but at most 42
//               -- fibers will be alive at any given time
//       });
//     }
//     bundle.JoinAll();
//
// BoundedBundle limits the number of work items currently added to
// an internal thread::Bundle. The caller of Add() blocks for previous work
// items to finish to avoid exceeding the configured limit.

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_CONTRIB_BOUNDED_BUNDLE_BOUNDED_BUNDLE_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_CONTRIB_BOUNDED_BUNDLE_BOUNDED_BUNDLE_H_

#include <cstdint>

#include "absl/functional/any_invocable.h"
#include "gloop/thread/fiber/bundle.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/thread/fiber/semaphore/fifo_semaphore.h"

namespace thread {

class BoundedBundle {
 public:
  explicit BoundedBundle(uintptr_t max_live_fibers,
                         const FiberOptions& options = thread::FiberOptions{})
      : sem_(max_live_fibers), bundle_(options) {}

  BoundedBundle(const BoundedBundle&) = delete;
  BoundedBundle& operator=(const BoundedBundle&) = delete;
  BoundedBundle(BoundedBundle&&) = delete;
  BoundedBundle& operator=(BoundedBundle&&) = delete;

  // Add an operation to the BoundedBundle.
  // The BoundedBundle must not have been joined before calling Add().
  // Blocks until the BoundedBundle has capacity to execute this operation.
  void Add(absl::AnyInvocable<void() &&> fn);

  // Cancel all fibers, and any descendants, belonging to this bundle.  Fibers
  // added after cancellation will be created in a cancelled state.
  void CancelAll();

  // Returns whether the bundle has been cancelled (as per CancelAll()).
  bool Cancelled() const;

  // Join all fibers, and any descendants, belonging to this bundle.  Returns
  // immediately if the bundle is empty (i.e. no children currently exist).
  // It is illegal to Add() children after calling JoinAll().
  void JoinAll();

  // Returns a token that may be used to Select() against the completion of all
  // fibers belonging to this Bundle.
  // It is illegal to Add() children after calling OnJoinable().
  // Note: Callers must still ensure JoinAll() is called.
  thread::Case OnJoinable();

 private:
  thread::FifoSemaphore sem_;
  thread::Bundle bundle_;
};

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_CONTRIB_BOUNDED_BUNDLE_BOUNDED_BUNDLE_H_
