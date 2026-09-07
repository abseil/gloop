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

// Internal data-structures useful in the standard scheduler implementations.

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_SCHEDULER_TYPES_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_SCHEDULER_TYPES_H_

#include <atomic>
#include <cstdint>
#include <utility>

#include "absl/container/fixed_array.h"

namespace base {
namespace scheduling {
class Slot;
}  // namespace scheduling
}  // namespace base

namespace thread {
namespace internal {

// A fixed-size, lock-free stack of Slots.  Typically used by a Scheduler
// implementation to represent their idle slots.  All methods are thread-safe.
class FixedSlotStack {
 public:
  // REQUIRES: This stack may only ever hold "max_size" elements.
  explicit FixedSlotStack(int max_size);

  // This type is neither copyable nor movable.
  FixedSlotStack(const FixedSlotStack&) = delete;
  FixedSlotStack& operator=(const FixedSlotStack&) = delete;

  // REQUIRES: Stack is empty.
  ~FixedSlotStack();

  // Push "slot" to the top of this stack.  Thread-safe.
  // REQUIRES: slot != nullptr
  // REQUIRES: No more than "max_size" concurrent Push() operations.
  void Push(base::scheduling::Slot slot);

  // Pops, and returns, the top of this stack.  If the stack is empty, spins
  // until there is a matching Push().
  // REQUIRES: No more than "max_size" concurrent Pop() operations.
  base::scheduling::Slot Pop();

 private:
  std::atomic<int32_t> height_;
  absl::FixedArray<std::atomic<intptr_t>> stack_;
};

// CombinerLocks are a special type of spin-lock in which the critical section
// may be designated for remote execution under contention.  This provides
// improved cache-locality, especially for list-like data-structures in which
// significant state is shared between operations.
class CombinerLock {
 public:
  typedef intptr_t (*CombinableFunction)(void*);

  CombinerLock();

  // This type is neither copyable nor movable.
  CombinerLock(const CombinerLock&) = delete;
  CombinerLock& operator=(const CombinerLock&) = delete;

  // REQUIRES: No outstanding calls to ExecuteLocked()
  ~CombinerLock();

  // Execute the critical section specified by "function(arg)", which may return
  // a word sized value, exclusively under this lock.  Under contention the
  // actual execution of "function" may be delegated to a remote thread.
  intptr_t ExecuteLocked(CombinableFunction function, void* arg);

  // Cast-simplifying versions of ExecuteLocked(...).
  template <typename ArgType>
  inline intptr_t ExecuteLocked(intptr_t (*combinable)(ArgType*), ArgType* arg);
  template <typename ResultType, typename ArgType>
  inline ResultType* ExecuteLocked(ResultType* (*combinable)(ArgType*),
                                   ArgType* arg);

 private:
  template <typename ResultPtr, typename ArgType>
  static intptr_t InternalApply(void* callback_arg_pair);

  void Wait(std::atomic<int32_t>* done);
  bool WakeWaiters();

  std::atomic<intptr_t> queue_;
  std::atomic<uint32_t> waiters_;
};

//------------------------------------------------------------------------------
// End of public interfaces.
//------------------------------------------------------------------------------

template <typename ResultPtr, typename ArgType>
intptr_t CombinerLock::InternalApply(void* callback_arg_pair) {
  auto pair_ptr =
      reinterpret_cast<std::pair<ResultPtr (*)(ArgType*), ArgType*>*>(
          callback_arg_pair);
  return reinterpret_cast<intptr_t>(pair_ptr->first(pair_ptr->second));
}

template <typename ArgType>
intptr_t CombinerLock::ExecuteLocked(intptr_t (*combinable)(ArgType*),
                                     ArgType* arg) {
  std::pair<intptr_t (*)(ArgType*), ArgType*> closure(combinable, arg);
  return ExecuteLocked(InternalApply<intptr_t, ArgType>, &closure);
}

template <typename ResultType, typename ArgType>
ResultType* CombinerLock::ExecuteLocked(ResultType* (*combinable)(ArgType*),
                                        ArgType* arg) {
  std::pair<ResultType* (*)(ArgType*), ArgType*> closure(combinable, arg);
  return reinterpret_cast<ResultType*>(
      ExecuteLocked(InternalApply<ResultType*, ArgType>, &closure));
}

}  // namespace internal
}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_SCHEDULER_TYPES_H_
