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

// The interface(s) exported by this module are subtle and intended only for the
// implementation of threading primitives.
#ifndef THIRD_PARTY_GLOOP_THREAD_STACK_RECLAIMER_H_
#define THIRD_PARTY_GLOOP_THREAD_STACK_RECLAIMER_H_

#include <stdint.h>
#include <stdlib.h>

namespace thread {
namespace internal {

class StackReclaimerTestHelper;

// On supported platforms, StackReclaimer allows the reclaim of pages allocated
// by the operating system to back stack frames which are no longer being used.
// This reclaimation is not exhaustive; the goal is that sufficient memory is
// allocated that typical future stack growth will not actually result in new
// memory being assigned (e.g. via demand paging, linked stack, or other).  We
// call this the "working size".
//
// This is intended for interfaces which maintain internal thread pools (such as
// Fibers and Executors) to reclaim memory from idle threads; particularly when
// execution does not commonly require a deep stack.
//
// Example usage:
//    StackReclaimer reclaimer;
//
//    while (have_more_requests) {
//      ... execute next request ...
//
//
//      // Automatically reclaim allocated stack memory in excess of the
//      // "working size".  This does not prevent this stack space from
//      // reallocated in the future.
//      reclaimer.ReduceMemoryUsage();
//    }
class StackReclaimer {
 public:
  // StackReclaimer objects are per-thread and may only be accessed by the
  // thread which constructed them.
  //
  // REQUIRES: May not be used by the 'main()' thread.
  StackReclaimer();

  // Not copyable or movable.
  StackReclaimer(const StackReclaimer&) = delete;
  StackReclaimer(StackReclaimer&&) = delete;
  StackReclaimer& operator=(const StackReclaimer&) = delete;
  StackReclaimer& operator=(StackReclaimer&&) = delete;

  // Attempt to reduce the memory used by the current thread's stack to a
  // reasonable working size (by releasing storage for stack frames which are no
  // longer used).  This does not change the size of the stack for the calling
  // thread.
  //
  // The implementation reserves the right to adjust this working size
  // dynamically.  The intent is that the working size is sufficient to support
  // typical execution.  The working size is always calculated relative to the
  // point at which ReduceMemoryUsage() is called.
  //
  // This method has very low-overhead in the (ideally) common case that
  // execution (since the last call) has remained within the working size.
  //
  // REQUIRES: May only be called by the thread which constructed *this, on the
  //           stack that it was running on at the time of construction.
  void ReduceMemoryUsage();

  // As above, except that a minimum acceptable working size may be specified.
  // The implementation may still choose to preserve more than this.
  void ReduceMemoryUsage(size_t min_working_size);

 private:
  void ReclaimWithWorkingSize(size_t working_size);

  static bool Available();
  static void CalculateAreaToReclaim(intptr_t current_sp, size_t working_size,
                                     intptr_t limit, bool grows_up,
                                     intptr_t* keep_out, intptr_t* limit_out);

  intptr_t stack_addr_;  // Note:  Adjusted for guard region when present.
  size_t stack_size_;
  int last_ticker_read_;

  friend class StackReclaimerTestHelper;
};

}  // namespace internal
}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_STACK_RECLAIMER_H_
