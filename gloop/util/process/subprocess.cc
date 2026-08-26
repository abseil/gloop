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

//  This does all of the dirty work required to manage a large number
//  of subprocesses - or even just properly fork and exec a single
//  process.
//
//  See the header file for full documentation.

#include "gloop/util/process/subprocess.h"

#include <errno.h>
#include <fcntl.h>
#include <features.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/auxv.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __i386__
#include <asm/ldt.h>  // For struct user_desc
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ios>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/container/fixed_array.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"  // IWYU pragma: keep
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/log/vlog_is_on.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/commandlineflags.h"
#include "gloop/base/config.h"
#include "gloop/base/file_toc.h"
#include "gloop/base/linux_syscall_support.h"
#include "gloop/base/logging_extensions.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/strerror.h"
#include "gloop/base/sysinfo.h"
#include "gloop/strings/util.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/gtl/unique_array.h"
#include "gloop/util/process/nul_terminated_buf_appender.h"
#include "gloop/util/status/status_builder.h"

// Note, that special accommodations exist to make this code work for Android.
// Specifically:
//
//    1. Android is not a true Linux system and doesn't have glibc.  Code
//       dependent on glibc will not execute on Android.
//       Fortunately, that code exists to work around issues with Linux,
//       therefore no counterpart is necessary for Android.
//    2. Android threads aren't completely POSIX-compliant, but they are
//       compatible with what this library expects of them.
//    3. Android doesn't really support SOCKET_CLOEXEC, therefore this code may
//       still have race conditions on Android.

#if defined(__GLIBC__) && defined(__ARM_EABI__)
extern "C" {
extern void* __aeabi_read_tp();
}
#endif

static bool IsPrivileged();

static const char* GetDefaultSubprocessForkMethod() {
  return StringFromEnv("GOOGLE_SUBPROCESS_FORK_METHOD",
                       IsPrivileged() ? "fork" : "clone");
}

ABSL_FLAG(std::string, subprocess_fork_method, GetDefaultSubprocessForkMethod(),
          "Specify the method used for forking a child process. Valid "
          "choices are 'clone', 'fork'. The 'clone' method shares the "
          "virtual memory of the parent with the child; from a privileged "
          "process this can be a security risk therefore is not allowed, "
          "use the 'fork' method instead");

#if defined(__USE_FILE_OFFSET64) && defined(__linux) && defined(__i386__)
#warning rlim_t has the wrong size on 32 bit linux when _FILE_OFFSET_BITS == 64
#endif

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
//
//        SubProcess implementation.
//
//  This is described extensively in the header file.
//

// --------------------------------------------------

// The system must be POSIX compliant (using NPTL threads), in which
// case every thread reports the same pid and any thread may wait for
// any forked process.  LinuxThreads is no longer supported.

ABSL_CONST_INIT static absl::Mutex subproc_mu(absl::kConstInit);
// protects process_map, using_selectserver, global_init_done,
// and running_ fields of each SubProcess
// > wait_mu_

static enum {
  UNKNOWN,
  CLONE,
  FORK,
} subprocess_fork_method;

static bool ValidateSubprocessForkMethod(const char* flagname,
                                         const std::string& value) {
  if (value == "clone") {
    subprocess_fork_method = CLONE;
    return true;
  } else if (value == "fork") {
    subprocess_fork_method = FORK;
    return true;
  }
  printf("Unsupported --%s: %s\n", flagname, value.c_str());
  return false;
}

static int CloseRange(unsigned int first, unsigned int last, int* local_errno) {
  int ret;
  while ((ret = lss_close_range(first, last, 0, local_errno)) != 0 &&
         *local_errno == EINTR) {
  }
  return ret;
}

static bool DetectCloseRangeAvailable() {
  int local_errno;
  int null_fd;
  // Open /dev/null and try to close it with close_range.
  while ((null_fd = lss_open("/dev/null", O_RDONLY | O_CLOEXEC, 0,
                             &local_errno)) < 0 &&
         local_errno == EINTR) {
  }
  if (null_fd < 0) {
    return false;
  }
  if (CloseRange(null_fd, null_fd, &local_errno) != 0) {
    int ret;
    while ((ret = lss_close(null_fd, &local_errno)) != 0 &&
           local_errno == EINTR) {
    }
    return false;
  }
  return true;
}

// The use of an atomic here is purely to make TSAN happy; it is all but
// impossible for this to be accessed when it's uninitialized. The alternative
// of using a bool under subproc_mu either requires excessive plumbing (as it
// would need to be copied for use by the forked child) or risks new contention
// in performance-sensitive code.
ABSL_CONST_INIT static std::atomic<bool> is_close_range_available = {false};

struct SubProcess::ChildArgs {
  struct pthread* tid;
  SubProcess* subprocess;
  struct kernel_sigset_t old_signals;
  int openmax;
  pid_t ppid;
};

static const int kMaxErrMsgSize = 1024;
static const int kMaxInfoBufSize = 1024;
static const int kNumDirentScratchEntries = 16;

struct SubProcess::ChildBuffers {
  explicit ChildBuffers(int num_chan, bool needs_dirent_scratch)
      : cmsg_buf(CMSG_SPACE(num_chan * sizeof(int)), 0),
        parent_fds(num_chan, -1),
        dirent_scratch(needs_dirent_scratch
                           ? new kernel_dirent64[kNumDirentScratchEntries]
                           : nullptr,
                       needs_dirent_scratch ? kNumDirentScratchEntries : 0) {
    errmsg_buf[0] = '\0';
    info_msgs[0] = '\0';
  }

  char errmsg_buf[kMaxErrMsgSize];
  char info_msgs[kMaxInfoBufSize];
  std::vector<char> cmsg_buf;
  std::vector<int> parent_fds;
  gtl::UniqueArray<kernel_dirent64> dirent_scratch;
};

// --------------------------------------------------

//  Map from running process ids to SubProcess objects.  Entries are
//  put in this when a process is started.  They are removed when it
//  exits, by HandleExit().

//  If a SubProcess object is marked "abandoned" and destroyed
//  before the child process exits, it leaves a NULL pointer in the
//  process map.
namespace {
typedef absl::flat_hash_map<pid_t, SubProcess*> ProcessMap;
};
static ProcessMap* process_map;  // under subproc_mu

static bool using_selectserver = false;  // under subproc_mu
static bool global_init_done = false;    // under subproc_mu

// L < subproc_mu
void SubProcess::UsingSelectServer() {
  absl::MutexLock l(subproc_mu);
  using_selectserver = true;
}

// L < subproc_mu
void SubProcess::GlobalInit() {
  absl::MutexLock l(subproc_mu);
  if (!global_init_done) {
    global_init_done = true;
    process_map = new ProcessMap;
#if __ANDROID__ && __ANDROID_MIN_SDK_VERSION__ < 33
#else
    is_close_range_available = DetectCloseRangeAvailable();
#endif
    if (!using_selectserver) {
      struct sigaction act;
      memset(&act, 0, sizeof(act));
      act.sa_handler = SIG_IGN;
      sigemptyset(&act.sa_mask);
      if (sigaction(SIGPIPE, &act, nullptr) < 0) {
        PLOG(FATAL) << "Couldn't ignore SIGPIPE";
      }
    }
  }
}

// L < subproc_mu
bool SubProcess::running() const {
  absl::MutexLock l(subproc_mu);
  return running_;
}

// L < subproc_mu
void SubProcess::set_running(bool running) {
  absl::MutexLock l(subproc_mu);
  running_ = running;
}

// L < subproc_mu
void SubProcess::Ref() {
  absl::MutexLock l(subproc_mu);
  RefLocked();
}

// L >= subproc_mu
void SubProcess::RefLocked() {
  subproc_mu.AssertHeld();
  extcount_++;
}

// L < subproc_mu
void SubProcess::Unref() {
  absl::MutexLock l(subproc_mu);
  CHECK_GT(extcount_, 0);
  extcount_--;
}

// L < subproc_mu
int SubProcess::Refcount() const {
  absl::MutexLock l(subproc_mu);
  return extcount_;
}

// L < subproc_mu
void SubProcess::set_reported_not_running(bool reported_not_running) {
  absl::MutexLock l(subproc_mu);
  reported_not_running_ = reported_not_running;
}

// L < subproc_mu
SubProcess::SubProcess(int nfds)
    : child_errno_(0),
      umask_(kInheritUmask),
      priority_(0),
      priority_type_(OFF),
      nfds_(nfds),
      inherit_higher_fds_(false),
      action_(nullptr),
      exit_cb_(nullptr),
      exit_cb_tid_(-1),
      extcount_(0),
      fd_(nullptr),
      pid_(-1),
      start_time_(absl::UnixEpoch()),
      finish_time_(absl::UnixEpoch()),
      running_(false),
      reported_not_running_(true),
      exit_status_(0),
      set_fd_(nullptr),
      pidmode_(PIDMODE_NONE),
      disable_thp_(false),
      abandoned_(false),
      parent_death_signal_(0),
      execve_fd_(-1),
      argv_(nullptr),
      argc_(0),
      envp_(nullptr),
      child_to_parent_fd_(-1),
      set_sigmask_(false),
      additional_wait_flags_(0),
      child_setup_logs_enabled_(false) {
  GlobalInit();
  action_ = gtl::UniqueArray<ChannelAction>(new ChannelAction[nfds_], nfds_);
  set_fd_ = gtl::UniqueArray<int>(new int[nfds_], nfds_);
  fd_ = gtl::UniqueArray<int>(new int[nfds_], nfds_);
  for (int i = CHAN_STDIN; i < CHAN_STDIN + nfds_; i++) {
    action_[i] = ACTION_CLOSE;
    set_fd_[i] = -1;
    fd_[i] = -1;
  }
  for (int i = 1; i < NSIG; ++i) {
    sigactions_[i] = SIGACTION_INHERIT;
  }
  DLOG_IF(FATAL, nfds < 3)
      << "SubProcess(" << nfds << ") may result in unexpected behavior "
      << "-- most programs expect 3 open file descriptors.";
}

// Return whether *x is zero; used with Condition.
static bool IntIsZero(int* x) { return *x == 0; }

// Returns whether the process is privileged.
// If the process has an effective user/group ID of 0 (root), or if it was
// started from a set-uid or set-gid binary (ELF AT_SECURE non-zero), it is
// considered privileged.
// Knowing if the process is privileged helps to decide whether dangerous clone
// flags such as CLONE_VM (virtual memory sharing between parent and child)
// should be allowed.
static bool IsPrivileged() {
  if (geteuid() == 0 || getegid() == 0) {
    return true;
  }
  errno = 0;
  if (getauxval(AT_SECURE) == 0 && errno == 0) {
    // We know for sure it is not privileged.
    return false;
  }
  // Either it is privileged or we don't have getauxval, or it failed.
  // Do not take any chances and consider the process privileged.
  return true;
}

// L < subproc_mu
SubProcess::~SubProcess() {
  //  Remove the entry from process_map first so subsequent calls to DoWait()
  //  will not access *this.
  {
    absl::MutexLock l(subproc_mu);
    if (running_) {
      ProcessMap::iterator sp_i = process_map->find(pid_);
      if (sp_i == process_map->end()) {
        LOG(ERROR) << "PID " << pid_ << ": Couldn't find process.";
      } else {
        if (abandoned_) {
          sp_i->second = nullptr;
        } else {
          process_map->erase(sp_i);
        }
      }
    }

    if (extcount_ != 0 && exit_cb_tid_ == GetTID())
      LOG(FATAL) << "Deleting SubProcess "
                 << (filename_.has_value()
                         ? *filename_
                         : absl::StrCat("[fd=", execve_fd_, "] "))
                 << "[" << pid_ << "]" << " inside callback routine";

    // Wait for other calls (such as DoWait(), which any thread may call at any
    // time) to stop accessing *this.
    subproc_mu.Await(absl::Condition(&IntIsZero, &extcount_));

    // After this point, no other calls in SubProcess may be touching *this,
    // so it is legal to modify instance fields.
    exit_cb_ = nullptr;
  }

  pid_ = -1;  //  Rank paranoia.
  set_running(false);

  for (int i = CHAN_STDIN; i < num_chan(); i++) {
    Close(static_cast<Channel>(i));
  }

  FreeCommand();
  FreeEnviron();

  change_callback_ = nullptr;
}

// SetChannelAction()
// L < subproc_mu
// Called before Start()
void SubProcess::SetChannelAction(Channel chan, ChannelAction action) {
  if (!chan_valid(chan)) {
    LOG(FATAL) << "SetChannelAction called with invalid channel: " << chan;
    return;
  }
  if (action < ACTION_CLOSE || action >= ACTION_NUM) {
    LOG(FATAL) << "SetChannelAction called with invalid action: " << action;
    return;
  }
  if (running()) {
    LOG(FATAL) << "SetChannelAction called while process is running.";
    return;
  }
  //  Only one special case!
  if (action == ACTION_MAPTOSTDOUT && chan != CHAN_STDERR) {
    LOG(FATAL) << "SetChannelAction(MAPTOSTDOUT) called on channel: " << chan;
    return;
  }
  if (set_fd_[chan] >= 0) {
    LOG(FATAL) << "SetChannelAction called after SetChannelFD on channel "
               << chan;
    return;
  }
  action_[chan] = action;
}

// SetChannelFD()
//    When the process is started, connect the given file descriptor
//    to the processes stdin/out/err.  This will reset any other
//    previously-specified action for the channel.
//
//    This must be called for each process started, before it is
//    started.  When the process is started, all of the descriptors
//    provided to SetChannelFD() are closed (in the parent process).
//
//    @param chan The channel being considered.
//    @param fd The file descriptor to attach to the given channel.
//              This can be canceled by specifying fd < 0.
//              Setting the same fd for two channels is allowed.
// L < subproc_mu
// Called before Start()
void SubProcess::SetChannelFD(Channel chan, int fd) {
  if (!chan_valid(chan)) {
    LOG(FATAL) << "SetChannelFD called with invalid channel: " << chan;
    return;
  }
  if (running()) {
    LOG(FATAL) << "SetChannelFD called while process is running.";
    return;
  }
  if (set_fd_[chan] >= 0) {
    LOG(FATAL) << "SetChannelFD called twice for channel " << chan;
  }
  if (fd > 0 && fcntl(fd, F_GETFD) < 0) {
    PLOG(FATAL) << "SetChannelFD called with invalid file descriptor: " << fd;
    return;
  }
  if (fd > 0 && fd < 3) {
    // It is almost certainly a bug, if the caller tries to set the child's
    // channel to one of the standard file descriptors, as this will end up
    // closing the descriptor in the parent.
    // There is a small argument to be made, that this might be intended in
    // the case of stdin. So, we only warn about stdout and stderr.
    LOG(WARNING) << "SetChannelFD will close standard file descriptor " << fd
                 << ". Did you mean to call SetChannelAction(" << chan
                 << ", ACTION_DUPPARENT)?";
  }

  action_[chan] = ACTION_CLOSE;
  set_fd_[chan] = fd;
}

// SetChannelFD()
//    Same as above, except rather than taking a file descriptor,
//    use a channel from another SubProcess.  This is provided so
//    that we can keep descriptor bookkeeping straight.  It is a
//    convenience function for
//      SetChannelFD(chan, sp->TakeoverFD(sp_chan))
//    with better error checking.
//
//    @param chan The channel being considered.
//    @param sp Another process to get the channel from.
//    @param sp_chan Which channel from that process to use.
// Called before Start()
void SubProcess::SetChannelFD(Channel chan, SubProcess* sp, Channel sp_chan) {
  CHECK(sp != nullptr);
  int fd = sp->TakeoverFD(sp_chan);
  if (fd < 0) {
    LOG(FATAL) << "SetChannelFD called on invalid channel: " << sp_chan;
    return;
  }
  if (fcntl(fd, F_SETFL, 0) < 0) {
    PLOG(FATAL) << "fcntl(0) failed";
    return;  // not reached
  }
  SetChannelFD(chan, fd);
}

// SetProgram()
//    Set up a program and argument list for execution, with the full
//    "raw" argument list passed as an array of char*.  argv[0] should
//    be the program name, just as in execv().
//    @param file The file containing the program.  The PATH is NOT
//                searched to find it.
//    @param argv The argument list.
// Called before Start()
void SubProcess::SetProgram(absl::string_view file, const char* const argv[]) {
  FreeCommand();
  filename_ = std::string(file);
  argc_ = 0;
  while (argv[argc_] != nullptr) argc_++;
  argv_ = new char*[argc_ + 1];
  for (int i = 0; i < argc_; i++) argv_[i] = strdup(argv[i]);
  argv_[argc_] = nullptr;
}

// SetProgram()
//    Set up a program and argument list for execution, with the full
//    "raw" argument list passed as a vector of strings.  arv[0]
//    should be the program name, just as in execv().
//    @param file The file containing the program.  The PATH is NOT
//                searched to find it.
//    @param argv The argument list.
// Called before Start()
void SubProcess::SetProgram(absl::string_view file,
                            const std::vector<std::string>& argv) {
  FreeCommand();
  filename_ = std::string(file);
  argc_ = argv.size();
  argv_ = new char*[argc_ + 1];
  for (int i = 0; i < argc_; i++) argv_[i] = strdup(argv[i].c_str());
  argv_[argc_] = nullptr;
}

// SetProgram()
//    Set up a program and argument list for execution, with the full
//    "raw" argument list passed as an array of char*.  argv[0] should
//    be the program name, just as in execv().
//    @param fd The file descriptor to file containing the program.
//              It needs to be different than Channels.
//              File descriptor is not owned by this class.
//    @param argv The argument list.
// Called before Start()
void SubProcess::SetProgram(int fd, const char* const argv[]) {
  if (fd < 0) {
    LOG(FATAL) << "[fd=" << fd << "] is not a valid file descriptor";
  }
  if (fd >= CHAN_STDIN && fd < num_chan()) {
    LOG(FATAL) << absl::StrCat("[fd=", fd, "] is in Channel range");
  }
  FreeCommand();
  execve_fd_ = fd;
  argc_ = 0;
  while (argv[argc_] != nullptr) argc_++;
  argv_ = new char*[argc_ + 1];
  for (int i = 0; i < argc_; i++) argv_[i] = strdup(argv[i]);
  argv_[argc_] = nullptr;
}

// SetProgram()
//    Set up a program and argument list for execution, with the full
//    "raw" argument list passed as a vector of strings.  arv[0]
//    should be the program name, just as in execv().
//    @param fd The file descriptor to file containing the program.
//              It needs to be different than Channels.
//              File descriptor is not owned by this class.
//    @param argv The argument list.
// Called before Start()
void SubProcess::SetProgram(int fd, const std::vector<std::string>& argv) {
  if (fd < 0) {
    LOG(FATAL) << "[fd=" << fd << "] is not a valid file descriptor";
  }
  if (fd >= CHAN_STDIN && fd < num_chan()) {
    LOG(FATAL) << absl::StrCat("[fd=", fd, "] is in Channel range");
  }
  FreeCommand();
  execve_fd_ = fd;
  argc_ = argv.size();
  argv_ = new char*[argc_ + 1];
  for (int i = 0; i < argc_; i++) argv_[i] = strdup(argv[i].c_str());
  argv_[argc_] = nullptr;
}

// SetArgv()
//   Convenience wrapper.
// Called before Start()
void SubProcess::SetArgv(const std::vector<std::string>& argv) {
  QCHECK(!argv.empty());
  SetProgram(argv[0], argv);
}

// SetEnviron()
//    Set the environment the process is execed in.  `envp` must be a non-NULL
//    array of strings, usually of the form "key=value".
//    @param envp the environment.  The last element must be NULL
// Called before Start()
void SubProcess::SetEnviron(const char* const envp[]) {
  FreeEnviron();
  int envc = 0;
  while (envp[envc]) {
    ++envc;
  }
  envp_ = new char*[envc + 1];
  for (int i = 0; i < envc; i++) {
    envp_[i] = strdup(envp[i]);
  }
  envp_[envc] = nullptr;
}

// Called before Start()
void SubProcess::SetEnviron(const EnvMap& environ) {
  FreeEnviron();
  int envc = environ.size();
  envp_ = new char*[envc + 1];
  int i = 0;
  for (EnvMap::const_iterator it = environ.begin(); it != environ.end(); ++it) {
    std::string tmp = it->first + "=" + it->second;
    envp_[i] = strdup(tmp.c_str());
    i++;
  }
  envp_[envc] = nullptr;
}

static void ParseEnviron(const char* const env[], SubProcess::EnvMap* result) {
  if (env) {
    for (auto p = env; *p; ++p) {
      if (const char* eq = strchr(*p, '=')) {
        (*result)[std::string(*p, eq - *p)] = eq + 1;
      }
    }
  }
}

const char* const* SubProcess::GetEnviron() const {
  return envp_ != nullptr ? envp_ : environ;
}

void SubProcess::GetEnviron(EnvMap* result) const {
  ParseEnviron(GetEnviron(), result);
}

SubProcess::EnvMap SubProcess::GetThisProcessEnviron() {
  EnvMap result;
  ParseEnviron(environ, &result);
  return result;
}

// SetCommand()
//   Convenience method to resolve argv[0] against the system PATH
void SubProcess::SetCommand(const std::vector<std::string>& argv) {
  FreeCommand();
#ifdef __ANDROID__
  filename_ = "/system/bin/env";
#else
  filename_ = "/usr/bin/env";
#endif
  argc_ = argv.size() + 1;
  argv_ = new char*[argc_ + 1];
  argv_[0] = strdup(filename_->c_str());
  for (int i = 0; i < argv.size(); i++) argv_[i + 1] = strdup(argv[i].c_str());
  argv_[argc_] = nullptr;
}

// strdup for string_view.
static char* StrDup(absl::string_view view) {
  char* dup = static_cast<char*>(malloc(view.size() + 1));
  memcpy(dup, view.data(), view.size());
  dup[view.size()] = '\0';
  return dup;
}

// SetShellCommand()
//    Convenience method to run the given command, which will be
//    interpreted by /bin/sh.  It will be executed as:
//       /bin/sh -c {command}
//    @param command A well-formed shell command.
// Called before Start().
void SubProcess::SetShellCommand(absl::string_view command) {
  FreeCommand();
  argv_ = new char*[4];
#ifdef __ANDROID__
  filename_ = "/system/bin/sh";
#else
  filename_ = "/bin/sh";
#endif
  argv_[0] = strdup(filename_->c_str());
  argv_[1] = strdup("-c");
  argv_[2] = StrDup(command);
  argv_[3] = nullptr;
  argc_ = 3;
}

// Called before Start().
bool SubProcess::SetRLimit(int resource, uint64_t soft, uint64_t hard) {
  if (soft > hard || hard > kRLimitInfinity) return false;

  // Just append the triple.  Don't bother to check if the resource is
  // already in the list (it's OK to invoke the setrlimit syscall
  // repeatedly for the same resource in ForkedChild).
  const RLimitTuple rlimittuple = {resource, soft, hard};
  rlimits_.push_back(rlimittuple);
  return true;
}

void SubProcess::ClearRLimits() { rlimits_.clear(); }

const uint64_t SubProcess::kRLimitInfinity = RLIM_INFINITY;

// Called before Start().
void SubProcess::SetPriority(SchedPriorityType type, int priority) {
  priority_type_ = type;
  priority_ = priority;
}

void SubProcess::SetSignalAction(int signum, SigAction action) {
  CHECK_LT(signum, NSIG);
  CHECK_GT(signum, 0);

  // These will fail later if we allow them, so catch them early.
  CHECK_NE(SIGKILL, signum);
  CHECK_NE(SIGSTOP, signum);

  CHECK(!running()) << "SetSignalAction called while running.";

  sigactions_[signum] = action;
}

void SubProcess::SetSignalMask(const sigset_t* sigmask) {
  CHECK(sigmask);
  CHECK(!running()) << "SetSignalMask called while running.";

  sigmask_ = *sigmask;
  set_sigmask_ = true;
}

// Called before Start().
void SubProcess::FreeCommand() {
  if (argv_ != nullptr) {
    for (int i = 0; i < argc_; i++) {
      free(argv_[i]);
    }
    delete[] argv_;
    argv_ = nullptr;
  }
  argc_ = 0;
  filename_.reset();
  execve_fd_ = -1;
}

// Called before Start().
void SubProcess::FreeEnviron() {
  if (envp_ != nullptr) {
    for (char** envpp = envp_; *envpp; ++envpp) {
      free(*envpp);
    }
    delete[] envp_;
    envp_ = nullptr;
  }
}

bool SubProcess::Start() {
  if (running()) {
    LOG(FATAL) << "Attempt to start a process when already running.";
  }
  if ((!filename_.has_value() && execve_fd_ == -1) || argv_ == nullptr) {
    LOG(FATAL) << "Attempt to start a process without setting the command.";
  }
  if (filename_.has_value() && execve_fd_ != -1) {
    LOG(FATAL)
        << "Attempt to start a process with both filename and execve_fd set.";
  }

#if defined(ABSL_HAVE_ADDRESS_SANITIZER) ||   \
    defined(ABSL_HAVE_HWADDRESS_SANITIZER) || \
    defined(ABSL_HAVE_MEMORY_SANITIZER) ||    \
    defined(ABSL_HAVE_THREAD_SANITIZER) || defined(CONFIG_UBSAN)
  // N.B. UNDEFINED_BEHAVIOR_SANITIZER applies to unsanitized fastbuild/dbg too
  // (because UBSan checks are enabled). We use a custom CONFIG_UBSAN macro to
  // detect --config=ubsan.
  for (auto lim : rlimits_) {
    if (lim.resource == RLIMIT_AS)
      LOG(WARNING)
          << "A subprocess is started with RLIMIT_AS limit set when"
             " running under a Sanitizer. The subprocess can fail with"
             " 'cannot mmap shadow memory' error because sanitizers mmap lots"
             " of memory or specify a custom allocator. Disable RLIMIT_AS"
             " limit if that happens.";
  }
#endif

  //  Unconditionally close all open files.
  if (execve_fd_ >= CHAN_STDIN && execve_fd_ < num_chan()) {
    LOG(WARNING) << "File descriptor to program is in Channel range.";
    return false;
  }

  for (int i = CHAN_STDIN; i < num_chan(); i++) {
    Close(static_cast<Channel>(i));
  }

  bzero(&final_usage_, sizeof(final_usage_));
  bool rv;
  ForkAndExec(&rv);

  if (filename_.has_value()) {
    VLOG(2) << "SubProcess::Start " << *filename_ << "[" << pid_ << "]";
  } else {
    VLOG(2) << "SubProcess::Start [fd = " << execve_fd_ << "] [" << pid_ << "]";
  }

  return rv;
}

using ::util_process_internal::NulTerminatedBufAppender;

// InternalStrError()
//    We'd really like to call something like strerror_r() whenever we
//    notice an error in the child process, but we cannot reliably make
//    library calls that could acquire locks. Use a stupid little
//    hand-coded function instead.
// Called in child process.
void SubProcess::InternalStrError(int child_errno, absl::string_view msg,
                                  char* buf, size_t len) {
  // Sanity check. Do nothing, if there is no space left in buffer.
  if (len <= 0) return;

  NulTerminatedBufAppender appender(buf, len);  // async-signal safe.
  appender.Append(msg);
  if (child_errno) {
    appender.Append(" (errno = ");
    appender.Append(child_errno);
    appender.Append(")");
  }
}

// SendChildToParentMessage
//    This message performs all the communication from the child to the
//    parent. It can send the value of errno, an error message, an
//    informational message, and any number of file descriptors. All of
//    these parameters are optional.
// Called in child process.
int SubProcess::SendChildToParentMessage(int child_errno,
                                         absl::string_view errstr,
                                         const char* infostr, int nfd,
                                         int* fds) {
  struct kernel_iovec iov[2] = {{nullptr}};
  struct kernel_msghdr msg = {nullptr, 0};
  struct cmsghdr* cmsg;
  memcpy(child_buffers_->errmsg_buf, &child_errno, sizeof(int));
  InternalStrError(child_errno, errstr,
                   child_buffers_->errmsg_buf + sizeof(int),
                   kMaxErrMsgSize - sizeof(int));
  iov[0].iov_base = child_buffers_->errmsg_buf;
  iov[0].iov_len =
      sizeof(int) + strlen(child_buffers_->errmsg_buf + sizeof(int)) + 1;
  iov[1].iov_base = const_cast<char*>(infostr);
  iov[1].iov_len = strlen(infostr) + 1;
  msg.msg_iov = iov;
  msg.msg_iovlen = 2;
  if (nfd > 0) {
    msg.msg_control = &child_buffers_->cmsg_buf[0];
    msg.msg_controllen = CMSG_SPACE(nfd * sizeof(int));
    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(nfd * sizeof(int));
    while (nfd--) {
      reinterpret_cast<int*>(CMSG_DATA(cmsg))[nfd] = fds[nfd];
    }
  }
  int rc;
  while ((rc = lss_sendmsg(child_to_parent_fd_, &msg, 0, &child_errno_)) < 0 &&
         child_errno_ == EINTR) {
  }
  return rc;
}

// SendFatalError()
//    Return a fatal error and any accumulated informational messages to
//    the parent process. Includes the current value of errno in the message.
//    This method does not return and terminates the child process.
// Called in child process.
void SubProcess::SendFatalError(absl::string_view msg, int arg_errno) {
  // We have to send a non-zero error code for the parent process to know that
  // this was the last message it will receive. Normally, our caller would have
  // made sure that errno is set appropriately. But if, because of a bug, this
  // was not the case, we set errno to a negative value. This is not normally
  // possible and will be easy to notice in log files.
  SendChildToParentMessage(arg_errno ? arg_errno : -1, msg,
                           child_buffers_->info_msgs, 0, nullptr);
  for (;;) {
    // We never return from this method
    lss__exit(1, &child_errno_);
  }
}

// FlushInfoMessages()
//    Flushes any pending info messages by sending them to the parent.
// Called in child process.
void SubProcess::FlushInfoMessages() {
  if (*child_buffers_->info_msgs &&
      SendChildToParentMessage(0, "", child_buffers_->info_msgs, 0, nullptr) <
          0) {
    SendFatalError("Failed to flush info messages");
  }
  *child_buffers_->info_msgs = '\000';
}

// ChildLogInfo()
//     Append a message to the child's info log. The info buffer will
//     eventually be sent to the parent, and might get printed, if the
//     parent thinks outputting extra debugging information is going to
//     be helpful.
//     Prints a EOL terminated sequence of strings. Instead of a string,
//     it can also print numbers, if the argument is preceded by the NUM tag.
// Called in child process.
const char* SubProcess::NUM = reinterpret_cast<char*>(-1);
#define EOL nullptr
void SubProcess::ChildLogInfo(const char* msg, ...) {
  int len = strlen(child_buffers_->info_msgs);
  va_list ap;
  va_start(ap, msg);
  NulTerminatedBufAppender appender(child_buffers_->info_msgs + len, len);
  while (msg != EOL && !appender.IsFull()) {
    if (msg == NUM) {
      // Any argument preceded by the NUM tag is an "int"
      int i = va_arg(ap, int);
      appender.Append(i);
    } else {
      // Append another string to the buffer, as long as there is still
      // space left.
      appender.Append(msg);
    }

    // Retrieve the next argument, tag, or end-of-line marker.
    msg = va_arg(ap, const char*);
  }

  if (!appender.IsFull()) {
    // If there is space, terminate the line with a newline character.
    // The parent will later use these to split the output across
    // multiple invocations of LOG(INFO)
    appender.Append("\n");
  }
  va_end(ap);

  // If our buffer gets more than 2/3 full, better flush it.
  if (appender.SizeLeft() * 3 < kMaxInfoBufSize) {
    FlushInfoMessages();
  }
}

void SubProcess::CloseNonchannelFileDescriptors(int openmax) {
  if (execve_fd_ != -1 &&
      lss_fcntl(execve_fd_, F_SETFD, FD_CLOEXEC, &child_errno_) == -1) {
    SendFatalError("fcntl(execve_fd_, F_SETFD, FD_CLOEXEC) failed");
  }

  // Close everything from num_chan() to the maximum FD, excluding
  // child_to_parent_fd_ and execve_fd_.
  // If execve_fd_ is not set use child_to_parent_fd_ to create empty range.
  int fds[2] = {child_to_parent_fd_,
                execve_fd_ != -1 ? execve_fd_ : child_to_parent_fd_};
  if (fds[1] < fds[0]) std::swap(fds[0], fds[1]);

  if (is_close_range_available) {
    // N.B. close_range() operates on inclusive ranges.
    CloseRange(num_chan(), fds[0] - 1, &child_errno_);
    if (fds[1] - fds[0] > 1) CloseRange(fds[0] + 1, fds[1] - 1, &child_errno_);
    CloseRange(fds[1] + 1, std::numeric_limits<unsigned int>::max(),
               &child_errno_);
    return;
  }
  int procfd = lss_open("/proc/self/fd", O_RDONLY, 0, &child_errno_);
  if (procfd != -1) {
    // Scan /proc/self/fd looking for filehandles.
    int bytes;
    kernel_dirent64* buffer = child_buffers_->dirent_scratch.data();
    while (
        (bytes = lss_getdents64(
             procfd, buffer, sizeof(kernel_dirent64) * kNumDirentScratchEntries,
             &child_errno_)) > 0) {
      struct kernel_dirent64* de;
      for (int offset = 0; offset < bytes; offset += de->d_reclen) {
        de = reinterpret_cast<struct kernel_dirent64*>(
            reinterpret_cast<char*>(buffer) + offset);
        if (de->d_name[0] == '.') continue;

        // Simple atoi() loop since apparently calling atoi() in this
        // environment isn't safe
        int fd = 0;
        char* p = de->d_name;
        while (*p) {
          fd = fd * 10 + *p++ - '0';
        }
        if (fd < num_chan()) continue;
        if (fd == fds[0] || fd == fds[1]) continue;
        if (fd == procfd) continue;  // Don't close the directory handle
        lss_close(fd, &child_errno_);
      }
    }
    lss_close(procfd, &child_errno_);
    return;
  }
  // close_range and /proc/self/fd didn't work - try the slow way instead ...
  for (int fd = num_chan(); fd < openmax; fd++) {
    if (fd == fds[0] || fd == fds[1]) continue;
    lss_close(fd, &child_errno_);
  }
}

// ForkedChild()
//    The code that runs in the child thread after fork()'ing, but before
//    exec()'ing.
// Called in child process.
void SubProcess::ForkedChild(struct ChildArgs* args) {
  // We're in the child process. From here on, we cannot call any
  // non-reentrant library functions! We have to restrict ourselves to
  // system calls, and function calls that are guaranteed not to
  // acquire any locks. We can send fatal error messages back to the
  // parent on the "pair[1]" socket. The last call in the child must be
  // either exec() or _exit() (or an unhandled fatal signal).

  // When using fork, this function is called in the memory space
  // of the child process. Changes performed to global or class variables
  // in this function will not affect the parent and will not persist across
  // multiple calls of this function (in different child processes).
  // In contrast, when clone is used, the child and the parent share the same
  // address space. If any global or class variable is changed, its value will
  // be carried over to the next invocation of ForkedChild.
  //
  // Reset the value of errno_ before doing anything else.
  child_errno_ = 0;

  // All signals are blocked at this time, but we could still end up
  // executing synchronous signals (such as SIGILL, SIGFPE, SIGSEGV, SIGBUS,
  // or SIGTRAP). Reset them to SIG_DFL.
  for (int signo : {SIGABRT, SIGILL, SIGFPE, SIGSEGV, SIGBUS}) {
    static const struct kernel_sigaction act = {.sa_handler_ = SIG_DFL,
                                                .sa_flags = SA_RESTART};
    lss_sigaction(signo, &act, nullptr, &child_errno_);
  }

  TEST_AfterForkBeforeExecEarly(child_to_parent_fd_);

  // If the user asked for a very large number of channels, or if the
  // caller had a very small number of open file handles, there is a
  // risk of "pair[1]" conflicting with the file handles that we want
  // to set up for the child. So, move it out of the way. There is no
  // problem with temporarily dup()'ing more file handles, as these
  // will be closed again soon.

  // The rest of the code relies on the fact that this loop won't leave
  // any unused file handles in the range 0..num_chan().
  while (child_to_parent_fd_ < num_chan()) {
    int fd;
    while ((fd = lss_dup(child_to_parent_fd_, &child_errno_)) < 0 &&
           child_errno_ == EINTR) {
    }
    if (fd < 0) {
      SendFatalError("dup() failed.");
    } else {
      ChildLogInfo("Moving communications fd above ", NUM, num_chan(),
                   ", from ", NUM, child_to_parent_fd_, " to ", NUM, fd, EOL);

      child_to_parent_fd_ = fd;
    }
  }
  if (lss_fcntl(child_to_parent_fd_, F_SETFD, FD_CLOEXEC, &child_errno_) < 0) {
    SendFatalError("fcntl() failed.");
  }

  // We have to do the same thing for any file descriptor set with
  // SetChannelFD().
  for (int i = CHAN_STDIN; i < num_chan(); i++) {
    while (set_fd_[i] >= 0 && set_fd_[i] < num_chan()) {
      int fd;
      while ((fd = lss_dup(set_fd_[i], &child_errno_)) < 0 &&
             child_errno_ == EINTR) {
      }
      if (fd < 0) {
        SendFatalError("dup() failed.");
      } else {
        ChildLogInfo("Moving channel fd above ", NUM, num_chan(), ", from ",
                     NUM, set_fd_[i], " to ", NUM, fd, EOL);

        set_fd_[i] = fd;
      }
    }
  }

  // Even if the user asked us to ACTION_CLOSE some of the file handles,
  // this is a very bad idea for the first three file handles. Most UNIX
  // programs cannot tolerate if their stdin/out/err handles are closed.
  // Instead, we'll point them to "/dev/null". This is a lot safer.
  int null_fd;
  while ((null_fd = lss_open("/dev/null", O_RDWR | O_CLOEXEC, 0,
                             &child_errno_)) < 0 &&
         child_errno_ == EINTR) {
  }
  if (null_fd < 0) {
    SendFatalError("open(\"/dev/null\", O_RDWR) failed.");
  }

  // Prepare for sending file handles back to the parent.
  int send_count = 0;

  // Create socketpairs for the ACTION_PIPE channels, close or redirect
  // the other ones.
  // [0] is the parent's end, [1] is the child's end.
  for (int i = CHAN_STDIN; i < num_chan(); i++) {
    if (action_[i] == ACTION_PIPE) {
      int channel_pair[2] = {0};  // initialize to suppress a gcc warning.
      if (lss_socketpair(AF_UNIX, SOCK_STREAM, 0, channel_pair, &child_errno_) <
          0) {
        SendFatalError("socketpair() failed.");
      }

      // Move file handle out of the way, if necessary
      while (channel_pair[0] < num_chan()) {
        int fd;
        while ((fd = lss_dup(channel_pair[0], &child_errno_)) < 0 &&
               child_errno_ == EINTR) {
        }
        if (fd < 0) {
          SendFatalError("dup() failed.");
        } else {
          channel_pair[0] = fd;
        }
      }

      // Mark our parent's end as close-on-exec. We want to do this in the
      // child to avoid race conditions, but it is not quite clear whether
      // this flag will be retained in the parent's copy (in fact, there is
      // a good chance it won't)
      if (lss_fcntl(channel_pair[0], F_SETFD, FD_CLOEXEC, &child_errno_) < 0) {
        SendFatalError("fcntl(FD_CLOEXEC) failed");
      }

      // Connect child to socket.
      if (channel_pair[1] != i) {
        while (lss_dup2(channel_pair[1], i, &child_errno_) < 0) {
          if (child_errno_ != EINTR) {
            SendFatalError("dup2() failed.");
          }
        }
        // The fd closing loop below expects all fds less than num_chan()
        // to be allocated. If this fd is less than num_chan(), then
        // that loop will take care of closing it for us.
        if (channel_pair[1] >= num_chan() &&
            lss_close(channel_pair[1], &child_errno_) < 0 &&
            child_errno_ != EINTR) {
          SendFatalError("close() failed.");
        }
      }

      // Record file handle for sending it to the parent
      child_buffers_->parent_fds[send_count++] = channel_pair[0];

      ChildLogInfo("Created socket pair; child = ", NUM, i, ", parent = ", NUM,
                   channel_pair[0], EOL);
    } else if (i == CHAN_STDERR && action_[i] == ACTION_MAPTOSTDOUT) {
      // Connect STDERR to STDOUT.
      while (lss_dup2(CHAN_STDOUT, CHAN_STDERR, &child_errno_) < 0) {
        if (child_errno_ != EINTR) {
          SendFatalError("dup2() failed.");
        }
      }
      ChildLogInfo("Connected stderr to stdout", EOL);
    } else if (action_[i] == ACTION_DUPPARENT) {
      // Don't do anything. Continue using the copy of the parent's
      // file descriptor, that we already got when we fork()'d
      ChildLogInfo("Using parent's definition of fd ", NUM, i, EOL);
    } else if (set_fd_[i] >= 0) {
      //  Connect child to a preset descriptor.
      while (lss_dup2(set_fd_[i], i, &child_errno_) < 0) {
        if (child_errno_ != EINTR) {
          SendFatalError("dup2() failed.");
        }
      }
      ChildLogInfo("Connected fd ", NUM, i, " to fd ", NUM, set_fd_[i],
                   " opened by parent", EOL);
    } else {
      // Not using this descriptor.
      if (i <= CHAN_STDERR) {
        while (lss_dup2(null_fd, i, &child_errno_) < 0) {
          if (child_errno_ != EINTR) {
            SendFatalError("dup2() failed.");
          }
          ChildLogInfo("Connected fd ", NUM, i, " to /dev/null", EOL);
        }
      }
    }
  }

  // Close file handles that the user did not want open (we have to do this
  // here, because the previous loop implicitly assumed that there were
  // no unused file handles in the range 0..num_chan()).
  int close_start_fd = -1;
  int num_closed_fds = 0;
  for (int fd = CHAN_STDERR + 1; fd < num_chan(); fd++) {
    if (action_[fd] != ACTION_PIPE && action_[fd] != ACTION_DUPPARENT &&
        set_fd_[fd] < 0) {
      if (lss_close(fd, &child_errno_) != 0 && child_errno_ != EINTR) {
        SendFatalError("close() failed.");
      }

      if (close_start_fd == -1) {
        close_start_fd = fd;
      }
      ++num_closed_fds;
      if (fd != num_chan() - 1) {
        // continue to close consecutive fds to log once.
        continue;
      }
    }
    switch (num_closed_fds) {
      case 0:
        break;

      case 1:
        ChildLogInfo("Closed fd ", NUM, close_start_fd, EOL);
        close_start_fd = -1;
        num_closed_fds = 0;
        break;

      default:
        // more than one fds.
        ChildLogInfo("Closed fds from ", NUM, close_start_fd, " to ", NUM,
                     close_start_fd + num_closed_fds - 1, EOL);
        close_start_fd = -1;
        num_closed_fds = 0;
        break;
    }
  }

  // Change the current working directory, if the caller asked us to do so.
  if (!chdir_.empty()) {
    if (lss_chdir(chdir_.c_str(), &child_errno_) < 0) {
      SendFatalError("chdir() failed.");
    }
  }

  // Change the root directory, if the caller asked us to do so.
  if (!chroot_dir_.empty()) {
    if (lss_chroot(chroot_dir_.c_str(), &child_errno_) < 0) {
      SendFatalError("chroot() failed.");
    }
  }

  // Change the process group, if the caller asked us to do so.
  switch (pidmode_) {
    case PIDMODE_NONE:
      break;
    case PIDMODE_PGRP:
      lss_setpgrp(&child_errno_);
      break;
    case PIDMODE_SESSION:
      lss_setsid(&child_errno_);
      break;
  }

  // Send file handle(s) to parent, and notify parent that the client is now
  // fully initialized.
  ChildLogInfo("Sending ", NUM, send_count, " file handles to parent", EOL);

  TEST_AfterForkBeforeExecLate(child_to_parent_fd_);

  if (send_count) {
    if (SendChildToParentMessage(0, "", child_buffers_->info_msgs, send_count,
                                 &child_buffers_->parent_fds[0]) < 0) {
      SendFatalError("sendmsg() failed.");
    }
    *child_buffers_->info_msgs = '\000';
  }

  // Signal masks are inherited by child processes, so reset them prior to
  // calling execve(). Since this exposes us to the delivery of pending
  // signals we have to clear all signals first.
  struct kernel_sigset_t pending;
  lss_sigpending(&pending, &child_errno_);
  for (int i = 1; i < NSIG; i++) {
    if (lss_sigismember(&pending, i, &child_errno_)) {
      struct kernel_sigaction act, old;
      memset(&act, 0, sizeof(act));
      act.sa_handler_ = SIG_IGN;
      act.sa_flags = SA_RESTART;
      lss_sigaction(i, &act, &old, &child_errno_);
      lss_sigaction(i, &old, nullptr, &child_errno_);
    }
    if (sigactions_[i] != SIGACTION_INHERIT) {
      struct kernel_sigaction act;
      memset(&act, 0, sizeof(act));
      if (sigactions_[i] == SIGACTION_IGNORE) {
        act.sa_handler_ = SIG_IGN;
      } else if (sigactions_[i] == SIGACTION_DEFAULT) {
        act.sa_handler_ = SIG_DFL;
      } else {
        SendFatalError("Unknown signal action");
      }
      act.sa_flags = SA_RESTART;
      if (0 != lss_sigaction(i, &act, nullptr, &child_errno_)) {
        SendFatalError("Could not install signal handler");
      }
    }
  }

  kernel_sigset_t* child_mask;
  if (set_sigmask_) {
    child_mask = reinterpret_cast<kernel_sigset_t*>(&sigmask_);
  } else {
    child_mask = reinterpret_cast<kernel_sigset_t*>(&args->old_signals);
  }
  if (0 != lss_sigprocmask(SIG_SETMASK, child_mask, nullptr, &child_errno_)) {
    SendFatalError("Could not set signal mask");
  }

  // Close all unused file handles.
  if (!inherit_higher_fds_) {
    CloseNonchannelFileDescriptors(args->openmax);
  }

  // set the rlimits if necessary.
  for (const auto& rlim_iter : rlimits_) {
    // Initializing this the long way avoids C++0x diagnostics about
    // the narrowing conversions as well as being more explicit about
    // the order of the struct members.
    // TODO: eliminate truncation problems.
    struct kernel_rlimit lim;
    lim.rlim_cur = rlim_iter.soft_limit;
    lim.rlim_max = rlim_iter.hard_limit;

    // finally run the syscall
    if (lss_setrlimit(rlim_iter.resource, &lim, &child_errno_)) {
      SendFatalError("setrlimit() failed.");
    }
  }

  if (umask_ != kInheritUmask) {
    lss_umask(umask_, &child_errno_);  // cannot fail
  }

  // set scheduling priority
  switch (priority_type_) {
    case RELATIVE:
      // SYS_nice is removed in the 64-bit kernel, so to be
      // consistent, just use get/set priority everywhere.
      // And the best part of all: getpriority returns numbers in
      // [1, 39], but setpriority expects them in [-20,19].
      priority_ += (20 - lss_getpriority(PRIO_PROCESS, 0, &child_errno_));
      [[fallthrough]];
    case ABSOLUTE:
      lss_setpriority(PRIO_PROCESS, 0, priority_, &child_errno_);
      break;
    default:
      break;
  }

// Set the signal that should be delivered to us if the parent process dies.
#ifdef PR_SET_PDEATHSIG
  TEST_AfterForkBeforeSetParentDeathSignal();
  if (parent_death_signal_ != 0) {
    lss_prctl(PR_SET_PDEATHSIG, parent_death_signal_, 0, 0, 0, &child_errno_);
    // If the parent has already died and the parent_death_signal is SIGKILL,
    // deliver it now. This special-casing is only done for SIGKILL as there are
    // other races which may prevent other signals from being delivered.
    if (parent_death_signal_ == SIGKILL && args->ppid != getppid()) {
      kill(getpid(), SIGKILL);
    }
  }
#endif

  // Disable THP if requested
#ifdef PR_SET_THP_DISABLE
  if (disable_thp_) {
    if (lss_prctl(PR_SET_THP_DISABLE, 1, 0, 0, 0, &child_errno_) < 0) {
      ChildLogInfo("Could not disable THP ", NUM, child_errno_, EOL);
    }
  }
#endif

  // Flush info string
  if (*child_buffers_->info_msgs) {
    child_errno_ = 0;
    FlushInfoMessages();
  }

  ExecChild();
  // Callers normally expect the child to exit with SIGABRT if things went
  // really wrong (e.g. exec() failed because the file could not be found),
  // but we cannot just call abort() directly.
  // Instead, we will inform the parent process and then have it report
  // a (fake) SIGABRT condition to the caller.
  SendFatalError("exec() failed");
  lss__exit(127, &child_errno_);  // We never get here
}

// CloneFnc()
//    Helper function called from clone().
// Called in child process.
int SubProcess::CloneFnc(void* void_args) {
  struct ChildArgs* args = static_cast<struct ChildArgs*>(void_args);
  args->subprocess->ForkedChild(args);
  return 0;
}

// ForkAndExec()
//    Set up file descriptors, fork, and exec.  This does all of the
//    intricate work.
//    @success : set *success to true before return only if successful.
// L < subproc_mu
void SubProcess::ForkAndExec(bool* success) {
  struct ChildArgs args;
  args.subprocess = this;
  args.ppid = getpid();
  pid_ = 0;
  subproc_mu.lock();
  running_ = false;
  reported_not_running_ = false;
  subproc_mu.unlock();
  exit_status_ = kExecFailed;
  finish_time_ = absl::UnixEpoch();
  start_time_ = absl::Now();

  // Already reserve a good amount of space in the error and debug info
  // strings, as we will be filling them while we share address space
  // with the child. Though probably benign, it is better not to do any
  // memory allocations during that time.
  // While later shrink the error text to the appropriate size, when
  // we know what the final error message was.
  error_text_ = "";
  error_text_.reserve(kMaxInfoBufSize);
  std::string info_text;
  info_text.reserve(kMaxInfoBufSize);

  // Assume failure at first
  *success = false;

  // Create file descriptor(s) that can be used for reading data from
  // our child process. This is a little complicated because we need
  // to make sure there is no race condition with other threads
  // calling fork() at the same time. We have to avoid other processes
  // holding our file handles open. We can do this by creating the
  // socketpairs in the child and passing the file handles back to the
  // parent over another socketpair.
  //
  // TODO: make the child process create another socketpair and use that
  // one for the rest of the communication, so that we don't need to wait for
  // EOF in this socketpair, thus avoiding race conditions where this socketpair
  // ends up being shared with other processes. Then remove the SOCK_CLOEXEC
  // flag.
  int pair[2];
  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) < 0) {
    int tmp_errno = errno;
    error_text_ = base::StrError(errno);
    errno = tmp_errno;
    PLOG(ERROR) << "socketpair() failed";
    errno = tmp_errno;
    return;
  }

  // Set up file handles and scratch space for the child to communicate with
  // the parent
  child_to_parent_fd_ = pair[1];
  child_buffers_ =
      std::make_unique<ChildBuffers>(num_chan(), !inherit_higher_fds_);

  // Find maximum number of file handles, so that we can later close
  // unneeded ones.
  args.openmax = sysconf(_SC_OPEN_MAX);
  if (args.openmax < 0) {
    LOG(FATAL) << "sysconf(_SC_OPEN_MAX) failed.";
  }

  // Iterate over all channels and invalidate their file handle
  // (for ACTION_CLOSE and ACTION_MAPTOSTDOUT). Remember file handles, so
  // that we can close them after fork()ing.
  int expected_count = 0;
  absl::flat_hash_set<int> action_close;
  for (int i = CHAN_STDIN; i < num_chan(); i++) {
    // Mark any descriptors provided with SetChannelFD() for closing.
    if (set_fd_[i] >= 0) {
      CHECK_NE(action_[i], ACTION_PIPE);
      // Check for duplicates. If the caller added the same fd to more than one
      // channel (say, stdout and stderr redirected to the same file) then we
      // want to close that fd just once in the parent.
      action_close.insert(set_fd_[i]);
      fd_[i] = -1;
    } else if (action_[i] == ACTION_PIPE) {
      expected_count++;
    } else {
      fd_[i] = -1;
    }
  }

  // Block signals prior to clone()'ing. Technically, POSIX requires us to call
  // pthread_sigmask(), if this is a threaded application. When using
  // glibc, we are OK calling sigprocmask(), though. We will end up
  // blocking additional signals that libpthread uses internally, but that
  // is actually exactly what we want.
  struct kernel_sigset_t blocked_signals;
  sys_sigfillset(&blocked_signals);
  sys_sigprocmask(SIG_BLOCK, &blocked_signals, &args.old_signals);

  // Call the clone(), or fork() system call directly in order to avoid
  // complications with pthread_atfork() handlers. In the child process,
  // we should only ever call system calls.
  //
  // Calling clone() with the CLONE_VM option has the added benefit that it
  // allows launching of processes, even if the kernel enforces overcommit
  // protection and the calling process has allocated large amounts of memory.
  // However sharing the same address space with the parent presents a security
  // risk: the child can take over its parent. This is an issue in different
  // privileges context: for instance the child could drop its privileges with
  // setuid() but can still control its parent by modifying the parent address
  // space as it is shared.
  // Conclusion: disallow it for privileged process.
  //
  // fork() does not have this benefit, but some debugging tools (such as
  // valgrind), get confused by clone() and thus prefer fork().
  //
  // Some versions of libc find errno by looking at the current stack
  // pointer. This means, in the case of clone(), allocating the childs stack
  // on the heap would likely result in a crash the very first time any
  // system call is made.
  //
  // Making all system calls through child_syscalls_ helps, but still fails
  // in the case where somebody subclasses ExecChild().
  //
  // As a solution, we allocate the child's stack within the parent's stack,
  // and we make sure that the parent avoids accessing errno.
  //
  // Because of the possibility that ExecChild() references the global instance
  // of errno, we play it safe and use a private copy of errno in the parent
  // as well.
  pid_t pid;
  int clone_errno;
  char stack[4096] __attribute__((aligned(16)));

#ifdef __GLIBC__
  // As of eglibc-2.11.1:
  // sizeof(struct pthread) == 1168 in 32-bit mode,
  //                        == 2288 in 64-bit mode.
  // See comment under 'case CLONE' below.
  char tls[4096] __attribute__((aligned(16)));
#endif

  int method = subprocess_fork_method;
#if defined(ABSL_HAVE_ADDRESS_SANITIZER) ||   \
    defined(ABSL_HAVE_HWADDRESS_SANITIZER) || \
    defined(ABSL_HAVE_MEMORY_SANITIZER) ||    \
    defined(ABSL_HAVE_THREAD_SANITIZER) || defined(SAFESTACK_SANITIZER)
  method = FORK;  // Sanitizers do not like the TLS hacks in the CLONE code.
#endif

#ifdef __riscv
  // Unconditionally use fork() on RISCV, see b/350032252.
  method = FORK;
#endif

  if (method == UNKNOWN) {
    if (ValidateSubprocessForkMethod("subprocess_fork_method",
                                     GetDefaultSubprocessForkMethod())) {
      method = subprocess_fork_method;
    }
    // This can only happen if GOOGLE_SUBPROCESS_FORK_METHOD is something
    // other than 'fork' or 'clone'.
    CHECK_NE(method, UNKNOWN) << "Invalid GOOGLE_SUBPROCESS_FORK_METHOD?";
    if (getenv("GOOGLE_SUBPROCESS_FORK_METHOD") == nullptr) {
      ABSL_RAW_LOG(WARNING,
                   "Calling SubProcess::ForkAndExec before InitGoogle. "
                   "Will use '%s' method. If that isn't what you want, set "
                   "GOOGLE_SUBPROCESS_FORK_METHOD in the environment or "
                   "move InitGoogle earlier, so flags can be processed.",
                   method == FORK ? "fork" : "clone");
    }
  }
  // Enforce the use of fork if caller asked to disable THP in child, as memory
  // config is shared with parent in the case of clone.
  if (disable_thp_) {
    method = FORK;
  }
  if (method == CLONE && IsPrivileged()) {
    LOG(FATAL) << "Using '--subprocess_fork_method=clone' from a privileged "
               << "process is a security risk. Use 'fork' method instead. "
               << "Change (or remove) the flag "
               << "(or GOOGLE_SUBPROCESS_FORK_METHOD environment) to get rid "
               << "of this error.";
  }

  switch (method) {
    case CLONE: {
      void* ptls = nullptr;
      // NOTE: CLONE_UNTRACED actually breaks strace for the child.
      int flags = CLONE_UNTRACED | SIGCHLD | CLONE_VM;

#ifdef __GLIBC__
      // This is a gross hack. See http://b/2916327 for discussion of
      // the problem, and other approaches that were attempted.
      //
      // In order to prevent a race in dynamic linker during lazy PLT symbol
      // resolution, we set up a new "empty" TLS area for the thread we are
      // about to clone.
      // This is not how a real pthread_create would have set it up, but
      // is sufficient to fool eglibc-2.11.1. More precise set up may be
      // required for future glibc versions.
      memset(tls, 0, sizeof(tls));
      flags |= CLONE_SETTLS;

      // http://b/8386821. Thread-local variables are accessed at relative
      // (negative) offsets from tls[0]. Make it point into the middle of
      // tls[], so both positive and negative offsets work. Keep 16-byte
      // alignment.
      // Note: this is fragile, and will break if there are too many TLS
      // variables.
      *(reinterpret_cast<char**>(tls)) =
          tls + (((sizeof(tls) / 2) + 15) & ~static_cast<intptr_t>(15));

#if defined(__i386__)
      struct user_desc u_info;
      // Get %gs from current thread.
      __asm__ __volatile__("movl %%gs, %0" : "=q"(u_info.entry_number));
      u_info.entry_number >>= 3;  // Discard GDT part.
      // Copy thread_area from current thread.
      CHECK_ERR(sys_get_thread_area(&u_info));
      u_info.base_addr = (unsigned int)tls;
      ptls = &u_info;
#elif defined(__x86_64__)
      ptls = tls;
#elif defined(__arm__)
#ifdef __ARM_EABI__
      ptls = __aeabi_read_tp();
#else
#error TLS access is only implemented for EABI on ARM.
#endif
#elif defined(__powerpc64__)
      // TLS is actually ignored in clone() by the PowerPC kernel.
      __asm__ __volatile__("mr %0, 13" : "=r"(ptls) : :);
#elif defined(__aarch64__)
      __asm__ __volatile__("mrs %0, tpidr_el0" : "=r"(ptls) : :);
#else
#error Do not know how to pass TLS on this architecture
#endif
#endif
      pid = lss_clone(CloneFnc, &stack[sizeof(stack)], flags, &args, nullptr,
                      ptls, nullptr, &clone_errno);
      break;
    }
    case FORK:
      if ((pid = lss_fork(&clone_errno)) == 0) {
        ForkedChild(&args);
        for (;;) {
          lss__exit(1, &clone_errno);
        }
      }
      break;
    default:
      LOG(FATAL) << "Unhandled fork method";
  }

  // Unblock signals in parent
  if (pid != 0) {
    sys_sigprocmask(
        SIG_SETMASK,
        reinterpret_cast<struct kernel_sigset_t*>(&args.old_signals), nullptr);
  }

  // Close file handles marked for closing
  for (int fd : action_close) {
    sys_close(fd);
  }

  if (pid < 0) {
    error_text_ = base::StrError(clone_errno);
    LOG(ERROR) << "clone() failed - pid = " << pid
               << ", errno = " << clone_errno << ": " << error_text_;
    close(pair[0]);
    close(pair[1]);
    errno = clone_errno;
    return;
  }
  pid_ = pid;

  sys_close(pair[1]);

  // Get socketpairs from our child
  char buf[kMaxInfoBufSize];
  absl::FixedArray<char> cmsg_buf(CMSG_SPACE(num_chan() * sizeof(int)));
  union {
    int error;
    char bytes[8];
  } tmp_errno;
  int client_errno = 0;
  ssize_t len;
  int retry = 10;
  int send_count = 0;
  int state = 0;
  bool child_died_prematurely = false;
  struct kernel_iovec iov;
  struct kernel_msghdr msg;
  CHECK_EQ(nfds_, num_chan());
  for (;;) {
    memset(&iov, 0, sizeof(iov));
    memset(&msg, 0, sizeof(msg));
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf) - 1;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf.data();
    msg.msg_controllen = cmsg_buf.size();
    msg.msg_flags = MSG_CMSG_CLOEXEC;
    int local_errno;
    while ((len = lss_recvmsg(pair[0], &msg, 0, &local_errno)) < 0 &&
           (local_errno == EINTR || local_errno == EAGAIN)) {
    }
    if (len < 0) {
      errno = local_errno;
      PLOG(FATAL) << "recvmsg() failed";
    }

    // Parse any text messages received from the client. Messages always
    // consist of an errno value (possibly zero, if no error occurred), a
    // detailed zero-terminated error message, and a zero-terminated
    // info messages. These strings can conceivably be zero-length.
    for (int i = 0; i < len; i++) {
      switch (state) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
          // Errno value (possibly zero, if no error)
          tmp_errno.bytes[state++] = buf[i];
          if (state == sizeof(int)) {
            if (tmp_errno.error) {
              CHECK_EQ(client_errno, 0);
              client_errno = tmp_errno.error;
            }
            state = 8;
          }
          break;
        case 8:
          // Detailed error message (possibly zero-length, if none)
          if (buf[i]) {
            error_text_.append(buf + i, 1);
          } else {
            state++;
          }
          break;
        case 9:
          // Detailed debug messages (possibly zero-length, if none)
          if (buf[i]) {
            info_text.append(buf + i, 1);
          } else {
            state = 0;
          }
          break;
        default:
          LOG(FATAL) << "Broken state machine: " << state;
      }
    }

    // We expect to receive exactly zero or one control messages containing
    // file handles.
    CHECK(!(msg.msg_flags & MSG_CTRUNC));
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg) {
      CHECK_EQ(send_count, 0);
      CHECK_EQ(cmsg->cmsg_level, SOL_SOCKET);
      CHECK_EQ(cmsg->cmsg_type, static_cast<int>(SCM_RIGHTS));

      // Iterate over all channels and extract the file
      // handle that the client sent us (for ACTION_PIPE).
      for (int i = CHAN_STDIN; i < num_chan(); i++) {
        if (action_[i] == ACTION_PIPE) {
          // Get file handle from child
          CHECK(cmsg);
          CHECK_LE((send_count + 1) * sizeof(int),
                   cmsg->cmsg_len - sizeof(struct cmsghdr));
          fd_[i] = reinterpret_cast<int*>(CMSG_DATA(cmsg))[send_count++];

          // Pre-2.4.24 kernels did not support a way of receiving file
          // handles with the close-on-exec flag set. So, mark our end as
          // close-on-exec, again. This opens us up to a minor race condition
          // if anybody else called fork at the wrong time. In the worst
          // case, a child launched from a different thread could access
          // (and hold open) our file handle. As well-behaved fork()/exec()
          // users close unused file handles, this hopefully does not matter
          // much.
          int err;
          if (lss_fcntl(fd_[i], F_SETFD, FD_CLOEXEC, &err) < 0) {
            // Print some extra debugging information, as we have noticed
            // unexpected problems with receiving invalid file handles.
            LOG(ERROR) << "recvmsg() set flags 0x" << std::hex << msg.msg_flags;
            for (int j = CHAN_STDIN, k = 0; j < num_chan(); j++) {
              if (action_[j] == ACTION_PIPE) {
                int fd = reinterpret_cast<int*>(CMSG_DATA(cmsg))[k++];
                char proc[256], link[256] = {0};
                snprintf(proc, sizeof(proc), "/proc/self/fd/%d", fd);
                if (readlink(proc, link, sizeof(link) - 1) < 0) {
                  safestrncpy(link, base::StrError(errno).c_str(),
                              sizeof(link));
                }
                LOG(ERROR) << "Received file handle " << fd << " -> " << link;
              }
            }
            std::vector<std::string> info =
                absl::StrSplit(info_text, '\n', absl::SkipEmpty());
            for (int j = 0; j < info.size(); j++) {
              if (!info[j].empty()) {
                LOG(ERROR) << info[j];
              }
            }

            errno = err;
            PLOG(FATAL) << "fcntl(FD_CLOEXEC) failed (fd=" << fd_[i] << ")";
            return;  // not reached
          }
          //  Make our end non-blocking - all of the other bits are zero.
          if (sys_fcntl(fd_[i], F_SETFL, O_NONBLOCK) < 0) {
            PLOG(FATAL) << "fcntl(O_NONBLOCK) failed";
            return;  // not reached
          }
        }
      }
      CHECK_EQ(send_count * sizeof(int),
               cmsg->cmsg_len - sizeof(struct cmsghdr));
    } else if (len == 0) {
      if (send_count == expected_count || client_errno) {
        break;
      }
      if (!retry--) {
        // We repeatedly got EOF while reading from the socket. This probably
        // means the child was killed by a third party before it could
        // re-organize its file descriptors and send back the ACTION_PIPE ones
        // to us. But in theory it could also be due to some sort of problem
        // with the socket. We try to detect this by sending SIGSTOP to the
        // child. If the child already died, then the SIGSTOP will have no
        // effect, and we will see a non-stopped status when we wait for it
        // below. On the other hand, if the child is still alive, then the
        // SIGSTOP will stop it, and WIFSTOPPED(status) will return true below,
        // suggesting that the reason for the EOFs was a problem with the
        // socket.
        child_died_prematurely = true;
        error_text_ =
            "Child process failed unexpectedly, "
            "and before it called exec()";
        int local_errno;
        if (lss_kill(pid_, SIGSTOP, &local_errno) < 0) {
          LOG(ERROR) << "Error stopping child process " << pid_ << ": "
                     << base::StrError(local_errno);
        }
        break;
      }
      // Every so often, the child thinks it successfully sent a message,
      // but the parent receives a spurious end-of-file. There is not
      // much we can do other than retry.
    }
  }

  // At this point, we know that the child has either successfully called
  // exec(), or it has failed and is about to terminate. In either case,
  // it is no longer going to stomp on the global copy of errno. So, we can
  // make regular system calls, again.

  // Release dirent_scratch; it will never be used again.
  child_buffers_->dirent_scratch.reset();

  // Release set_fd_ information, so that SetChannelFD() can be called again.
  for (int i = CHAN_STDIN; i < num_chan(); i++) {
    set_fd_[i] = -1;
  }

  // Print all the info messages that the child sent to us.
  if (child_setup_logs_enabled_ || VLOG_IS_ON(2)) {
    std::vector<std::string> info =
        absl::StrSplit(info_text, '\n', absl::SkipEmpty());
    for (int i = 0; i < info.size(); i++) {
      if (!info[i].empty()) {
        LOG(INFO) << info[i];
      }
    }
  }

  if (client_errno || !error_text_.empty()) {
    // The child process cannot directly call LOG(FATAL), so we have
    // it return errors back to us, and then call LOG(ERROR) on its
    // behalf.
    error_text_ = absl::StrCat(
        filename_.has_value() ? *filename_
                              : absl::StrCat("[fd=", execve_fd_, "]"),
        ": ", error_text_, ": ", base::StrError(client_errno));
    LOG(ERROR) << error_text_;
    close(pair[0]);
    struct rusage usage;
    int status;
    const int flags = child_died_prematurely ? WUNTRACED : 0;
    if (wait4(pid, &status, flags, &usage) < 0) {
      PLOG(ERROR) << "Error waiting for failed child " << pid;
    } else if (WIFEXITED(status)) {
      if (child_died_prematurely) {
        // The socket to the child got screwed up, but the child exited before
        // our SIGSTOP was delivered. This could happen if the child called
        // SendFatalError() during setup, or if the child binary was exec()ed
        // and then ran to completion.
        LOG(FATAL) << "Child exited prematurely with code "
                   << WEXITSTATUS(status);
      } else if (WEXITSTATUS(status) != 1) {
        LOG(ERROR) << "Child exited with code " << WEXITSTATUS(status);
      }
    } else if (WIFSIGNALED(status)) {
      LOG(ERROR) << "Child killed by signal " << WTERMSIG(status);
    } else if (WIFSTOPPED(status)) {
      if (child_died_prematurely && WSTOPSIG(status) == SIGSTOP) {
        kill(pid, SIGKILL);
        LOG(FATAL) << "Socket to child " << pid
                   << " was closed while child was still alive!?";
      } else {
        LOG(ERROR) << "Child " << pid << " unexpectedly stopped by signal "
                   << WSTOPSIG(status);
      }
    } else {
      LOG(ERROR) << "Child terminated with raw status " << status;
    }
    errno = client_errno;
    return;
  }
  shutdown(pair[0], SHUT_RDWR);
  close(pair[0]);

  set_running(true);
  exit_status_ = 0;
  subproc_mu.lock();
  (*process_map)[pid] = this;
  subproc_mu.unlock();

  *success = true;
}

// ExecChild()
//   N.B. If you override this in a subclass, think very carefully
//        before using execvp(), which has a race condition in this
//        context: Someone else might hold a lock on the environment
//        (via getenv()/setenv()) when execvp is called, in which
//        case we would deadlock. (Guaranteed, because threads do not
//        exist in the child's address space anymore)
//
//        In fact, you cannot call any library call that might hold
//        locks, and you should restrict yourself to only calling
//        direct system calls. Most importantly, do not even think about
//        calling LOG(), and consider the effects of signal handlers
//        that might be inherited from the parent.
//
//        In other words, avoid overriding this method unless you
//        really know what you are doing.
// Called in child process.
void SubProcess::ExecChild() {
  // Note: we could unify all 3 cases below into lss_execveat() call,
  // but that may require updating many existing sandboxes.
  if (execve_fd_ != -1) {
    const char* const* envp = envp_ ? envp_ : (const char* const*)environ;
    lss_execveat(execve_fd_, "", argv_, envp, AT_EMPTY_PATH, &child_errno_);
  } else if (envp_) {
    lss_execve(filename_->c_str(), argv_, envp_, &child_errno_);
  } else {
    lss_execv(filename_->c_str(), argv_, &child_errno_);
  }
  // Alas, we cannot call LOG(FATAL) from the child process. The caller
  // will do this for us, when we return from this method.
}

// Kill()
//    Send the given signal to the process.  No special handling
//    will occur -- if the process exits, the usual things will happen.
//    @return true, normally;
//            false, if we couldn't send the signal - likely because
//                   the process doesn't exist.
// L < subproc_mu
bool SubProcess::Kill(int signal) {
  if (!running()) {
    errno = ESRCH;
    return false;
  }
  CHECK_GE(pid_, 0);
  pid_t target = pid_;
  switch (pidmode_) {
    case PIDMODE_NONE:
      target = pid_;
      break;
    case PIDMODE_PGRP:
    case PIDMODE_SESSION:
      target = -pid_;
      break;
  }
  return (kill(target, signal) == 0);
}

// CheckRunning()
//    Check to see if the process has exited, and get the exit status.
//
//    This calls wait4() to check for sure.  This may cause us to
//    notice that the process has exited, in which case the caller
//    will get this notification *before* the exit callback runs.
//    In that case the exit call will be run the next time the
//    SelectServer regains control.
//
//    @return false if the process has exited;
//            true if the process is still running.
// L < subproc_mu
bool SubProcess::CheckRunning() {
  bool is_running = running();
  if (is_running) {
    LockAndWaitInternal(pid_, WNOHANG);
    is_running = running();
  }
  // Remember whether we have told the client via CheckRunning() or Wait() that
  // the process has died.
  if (!is_running) {
    set_reported_not_running(true);
  }
  return is_running;
}

// GetResourceUsage()
//    Get the resource usage for the process and put it into the
//    given structure.  If a process hasn't yet started, zero the
//    structure.  If a process is currently running, return the
//    current usage.  If the process has exited, return the usage as
//    of process exit.
//    *** At present, this doesn't work on a running process.
//    @param usage The resource usage structure to write to.
// L < subproc_mu
// Called after Start().
void SubProcess::GetResourceUsage(struct rusage* usage) {
  bzero(usage, sizeof(*usage));
  if (!CheckRunning()) {
    *usage = final_usage_;
    return;
  }
}

// Called after Start().
// L < mu_
void SubProcess::Close(Channel chan) {
  if (!chan_valid(chan)) {
    LOG(FATAL) << "Close called with invalid channel: " << chan;
    return;
  }
  mu_.lock();
  int fd = fd_[chan];
  fd_[chan] = -1;
  mu_.unlock();

  if (fd < 0) return;

  // Not only do we close the file handle, but we also shut it down.
  // This is necessary in order to deal with file handles that might
  // have accidentally been leaked to another child in between the time
  // that we received the file handle and when we set the close-on-exec flag.
  shutdown(fd, SHUT_RDWR);

  if (close(fd) < 0) {
    PLOG(ERROR) << "PID " << pid_ << ": Failed to close channel " << chan
                << " fd=" << fd;
  }
}

// HandleExit()
//    This is the single entry point into the exit/cleanup path.  It
//    is called either by Reap() or DoWait(), which figure out which
//    SubProcess has exited, based on the pid.  Both of those should
//    reflect getting this process from wait4(), and that will only
//    happen once for a process.
//
//    @param pid The pid that has exited.
//    @param status The exit status, as returned by wait4().
//    @param usage The resource usage, as reported by wait4().
//                 This may be NULL.
// L < subproc_mu
void SubProcess::HandleExit(pid_t pid, int status, const struct rusage* usage) {
  finish_time_ = absl::Now();
  VLOG(2) << "SubProcess::HandleExit " << *filename_ << "[" << pid
          << "] status=" << status;

  //  Don't clear running_, because regular handlers might get called
  //  before DoExit() runs.
  CHECK(running());
  CHECK_EQ(pid, pid_);
  exit_status_ = status;
  if (usage != nullptr) final_usage_ = *usage;
  DoExit();  //  Subclass might do stuff in here.
}

// SubProcess::DoExit()
//    Run the exit callback and Close() all open descriptors.
//    DoExit is *only* ever called from SubProcess::HandleExit()
// L < subproc_mu
void SubProcess::DoExit() {
  VLOG(2) << "SubProcess::DoExit[" << pid_ << "]";
  set_running(false);
  RunExitCallback();  //  Subclass might do stuff in here.
  // ****  The process is now officially dead and buried.  ****
}

// L < subproc_mu
void SubProcess::RunExitCallback() {
  if (exit_cb_ != nullptr) {
    subproc_mu.lock();
    CHECK_GT(extcount_, 0);   // Must be incremented before HandleExit().
    exit_cb_tid_ = GetTID();  // Allows destructor to test for illegal
                              // destruction in the callback.
    subproc_mu.unlock();

    exit_cb_(this);

    subproc_mu.lock();
    exit_cb_tid_ = -1;
    subproc_mu.unlock();
  }
}

// Reap() - static
//    Reap is called as the result of waitN() indicating that the
//    process has exited.  We look up the PID in the map, erase the
//    entry, and then call HandleExit().
// L < subproc_mu
//  We should require:
//     L >= sp->wait_mu_ if sp in process_map and sp->pid_ == pid
//  but SelectProcess reaches across the abstraction boundary and calls Reap()
//  in some failure conditions. Grr.
SubProcess* SubProcess::Reap(pid_t pid, int status,
                             const struct rusage* usage) {
  VLOG(2) << "Reap(" << pid << ")";
  SubProcess* sp;
  {
    absl::MutexLock l(subproc_mu);

    ProcessMap::iterator sp_i = process_map->find(pid);
    if (sp_i == process_map->end()) {
      //  This was probably bad for some other poor schmoe.
      LOG(WARNING) << "PID " << pid << ": Reaped unknown process.";
      return nullptr;
    }

    //  Erase the entry first, then call HandleExit.
    sp = sp_i->second;
    process_map->erase(sp_i);
    if (sp) {
      sp->RefLocked();  // ensure *sp survives until after HandleExit()
    }
  }
  if (sp) {
    sp->HandleExit(pid, status, usage);
    sp->Unref();  // compensates for RefLocked(), above
  }
  return sp;
}

// Wait()
//    Block until the process exits.  When it does, ensure that the exit
//    callback is run, if one is set.
//    @return true, normally (callback run);
//            false, if the process wasn't running (callback not run).
// L < subproc_mu, wait_mu_
bool SubProcess::Wait() {
  if (running()) {
    LockAndWaitInternal(pid_, 0 /*blocking wait*/);
  }
  // We report the process is done if we haven't previously done so.
  absl::MutexLock l(subproc_mu);
  bool reported = reported_not_running_;
  reported_not_running_ = true;
  return !reported;
}

// LockAndWaitInternal()
//    Wrap wait_mu_ lock around WaitInternal().
//
// To guard against the possibility that two threads race in their use
// of wait4() and Reap(), we protect the calls with a lock: wait_mu_.
// For a blocking wait4(), we use a blocking acquire of the lock.
// For a non-blocking wait4(), we use a non-blocking acquire, and if we fail to
// acquire, we assume the process is still running (because some other thread
// is waiting for it).
//
// L < subproc_mu;  L < wait_mu_ (if WNOHANG)
void SubProcess::LockAndWaitInternal(pid_t sp_pid, int flags) {
  if ((flags & WNOHANG) == 0) {  // a blocking call
    wait_mu_.lock();             // wait until we can get the lock
  } else if (!wait_mu_.try_lock()) {
    return;
  }

  if (running()) {
    flags |= additional_wait_flags_;

    WaitInternal(this, sp_pid, flags);
  }

  wait_mu_.unlock();
}

// WaitInternal()
//    If WNOHANG is unset in flags, block until the process exits.  Otherwise,
//    check whether the process has exited.  In either case, if the process is
//    found to have exited, call Reap(), which will run the exit callback.
/*static*/ void SubProcess::WaitInternal(SubProcess* sp /*may be NULL*/,
                                         pid_t sp_pid, int flags) {
  pid_t pid;
  int status = 0;
  struct rusage usage;

  if (sp) {
    sp->wait_mu_.AssertHeld();
  }

  do {
    base::scheduling::Domain::StartPotentiallyBlockingRegion();
    pid = wait4(sp_pid, &status, flags, &usage);
    base::scheduling::Domain::FinishPotentiallyBlockingRegion();
  } while (pid < 0 && errno == EINTR);

  if (pid == sp_pid) {  //  Just exited.
    if (WIFEXITED(status) || WIFSIGNALED(status)) {
      VLOG(2) << "SubProcess::WaitInternal: wait4() returns " << pid
              << " exited";
      if (WIFEXITED(status)) {
        VLOG(2) << "SubProcess::WaitInternal: sub process " << pid
                << " exited with status " << WEXITSTATUS(status);
      }
      if (WIFSIGNALED(status)) {
        VLOG(2) << "SubProcess::WaitInternal: sub process " << pid
                << " terminated by signal " << WTERMSIG(status);
      }
      Reap(pid, status, &usage);
    } else {
      VLOG(2) << "SubProcess::WaitInternal: wait4() returns " << pid
              << " did not exit";
      if (sp && sp->change_callback_) {
        (*sp->change_callback_)(sp, status);
      }
    }
  } else if (pid < 0 && errno == ECHILD) {
    if (sp != nullptr) {  // don't warn about abandoned processes
      // This branch generally happens if either:
      // 1) Another thread concurrently called `wait` without specifying a
      //    PID, and that call already collected this process, or
      // 2) A SIGCHLD handler has been installed, has been called, and
      //    has performed `wait4` before this `wait4` call.
      LOG(WARNING) << "PID " << sp_pid << ": Missed child's exit.";
    }
    Reap(sp_pid, kStatusUnknown, nullptr);
  } else if (pid < 0) {
    PLOG(ERROR) << "PID " << sp_pid << ": Unexpected error from wait4()";
  } else {
    CHECK_EQ(pid, 0);              // still running, nothing to do
    CHECK_NE(flags & WNOHANG, 0);  // must have been a non-blocking wait
  }
}

// DoWait() - static
//    Call wait4() to reap exited children.  For any that
//    have exited, call HandleExit().
//
//    @return The number of processes still in the "running" list.
// L < subproc_mu
int SubProcess::DoWait() {
  DoWaitInternal();
  return ProcessCount();
}

// DoWaitInternal() - static
//    This routine has the same specification as DoWait() except
//    that on non-POSIX systems it may be called only within *subproc_thread,
//    and it returns no value.
// L < subproc_mu
void SubProcess::DoWaitInternal() {
  // list all the SubProcesses not using ptrace, and all abandoned child pids
  std::vector<std::pair<pid_t, SubProcess*> > list;
  subproc_mu.lock();
  if (process_map != nullptr) {
    list.reserve(process_map->size());
    for (ProcessMap::iterator sp_i = process_map->begin();
         sp_i != process_map->end(); ++sp_i) {
      list.push_back(*sp_i);

      SubProcess* sp = sp_i->second;
      if (sp != nullptr) sp->RefLocked();
    }
  }
  subproc_mu.unlock();

  // For each process, do a non-blocking wait, then decrement the refcount.
  for (int i = 0; i != list.size(); i++) {
    SubProcess* sp = list[i].second;  // might be NULL
    if (sp == nullptr) {
      WaitInternal(nullptr, list[i].first, WNOHANG);
    } else {
      sp->LockAndWaitInternal(list[i].first, WNOHANG);
      sp->Unref();  // compensates for RefLocked(), above
    }
  }
}

// ProcessCount()
//    Find out how many processes are currently running.
//    @return The number of processes in the "running" list.
// L < subproc_mu
int SubProcess::ProcessCount() {
  absl::MutexLock l(subproc_mu);
  return process_map != nullptr ? process_map->size() : 0;
}

// L < subproc_mu
void SubProcess::GetRunningPids(std::vector<pid_t>* pids) {
  absl::MutexLock l(subproc_mu);
  if (process_map != nullptr) {
    for (ProcessMap::const_iterator sp_i = process_map->begin();
         sp_i != process_map->end(); ++sp_i) {
      const SubProcess* sp = sp_i->second;
      if (sp->running_) {
        pids->push_back(sp_i->first);
      }
    }
  }
}

// L < subproc_mu
int SubProcess::ProcessListMemoryUsage(int64_t* rss) {
  int process_count = 0;
  int64_t total_mem = 0;
  std::vector<pid_t> pids;
  GetRunningPids(&pids);
  for (int i = 0; i < pids.size(); i++) {
    int64_t size;
    if ((size = MemoryUsage(pids[i])) != -1) {
      total_mem += size;
      process_count++;
    } else {
      LOG(INFO) << "Failed to get memory usage for pid " << pids[i];
    }
  }
  *rss = total_mem;
  return process_count;
}

// Dup stdin, stdout, or stderr and set it as the channel FD.  Returns false on
// errors other than EBADF, or true otherwise.
static bool MaybeDupDescriptor(SubProcess* sp, Channel chan) {
  int fd = dup(chan);
  if (fd == -1) {
    // Do nothing on EBADF; it indicates that our channel is closed.  If we do
    // nothing here then SubProcess ends up reopening /dev/null onto the
    // channel.
    if (errno != EBADF) {
      PLOG(ERROR) << "Couldn't dup fd " << chan;
      return false;
    }
  } else {
    sp->SetChannelFD(chan, fd);
  }
  return true;
}

// System()
//   Emulate the system() library function
//   Compatibility notes:
//    - Reopens stdin/stdout/stderr that are closed in the parent to be
//      /dev/null. This is technically not POSIXly correct unless setuid/setgid
//      is involved, but seems to be better tolerated by many binaries.
//    - *closes* all channels besides stdin/stdout/stderr, even if they are not
//      FD_CLOEXEC
// L < subproc_mu
/* static */ int SubProcess::System(absl::string_view cmd) {
  SubProcess sp;
  sp.SetShellCommand(cmd);
  // Intentionally not using ACTION_DUPPARENT because that will leave the
  // channel closed if it's closed in the parent process (vs. reopening
  // /dev/null).
  if (!MaybeDupDescriptor(&sp, CHAN_STDIN)) return -1;
  if (!MaybeDupDescriptor(&sp, CHAN_STDOUT)) return -1;
  if (!MaybeDupDescriptor(&sp, CHAN_STDERR)) return -1;
  if (!sp.Start()) return -1;
  if (!sp.Wait()) return -1;
  return sp.exit_status();
}

void SubProcess::SetCallbackOnChange(
    int wait_flags,
    ::util::functional::CallbackFunctor<SubProcess*, int> callback) {
  additional_wait_flags_ = wait_flags;
  change_callback_ = callback;
}

// --------------------------------------------
//  popen/pclose
static absl::flat_hash_map<FILE*, SubProcess*>* subproc_popen_table;
// maps FILE* to SubProcess* for POpen()
// under subproc_mu

// POpen()
//   Emulate the popen() library function.
// L < subproc_mu
/* static */ FILE* SubProcess::POpen(absl::string_view cmd,
                                     absl::string_view mode) {
  SubProcess* sp = new SubProcess;
  FILE* fp = nullptr;
  Channel pipe_channel = CHAN_STDIN;
  Channel other_channel = CHAN_STDOUT;
  CHECK(mode == "w" || mode == "r");
  if (mode == "r") {
    pipe_channel = CHAN_STDOUT;
    other_channel = CHAN_STDIN;
  }
  sp->SetShellCommand(cmd);
  sp->SetChannelAction(pipe_channel, ACTION_PIPE);
  sp->SetChannelAction(other_channel, ACTION_DUPPARENT);
  sp->SetChannelAction(CHAN_STDERR, ACTION_DUPPARENT);

  if (!sp->Start()) {
    delete sp;
  } else {
    int pipe_fd = sp->TakeoverFD(pipe_channel);
    fcntl(pipe_fd, F_SETFL, 0);  //  Clear non-blocking.
    fp = fdopen(pipe_fd, std::string(mode).c_str());
    subproc_mu.lock();
    if (subproc_popen_table == nullptr) {
      subproc_popen_table = new absl::flat_hash_map<FILE*, SubProcess*>;
    }
    (*subproc_popen_table)[fp] = sp;
    subproc_mu.unlock();
  }
  return fp;
}

// PClose()
//   Emulate the pclose() library function.
// L < subproc_mu
/* static */ int SubProcess::PClose(FILE* fp) {
  SubProcess* sp = nullptr;
  subproc_mu.lock();
  auto it = subproc_popen_table->find(fp);
  CHECK(it != subproc_popen_table->end());
  sp = it->second;
  subproc_popen_table->erase(it);
  subproc_mu.unlock();
  int result = fclose(fp);
  sp->Wait();
  if (result == 0) {
    result = sp->exit_status();
  }
  delete sp;
  return result;
}

// StringToExecutableFile()
//   Convert a raw string into an executable temporary file.
// L=*
/* static */
int SubProcess::StringToExecutableFile(const char* data, size_t len,
                                       std::string* filename) {
  // Choose the highest-priority temp directory.
  std::vector<std::string> temp_dirs;
  GetExistingTempDirectories(&temp_dirs);
  if (temp_dirs.empty()) {
    errno = ENOENT;
    LOG(ERROR) << "No temporary directories available to extract files into.";
    return -1;
  }

  std::string temp_path(absl::StrCat(temp_dirs[0], "subprocess.XXXXXX"));
  gtl::UniqueArray<char> temp_template(
      strdup_with_new(temp_path.c_str()),
      temp_path.c_str() != nullptr ? strlen(temp_path.c_str()) + 1 : 0);

  // Create a new temporary file.
  int write_fd = TEMP_FAILURE_RETRY(mkstemp(temp_template.data()));
  if (write_fd < 0) {
    PLOG(ERROR) << "mkstemp " << temp_template.data();
    return -1;
  }

  // Unlink the file, but hold onto its file descriptor.
  unlink(temp_template.data());

  // Dump all string data into the file.  This is based on
  // LinuxFileOps::WriteToFD, but we don't want to depend on the file library.
  while (len > 0) {
    ssize_t result = TEMP_FAILURE_RETRY(write(write_fd, data, len));
    if (result <= 0) {
      int saved_errno = errno;
      close(write_fd);
      errno = saved_errno;
      PLOG(ERROR) << "write";
      return -1;
    }
    len -= result;
    data += result;
  }

  // Re-open the file with no permissions, and close the original.
  // This avoids a "Text file busy" error on exec().
  const std::string write_filename = absl::StrCat("/proc/self/fd/", write_fd);
  int exec_fd = TEMP_FAILURE_RETRY(open(write_filename.c_str(), 0));
  close(write_fd);
  if (exec_fd < 0) {
    // (Assuming here that "close" above did not clobber errno.)
    PLOG(ERROR) << "open " << write_filename;
    return exec_fd;
  }

  // Permit execution.
  fchmod(exec_fd, S_IRUSR | S_IXUSR);

  // Generate filename for caller, if requested.
  if (filename != nullptr) {
    *filename = absl::StrCat("/proc/", getpid(), "/fd/", exec_fd);
  }

  // Caller should close() this when done.
  return exec_fd;
}

// Simple support for converting embedded binaries to executable files
// without having to worry about initialization or repeated
// conversion.
ABSL_CONST_INIT static absl::Mutex resource_to_file_lock(absl::kConstInit);
typedef std::unordered_map<const FileToc*, std::string> ResourceToFileMap;
static ResourceToFileMap* resource_to_file_map;
// CachedResourceToExecutableFileOrDie()
// L=*
absl::StatusOr<std::string> SubProcess::CachedResourceToExecutableFile(
    const FileToc* toc) {
  absl::MutexLock l(resource_to_file_lock);
  if (!resource_to_file_map) resource_to_file_map = new ResourceToFileMap();
  std::string& filename = (*resource_to_file_map)[toc];
  if (filename.empty() &&
      StringToExecutableFile(toc->data, toc->size, &filename) < 0) {
    return util::InternalErrorBuilder()
           << "Failed to convert file " << toc->name << ": "
           << base::StrError(errno);
  }
  return filename;
}
std::string SubProcess::CachedResourceToExecutableFileOrDie(
    const FileToc* toc) {
  absl::StatusOr<std::string> filename = CachedResourceToExecutableFile(toc);
  CHECK_OK(filename);
  return *filename;
}

// ------------------------------------------------
// Override system libraries

// This is not 100% accurate emulation, please see compatibility notes in
// SubProcess::System.
int system(const char* cmd) { return SubProcess::System(cmd); }

void SubProcess::EnableChildSetupLogs(bool enable) {
  child_setup_logs_enabled_ = enable;
}
