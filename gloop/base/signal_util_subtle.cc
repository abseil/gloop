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

#include "gloop/base/signal_util_subtle.h"

#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <cstdint>
#include <ctime>
#include <utility>
#include <vector>

#include "absl/base/internal/raw_logging.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace base {
namespace internal {

int signal_safe_open(const char* path, int flags) {
  int fd;
  ABSL_RAW_CHECK((flags & O_CREAT) == 0, "O_CREAT not supported");

  do {
    fd = open(path, flags);
  } while (fd == -1 && errno == EINTR);

  return fd;
}

int signal_safe_close(int fd) {
  int rc;

  do {
    rc = close(fd);
  } while (rc == -1 && errno == EINTR);

  return rc;
}

ssize_t signal_safe_write(int fd, const char* buf, size_t count,
                          size_t* bytes_written) {
  ssize_t rc;
  size_t total_bytes = 0;

  do {
    rc = write(fd, buf + total_bytes, count - total_bytes);
    if (rc > 0) total_bytes += rc;
  } while ((rc > 0 && count > total_bytes) || (rc == -1 && errno == EINTR));

  if (bytes_written != nullptr) *bytes_written = total_bytes;

  return rc;
}

int signal_safe_poll(struct pollfd* fds, int nfds, int timeout_ms) {
  int rc = 0;
  int elapsed_ms = 0;

  // We can't use gettimeofday since it's not async signal safe.  We could use
  // clock_gettime but that would require linking against librt.  Fortunately,
  // timeout is of sufficiently coarse granularity that we can just approximate
  // it.
  while ((elapsed_ms <= timeout_ms || timeout_ms == -1) && (rc == 0)) {
    if (elapsed_ms++ > 0) ::absl::SleepFor(::absl::Milliseconds(1));
    while ((rc = poll(fds, nfds, 0)) == -1 && errno == EINTR) {
    }
  }

  return rc;
}

ssize_t signal_safe_read(int fd, char* buf, size_t count, size_t* bytes_read) {
  ssize_t rc;
  size_t total_bytes = 0;
  struct pollfd pfd;

  // poll is required for testing whether there is any data left on fd in the
  // case of a signal interrupting a partial read.  This is needed since this
  // case is only defined to return the number of bytes read up to that point,
  // with no indication whether more could have been read (up to count).
  pfd.fd = fd;
  pfd.events = POLL_IN;
  pfd.revents = 0;

  do {
    rc = read(fd, buf + total_bytes, count - total_bytes);
    if (rc > 0) total_bytes += rc;

    if (rc == 0) break;  // EOF
    // try again if there's space to fill, no (non-interrupt) error,
    // and data is available.
  } while (total_bytes < count && (rc > 0 || errno == EINTR) &&
           (signal_safe_poll(&pfd, 1, 0) == 1 || total_bytes == 0));

  if (bytes_read) *bytes_read = total_bytes;

  if (rc != -1 || errno == EINTR)
    rc = total_bytes;  // return the cumulative bytes read
  return rc;
}

// POSIX provides the **environ array which contains environment variables in a
// linear array, terminated by a NULL string.  This array is only perturbed when
// the environment is changed (which is inherently unsafe) so it's safe to
// return a const pointer into it.
// e.g. { "SHELL=/bin/bash", "MY_ENV_VAR=1", "" }
extern "C" char** environ;
const char* thread_safe_getenv(const char* env_var) {
  int var_len = strlen(env_var);

  char** envv = environ;
  if (!envv) {
    return nullptr;
  }

  for (; *envv != nullptr; envv++)
    if (strncmp(*envv, env_var, var_len) == 0 && (*envv)[var_len] == '=')
      return *envv + var_len + 1;  // skip over the '='

  return nullptr;
}

static int char_value(char c, int base) {
  if ('0' <= c && c <= '9') {
    return c - '0';
  }

  if (base == 16 && 'a' <= c && c <= 'f') {
    return 10 + (c - 'a');
  }

  if (base == 16 && 'A' <= c && c <= 'F') {
    return 10 + (c - 'A');
  }

  return -1;
}

uint64_t signal_safe_strtou64(const char* ptr, const char** endptr, int base,
                              int* err) {
  ABSL_RAW_DCHECK(err != nullptr, "err is nullptr");
  ABSL_RAW_DCHECK(base == 10 || base == 16, "invalid base");
  if (base == 16) {
    if (*ptr != '0') {
      *err = -EINVAL;
      return 0;
    }
    ptr++;
    if (*ptr != 'x' && *ptr != 'X') {
      *err = -EINVAL;
      return 0;
    }
    ptr++;
  }

  int val = char_value(*ptr, base);
  if (val == -1) {
    *err = -EINVAL;
    return 0;
  }
  uint64_t ret = 0;
  do {
    ret *= base;
    ret += val;
    ptr++;
    val = char_value(*ptr, base);
  } while (val >= 0);

  if (endptr != nullptr) {
    *endptr = ptr;
  }
  *err = 0;
  return ret;
}

void SleepForNanoseconds(int64_t nanos) {
  static const int64_t kNanosPerSecond = 1000 * 1000 * 1000;
  struct timespec spec;
  spec.tv_sec = nanos / kNanosPerSecond;
  spec.tv_nsec = nanos - (spec.tv_sec) * kNanosPerSecond;
  do {
    if (0 == nanosleep(&spec, &spec)) {
      return;
    }
    ABSL_RAW_CHECK(errno == EINTR, "other errors should not happen");
  } while (true);
}

LineReader::LineReader(int fd, char* buf, size_t buf_size)
    : buf_(buf),
      size_(buf_size),
      start_(&buf[buf_size - 1]),
      valid_(0),
      fd_(fd),
      overwritten_('\0') {}

char* LineReader::OutputLine(char* end) {
  char* ret = start_;
  valid_ -= (end - start_) + 1;
  start_ = end + 1;
  overwritten_ = *start_;
  *start_ = '\0';
  return ret;
}

// Invariant at each call to Next(): <start> is the beginning of whatever
// we've read ahead, which goes for <valid> characters.
char* LineReader::Next(int* error) {
  *error = 0;
  // We overwrote the byte _after_ the last line with a '\0' (to make
  // it a valid C string). Undo that.
  *start_ = overwritten_;

  // Maybe we have a full line ready to go:
  char* end = static_cast<char*>(memchr(start_, '\n', valid_));
  if (end != nullptr) {
    return OutputLine(end);
  }
  // We need more data.
  if (fd_ < 0) {
    // we hit EOF last time.  Return anything there is and give up.
    if (valid_ == 0) {
      return nullptr;
    }
    return OutputLine(start_ + valid_ - 1);
  }

  memmove(buf_, start_, valid_);
  start_ = buf_;
  size_t unused = (size_ - 1) - valid_;
  size_t nread;
  ssize_t r = signal_safe_read(fd_, buf_ + valid_, unused, &nread);
  if (r < 0) {
    *error = errno;
    return nullptr;
  }
  if (nread < unused) {
    // We got all there was.
    fd_ = -1;
  }
  valid_ += nread;
  if (valid_ == 0) {
    // None left.
    return nullptr;
  }
  // Now we definitely have enough data--either there's a full line,
  // we've exhausted the buffer size, or we're at EOF.
  end = static_cast<char*>(memchr(start_, '\n', valid_));
  if (end == nullptr) {
    end = start_ + valid_ - 1;
  }
  return OutputLine(end);
}

std::vector<int> AllowedCpus() {
  // We have no need for dynamically sized sets (currently >1024 CPUs for glibc)
  // at the present time.  We could change this in the future.
  cpu_set_t allowed_cpus;
  ABSL_RAW_CHECK(sched_getaffinity(0, sizeof(allowed_cpus), &allowed_cpus) == 0,
                 "unable to read affinity mask");
  int n = CPU_COUNT(&allowed_cpus), c = 0;

  std::vector<int> result(n);
  for (int i = 0; i < CPU_SETSIZE && n; i++) {
    if (CPU_ISSET(i, &allowed_cpus)) {
      result[c++] = i;
      n--;
    }
  }
  ABSL_RAW_CHECK(0 == n, "n != 0");

  return result;
}

cpu_set_t VectorToCpuSetT(std::vector<int> mask) {
  cpu_set_t result;
  CPU_ZERO(&result);
  for (int cpu : mask) {
    CPU_SET(cpu, &result);
  }
  return result;
}

ScopedAffinityMask::ScopedAffinityMask(std::vector<int> allowed_cpus) {
  specified_cpus_ = VectorToCpuSetT(std::move(allowed_cpus));
  // getaffinity should never fail.
  ABSL_RAW_CHECK(
      sched_getaffinity(0, sizeof(original_cpus_), &original_cpus_) == 0,
      "unable to get cpu affinity");
  // See destructor comments on setaffinity interactions.  Tampered() will
  // necessarily be true in this case.
  sched_setaffinity(0, sizeof(specified_cpus_), &specified_cpus_);
}

ScopedAffinityMask::~ScopedAffinityMask() {
  // If something else has already reset our affinity, do not attempt to
  // restrict towards our original mask.  This is best-effort as the tampering
  // may obviously occur during the destruction of *this.
  if (!Tampered()) {
    // Note:  We do not assert success here, conflicts may restrict us from all
    // 'original_cpus_'.
    sched_setaffinity(0, sizeof(original_cpus_), &original_cpus_);
  }
}

bool ScopedAffinityMask::Tampered() {
  cpu_set_t current_cpus;
  ABSL_RAW_CHECK(sched_getaffinity(0, sizeof(current_cpus), &current_cpus) == 0,
                 "unable to get cpu affinity");
  return !CPU_EQUAL(&current_cpus, &specified_cpus_);  // Mismatch => modified.
}

}  // namespace internal
}  // namespace base
#endif  // __linux__
