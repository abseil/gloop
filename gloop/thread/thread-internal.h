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

// Implementation details for thread.cc; other users should not include this.
#ifndef THIRD_PARTY_GLOOP_THREAD_THREAD_INTERNAL_H_
#define THIRD_PARTY_GLOOP_THREAD_THREAD_INTERNAL_H_

#include <pthread.h>
#include <sys/types.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/base/internal/thread_identity.h"
#include "absl/base/thread_annotations.h"
#include "absl/flags/declare.h"
#include "absl/strings/string_view.h"
#include "gloop/base/scheduling/scheduling_mode.h"
#include "gloop/base/spinlock.h"
#include "gloop/util/gtl/intrusive_list.h"

ABSL_DECLARE_FLAG(bool, stacktrace_skip_waiting_threads);

namespace thread {
class Note;
}  // namespace thread

// Max number of stack trace entries to extract
inline constexpr int kStackCount = 128;

// Information about each live thread
class LiveThread : public gtl::intrusive_link<LiveThread> {
 public:
  pid_t thread_id_;
  // Use SCHEDULE_KERNEL_ONLY so that the spinlock can be taken in a signal
  // handler. See <internal thread>.
  mutable SpinLock notes_lock_{absl::base_internal::SCHEDULE_KERNEL_ONLY};
  // notes_version_ is incremented every times notes_ is modified. Writes are
  // guarded by notes_lock_ and always happen on the thread itself. Reads either
  // can be guarded by notes_lock_ or can use an atomic load (typically from a
  // signal handler).
  std::atomic<int64_t> notes_version_ = 0;
  // Add more information here as necessary.

  // Assigned by LiveThreadList to indicate insertion order into the list, which
  // is used during list iteration to present a consistent view of the list.
  // This is GUARDED_BY(&LiveThreadList::mu_), but that can't be represented
  // with thread annotations.
  int64_t list_seq_ = 0;
  int creator_stack_depth_;
  // Used by LiveThreadList to implement a lock-free list to reduce global
  // synchronization requirements on our intrusive list.
  LiveThread* next_unprocessed_ = nullptr;
  size_t alt_signal_stack_size_;
  char* name_;
  pthread_t tid_;
  // The stack used by SA_ONSTACK signal handlers in this thread.
  // If non-NULL, will be munmap()ed on destruction.
  void* alt_signal_stack_addr_;
  absl::base_internal::ThreadIdentity* identity_;

  char* name_prefix_;

  // Set true iff LiveThreadList is currently iterating this thread.
  // This is GUARDED_BY(&LiveThreadList::mu_), but that can't be represented
  // with thread annotations.
  int64_t num_currently_iterating_ = 0;
  std::vector<thread::Note*> notes_ ABSL_GUARDED_BY(notes_lock_);
  // Put the creator stack at the end because it is not needed to create
  // or destruct the class and takes up 256 bytes.
  void* creator_stack_[kStackCount];

  // Create a LiveThread structure for the current thread.  The new
  // thread's name is set to the given name_prefix with "/TID"
  // appended.  If name_prefix is empty, the thread will be given the
  // name "unnamed/TID".
  explicit LiveThread(absl::string_view name_prefix);

  // This type is neither copyable nor movable.
  LiveThread(const LiveThread&) = delete;
  LiveThread& operator=(const LiveThread&) = delete;

  // Destroy the live thread.  This function removes the thread from
  // the live_threads list.  MakeLive *must* be called before the
  // object is destroyed
  ~LiveThread();

  // Mark the thread as live, by putting it on the live_threads list.
  // This function *must* be called before the object is destroyed.
  void MakeLive();
};

namespace thread {

// Returns true, if pthread stack sizing include space for the guard area.
bool StackShouldIncludeGuardSize();

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_THREAD_INTERNAL_H_
