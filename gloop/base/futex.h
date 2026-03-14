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

#ifndef THIRD_PARTY_GLOOP_BASE_FUTEX_H_
#define THIRD_PARTY_GLOOP_BASE_FUTEX_H_
// YOU PROBABLY DON'T WANT TO USE THIS FILE!  man futex(2): "To
// reiterate, bare futexes are not intended as an easy-to-use abstraction
// for end-users.  (There is no wrapper function for this system call
// in  glibc.)   Implementors  are expected to be assembly literate and to
// have read the sources of the futex user-space library referenced below."
//
// This is not a nice user space library wrapping futexes, this is a
// google3 shim to make raw futex calls.  You have been warned.

#include <errno.h>
#include <stdint.h>
#include <syscall.h>
#include <time.h>

#include <atomic>
#include <cstdint>

#include "absl/base/internal/raw_logging.h"
#include "absl/synchronization/internal/kernel_timeout.h"
#include "gloop/base/inlined_syscall.h"

#ifdef __linux__
#include <linux/futex.h>
#endif

namespace base {

// Expose (a useful subset of) the basic Futex API in a more C++y fashion.
// ALL of these use FUTEX_PRIVATE; do not rely on them cross process.
// TODO: expose both kinds if there's any demand (I doubt it.)
// All of them will CHECK-fail if !__linux__.
class Futex {
 public:
  // These operations correspond one-to-one to calls in futex(2), with
  // the exception that return values on error are -<error code>, not
  // -1 with errno set. i.e. Wait(nullptr) will return -EACCES.

  // This is FUTEX_WAIT.
  static int Wait(std::atomic<int32_t>* v, int32_t val);
  // FUTEX_WAIT with a timeout.
  static int WaitRelativeTimeout(std::atomic<int32_t>* v, int32_t val,
                                 const struct timespec* reltime);
  // FUTEX_WAIT with an absolute (CLOCK_REALTIME) timeout (implemented using
  // FUTEX_WAIT_BITSET and FUTEX_BITSET_MATCH_ANY.)
  static int WaitAbsoluteTimeout(std::atomic<int32_t>* v, int32_t val,
                                 const struct timespec* abstime);
  // As WaitAbsoluteTimeout, but takes a google3 Timeout.
  // Note that *unlike* the above, this function considers a timeout before
  // the unix epoch valid.
  static int WaitUntil(std::atomic<int32_t>* v, int32_t val,
                       absl::synchronization_internal::KernelTimeout t);

  // FUTEX_WAIT_BITSET
  static int WaitBitsetAbsoluteTimeout(std::atomic<int32_t>* v, int32_t val,
                                       int32_t bits,
                                       const struct timespec* abstime);
  // FUTEX_WAKE
  static int Wake(std::atomic<int32_t>* v, int32_t count);

  // FUTEX_WAKE_BITSET
  static int WakeBitset(std::atomic<int32_t>* v, int32_t count, int32_t bits);

  // Despite the disclaimer above, we do provide some _very simple_
  // logic on top of futexes; to be more specific, a one-waiter semaphore.
  // You _still do not want to be using this_.

  // Synchronously decrement the semaphore at <*v> (waiting on a futex there
  // until you can.) Acquire semantics versus a Post.
  // Note that at most one thread at a time may be waiting on v.
  static void SemWait(std::atomic<int32_t>* v);
  // As Wait, but with an absolute CLOCK_REALTIME timeout. Returns false
  // if timed out, true if wait successful.
  static bool SemTimedWait(std::atomic<int32_t>* v,
                           const struct timespec* abstime);
  // Increment the semaphore at *v. Release semantics versus Wait.
  // Multiple increments *are* allowed.
  static void SemPost(std::atomic<int32_t>* v);

  // Semantically, Futex::Swap(v1, val, reltime, v2) behaves exactly as
  //
  //   Futex::Wake(v2, 1);
  //   return Futex::WaitRelativeTimeout(v1, val, reltime);
  //
  static int Swap(std::atomic<int32_t>* v1, int32_t val,
                  const struct timespec* reltime, std::atomic<int32_t>* v2);
};

// Public API ends here.
#ifndef __linux__
inline int Futex::Wait(std::atomic<int32_t>* v, int32_t val) {
  ABSL_RAW_LOG(FATAL, "Do not invoke futexes on non Linux");
  return 0;
}
inline int Futex::WaitRelativeTimeout(std::atomic<int32_t>* v, int32_t val,
                                      const struct timespec* reltime) {
  ABSL_RAW_LOG(FATAL, "Do not invoke futexes on non Linux");
  return 0;
}
inline int Futex::WaitAbsoluteTimeout(std::atomic<int32_t>* v, int32_t val,
                                      const struct timespec* abstime) {
  ABSL_RAW_LOG(FATAL, "Do not invoke futexes on non Linux");
  return 0;
}

inline int Futex::WaitUntil(std::atomic<int32_t>* v, int32_t val,
                            absl::synchronization_internal::KernelTimeout t) {
  ABSL_RAW_LOG(FATAL, "Do not invoke futexes on non Linux");
  return 0;
}
inline int Futex::WaitBitsetAbsoluteTimeout(std::atomic<int32_t>* v,
                                            int32_t val, int32_t bits,
                                            const struct timespec* abstime) {
  ABSL_RAW_LOG(FATAL, "Do not invoke futexes on non Linux");
  return 0;
}
inline int Futex::Wake(std::atomic<int32_t>* v, int32_t count) {
  ABSL_RAW_LOG(FATAL, "Do not invoke futexes on non Linux");
  return 0;
}
inline int Futex::WakeBitset(std::atomic<int32_t>* v, int32_t count,
                             int32_t bits) {
  ABSL_RAW_LOG(FATAL, "Do not invoke futexes on non Linux");
  return 0;
}
inline void Futex::SemWait(std::atomic<int32_t>* v) {
  ABSL_RAW_LOG(FATAL, "Do not invoke futexes on non Linux");
}
inline bool Futex::SemTimedWait(std::atomic<int32_t>* v,
                                const struct timespec* abstime) {
  ABSL_RAW_LOG(FATAL, "Do not invoke futexes on non Linux");
  return false;
}
inline void Futex::SemPost(std::atomic<int32_t>* v) {
  ABSL_RAW_LOG(FATAL, "Do not invoke futexes on non Linux");
}
#else

inline int Futex::Wait(std::atomic<int32_t>* v, int32_t val) {
  return WaitAbsoluteTimeout(v, val, nullptr);
}

inline int Futex::WaitRelativeTimeout(std::atomic<int32_t>* v, int32_t val,
                                      const struct timespec* reltime) {
  return InlinedSyscall4(__NR_futex, reinterpret_cast<intptr_t>(v),
                         FUTEX_WAIT_PRIVATE, val,
                         reinterpret_cast<intptr_t>(reltime));
}

inline int Futex::WaitAbsoluteTimeout(std::atomic<int32_t>* v, int32_t val,
                                      const struct timespec* abstime) {
  return WaitBitsetAbsoluteTimeout(v, val, FUTEX_BITSET_MATCH_ANY, abstime);
}

inline int Futex::WaitUntil(std::atomic<int32_t>* v, int32_t val,
                            absl::synchronization_internal::KernelTimeout t) {
  if (t.has_timeout()) {
    struct timespec abstime = t.MakeAbsTimespec();
    return WaitAbsoluteTimeout(v, val, &abstime);
  }
  return Wait(v, val);
}

inline int Futex::WaitBitsetAbsoluteTimeout(std::atomic<int32_t>* v,
                                            int32_t val, int32_t bits,
                                            const struct timespec* abstime) {
  return InlinedSyscall6(__NR_futex, reinterpret_cast<intptr_t>(v),
                         FUTEX_WAIT_BITSET_PRIVATE | FUTEX_CLOCK_REALTIME, val,
                         reinterpret_cast<intptr_t>(abstime),
                         reinterpret_cast<intptr_t>(nullptr), bits);
}
inline int Futex::Wake(std::atomic<int32_t>* v, int32_t count) {
  return InlinedSyscall3(__NR_futex, reinterpret_cast<intptr_t>(v),
                         FUTEX_WAKE_PRIVATE, count);
}
inline int Futex::WakeBitset(std::atomic<int32_t>* v, int32_t count,
                             int32_t bits) {
  return InlinedSyscall6(__NR_futex, reinterpret_cast<intptr_t>(v),
                         FUTEX_WAKE_BITSET_PRIVATE, count,
                         reinterpret_cast<intptr_t>(nullptr),
                         reinterpret_cast<intptr_t>(nullptr), bits);
}

// Sadly we can't use this directly in PerThreadSem etc but c'est la vie.
inline void Futex::SemWait(std::atomic<int32_t>* v) {
  int x = v->fetch_sub(1, std::memory_order_acquire) - 1;
  if (x >= 0) return;
  while (true) {
    const int ret = Wait(v, x);
    if (ret == 0) return;
    if (ret == -EWOULDBLOCK) {
      x = v->load(std::memory_order_relaxed);
      // raced with a waker, who will retry until we get woken.
      continue;
    }
    if (ret == -EINTR) continue;
    ABSL_RAW_LOG(FATAL, "Semaphore got a futex error: %d", ret);
  }
}

inline bool Futex::SemTimedWait(std::atomic<int32_t>* v,
                                const struct timespec* abstime) {
  int32_t x = v->fetch_sub(1, std::memory_order_acquire) - 1;
  if (x >= 0) return true;
  while (true) {
    const int ret = WaitAbsoluteTimeout(v, x, abstime);
    if (ret == 0) return true;
    if (ret == -ETIMEDOUT) {
      ABSL_RAW_CHECK(abstime != nullptr, "shouldn't timeout on untimed wait");
      const int32_t out = v->fetch_add(1, std::memory_order_acquire) + 1;
      if (out == 0) return false;
      // OK, at least one thread Posted (and exactly one will be
      // trying to FUTEX_WAKE us, we just won the race.)  Wait for
      // them.  We need to undo our increment above, since we did in fact
      // consume a wake.
      x = v->fetch_sub(1, std::memory_order_acquire) - 1;
      abstime = nullptr;
      continue;
    }
    if (ret == -EWOULDBLOCK) {
      x = v->load(std::memory_order_relaxed);
      // raced with a waker, who will retry until we get woken.
      continue;
    }
    if (ret == -EINTR) continue;
    ABSL_RAW_LOG(FATAL, "Semaphore got a futex error: %d", ret);
  }
}

inline void Futex::SemPost(std::atomic<int32_t>* v) {
  const int32_t x = v->fetch_add(1, std::memory_order_release) + 1;
  if (x > 0) return;
  while (true) {
    const int ret = Wake(v, 1);
    if (ret == 1) return;
    if (ret == 0) {
      // raced with the waiter, retry
      continue;
    }
    ABSL_RAW_LOG(FATAL, "Semaphore got a futex error: %d", ret);
  }
}
#endif
}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_FUTEX_H_
