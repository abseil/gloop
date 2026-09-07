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

// This is an internal API which abstracts away the API differences between the
// platform specific sem_t and semaphore_t types.

#ifndef THIRD_PARTY_GLOOP_THREAD_OS_SEMAPHORE_H_
#define THIRD_PARTY_GLOOP_THREAD_OS_SEMAPHORE_H_

#include <ctime>

#include "absl/time/clock.h"
#include "absl/time/time.h"

#if !defined(__APPLE__)
#include <semaphore.h>
#else
#include <mach/mach_init.h>
#include <mach/semaphore.h>
#include <mach/task.h>
#endif

namespace thread {
namespace internal {

#if !defined(__APPLE__)

using OsSemaphore = sem_t;

inline int OsSemaphoreInit(sem_t* sem) {
  return sem_init(sem, 0 /* not shared */, 0 /*initial value */);
}

inline int OsSemaphoreDestroy(sem_t* sem) {
#if defined(__ANDROID__) && __ANDROID_API__ < __ANDROID_API_M__
  int orig_errno = errno;
  int result = sem_destroy(sem);
  if (result == -1 && errno == EBUSY) {
    // b/37003968, sem_destroy after timedwait can return EBUSY. Silently ignore
    // for consistency with newer Androids.
    result = 0;
    errno = orig_errno;
  }
  return result;
#else
  return sem_destroy(sem);
#endif
}

inline int OsSemaphoreWait(sem_t* sem) { return sem_wait(sem); }

inline int OsSemaphorePost(sem_t* sem) { return sem_post(sem); }

inline int OsSemaphoreTimedWait(sem_t* sem, const struct timespec* abs) {
  return sem_timedwait(sem, abs);
}

inline int OsSemaphoreTimedWaitRelative(sem_t* sem, absl::Duration timeout) {
  struct timespec timeout_time = absl::ToTimespec(absl::Now() + timeout);
  return sem_timedwait(sem, &timeout_time);
}

#else  // __APPLE__

// Mac OS X lacks the POSIX sem_timedwait() function, but it has kernel
// semaphores which do provide a semaphore_timedwait() function.

using OsSemaphore = semaphore_t;

inline int OsSemaphoreInit(semaphore_t* sem) {
  if (semaphore_create(mach_task_self(), sem, SYNC_POLICY_FIFO,
                       0 /* initial value */) != KERN_SUCCESS) {
    errno = EINVAL;
    return -1;
  }
  return 0;
}

inline int OsSemaphoreDestroy(semaphore_t* sem) {
  if (semaphore_destroy(mach_task_self(), *sem) != KERN_SUCCESS) {
    errno = EINVAL;
    return -1;
  }
  return 0;
}

inline int OsSemaphorePost(semaphore_t* sem) {
  if (semaphore_signal(*sem) != KERN_SUCCESS) {
    errno = EINVAL;
    return -1;
  }
  return 0;
}

inline int OsSemaphoreWait(semaphore_t* sem) {
  if (semaphore_wait(*sem) != KERN_SUCCESS) {
    errno = EINVAL;
    return -1;
  }
  return 0;
}

inline int OsSemaphoreTimedWaitRelative(semaphore_t* sem,
                                        absl::Duration timeout) {
  mach_timespec_t mach_timeout;
  if (timeout < absl::Seconds(0)) {  // Seconds in mach_timespec_t is unsigned.
    mach_timeout.tv_sec = 0;
    mach_timeout.tv_nsec = 0;
  } else {
    struct timespec timeout_time = absl::ToTimespec(timeout);
    mach_timeout.tv_sec = static_cast<unsigned int>(timeout_time.tv_sec);
    mach_timeout.tv_nsec = static_cast<clock_res_t>(timeout_time.tv_nsec);
  }
  // Note: This API uses relative time.
  kern_return_t err = semaphore_timedwait(*sem, mach_timeout);
  switch (err) {
    case KERN_OPERATION_TIMED_OUT:
      errno = ETIMEDOUT;
      return -1;
    case KERN_ABORTED:
      errno = EINTR;
      return -1;
    case KERN_SUCCESS:
      return 0;
    default:
      errno = EINVAL;
      return -1;
  }
}

inline int OsSemaphoreTimedWait(semaphore_t* sem, const struct timespec* abs) {
  return OsSemaphoreTimedWaitRelative(
      sem, absl::TimeFromTimespec(*abs) - absl::Now());
}

#endif  // __APPLE__

}  // namespace internal
}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_OS_SEMAPHORE_H_
