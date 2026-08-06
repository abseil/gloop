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

#include "gloop/base/init_google.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>

#include "absl/base/config.h"
#include "absl/base/internal/scheduling_mode.h"
#include "absl/base/log_severity.h"
#include "absl/base/macros.h"
#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/flags/internal/program_name.h"
#include "absl/flags/parse.h"
#include "absl/log/vlog_is_on.h"
#include "gloop/base/config.h"

// TODO: Migrate this to either base/config.h or
// absl/base/config.h once it gains more use.  prctl() is used
// in several places across google3, so this should be fairly straight
// forward.  It's also a well-defined interface on Linux.
//
// GOOGLE_HAVE_PRCTL is defined when the system supports the prctl(2)
// system call and provides the <sys/prctl.h> header file.
#ifdef __linux__
#define GOOGLE_HAVE_PRCTL
#endif

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include <algorithm>  // for min
#include <exception>  // for terminate_handler
#include <set>
#include <string>
#include <typeinfo>
#include <vector>

// You're not allowed to use anything outside of base/ here (otherwise
// the library you use might as well be part of base!).  So, no strutil
// or other handy things -- you get to do that sort of thing "by hand".

#include "absl/base/attributes.h"
#include "absl/base/call_once.h"
#include "absl/base/const_init.h"
#include "absl/base/thread_annotations.h"
#include "absl/debugging/internal/demangle.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/flag.h"
#include "absl/flags/usage.h"
#include "absl/log/check.h"
#include "absl/log/internal/globals.h"
#include "absl/log/log.h"
#include "absl/log/log_sink_registry.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/commandlineflags.h"
#include "gloop/base/examine_stack.h"
#include "gloop/base/googleinit.h"
#include "gloop/base/init_google_flags.h"
#include "gloop/base/internal/init_google.h"
#include "gloop/base/internal/logging_globals.h"
#include "gloop/base/logging.h"
#include "gloop/base/raw_logging.h"
#include "gloop/base/spinlock.h"
#include "gloop/base/sysinfo.h"
#include "gloop/base/user_name.h"
#include "tcmalloc/malloc_extension.h"

#ifdef ABSL_HAVE_MMAP
#include <sys/mman.h>
#endif

#ifdef GOOGLE_HAVE_PRCTL
#include <sys/prctl.h>
#endif

#ifdef GOOGLE_ENABLE_SETGID
#include <grp.h>
#endif

#if BASE_HAVE_PROCESS_STATE
#include "gloop/base/process_state.h"
#endif  // BASE_HAVE_PROCESS_STATE

#if !PORTABLE_BASE
#include "gloop/concurrent/rcu/rcu.h"

#endif  // !PORTABLE_BASE

#if BASE_HAVE_THREAD_STACK
#else
inline void GoogleInitMainStackLimits() {}
#endif

#if GOOGLE_ENABLE_SYSLOG
#endif  // GOOGLE_ENABLE_SYSLOG

#if BASE_HAVE_CPU_PROFILER
#endif  // BASE_HAVE_CPU_PROFILER

#if !defined(_WIN32) && !defined(__myriad2__)
#include "gloop/base/nsscache.h"
#endif  // !_WIN32 && !__myriad2__

// Controls the default of the --uid flag.
#ifdef BASE_CONFIG_DEFAULT_UID_FLAG
#error BASE_CONFIG_DEFAULT_UID_FLAG cannot be directly set
#elif defined(__linux__) && !defined(__ANDROID__)
#define BASE_CONFIG_DEFAULT_UID_FLAG ""  // Means 'do not change uid'
#endif

ABSL_FLAG(std::string, uid, BASE_CONFIG_DEFAULT_UID_FLAG,
          "If root, switch to this user id "
          "(or empty-string not to switch)");
ABSL_FLAG(std::string, gid, "=uid",
          "If root, switch to this group id "
          "(matches --uid unless overridden)");

ABSL_FLAG(int32_t, nice_priority_level, 0,
          "Nice priority level of the program");

ABSL_FLAG(bool, syslog_on_start, true,
          "syslog() on program start-up.  We collect these logs to "
          "identify used (vs obsolete) binaries");

// TODO: This should move somewhere reasonable (selectserver.cc)
ABSL_FLAG(std::string, data_version, "", "Data version of the server");

#if defined(__ANDROID__)
#define mlockall(flags) false
#endif  // defined(__ANDROID__)

// Operating system name used for diagnostic error messages.  Only
// defined for operating systems for which errors are reported.
#if defined(COMPILER_MSVC)
static const char kOsName[] = "Windows";
#endif

// Lock for the globals below.
ABSL_CONST_INIT static SpinLock global_lock(
    absl::base_internal::SCHEDULE_KERNEL_ONLY);
static enum {
  BEFORE_INIT_GOOGLE = 0,
  INIT_GOOGLE_RUNNING,
  INIT_GOOGLE_DONE
} init_google_state = BEFORE_INIT_GOOGLE;
namespace {

// 'Plain' atomic to indicate if InitGoogle has been done.
//
// 'absl::Notification` implies a causality, i.e., calling `HasBeenNotified()`
// implies that the caller is explicitly making a causal decision to invoke
// some logic conditional on the output, which is recorded in Dapper as a
// causality event that can be tracked back to the original execution where the
// notification was notified.
//
// `IsInitGoogleDone()` doesn't fit this use case as the caller typically either
// asserts or checks that google init has been done as a requirement, and there
// is no useful case to record this as a causality to 'start of program`.
ABSL_CONST_INIT std::atomic<bool> init_google_done(false);

// TODO: b/399660994 - we could simply use `absl::Mutex::Await`, but as per the
// linked bug, linking in `absl::Notification` has load bearing side effects
// resulting in erroneous backref linking errors when removing it.
absl::Notification* InitGoogleDoneNotification() {
  static absl::NoDestructor<absl::Notification> result;
  return result.get();
}

}  // namespace

#if GOOGLE_ENABLE_CHROOT
static bool IsChrootUser() {
  std::string username = MyUserName();
  if (username == "root") {
    return true;
  }
  return false;
}
#endif  // GOOGLE_ENABLE_CHROOT

#if GOOGLE_ENABLE_SETUID
static bool IsSetuidUser(absl::string_view new_username) {
  std::string username = MyUserName();

  if (username == "root") return true;

  return false;
}
#endif

// setgid privileges are highly restricted for security
// reasons. Currently, only root can do it.
#if defined(GOOGLE_ENABLE_SETGID)
static bool IsSetgidUser() {
  const uid_t uid = geteuid();
  if (uid == 0) return true;  // root can do it

  return false;
}
#endif  // GOOGLE_ENABLE_SETGID

// If running as a setuid-capable user, this routine switches the user
// id to the specified "username".  If the current uid does not have
// enough capabilities, request is ignored.  Various logging messages
// are generated, and if information about username cannot be obtained,
// the program will abort with a fatal error.
static void SwitchUser(const std::string& username) {
#if GOOGLE_ENABLE_SETUID
  uid_t new_uid;
  CHECK(LookupUIDByName(username, &new_uid))
      << " User " << username << " not found";

  if (!absl::GetFlag(FLAGS_silent_init)) {
    VLOG(1) << "Attempt to change userid from " << MyUserName() << ":"
            << getuid() << " to: " << username << ":" << new_uid;
  }
  if (IsSetuidUser(username)) {  // Enough capabilities to switch UIDs?
#ifdef GOOGLE_HAVE_PRCTL
    // Per prctl(2) operations such as dropping privileges reset process
    // dumpable value (default 1) to the value of suid_dumpable (default 0).
    // This is a good feature in general to avoid binaries started as root and
    // using setuid to be dumpable, as they may hold secrets or have shared
    // memory that we do not want the target user to access.
    // However in ChangeRootAndUser/SwitchUser/SwitchGroup case we assume that:
    //  - the root process does not hold anything sensitive
    //  - other processes running as the target uid are not a threat
    // This is not necessarily true for other operations, such as subprocesses.
    // Therefore, we ensure to keep dumpable if it was set before.
    const int dumpable_before = prctl(PR_GET_DUMPABLE, 0);
#endif

    PCHECK(setuid(new_uid) != -1) << ": Failed to setuid to " << new_uid;

#ifdef GOOGLE_HAVE_PRCTL
    if (dumpable_before) prctl(PR_SET_DUMPABLE, 1);
#endif

    if (!absl::GetFlag(FLAGS_silent_init)) {
      VLOG(1) << "Changed to user " << username;
    }
    if ("nobody" == username) {
      LOG(ERROR) << "Switching to user nobody, LOAS will not work"
                 << " (consider using --uid=)";
    }
  } else {
    if (!absl::GetFlag(FLAGS_silent_init)) {
      VLOG(1) << "UID " << getuid() << " does not have enough capabilities "
              << "for setuid() call to user " << username << ". Skipping";
    }
  }
#else   // GOOGLE_ENABLE_SETUID
  if (!absl::GetFlag(FLAGS_silent_init)) {
    VLOG(1) << "Running on " << kOsName << ".  No attempt to setuid to user "
            << username;
  }
#endif  // !GOOGLE_ENABLE_SETUID
}

// If running as a setgid-capable user, this routine switches the
// group id to the specified "group". If uid is not empty, also
// adds the user's supplementary groups from /etc/group to the access list. If
// the current uid does not have enough capabilities, request is ignored.
// Various logging messages are generated, and if information about username
// cannot be obtained, the program will abort with a fatal error.
static void SwitchGroup(const std::string& groupname, const std::string& uid) {
#if defined(GOOGLE_ENABLE_SETGID)
  const int kMaxTries = 48;

  gid_t new_gid;
  CHECK(LookupGIDByGroupName(groupname, &new_gid))
      << " Group " << groupname << " not found";

  if (!absl::GetFlag(FLAGS_silent_init) && VLOG_IS_ON(1)) {
    std::string group;
    int i = 0;
    while (true) {
      if (LookupGroupNameByGID(getgid(), &group)) break;

      if (++i >= kMaxTries) {
        LOG(ERROR) << "Could not successfully call getgrgid after " << kMaxTries
                   << " attempts.";
        break;
      }

      VLOG(1) << "getgrgid failed on try " << i
              << " - sleeping for 200ms and retrying";

      // We're going to sleep for a while because we don't want to suck
      // up all the processor.  We did notice that sleeping doesn't necessarily
      // help getgrgid() succeed any faster.  It would generally fail for
      // the same number of times before succeeding whether we were sleeping
      // for 1 second or not at all.
      timespec ts;
      ts.tv_sec = 0;
      ts.tv_nsec = 200000000;  // 200 ms
      nanosleep(&ts, nullptr);
    }

    std::string old_group = (i < kMaxTries) ? group : "\"unknown\"";
    VLOG(1) << "Attempt to change groupid from " << old_group << ":" << getgid()
            << " to " << groupname << ":" << new_gid;
  }

  if (IsSetgidUser()) {  // Enough capabilities to switch groups?
#ifdef GOOGLE_HAVE_PRCTL
    const int dumpable_before = prctl(PR_GET_DUMPABLE, 0);  // cf. SwitchUser
#endif                                                      // GOOGLE_HAVE_PRCTL

    if (!uid.empty()) {
      // initgroups gives access to user's supplementary groups.
      PCHECK(initgroups(uid.c_str(), new_gid) == 0)
          << "initgroups(" << uid << ", " << new_gid << ") failed";
    } else {
      // Restrict access to just the specified group.
      gid_t group_list[] = {new_gid};
      int group_count = std::size(group_list);
      PCHECK(setgroups(group_count, group_list) == 0)
          << "setgroups(" << group_count << ", {" << new_gid << "}) failed";
    }
    PCHECK(setgid(new_gid) == 0) << "setgid(" << new_gid << ") failed";

#ifdef GOOGLE_HAVE_PRCTL
    if (dumpable_before) prctl(PR_SET_DUMPABLE, 1);
#endif  // GOOGLE_HAVE_PRCTL

    if (!absl::GetFlag(FLAGS_silent_init)) {
      VLOG(1) << "Changed to group " << groupname;
    }
  } else {
    if (!absl::GetFlag(FLAGS_silent_init)) {
      VLOG(1) << "UID " << getuid() << " does not have enough capabilities "
              << "for setgid() call. NOT changing to group " << groupname;
    }
  }
#else  // GOOGLE_ENABLE_SETGID
  if (!absl::GetFlag(FLAGS_silent_init)) {
    VLOG(1) << "Running on " << kOsName << ".  No attempt to change to group "
            << groupname;
  }
#endif
}

// If running as root, change our root directory to the given
// directory. Return true if and only if we changed the root
// directory. Don't log anything in this function if we're successful,
// because that would create a file and symlink owned by root inside
// our chroot jail.
static bool ChangeRoot(const char* root_dir) {
#if GOOGLE_ENABLE_CHROOT
  if (!absl::GetFlag(FLAGS_silent_init)) {
    VLOG(1) << "Attempt to chroot with uid:" << getuid() << MyUserName();
  }
  bool erase = false;
  if (!strcmp("env", root_dir)) {
    // use environment variable $CHROOT instead, and erase it
    erase = true;
    root_dir = getenv("CHROOT");
    CHECK(root_dir) << ": --chroot=env, but no $CHROOT variable";
  }

  if (IsChrootUser()) {
    // The chroot() will fail if root_dir is not an accessible directory.
    PCHECK(chroot(root_dir) == 0) << "Failed to chroot to " << root_dir;
    // We have to chdir, or else "." will be outside the new root.
    PCHECK(chdir("/") == 0) << "Failed to chdir to /";

    if (erase) {
      // Mangle the environment variable so there is no record in our
      // process's memory of where we chrooted to
      memset(const_cast<char*>(root_dir), 0, strlen(root_dir));
    }

    return true;
  } else {
    if (!absl::GetFlag(FLAGS_silent_init)) {
      VLOG(1) << "Not running with correct permissions. uid:" << getuid()
              << " No attempt to change root directory to " << root_dir;
    }
  }
#else
  if (!absl::GetFlag(FLAGS_silent_init)) {
    VLOG(1) << "Running on " << kOsName << ".  No attempt to chroot to "
            << root_dir;
  }
#endif  // GOOGLE_ENABLE_CHROOT
  return false;
}

void ChangeRootAndUser() {
  // Make sure to keep the ChangeRoot() call before the
  // SwitchUser/SwitchGroup() calls, because we need high-level
  // capabilities to call chroot().
  bool changed_root = false;
  std::string chroot_flag = absl::GetFlag(FLAGS_chroot);
  if (!chroot_flag.empty()) {
    changed_root = ChangeRoot(chroot_flag.c_str());
  }

  // Make sure we keep the SwitchUser/Group() calls right after the
  // ChangeRoot() call. We don't want to put anything root-ish inside
  // our chroot jail.
  if (absl::GetFlag(FLAGS_gid) == "=uid") {
    absl::SetFlag(&FLAGS_gid, absl::GetFlag(FLAGS_uid));
    if (!absl::GetFlag(FLAGS_gid).empty()) {
      SwitchGroup(absl::GetFlag(FLAGS_gid), absl::GetFlag(FLAGS_uid));
    }
  } else if (!absl::GetFlag(FLAGS_gid).empty()) {
    SwitchGroup(absl::GetFlag(FLAGS_gid), "");
  }

  // We are ready to drop our privileges now
  if (!absl::GetFlag(FLAGS_uid).empty()) SwitchUser(absl::GetFlag(FLAGS_uid));

  // If we were to log this message inside ChangeRoot, we'd create a
  // log file and symlink to it that were owned by root.
  if (!absl::GetFlag(FLAGS_silent_init) && changed_root) {
    VLOG(1) << "Changed root directory to " << chroot_flag;
  }
}

// The default terminate() handler set up by the standard C++ library. This is
// called when an unhandled exception is thrown.  The default handler (at least
// with libstdc++ in our Linux crosstool builds) prints some basic exception
// information and then abort()s which triggers our signal handler but with an
// unwalkable mangled stack.
static std::terminate_handler old_terminate_handler = nullptr;

// A terminate() handler called when an unhandled exception is thrown. We try
// to print a useful stack trace before aborting. This is installed by
// RealInitGoogle(), but does its work only for Linux builds at present,
// just to be paranoid, even though it shouldn't hurt in Windows and Mac builds.
// It is implementation-defined whether the stack is unwound before the
// terminate() handler is called but currently, with crosstool v12, good
// stacktraces are produced and this is no worse than the useless traces that
// would be printed without this handler. The alternative would be to provide
// a strong definition of __cxa_throw() but set_terminate() is more portable.
static void GoogleTerminateHandler() {
#if defined(__linux__)
  DebugWriteToStderr("Unhandled exception:\n", nullptr);
  DumpStackTrace(0, DebugWriteToStderr, nullptr);

  // Don't print a (useless) stacktrace again when the
  // standard handler abort()s.
  absl::log_internal::SetSuppressSigabortTrace(true);
#endif  // defined(__linux__)

  // Now defer to the original terminate() handler, which
  // prints some exception info and abort()s.
  (*old_terminate_handler)();
}

// Record the time we started, or at least when InitGoogle was called
static char g_start_time[64] = "";  // enough to hold a time
const char* GetStartTime() { return g_start_time; }

#if _WIN32
// Called via atexit() call in InitGoogle
void CleanupWinsock() { WSACleanup(); }
#endif

// Flag for checking in the fake command_line_flags_parsing initializer below.
// RealInitGoogle below sets it when it causes the initializer to run.
static bool doing_command_line_flags_parsing = false;

REGISTER_MODULE_INITIALIZER(command_line_flags_parsing, {
  CHECK(doing_command_line_flags_parsing)
      << "Can't call REQUIRE_MODULE_INITIALIZED(command_line_flags_parsing):"
         " InitGoogle does it for you.";
  // Do nothing: RealInitGoogle below ensures that things registered to run
  // before command line parsing do that
});

// As above, for the command_line_flags_parsed.
static bool after_command_line_flags_parsing = false;

REGISTER_MODULE_INITIALIZER(command_line_flags_parsed, {
  CHECK(after_command_line_flags_parsing)
      << "Can't call REQUIRE_MODULE_INITIALIZED(command_line_flags_parsed):"
         " InitGoogle does it for you.";
  // Do nothing: RealInitGoogle below ensures that things registered to run
  // immediately after command line parsing do that
});

// Helper function for possible mlock_style flag values.
namespace base {
namespace internal {

bool IsValidMlockStyle(absl::string_view style) {
  if (style.empty() || style == "all" || style == "executable" ||
      style == "executable-hot" || style == "executable-all" ||
      style == "none" || style == "startup") {
    return true;
  }
  return false;
}

}  // namespace internal
}  // namespace base

#if defined(ABSL_HAVE_ADDRESS_SANITIZER) || defined(DATAFLOW_SANITIZER) || \
    defined(HWADDRESS_SANITIZER) || defined(ABSL_HAVE_MEMORY_SANITIZER) || \
    defined(ABSL_HAVE_THREAD_SANITIZER)

// We need a weak definition because we want to override when linking in
// testing/*san/allocator.cc.
// Note that this is defined (as opposed to just being declared) because the
// Apple linker requires a definition of all functions at link time.
// This is not a problem on Linux.
// <link>
extern "C" ABSL_ATTRIBUTE_WEAK void __sanitizer_allocator_init_once() {}

// Force sanitizer registration. See b/33837666.
static void InitializeSanitizers() { __sanitizer_allocator_init_once(); }
#else
static void InitializeSanitizers() {}  // No-op without sanitizers
#endif

// Initializes on demand CPU profiling configured by the --cpu_profile flag or
// CPUPROFILE environment variable. The implementation is weakly provided
// by //gloop/util/profiling/flags package. The weak linking avoids a build
// dependency from base to protobuf.
extern "C" ABSL_ATTRIBUTE_WEAK void RegisterCpuProfiler() {}

// Initializes on demand wall profiling configured by the --wall_profile flag.
// The implementation is weakly provided by
// //gloop/util/profiling/wallprofiler.cc.
extern "C" ABSL_ATTRIBUTE_WEAK void RegisterWallProfiler() {}

// Try to syslog() on program start-up.  We collect these logs to
// identify used (vs obsolete) binaries.
//
// Please add someone from //devtools/toolindex/OWNERS as reviewer for any
// changes.
static void MaybeSyslogOnStart() {
#if GOOGLE_ENABLE_SYSLOG
  if (!absl::GetFlag(FLAGS_syslog_on_start)) return;

#endif  // GOOGLE_ENABLE_SYSLOG
}

static void RealInitGoogle(absl::string_view usage, int* argc, char*** argv,
                           bool remove_flags, bool do_chroot) {
  GoogleInitMainStackLimits();
  // absl::InitializeSymbolizer() calls VDSOSupport::Init(), which must be
  // called before any setuid or chroot calls.
  absl::InitializeSymbolizer((*argv)[0]);
  int previous_init_google_state;
  {
    SpinLockHolder l(global_lock);
    previous_init_google_state = init_google_state;
    init_google_state = INIT_GOOGLE_RUNNING;
  }
  // Allowed only once, but mustn't LOG while holding global_lock!
  CHECK_EQ(previous_init_google_state, BEFORE_INIT_GOOGLE);

  if (!usage.empty()) absl::SetProgramUsageMessage(usage);

  // This needs to happen before we create any threads. b/33837666.
  InitializeSanitizers();

  // Save the time we were called; probably the program start time
  time_t now;
#if BASE_HAVE_PROCESS_STATE
  base::internal::StartUpWallTimer();
  now = absl::ToTimeT(GetInitGoogleTime());
#else
  now = time(nullptr);
#endif
  char start_time[sizeof(g_start_time)];
#ifdef _WIN32
  PCHECK(ctime_s(start_time, sizeof(start_time), &now) != -1);
#else
  CHECK_EQ(start_time, ctime_r(&now, start_time));
#endif
  absl::SNPrintF(g_start_time, sizeof(g_start_time), "%s", start_time);

  // Trim trailing \n because the concentrator likes that.
  const auto len = strlen(g_start_time);
  CHECK_GT(len, 0U);
  g_start_time[len - 1] = '\0';

#ifdef _WIN32
  // Initialize Winsock (needed for gethostname, for starters)
  WSADATA winsock;
  WSAStartup(MAKEWORD(2, 0), &winsock);
  atexit(CleanupWinsock);
#endif

  // Set up testing environment vars
  absl::string_view test_tmpdir =
      absl::NullSafeStringView(getenv("TEST_TMPDIR"));
  if (!test_tmpdir.empty()) {
    SetCommandLineOption("datadir", test_tmpdir);
  }

  doing_command_line_flags_parsing = true;
  REQUIRE_MODULE_INITIALIZED(command_line_flags_parsing);
  doing_command_line_flags_parsing = false;

  ParseCommandLineNonHelpFlags(argc, argv, remove_flags);

  base_logging::logging_internal::SetLoggingFlagsParsed();

  // This is a no-op since the hooks are installed in a static initializer, but
  // MSVC strips the static initializer because raw_logging.cc exports no called
  // functions, even with alwayslink=1. So we work around this by calling it as
  // a no-op here as well as in the static initializer.
  base_raw_log::raw_log_internal::InstallGoogle3Hooks();

  // Initialize the base_logging library. This optionally returns a function
  // to be called at the very end of InitGoogle() to complete initialization.
  //
  // Do not write to LOG(XXX) before base_logging::Initialize().  The
  // output may not go where you expect.
  base_logging::InitializeCallback end_of_init_google_logging_init =
      base_logging::Initialize();

  // Check if there are flags that ask us to print something and exit.
  // Do that early to avoid unnecessary logging (and other unnecessary work)
  // below in case we are going to exit anyway.
  const absl::flags_internal::HelpMode help_mode =
      absl::flags_internal::HandleUsageFlags(std::cout,
                                             absl::ProgramUsageMessage());
  absl::flags_internal::MaybeExit(help_mode);

  after_command_line_flags_parsing = true;
  REQUIRE_MODULE_INITIALIZED(command_line_flags_parsed);
  after_command_line_flags_parsing = false;

#if BASE_HAVE_PROCESS_STATE
  // Read and parse the kernel version from file.
  base::internal::GetKernelVersionInfo();
  if (!absl::GetFlag(FLAGS_silent_init)) {
    LOG(INFO) << GetKernelVersionString();
  }
#endif

  if (!absl::GetFlag(FLAGS_silent_init)) {
    std::string enabled;
    // Use GetProperties to minimize dependencies, since init_google.cc needs to
    // compile for a wide variety of environments.  Elsewhere, use
    // WalkExperiments instead.
    for (const auto& [name, val] : tcmalloc::MallocExtension::GetProperties()) {
      constexpr absl::string_view prefix = "tcmalloc.experiment.";
      if (val.value && absl::StartsWith(name, prefix)) {
        absl::StrAppend(&enabled, (enabled.empty() ? "" : ","),
                        name.substr(prefix.size()));
      }
    }
    if (!enabled.empty()) {
      LOG(INFO) << "Enabled <link> experiments: " << enabled;
    }
  }

  if (absl::GetFlag(FLAGS_nice_priority_level) != 0) {
#if GOOGLE_ENABLE_NICE
    // Increase the nice priority level by the amount given.
    // positive values decrease the priority.
    errno = 0;
    PCHECK(nice(absl::GetFlag(FLAGS_nice_priority_level)) != -1 || errno == 0)
        << "Could not change the nice priority level by "
        << absl::GetFlag(FLAGS_nice_priority_level);
    if (!absl::GetFlag(FLAGS_silent_init)) {
      LOG(INFO) << "Changed the nice priority level by "
                << absl::GetFlag(FLAGS_nice_priority_level);
    }
#else
    if (!absl::GetFlag(FLAGS_silent_init)) {
      VLOG(1) << "Running on " << kOsName << ".  No attempt to set nice to "
              << absl::GetFlag(FLAGS_nice_priority_level);
    }
#endif
  }

  if (do_chroot) ChangeRootAndUser();

  if (!absl::GetFlag(FLAGS_silent_init)) {
    LOG(INFO) << "Process id " << getpid();

    LOG(INFO) << "Current timezone is "
              << absl::FormatTime("%Z (currently UTC %Ez)", absl::Now(),
                                  absl::LocalTimeZone());

#if !defined(NDEBUG)
    LOG(WARNING) << "DEBUG BINARY -- Performance may suffer";
#endif

    const std::vector<std::string>& argvs = base::GetArgvs();
    const size_t argvs_size = argvs.size();
    LOG(INFO) << "Command line arguments:";
    for (size_t i = 0; i < argvs_size; ++i) {
      LOG(INFO) << "argv[" << i << "]: '" << argvs[i] << "'";
    }
  }

  // Set a handler for unhandled C++ exceptions that prints a
  // sensible stack trace.
  old_terminate_handler = std::set_terminate(&GoogleTerminateHandler);

#if GOOGLE_ENABLE_SIGNAL_HANDLERS
  InstallSignalHandlers();
#endif  // GOOGLE_ENABLE_SIGNAL_HANDLERS

#ifdef M_MMAP_MAX              // probably means mallopt() is defined
  mallopt(M_MMAP_MAX, 16384);  // Enables access to more than 1 GB
#endif
  absl::FlushLogSinks();  // so you can see command line early

#if !PORTABLE_BASE
  // Initialize RCU domains.
  base::rcu::DomainInit();
#endif

#if BASE_HAVE_CPU_PROFILER
  // Initialize on demand CPU profiling, see comment to RegisterCpuProfiler.
  RegisterCpuProfiler();

#endif  // BASE_HAVE_CPU_PROFILER

  // Initialize on demand wall profiling, see comment to RegisterWallProfiler.
  RegisterWallProfiler();

  // Run registered initializers
  RUN_MODULE_INITIALIZERS();

  MaybeSyslogOnStart();

#if !PORTABLE_BASE
  // TODO: clean up references to old heap checker interface.
#endif

  absl::SetFlag(&FLAGS_silent_init, false);  // init is over now
  {
    SpinLockHolder l(global_lock);
    init_google_state = INIT_GOOGLE_DONE;
  }

  if (end_of_init_google_logging_init != nullptr) {
    (*end_of_init_google_logging_init)();
  }
#if BASE_HAVE_PROCESS_STATE
  // Report that all InitGoogle-related tasks have completed.
  base::process_state::ReportChange(base::process_state::kInitGoogleDone,
                                    nullptr);
#endif
  init_google_done.store(true, std::memory_order_release);
  InitGoogleDoneNotification()->Notify();
}

void base::InitGoogleState::Wait() {
  if (!IsDone()) {
    InitGoogleDoneNotification()->WaitForNotification();
  }
}

bool base::InitGoogleState::IsDone() {
  // See `init_google_done` for motivation of the atomic check.
  return init_google_done.load(std::memory_order_acquire);
}

// Protects "AllowlistedItem::seen"
ABSL_CONST_INIT static absl::Mutex allowlist_mu(absl::kConstInit);

namespace {

enum AllowlistAction {
  // Update usage of enum numbers in init_google_test.cc if you change this.
  kNoOp = 0,
  kInfo = 1,
  kError = 2,
  kFatal = 3
};

// A string matching a allowlisted call to CheckInitGoogleIsDone()
struct AllowlistedItem {
  absl::string_view item;  // string to match against
  char action;             // default action to take  (see AllowlistAction)
  bool seen ABSL_GUARDED_BY(allowlist_mu);  // entry has already been seen
};

}  // namespace

// Private declaration between base/init_google.cc and base/googleinit.cc
void GoogleInitializerGetModuleRunning(std::set<absl::string_view>* running);

void CheckInitGoogleIsDone(absl::string_view message) {}

void AssertInitGoogleIsDone() {
#ifndef NDEBUG
  global_lock.lock();
  bool done = (init_google_state == INIT_GOOGLE_DONE);
  global_lock.unlock();
  // outside lock
  CHECK(done) << "InitGoogle() has not executed yet.  " << CurrentStackTrace();
#endif  // NDEBUG
}

void InitGoogle(absl::string_view usage, int* absl_nonnull argc,
                char* absl_nullable* absl_nonnull* absl_nonnull argv,
                bool remove_flags) {
  RealInitGoogle(usage, argc, argv, remove_flags, true /* do_chroot */);
}

void InitGoogle(std::nullptr_t /*usage*/, int* absl_nonnull argc,
                char* absl_nullable* absl_nonnull* absl_nonnull argv,
                bool remove_flags) {
  return InitGoogle(/*usage=*/absl::string_view(), argc, argv, remove_flags);
}

void InitGoogle(const char* absl_nullable usage, int* absl_nonnull argc,
                char* absl_nullable* absl_nonnull* absl_nonnull argv,
                bool remove_flags) {
  return InitGoogle(absl::NullSafeStringView(usage), argc, argv, remove_flags);
}

extern "C" void InitGoogleExportedForColab(
    const char* absl_nullable usage, int* absl_nonnull argc,
    char* absl_nullable* absl_nonnull* absl_nonnull argv, bool remove_flags) {
  return InitGoogle(usage, argc, argv, remove_flags);
}

void InitGoogleExceptChangeRootAndUser(
    absl::string_view usage, int* absl_nonnull argc,
    char* absl_nullable* absl_nonnull* absl_nonnull argv, bool remove_flags) {
  RealInitGoogle(usage, argc, argv, remove_flags, false /* do_chroot */);
}

// We use some tricks to try and print the class name when a pure virtual
// function is called. In order for this to work, we have to intercept calls to
// the __cxa_pure_virtual runtime library. This isn't portable in any sense of
// the word, so only enable it on Linux.
//
// NOTE: This has binary size implications as implemented as it forces
// init_google.cc and all dependencies to be retained by the linker.
// Since Android reports __linux__, we explicitly disable for __ANDROID__.
//
// This code doesn't really belong here, but moving it to a separate alwayslink
// library is a potentially disruptive change.
// TODO: Move all of this to an alwayslink=1 library or to the actual
// runtime library.
#if defined(__linux__) && !defined(__ANDROID__)
namespace {
// Provides a fake base class against which __cxa_pure_virtual() below will use
// typeid, to hopefully print a class name.
class FakeBaseClass {
  virtual ~FakeBaseClass() {}
  virtual void foo() = 0;
};
}  // namespace

// Handle undefined pure virtual methods so that the last line in the
// crash dump is more useful than simply "Received SIGABRT".
extern "C" {
// Not super-documented, but in the gcc/clang runtime, we get the "this"
// pointer as the first parameter to __cxa_pure_virtual(). Since the mangling
// of __cxa_pure_virtual is C, we can use an asm label to emit our interceptor
// under that symbol despite it having a different signature and type. We
// cannot just call the function '__cxa_pure_virtual' both because that is
// a reserved identifier and because this function's type differs (even though
// that type is irrelevant to its mangling due to being a C function).
extern void google_cxa_pure_virtual(FakeBaseClass* f) __asm__(
    "__cxa_pure_virtual") ABSL_ATTRIBUTE_WEAK;
void google_cxa_pure_virtual(FakeBaseClass* f) {
  char buf[128];
  const char* type_name = "(unknown)";
#if __GXX_RTTI
  type_name = typeid(*f).name();
#endif
  absl::string_view class_name = absl::NullSafeStringView(type_name);

  if (type_name) {
    if (absl::debugging_internal::Demangle(type_name, buf, sizeof(buf) - 1)) {
      class_name = buf;
    }
    LOG(FATAL) << "C++ pure virtual method on class " << class_name
               << " invoked in constructor/destructor or after free; "
               << "see <link>";
  } else {
    LOG(FATAL) << "C++ pure virtual method invoked in constructor or"
               << "in constructor/destructor or after free; "
               << "see <link>";
  }
}
}
#endif  // defined(__linux__)
