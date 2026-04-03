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

// A somewhat ad-hoc collection of tests to check that sysinfo.cc is
// returning reasonably sane values.

#include "gloop/base/sysinfo.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>  // for makedev
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/log/flags.h"
#include "absl/log/globals.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "gloop/base/auxiliary/parsed_process_stat.h"

// getpid() is deprecated on MSVC, use _getpid() instead.
// Reference: https://msdn.microsoft.com/en-us/library/ms235372.aspx
#ifdef _MSC_VER
#include <process.h>

#define getpid _getpid
#endif

#include <algorithm>
#include <cmath>
#include <ios>
#include <list>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/internal/cycleclock.h"
#include "absl/container/fixed_array.h"
#include "absl/flags/flag.h"
#include "absl/flags/reflection.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "gloop/base/init_google.h"
#include "gloop/base/proc_maps.h"
#include "gloop/thread/thread.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tcmalloc/malloc_extension.h"

namespace {

using ::absl::base_internal::CycleClock;
using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::testing::AnyOf;
using ::testing::Eq;

#ifdef __linux__

int example_global_const = 30;
void* example_heap_address = malloc(sizeof(int));

// Check the base /proc file parsing functions
void TestParseFunctions() {
  char buf[kScanfileBufsize];
  int intval;

  LOG(INFO) << "Checking ReadProcKeyword()";

  // Am I called sysinfo_unittest?
  ASSERT_TRUE(ReadProcKeyword("/proc/%d/status", 0, "Name:", "%s", buf));
  EXPECT_STREQ(buf, "sysinfo_unittes");  // 15 char limit on name in
                                         // /proc/pid/status

  // Does the nullptr format string passing work?
  const char* nullfmt = nullptr;
  ASSERT_TRUE(ReadProcKeyword("/proc/%d/status", 0, "Name:", nullfmt, buf));
  EXPECT_STREQ(buf, "\tsysinfo_unittes");

  // Is pid 1 really pid 1?
  ASSERT_TRUE(ReadProcKeyword("/proc/%d/status", 1, "Pid:", "%d", &intval));
  EXPECT_EQ(intval, 1);

  // Am I my own pid?
  ASSERT_TRUE(ReadProcKeyword("/proc/%d/status", 0, "Pid:", "%d", &intval));
  EXPECT_EQ(intval, getpid());

  // Check for missing keywords
  ASSERT_TRUE(!ReadProcKeyword("/proc/%d/status", 0,
                               "NonexistentKeyword:", "%d", &intval));
  ASSERT_TRUE(!ReadProcKeywordQuiet("/proc/%d/status", 0,
                                    "YouShouldntSeeThisError:", "%d", &intval));
  // Check for missing files
  ASSERT_TRUE(
      !ReadProcKeyword("/proc/nonexistentfile", 0, "Foo:", "%d", &intval));
  ASSERT_TRUE(!ReadProcKeywordQuiet("/proc/nonexistentquietfile", 0,
                                    "Foo:", "%d", &intval));
  LOG(INFO) << "Checking ReadProcField()";

  // Ensure /proc/*/stat is not parsed
  ASSERT_TRUE(
      !ReadProcField("/proc/%d/stat", /*pid=*/1, /*field=*/1, "%s", buf));

  ASSERT_TRUE(
      ReadProcField("/proc/%d/statm", /*pid=*/0, /*field=*/1, "%d", &intval));
}

void TestMemoryUsage() {
  LOG(INFO) << "Testing memory usage";

  // Find the "real" memory usage values
  int64_t vmsize = VirtualMemorySize(getpid()) >> 10;
  int64_t rss = MemoryUsage(getpid()) >> 10;

  // Read all the memory stats.
  base::MemoryStats mem_stats;
  CHECK(GetMemoryStats(getpid(), &mem_stats));

  // Get the number of threads.
  int num_threads = GetProcessThreadCount(getpid());
  CHECK_GT(num_threads, 0);
  CHECK_LT(num_threads, 100);

  // check that Nice() returns a sane value
  int niceval = Nice();
  CHECK_GE(niceval, -20);
  CHECK_LE(niceval, 19);

  // Get the expected values by reading the proc/<pid>/stat file
  // (we do this after calling VirtualMemorySize and MemoryUsage
  // since those routines can cause memory allocation).
  std::string filename = absl::StrCat("/proc/", getpid(), "/stat");
  FILE* f = fopen(filename.c_str(), "r");
  CHECK(f);

  char proc_contents[PATH_MAX];
  std::string result = fgets(proc_contents, sizeof(proc_contents), f);
  CHECK(!result.empty());

  std::vector<std::string> col =
      absl::StrSplit(result, absl::ByAnyChar("\t "), absl::SkipEmpty());

  int32_t nicetarget;
  CHECK(absl::SimpleAtoi(col[18], &nicetarget));

  // Stat file reports virtual memory size in bytes. We shift to KB.
  int64_t vmtarget;
  CHECK(absl::SimpleAtoi(col[22], &vmtarget));
  vmtarget = vmtarget >> 10;

  // Stat file reports the number of pages the process has in real memory.
  // Convert that to bytes based on system page size and then make it
  // user-friendlier in KB.
  int64_t rsstarget;
  CHECK(absl::SimpleAtoi(col[23], &rsstarget));
  rsstarget *= getpagesize();
  rsstarget = rsstarget >> 10;

  LOG(INFO) << "Got"
            << " nice:" << nicetarget << " vm:" << vmtarget
            << " rss:" << rsstarget << " from proc/" << getpid() << "/stat";

  fclose(f);

  int64_t vmsize_mem_stats = mem_stats.vsize;
  const double epsilon = 0.01;
  // VMSize is much more predictable then RSS
  CHECK_GE(vmsize, (1 - epsilon) * vmtarget);
  CHECK_LE(vmsize, (1 + epsilon) * vmtarget);
  if (tcmalloc::MallocExtension::GetNumericProperty(
          "dynamic_tool.virtual_memory_overhead")
          .value_or(0) == 0) {
    // Dynamic tools are not being used.
    CHECK_GE(vmsize_mem_stats >> 10, (1 - epsilon) * vmtarget);
    CHECK_LE(vmsize_mem_stats >> 10, (1 + epsilon) * vmtarget);
  } else {
    // GetMemoryStats doesn't count part of virtual memory overhead
    // introduced by dynamic testing tools, such as ASan and TSan. It's too
    // difficult to predict VM usage in this case.
  }

  CHECK_GE(rss, rsstarget - 500);
  CHECK_LE(rss, rsstarget + 500);
  CHECK_GE(mem_stats.rss >> 10, rsstarget - 500);
  CHECK_LE(mem_stats.rss >> 10, rsstarget + 500);

  int mintarget = niceval;
  int maxtarget = niceval;
  CHECK_GE(nicetarget, mintarget);
  CHECK_LE(nicetarget, maxtarget);

  // reduce our nice level and make sure Nice() returns the new value
  errno = 0;
  niceval = Nice();
  // You can't go lower than 19 ...
  if (niceval < 19) {
    PCHECK(nice(1) != -1);
    CHECK_EQ(niceval + 1, Nice());
  }
}

void TestProcessState() {
  LOG(INFO) << "Testing process names";
  EXPECT_EQ(ProcessName(0), "sysinfo_unittes");
  // Test for non-existent process
  EXPECT_EQ(ProcessName(999999999), "");

  // Several of the following are something of a duplication of the tests in
  // TestParseFunctions, but they're testing slightly different things.

  // Check our own parent
  EXPECT_EQ(ProcessParent(0), getppid());
  EXPECT_EQ(ProcessParent(getpid()), getppid());
  // Check init's parent
  EXPECT_EQ(ProcessParent(1), 0);

  // Check the process exe path.
  // Reminder: readlink does not '\0'-terminate.
  char self_exe_buf[PATH_MAX];
  ssize_t n = readlink("/proc/self/exe", self_exe_buf, sizeof(self_exe_buf));
  ASSERT_GT(n, 0);
  ASSERT_LT(n, sizeof(self_exe_buf));
  self_exe_buf[n] = '\0';
  EXPECT_EQ(ProcessExePath(getpid()), self_exe_buf);
  EXPECT_EQ(ProcessExePath(-1), "");

  // Check the number of open file descriptors.
  int num_open_fds = NumOpenFDs();
  if (num_open_fds < 0) {
    ASSERT_GE(num_open_fds, 0);

    int fds[2];
    CHECK_EQ(pipe(fds), 0);
    EXPECT_EQ(num_open_fds + 2, NumOpenFDs());
    CHECK_EQ(close(fds[0]), 0);
    EXPECT_EQ(num_open_fds + 1, NumOpenFDs());
    CHECK_EQ(close(fds[1]), 0);
    EXPECT_EQ(num_open_fds, NumOpenFDs());
  }
}

static void BM_ProcessName(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(ProcessName(1));
  }
}
BENCHMARK(BM_ProcessName);

void TestProcessPageFaults() {
  LOG(INFO) << "Testing ProcessPageFaults()";

  // We cannot assert that page faults will not occur between the
  // getrusage() call and reading /proc, but once "warmed up," an
  // otherwise idle process should see no major faults most of the
  // time and a low rate of minor faults.  Try multiple times.
  for (int attempt = 0;; ++attempt) {
    struct rusage self, children;
    int64_t minor_faults, cminor_faults, major_faults, cmajor_faults;

    PCHECK(getrusage(RUSAGE_SELF, &self) != -1);
    PCHECK(getrusage(RUSAGE_CHILDREN, &children) != -1);
    CHECK(ProcessPageFaults(0, &minor_faults, &cminor_faults, &major_faults,
                            &cmajor_faults));

    VLOG(1) << "getrusage():" << self.ru_minflt << " " << self.ru_majflt << " "
            << children.ru_minflt << " " << children.ru_majflt;

    VLOG(1) << "/proc:      " << minor_faults << " " << major_faults << " "
            << cminor_faults << " " << cmajor_faults;

    // It's possible that minor faults occurred between the getrusage
    // call and the ProcessPageFaults call.  We use a small epsilon to
    // reduce false negatives.
    const int epsilon = 3;
    const bool pass = (self.ru_minflt <= minor_faults &&
                       self.ru_minflt >= (minor_faults - epsilon) &&
                       self.ru_majflt <= major_faults &&
                       self.ru_majflt >= (major_faults - epsilon) &&
                       children.ru_minflt <= cminor_faults &&
                       children.ru_minflt >= (cminor_faults - epsilon) &&
                       children.ru_majflt <= cmajor_faults &&
                       children.ru_majflt >= (cmajor_faults - epsilon));
    if (pass || attempt >= 100) {
      CHECK_LE(self.ru_minflt, minor_faults);
      CHECK_GE(self.ru_minflt, minor_faults - epsilon);
      CHECK_LE(self.ru_majflt, major_faults);
      CHECK_GE(self.ru_majflt, major_faults - epsilon);
      CHECK_LE(children.ru_minflt, cminor_faults);
      CHECK_GE(children.ru_minflt, cminor_faults - epsilon);
      CHECK_LE(children.ru_majflt, cmajor_faults);
      CHECK_GE(children.ru_majflt, cmajor_faults - epsilon);
      CHECK(pass) << "; This shouldn't fire -- a prior CHECK should have.";
      break;
    }
  }
}

class TestThread : public Thread {
 public:
  int parent_pid;
  int parent_tid;
  int pid;
  int tid;

  absl::Notification child_notify;
  absl::Notification parent_notify;

 protected:
  void Run() {
    pid = getpid();
    tid = GetTID();

    CHECK_EQ(pid, ThreadGroup(0));
    CHECK_NE(parent_tid, tid);
    CHECK_EQ(parent_pid, ThreadGroup(parent_tid));

    if (pid == parent_pid) {
      // Our kernel/thread library shares pids between different threads
      CHECK_NE(tid, pid);
    } else {
      // Our threads all have different pids
      CHECK_EQ(tid, pid);
    }

    parent_notify.Notify();
    child_notify.WaitForNotification();
  }
};

void TestThreads() {
  int pid;

  LOG(INFO) << "Checking GetTID()";
  CHECK_EQ(GetTID(), getpid());

  LOG(INFO) << "Checking ThreadGroup(0)";
  CHECK_EQ(ThreadGroup(0), getpid());

  LOG(INFO) << "Checking ThreadGroup(child process)";

  PCHECK((pid = fork()) != -1);
  if (pid != 0) {
    // Parent
    CHECK_EQ(ThreadGroup(pid), pid);
    waitpid(pid, nullptr, 0);
  } else {
    exit(0);
  }

  LOG(INFO) << "Checking ThreadGroup(child thread)";

  TestThread t;
  t.parent_pid = getpid();
  t.parent_tid = GetTID();

  t.SetJoinable(true);
  t.Start();

  // Wait for child to set up pid/tid
  t.parent_notify.WaitForNotification();

  CHECK_EQ(ThreadGroup(t.tid), t.pid);

  t.child_notify.Notify();
  t.Join();
}

void TestHasPosixThreads() {
  LOG(INFO) << "Testing HasPosixThreads()";

  //  Our litmus test for "is POSIX compliant" is that all threads in
  //  a process have the same PID.  We test this directly here.
  class GetPidThread : public Thread {
   public:
    GetPidThread() { SetJoinable(true); }
    void Run() { pid_ = getpid(); }
    pid_t pid_;
  };
  GetPidThread t;
  t.Start();
  t.Join();

  LOG(INFO) << "HasPosixThreads() => " << HasPosixThreads();
  CHECK_EQ((t.pid_ == getpid()), HasPosixThreads());
}

void ReadPipe(std::string str, const char* fmt, void* result) {
  FILE* f = popen(str.c_str(), "r");
  CHECK(f);
  EXPECT_EQ(fscanf(f, fmt, result), 1);
  CHECK_EQ(pclose(f), 0);
}

void TestSystemState() {
  LOG(INFO) << "Testing NumCPUs()";
  int numcpus;
  ReadPipe("grep -c -i ^processor /proc/cpuinfo", "%d", &numcpus);
  EXPECT_EQ(NumCPUs(), numcpus);

  LOG(INFO) << "Testing NominalCPUFrequency()";
  // There are many ways to assign NominalCPUFrequency, depending on the
  // machine.  We just want to make sure at least one of them gave
  // back a plausible value.
  EXPECT_GT(NominalCPUFrequency(), 10.0);

  LOG(INFO) << "Testing CycleClock::Frequency()";
  // There are many ways to assign CycleClock::Frequency(), depending on the
  // machine.  We just want to make sure at least one of them gave
  // back a plausible value.
  EXPECT_GT(CycleClock::Frequency(), 10.0);

  LOG(INFO) << "Testing boot time";

  // Extract the uptime from the output of "uptime", and use "date" to
  // convert it into a number by adding it to the Unix epoch and
  // printing the result in seconds. (Only has 1-minute accuracy ...)

  LOG(INFO) << "Testing SystemLoadAverage()";

  // Check SystemLoadAverage() which returns 1 min load average
  // This is a little non-deterministic. Be happy with one out of three.
  int success_load_average = 0;
  for (int i = 0; i < 3; i++) {
    double loadavg;
    ReadPipe("awk '{print $1}' < /proc/loadavg", "%lf", &loadavg);
    const double reported_load = SystemLoadAverage();
    if (fabs(reported_load - loadavg) > 0.02) {
      LOG(ERROR) << "loadavg = " << loadavg << ", reported = " << reported_load;
      LOG(INFO) << "Sleeping to let load fluctuation resolve";
      sleep(15);
    } else {
      success_load_average++;
    }
  }
  EXPECT_GT(success_load_average, 0);

  // Check for 1/5/15 minute load averages using SystemLoadAverage()
  for (int j = 0; j < 3; j++) {
    LOG(INFO) << "Testing SystemLoadAverageForTimeRange(" << j << ")";
    success_load_average = 0;
    // This is a little non-deterministic. Be happy with one out of three.
    for (int i = 0; i < 3; i++) {
      double loadavg;
      char awk_cmd[] = "awk '{print $1}' < /proc/loadavg";
      snprintf(awk_cmd, sizeof(awk_cmd), "awk '{print $%1d}' < /proc/loadavg",
               j + 1);
      ReadPipe(awk_cmd, "%lf", &loadavg);
      const double reported_load = SystemLoadAverageForTimeRange(
          static_cast<SystemLoadAverageTimeRange>(j));
      if (fabs(reported_load - loadavg) > 0.02) {
        LOG(ERROR) << "loadavg = " << loadavg
                   << ", reported = " << reported_load;
        LOG(INFO) << "Sleeping to let load fluctuation resolve";
        sleep(15);
      } else {
        success_load_average++;
      }
    }
    EXPECT_GT(success_load_average, 0);
  }

  // Make sure PhysicalMem() returns a plausible value.
  EXPECT_GE(PhysicalMem(), uint64_t{128} << 20);  // 128 MB
  EXPECT_LE(PhysicalMem(), uint64_t{1} << 50);    // 1 PB
  // Make sure the FreeMem returns a plausible value.
  EXPECT_GE(FreeMem(), 0);
  EXPECT_LE(FreeMem(), PhysicalMem());
}

void BM_NumCPUs(benchmark::State& state) {
  for (auto _ : state) {
    for (int i = 0; i < NumCPUs(); ++i) {
    }
  }
}
BENCHMARK(BM_NumCPUs);

void TestGetIdleTime() { LOG(INFO) << "Testing GetIdleTime()"; }

void TestCommandLine(absl::string_view cmdline) {
  char buf[4096];
  int linelen;

  ASSERT_TRUE(linelen = CommandLine(0, buf, sizeof(buf)));
  EXPECT_EQ(cmdline, std::string(buf, linelen));
  ASSERT_TRUE(linelen = CommandLine(1, buf, sizeof(buf)));
  // SysVinit 2.85 changed /proc/1/cmdline to include the current run level.
  EXPECT_TRUE((linelen >= 4 && strncmp("init", buf, 4)) ||
              (linelen >= 10 && strncmp("/sbin/init", buf, 10)));
}

void TestProcMaps() {
  // Some architecture-specific attributes of /proc/*/maps:
  static const struct ProcMapData {
    bool position_independent_executable;
    bool expect_consecutive_map_regions;
    uint64_t standard_elf_load_address;
  } kProcMapData = {
#if defined(_GOOGLE_HARDENED_BUILD) || defined(ABSL_HAVE_THREAD_SANITIZER) || \
    defined(ABSL_HAVE_ADDRESS_SANITIZER) ||                                   \
    defined(ABSL_HAVE_MEMORY_SANITIZER) || defined(__PIC__)
      // When a build is hardened (<link>), or dynamic tools are
      // used the ELF segments are loaded under random addresses due to full
      // ASLR of the binary. Therefore, testing addresses of TEXT and BSS
      // segments
      // doesn't make much sense in such case.
      true, false, 0x0
#elif defined(__i386__)
      false, true, 0x08048000
#elif defined(__x86_64__)
      false, false, 0x400000
#elif defined(__arm__)
      false, true, 0x8000
#elif defined(__powerpc64__)
      false, true, 0x10000000
#elif defined(__aarch64__)
      false, true, 0x400000
#else
#error "Unknown platform -- unsure how to test ProcMapsIterator."
#endif
  };
  LOG(INFO) << "Testing ProcMapsIterator";
  ProcMapsIterator it(0);
  ASSERT_TRUE(it.Valid());

  uint64_t start, end, offset, prev_end;
  int64_t inode;
  dev_t dev;
  char *filename, *flags;

  // Map a file with a really long name. We can't actually use
  // PATH_MAX as although the mapping works, the name seems to be too
  // long to show up in /proc/<pid>/maps ...
  absl::FixedArray<char> longname(PATH_MAX - 64);
  ASSERT_LT(testing::TempDir().size(), longname.size() - 255 - 100);
  absl::SNPrintF(longname.data(), longname.size(), "%s",
                 absl::StripSuffix(testing::TempDir(), "/"));
  char path_element[256] = "/";
  memset(path_element + 1, 'x', sizeof(path_element) - 1);
  path_element[255] = 0;

  while (strlen(longname.data()) <
         (longname.size() - (strlen(path_element)) - strlen(" (deleted)"))) {
    absl::SNPrintF(longname.data(), longname.size(), "%s",
                   absl::StrCat(longname.data(), path_element));
    PLOG_IF(FATAL, mkdir(longname.data(), 0700) == -1 && errno != EEXIST);
  }
  int longfd;
  PCHECK(rmdir(longname.data()) != -1);
  PCHECK((longfd = open(longname.data(), O_RDWR | O_CREAT, 0600)) != -1);
  PCHECK(unlink(longname.data()) != -1);
  void* longmap;
  longmap = mmap(nullptr, 4096, PROT_READ, MAP_PRIVATE, longfd, 0);
  ASSERT_NE(longmap, MAP_FAILED);
  LOG(INFO) << "Mapped longmap at " << longmap;
  // The name of the file in /proc/self/maps changes when we delete it
  absl::SNPrintF(longname.data(), longname.size(), "%s",
                 absl::StrCat(longname.data(), " (deleted)"));
  int example_stack_var;

  // Get some information about our binary
  const char* exe = "/proc/self/exe";
  struct stat statbuf;
  absl::FixedArray<char> binname(PATH_MAX);
  memset(binname.data(), 0, binname.size());
  PCHECK(stat(exe, &statbuf) != -1);
  PCHECK(readlink(exe, binname.data(), binname.size()) != -1);

  if (!kProcMapData.position_independent_executable) {
    CHECK(it.NextExt(&start, &end, &flags, &offset, &inode, &filename, &dev));
    // TEXT segment
    EXPECT_EQ(inode, statbuf.st_ino);
    EXPECT_EQ(dev, statbuf.st_dev);
    EXPECT_STREQ(filename, binname.data());
    EXPECT_STREQ(flags, "r-xp");
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(start, kProcMapData.standard_elf_load_address);
    EXPECT_GT(end, start);

    // Check that a text address is really in the range start-end.
    uintptr_t entry_point = reinterpret_cast<uintptr_t>(&TestProcMaps);
    EXPECT_LT(start, entry_point);
    EXPECT_GT(end, entry_point);
    // Check that the "canonical" line is correct
    absl::FixedArray<char> expected(PATH_MAX + 1024), actual(PATH_MAX + 1024);
    // This isn't the best test in the world, but good enough.
    dev_t st_dev = statbuf.st_dev;
    absl::SNPrintF(expected.data(), expected.size(),
                   "%08x-%08x %4s %08x %02x:%02x %-11d %s\n",
                   kProcMapData.standard_elf_load_address, end, "r-xp", 0,
                   st_dev / 256, st_dev % 256, inode, binname.data());
    EXPECT_GT(
        ProcMapsIterator::FormatLine(actual.data(), actual.size(), start, end,
                                     flags, offset, inode, filename, dev),
        0);
    EXPECT_STREQ(expected.data(), actual.data());
    // DATA/BSS  segment. Example:
    //  without -Wl,-z,relro (default config on 2013-10-28):
    //    00463000-00466000 rw-p 00063000 00:1f 5766957  /.../sysinfo_unittest
    //  with -Wl,-z,relro (proposed hardened config):
    //    00463000-00466000 r--p 00062000 00:1f 5766555  /.../sysinfo_unittest
    //    00466000-00467000 rw-p 00065000 00:1f 5766555  /.../sysinfo_unittest
    for (int j = 0; j < 2; ++j) {
      prev_end = end;
      ASSERT_TRUE(
          it.NextExt(&start, &end, &flags, &offset, &inode, &filename, &dev));
      LOG(ERROR) << "START: " << start << " FLAGS: " << flags;
      if (kProcMapData.expect_consecutive_map_regions) {
        EXPECT_EQ(start, prev_end);
      }
      EXPECT_EQ(flags[0], 'r');
      if (flags[1] == 'w') {
        // No RELRO segment
        ++j;
      } else {
        EXPECT_EQ(flags[1], '-');
      }
      // On linux 2.6 kernel, data segment may have x attribute.
      EXPECT_EQ(flags[3], 'p');
      EXPECT_EQ(inode, statbuf.st_ino);
      EXPECT_EQ(dev, statbuf.st_dev);
      EXPECT_NE(offset, 0);
      EXPECT_GT(end, start);
    }
    // Check that a constant-data address is really in the range start-end.
    EXPECT_LT(start, (uintptr_t)&example_global_const);
    EXPECT_GT(end, (uintptr_t)&example_global_const);
  }

  // Scan entries for the really long filename we created and for the
  // entry which contains example_heap_address.
  //
  // We keep going when we get to the stack, for several reasons.  First,
  // it is followed by one or more additional segments, depending on
  // kernel version (vsyscall and vdso regions).  In additionally on
  // some kernels (e.g., the gHardy kernel which seems to have some
  // address space randomization enabled), it's possible to get other
  // mappings after the stack segment.
  int maps_seen = 0;
  bool long_seen = false;
  bool example_heap_seen = false;
  while (it.NextExt(&start, &end, &flags, &offset, &inode, &filename, &dev)) {
    VLOG(1) << std::hex << start << "-" << end << ": " << filename;
    maps_seen++;
    if (start <= reinterpret_cast<uintptr_t>(example_heap_address) &&
        reinterpret_cast<uintptr_t>(example_heap_address) < end) {
      EXPECT_EQ(inode, 0);
      // Linux 2.6.12 gives names to some segments, while others
      // versions leave the name field blank. Tcmalloc names its memory regions.
      EXPECT_TRUE(!strcmp(filename, "") || !strcmp(filename, "[heap]") ||
                  absl::StrContains(filename, "tcmalloc_region_"));
      EXPECT_TRUE(!strcmp(flags, "rwxp") || !strcmp(flags, "rw-p"));
      EXPECT_EQ(dev, 0);
      example_heap_seen = true;

    } else if (start == reinterpret_cast<uintptr_t>(longmap)) {
      EXPECT_STREQ(longname.data(), filename);
      long_seen = true;
    }
    if (start < (uintptr_t)&example_stack_var &&
        end > (uintptr_t)&example_stack_var) {
      // Found the stack.
      EXPECT_EQ(inode, 0);
      EXPECT_TRUE(!strcmp(filename, "") || !strcmp(filename, "[stack]"));
    }
  }
  EXPECT_TRUE(example_heap_seen);
  EXPECT_TRUE(long_seen);
  // Make sure we saw a reasonable number of mappings
  EXPECT_GT(maps_seen, 3);
}

#endif

using base::CPUUsage;
using base::ReapedCPUUsage;
using base::ThreadCPUUsage;

static void BM_CPUUsageOther(benchmark::State& state) {
  // use init as our guineapig -- marginally easier than spawning a target
  pid_t pid = 1;
  absl::Duration total = absl::ZeroDuration();
  for (auto _ : state) {
    total += CPUUsage(pid);
  }
  // avoid optimizing out the calls
  VLOG(1) << total;
}

BENCHMARK(BM_CPUUsageOther);

static void BM_ThreadCPUUsage(benchmark::State& state) {
  absl::Duration total = absl::ZeroDuration();
  for (auto _ : state) {
    total += ThreadCPUUsage();
  }
  CHECK_GT(total, absl::Nanoseconds(0));
}

BENCHMARK(BM_ThreadCPUUsage);

static void TestReadProcMap() {
  LOG(INFO) << "Testing ReadProcMap()";

  ProcMap pm;
  // Should fail on a non-existent entry
  CHECK(!ReadProcMap("/proc/doesnt_exist", &pm));

  // Should succeed on a non map-file, but find nothing
  CHECK(ReadProcMap("/proc/filesystems", &pm));
  CHECK(pm.empty());

  // Should succeed with useful results on a real map
  CHECK(ReadProcMap("/proc/self/status", &pm));
  CHECK(pm.count("Name"));
  CHECK_EQ(pm["Name"], "\tsysinfo_unittes\n");
  CHECK_EQ(pm["State"], "\tR (running)\n");
}

static void TestProcessList() {
  std::vector<pid_t> pids;
  ProcessList(&pids);

  EXPECT_TRUE(std::find(pids.begin(), pids.end(), 1) != pids.end());
  EXPECT_TRUE(std::find(pids.begin(), pids.end(), getpid()) != pids.end());
}

class SysinfoUnittest : public testing::Test {};

// Use some CPU time.  Aim for being active for <time> at a cpu usage
// rate of <activity>.  (On a contended system we will get less time.)
void SoakCpuTime(absl::Duration time, double activity) {
  // if we want to work for W and sleep for S s.t. W/(S+W) = activity,
  // then sleep = W * this ratio.
  double ratio = (1 - activity) / activity;
  absl::Duration work_time = time / 100;
  absl::Duration idle_time = work_time * ratio;
  CHECK_GE(work_time, absl::Milliseconds(5));
  CHECK_GE(idle_time, absl::Milliseconds(5));
  for (int i = 0; i < 100; ++i) {
    absl::Time end = absl::Now() + work_time;
    while (absl::Now() < end) {
      // spin!
    }
    absl::SleepFor(idle_time);
  }
}

// Report how much usage <func> claims for this thread doing work.
absl::Duration MeasureUsageInThread(absl::Duration (*func)(),
                                    absl::Duration duration, double activity) {
  absl::Duration before = func();
  SoakCpuTime(duration, activity);
  absl::Duration after = func();
  return after - before;
}

TEST_F(SysinfoUnittest, ThreadCPUUsage) {
#ifndef __linux__
  LOG(WARNING) << "unsupported";
  return;
#endif
  const absl::Duration kDuration = absl::Milliseconds(500);

  // This test is flaky; accept the first successful measurement attempt.
  for (int attempt = 0;; ++attempt) {
    absl::Duration usage = MeasureUsageInThread(ThreadCPUUsage, kDuration, 0.5);
    ASSERT_LT(absl::Nanoseconds(0), usage);
    const absl::Duration kMinUsage = kDuration / 10;
    const absl::Duration kMaxUsage = kDuration * 1.1;
    if ((usage >= kMinUsage && usage <= kMaxUsage) || attempt >= 10) {
      // Ideally we've used kDuration time but it might be less (forge
      // is pretty contended.)  Hopefully we can guarantee at least 10%.
      EXPECT_GE(usage, kMinUsage);
      // This should be tighter - we can't generate extra usage.
      EXPECT_LE(usage, kMaxUsage);
      break;
    }
  }
}

// Computing cpu usage isn't free so we can't expect multiple calls to
// be identical, but we do want both ways to do this to be similar.
// So make sure multiple calls bracket each other.
static absl::Duration CPUUsageMatch() {
  // These should all increase--which means CPUUsage(), CPUUsage(0), and
  // CPUUsage(ourpid) are tracking each other well.
  pid_t pid = getpid();
  absl::Duration a = CPUUsage();
  absl::Duration b = CPUUsage(0);
  absl::Duration c = CPUUsage(pid);
  absl::Duration d = CPUUsage();
  absl::Duration e = CPUUsage(0);
  absl::Duration f = CPUUsage(pid);
  EXPECT_LE(a, b);
  EXPECT_LE(b, c);
  EXPECT_LE(c, d);
  EXPECT_LE(d, e);
  EXPECT_LE(e, f);

  // Pick the last (most accurate, since we've used all that time).
  return f;
}

TEST_F(SysinfoUnittest, CPUUsageInThread) {
#ifndef __linux__
  LOG(WARNING) << "unsupported";
  return;
#endif
  const absl::Duration kDuration = absl::Milliseconds(500);
  // As with ThreadCPUUsage, we should be charged for our own time...

  // This test is flaky, so we try multiple times to measure an
  // acceptable value.
  for (int attempt = 0;; ++attempt) {
    absl::Duration usage = MeasureUsageInThread(CPUUsageMatch, kDuration, 0.5);
    ASSERT_LT(absl::Nanoseconds(0), usage);
    EXPECT_LE(kDuration / 10, usage);
    const absl::Duration kMaxUsage = kDuration * 1.1;
    if (usage <= kMaxUsage || attempt >= 10) {
      EXPECT_LE(usage, kMaxUsage);
      break;
    }
  }
}

TEST_F(SysinfoUnittest, ProcessStartTime) {
#ifndef __linux__
  LOG(WARNING) << "unsupported";
  return;
#endif
  absl::Time start_time = base::ProcessStartTime();
  // Process start time should be before the current time.
  EXPECT_LT(start_time, absl::Now());
  // And also before InitGoogle() is called. We make use of the WallTimer
  // started in InitGoogle(), referenced by GetUptime().
  absl::Duration time_pre_initgoogle =
      absl::Now() - absl::Milliseconds(absl::ToInt64Milliseconds(GetUptime())) -
      start_time;
  EXPECT_GT(time_pre_initgoogle, absl::ZeroDuration());
  // Being very generous, the time between process start and InitGoogle() should
  // be less than 1 hour.
  EXPECT_LT(time_pre_initgoogle, absl::Hours(1));
}

std::string* cmdline = nullptr;
void SaveCommandLine(int argc, char* argv[]) {
  // Save the command line
  cmdline = new std::string;
  for (int i = 0; i < argc; i++) {
    *cmdline += argv[i] + std::string("\0", 1);
  }
}

const std::string& GetCommandLine() { return *cmdline; }
// These are all very old tests that need attention and love.
// TODO: split out each function into a test case here, update it
// to modern standards.
TEST_F(SysinfoUnittest, AntiquatedTests) {
  absl::SetVLogLevel("sysinfo", 3);

  TestHasPosixThreads();

#ifdef __linux__
  TestParseFunctions();
  TestMemoryUsage();
  TestProcessState();
  TestThreads();
  TestSystemState();
  TestCommandLine(GetCommandLine());
  TestGetIdleTime();
  TestReadProcMap();

  TestProcessPageFaults();

  TestProcMaps();
#endif

  // Check that the error logging only prints three times for each
  // distinct error file
  for (int i = 0; i < 4; i++) {
    ThreadGroup(-1);
    ThreadGroup(-2);
    MemoryUsage(-1);
  }

  // Check misc functions in sysinfo
  CHECK_GT(BootTime(), 0);
  TestProcessList();
}

TEST(ProcessState, GetUptimeInMs) {
  // It takes about 7 ms to get here, if we are here in 1 us, something is
  // wrong.
  const double kMicrosecond = 1e-6;
  EXPECT_LT(kMicrosecond, absl::ToInt64Milliseconds(GetUptime()));
  // We must get here faster than an hour.
  const double kHourMs = 3600 * 1000;
  EXPECT_GT(kHourMs, absl::ToInt64Milliseconds(GetUptime()));
}

TEST(ProcessState, GetInitGoogleTime) {
  EXPECT_GT(GetInitGoogleTime(), absl::Now() - absl::Hours(1));
  EXPECT_LE(GetInitGoogleTime(), absl::Now());
}

TEST(ParseProcessStatTest, StatPid) {
  absl::StatusOr<base::ParsedProcessStat> parsed =
      ParseProcessStat("/proc/self/stat", -1);
  ASSERT_THAT(parsed, IsOk());
  static constexpr int kPidFieldIdx = 0;  // From `man 5 proc`.
  EXPECT_THAT(parsed->GetSignedIntField(kPidFieldIdx),
              IsOkAndHolds(Eq(getpid())));
}

class ScopedProcessName {
 public:
  explicit ScopedProcessName(const std::string& name) {
    static constexpr size_t kNameLen = 16;  // From `man pthread_getname_np`.
    char old_buf[kNameLen];
    pthread_getname_np(pthread_self(), old_buf, kNameLen);
    old_name_ = old_buf;
    pthread_setname_np(pthread_self(), name.c_str());
  }
  ~ScopedProcessName() {
    pthread_setname_np(pthread_self(), old_name_.c_str());
  }

 private:
  std::string old_name_;
};

TEST(ParseProcessStatTest, StatComm) {
  ScopedProcessName scoped_process_name(") )(ab");
  absl::StatusOr<base::ParsedProcessStat> parsed =
      ParseProcessStat("/proc/self/task/%d/stat", GetTID());
  ASSERT_THAT(parsed, IsOk());
  EXPECT_THAT(parsed->GetComm(), IsOkAndHolds(Eq("() )(ab)")));
}

TEST(ParseProcessStatTest, StatState) {
  // Try to trick the parser into reading state Z by making the stat line start:
  // <pid> () Z ) ...
  ScopedProcessName scoped_process_name(") Z ");
  absl::StatusOr<base::ParsedProcessStat> parsed =
      ParseProcessStat("/proc/%d/stat", 0);
  ASSERT_THAT(parsed, IsOk());
  EXPECT_THAT(parsed->GetState(), IsOkAndHolds(Eq('R')));
}

}  // namespace

int main(int argc, char** argv) {
  // To support testing AvailableCPUs(), which is computed once per process.
  if (argc == 2 && !strcmp(argv[1], "--show_available_cpus")) {
    printf("%d\n", base::AvailableCPUs());
    return 0;
  }

  SaveCommandLine(argc, argv);

  InitGoogle(argv[0], &argc, &argv, true);

#ifdef __linux__
  // Check that idle time close to beginning of program is close to 0.
  // Allow more idle time if there are more CPUs.
  const double max_idle_time = 1.0 * NumCPUs();
  CHECK_LT(GetIdleTime(), max_idle_time);
#endif

  // Note: this cannot use benchmark::RunSpecifiedBenchmarksThenExit() because
  // it may be link with the external library.
  // FIXME(vyng): Fix this once we've moved the wrapper out of the internal
  // header.
  if (!benchmark::GetBenchmarkFilter().empty()) {
    benchmark::RunSpecifiedBenchmarks();
    exit(0);
  }

  // Run the gUnit tests
  int ret = RUN_ALL_TESTS();
  free(example_heap_address);
  return ret;
}
