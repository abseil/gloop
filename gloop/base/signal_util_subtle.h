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

#ifndef THIRD_PARTY_GLOOP_BASE_SIGNAL_UTIL_SUBTLE_H_
#define THIRD_PARTY_GLOOP_BASE_SIGNAL_UTIL_SUBTLE_H_

#ifdef __linux__
#include <poll.h>  // IWYU pragma: keep
#include <sched.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include <cstdint>
#include <vector>

#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/sysinfo.h"
#include "absl/time/time.h"

namespace base {
namespace internal {

// signal_safe_open() - a wrapper for open(2) which ignores signals
// Semantics equivalent to open(2):
//   returns a file-descriptor (>=0) on success, -1 on failure, error in errno
// We do not support specifying mode_t.
// REQUIRES: flags & O_CREAT == 0
// If you need to pass a mode_t arg, add another function. varargs with mode_t
// are not easily portable since it is sometimes a short and sometimes an int.
int signal_safe_open(const char* path, int flags);

// signal_safe_close() - a wrapper for close(2) which ignores signals
// Semantics equivalent to close(2):
//   returns 0 on success, -1 on failure, error in errno
int signal_safe_close(int fd);

// signal_safe_write() - a wrapper for write(2) which ignores signals
// Semantics equivalent to write(2):
//   returns number of bytes written, -1 on failure, error in errno
//   additionally, (if not NULL) total bytes written in *bytes_written
//
// In the interrupted (EINTR) case, signal_safe_write will continue attempting
// to write out buf.  This means that in the:
//   write->interrupted by signal->write->error case
// That it is possible for signal_safe_write to return -1 when there were bytes
// flushed from the buffer in the first write.  To handle this case the optional
// bytes_written parameter is provided, when not-NULL, it will always return the
// total bytes written before any error.
ssize_t signal_safe_write(int fd, const char* buf, size_t count,
                          size_t* bytes_written);

// signal_safe_read() - a wrapper for read(2) which ignores signals
// Semantics equivalent to read(2):
//   returns number of bytes written, -1 on failure, error in errno
//   additionally, (if not NULL) total bytes written in *bytes_written
//
// In the interrupted (EINTR) case, signal_safe_read will continue attempting
// to read into buf.  This means that in the:
//   read->interrupted by signal->read->error case
// That it is possible for signal_safe_read to return -1 when there were bytes
// read by a previous read.  To handle this case the optional bytes_written
// parameter is provided, when not-NULL, it will always return the total bytes
// read before any error.
ssize_t signal_safe_read(int fd, char* buf, size_t count, size_t* bytes_read);

// signal_safe_poll() - a wrapper for poll(2) which ignores signals
// Semantics equivalent to poll(2):
//   Returns number of structures with non-zero revent fields.
//
// In the interrupted (EINTR) case, signal_safe_poll will continue attempting to
// poll for data.  Unlike ppoll/pselect, signal_safe_poll is *ignoring* signals
// not attempting to re-enable them.  Protecting us from the traditional races
// involved with the latter.
int signal_safe_poll(struct ::pollfd* fds, int nfds, int timeout_ms);

// WARNING ********************************************************************
// getenv(2) can only be safely used in the absence of calls which perturb the
// environment (e.g. putenv/setenv/clearenv).  The use of such calls is
// strictly thread-hostile since these calls do *NOT* synchronize and there is
// *NO* thread-safe way in which the POSIX **environ array may be queried about
// modification.
// ****************************************************************************
// The default getenv(2) is not guaranteed to be thread-safe as there are no
// semantics specifying the implementation of the result buffer.  The result
// from thread_safe_getenv() may be safely queried in a multi-threaded context.
// If you have explicit synchronization with changes environment variables then
// any copies of the returned pointer must be invalidated across modification.
const char* thread_safe_getenv(const char* env_var);

// Does not support locales, base != 10 or 16, or errno.  <err> set
// to any error.
uint64_t signal_safe_strtou64(const char* ptr, const char** endptr, int base,
                              int* err);

// Set up a line-by-line reader using the fixed buffer
// buf[]. Repeatedly calling Next() returns equivalent results to
// fgets(buf, buf_size), without requiring a FILE *. It also
// explicitly reports errors instead of conflating them with EOF.
// It will not malloc, and in fact is safe to use from signal
// handlers (other than scribbling on errno.)
//
// It is, however, only thread-compatible: parallel access to a single
// LineReader is unspecified, as is reentrant access.  In other words:
// Creating one of these in your signal handler is fine, using the
// same one from a signal handler and outside is not.
//
// One important caveat: freely reads ahead.  It is unspecified how far ahead
// of the last call to Next() an independent call to read(fd) will go.
class LineReader {
 public:
  // <fd> should not be a socket/pipe or similar: just local files
  // (proc/similar are fine.)
  LineReader(int fd, char* buf, size_t buf_size);
  // If we are at EOF, return NULL and set *error = 0.  If reading the
  // file causes an error, return NULL and set *error to errno as returned
  // by signal_safe_read.  (This will scribble on errno.)
  // Otherwise, return a null-terminated string (stored somewhere in
  // buf) that is either the next full line (including the newline) of
  // <fd>, the next line truncated to buf_size - 1, or the entire
  // remaining contents (whichever comes first.)
  char* Next(int* error);

 private:
  char* const buf_;
  size_t size_;
  char* start_;
  size_t valid_;
  int fd_;
  char overwritten_;
  char* OutputLine(char* end);
};

// RAII types for signals.
class ScopedSigaction {
 public:
  // Set the handler for <sig> to <handler> with default options;
  // restore to old value on destruction.
  ScopedSigaction(int sig,
                  void (*handler)(
                      // lint screws up on function pointer without newline
                      int))
      : signum_(sig) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    Enable(&action);
  }

  ScopedSigaction(int sig,
                  void (*action)(
                      // lint screws up on function pointer without newline
                      int, siginfo_t*, void*))
      : signum_(sig) {
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = action;
    Enable(&act);
  }

  // Set the handler for <sig> as described in <action>; restore to old
  // value on destruction.
  ScopedSigaction(int sig, struct sigaction* action) : signum_(sig) {
    Enable(action);
  }

  ~ScopedSigaction() {
    ABSL_RAW_CHECK(0 == sigaction(signum_, &old_, nullptr),
                   "Unable to restore");
  }

  // Not copyable or movable.
  ScopedSigaction(const ScopedSigaction&) = delete;
  ScopedSigaction& operator=(const ScopedSigaction&) = delete;

 private:
  void Enable(struct sigaction* action) {
    ABSL_RAW_CHECK(0 == sigaction(signum_, action, &old_),
                   "Unsable to set signal handler");
  }
  struct sigaction old_;
  int signum_;
};

// Timers that run for the length of this object's existence.
class ScopedPosixTimer {
 public:
  // Constructors added to this class on-demand, contributions welcome.
  enum TimerType {
    OneShot = 1,
    Repeated = 2,
  };
  // Set up a timer that delivers <sig> to the thread which
  // created the object.
  ScopedPosixTimer(TimerType t, ::absl::Duration interval, clockid_t clock,
                   int sig) {
    struct sigevent ev;
    ev.sigev_notify = SIGEV_THREAD_ID;
#if defined(__GLIBC__) || defined(__ASYLO__)
    // Libc doesn't feel like exposing <sigev_notify_thread_id>, because
    // it's not for normal people.  Like I care.
    ev._sigev_un._tid = absl::base_internal::GetTID();
#else
    ev.sigev_notify_thread_id = absl::base_internal::GetTID();
#endif
    ev.sigev_signo = sig;
    ABSL_RAW_CHECK(0 == timer_create(clock, &ev, &t_),
                   "Unable to create timer");
    struct timespec ts = ::absl::ToTimespec(interval);
    struct timespec zero = {0, 0};
    struct itimerspec spec = {t == Repeated ? ts : zero, ts};
    ABSL_RAW_CHECK(0 == timer_settime(t_, 0, &spec, nullptr),
                   "Unable to configure timer");
  }

  ~ScopedPosixTimer() {
    // explicitly safe on armed timer
    ABSL_RAW_CHECK(0 == timer_delete(t_), "Unable to delete timer");
  }

 private:
  timer_t t_;
};

// Read variable width={1,2,4,8} bytes from *ptr.  No other widths are
// supported.  Optimal when width is not known at compile time.
// It must be legal to make an 8-byte aligned read at <ptr>.
uint64_t ReadVariableUnsigned(volatile void* ptr, size_t width);
int64_t ReadVariableSigned(volatile void* ptr, size_t width);

// write  width={1,2,4,8} bytes from <val> into *ptr.  No other widths are
// supported.  Optimal when width is not known at compile time.
// It must be legal to make an 8-byte aligned read at <ptr>.
// <val> is passed as 64-bits, but must logically fit in the matching
// width-byte type.
void WriteVariableUnsigned(volatile void* ptr, size_t width, uint64_t val);
void WriteVariableSigned(volatile void* ptr, size_t width, int64_t val);

// Sleep for at least <nanos> total nanoseconds, even if you take signals.
void SleepForNanoseconds(int64_t nanos);

// implementation details after here.

inline uint64_t ReadVariableSwitchUnsigned(volatile void* ptr, size_t width) {
  switch (width) {
    case 1:
      return *reinterpret_cast<volatile uint8_t*>(ptr);
      break;
    case 2:
      return *reinterpret_cast<volatile uint16_t*>(ptr);
      break;
    case 4:
      return *reinterpret_cast<volatile uint32_t*>(ptr);
      break;
    case 8:
      return *reinterpret_cast<volatile uint64_t*>(ptr);
      break;
  }
  // should never get here
  return 0;
}

inline int64_t ReadVariableSwitchSigned(volatile void* ptr, size_t width) {
  switch (width) {
    case 1:
      return *reinterpret_cast<volatile int8_t*>(ptr);
      break;
    case 2:
      return *reinterpret_cast<volatile int16_t*>(ptr);
      break;
    case 4:
      return *reinterpret_cast<volatile int32_t*>(ptr);
      break;
    case 8:
      return *reinterpret_cast<volatile int64_t*>(ptr);
      break;
  }
  // should never get here
  return 0;
}

inline uint64_t ReadVariableAlignMaskUnsigned(volatile void* ptr,
                                              size_t width) {
  intptr_t orig = reinterpret_cast<intptr_t>(ptr);
  intptr_t align = orig & ~7;
  uint64_t out = *reinterpret_cast<volatile uint64_t*>(align);
  out >>= 8 * (orig - align);
  size_t shift = 8 * (8 - width);
  out <<= shift;
  out >>= shift;
  return out;
}

inline int64_t ReadVariableAlignMaskSigned(volatile void* ptr, size_t width) {
  intptr_t orig = reinterpret_cast<intptr_t>(ptr);
  intptr_t align = orig & ~7;
  uint64_t val = *reinterpret_cast<volatile uint64_t*>(align);
  val >>= 8 * (orig - align);
  size_t shift = 8 * (8 - width);
  val <<= shift;
  int64_t out = static_cast<int64_t>(val);
  out >>= shift;
  return out;
}

inline uint64_t ReadVariableUnsigned(volatile void* ptr, size_t width) {
  ABSL_RAW_DCHECK(width == 1 || width == 2 || width == 4 || width == 8,
                  "illegal width");
  // known-safe on all architectures.
  return ReadVariableSwitchUnsigned(ptr, width);
}

inline int64_t ReadVariableSigned(volatile void* ptr, size_t width) {
  ABSL_RAW_DCHECK(width == 1 || width == 2 || width == 4 || width == 8,
                  "illegal width");
  // known-safe on all architectures.
  return ReadVariableSwitchSigned(ptr, width);
}

inline void WriteVariableSwitchUnsigned(volatile void* ptr, size_t width,
                                        uint64_t val) {
  switch (width) {
    case 1:
      *reinterpret_cast<volatile uint8_t*>(ptr) = static_cast<uint8_t>(val);
      break;
    case 2:
      *reinterpret_cast<volatile uint16_t*>(ptr) = static_cast<uint16_t>(val);
      break;
    case 4:
      *reinterpret_cast<volatile uint32_t*>(ptr) = static_cast<uint32_t>(val);
      break;
    case 8:
      *reinterpret_cast<volatile uint64_t*>(ptr) = static_cast<uint64_t>(val);
      break;
  }
}

inline void WriteVariableUnsigned(volatile void* ptr, size_t width,
                                  uint64_t val) {
  ABSL_RAW_DCHECK(width == 1 || width == 2 || width == 4 || width == 8,
                  "illegal width");
  // no other implementations (yet)
  WriteVariableSwitchUnsigned(ptr, width, val);
}

inline void WriteVariableSigned(volatile void* ptr, size_t width, int64_t val) {
  ABSL_RAW_DCHECK(width == 1 || width == 2 || width == 4 || width == 8,
                  "illegal width");
  // this actually does exactly what we want; casting int64 -> uint64
  // doesn't change bits, and uint64 -> uint<smaller> just truncates bits.
  WriteVariableUnsigned(ptr, width, val);
}

// Affinity helpers.

// Returns a vector of the which cpus the currently allowed thread is allowed to
// run on.  There are no guarantees that this will not change before, after, or
// even during, the call to AllowedCpus().
std::vector<int> AllowedCpus();

// Enacts a scoped affinity mask on the constructing thread.  Attempts to
// restore the original affinity mask on destruction.
//
// REQUIRES: For test-use only.  Do not use this in production code.
class ScopedAffinityMask {
 public:
  // When racing with an external restriction that has a zero-intersection with
  // "allowed_cpus" we will construct, but immediately register as "Tampered()",
  // without actual changes to affinity.
  explicit ScopedAffinityMask(std::vector<int> allowed_cpus);

  // Restores original affinity iff our scoped affinity has not been externally
  // modified (i.e. Tampered()).  Otherwise, the updated affinity is preserved.
  ~ScopedAffinityMask();

  // Returns true if the affinity mask no longer matches what was set at point
  // of construction.
  //
  // Note:  This is instantaneous and not fool-proof.  It's possible for an
  // external affinity modification to subsequently align with our originally
  // specified "allowed_cpus".  In this case Tampered() will return false when
  // time may have been spent executing previously on non-specified cpus.
  bool Tampered();

 private:
  cpu_set_t original_cpus_, specified_cpus_;
};

}  // namespace internal
}  // namespace base

#endif  // __linux__
#endif  // THIRD_PARTY_GLOOP_BASE_SIGNAL_UTIL_SUBTLE_H_
