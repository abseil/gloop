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
// Copyright (C) 1999 and onwards Google, Inc.
//

//
// Typically, these routines will all be os, and possibly processor,
// specific.  Every routine should thus be protected by ifdefs so
// that programs won't compile if these routines are run on a
// processor/OS that haven't been supported yet.
#include "gloop/base/sysinfo.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>  // following three are needed for open()
#include <time.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <atomic>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>  // for TARGET_OS_* defines
#include <mach/mach.h>
#include <mach/mach_time.h>
#if TARGET_OS_OSX
#include <libproc.h>  // for os x process info; not available on ios
#endif                // TARGET_OS_OSX
#include <sys/mman.h>
#include <sys/sysctl.h>  // how os x figures things out
#include <sys/time.h>    // for fbsd to get clockinfo
#elif defined(_WIN32)

#include <processthreadsapi.h>
#include <stringapiset.h>
#include <winbase.h>

#include <algorithm>

#include "absl/cleanup/cleanup.h"

#elif defined(__linux__)
#include <dirent.h>  // for opendir() and readdir()
#include <features.h>
#include <sys/mman.h>      // for mmap()
#include <sys/resource.h>  // for getpriority()
#include <sys/sysinfo.h>

#elif defined(__Fuchsia__)
#include <sys/mman.h>  // for mmap()
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#endif

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/sysinfo.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/log/vlog_is_on.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/resize_and_overwrite.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/auxiliary/parsed_process_stat.h"
#include "gloop/base/proc_maps.h"
#include "gloop/base/strerror.h"

#if defined _WIN32
const int kTicksPerSecond = 1000;  // 1000 is standard on Windows.
#else
const int kTicksPerSecond = 100;  // 100 is standard on Unix.
#endif

// Re-run fn until it doesn't cause EINTR.
#define NO_INTR(fn) \
  do {              \
  } while ((fn) < 0 && errno == EINTR)

// Local reimplementation of LOG_FIRST_N to avoid circular dependencies.
// TODO: replace this with `LOG_FIRST_N` when that is moved to
// Abseil, and revert some of the `VLOG` to `RAW_VLOG` and `*LOG` to
// `ABSL_INTERNAL_LOG` changes.
#define BASE_SYSINFO_LOG_FIRST_N(LEVEL, N, ...)                            \
  do {                                                                     \
    ABSL_CONST_INIT static std::atomic<uint64_t> gLocalLogFirstCount{0};   \
    if (gLocalLogFirstCount.fetch_add(1, std::memory_order_relaxed) < N) { \
      ABSL_INTERNAL_LOG(LEVEL, __VA_ARGS__);                               \
    }                                                                      \
  } while (false)

// We log when there's a problem, but only a limited number of times
// -- these functions might be called a lot, and would give the same
// error each time!
ABSL_CONST_INIT static absl::Mutex error_map_lock(absl::kConstInit);
// TODO: Determine whether std::unordered_map would outperform
// std::map.
static std::map<std::string, int>* error_map = nullptr;

// Increment the count for the error class associated with "name", and
// return the old value of the count.  We use this to avoid logging a
// particular message too often.
static int NumTimesLogged(const char* name) {
  absl::MutexLock l(error_map_lock);
  if (error_map == nullptr) error_map = new std::map<std::string, int>;
  int num_times_logged = (*error_map)[std::string(name)]++;
  return num_times_logged;
}

static std::string DescribeErr(int err) {
  return absl::StrCat(base::StrError(err), " [", err, "]");
}

#if defined __linux__
// Useful utility functions for reading things from /proc

// OpenProcFileInternal()
//
// filename is a fully completed filename (often constructed with
// ConstructFilename to combine a printf-spec with a pid).  The source
// spec is also provided in "spec", to use as an identifier when
// printing error messages.  The specified filename is opened and
// returned. returns nullptr on error.
static FILE* OpenProcFileInternal(const char* spec, const char* filename,
                                  bool log_on_error) {
  FILE* f = fopen(filename, "r");
  if (log_on_error && !f && VLOG_IS_ON(2)) {
    ABSL_RAW_LOG(INFO, "%s: Error opening file %s for reading: %s", spec,
                 filename, DescribeErr(errno).c_str());
  }
  return f;
}

FILE* OpenProcFile(const char* filespec, pid_t pid) {
  char filename[PATH_MAX];
  proc_maps_internal::ConstructFilename(filespec, pid, filename,
                                        sizeof(filename));
  return OpenProcFileInternal(filespec, filename, true);
}

static int OpenProcFd(const char* filespec, pid_t pid) {
  char filename[PATH_MAX];
  proc_maps_internal::ConstructFilename(filespec, pid, filename,
                                        sizeof(filename));
  int fd = -1;
  do {
    fd = open(filename, O_RDONLY);
  } while (fd == -1 && errno == EINTR);

  if (fd == -1 && VLOG_IS_ON(2)) {
    ABSL_RAW_LOG(INFO, "%s: Error opening file %s for reading: %s", filespec,
                 filename, DescribeErr(errno).c_str());
  }
  return fd;
}

bool ProcFileReadable(const char* filespec, pid_t pid) {
  char filename[PATH_MAX];
  proc_maps_internal::ConstructFilename(filespec, pid, filename,
                                        sizeof(filename));
  return access(filename, R_OK) == 0;
}

static bool VScanFileForKeyword(FILE* f, const char* keyword,
                                const char* format, va_list ap, bool quiet) {
  // All callers should provide a freshly-opened or properly-rewound file
  ABSL_RAW_DCHECK(0 == ftell(f), "unexpected file offset");

  constexpr int scanfspec_size = 1024;
  char scanfspec[scanfspec_size];
  if (format) {
    ABSL_INTERNAL_CHECK(snprintf(scanfspec, scanfspec_size, "%s %s\n", keyword,
                                 format) < scanfspec_size,
                        "spec was too long");
  } else {
    ABSL_INTERNAL_CHECK(
        snprintf(scanfspec, scanfspec_size, "%s%%%d[^\n]", keyword,
                 kScanfileBufsize - 1) < scanfspec_size,
        "spec was too long");
  }
  // Scan through to find the specified keyword
  char buf[kScanfileBufsize];
  while (fgets(buf, sizeof(buf), f)) {
    if (vsscanf(buf, scanfspec, ap) == 1) {
      return true;
    }
  }

  if (NumTimesLogged(keyword) < 3) {
    if (!feof(f)) {
      ABSL_INTERNAL_LOG(
          ERROR,
          absl::StrCat(keyword, ": Error while reading: ", DescribeErr(errno)));
    } else if (!quiet) {
      ABSL_INTERNAL_LOG(ERROR, absl::StrCat(keyword, ": not found"));
    }
  }
  return false;
}

bool ScanFileForKeyword(FILE* f, const char* keyword, const char* format, ...) {
  if (fseek(f, 0, SEEK_SET) == -1) {
    BASE_SYSINFO_LOG_FIRST_N(
        WARNING, 3,
        absl::StrCat("ScanFileForKeyword: rewind(): ", DescribeErr(errno)));
    return false;
  }

  va_list ap;
  va_start(ap, format);
  bool result = VScanFileForKeyword(f, keyword, format, ap, /*quiet=*/false);
  va_end(ap);
  return result;
}

static bool ReadProcKeywordInt(const char* spec, pid_t pid, const char* keyword,
                               const char* format, va_list ap, bool quiet) {
  char filename[PATH_MAX];
  proc_maps_internal::ConstructFilename(spec, pid, filename, sizeof(filename));

  FILE* f = OpenProcFileInternal(spec, filename, !quiet);
  if (!f) return false;
  bool result = VScanFileForKeyword(f, keyword, format, ap, quiet);
  fclose(f);
  return result;
}

bool ReadProcKeyword(const char* filename, pid_t pid, const char* keyword,
                     const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  bool result = ReadProcKeywordInt(filename, pid, keyword, format, ap, false);
  va_end(ap);
  return result;
}

bool ReadProcKeywordQuiet(const char* filename, pid_t pid, const char* keyword,
                          const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  bool result = ReadProcKeywordInt(filename, pid, keyword, format, ap, true);
  va_end(ap);
  return result;
}

bool ReadProcField(const char* spec, pid_t pid, int field, const char* format,
                   ...) {
  // This function does not parse COMM field in "/proc/*/stat" correctly, so
  // fail if the file passed in matches "/proc/*/stat". See <link>
  if (absl::StartsWith(spec, "/proc/") && absl::EndsWith(spec, "/stat")) {
    return false;
  }

  int fd = OpenProcFd(spec, pid);
  if (fd == -1) {
    return false;
  }

  bool success = false;
  int field_index;
  const char* p = nullptr;
  const char* limit = nullptr;
  va_list ap;
  va_start(ap, format);
  std::string file_buf;

  while (true) {
    char buf[4096];
    // We use read() rather than pread() since more recent kernels
    // have made some important /proc files non-pread()-able
    int nread = read(fd, buf, sizeof(buf));
    if (nread <= 0) {
      break;
    } else {
      file_buf.append(buf, nread);
      if (nread > 0 && static_cast<size_t>(nread) < sizeof(buf) &&
          buf[nread - 1] == '\n') {
        // Since we're only going to read a single line, and we found the
        // newline, we don't need to do another read system call
        break;
      }
    }
  }
  p = file_buf.c_str();
  limit = p + file_buf.size();
  field_index = 0;
  while (p < limit && field_index < field) {
    while (p < limit && *p == ' ') p++;
    while (p < limit && *p != ' ') p++;
    field_index++;
  }
  if (p >= limit) {
    goto out;
  }
  if (vsscanf(p, format, ap) > 0) {
    success = true;
  }
out:
  close(fd);

  va_end(ap);
  return success;
}

absl::StatusOr<base::ParsedProcessStat> ParseProcessStat(const char* filespec,
                                                         pid_t pid) {
  // An upper bound on how long a "stat" file line can be: 51 values which are
  // at most 64-bit numbers (so at most 19 digits plus 1 sign) and a string
  // which should be 16 chars max enclosed in parens (2), plus 51 spaces.
  static constexpr size_t kMaxLineLength = 51 * 20 + 16 + 2 + 51;
  std::string stat_line;
  int read_bytes =
      ReadProcFileToString(filespec, pid, kMaxLineLength, &stat_line);
  if (read_bytes < 0) {
    return absl::InternalError(absl::StrCat("Failed to read file spec ",
                                            filespec, " (pid=", pid,
                                            "): ", DescribeErr(errno)));
  }
  return base::ParsedProcessStat(std::move(stat_line));
}

namespace {

int ReadToFixedBuffer(const char* filename, pid_t pid, char* buf,
                      size_t max_size) {
  int fd = OpenProcFd(filename, pid);
  if (fd == -1) return -1;

  absl::Cleanup close_fd = [fd] { close(fd); };
  size_t total_bytes = 0;
  while (max_size > total_bytes) {
    int num_bytes = read(fd, buf, max_size - total_bytes);
    if (num_bytes == 0) break;
    if (num_bytes == -1) {
      if (errno == EINTR) continue;
      return -1;
    }
    total_bytes += num_bytes;
    buf += num_bytes;
  }

  return total_bytes;
}

}  // namespace

// Opens a /proc file and slurps the entire file into a string.  This
// comes in handy for parsing the more irregular files, like
// /proc/net/dev.  Returns the number of bytes read into the string,
// to a maximum of max_size.
int ReadProcFileToString(const char* filename, pid_t pid, size_t max_size,
                         std::string* output) {
  ABSL_INTERNAL_CHECK(output != nullptr, "output cannot be null");

  int total_bytes;
  absl::StringResizeAndOverwrite(
      *output, max_size,
      [filename, pid, &total_bytes](char* buf, size_t buf_size) {
        total_bytes = ReadToFixedBuffer(filename, pid, buf, buf_size);
        return total_bytes >= 0 ? static_cast<size_t>(total_bytes) : size_t{0};
      });
  output->shrink_to_fit();
  return total_bytes;
}

int ReadProcFileToBuffer(const char* filename, pid_t pid, size_t max_size,
                         char* buf) {
  ABSL_INTERNAL_CHECK(buf != nullptr, "buf cannot be null");
  ABSL_INTERNAL_CHECK(max_size > 0, "max_size must be > 0");

  int total_bytes = ReadToFixedBuffer(filename, pid, buf, max_size);
  if (total_bytes == -1) {
    buf[0] = '\0';
    return -1;
  }

  if (total_bytes == static_cast<int64_t>(max_size)) {
    total_bytes -= 1;
  }
  buf[total_bytes] = '\0';
  return total_bytes;
}

// Read a file from /proc, split each line at the first colon
// boundary, and use the left and right sides as key and value to
// insert into the passed ProcMap.

bool ReadProcMap(const std::string& path, ProcMap* res) {
  FILE* f = OpenProcFile(path.c_str(), -1);
  if (!f) return false;

  // Scan through to find all keywords
  char buf[kScanfileBufsize];
  while (fgets(buf, sizeof(buf), f)) {
    char* sep = strchr(buf, ':');
    if (!sep) continue;
    *sep++ = '\0';
    (*res)[buf] = sep;
  }

  fclose(f);
  return true;
}

bool CountDentries(const char* path, int* count) {
  DIR* dir = opendir(path);
  if (dir == nullptr) {
    if (VLOG_IS_ON(2)) {
      ABSL_RAW_LOG(INFO, "Error opening %s to count dir entries: %s", path,
                   DescribeErr(errno).c_str());
    }
    return false;
  }
  *count = 0;
  errno = 0;
  while (readdir(dir) != nullptr) {
    (*count)++;
  }
  bool success = (errno == 0);
  if (!success && VLOG_IS_ON(2)) {
    ABSL_RAW_LOG(INFO, "Error reading %s to count dir entries: %s", path,
                 DescribeErr(errno).c_str());
  }
  ABSL_INTERNAL_CHECK(closedir(dir) == 0,
                      "Failed to close dir after counting dentries");
  return success;
}

#endif  // __linux__

#if !defined(__native_client__)
// ----------------------------------------------------------------------
// PhysicalMem()
//    The amount of physical memory (RAM) a machine has.
//    Returns 0 if it couldn't figure out the memory.
// ----------------------------------------------------------------------

static uint64_t PhysicalMemInternal() {
#if defined(__APPLE__)
  int mib[2] = {CTL_HW, HW_MEMSIZE};
  uint64_t cchMem; /* 8 bytes supported on little endian machines */
  size_t cchMemLen = sizeof(cchMem);
  if (sysctl(mib, sizeof(mib) / sizeof(*mib), &cchMem, &cchMemLen, nullptr,
             0) != 0)
    return 0;
  else
    return cchMem;
#elif defined _WIN32
  MEMORYSTATUS stat;
  GlobalMemoryStatus(&stat);
  return stat.dwTotalPhys;
#else
  long physical_pages;
  long physical_page_size = sysconf(_SC_PAGE_SIZE);

  if (physical_page_size <= 0) {
    BASE_SYSINFO_LOG_FIRST_N(
        ERROR, 3, "PhysicalMem: Physical page size could not be obtained");
    return 0;
  }

#if defined __linux__
  long long totalK;
  ABSL_INTERNAL_CHECK(
      ReadProcKeyword("/proc/meminfo", 0, "MemTotal:", "%lld", &totalK),
      "Could not query total memory");
  physical_pages = totalK * 1024 / physical_page_size;
#else
  physical_pages = sysconf(_SC_PHYS_PAGES);
#endif

  if (physical_pages <= 0) {
    BASE_SYSINFO_LOG_FIRST_N(
        ERROR, 3,
        "PhysicalMem: Physical number of pages could not be obtained");
    return 0;
  }

  return (static_cast<uint64_t>(physical_pages)) * physical_page_size;
#endif
}

ABSL_CONST_INIT static absl::Mutex physical_mem_lock(absl::kConstInit);
static uint64_t physical_mem = 0;
static bool physical_mem_initialized = false;

uint64_t PhysicalMem() {
  absl::MutexLock l(physical_mem_lock);
  if (!physical_mem_initialized) {
    physical_mem = PhysicalMemInternal();
    physical_mem_initialized = true;
  }
  return physical_mem;
}
#endif  // defined(__native_client__)

// This doesn't seem possible on windows
#if !defined(_WIN32)
// ----------------------------------------------------------------------
// MaxVMArea()
//    Returns the largest single available VM area. Watch out for race
//    conditions
// ----------------------------------------------------------------------
int64_t MaxVMArea() {
  int64_t physical_page_size = sysconf(_SC_PAGE_SIZE);
  int64_t max_size_found = 0;

  int64_t delta;

#ifdef _LP64
  // Not << 63 or we go negative ...
  delta = uint64_t{1} << 62;
#else
  delta = uint64_t{1} << 31;
#endif

  // Use a binary search to find the largest available area
  while (delta >= physical_page_size) {
    int64_t test_size = max_size_found + delta;

    void* addr =
        mmap(nullptr, test_size, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);

    if (addr != MAP_FAILED) {
      max_size_found = test_size;
      munmap(addr, test_size);
    }

    delta >>= 1;
  }

  ABSL_INTERNAL_LOG(INFO, absl::StrCat("Max VM Area = ", max_size_found));

  return max_size_found;
}

#endif  // not defined _WIN32

// The amount of physical memory not used by the OS (can be greater than what
// can be mapped into a single process, i.e. can be greater than 4 GB).
uint64_t NonKernelMem64() {
  uint64_t non_kernel_mem = 0;

#ifdef __USE_LARGEFILE64
#if defined __linux__
  // attempt to open the 64-bit "membus" device
  int fd;
  NO_INTR(fd = open("/dev/membus", O_RDONLY));

  if (fd >= 0) {
    // /dev/membus is available, so determine amount of non-kernel memory
    //
    // NOTE: /dev/membus *only* accesses non-kernel memory, so there is
    // no need to subtract out PhysicalMem() (the memory managed by the kernel)
    non_kernel_mem = lseek64(fd, 0, SEEK_END);
    close(fd);
  }

  // else fall through to default return value
#endif  // __linux__
#endif  // __USE_LARGEFILE64

  return non_kernel_mem;
}

// ----------------------------------------------------------------------
// VirtualProcessSize()
//    The size of this process. Returns 0 if it couldn't figure out
//    the process size. On linux, we get this info from /proc/self/stat
//    TODO: As of Mar 5th, 2003, getrusage does not work on either 2.2 or
//    2.4 kernels. Once we have a google patch to fix getrusage, change
//    this to use getrusage instead of accessing proc
// ----------------------------------------------------------------------

int64_t VirtualProcessSize() {
#if defined __linux__
  return VirtualMemorySize(0);
#else
  BASE_SYSINFO_LOG_FIRST_N(ERROR, 3, "VirtualProcessSize(): OS not supported");
  return 0;
#endif
}

// ----------------------------------------------------------------------
// VirtualProcessSizeForExport()
//    The size of this process, like VirtualProcessSize(), but caches the size
//    for 5 minutes, thus is usable for exported metrics and logging.
// ----------------------------------------------------------------------

int64_t VirtualProcessSizeForExport() {
  static const time_t kMeasurementPeriod = 300;  // seconds
  static time_t when_measured = 0;
  static int64_t vps = 0;
  ABSL_CONST_INIT static absl::Mutex when_measured_lock(absl::kConstInit);

  time_t now = 0;
  time(&now);

  absl::MutexLock l(when_measured_lock);
  if ((now - when_measured) >= kMeasurementPeriod) {
    vps = VirtualProcessSize();
    when_measured = now;
  }

  return vps;
}

// ----------------------------------------------------------------------
// GetMemoryStats()
//   The relatively complete memory stats of a process.  This is not
//   free, so don't call it in your inner loops.
// ----------------------------------------------------------------------
bool GetMemoryStats(pid_t pid, base::MemoryStats* mem_stats) {
#if defined __linux__
  static int page_size = getpagesize();

  // Fields in /proc/self/statm:
  //  [0] = vsize
  //  [1] = rss
  //  [2] = shared
  //  [3] = code
  //  [4] = unused
  //  [5] = data + stack
  //  [6] = unused
  long long vsize, rss, shared, code, data, unused;  // NOLINT
  if (ReadProcField("/proc/%d/statm", pid, 0, "%lld %lld %lld %lld %lld %lld",
                    &vsize, &rss, &shared, &code, &unused, &data)) {
    mem_stats->vsize = vsize * page_size;
    mem_stats->rss = rss * page_size;
    mem_stats->shared = shared * page_size;
    mem_stats->code = code * page_size;
    mem_stats->data = data * page_size;
    return true;
  }
#else
  BASE_SYSINFO_LOG_FIRST_N(ERROR, 3, "GetMemoryStats(): OS not supported");
#endif
  return false;
}

// ----------------------------------------------------------------------
// GetMemoryStatsForExport()
//   The relatively complete memory stats of this process, like
//   GetMemoryStats().  This is not a free operation, so this function
//   only actually gets the value if more that ttl seconds have elapsed
//   since the last call, and caches the value for the rest of the calls.
// ----------------------------------------------------------------------
bool GetMemoryStatsForExport(base::MemoryStats* mem_stats, int ttl) {
  static time_t when_measured = 0;
  static base::MemoryStats saved_stats = {0, 0, 0, 0, 0};

  time_t now = 0;
  time(&now);

  if ((now - when_measured) >= ttl) {
    if (GetMemoryStats(getpid(), &saved_stats) == false) {
      return false;
    }
    when_measured = now;
  }

  *mem_stats = saved_stats;
  return true;
}

// ----------------------------------------------------------------------
// GetProcessThreadCount()
//   Returns the number of threads in a process, as understood by the OS.
//   Returns -1 on failure or if not supported.
// ----------------------------------------------------------------------
int GetProcessThreadCount(pid_t pid) {
#if defined __linux__
  absl::StatusOr<base::ParsedProcessStat> parsed =
      ParseProcessStat("/proc/%d/stat", pid);
  if (!parsed.ok()) {
    return -1;
  }

  auto thread_count = parsed->GetSignedIntField(19);

  return thread_count.value_or(-1);
#else
  BASE_SYSINFO_LOG_FIRST_N(ERROR, 3,
                           "GetProcessThreadCount(): OS not supported");
  return -1;
#endif
}

// ----------------------------------------------------------------------
// SystemLoadAverageForTimeRange()
//    Returns the system load average over the last one, five, or fifteen
//    minutes.  Returns 0 if it couldn't figure out the CPU load.  On Linux,
//    we get this info from sysinfo(2).  The sysinfo.loads values return
//    integers with 16 bits of precision for the integer and 16 bits for the
//    fraction.  See fs/proc/array.c (2.2 kernel) and fs/proc/proc_misc.c
//    (2.4 kernel) for the /proc/loadavg interface.  We call sysinfo
//    directly so that this routine works even if /proc is not mounted
//    (chrooted servers).
//
//    sysinfo.loads array contains the one/five/fifteen minute load averages
//    These are an unsigned longs with 16 bits for the integer and 16 bits
//    for the fraction. (Unlike in the kernel, which uses 11 bits.)
//    So, we right shift 16 bits to obtain the integer. We mask off
//    the low 16 bits, multiply by 100 (see the kernel for details) and
//    then right shift 16 bits to obtain the fraction.
// ----------------------------------------------------------------------

double SystemLoadAverageForTimeRange(SystemLoadAverageTimeRange which) {
#if defined __linux__
  struct sysinfo loadavg;

#define FSHIFT 16
#define LOAD_INT(x) ((x) >> FSHIFT)
#define LOAD_FRAC(x) LOAD_INT(((x) & ((1 << FSHIFT) - 1)) * 100)

  if (sysinfo(&loadavg) == 0) {
    unsigned long load1 = loadavg.loads[which];
    double usage = LOAD_INT(load1) + LOAD_FRAC(load1) / 100.0;
    return usage;
  } else {
    BASE_SYSINFO_LOG_FIRST_N(
        ERROR, 3,
        absl::StrCat("SystemLoadAverageForTimeRange(): sysinfo(): ",
                     DescribeErr(errno)));
    return 0.0;
  }
#else
  BASE_SYSINFO_LOG_FIRST_N(ERROR, 3,
                           "SystemLoadAverageForTimeRange(): OS not supported");
  return 0.0;
#endif
}

// ----------------------------------------------------------------------
// SystemLoadAverage()
//   Returns the system load average over the last minute.
//   See SystemLoadAverageForTimeRange() above
// ----------------------------------------------------------------------

double SystemLoadAverage() {
  return SystemLoadAverageForTimeRange(SYSTEM_LOAD_1MIN);
}

// ----------------------------------------------------------------------
// BootTime()
//    Return time_t of last reboot.  Returns 0 if we can't figure it out.
// ----------------------------------------------------------------------

time_t BootTime() {
// b/37140047: Android selinux blocks access to /proc/stat
// TODO: investigate using /proc/uptime as an alternative source.
#if defined __linux__ && !defined(__ANDROID__)
  static std::atomic<time_t> btime = 0;
  time_t val = btime.load(std::memory_order_relaxed);
  if (val != 0) return val;

  bool success = ReadProcKeyword("/proc/stat", -1, "btime", "%ld", &val);
  if (!success) return 0;

  // Because all boot times for a single process will be the same, a blind
  // write is safe.
  btime.store(val, std::memory_order_relaxed);
  return val;
#elif defined(__APPLE__)
  int mib[2] = {CTL_KERN, KERN_BOOTTIME};
  struct timeval btime;
  size_t btime_len = sizeof(btime);
  if (sysctl(mib, ABSL_ARRAYSIZE(mib), &btime, &btime_len, nullptr, 0) != 0) {
    BASE_SYSINFO_LOG_FIRST_N(
        ERROR, 3,
        absl::StrCat("BootTime(): sysctl kern.boottime failed: ",
                     DescribeErr(errno)));
    return 0;
  }
  return btime.tv_sec;
#else
  BASE_SYSINFO_LOG_FIRST_N(ERROR, 3, "BootTime(): OS not supported");
  return 0;
#endif
}

// ----------------------------------------------------------------------
// FreeMem()
//    Returns the amount of free memory including buffers and file-backed page.
//    Returns -1 on error. We get the information from /proc/meminfo
// ----------------------------------------------------------------------

int64_t FreeMem() {
#if defined __linux__
  FILE* f = OpenProcFile("/proc/meminfo", 0);
  if (!f) {
    return -1;
  }

  int64_t mem = 0;

  long long line;  // NOLINT

  const char* keywords[] = {
      "MemFree:", "Active(file):", "Inactive(file):", "Buffers:"};

  for (const char* keyword : keywords) {
    if (ScanFileForKeyword(f, keyword, "%lld", &line)) {
      mem += line;
    } else {
      // A line was missing or different from how we expected it - bad!
      fclose(f);
      return -1;
    }
  }

  fclose(f);
  return mem * 1024;
#else
  BASE_SYSINFO_LOG_FIRST_N(ERROR, 3, "FreeMem(): OS not supported");
  return -1;
#endif
}

// ----------------------------------------------------------------------
// MemoryUsage()
//    Returns the real memory usage (resident size) of a process.
//    Returns -1 on error (e.g. if the process doesn't exist).
//    We get this information from /proc/PID/statm
// ----------------------------------------------------------------------

int64_t MemoryUsage(pid_t pid) {
#if defined __linux__
  long long rss;  // NOLINT
  if (ReadProcField("/proc/%d/statm", pid, 1, "%lld", &rss)) {
    return rss * getpagesize();
  } else {
    return -1;
  }
#else
  BASE_SYSINFO_LOG_FIRST_N(ERROR, 3, "MemoryUsage(): OS not supported");
  return -1;
#endif
}

// ----------------------------------------------------------------------
// VirtualMemorySize()
//    Returns the virtual memory size of process with id 'pid'.
//    Returns -1 on error (e.g. if the process doesn't exist).
//    We get this information from /proc/PID/statm
// ----------------------------------------------------------------------

int64_t VirtualMemorySize(pid_t pid) {
#if defined __linux__
  long long vsize;  // NOLINT
  if (ReadProcField("/proc/%d/statm", pid, 0, "%lld", &vsize)) {
    return vsize * getpagesize();
  } else {
    return -1;
  }
#else
  BASE_SYSINFO_LOG_FIRST_N(ERROR, 3, "VirtualMemorySize(): OS not supported");
  return -1;
#endif
}

// ----------------------------------------------------------------------
// CommandLine()
//    Returns the command line of a process. The string is stored in 'buf'
//    and its length is returned. Returns 0 on error (e.g. if the process
//    doesn't exist). We get this information from /proc/PID/cmdline or
//    from /proc/self/cmdline if PID is 0.
// ----------------------------------------------------------------------

size_t CommandLine(pid_t pid, char* buf, int len) {
#if defined __linux__
  FILE* f = OpenProcFile("/proc/%d/cmdline", pid);
  if (f == nullptr) {
    return 0;
  }

  size_t read = fread(buf, 1, len, f);

  if (ferror(f)) {
    BASE_SYSINFO_LOG_FIRST_N(
        ERROR, 3,
        absl::StrFormat("/proc/%d/cmdline: error reading command line", pid));
    read = 0;
    fclose(f);
  } else {
    fclose(f);
  }
  return read;
#else
  BASE_SYSINFO_LOG_FIRST_N(ERROR, 3, "CommandLine(): OS not supported");
  return 0;
#endif
}

// --------------------------------------------------------------------
// MemoryUsageForExport
//   Returns the memory usage like MemoryUsage(pid_t). We would like to
//   export the memory usage as an exported variable so as to enable GEMS
//   to graph this over time (and thus help identify memory leaks). This
//   is useful for processes like the indexserver where VirtualMemorySize()
//   is different from MemoryUsage(). The concentrator polls processes
//   for exported variables every 15 seconds. However, we compute this
//   variable once every kMemUsageMeasurementPeriod seconds. This also
//   provides an interface friendly to exported variables to access the
//   memory usage information without having to pass in a process id.
// --------------------------------------------------------------------
int64_t MemoryUsageForExport() {
  static const time_t kMemUsageMeasurementPeriod = 300;  // seconds
  static time_t when_memory_usage_last_measured = 0;
  static int64_t mem_usage = 0;
  ABSL_CONST_INIT static absl::Mutex mem_usage_lock(absl::kConstInit);

  time_t now = 0;
  time(&now);

  absl::MutexLock l(mem_usage_lock);
  if ((now - when_memory_usage_last_measured) >= kMemUsageMeasurementPeriod) {
    mem_usage = UncachedMemoryUsageForExport();
    when_memory_usage_last_measured = now;
  }

  return mem_usage;
}

// --------------------------------------------------------------------
// UncachedMemoryUsageForExport
//   As above, but does no caching.  We export this to find processes
//   whose memory usage spikes quickly, just before they crash.  To
//   avoid the overhead that the caching was added to fix, this is a
//   hidden variable (see stats/io/internal/expvar_global.cc).
// --------------------------------------------------------------------
int64_t UncachedMemoryUsageForExport() {
  return MemoryUsage(static_cast<pid_t>(0));
}

// ----------------------------------------------------------------------
// ProcessList()
//    Returns a list of pids for all processes on the system.
//    Returns false on error. We use /proc to produce the list.
// ----------------------------------------------------------------------

bool ProcessList(std::vector<pid_t>* list) {
#if defined __linux__
  char dirname[PATH_MAX];
  proc_maps_internal::ConstructFilename("/proc", -1, dirname, sizeof(dirname));
  DIR* dir = opendir(dirname);
  ABSL_INTERNAL_CHECK(dir, absl::StrCat("Could not open ", dirname));

  // read process ids from /proc/*
  while (true) {
    struct dirent tmp;
    struct dirent* ent;
    int err;
    NO_INTR(err = readdir_r(dir, &tmp, &ent));
    if (err != 0) {
      BASE_SYSINFO_LOG_FIRST_N(
          WARNING, 3,
          absl::StrCat("ProcessList: readdir_r failed: ", DescribeErr(errno)));
      break;
    }
    if (ent == nullptr) {
      break;
    }
    char* endptr;
    pid_t pid = strtol(ent->d_name, &endptr, 10);
    if (*endptr == '\0')  // valid process id
      list->push_back(pid);
  }
  closedir(dir);
  return true;  // success
#else
  BASE_SYSINFO_LOG_FIRST_N(ERROR, 3, "ProcessList: OS not supported");
  return false;
#endif
}

// ----------------------------------------------------------------------
// ProcessGroup()
//     Returns the process group id for a process, 0 on error (e.g.
//     if the process does not exist)
// ----------------------------------------------------------------------

pid_t ProcessGroup(pid_t pid) {
#if defined(__linux__) && !defined(__ANDROID__)  // or any SVr4 system ...
  // If we are testing, getpgid won't work since the process
  // isn't really on this machine.  But we can get the same
  // info out of the emulated /proc.
  if (proc_maps_internal::HasProcfsPrefix()) {
    absl::StatusOr<base::ParsedProcessStat> parsed =
        ParseProcessStat("/proc/%d/stat", pid);
    if (!parsed.ok()) {
      return 0;
    }

    auto pgid = parsed->GetSignedIntField(4);

    return pgid.value_or(0);
  }
  pid_t pgid = getpgid(pid);
  if (pgid == -1) {
    BASE_SYSINFO_LOG_FIRST_N(ERROR, 3,
                             absl::StrCat("ProcessGroup(): getpgid(", pid,
                                          ") failed: ", DescribeErr(errno)));
    return 0;
  } else {
    return pgid;
  }
#else
  BASE_SYSINFO_LOG_FIRST_N(ERROR, 3, "ProcessGroup(): OS not supported");
  return 0;
#endif
}

#if defined _WIN32
// Finds the process entry, storing it in process_entry.
// If the process isn't found, this returns false and process_entry will have an
// unknown value inside.
static bool FindProcess(pid_t pid, PROCESSENTRY32W* process_entry) {
  // Win32 doesn't support looking at one process, so snapshot all processes.
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return 0;
  }

  bool found = false;
  if (Process32FirstW(snapshot, process_entry)) {
    do {
      // Look for the process matching the queried process.
      if (process_entry->th32ProcessID == pid) {
        found = true;
        break;
      }
    } while (Process32NextW(snapshot, process_entry));
  }
  CloseHandle(snapshot);
  return found;
}
#endif

// ----------------------------------------------------------------------
// ProcessParent()
//     Returns the parent process id for a process, 0 on error (e.g.
//     if the process does not exist)
// ----------------------------------------------------------------------
pid_t ProcessParent(pid_t pid) {
#if defined __linux__
  pid_t parent = 0;
  if (ReadProcKeyword("/proc/%d/status", pid, "PPid:", " %d", &parent)) {
    return parent;
  } else {
    return 0;
  }
#elif defined _WIN32
  PROCESSENTRY32W process_entry;
  if (FindProcess(pid, &process_entry)) {
    return process_entry.th32ParentProcessID;
  } else {
    return 0;
  }
#else
  BASE_SYSINFO_LOG_FIRST_N(ERROR, 3, "ProcessParent(): OS not supported");
  return 0;
#endif
}

// ----------------------------------------------------------------------
// ProcessExePath()
//     Returns the path to the binary for a process, or "" on error
// ----------------------------------------------------------------------
std::string ProcessExePath(pid_t pid) {
#if defined __linux__
  char proc_pid_exe[PATH_MAX];
  proc_maps_internal::ConstructFilename("/proc/%d/exe", pid, proc_pid_exe,
                                        sizeof(proc_pid_exe));

  // readlink() does not NUL-terminate, but it does tell us how many bytes
  // it returned.
  char buf[PATH_MAX + 1];
  ssize_t result = readlink(proc_pid_exe, buf, sizeof(buf) - 1);
  if (result > 0) {
    ABSL_INTERNAL_CHECK(static_cast<size_t>(result) < sizeof(buf),
                        "buffer too small");
    buf[result] = '\0';
    return buf;
  }
  ABSL_INTERNAL_LOG(ERROR, absl::StrCat("readlink(\"", proc_pid_exe,
                                        "\"): ", DescribeErr(errno)));
#else
  BASE_SYSINFO_LOG_FIRST_N(ERROR, 3, "ProcessExePath(): OS not supported");
#endif
  return "";
}

// ----------------------------------------------------------------------
// Nice()
//   Returns the nice level that the process is currently running at, or
//   -INT_MAX on error.
// ----------------------------------------------------------------------

int Nice() {
#if !defined(_WIN32)
  // Note: We can't use nice() here, since it always returns 0 due to a
  // problem with the version of glibc we run in production.
  errno = 0;
  int niceval = getpriority(PRIO_PROCESS, 0);
  if (niceval == -1 && errno != 0) {
    BASE_SYSINFO_LOG_FIRST_N(
        ERROR, 3, absl::StrCat("Nice(): getpriority(): ", DescribeErr(errno)));
    return -INT_MAX;
  }
  return niceval;
#else
  BASE_SYSINFO_LOG_FIRST_N(ERROR, 3, "Nice(): OS not supported");
  return -INT_MAX;
#endif
}

// ----------------------------------------------------------------------
// ThreadGroup()
//     Returns the thread group id for a process, 0 on error (e.g.
//     if the process does not exist or the kernel does not support this)
// ----------------------------------------------------------------------
pid_t ThreadGroup(pid_t pid) {
#if defined __linux__
  int result = 0;
  if (ReadProcKeyword("/proc/%d/status", pid, "Tgid:", " %d", &result)) {
    return result;
  } else {
    return 0;
  }
#else
  BASE_SYSINFO_LOG_FIRST_N(ERROR, 3, "ThreadGroup(): OS not supported");
  return 0;
#endif
}

// ----------------------------------------------------------------------
// ProcessName()
//     Returns the name of a process, empty string on error (e.g.
//     if the process does not exist or the OS does not support this)
// ----------------------------------------------------------------------
std::string ProcessName(pid_t pid) {
#if defined __linux__
  char filename[PATH_MAX];
  proc_maps_internal::ConstructFilename("/proc/%d/comm", pid, filename,
                                        sizeof(filename));
  int fd;
  NO_INTR(fd = open(filename, O_RDONLY));
  if (fd == -1) {
    return {};
  }
  char buf[64];
  int len;
  NO_INTR(len = read(fd, buf, sizeof(buf)));
  close(fd);
  if (len == -1) {
    return {};
  }
  if (len > 0 && buf[len - 1] == '\n') len--;
  return std::string(buf, len);
#elif defined _WIN32
  HANDLE process_handle;
  DWORD process_id;
  if (pid == 0) {
    process_id = GetCurrentProcessId();
  } else {
    process_id = pid;
  }
  process_handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                               /*bInheritHandle=*/FALSE, process_id);
  if (!process_handle) {
    // Failed to open a handle to the target process.
    BASE_SYSINFO_LOG_FIRST_N(
        ERROR, 3,
        absl::StrCat("ProcessName(): Couldn't open handle to pid: ", pid,
                     " error: ", GetLastError()));
    return "";
  }
  absl::Cleanup process_handle_closer = [process_handle] {
    CloseHandle(process_handle);
  };
  // There's no way to know a priori the length of the current process's
  // filename, so follow the guidelines here (typically up to MAX_PATH, but can
  // be "approximately" 32767 when expanded at runtime).
  // https://docs.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation
  DWORD len = MAX_PATH;
  static constexpr DWORD max_len = 64738;
  std::vector<WCHAR> process_path_utf16;
  while (true) {
    process_path_utf16.resize(len);
    BOOL result = QueryFullProcessImageNameW(process_handle, /*dwFlags=*/0,
                                             process_path_utf16.data(), &len);
    if (result) {
      // Success. Trim the buffer (which does not include the trailing NUL) and
      // stop.
      process_path_utf16.resize(len);
      break;
    }
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
      BASE_SYSINFO_LOG_FIRST_N(
          ERROR, 3,
          absl::StrCat("ProcessName(): QueryFullProcessImageNameW(",
                       reinterpret_cast<intptr_t>(process_handle),
                       ") returned error: ", GetLastError()));
      return "";
    }
    // Ensure the loop ends long before integer overflow.
    if (len > max_len) {
      return "";
    }
    len *= 2;
  }
  // `process_path_utf16` includes the drive name and full path, so
  // ignore everything before the last `\`.
  auto iter =
      std::find(process_path_utf16.rbegin(), process_path_utf16.rend(), '\\');
  const WCHAR* process_filename_utf16;
  if (iter == process_path_utf16.rend()) {
    // No backslash found. This shouldn't happen, but use the whole buffer in
    // this case.
    process_filename_utf16 = process_path_utf16.data();
  } else if (iter == process_path_utf16.rbegin()) {
    // String ended in a `\`, which would indicate that the process name is
    // empty (this won't happen in practice, but avoid UB in this case).
    return "";
  } else {
    // Move past the last `\`.
    iter--;
    process_filename_utf16 = &(*iter);
  }
  const int process_filename_utf16_length =
      iter - process_path_utf16.rbegin() + 1;
  std::string result;
  // Each UTF-16 code unit is encoded in 1 to 3 bytes of UTF-8 (UTF-16 surrogate
  // pairs map 2 UTF-16 code units to 4 bytes of UTF-8, so 2 bytes of UTF-8 for
  // each surrogate code unit), so multiply by 3 for the worst case.
  const int max_utf8_len = process_path_utf16.size() * 3;
  result.resize(max_utf8_len);
  const int utf8_len = WideCharToMultiByte(
      CP_UTF8, /*dwFlags=*/0, process_filename_utf16,
      process_filename_utf16_length, result.data(), max_utf8_len,
      /*lpDefaultChar=*/nullptr,
      /*lpUsedDefaultChar=*/nullptr);
  if (!utf8_len) {
    BASE_SYSINFO_LOG_FIRST_N(
        ERROR, 3,
        absl::StrCat("ProcessName(): WideCharToMultiByte() returned error: ",
                     GetLastError()));
    return "";
  }
  // When the input string length is explicitly specified (e.g., not -1),
  // WideCharToMultiByte() does not NUL-terminate, so no need to subtract 1
  // here.
  result.resize(utf8_len);
  return result;
#elif defined(__APPLE__) && TARGET_OS_OSX
  if (!pid) {
    pid = getpid();
  }
  char buf[sizeof(proc_bsdinfo::pbi_name)];
  if (proc_name(pid, buf, sizeof(buf)) > 0) {
    return buf;
  } else if (errno == EPERM) {
    // Non-privileged processes cannot read other users' process names using
    // proc_name(). Fall back to PROC_PIDT_SHORTBSDINFO flavor of proc_pidinfo,
    // which doesn't require privileges, but truncates the name to 15
    // characters.
    struct proc_bsdshortinfo info;
    if (proc_pidinfo(pid, PROC_PIDT_SHORTBSDINFO, 0, &info, sizeof(info)) ==
        sizeof(info)) {
      return info.pbsi_comm;
    }
  }
  return "";
#elif defined(__Fuchsia__)
  if (pid != 0) {
    BASE_SYSINFO_LOG_FIRST_N(
        ERROR, 3,
        "ProcessName(): Fuchsia only supports getting the process name of "
        "the current process through this API.");
    return "";
  }
  char out_name[ZX_MAX_NAME_LEN];
  zx_status_t status = zx_object_get_property(zx_process_self(), ZX_PROP_NAME,
                                              out_name, ZX_MAX_NAME_LEN);
  if (status != ZX_OK) {
    BASE_SYSINFO_LOG_FIRST_N(
        ERROR, 3,
        absl::StrCat("object_get_property failed with status: ", status));
    return "";
  } else {
    return out_name;
  }
#else
  BASE_SYSINFO_LOG_FIRST_N(ERROR, 3, "ProcessName(): OS not supported");
  return "";
#endif
}

bool ProcessPageFaults(pid_t pid, int64_t* minor_faults, int64_t* cminor_faults,
                       int64_t* major_faults, int64_t* cmajor_faults) {
#if defined __linux__
  absl::StatusOr<base::ParsedProcessStat> parsed =
      ParseProcessStat("/proc/%d/stat", pid);
  if (!parsed.ok()) {
    return false;
  }

  auto local_minor_faults = parsed->GetUnsignedIntField(9);
  auto local_cminor_faults = parsed->GetUnsignedIntField(10);
  auto local_major_faults = parsed->GetUnsignedIntField(11);
  auto local_cmajor_faults = parsed->GetUnsignedIntField(12);

  if (!local_minor_faults.ok() || !local_cminor_faults.ok() ||
      !local_major_faults.ok() || !local_cmajor_faults.ok()) {
    return false;
  }

  *minor_faults = static_cast<int64_t>(*local_minor_faults);
  *cminor_faults = static_cast<int64_t>(*local_cminor_faults);
  *major_faults = static_cast<int64_t>(*local_major_faults);
  *cmajor_faults = static_cast<int64_t>(*local_cmajor_faults);

  return true;
#else
  BASE_SYSINFO_LOG_FIRST_N(ERROR, 3, "ProcessPageFaults(): OS not supported");
  return false;
#endif
}

#if defined __linux__
ABSL_CONST_INIT static absl::Mutex jiffie_lock(absl::kConstInit);
static double jiffie_to_sec = -1;

// Compute number of clock ticks per second and the conversion factor to secs
static double JiffiesToSec() {
  absl::MutexLock l(jiffie_lock);
  if (jiffie_to_sec < 0) {
    int clock_hz = sysconf(_SC_CLK_TCK);
    if (clock_hz <= 0) {
      ABSL_INTERNAL_LOG(
          ERROR, absl::StrCat("sysconf(_SC_CLK_TCK): ", DescribeErr(errno)));
      clock_hz = kTicksPerSecond;
    } else if (clock_hz != kTicksPerSecond) {
      // Unexpected
      ABSL_INTERNAL_LOG(WARNING, absl::StrCat("Found clock_hz == ", clock_hz,
                                              ", expected ", kTicksPerSecond));
    }
    jiffie_to_sec = 1.0 / static_cast<double>(clock_hz);
  }
  return jiffie_to_sec;
}
#endif

namespace base {

#if defined(__linux__) || defined(__APPLE__)
static absl::Duration CPUUsageClock(clockid_t clock) {
  struct timespec spec;
  int ret = clock_gettime(clock, &spec);
  if (ret != 0) {
    int err = errno;
    if (err != EINVAL) {
      // Something is very, very wrong.  No other errors are plausible.
      ABSL_RAW_LOG(ERROR, "Got bad error code from clock_gettime: %d", err);
    }

    // Clock invalid somehow.  Most likely: another process that no
    // longer exists.
    return absl::ZeroDuration();
  }
  return absl::DurationFromTimespec(spec);
}
#endif

absl::Duration CPUUsage(pid_t pid) {
#if defined(__linux__) && !defined(__ANDROID__)
  clockid_t clock;
  if (0 != clock_getcpuclockid(pid, &clock)) {
    // two possibilities:
    // - <pid> doesn't exist/exited (likely)
    // - we're on a weird system without clocks (highly unlikely)
    // not much of a way to disambiguate.  Give up.
    return absl::ZeroDuration();
  }
  return CPUUsageClock(clock);
#else
  ABSL_RAW_LOG(ERROR, "OS not supported");
  return absl::ZeroDuration();
#endif
}

absl::Duration CPUUsage() {
#if defined(__linux__) || defined(__APPLE__)
  // This clock ID includes both user and system time, according to the man
  // pages for timer_create(2) on Linux and clock_gettime(3) on macOS.
  return CPUUsageClock(CLOCK_PROCESS_CPUTIME_ID);
#else
  ABSL_RAW_LOG(ERROR, "OS not supported");
  return absl::ZeroDuration();
#endif
}

absl::Duration ThreadCPUUsage() {
#if defined(__linux__) || defined(__APPLE__)
  // This clock ID includes both user and system time, according to the man
  // pages for timer_create(2) on Linux and clock_gettime(3) on macOS.
  return CPUUsageClock(CLOCK_THREAD_CPUTIME_ID);
#else
  ABSL_RAW_LOG(ERROR, "OS not supported");
  return absl::ZeroDuration();
#endif
}

#ifdef __linux__
static absl::Duration ReapedCPUFromRUsage() {
  struct rusage ru;
  if (getrusage(RUSAGE_CHILDREN, &ru) != 0) {
    ABSL_RAW_LOG(ERROR, "getrusage failed: %d", errno);
    return absl::ZeroDuration();
  }
  return absl::DurationFromTimeval(ru.ru_utime) +
         absl::DurationFromTimeval(ru.ru_stime);
}
#endif

absl::Duration ReapedCPUUsage() {
#ifdef __linux__
  // TODO: once we have a clockid for this, use clock_gettime
  // where available; it'll be more accurate.
  return ReapedCPUFromRUsage();
#else
  ABSL_RAW_LOG(ERROR, "OS not supported");
  return absl::ZeroDuration();
#endif
}

absl::Time ProcessStartTime() {
#if defined(__linux__) && defined(CLOCK_BOOTTIME)
  // CLOCK_BOOTTIME is available since 2.6.39
  double jiffies_to_sec = JiffiesToSec();

  absl::StatusOr<base::ParsedProcessStat> parsed =
      ParseProcessStat("/proc/%d/stat", 0);
  if (!parsed.ok()) {
    return absl::InfinitePast();
  }

  auto start_time = parsed->GetUnsignedIntField(21);

  if (!start_time.ok()) {
    return absl::InfinitePast();
  }

  return absl::Now() - CPUUsageClock(CLOCK_BOOTTIME) +
         absl::Seconds(*start_time * jiffies_to_sec);
#elif defined(__APPLE__) && TARGET_OS_OSX
  struct proc_bsdinfo proc_info;
  int result = proc_pidinfo(getpid(), PROC_PIDTBSDINFO, 0, &proc_info,
                            PROC_PIDTBSDINFO_SIZE);
  if (result != PROC_PIDTBSDINFO_SIZE) {
    // On error, proc_pidinfo should return 0 and set errno.
    ABSL_RAW_LOG(ERROR,
                 "Unexpected return value from proc_pidinfo: %d, errno: %d",
                 result, errno);
    return absl::InfinitePast();
  } else {
    return absl::FromUnixSeconds(proc_info.pbi_start_tvsec) +
           absl::Microseconds(proc_info.pbi_start_tvusec);
  }
#else
  ABSL_RAW_LOG(ERROR, "OS not supported");
  return absl::InfinitePast();
#endif
}

int AvailableCPUs() {
  // The public API specifies that the result may change between calls.
  // That's currently not the case, but is not precluded.
  static const int nproc = []() -> int {
    if (const char* e = getenv("NPROC")) {
      if (int n = strtol(e, nullptr, 10); n > 0) return n;
    }
    return absl::base_internal::NumCPUs();
  }();
  return nproc;
}

bool IsSeccompEnabled() {
#if defined __linux__
  // Disabled value for the `Seccomp` line. See `man proc`, under
  // `/proc/pid/status`.
  static constexpr int kSeccompDisabled = 0;

  FILE* status_file = OpenProcFileInternal(
      "/proc/self/status", "/proc/self/status", /*log_on_error=*/false);
  if (status_file == nullptr) {
    // We most likely have a seccomp which does not allow us to read the file.
    return true;
  }
  absl::Cleanup close_status_file = [status_file] { fclose(status_file); };

  int seccomp;
  bool found_seccomp_line = [](FILE* f, ...) {
    va_list ap;
    va_start(ap, f);
    bool r = VScanFileForKeyword(f, "Seccomp:", " %d", ap, /*quiet=*/true);
    va_end(ap);
    return r;
  }(status_file, &seccomp);
  // If there's no "Seccomp" line, seccomp is not enabled.
  if (!found_seccomp_line) return false;

  return seccomp != kSeccompDisabled;
#else
  return false;
#endif
}

}  // namespace base

static const absl::Time* start_up_time = nullptr;

namespace base {
namespace internal {

void StartUpWallTimer() {
  assert(!start_up_time);
  start_up_time = new absl::Time(absl::Now());
}

bool AreAdaptiveTicksEnabled() {
#if defined __linux__
  // Adaptive tick mode is specified via `nohz_full=` in the kernel
  // configuration (/proc/cmdline).

  // https://docs.kernel.org/admin-guide/kernel-parameters.html
  // > The number of kernel parameters is not limited, but the length of the
  // > complete command line (parameters including spaces etc.) is limited to a
  // > fixed number of characters. This limit depends on the architecture and is
  // > between 256 and 4096 characters. It is defined in the file
  // > ./include/uapi/asm-generic/setup.h as COMMAND_LINE_SIZE.
  static constexpr int kMaxProcCmdlineSize = 4096;

  std::string contents;
  [[maybe_unused]] int size =
      ReadProcFileToString("/proc/cmdline", 0, kMaxProcCmdlineSize, &contents);

  // Rather than fully parsing /proc/cmdline, we just look for string presence.
  // This can have false positives.

  // See `man bootparam` for the format of /proc/cmdline.
  // Note that either `nohz_full` or `nohz-full` are allowed.
  return
      // Either we start with the key, ...
      absl::StartsWith(contents, "nohz_full=") ||
      absl::StartsWith(contents, "nohz-full=") ||
      // or it's preceded by a space.
      absl::StrContains(contents, " nohz_full=") ||
      absl::StrContains(contents, " nohz-full=");
#else
  return false;
#endif
}

}  // namespace internal
}  // namespace base

absl::Time GetInitGoogleTime() {
  assert(start_up_time);
  return *start_up_time;
}

absl::Duration GetUptime() {
  assert(start_up_time);
  return absl::Now() - *start_up_time;
}

// Returns the number of seconds of idle time since the system was started.
static double GetIdleTimeRaw() {
// b/37140047: Android selinux blocks access to /proc/stat
#if defined __linux__ && !defined(__ANDROID__)
  double uptime;
  return ReadProcField("/proc/uptime", -1, 1, "%lf", &uptime) ? uptime : -1.0;
#else
  // This gets called at static initialization, so we don't want to exit the
  // process.  Logging isn't initialized at this point either so can't log
  // warning.
  // LOG_FIRST_N(ERROR, 3) << "GetIdleTime(): OS not supported";
  return -1.0;
#endif
}

double GetIdleTime() {
  static const double idle_time_base = GetIdleTimeRaw();
  // Get the raw idle time, and convert it to an idle time number
  // since the process started by subtracting off the idle_time_base
  // value we saved at initialization time (being careful about error
  // values).
  double idle_time = GetIdleTimeRaw();
  if ((idle_time < 0) || (idle_time_base < 0)) {
    return -1.0;
  } else {
    return idle_time - idle_time_base;
  }
}

// Return the number of open file descriptors for the current process.
int NumOpenFDs() {
#if defined __linux__
  int count;
  // Fallback for normal Linux systems: count directory entries in
  // /proc/self/fd.
  if (CountDentries("/proc/self/fd", &count)) {
    return count;
  }
  BASE_SYSINFO_LOG_FIRST_N(
      WARNING, 3, "NumOpenFDs: can't read /proc/self/status field open_files");
  return -1;
#else
  BASE_SYSINFO_LOG_FIRST_N(ERROR, 3, "NumOpenFDs: OS not supported");
  return -1;
#endif
}

#ifndef _WIN32
// If current process is being ptrace()d, 'TracerPid' in /proc/self/status
// will be non-zero.
bool IsDebuggerAttached() {
  // Since we could be called from FailureSignalHandler, avoid stdio
  // which could have been corrupted.
  // Limit stack usage as well.  Currently, TracerPid is at max offset 96
  // (depending on length of argv[0]) into /proc/self/status.
  char buf[150];
  int fd = open("/proc/self/status", O_RDONLY);
  if (fd == -1) {
    return false;  // Can't tell for sure.
  }
  const int len = read(fd, buf, sizeof(buf));
  bool rc = false;
  if (len > 0) {
    const char* const kTracerPid = "TracerPid:\t";
    buf[len - 1] = '\0';
    const char* p = strstr(buf, kTracerPid);
    if (p != nullptr) {
      rc = (strncmp(p + strlen(kTracerPid), "0\n", 2) != 0);
    }
  }
  close(fd);
  return rc;
}
#endif  // _WIN32

// ----------------------------------------------------------------------
// HasPosixThreads()
//      Return true if we're running POSIX (e.g., NPTL on Linux)
//      threads, as opposed to a non-POSIX thread library.  The thing
//      that we care about is whether a thread's pid is the same as
//      the thread that spawned it.  If so, this function returns
//      true.
// ----------------------------------------------------------------------
bool HasPosixThreads() {
#if defined(__linux__) && !defined(__ANDROID__)
#ifndef _CS_GNU_LIBPTHREAD_VERSION
#define _CS_GNU_LIBPTHREAD_VERSION 3
#endif
  char buf[32];
  //  We assume that, if confstr() doesn't know about this name, then
  //  the same glibc is providing LinuxThreads.
  if (confstr(_CS_GNU_LIBPTHREAD_VERSION, buf, sizeof(buf)) == 0) return false;
  return strncmp(buf, "NPTL", 4) == 0;
#elif defined(_WIN32)
  return false;
#else   // other OS
  return true;  //  Assume that everything else has Posix
#endif  // else __linux__
}

#ifdef __linux__
bool GetSwapDisks(std::set<dev_t>* swapdisks) {
  bool ok = true;
  FILE* f = OpenProcFile("/proc/swaps", -1);
  if (!f) {
    BASE_SYSINFO_LOG_FIRST_N(
        WARNING, 3, "GetSwapDisks: OpenProcFile: Couldn't open /proc/swaps");
    ok = false;
  } else {
    char buf[PATH_MAX + 80] = {0};
    bool firstline = true;
    while (fgets(buf, sizeof(buf), f)) {
      buf[sizeof(buf) - 1] = 0;
      if (firstline) {
        firstline = false;
        // Skip the header line if necessary. This is always present
        // in 2.4, and may or may not be present in 2.6 depending on
        // which swap areas are active
        if (strstr(buf, "Filename")) continue;
      }
      char path[sizeof(buf)];
      if (sscanf(buf, "%s", path) == 1) {
        // Newer kernels support per-container swaps.  For consistency, we
        // explicitly exclude these swap devices as they aren't really system
        // swap disks.
        if (strstr(buf + strlen(path), "private")) {
          continue;
        }
        struct stat sb;
        if (stat(path, &sb) == 0) {
          dev_t dev = S_ISBLK(sb.st_mode) ? sb.st_rdev : sb.st_dev;
          // NOLINTNEXTLINE(runtime/int)
          swapdisks->insert(dev);
        }
      } else {
        BASE_SYSINFO_LOG_FIRST_N(
            WARNING, 3,
            absl::StrCat("GetSwapDisks: Bad line in /proc/swaps: ", buf));
        ok = false;
      }
    }
    fclose(f);
  }
  return ok;
}
#endif  // __linux__
