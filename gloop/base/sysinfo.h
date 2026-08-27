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

//
// This file includes routines to find out characteristics
// of the machine a program is running on.  It is undoubtedly
// system-dependent.

// Functions listed here that accept a pid_t as an argument act on the
// current process if the pid_t argument is 0

// All functions here are thread-hostile due to file caching unless
// commented otherwise.

#ifndef THIRD_PARTY_GLOOP_BASE_SYSINFO_H_
#define THIRD_PARTY_GLOOP_BASE_SYSINFO_H_

#include <limits.h>
#include <stddef.h>
#include <stdio.h>  // for FILE*
#include <time.h>

#if defined _WIN32
// clang-format off
#include <windows.h>  // Must come before TlHelp32.h
#include <TlHelp32.h>  // for CreateToolhelp32Snapshot
// clang-format on
#else
#include <sys/types.h>
#endif

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/internal/sysinfo.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "gloop/base/auxiliary/parsed_process_stat.h"  // IWYU pragma: export
#include "gloop/base/port.h"                           // IWYU pragma: keep

// APIs in this namespace are more modern, have received attention
// since 2015, and are favored over ones outside it.  Prefer them when
// available.
namespace base {

// Return the total cpu time used by all threads in a given
// process (or self, for pid==0). If the PID does not exist, returns 0.
absl::Duration CPUUsage(pid_t pid);

// Return the total cpu time used by all threads in the current process.
absl::Duration CPUUsage();

// Return the total cpu time used by the current thread.
absl::Duration ThreadCPUUsage();

// Return the best approximation possible to the total cpu time used by
// dead and wait(2)'d children of the current process.
// Please note that this does NOT measure anything from children that are
// currently running!
absl::Duration ReapedCPUUsage();

// Return the walltime that the process actually started, via fork(), clone(),
// etc.
absl::Time ProcessStartTime();

// If seccomp is enabled, the process may be in a sandbox forbidding certain
// syscalls or file accesses, etc.
bool IsSeccompEnabled();

namespace internal {

// Start timer for GetUptime() and GetInitGoogleTime()
//
// The timer for GetUptime() and GetInitGoogleTime() must by initialized
// by calling StartUpWallTimer(). This timer initialization function is
// thread-hostile; it must be called before threads are spawned (from
// InitGoogle(), for instance) and must not be called more than once.
void StartUpWallTimer();

// Whether at least one core is configured in "adaptive tick" mode.
// https://www.kernel.org/doc/Documentation/timers/NO_HZ.txt
bool AreAdaptiveTicksEnabled();

}  // namespace internal

// memory statistics
struct MemoryStats {
  int64_t vsize;
  int64_t rss;
  int64_t shared;
  int64_t code;
  int64_t data;
};

// An estimate of the available hardware concurrency.  This value is a
// reasonable default size for a potentially CPU-bound thread pool.
// Unlike NumCPUs(), the return value may change between calls.
//
// OK to call before InitGoogle().  Thread-safe.
int AvailableCPUs();

};  // namespace base

// Number of logical processors (hyperthreads) in system.
// See https://github.com/abseil/gloop/tree/main/gloop/base/cpuid/cpuid.h for
// more CPU-related info.
//
// This value is appropriate for sizing a per-CPU structure, such as an array
// indexed by the result of getcpu() / RDTSCP.  It's also a reasonable choice
// for sizing a striped lock or a concurrent hashtable to minimize contention.
//
// To size a CPU-bound thread pool, prefer base::AvailableCPUs().
//
// OK to call before InitGoogle().  Thread-safe.
inline int NumCPUs() { return absl::base_internal::NumCPUs(); }

// Returns the time at which when InitGoogle was called
absl::Time GetInitGoogleTime();

// Time since InitGoogle() was called.
absl::Duration GetUptime();

// The amount of physical memory available on a machine in bytes
uint64_t PhysicalMem();

// The amount of physical memory not used by the OS (can be greater than what
// can be mapped into a single process, i.e. can be greater than 4 GB).
uint64_t NonKernelMem64();

// The largest single area of virtual memory space that can currently
// be mapped. (Not necessarily related to available physical
// memory). Beware of race conditions if other threads are allocating
// memory.
int64_t MaxVMArea();

// amount of free memory in bytes
int64_t FreeMem();

// Tries to return the nominal core processor cycles per second of each
// processor.   This is _not_ necessarily the frequency of the CycleClock
// counter (see cycleclock.h).  If the frequency cannot be determined, returns
// `1.0`.
inline double NominalCPUFrequency() {
  return absl::base_internal::NominalCPUFrequency();
}

// size of the process in bytes
int64_t VirtualProcessSize();

// size of the process in bytes, cached for efficiency.  Thread-hostile.
int64_t VirtualProcessSizeForExport();

// memory stats of a process.  Thread-safe.
bool GetMemoryStats(pid_t pid, base::MemoryStats* mem_stats);

// current process memory stats, cached for ttl seconds.  Thread-hostile.
bool GetMemoryStatsForExport(base::MemoryStats* mem_stats, int ttl);

// real memory usage (RSS) of a process in bytes
int64_t MemoryUsage(pid_t pid);

// virtual memory size of a process in bytes
int64_t VirtualMemorySize(pid_t pid);

// number of threads in a process, or -1 on failure
int GetProcessThreadCount(pid_t pid);

// pid of parent of the given process
pid_t ProcessParent(pid_t pid);

// full path to the executable of the given process, or "" on failure
std::string ProcessExePath(pid_t pid);

// the nice level of the current process
int Nice();

// command line of a process
size_t CommandLine(pid_t pid, char* buf, int len);

// real memory usage of a process in bytes, cached for efficiency
// and without process id.  Thread-hostile.
int64_t MemoryUsageForExport();

// As above, but no caching.  Still thread-hostile.
int64_t UncachedMemoryUsageForExport();

// Returns the system load average over the last 1, 5 or 15 minutes. On Linux,
// the load average is defined as the number of processes in the run queue or
// waiting for disk IO. On non-Linux systems, this function will return 0.
enum SystemLoadAverageTimeRange {
  SYSTEM_LOAD_1MIN,
  SYSTEM_LOAD_5MIN,
  SYSTEM_LOAD_15MIN
};
double SystemLoadAverageForTimeRange(SystemLoadAverageTimeRange which);

// Returns the system load average over the last minute.
double SystemLoadAverage();

// mreturn time last booted
time_t BootTime();

// list of processes running on the system
bool ProcessList(std::vector<pid_t>* list);

// the process group id for a process
pid_t ProcessGroup(pid_t pid);

// the thread group id for a process
pid_t ThreadGroup(pid_t pid);

//  Return true if we're running POSIX (e.g., NPTL on Linux) threads,
//  as opposed to a non-POSIX thread library.  The thing that we care
//  about is whether a thread's pid is the same as the thread that
//  spawned it.  If so, this function returns true.
//  Thread-safe.
//  Note: We consider false negatives to be OK, although the unit test
//  compares the result of HasPosixThreads() against the condition
//  that subthread.pid==process.pid.
bool HasPosixThreads();

// Return the thread id of the current thread, as told by the system.
// No two currently-live threads implemented by the OS shall have the same ID.
// Thread ids of exited threads may be reused.   Multiple user-level threads
// may have the same thread ID if multiplexed on the same OS thread.
//
// On Linux, you may send a signal to the resulting ID with tgkill().  However,
// it is recommended for portability that you use pthread_kill() instead.
inline pid_t GetTID() { return absl::base_internal::GetTID(); }

// Like GetTID(), but caches the result in thread-local storage in order
// to avoid unnecessary system calls. Note that there are some cases where
// one must call through to GetTID directly, which is why this exists as a
// separate function. For example, GetCachedTID() is not safe to call in
// an asynchronous signal-handling context nor right after a call to fork().
inline pid_t GetCachedTID() { return absl::base_internal::GetCachedTID(); }

// return the first 15 characters of the name of the given process
std::string ProcessName(pid_t pid);

// Retrieve page fault information for a process; returns true on
// success, false on error
bool ProcessPageFaults(pid_t pid, int64_t* minor_faults, int64_t* cminor_faults,
                       int64_t* major_faults, int64_t* cmajor_faults);

// Return total idle time for all CPUs on this machine in seconds,
// since the process was started.  Returns a value < 0 if there was an
// error getting the idle time.
double GetIdleTime();

// Return the number of open file descriptors for the current process.
int NumOpenFDs();

#ifndef _WIN32
// Returns true if current process is being ptraced.
// Currently only works on Linux.
bool IsDebuggerAttached();
#endif  // _WIN32

#ifdef __linux__
// Find the set of disks in use as swap disks
bool GetSwapDisks(std::set<dev_t>* swapdisks);

// Useful utility functions for reading things from /proc. You
// probably shouldn't generally use them, but rather define a new
// function above, which can be made portable if necessary. These
// functions are primarily exported so the unittest can access them.

// OpenProcFile()
//
// filename: format specifier (which may contain up to two %d's)
// pid:      process id of interest; 0 indicates current process; is ignored
//           if filename doesn't contain a %d
//
// filename and pid are combined via snprintf to create a filename,
// which is opened and returned. returns null on error.
FILE* OpenProcFile(const char* filespec, pid_t pid);

// Like File::Readable, but does the /proc dance.
bool ProcFileReadable(const char* filespec, pid_t pid);

// ScanFileForKeyword()
//
// f:         open file to be scanned
// keyword:   line prefix of interest
// format:    scanf format for interpreting rest of line (single value)
// ...        location for scanf result
//
// Look for a line with a particular prefix, and return the remainder
// of the line as interpreted by a sscanf() format (or just the rest
// of the line if format is null). Returns true/false for
// success/failure. Internally uses a buffer of size kScanfileBufsize,
// so string formats without field width restrictions should supply a
// buffer of at least this size if the line that you're reading
// doesn't have a well-defined maximum length.
//
// WARNING: Will not work correctly with > 1 scanf arg. See (b/35995071)
bool ScanFileForKeyword(FILE* f, const char* keyword, const char* format, ...)
    ABSL_SCANF_ATTRIBUTE(3, 4);

// The maximum size string that ScanFileForKeyword() might return if
// you don't specify a width limit.
#define kScanfileBufsize 4096

// ReadProcKeyword()
//
// filename, pid:        see OpenProcFile()
// keyword, format, ...: see ScanFileForKeyword()
//
// Does OpenProcFile() followed by ScanFileForKeyword(), returns true
// if both succeed else false.
//
// WARNING: Will not work correctly with > 1 scanf arg. See (b/35995071)
bool ReadProcKeyword(const char* filename, pid_t pid, const char* keyword,
                     const char* format, ...) ABSL_SCANF_ATTRIBUTE(4, 5);

// ReadProcKeywordQuiet() is the same as ReadProcKeyword() but doesn't
// log an error if the file can't be opened or the keyword isn't found
//
// WARNING: Will not work correctly with > 1 scanf arg. See (b/35995071)
bool ReadProcKeywordQuiet(const char* filename, pid_t pid, const char* keyword,
                          const char* format, ...) ABSL_SCANF_ATTRIBUTE(4, 5);

// ReadProcField()
//
// filename, pid: see OpenProcFile()
// field:         field number to retrieve (0-based)
// format:        scanf format for field (single value)
// ...:           location for scanf result
//
// Opens a /proc file, splits into *space*-delimited fields and parses
// the value of the nth field into the result location, according to
// the passed scanf format. Returns true/false to indicate
// success/failure.
//
// This cannot be used for `/proc/*/stat` and other files with that format;
// use `ParseProcessStat` instead. See <link>
//
// REQUIRES: There must be exactly one argument after "format".
//
// TODO: The current implementation allows multiple
// arguments but does not provide any indication of an error
// when only some of the values were parsed correctly.
// Fix existing users who depend on that feature and remove
// support for parsing multiple values from this function.
bool ReadProcField(const char* spec, pid_t pid, int field, const char* format,
                   ...) ABSL_SCANF_ATTRIBUTE(4, 5);

// ParseProcessStat()
//
// filespec, pid: see OpenProcFile()
//
// Safer alternative to ReadProcField for `/proc/self/stat` and files with that
// format (see `man 5 proc`). Parses once but keeps state to allow multiple
// value accesses. Reads the single line and returns the corresponding parsed
// object.
absl::StatusOr<base::ParsedProcessStat> ParseProcessStat(const char* filespec,
                                                         pid_t pid);

// ReadProcFileToString()
//
// filename, pid: see OpenProcFile()
// max_size:      maximum number of bytes to be read.
// output:        the string that is filled.
// result:        number of bytes read, or -1 if file could not be opened.
//
// Opens a given /proc file and reads entire contents into a string.
// Useful for poorly formatted files, such as "/proc/net/dev".
int ReadProcFileToString(const char* filename, pid_t pid, size_t max_size,
                         std::string* output);

// ReadProcFileToBuffer()
//
// filename, pid: see OpenProcFile()
// max_size:      the maximum size of the buffer. If the underlying file is
//                larger or equal in length, the output buffer is filled with
//                at most max_size - 1 bytes from the file, and a terminator
//                NUL character.
// buf:           the buffer to be filled. If result is -1, no changes are made
//                to the buffer.
// result:        number of bytes read, or -1 if file could not be opened.
//
// See ReadProcFileToString for behavior. This function is useful in a context
// where heap allocations must be avoided.
int ReadProcFileToBuffer(const char* filename, pid_t pid, size_t max_size,
                         char* buf);

typedef absl::flat_hash_map<std::string, std::string> ProcMap;

// Read a file from /proc, split each line at the first colon
// boundary, and use the left and right sides as key and value to
// insert into the passed ProcMap. Any lines not containing a colon
// are ignored. Whitespace is not (currently) stripped from the results.
bool ReadProcMap(const std::string& path, ProcMap* res);

#endif /* __linux__ */

#endif  // THIRD_PARTY_GLOOP_BASE_SYSINFO_H_
