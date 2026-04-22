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

//
// This file defines the SchedPolicy enum and thread::Options class.

#ifndef THIRD_PARTY_GLOOP_THREAD_THREAD_OPTIONS_H_
#define THIRD_PARTY_GLOOP_THREAD_THREAD_OPTIONS_H_

#include <sys/types.h>  // for size_t

#include <cstddef>

namespace thread {

/**
   SchedPolicy defines the thread scheduling policy. Policies
   SCHEDPOLICY_BESTEFFORT, SCHEDPOLICY_NORMAL and SCHEDPOLICY_URGENT can be
   used without special permissions.

   SCHEDPOLICY_NORMAL should be used by most threads and threadpools.

   SCHEDPOLICY_BESTEFFORT should be used for threads that run only when no
   other threads are runnable.  It should be used by background tasks.

   SCHEDPOLICY_URGENT is reserved for threads that should be run promptly.
   The goal of this class is to improve latency, but not throughput;
   threads that use a lot of CPU may be penalized, and receive less CPU
   than threads using SCHEDPOLICY_NORMAL. Please note that depending on the
   implementation and configuration, SCHEDPOLICY_URGENT may actually behave
   identically to SCHEDPOLICY_NORMAL.

   SCHEDPOLICY_FIFO is not for general use. It forces the kernel to use strict
   priority scheduling.  This policy requires special permissions, could fail
   silently, and may behave differently on different platforms.

   WARNING: These policies have different platform-specific and context-specific
   caveats. Please verify that your thread's scheduling policy is applied as
   intended.

   See //gloop/thread/thread.cc and //gloop/thread/cpu_subcontainer.cc for
   details about the implementations.
*/
enum SchedPolicy {
  SCHEDPOLICY_BESTEFFORT,  // Low-priority
  SCHEDPOLICY_NORMAL,      // Default policy
  SCHEDPOLICY_URGENT,      // For threads that must run whenever it can
  SCHEDPOLICY_FIFO,        // Strict FIFO Scheduling. Not for general use
};

/** Options to configure a Thread.  Default values are listed in
    the field descriptions */
class Options {
 public:
  /** Initialize options to default values. Note that there is no
      constructor that accepts values for all of the fields, because it
      and all callers would need to be modified every time a new field
      was added.  All newly added fields must have default values that
      do not change existing behavior. */
  Options();

  Options(const Options& options) = default;

  /** Cleanup options. */
  ~Options();

  /** Mark the thread as joinable, which requires that the thread be joined
      in order to clean up its state after it exits.  By default threads are
      not joinable. */
  Options& set_joinable(bool joinable) {
    joinable_ = joinable;
    return *this;
  }

  /** Return whether the thread is joinable (and must be joined) or not */
  bool joinable() const { return joinable_; }

  /** Set the thread stack size (in bytes).  Passing bytes==0 resets the
      stack size to the default value. */
  Options& set_stack_size(size_t bytes) {
    stack_size_ = bytes;
    return *this;
  }

  /** Return the thread stack size */
  size_t stack_size() const { return stack_size_; }

  /** Set the thread stack guard size (in bytes).  A size of 0 resets the
      stack guard size to the default value, which is PAGESIZE (4096 bytes)
      with glibc versions 2.2.2 through 2.3.6 on i686-unknown-linux-gnu.

      The stack guard is a protected region at the bottom of the stack,
      created in order to protect against stack overflow.  The guard region
      is recommended to be at least as large as the largest possible
      stack frame or stack probe interval.  See also:

      <link>

      The size of the stack guard is in addition to the size of the stack.
      A memory access into the stack guard region causes SIGSEGV;
      a system call (such as time(2), pipe(2), or read(2)) returns EFAULT.
  */
  Options& set_guard_size(size_t guard_size) {
    guard_size_ = guard_size;
    return *this;
  }

  /** Return the thread guard region size */
  size_t guard_size() const { return guard_size_; }

  /** Set the scheduling policy of this thread or threadpool */
  Options& set_scheduling_policy(SchedPolicy policy) {
    scheduling_policy_ = policy;
    return *this;
  }

  /** Return the scheduling policy */
  SchedPolicy scheduling_policy() const { return scheduling_policy_; }

  /** Set the scheduling priority if the scheduling policy is FIFO.
   *  Has no effect otherwise.  A negative value results in the default/legacy
   *  behavior of setting priority to "sched_get_priority_max(SCHED_FIFO) - 1".
   *
   *  WARNING: Using a negative value (the default) may actually result in
   *  the scheduling policy being silently ignored, depending on your platform.
   *  Please verify that your thread's policy and priority work as intended, and
   *  see the implementation for details.
   */
  Options& set_sched_priority(int sched_priority) {
    sched_priority_ = sched_priority;
    return *this;
  }

  int sched_priority() const { return sched_priority_; }

  //
  // CAUTION: The following methods are not for general use
  //

  Options& set_nice_priority_level(int nice_priority_level) {
    nice_priority_level_ = nice_priority_level;
    return *this;
  }

  /** Return thread nice level */
  int nice_priority_level() const { return nice_priority_level_; }

  /** Advanced usage: Set the thread io_class and ioprio level. Use of this flag
      is not recommended in most situations. See util/priority/io-priority.h for
      possible values of ioprio and io_class.

      The exact effect varies with different versions of the Linux kernel and
      glibc. Refer to system documentation (man ioprio_set) for more details.
  */
  Options& set_io_priority(int io_class, int io_priority_level) {
    io_class_ = io_class;
    io_priority_level_ = io_priority_level;
    return *this;
  }

  /** Return thread io_priority level and io_class */
  int io_priority_level() const { return io_priority_level_; }
  int io_class() const { return io_class_; }

 private:
  size_t stack_size_;              // Size of thread stack
  size_t guard_size_;              // Size of thread stack guard
  SchedPolicy scheduling_policy_;  // Thread scheduling policy
  int sched_priority_;             // Thread sched priority
  int nice_priority_level_;        // Thread nice level
  int io_priority_level_;          // Thread io priority
  int io_class_;                   // Thread io class
  bool joinable_;                  // Thread is joinable?
};

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_THREAD_OPTIONS_H_
