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

// AddAfterHelper is intended for use by Executor implementors and should be
// ignored by Executor clients. In particular, Executors can use this class to
// implement AddAfter() without crashing when the Executor is destroyed before
// the last delayed call is enqueued.
//
// If we create an instance of AddAfterHelper:
//
//   AddAfterHelper helper((Executor*)underlying_executor,
//                         (Callback1<Closure*>*) complete_add_after);
//
// and then call
//
//   helper.ScheduleAddAfter(N/*ms*/, task)
//
// then approximately N ms later, helper will arrange to call
// complete_add_after->Run(task) on a thread belonging to
// underlying_executor.  If underlying_executor is NULL, helper will
// use the TimedCall thread instead (see //thread/timedcall.h).
//
// Instances can be shut down, which helps ensure that surrounding
// objects aren't touched after they're destroyed.  On shutdown,
// scheduled calls will run immediately outside the underlying Executor.

#ifndef THIRD_PARTY_GLOOP_THREAD_ADD_AFTER_HELPER_H_
#define THIRD_PARTY_GLOOP_THREAD_ADD_AFTER_HELPER_H_

#include <cstdint>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/base/callback.h"

namespace util {
namespace callback {
class CancellableClosure;
}
}  // namespace util

namespace thread {

class Executor;

// See the file comment.  This class is thread-safe.
class AddAfterHelper {
 public:
  AddAfterHelper(Executor* underlying_executor,
                 absl::AnyInvocable<void(absl::AnyInvocable<void() &&>)>
                     complete_add_after);

  // This type is neither copyable nor movable.
  AddAfterHelper(const AddAfterHelper&) = delete;
  AddAfterHelper& operator=(const AddAfterHelper&) = delete;

  // The destructor runs all pending tasks.
  ~AddAfterHelper();

  // Arranges to call complete_add_after->Run(task) after ms milliseconds.
  void ScheduleAddAfter(absl::Duration delay, Closure* task);
  void ScheduleAddAfterAt(absl::Time when,
                          absl::AnyInvocable<void() &&> callback);

  // If any scheduled AddAfter tasks are still pending, this method
  // passes them to complete_add_after immediately, in the current
  // thread.  After this call returns, complete_add_after will never
  // be called again.  tasks passed to ScheduleAddAfter will be
  // deleted instead of being run.  An Executor using this class to
  // implement AddAfter() will usually call this function in its
  // destructor.
  //
  // Don't call this twice concurrently.  The second call may return
  // before all of the AddAfter calls have completed.
  void ShutdownAndRunPendingImmediately();

  // True if ShutdownAndRunPendingImmediately() has been called or the
  // destructor has started.
  bool IsShuttingDown() const;

 private:
  void CompleteAndRemoveFromMap(int64_t sequence_number,
                                absl::AnyInvocable<void() &&> task);
  util::callback::CancellableClosure* AddTaskForCompletion(
      absl::AnyInvocable<void() &&> task);

  mutable absl::Mutex mu_;

  Executor* const underlying_executor_;
  absl::AnyInvocable<void(absl::AnyInvocable<void() &&>)> complete_add_after_;

  int64_t next_sequence_number_ ABSL_GUARDED_BY(mu_);
  bool shutting_down_ ABSL_GUARDED_BY(mu_);

  // Tracks pending calls to AddAfter so we can cancel them on shutdown.  Each
  // pending call gets a sequence number so that we can distinguish distinct
  // adds of the same Closure*.
  absl::flat_hash_map<int64_t, util::callback::CancellableClosure*> add_afters_
      ABSL_GUARDED_BY(mu_);
};

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_ADD_AFTER_HELPER_H_
