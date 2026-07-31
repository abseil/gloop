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

// We want access to pread().
#include <fcntl.h>

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iterator>
#include <limits>
#include <type_traits>

#define _GNU_SOURCE 1

#include "gloop/thread/thread.h"

#if defined(__ANDROID__)
#include <android/api-level.h>
#endif
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>  // for va_start and va_end
#include <stdlib.h>  // for abort()
#include <string.h>
#include <sys/mman.h>  // for PAGE_SIZE
#if defined(__Fuchsia__)
#include <zircon/process.h>
#include <zircon/syscalls.h>
#else
#include <sys/resource.h>
#endif
#if defined(__APPLE__)
#include <mach/thread_info.h>  // for MAXTHREADNAMESIZE
#endif
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/prctl.h>
#endif
#if !defined(__APPLE__)
#include <ucontext.h>  // for dumping register contents
#endif
#include <alloca.h>
#include <dlfcn.h>  // for dlsym

#include <algorithm>
#include <atomic>
#include <iostream>  // for std::endl
#include <list>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/dynamic_annotations.h"
#include "absl/base/internal/direct_mmap.h"  // For direct mmap
#include "absl/base/log_severity.h"
#include "absl/base/macros.h"
#include "absl/base/no_destructor.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/debugging/leak_check.h"
#include "absl/debugging/stacktrace.h"
#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/log/log_streamer.h"
#include "absl/log/vlog_is_on.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/internal/create_thread_identity.h"
#include "absl/synchronization/internal/per_thread_sem.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/source_location.h"
#include "gloop/base/config.h"
#include "gloop/base/context.h"
#include "gloop/base/examine_stack.h"
#include "gloop/base/getpc.h"
#include "gloop/base/googleinit.h"
#include "gloop/base/per-thread-sem.h"
#include "gloop/base/port.h"
#include "gloop/base/process_state.h"
#include "gloop/base/raw_logging.h"
#include "gloop/base/raw_printer.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/scheduling_mode.h"
#include "gloop/base/signal-handler.h"
#include "gloop/base/spinlock.h"
#include "gloop/base/static_threadlocal.h"
#include "gloop/base/sysinfo.h"
#include "gloop/base/thread-identity.h"
#include "gloop/base/tracecontext.h"
#include "gloop/thread/config.h"
#include "gloop/thread/cpu_subcontainer.h"
#include "gloop/thread/exit_timeout_seconds.h"
#include "gloop/thread/logger.h"
#include "gloop/thread/os_semaphore.h"
#include "gloop/thread/run_in_thread.h"
#include "gloop/thread/thread-internal.h"
#include "gloop/thread/thread_control.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/wait_state.h"
#include "gloop/thread/watchdog.h"
#include "gloop/util/functional/with_context.h"
#include "gloop/util/gtl/intrusive_list.h"
#include "tcmalloc/malloc_extension.h"

#ifdef THREAD_HAVE_IOPRIORITY
#include "gloop/util/priority/io-priority.h"
#endif

#ifdef MAP_STACK
// Linux specific, available since 2.6.27 and glibc-2.19
// Currently a a no-op according to man pages.
#define THREAD_MAP_STACK MAP_STACK
#else
#define THREAD_MAP_STACK (0)
#endif

#ifdef __linux__
// The <sys/prctl.h> on some systems may not define these macros yet even though
// the kernel may have support for the new PR_SET_VMA syscall, so we explicitly
// define them here.
#ifndef PR_SET_VMA
#define PR_SET_VMA 0x53564d41
#endif

#ifndef PR_SET_VMA_ANON_NAME
#define PR_SET_VMA_ANON_NAME 0
#endif
#endif  // __linux__

#if !THREAD_HAVE_THREAD_CLASS
#error Feature macros and BUILD file are out of sync.
#endif

extern "C" void CoreDumpSanitization_SetupAlternateSignalHandlerStack()
    ABSL_ATTRIBUTE_WEAK;

#ifdef __GLIBC__
// The code below is used to figure out the size of the TLS space.
// TLS variables live in the stack space allocated for each thread, so
// the function is used to automatically adjust requested stack sizes.
// This code only works with GCC since it relies on an internal symbol
// in glibc.  When doing a profile collection run with FDO, the
// compiler will define several TLS variables per object file (making
// this function call frequently necessary during FDO collection).
// p_dl_get_tls_static_info is initialized in ThreadModuleInitializer.
#ifdef __i386__
#define internal_function __attribute((regparm(3), stdcall))
#else
#define internal_function
#endif
extern "C" void _dl_get_tls_static_info(size_t*, size_t*)
    ABSL_ATTRIBUTE_WEAK internal_function;
typedef void (*GetTLSType)(size_t*, size_t*) internal_function;
#undef internal_function

GetTLSType p_dl_get_tls_static_info = nullptr;

namespace {

void InitGetTLSSize() {
  // This complicated sequence is used to support both static and
  // dynamic linking when using the gold linker.
  // _dl_get_tls_static_info is defined in libc.a and ld-linux.so.
  // When linking statically, the symbol is available.  When linking
  // dynamically, the symbol is *not* available because gold does not
  // consider ld-linux.so to be part of the link unless explicitly
  // specified.  The code below uses the static symbol if available.
  // If not, it will use dlsym to search for the symbol at runtime.
  p_dl_get_tls_static_info = &_dl_get_tls_static_info;
  if (p_dl_get_tls_static_info == nullptr) {
    p_dl_get_tls_static_info =
        (GetTLSType)dlsym(RTLD_NEXT, "_dl_get_tls_static_info");
  }
}

size_t GetTLSSize() {
  size_t tls_size = 0;
  size_t tls_align;

  if (p_dl_get_tls_static_info) {
    (*p_dl_get_tls_static_info)(&tls_size, &tls_align);
  }
  return tls_size;
}
}  // namespace

#else
namespace {
void InitGetTLSSize() {}

size_t GetTLSSize() { return 0; }
}  // namespace
#endif

namespace {

constexpr absl::Duration kBtmExtraExitTimeout = absl::Seconds(15);
[[maybe_unused]] const int kSanitizerMultiplier = 100;

#if !defined(_POSIX_THREAD_ATTR_STACKSIZE)
int pthread_attr_setstacksize(pthread_attr_t* attr, size_t stacksize) {
  return 0;
}
#endif
#if !defined(_POSIX_THREAD_ATTR_STACKSIZE)
// Some platforms do not support pthread_attr_setguardsize or
// pthread_attr_getguardsize.
int pthread_attr_setguardsize(pthread_attr_t* attr, size_t guardsize) {
  return 0;
}

int pthread_attr_getguardsize(const pthread_attr_t* attr, size_t* guardsize) {
  *guardsize = 0;
  return 0;
}
#endif

// Linux and Apple both have `pthread_setname_np()`, but the args are
// different ("_np" means non-portable).
#if defined(__linux__)

static int SetThreadNameNoTrunc(const char* name) {
  return pthread_setname_np(pthread_self(), name);
}
// Linux requires names (with nul) fit in 16 chars
const size_t kMaxThreadNameLen = 15;

#elif defined(__APPLE__)

static int SetThreadNameNoTrunc(const char* name) {
  // On Apple platforms, this function doesn't return an error code and it only
  // works on the current thread.
  pthread_setname_np(name);
  return 0;
}
const size_t kMaxThreadNameLen = MAXTHREADNAMESIZE - 1;

#elif defined(__Fuchsia__)

static int SetThreadNameNoTrunc(const char* name) {
  zx_status_t status = zx_object_set_property(zx_thread_self(), ZX_PROP_NAME,
                                              name, strlen(name));
  return status;
}
const size_t kMaxThreadNameLen = ZX_MAX_NAME_LEN - 1;

#else

static int SetThreadNameNoTrunc(const char* name) { return 0; }
const size_t kMaxThreadNameLen = 0;

#endif

static void SetCurrentThreadName(const char* name) {
  char thread_name[kMaxThreadNameLen + 1];
  strncpy(thread_name, name, sizeof(thread_name));
  thread_name[sizeof(thread_name) - 1] = '\0';
  int res = SetThreadNameNoTrunc(thread_name);
  if (res != 0) {
    LOG_FIRST_N(INFO, 1) << "Can't set pthread names: name: \"" << name
                         << "\"; error: " << res;
  }

  // Pass full (unstripped) name to ANNOTATE_THREAD_NAME.
  // Intentionally called after pthread_setname_np so it overrides
  // the stripped name registered via pthread_setname_np.
  ABSL_ANNOTATE_THREAD_NAME(name);
}

#if defined(__ANDROID__) || defined(__APPLE__)
// Disable thread liveness watchdog on Android to reduce idle power consumption.
// See b/179100900, b/172886135 for more information.
static constexpr bool kWatcherThreads = false;
#else
static constexpr bool kWatcherThreads = true;
#endif

}  // namespace

ABSL_FLAG(bool, watch_pthread_manager, kWatcherThreads,
          "Run exit() deadlock watcher thread");
ABSL_FLAG(bool, watch_thread_liveness, kWatcherThreads,
          "Enable watchdogs; this flag may be renamed in the future to more "
          "accurately describe what it does, the current name was kept for "
          "historical reasons (when it controlled both watchdogs and the "
          "thread liveness watcher, which has been deleted).");

ABSL_FLAG(bool, use_thread_subcontainers, false,
          "Enable CPU subcontainers. Each threadpool will create a CPU "
          "subcontainer, so that threadspools have roughly the same CPU "
          "shares, regardless of the number of threads in each. For more "
          "details, see //gloop/thread/cpu_subcontainer.cc.");

ABSL_FLAG(bool, stacktrace_skip_waiting_threads, false,
          "Whether to skip printing stacktraces of threads waiting for work.");

// Starts watcher threads if enabled.
static void Thread_EnsureWatchersRunIfNeeded();

namespace thread {

// Returns true if the stack allocation must include space for the guard area.
extern bool StackShouldIncludeGuardSize() {
#if defined(__ANDROID__) && defined(__ANDROID_API_P__)
  return __ANDROID_API__ < __ANDROID_API_P__;
#elif defined(__ANDROID__)
  // __ANDROID_API_P__ is not defined, so assume this build is targeting an
  // earlier version of Android.
  return true;
#elif defined(__linux__)
#if defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 27)
  return false;
#else
  return true;
#endif
#else
  return true;  // Not glibc
#endif
#else
  return false;
#endif
}

namespace {

bool IsValidThreadNameCharacter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || (c == '-') || (c == '_');
}

}  // namespace

// Return true if name_prefix is a valid thread name prefix.
bool IsValidThreadNamePrefix(const absl::string_view name_prefix) {
  // Names may contain only A-Z, a-z, 0-9, - and _ and may not start
  // with a digit.
  for (char c : name_prefix) {
    if (!IsValidThreadNameCharacter(c)) return false;
  }
  if (!name_prefix.empty() && name_prefix[0] >= '0' && name_prefix[0] <= '9') {
    return false;
  }
  return true;
}

std::string SanitizeThreadNamePrefix(std::string name_prefix) {
  // Replace invalid characters with underscores.
  for (auto& c : name_prefix) {
    if (!IsValidThreadNameCharacter(c)) c = '_';
  }
  // If first character is a digit, replace it with an underscore.
  if (!name_prefix.empty() && name_prefix[0] >= '0' && name_prefix[0] <= '9') {
    name_prefix[0] = '_';
  }
  DCHECK(IsValidThreadNamePrefix(name_prefix));
  return name_prefix;
}

absl::string_view InternalGetCurrentFiberName();

// Fibers needs a way to get the default stack size in order to support a legacy
// mode where one asks for "0" stack space via
// FLAGS_fibers_default_thread_stack_size. See
// InternalRequestedStackSizeToStackSizeClass in fiber-domain-support.cc
namespace internal {
extern const size_t kDefaultRequiredStackSize = 1952 * 1024;
}
}  // namespace thread

Thread::Thread()
    : options_(),
      subcontainer_(nullptr),
      name_prefix_(),
      created_(false),
      needs_join_(false),
      creator_stack_depth_(0) {
  CHECK_EQ(0, thread::internal::OsSemaphoreInit(&tid_set_sem_)) << errno;
}

Thread::Thread(const thread::Options& options,
               const absl::string_view name_prefix)
    : options_(options),
      subcontainer_(nullptr),
      name_prefix_(name_prefix),
      created_(false),
      needs_join_(false),
      creator_stack_depth_(0) {
  // TODO: Turn this into a check after all uses have been fixed.
  if (!thread::IsValidThreadNamePrefix(name_prefix)) {
    ABSL_RAW_LOG(
        ERROR,
        "Thread name prefix \"%s\" contains a disallowed character: "
        "names may contain only A-Z, a-z, 0-9, - and _ and may not start "
        "with a digit.",
        absl::CEscape(name_prefix_).c_str());
  }
  CHECK_EQ(0, thread::internal::OsSemaphoreInit(&tid_set_sem_)) << errno;
}

Thread::~Thread() {
  if (needs_join_) {
    ABSL_RAW_LOG(ERROR,
                 "Joinable thread was not joined.  This will likely leak "
                 "resources associated with the thread.");
  }
  thread::internal::OsSemaphoreDestroy(&tid_set_sem_);
}

void Thread::SetInitialCpuSubContainer(thread::CpuSubContainer* subcontainer) {
  subcontainer_ = subcontainer;
}

void Thread::SetJoinable(bool joinable) {
  ABSL_RAW_CHECK(!created_, "Only call SetJoinable() *before* Start!");
  options_.set_joinable(joinable);
}

void Thread::SetStackSize(size_t bytes) { options_.set_stack_size(bytes); }

void Thread::SetFIFOScheduling() {
  options_.set_scheduling_policy(thread::SCHEDPOLICY_FIFO);
}

void Thread::SetNamePrefix(const absl::string_view name_prefix) {
  CHECK(!created_) << "Only call SetNamePrefix() *before* Start!";
  CHECK(thread::IsValidThreadNamePrefix(name_prefix))
      << "Thread name prefix \"" << absl::CEscape(name_prefix)
      << "\" contains a disallowed character.";

  // Intentionally avoid using name_prefix_ = string(name_prefix) to prevent
  // copying the data twice.
  name_prefix_.assign(name_prefix.data(), name_prefix.size());
}

void Thread::set_nice_priority_level(int level) {
  options_.set_nice_priority_level(level);
}

namespace {

size_t RoundUpToPageSize(size_t size) {
  size_t page_size = static_cast<size_t>(getpagesize());
  return (size + page_size - 1) & ~(page_size - 1);
}

// Return the minimum valid stack size that can be passed to
// pthread_attr_setstacksize.
size_t MinValidStackSize(size_t request_size) {
  if (request_size < PTHREAD_STACK_MIN) request_size = PTHREAD_STACK_MIN;

  // On some systems, pthread_attr_setstacksize() can fail if stacksize is
  // not a multiple of the system page size.
  return RoundUpToPageSize(request_size);
}

}  // namespace

namespace thread::internal {
// Exposed for testing.
size_t MinValidStackSizeWithTlsAndSanitizers(size_t request_size,
                                             size_t* guard_size) {
  // Dynamic testing tools like AddressSanitizer may modify and add
  // additional objects on stack, thus increasing the stack size.
  // So we want to use more stack.
  std::optional<size_t> dynamic_tool_stack_multiplier =
      tcmalloc::MallocExtension::GetNumericProperty(
          "dynamic_tool.stack_size_multiplier");
  if (dynamic_tool_stack_multiplier.has_value()) {
    request_size *= *dynamic_tool_stack_multiplier;
  }

  if (*guard_size == 0) {  // use default guard size
    if (sizeof(void*) > 4) {
      *guard_size = 1 << 20;  // 64-bit addresses => default 1MB guard region
    } else {
      *guard_size = 1 << 14;  // 32-bit addresses => default 16kB guard region
    }
  }
  // Make sure that guard_size is page-aligned. http://b/14272770.
  *guard_size = RoundUpToPageSize(*guard_size);

  if (thread::StackShouldIncludeGuardSize()) {
    request_size += *guard_size;
  }
  return MinValidStackSize(request_size + GetTLSSize());
}
}  // namespace thread::internal

void Thread::CreatePthread(pthread_attr_t& attr) {
  // Start it off
  int res = pthread_create(&tid_, &attr, ThreadBody, this);
  if (res != 0 && options_.scheduling_policy() == thread::SCHEDPOLICY_FIFO) {
    if (res == EPERM && geteuid() != 0) {
      // we are not running as root
      ABSL_RAW_LOG(INFO,
                   "###### Warning: not running as root -- "
                   "can't use high-priority threads");
#if !defined(__Fuchsia__)
      pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
#endif
      res = pthread_create(&tid_, &attr, ThreadBody, this);
    }
  }

  if (res != 0) {
    // EAGAIN might mean that we unable to allocate resources (see man
    // pthread_create) OR that we are racing against the exec syscall (see
    // b/216182558). In the first case it is *probably* harmless to retry as we
    // might get lucky and be able to allocate and in the second case this is
    // most definetly the correct thing to do as we just need to wait for the
    // process to finish exec'ing. Like the go runtime
    // (https://github.com/golang/<link>.c#L94),
    // we wrap pthread_create in a retry loop when it returns EAGAIN, with
    // successively longer backoffs.
    if (res == EAGAIN) {
      // Try again with some backoff each successive time.
      const int retries = 20;
      const absl::Duration min_delay = absl::Milliseconds(100);  // 1/10 seconds
      const absl::Duration backoff = absl::Milliseconds(1000);   // 1 seconds
      // Max backoff ends up being 100 + 1000 * 19 = 19100 ms.
      for (int i = 0; i < retries; i++) {
        res = pthread_create(&tid_, &attr, ThreadBody, this);
        if (res != EAGAIN) {
          break;
        }
        ABSL_RAW_LOG(INFO, "pthread_create returned EAGAIN, trying again.");
        absl::SleepFor(min_delay + i * backoff);
      }

      // We still got EAGAIN after retrying multiple tries, implying this was a
      // resource issue.
      if (res == EAGAIN) {
        ABSL_RAW_LOG(
            FATAL,
            "pthread_create failed due to being unable to allocate "
            "resources (e.g. memory, tid) to construct the thread. This "
            "generally "
            "indicates that you are trying to create too many threads; either "
            "by "
            "implementation error (leaking threads, unbounded thread creation) "
            "or "
            "specification error (memory or address space limits).");
      }
    }

    // We either got EAGAIN, retried, and got a new error, or we got an error
    // other than EAGAIN in the first place.
    if (res != 0) {
      ABSL_RAW_LOG(FATAL, "pthread_create: %s", strerror(res));
    }
  }

  thread::internal::OsSemaphorePost(&tid_set_sem_);
}

void Thread::Start(absl::SourceLocation loc) {
  CHECK(!created_) << "Thread is not restartable!"
                      " Do not call Start() more than once.";
  created_ = true;
  needs_join_ = options_.joinable();

  if (name_prefix_.empty()) {
    std::string file_name = std::string(loc.file_name());
    // Erase file extension.
    size_t dot = file_name.rfind('.');
    if (dot != std::string::npos) file_name.erase(dot);
    name_prefix_ = thread::SanitizeThreadNamePrefix(std::move(file_name));
  }

  Thread_InitStacksIfNecessary();
  Thread_EnsureWatchersRunIfNeeded();

  // Initialize thread attribute structure
  pthread_attr_t attr;
  int detach =
      (options_.joinable() ? PTHREAD_CREATE_JOINABLE : PTHREAD_CREATE_DETACHED);
  CHECK_EQ(pthread_attr_init(&attr), 0);
  // we don't CHECK() here because some systems don't support it.
#if !defined(__Fuchsia__)
  pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
#endif
  CHECK_EQ(pthread_attr_setdetachstate(&attr, detach), 0);

  // Use a fixed default size if no other size has been specified.  We use a
  // size a little under 2MB to avoid http://b/23446180, in which the kernel may
  // allocate a huge page for the stack if its alignment happens to permit that.
  size_t required_stack = thread::internal::kDefaultRequiredStackSize;
  if (options_.stack_size() != 0) {
    required_stack = options_.stack_size();
  }

  size_t guard_size = options_.guard_size();

  const size_t stack_size =
      thread::internal::MinValidStackSizeWithTlsAndSanitizers(required_stack,
                                                              &guard_size);
  if (VLOG_IS_ON(2)) {
    ABSL_RAW_LOG(
        INFO, "Thread stack size: %ld (required: %ld, guard: %ld, TLS: %ld)",
        static_cast<long>(stack_size), static_cast<long>(required_stack),
        static_cast<long>(guard_size), static_cast<long>(GetTLSSize()));
  }
  int ret;
  if ((ret = pthread_attr_setstacksize(&attr, stack_size)) != 0) {
    ABSL_RAW_LOG(FATAL,
                 "pthread_attr_setstacksize: (%s) required_stack = %" PRIuS
                 ", PTHREAD_STACK_MIN= %" PRIuS "",
                 strerror(ret), required_stack,
                 static_cast<size_t>(PTHREAD_STACK_MIN));
  }

  if ((ret = pthread_attr_setguardsize(&attr, guard_size)) != 0) {
    ABSL_RAW_LOG(FATAL, "pthread_attr_setguardsize: (%s)", strerror(ret));
  }
  switch (options_.scheduling_policy()) {
    case thread::SCHEDPOLICY_FIFO: {
      CHECK(subcontainer_ == nullptr);
      sched_param sched_param;
      pthread_attr_getschedparam(&attr, &sched_param);
#if !defined(__Fuchsia__)
      pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
#endif
      if (options_.sched_priority() < 0) {
        sched_param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 1;
      } else {
#ifdef THREAD_HAVE_IOPRIORITY
        // pthread_attr_setinheritsched call below is needed for the
        // sched policy and priority to work in ThreadTest.SchedPriority
        // test in thread_unittest.cc (when it is run manually as root).
        //
        // However, pthread_attr_setinheritsched is not defined for Android
        // google3 targets/tests (as in --config=android_arm). In addition,
        // some build tools for fuchsia fail if the call below is made
        // (pthread_create below returns EINVAL). So at the moment the call
        // is restricted to the new case of when sched_priority option is set,
        // even though it may be needed in other SCHED_FIFO cases to work
        // properly, as some "legacy" cases fail if PTHREAD_EXPLICIT_SCHED
        // is set.
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
#endif
        sched_param.sched_priority = options_.sched_priority();
      }
      pthread_attr_setschedparam(&attr, &sched_param);
      break;
    }
    default:
      break;
  }

  creator_stack_depth_ =
      absl::GetStackTrace(creator_stack_, std::size(creator_stack_), 0);

  // Do not access 'this' below this point as
  // it may be destroyed by a detached thread.
  CreatePthread(attr);

  // Kill the attribute structure
  ABSL_RAW_CHECK(pthread_attr_destroy(&attr) == 0,
                 "pthread_attr_destroy failed");
}

void Thread::Join() {
  CHECK(options_.joinable());
  CHECK(created_) << "Thread was not started before attempting to join";
  std::atomic<int>* blocked_count_ptr =
      absl::synchronization_internal::PerThreadSem::GetThreadBlockedCounter();
  if (blocked_count_ptr != nullptr) {
    // Mark this thread as blocked (for benefit of auto-sizing threadpools).
    blocked_count_ptr->fetch_add(1, std::memory_order_relaxed);
  }
  base::scheduling::Domain::StartPotentiallyBlockingRegion();
  int result = pthread_join(tid_, nullptr);
  base::scheduling::Domain::FinishPotentiallyBlockingRegion();
  if (blocked_count_ptr != nullptr) {
    blocked_count_ptr->fetch_sub(1, std::memory_order_relaxed);
  }
  CHECK_EQ(0, result) << ": Error code returned was " << result << ". "
                      << (result == EDEADLK
                              ? "(Maybe thread tries to join itself?) "
                              : "")
                      << "See the pthread_join man page for error codes.";
  needs_join_ = false;
}

// -------------------------------------------------------------------------
// Keep track of all live threads
// -------------------------------------------------------------------------

namespace {

// Maximum length of thread status string.
constexpr size_t kThreadStatusLength = 1000;

// guaranteed zero ucontext_t for use when no real ucontext_t is
// available
const ucontext_t zero_uc = {0};

constexpr size_t kMaxFiberNameLength = 64;

// Clang on IOS does not support C++11 thread_local, so we have to use
// STATIC_THREAD_LOCAL.
STATIC_THREAD_LOCAL(absl::string_view, tls_fiber_name);
}  // namespace

// Structure into which stack trace is extracted
struct StackTrace {
  void* stack[kStackCount];
  int depth;

  // Worker thread wait state of this thread at the time of collection.
  thread::WaitStateScope::WaitState wait_state;

  // Only populated if a Python interpreter is linked in (see
  // `IsCurrentThreadHoldingPythonGil()`). Note that it is possible for
  // this to be true on multiple stack traces, as we query whether each
  // thread is holding the GIL at a different moment.
  bool holding_python_gil = false;

  size_t stack_size;
  size_t stack_usage;
  // TODO: Add name if available
  ucontext_t uc;  // CPU context
  // NOTE: it may not be safe to use thread_status if the trace
  // context status strings are ever deallocated.
  char thread_status[kThreadStatusLength];
  char fiber_name[kMaxFiberNameLength];
  // Records notes_version_ of the thread at the time the stack trace was taken.
  int64_t notes_version;

  // The trace_id of the TraceContext currently installed on this thread.
  uint64_t context_trace_id;
};

namespace {

void CopyUcontext(ucontext_t* dst, const ucontext_t* src) {
#if defined(__linux__) && (defined(__i386__) || defined(__x86_64__))
  // Copying of ucontext_t argument of signal handler is tricky on x86.
  // User-space definition of ucontext_t contains uc_flags, uc_link, uc_stack,
  // uc_mcontext and also an undocumented __fpregs_mem members.
  // Kernel allocates ucontext_t on stack, but it does not always allocate
  // space for __fpregs_mem (if the task was not using FP, it is not
  // allocated/saved/restored; see get_sigframe function in
  // arch/x86/kernel/signal.c for details).
  // On top of that user-space definition of sigset_t contains space for
  // 1024 signals, while kernel-space allocates space for only 64 signals.
  // So if we blindly copy whole ucontext_t argument of a signal handler,
  // we will do an out-of-bounds access to stack and potentially cause
  // paging fault. The maximum distance of OOB access would be:
  // sizeof(__fpregs_mem) + (1024 - 64) / CHAR_BIT == 512 + 120 = 632 bytes.
  //
  // Carefully tread as follows:
  // 1. Check that the layout of ucontext_t matches our expectations.
  // 2. Calculate size of the always allocated part of ucontext_t.
  // 3. Copy the always allocated part.
  // 4. In debug mode spray the rest of ucontext_t with a specific byte pattern.
  static_assert(
      offsetof(ucontext_t, uc_flags) < offsetof(ucontext_t, uc_sigmask),
      "unexpected_ucontext_t_layout");
  static_assert(
      offsetof(ucontext_t, uc_link) < offsetof(ucontext_t, uc_sigmask),
      "unexpected_ucontext_t_layout");
  static_assert(
      offsetof(ucontext_t, uc_stack) < offsetof(ucontext_t, uc_sigmask),
      "unexpected_ucontext_t_layout");
  static_assert(
      offsetof(ucontext_t, uc_mcontext) < offsetof(ucontext_t, uc_sigmask),
      "unexpected_ucontext_t_layout");
  const size_t size = offsetof(ucontext_t, uc_sigmask) + (NSIG - 1) / CHAR_BIT;
  static_assert(size <= sizeof(ucontext_t), "unexpected_ucontext_t_layout");
  memcpy(dst, src, size);
#ifndef NDEBUG
  memset(reinterpret_cast<char*>(dst) + size, 0xAB, sizeof(ucontext_t) - size);
#endif
#elif defined(__linux__) && defined(__aarch64__)
  static_assert(
      offsetof(ucontext_t, uc_flags) < offsetof(ucontext_t, uc_mcontext),
      "unexpected ucontext_t layout on arm");
  static_assert(
      offsetof(ucontext_t, uc_link) < offsetof(ucontext_t, uc_mcontext),
      "unexpected ucontext_t layout on arm");
  static_assert(
      offsetof(ucontext_t, uc_stack) < offsetof(ucontext_t, uc_mcontext),
      "unexpected ucontext_t layout on arm");
  static_assert(
      offsetof(ucontext_t, uc_sigmask) < offsetof(ucontext_t, uc_mcontext),
      "unexpected ucontext_t layout on arm");
  // The last field of uc_mcontext is reserved for Arm extensions and may be
  // partially allocated by the kernel based on the presence of extensions.
  // We are skipping all reserved fields since they are not used in the stack
  // traces right now. If we decide to add extra context in the future the
  // fields must by synced with the kernel setup of the sigframe at
  // arch/arm64/kernel/signal.c.
  const size_t size = offsetof(ucontext_t, uc_mcontext.__reserved);
  static_assert(size <= sizeof(ucontext_t),
                "unexpected ucontext_t layout on arm");

  memcpy(dst, src, size);
#ifndef NDEBUG
  memset(reinterpret_cast<char*>(dst) + size, 0xAB, sizeof(ucontext_t) - size);
#endif
#else   // !__linux__ || !(__i386__ || __x86_64__ || __aarch64__)
  // We are not aware of any such troubles on other architectures,
  // so just copy the struct.
  memcpy(dst, src, sizeof(*dst));
#endif  // __linux__ && (__i386__ || __x86_64__)
}

void* MaybeGetPC(const StackTrace* trace) { return GetPC(trace->uc); }

}  // namespace

// This weak symbol is overridden when the program has a Python
// interpreter linked in. It returns a non-NULL pointer if the current
// thread holds the Python GIL, and NULL otherwise
// (https://docs.python.org/3/c-api/threads.html#c.PyThreadState_GetUnchecked).
// This function is async-signal-safe.
typedef struct _ts PyThreadState;
extern "C" ABSL_ATTRIBUTE_WEAK PyThreadState* PyThreadState_GetUnchecked() {
  return nullptr;
}

namespace {
// Returns true if a Python interpreter is linked in, and the current
// thread holds the Python GIL. This is async-signal-safe.
bool IsCurrentThreadHoldingPythonGil() {
#if !PORTABLE_BASE
  return PyThreadState_GetUnchecked() != nullptr;
#else
  return false;
#endif
}
}  // namespace

// 'context' may be nullptr, in which case an all-zero ucontext_t will be used.
//
// (Note that this function must be marked noinline for this to work reliably.)
ABSL_ATTRIBUTE_NOINLINE void FillStackTrace(StackTrace* trace,
                                            const ucontext_t* context,
                                            int skip) {
  // Skip an extra frame for this function.
  const int frames_to_skip = skip + 1;
  if (context == nullptr) {
    context = &zero_uc;
  }
  CopyUcontext(&trace->uc, context);
#if defined(ENABLE_COMBINED_UNWINDER)
  // For the combined unwinder, the top-most active routine shows up as a
  // regular frame, so we don't need to get it from the signal ucontext.
  constexpr bool kCanHaveDuplicatedLeaf = false;
#else
  constexpr bool kCanHaveDuplicatedLeaf = true;
#endif
  int depth = 0;
  if constexpr (kCanHaveDuplicatedLeaf) {
    void* pc = MaybeGetPC(trace);
    if (pc != nullptr) {
      trace->stack[depth] = pc;
      ++depth;
    }
  }
  depth +=
      absl::GetStackTraceWithContext(&trace->stack[depth], kStackCount - depth,
                                     frames_to_skip, context, nullptr);
  if (kCanHaveDuplicatedLeaf && depth >= 2 &&
      trace->stack[1] == trace->stack[0]) {
    // If the first valid frame has the same value as the leaf
    // frame, it is a strong indication that the leaf address has
    // been duplicated. Remove it so the proto encoder doesn't adjust it by -1,
    // since it is unsafe to adjust the leaf address as it is not a call.
    memmove(&trace->stack[1], &trace->stack[2], (depth - 2) * sizeof(void*));
    depth--;
  }
  ABSL_BLOCK_TAIL_CALL_OPTIMIZATION();
  trace->depth = depth;

  trace->stack_size = 0;
  trace->stack_usage = 0;
#ifdef HAVE_GOOGLE_THREAD_STACK
  size_t stack_size = GoogleGetThreadStackSize();
  size_t stack_remaining = GoogleGetThreadStackRemaining();
  if (stack_size != GOOGLE_THREAD_STACK_SIZE_UNKNOWN &&
      stack_remaining != GOOGLE_THREAD_STACK_SIZE_UNKNOWN) {
    trace->stack_size = stack_size;
    trace->stack_usage = stack_size - stack_remaining;
  }
#endif

  // Copy the Context thread_status string, if one is available.
  // We copy, rather than just keeping a pointer, in case the thread
  // clobbers or frees the status string right after we grab the trace.
  trace->thread_status[0] = '\0';
  const char* current_status = base::CurrentThreadStatus();
  if (current_status != nullptr) {
    // Would prefer to use safestrncpy, but it is not allowed here.
    strncat(trace->thread_status, current_status,
            sizeof(trace->thread_status) - 1);
  }

  const absl::string_view fiber_name = ::thread::InternalGetCurrentFiberName();
  if (fiber_name.empty()) {
    trace->fiber_name[0] = '\0';
  } else {
    const int sz = std::min(kMaxFiberNameLength - 1, fiber_name.size());
    memcpy(trace->fiber_name, fiber_name.data(), sz);
    trace->fiber_name[sz] = '\0';
  }

  // Record the notes_version_ at the time of the stack trace. `thread` should
  // be non-null in all code that respects our module boundary. However, it may
  // be null in tests like <path>
  const LiveThread* thread = Thread_GetMyLiveThread();
  trace->notes_version = thread != nullptr ? thread->notes_version_.load() : 0;

  if (thread != nullptr) {
    trace->wait_state =
        thread->identity_->wait_state.load(std::memory_order_relaxed);
  } else {
    trace->wait_state = thread::WaitStateScope::WaitState::kActive;
  }

  trace->holding_python_gil = IsCurrentThreadHoldingPythonGil();
}

void FillCurrentRequestInfo(StackTrace* trace) {
  const TraceContext* tc = base::CurrentTraceContextNoAlloc();
  trace->context_trace_id = tc != nullptr ? tc->global_id() : 0;
}

int StackTrace_GetPCs(const StackTrace* trace, int max_pcs,
                      const void** pc_buffer) {
  const int count = std::min(max_pcs, trace->depth);
  if (count <= 0) return 0;
  memcpy(pc_buffer, trace->stack, count * sizeof(pc_buffer[0]));
  return count;
}

const ucontext_t* StackTrace_GetUContext(const StackTrace* trace) {
  return &trace->uc;
}

uint64_t StackTrace_GetTraceId(const StackTrace* trace) {
  return trace->context_trace_id;
}

uint64_t StackTrace_GetCensusRootId(const StackTrace* trace) { return 0; }

absl::Time StackTrace_GetRequestStartTime(const StackTrace* trace) {
  return absl::UnixEpoch();
}

size_t StackTrace_GetStackSize(const StackTrace* trace) {
  return trace->stack_size;
}

size_t StackTrace_GetStackUsage(const StackTrace* trace) {
  return trace->stack_usage;
}

bool StackTrace_IsHoldingPythonGil(const StackTrace* trace) {
  return trace->holding_python_gil;
}

class LiveThread;

namespace {

// This class is a wrapper around a doubly-linked list of all known threads (in
// creation order)
class LiveThreadList {
 public:
  LiveThreadList() = default;

  // This type is neither copyable nor movable.
  LiveThreadList(const LiveThreadList&) = delete;
  LiveThreadList& operator=(const LiveThreadList&) = delete;
  ~LiveThreadList() = default;

  // Adds live_thread to this list. It should not already be on the list. This
  // method is static since otherwise, if we call Add() before main, we get
  // undefined behavior error from ubsan for accessing "this" (even though we do
  // not access any members of "this").
  static void Add(LiveThread* live_thread);

  // Removes live_thread from this list. It should already be on the list.
  void Remove(LiveThread* live_thread);

  // Calls 'action' with each 'LiveThread*' in the list; 'action' is also passed
  // a 'bool*', which if set true will cause iteration to stop. Each call to
  // 'action' is made sequentially. The called code must not reentrantly invoke
  // methods on this list at risk of deadlock. The iteration may not see a
  // snapshot of threads (e.g., if a thread is concurrently removed), but it is
  // guaranteed a single pointer value won't appear more than once (e.g., even
  // if a new thread with the same pointer value is concurrently re-added).
  //
  // (Note that this function must be marked noinline for stack traces to work
  // reliably at different optimization levels.)
  template <typename Action>
  ABSL_ATTRIBUTE_NOINLINE void Iterate(const Action& action);

  // Signal-safe alternative to Iterate above. If it'd be necessary to block to
  // acquire any locks, this function instead does nothing and returns false.
  // Also, when iteration is performed with this method, the list is fully
  // locked, so Add/Remove calls are blocked.
  //
  // (Note that this function must be marked noinline for stack traces to work
  // reliably at different optimization levels.)
  template <typename Action>
  ABSL_ATTRIBUTE_NOINLINE bool IterateSignalSafe(const Action& action);

 private:
  void ProcessAddedThreadsLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Sentinel value assigned to LiveThread list_seq_ when the thread is being
  // removed. This value must be greater than any assigned by latest_list_seq_
  // so as to prevent the thread from being included in subsequent iterations.
  static constexpr int64_t kRemovingFromListSeq =
      std::numeric_limits<int64_t>::max();

  absl::Mutex mu_;
  // main list of threads
  gtl::intrusive_list<LiveThread> list_ ABSL_GUARDED_BY(mu_);
  // A sequence number which monotonically increases for each batch of threads
  // added by ProcessAddedThreadsLocked().  May be used to filter against newly
  // added threads by iteration operations.
  int64_t latest_list_seq_ ABSL_GUARDED_BY(mu_) = 0;
};

// The global list of live_threads used by the rest of the code
static LiveThreadList* live_threads;
static std::atomic<LiveThread*> new_live_threads;

// Every google3 thread within the binary participates within the LiveThread
// list.  To avoid a global mutex within the creation path, Add() uses a
// separate lock-free list.  Operations which interact with the list (e.g.
// iteration, removal) must first call ProcessAddedThreadsLocked() so that new
// threads are visible.
void LiveThreadList::Add(LiveThread* live_thread) {
  LiveThread* old_top = new_live_threads.load(std::memory_order_relaxed);
  do {
    live_thread->next_unprocessed_ = old_top;
  } while (!new_live_threads.compare_exchange_weak(old_top, live_thread,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed));
}

// Move threads from Add()'s lock-free list to the global list.  All threads
// moved within the same 'batch' will have an equivalent, monotonically
// increasing, sequence number.  This is used by iteration operations to ensure
// that we do not interact with threads created after iteration began.
// REQUIRES: mu_ must be held.
void LiveThreadList::ProcessAddedThreadsLocked() {
  LiveThread* t = new_live_threads.exchange(nullptr, std::memory_order_acquire);

  if (t != nullptr) {
    int64_t list_seq = ++latest_list_seq_;
    auto insert_before = list_.end();

    do {
      t->list_seq_ = list_seq;
      // new_live_threads is a stack (LIFO), we restore a FIFO ordering here so
      // that threads appear to iterators in creation order.
      insert_before = list_.insert(insert_before, t);
      t = t->next_unprocessed_;
    } while (t != nullptr);
  }
}

void LiveThreadList::Remove(LiveThread* live_thread) {
  mu_.lock();
  // It's possible that we're still on the new-thread list.  Drain it first.
  ProcessAddedThreadsLocked();
  // Prevent any new concurrent list iteration from visiting live_thread, and
  // wait for any iteration already-running on live_thread to complete.
  live_thread->list_seq_ = kRemovingFromListSeq;
  mu_.Await(absl::Condition(
      +[](LiveThread* t) ABSL_SHARED_LOCKS_REQUIRED(mu_) {
        return t->num_currently_iterating_ == 0;
      },
      live_thread));
  list_.erase(live_thread);
  mu_.unlock();
}

template <typename Action>
ABSL_ATTRIBUTE_NOINLINE void LiveThreadList::Iterate(const Action& action) {
  mu_.lock();
  // Retrieve threads Add()-ed before iteration began.
  ProcessAddedThreadsLocked();
  // Filter iteration by the latest-assigned sequence to prevent the possibility
  // of iterating the same pointer value more than once (e.g., a thread that's
  // removed/deleted, and a new thread allocated/added with the same address).
  int64_t latest_visible_list_seq = latest_list_seq_;
  bool stop = false;
  for (LiveThread& thread : list_) {
    if (thread.list_seq_ > latest_visible_list_seq) {
      continue;
    }
    thread.num_currently_iterating_++;
    mu_.unlock();
    action(&thread, &stop);
    mu_.lock();
    thread.num_currently_iterating_--;
    if (stop) {
      break;
    }
  }
  mu_.unlock();
}

template <typename Action>
ABSL_ATTRIBUTE_NOINLINE bool LiveThreadList::IterateSignalSafe(
    const Action& action) {
  if (!mu_.try_lock()) {
    return false;
  }
  // Retrieve threads Add()-ed before iteration began.
  ProcessAddedThreadsLocked();
  bool stop = false;
  for (LiveThread& thread : list_) {
    action(&thread, &stop);
    if (stop) {
      break;
    }
  }
  mu_.unlock();
  return true;
}

}  // namespace

// Scoped pointer to current thread's LiveThread structure.  This
// variable's LiveThread pointer is non-nullptr from the time a thread is
// registered as live until the thread exits.
//
// The thread-local variable's destructor will arrange for the
// LiveThread object to be destroyed on thread exit.  See also the
// comments in LiveThread::~LiveThread.
typedef std::unique_ptr<LiveThread> LiveThreadHolder;
STATIC_THREAD_LOCAL(LiveThreadHolder, my_live_thread_holder);

// Initializes a LiveThread object to values in the current thread.
// This minimally initializes the stack trace (to have no entries),
// but the caller can fill it in further after calling this function,
// if desired.
LiveThread::LiveThread(const absl::string_view name_prefix)
    : thread_id_(GetTID()),
      creator_stack_depth_(0),
      alt_signal_stack_size_(0),
      name_(nullptr),
      tid_(pthread_self()),
      alt_signal_stack_addr_(nullptr),
      identity_(
          absl::synchronization_internal::GetOrCreateCurrentThreadIdentity()),
      name_prefix_(nullptr) {
  const std::string prefix_to_use(name_prefix.empty() ? "unnamed"
                                                      : name_prefix);
  name_prefix_ = strdup(prefix_to_use.c_str());
  CHECK_NE(static_cast<char*>(nullptr), name_prefix_);
  CHECK_NE(-1, asprintf(&name_, "%s/%lld", prefix_to_use.c_str(),
                        static_cast<long long>(thread_id_)));  // NOLINT
}

void LiveThread::MakeLive() {
  LiveThreadList::Add(this);

  LiveThreadHolder* live_thread_holder = my_live_thread_holder.pointer();
  CHECK_EQ(static_cast<LiveThread*>(nullptr), live_thread_holder->get());
  live_thread_holder->reset(this);
}

// Calls sigaltstack() and returns true if successful.
static bool SetUpSignalAltStack(stack_t* sigstk) {
#if GOOGLE_HAVE_POSIX_SIGNAL_ALT_STACK
  return sigaltstack(sigstk, nullptr) == 0;
#else
  return false;
#endif
}

LiveThread::~LiveThread() {
  // Remove the thread from the live thread list.

  // Note that this code relies on pthread_key_create's destructor
  // semantics, specifically that before the destructor is called,
  // the pointer data associated with the key will set to nullptr.
  //
  // That means that from (a bit before now) on, any attempt to get
  // the current LiveThread pointer will return nullptr.
  //
  // This is important: e.g., a signal handler that runs now and
  // tries to use Thread_GetMyLiveThread will get nullptr.  Our
  // LiveThread structus is about to be deallocated.  Any signal
  // handler (or other code) that runs between now and thread exit
  // will get nullptr back from Thread_GetMyLiveThread, and therefore
  // shouldn't try to touch this memory.

  live_threads->Remove(this);

  free(name_prefix_);
  free(name_);

  if (alt_signal_stack_addr_) {
    stack_t sigstk = {};
    sigstk.ss_flags = SS_DISABLE;
    if (!SetUpSignalAltStack(&sigstk)) {
      ABSL_RAW_LOG(ERROR,
                   "Disabling alternate signal stack failed: %s.  "
                   "We may be executing on it; leaking it.",
                   strerror(errno));
    } else {
      PCHECK(absl::base_internal::DirectMunmap(alt_signal_stack_addr_,
                                               alt_signal_stack_size_) != -1);
    }
  }
}

pid_t LiveThread_OS_TID(const LiveThread* thread) { return thread->thread_id_; }

pthread_t LiveThread_Pthread_TID(const LiveThread* thread) {
  return thread->tid_;
}

const char* LiveThread_NamePrefix(const LiveThread* thread) {
  return thread->name_prefix_;
}

const char* LiveThread_Name(const LiveThread* thread) { return thread->name_; }

const absl::base_internal::ThreadIdentity* LiveThread_Identity(
    const LiveThread* thread) {
  return thread->identity_;
}

int LiveThread_CreatorStack(const LiveThread* thread, int max_pcs,
                            const void** pc_buffer) {
  int count = std::min(max_pcs, thread->creator_stack_depth_);
  memcpy(pc_buffer, thread->creator_stack_, sizeof(void*) * count);
  return count;
}

// Note that this function requires STATIC_THREAD_LOCAL's safe_pointer
// function to be async-signal-safe and to return the thread-local value
// (rather than nullptr) after that value is allocated.  safe_pointer is
// documented to do this, so this seems reasonable.
const LiveThread* Thread_GetMyLiveThread() {
  LiveThreadHolder* live_thread_holder = my_live_thread_holder.safe_pointer();

  return (live_thread_holder == nullptr) ? nullptr : live_thread_holder->get();
}

namespace {

// Pointer to main thread's LiveThread structure, initialized at
// startup.
LiveThread* main_thread;

// Arrange to record the main thread's ID
class ThreadModuleInitializer {
 public:
  ThreadModuleInitializer() {
    live_threads = new LiveThreadList();

    // There is no creator stack for the main thread.
    main_thread = new LiveThread("main");
    main_thread->MakeLive();

    InitGetTLSSize();
  }
};
ThreadModuleInitializer module_initializer;

void SetupAlternateSignalHandlerStack(LiveThread* thread_info) {
#if !PORTABLE_BASE
  // CoreDumpSanitization is only available in google3 and might not be linked
  // in. If it is linked in it takes up ownership of the altstacks.
  if (&CoreDumpSanitization_SetupAlternateSignalHandlerStack != nullptr) {
    CoreDumpSanitization_SetupAlternateSignalHandlerStack();
    return;
  }
#endif  // !PORTABLE_BASE
  // Create an alternate stack to execute signal handlers. This is useful in
  // detecting stack overflow errors (in which case there is no room to run
  // the signal handler on that stack).
  // We create the alternate stack for every thread so that the signal
  // handlers in multiple threads can run concurrently.
  // Note that SA_ONSTACK is only set for the "failure signals".
  // Handlers for other signals like SIGPROF run on the main stack.
  stack_t sigstk = {};
  sigstk.ss_size = GetRequiredAlternateSignalStackSize();
  sigstk.ss_sp = absl::base_internal::DirectMmap(
      nullptr, sigstk.ss_size, PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS | THREAD_MAP_STACK, -1, 0);
  if (sigstk.ss_sp == MAP_FAILED) {
    ABSL_RAW_LOG(FATAL, "mmap for alternate signal stack: %s", strerror(errno));
  }
  if (!SetUpSignalAltStack(&sigstk)) {
    ABSL_RAW_LOG(FATAL, "sigaltstack: %s", strerror(errno));
  }
  // ~LiveThread() will unmap this region.
  thread_info->alt_signal_stack_addr_ = sigstk.ss_sp;
  thread_info->alt_signal_stack_size_ = sigstk.ss_size;
}

}  // namespace

// -------------------------------------------------------------------------
// Thread body wrapper -- takes control of brand new thread
// -------------------------------------------------------------------------

void* Thread::ThreadBody(void* arg) {
  // WARNING: until `this_thread->Run()`, this code should not acquire
  // cooperative resources, because `this_thread` might be a fiber thread but
  // cannot participate in cooperative scheduling until `this_thread->Run()`.
  // Otherwise, if a fiber `A` attempts to acquire that same resource but
  // instead yields to a different fiber, we may deadlock, because `this_thread`
  // is waiting to be notified to wake up to acquire the resource, but `A` won't
  // be rescheduled until `this_thread` interacts with the fiber scheduler.

#if !PORTABLE_BASE  // TODO: base::Logger is not ported yet.
  static volatile bool always_false = false;
  if (always_false) {
    // Has no effect except to ensure that logger.cc is linked in.
    threadlogger::EnableThreadedLogging(base_logging::WARNING);
  }
#endif
  Thread* this_thread = reinterpret_cast<Thread*>(arg);

  pthread_cleanup_push(Thread::ThreadExitHandler, nullptr);  // nullptr: no args

  LiveThread* thread_info = new LiveThread(this_thread->name_prefix_);

  if (UseAlternateSignalHandlerStack()) {
    SetupAlternateSignalHandlerStack(thread_info);
  }

  // Copy the stack trace of the creator
  thread_info->creator_stack_depth_ =
      std::min(kStackCount, this_thread->creator_stack_depth_);
  for (int d = 0; d < thread_info->creator_stack_depth_; d++) {
    thread_info->creator_stack_[d] = this_thread->creator_stack_[d];
  }

  // We want to be able to get the thread id from the tid_ field whether we are
  // on this thread, the creating thread, or another.
  // Furthermore, ownership of the Thread object after it is started is up to
  // the application: it is legal for a detached thread to delete itself.
  // In summary:
  // - the tid_ field must be set before the creating thread returns from Start;
  // - the creating thread cannot access the Thread object once this thread
  //   enters the Run method (because Run might delete the object);
  // - the tid_ field must be set before this thread uses it or calls Run.
  // Therefore, this thread is forced to wait before entering Run. However, it
  // is very likely that sem_post has already occurred by this point.
  while (thread::internal::OsSemaphoreWait(&this_thread->tid_set_sem_) != 0 &&
         errno == EINTR) {
    // Keep waiting if interrupted by signal.
  }
  DCHECK(pthread_equal(pthread_self(), this_thread->tid_));

  SetCurrentThreadName(LiveThread_NamePrefix(thread_info));

  thread_info->MakeLive();

#if defined(__Fuchsia__)
  // Fuchsia does not support a concept of numerical thread priority so we do
  // nothing here
#else
  if (this_thread->options_.nice_priority_level() != 0) {
    // pthread_attr_setschedparam (etc.) can not change priority of a thread
    // under linux.
    // use nice() here
    errno = 0;
    if (nice(this_thread->options_.nice_priority_level()) != -1 || errno == 0) {
      LOG(INFO) << "Changed the nice priority level by "
                << this_thread->options_.nice_priority_level();
    } else {
      LOG(ERROR) << "Error : " << strerror(errno) << std::endl
                 << "Could not change the nice priority level by "
                 << this_thread->options_.nice_priority_level();
    }
  }
#endif  // defined(__Fuchsia__)

#ifdef THREAD_HAVE_IOPRIORITY
  int io_priority_level = this_thread->options_.io_priority_level();
  IOPriorityClass io_class =
      static_cast<IOPriorityClass>(this_thread->options_.io_class());
  // Set I/O priority for this thread only if it was explicitly
  // configured in the thread options.
  if (io_priority_level >= 0 && io_class >= 0) {
    util::SetIOPriority(GetTID(), io_class, io_priority_level);
  }

  if (this_thread->subcontainer_ != nullptr) {
    this_thread->subcontainer_->RegisterThread();
  }
#endif

#ifdef HAVE_GOOGLE_THREAD_STACK
  // Retrieve the current thread's stack and annotate it with a named VMA to
  // make stack identification in profiles/mappings tractable.
  if (size_t lo, hi; GoogleGetThreadStackLowHigh(&lo, &hi)) {
    const void* stackaddr = reinterpret_cast<const void*>(lo);
    const size_t stacksize = GoogleGetThreadStackSize();

    // Upperbound of what we need, but the kernel allows a longer name.
    char thread_stack_name[32];
    absl::SNPrintF(thread_stack_name, sizeof(thread_stack_name), "stack:%u",
                   thread_info->thread_id_);
    prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, stackaddr, stacksize,
          thread_stack_name);
  }
#endif  // HAVE_GOOGLE_THREAD_STACK

  this_thread->Run();

  // The Thread object may have been deleted by the Run() method, so it should
  // not be accessed beyond this point.
  this_thread = nullptr;

  pthread_cleanup_pop(0);  // 0 to not execute the handler
  return nullptr;
}

namespace {

void ThreadTick() {
  live_threads->Iterate([](LiveThread* t, bool* stop) {
    absl::synchronization_internal::PerThreadSem::Tick(t->identity_);
  });
}

// Body of thread that checks that watchdogs are alive.
void* ThreadLivenessWatcher(void* arg) {
  Thread_RegisterExternalThread("ThreadLivenessWatcher");
  // We do not allocate polling for liveness.
  tcmalloc::MallocExtension::MarkThreadIdle();

  int watchdog_counter = 0;
  while (true) {
    {
      thread::WaitStateScope scope(
          thread::WaitStateScope::WaitState::kWaitingForWork);
      absl::SleepFor(absl::Seconds(1));
    }

    ThreadTick();

    // Every WatchDog::kInternalCheckSeconds, check any WatchDogs that have
    // been created. This is a no-op if there are no WatchDogs.
    watchdog_counter--;
    if (watchdog_counter <= 0) {
      WatchDog::CheckAlive();
      watchdog_counter = WatchDog::kInternalCheckSeconds;
    }
  }
  return nullptr;
}

ABSL_CONST_INIT SpinLock exit_handler_list_lock{
    absl::base_internal::SCHEDULE_KERNEL_ONLY};

std::list<std::function<void()>>* g_exit_handlers
    ABSL_GUARDED_BY(&exit_handler_list_lock);

void RunExitHandlers() {
  SpinLockHolder lock(exit_handler_list_lock);
  if (g_exit_handlers == nullptr) {
    return;
  }

  for (auto& fn : *g_exit_handlers) {
    fn();
  }
}

}  // namespace

void Thread::RegisterExitHandler(std::function<void()> handler) {
  SpinLockHolder lock(exit_handler_list_lock);
  if (g_exit_handlers == nullptr) {
    static absl::NoDestructor<std::list<std::function<void()>>> exit_handlers;
    g_exit_handlers = exit_handlers.get();
  }

  g_exit_handlers->push_back(handler);
}

// to be invoked whenever a thread exits or got cancelled
// Currently, it only dumps stack trace and address map.
void Thread::ThreadExitHandler(void* unused) {
  // skip one stack frame (myself)
  DumpStackTrace(1, DebugWriteToStderr, nullptr);
  DumpAddressMap(DebugWriteToStderr, nullptr);
  DumpStackTrace(
      1, base::DebugWriteToStream,
      &absl::LogErrorStreamer(absl::SourceLocation::current()).stream());
  DumpAddressMap(
      base::DebugWriteToStream,
      &absl::LogErrorStreamer(absl::SourceLocation::current()).stream());

  RunExitHandlers();
}

// -------------------------------------------------------------------------
// Support for detecting deadlocks in exit()
// -------------------------------------------------------------------------

namespace thread::internal {
ABSL_ATTRIBUTE_WEAK int GetProcessMigrationCount();
int GetProcessMigrationCount() { return 0; }
}  // namespace thread::internal

namespace {

void WriteToStderr(const char* message) {
  (void)write(STDERR_FILENO, message, strlen(message));
}

ABSL_ATTRIBUTE_NORETURN void ShutdownTimedOutExit() { _exit(2); }

// Body of thread that guarantees death after timeout_seconds.
//
// This is intended to be a fail-safe mechanism in case I/O is blocking, which
// would cause the logging statements in ExitTimeoutWatcher to hang. As such,
// no I/O or calls to other google3 code are allowed in this function.
void* SleepAndDie(void* timeout_seconds) {
  const absl::Duration time =
      absl::Seconds(reinterpret_cast<intptr_t>(timeout_seconds));
  int migrations = thread::internal::GetProcessMigrationCount();
  absl::SleepFor(time);
  if (migrations != thread::internal::GetProcessMigrationCount()) {
    absl::SleepFor(kBtmExtraExitTimeout);
  }
  ShutdownTimedOutExit();
}

// Body of thread that checks that we're not hanging while trying to
// exit/crash-dump.
//
// This thread loops once a second, checking for whether the flag
// "process_is_dying" (which is declared in google.cc) has been set. Once set
// there is no turning back: after FLAGS_exit_timeout_seconds we send a SIGTRAP;
// after another 15 seconds we call _exit.
//
// Since SIGABRT is commonly used in assertions for invariants, the stacktrace
// it generates shows what invariant was broken.  With the exit timeout we don't
// assert in the context of the blocked thread (which could be many) so the
// crashing stack is always not the right one, possibly confusing the reader
// debugging the crash.  To work around this confusion, SIGTRAP was chosen as
// a non-specific failure signal.
void* ExitTimeoutWatcher(void* arg) {
  Thread_RegisterExternalThread("ExitTimeoutWatcher");
  // We do not allocate while waiting for base::ProcessIsDying().
  tcmalloc::MallocExtension::MarkThreadIdle();

  // Wait until the process_is_dying flag is true. Note: according to
  // process_state.h the flag can not be reset to false.
  {
    thread::WaitStateScope scope(
        thread::WaitStateScope::WaitState::kWaitingForWork);
    while (!base::ProcessIsDying()) {
      absl::SleepFor(absl::Seconds(1));
    }
  }

  tcmalloc::MallocExtension::MarkThreadBusy();

#if defined(ABSL_HAVE_ADDRESS_SANITIZER) || \
    defined(ABSL_HAVE_MEMORY_SANITIZER) || defined(ABSL_HAVE_THREAD_SANITIZER)
  // *San builds often have file/line info, and enable decoding it in
  // the failure stack trace.
  // Thus a CHECK-failure under *San takes a really long time to print, and
  // sometimes runs into the `exit hanging` timeout.
  // In addition, currently the root cause of CHECK failure is not logged
  // until after stack trace is complete (b/214593069).
  // To make failures easier to debug, increase the timeout under sanitizers.
  const int kTimeoutMultiplier = kSanitizerMultiplier;
#else
  // When code coverage collection is enabled, all our timings need to be
  // expanded to let that work complete.
  const int kTimeoutMultiplier = 1;
#endif

  // Delay for FLAGS_exit_timeout_seconds and then send a SIGTRAP.
  // Wait for another 15 seconds and then send a SIGKILL.
  const int kTimeUntilSigtrapSeconds =
      absl::GetFlag(FLAGS_exit_timeout_seconds) * kTimeoutMultiplier;
  const int kTimeFromSigtrapToSigkillSeconds = 15 * kTimeoutMultiplier;

  // Start a new fail-safe thread whose only purpose is to ensure that blocking
  // I/O won't prevent our death.
  pthread_t t;
  pthread_attr_t t_attr;
  intptr_t t_timeout =
      kTimeUntilSigtrapSeconds + kTimeFromSigtrapToSigkillSeconds + 5;
  void* t_arg = reinterpret_cast<void*>(t_timeout);

  if (pthread_attr_init(&t_attr) != 0 ||
      pthread_attr_setdetachstate(&t_attr, PTHREAD_CREATE_DETACHED) != 0 ||
      pthread_create(&t, &t_attr, &SleepAndDie, t_arg) != 0) {
    // We couldn't create the fail-safe thread, so just exit now.
    ShutdownTimedOutExit();
  }

  // Our fail-safe thread is running, we can now do I/O.

  // SIGTRAP if we're still around after the timeout.
  int migrations = thread::internal::GetProcessMigrationCount();
  absl::SleepFor(absl::Seconds(kTimeUntilSigtrapSeconds));
  if (migrations != thread::internal::GetProcessMigrationCount()) {
    WriteToStderr("exit() hanging: SIGTRAP delayed due to task migration\n");
    absl::SleepFor(kBtmExtraExitTimeout);
  }
  WriteToStderr("exit() hanging: killing process with SIGTRAP\n");
  kill(GetMainThreadPid(), SIGTRAP);

  // _exit if we're still around after the timeout.
  absl::SleepFor(absl::Seconds(kTimeFromSigtrapToSigkillSeconds));
  WriteToStderr("exit() hanging: exiting process with _exit()\n");
  ShutdownTimedOutExit();
}

// Default value for stack space after allocating thread
// descriptor and guard. The value chosen based on historical value
// passed to MinValidStackSize on x86 (which had 32kB total for guard
// page and usable space).
const size_t kHelperThreadStackSize = 28 * 1024;

// Start a small helper thread using a detached raw pthread.
// This is a separate function due to the complexity of calculating
// the correct minimum stack size.
// This function CHECKs on any error during thread setup.
void StartHelperThread(void* (*start_routine)(void*)) {
  pthread_attr_t attr;
  pthread_attr_init(&attr);

  int err = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  CHECK_EQ(err, 0) << ": pthread_attr_setdetachstate " << strerror(err);

  size_t required_stack = kHelperThreadStackSize;
  if (thread::StackShouldIncludeGuardSize()) {
    size_t guard_size;
    err = pthread_attr_getguardsize(&attr, &guard_size);
    CHECK_EQ(err, 0) << ": pthread_attr_getguardsize " << strerror(err);
    required_stack += guard_size;
  }
  size_t stack_size = MinValidStackSize(required_stack + GetTLSSize());
  err = pthread_attr_setstacksize(&attr, stack_size);
  CHECK_EQ(err, 0) << ": pthread_attr_setstacksize " << strerror(err);
  pthread_t t;
  err = pthread_create(&t, &attr, start_routine, nullptr);
  CHECK_EQ(err, 0) << ": can't start helper thread: " << strerror(err);
}

}  // namespace

// -------------------------------------------------------------------------
// DetachedThread
// -------------------------------------------------------------------------
namespace {
class DetachedThread : public Thread {
 public:
  // Invokes the given closure and then deletes
  // the thread object (this). The closure itself is not deleted. If you want
  // it deleted use NewCallback to create a self-deleting closure.
  // A DetachedThread should not be accessed once Start() was invoked since it
  // may be deleted at any time. This is why the class is not publicly
  // exposed.
  DetachedThread(const thread::Options& options,
                 const absl::string_view name_prefix,
                 absl::AnyInvocable<void() &&> closure)
      : Thread(options, name_prefix),
        closure_(util::functional::WithCurrentContext(std::move(closure))) {}

 protected:
  void Run() override {
    std::move(closure_)();
    delete this;
  }

 protected:
  absl::AnyInvocable<void() &&> closure_;
};
}  // namespace.

void StartDetachedThread(absl::string_view name_prefix,
                         absl::AnyInvocable<void() &&> closure) {
  DetachedThread* t =
      new DetachedThread(thread::Options(), name_prefix, std::move(closure));
  // t deletes itself. If it didn't finish, don't report a leak.
  absl::IgnoreLeak(t);
  t->Start();
}

void StartDetachedThread(const thread::Options& options,
                         absl::string_view name_prefix,
                         absl::AnyInvocable<void() &&> closure) {
  DetachedThread* t =
      new DetachedThread(options, name_prefix, std::move(closure));
  // t deletes itself. If it didn't finish, don't report a leak.
  absl::IgnoreLeak(t);
  t->Start();
}

int Thread_ForEach(bool (*for_each)(void* arg, const LiveThread* thread),
                   void* for_each_arg,
                   void (*in_each)(void* arg, ucontext_t* uc,
                                   const LiveThread* thread),
                   void* in_each_arg, int in_each_timeout_ms) {
  RunInThread* run_in_thread = RunInThread::Instance();
  int delivery_failures = 0;

  live_threads->Iterate([&](const LiveThread* t, bool* stop) {
    if ((for_each != nullptr && !(*for_each)(for_each_arg, t)) ||
        in_each == nullptr) {
      return;
    }
    if (!run_in_thread->Run(t, in_each, in_each_arg, in_each_timeout_ms)) {
      delivery_failures++;
    }
  });

  return delivery_failures;
}

// -------------------------------------------------------------------------
// Support for generating output
// -------------------------------------------------------------------------

ThreadStackWriter::~ThreadStackWriter() = default;

void ThreadStackWriter::Write(const char* data, int data_length,
                              const ThreadStackWriterOptions& options) {
  Write(data, data_length);
}

void StderrThreadStackWriter::Write(const char* data, int data_length) {
  (void)write(STDERR_FILENO, data, data_length);
}

void LogErrorThreadStackWriter::Write(const char* data, int data_length) {
  LOG(ERROR) << absl::string_view(data, data_length);
}

namespace {

// A subclass that writes to LOG(WARNING)
class LogWarningThreadStackWriter : public ThreadStackWriter {
 public:
  void Write(const char* data, int data_length) override {
    LOG(WARNING) << absl::string_view(data, data_length);
  }
};

// printf-style output generator
void OUTPUT(ThreadStackWriter* writer, const char* format, ...)
    __attribute__((__format__(__printf__, 2, 3)));
void OUTPUT(ThreadStackWriter* writer, const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  char buf[100];
  vsnprintf(buf, sizeof(buf), format, ap);
  writer->Write(buf, strlen(buf));
  va_end(ap);
}

}  // namespace

// Associated DebugWriter
void ThreadDebugWriter(const char* buf, void* writer_arg) {
  ThreadStackWriter* writer = static_cast<ThreadStackWriter*>(writer_arg);
  writer->Write(buf, strlen(buf));
}

// -------------------------------------------------------------------------
// Extract stack traces of all threads
// -------------------------------------------------------------------------

// Forward declaration of function google.cc should call to dump stack
// traces.  vuc is a ucontext_t *.  We use void* to avoid the use of
// ucontext_t on non-POSIX systems in base/google.cc.  (This module is
// not used on non-POSIX systems.)
static void ThreadStackDumper(void* vuc);

static void Thread_InitStacks() {
  // Initialize the RunInThread handler.
  if (RunInThread::Instance()->installed_signal_handler()) {
    // Register hook with google.cc so signal handler calls in here
    // uc is a ucontext_t *.  We use void* to avoid the use
    // of ucontext_t on non-POSIX systems in base/google.cc
    // This module is not used on non-POSIX systems.
    extern void (*thread_stack_dumper)(void* uc);
    thread_stack_dumper = &ThreadStackDumper;
  }
}

void Thread_InitStacksIfNecessary() {
  [[maybe_unused]] static const bool once = [] {
    Thread_InitStacks();
    return false;
  }();
}

namespace {

// Print a wrapped stack trace that looks like:
//  PREFIX 0x2aaaaac40ae7 0x401088 0x2aaaaab3ac55 0x2aaaaab3e8a8
//      0x2aaaaab44b2d 0x2aaaaab3f5af 0x2aaaaab3e793 0x2aaaaab44b0e
//      0x2aaaaab3f381 0x2aaaaab3e5c6 0x2aaaaab3e6f7 0x4064e0 0x2aaaac1f6afa
void PrintWrappedStack(base::RawPrinter* printer, const char* prefix,
                       void* const* stack, int n) {
  // Print a compact form of stack trace, with 80 columns max.
  for (int i = 0; i < n;) {
    int line_start = printer->length();
    printer->Printf("%s", prefix);

    // Fill this line.  Stop when we run out of items, or hit the
    // creator stack trace, or the line is about to overflow.
    while (i < n) {
      char token[50];
      snprintf(token, sizeof(token), " %p", stack[i]);
      if (printer->length() - line_start + strlen(token) >= 80) break;
      i++;
      printer->Printf("%s", token);
    }

    printer->Printf("\n");
    prefix = "     ";
  }
}

void PrintCreatorStack(base::RawPrinter* printer, const LiveThread* thread) {
  // We limit the creator to one line on purpose
  printer->Printf("creator:");
  for (int i = 0; i < thread->creator_stack_depth_; i++) {
    printer->Printf(" %p", thread->creator_stack_[i]);
  }
  printer->Printf("\n");
}

// Scratch buffer used by PrintStackTrace; this enables async-signal-safety by
// avoiding the need for memory allocation, and enables it to be called in
// contexts where stack space is limited. The type is trivially-destructible
// and so may be safely used for objects with static storage duration.
struct PrintArgBuf {
  char data[2048];
};

// Shared global instance of PrintArgBuf for cases where PrintStackTrace may be
// called from a signal handler, so it's not possible to heap-allocate a buffer.
// (The mutex must be acquired with TryLock in such cases for signal safety.)
ABSL_CONST_INIT absl::Mutex shared_print_mu(absl::kConstInit);
PrintArgBuf shared_print_buf ABSL_GUARDED_BY(shared_print_mu);

// Used to pass arguments to PrintStackTrace when invoking ProcessStackTraces,
// i.e., this should be set as the 'process_trace_arg'.
struct PrintArg {
  ThreadStackWriter* writer;
  // Disables any symbolization.
  bool raw;
  // Skips threads marked as waiting for work (not threads waiting on mutexes in
  // general).
  bool skip_waiting_threads;
  PrintArgBuf* buf;

  int total_threads;
  int skipped_threads;

  PrintArg(ThreadStackWriter* writer, bool raw, PrintArgBuf* buf,
           bool skip_waiting_threads)
      : writer(writer),
        raw(raw),
        skip_waiting_threads(skip_waiting_threads),
        buf(buf),
        total_threads(0),
        skipped_threads(0) {}
};

// This function prints the provided stacktrace by the given thread in one of
// the following formats. It always starts with a per-thread header, and the
// stacktrace may or may not be symbolized.
//
// If symbolized (args->raw = false):
//   --- Thread [pthread addr] (name: name/tid) stack: ---
//      0x1 Foo
//      0x2 Bar
//      0x3 Baz
//
// Otherwise:
//   --- Thread [pthread addr] (name: name/tid) stack: ---
//      0x1 0x2 0x3 0x4 ...
void PrintStackTrace(void* print_arg, const LiveThread* thread,
                     const StackTrace* trace) {
  PrintArg* arg = static_cast<PrintArg*>(print_arg);
  ThreadStackWriter* writer = arg->writer;
  PrintArgBuf* buf = arg->buf;

  arg->total_threads++;

  if (arg->skip_waiting_threads && trace != nullptr &&
      trace->wait_state == thread::WaitStateScope::WaitState::kWaitingForWork) {
    arg->skipped_threads++;
    return;
  }

  int depth;
  void* const* stack;
  const char* status;
  const char* fiber_name = nullptr;
  size_t stack_size_kb = 0;
  size_t stack_used_kb = 0;
  ThreadNotesForTrace notes;
  bool holding_python_gil = false;
  if (trace == nullptr) {
    // Print fake info.
    stack = nullptr;
    depth = 0;
#ifdef __linux__
    status =
        "could not fetch userspace stack trace, showing "
        "kernel stack instead.";
#else
    status = "could not fetch stack trace";
#endif
  } else {
    depth = trace->depth;
    stack = trace->stack;
    status = trace->thread_status;
    fiber_name = trace->fiber_name;
    notes = LiveThread_GetNotesForTrace(thread, trace);
    stack_size_kb = StackTrace_GetStackSize(trace) / 1024;
    stack_used_kb = StackTrace_GetStackUsage(trace) / 1024;
    holding_python_gil = trace->holding_python_gil;
    trace = nullptr;  // Catch mistaken access below
  }

  base::RawPrinter printer(buf->data, std::size(buf->data));
  printer.Printf("--- Thread %" GPRIxPTHREAD " (name: %s) stack: ---\n",
                 PRINTABLE_PTHREAD(thread->tid_), LiveThread_Name(thread));
  if (stack_size_kb != 0) {
    if (stack_used_kb != 0) {
      printer.Printf("stack used: %d KiB of %d KiB\n", stack_used_kb,
                     stack_size_kb);
    } else {
      printer.Printf("stack used: ? of %d KiB\n", stack_size_kb);
    }
  }
  if (fiber_name && fiber_name[0] != '\0') {
    printer.Printf("fiber_name: %s\n", fiber_name);
  }
  if (status[0] != '\0') {
    printer.Printf("status: %s\n", status);
  }

  if (notes.notes_changed_since_stack_trace) {
    printer.Printf(
        "(WARNING: Thread notes may be out of date with stack "
        "trace. See <link>.)\n");
  }
  for (const std::string& note : notes.notes) {
    printer.Printf("note: %s\n", note);
  }

  if (holding_python_gil) {
    // Note: It is possible for this to be printed for multiple threads,
    // as thread states are not queried at a single moment in time.
    printer.Printf("python_gil: held\n");
  }

  if (stack == nullptr) {
#ifdef __linux__
    char proc_path[256];
    // Flush output accumulated so far so we can re-use the buffer.
    writer->Write(buf->data, printer.length());
    absl::SNPrintF(proc_path, sizeof(proc_path), "/proc/self/task/%d/stack",
                   LiveThread_OS_TID(thread));

    // PID=0 since we are using /self/ in the path above and do not need
    // string formatting.
    // If we end up hitting this limit, consider extending the API to support
    // reading from offsets and make multiple calls.
    static_assert(std::extent_v<decltype(buf->data)> >= 2048);
    int bytes_read =
        ReadProcFileToBuffer(proc_path, 0, std::size(buf->data), buf->data);
    if (bytes_read != -1) {
      // The kernel stack contains addresses, but they cannot be symbolized.
      // We use ThreadStackWriterOptions{.symbolize = false} to tell
      // pretty-printers to not do any kind of modification of the contents
      // of buf->data.
      writer->Write(buf->data, bytes_read,
                    ThreadStackWriterOptions{.symbolize = false});
    } else {
      int out = absl::SNPrintF(buf->data, std::size(buf->data),
                               "ERROR: Failed to read kernel stack from %s.",
                               proc_path);
      if (out != -1) {
        writer->Write(buf->data, out);
      }
    }
#endif /* __linux__ */
  } else if (arg->raw) {
    PrintWrappedStack(&printer, "     ", stack, depth);
    if (thread->creator_stack_depth_ > 0) {
      printer.Printf("      ");
      PrintCreatorStack(&printer, thread);
    }
    writer->Write(buf->data, printer.length());
  } else {
    // Flush the printer so its contents are before DumpPCAndStackTrace output.
    writer->Write(buf->data, printer.length());
    DumpPCAndStackTrace(nullptr, stack, depth, ThreadDebugWriter, writer);
    if (thread->creator_stack_depth_ > 0) {
      base::RawPrinter printer2(buf->data, std::size(buf->data));
      PrintCreatorStack(&printer2, thread);
      writer->Write(buf->data, printer2.length());
    }
  }

  // We could dump registers here, but we do not, in the interest of brevity.
  // FailureSignalHandler() will dump registers for the primary thread, however.
}

// a pointer to a StackTrace structure.
ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL void GatherStackTrace(
    void* arg, ucontext_t* uc, const LiveThread* thread) {
  // Skip the following caller frames:
  //
  // 1. this frame (unless FillStackTrace is a tail call)
  // 2. RunInThread::SignalHandler
  // 3. signal trampoline
  int skip = 2;
#if !defined(NDEBUG) || ABSL_HAVE_ATTRIBUTE_NO_TAIL_CALL
  // Account for the call to FillStackTrace as well. Typically we will take
  // this path since ABSL_HAVE_ATTRIBUTE_NO_TAIL_CALL is widely supported. If
  // ABSL_HAVE_ATTRIBUTE_NO_TAIL_CALL, we assume only debug mode generates a
  // non-tail call.
  skip++;
#endif

  FillStackTrace(static_cast<StackTrace*>(arg), uc, skip);
  FillCurrentRequestInfo(static_cast<StackTrace*>(arg));
}

}  // namespace

void StackTrace_Print(void* writer, const LiveThread* thread,
                      const StackTrace* trace) {
  // Heap-allocate the buffer to save stack space. (Heap allocation is ok since
  // this function is not async-signal-safe.)
  auto buf = std::make_unique<PrintArgBuf>();
  // Do not skip threads waiting for work when explicitly asked to print a given
  // thread.
  const bool skip_waiting_threads = false;
  PrintArg arg(static_cast<ThreadStackWriter*>(writer),
               /*raw=*/true, buf.get(), skip_waiting_threads);
  PrintStackTrace(&arg, thread, trace);
}

// Implements the LiveThreadList iterate callback for ProcessStackTraces.
//
// This explicit function object is preferred vs a lambda since this is
// sometimes used in a signal-safe context, and semantics of lambdas aren't
// clearly defined to be signal-safe according to the C++11 standard.
struct StackTraceProcessor {
  StackTraceProcessor(const Thread_ProcessStackTracesArg* arg, ucontext_t* uc,
                      int* missed_traces)
      : arg(arg),
        uc(uc),
        run_in_thread(RunInThread::Instance()),
        me(Thread_GetMyLiveThread()),
        missed_traces(missed_traces) {}

  // Called by LiveThreadList::{Iterate,IterateSignalSafe} for each thread.
  ABSL_ATTRIBUTE_NOINLINE void operator()(LiveThread* t, bool* stop) const;

  // Arguments passed to ProcessStackTraces.
  const Thread_ProcessStackTracesArg* arg;
  ucontext_t* uc;

  // Additional context for the iteration.
  RunInThread* run_in_thread;
  const LiveThread* me;

  // Incremented when this processor cannot gather a trace.
  int* missed_traces;
};

ABSL_ATTRIBUTE_NOINLINE
void StackTraceProcessor::operator()(LiveThread* t, bool* stop) const {
  if ((arg->filter != nullptr && !(*arg->filter)(arg->filter_arg, t)) ||
      (arg->process_trace == nullptr && arg->process_thread == nullptr)) {
    return;
  }

  StackTrace trace;
  bool gathered;
  if (t == me) {
    // We gather from the calling thread directly (rather than using
    // RunInThread) to keep the results consistent with the previous
    // implementation, and to ensure that the results are consistent
    // between calls regardless of the setting of arg->sigsafe.
    //
    // Skip this frame and those back to the caller of ProcessStackTraces:
    // 1. this frame
    // 2. ProcessStackTraces
    // 3. LiveThreadList::{Iterate,IterateSignalSafe}
    // 4. caller of #3, e.g., Thread_ExtractStacks
    //
    // (Note that this function, and the others in this list, must all be marked
    // noinline for this technique to work reliably at different optimization
    // levels. Each frame may also need a blocker or non-trivial local variable
    // to avoid tail call optimization; non-trivial control flow -- such as a
    // loop -- is sufficient without a blocker.)
    FillStackTrace(&trace, uc, 4);
    FillCurrentRequestInfo(&trace);
    gathered = true;
  } else if (!arg->sigsafe) {
    gathered = run_in_thread->Run(t, &GatherStackTrace, &trace,
                                  arg->per_thread_timeout_ms);
  } else {
    gathered = run_in_thread->TryRun(t, &GatherStackTrace, &trace,
                                     arg->per_thread_timeout_ms);
  }

  if (!gathered) {
    (*missed_traces)++;
  }
  if (arg->process_trace != nullptr) {
    (*arg->process_trace)(arg->process_trace_arg, t,
                          gathered ? &trace : nullptr);
  }
  if (arg->process_thread != nullptr) {
    LiveThreadState state;
    state.thread = t;
    state.fiber_name = gathered ? trace.fiber_name : nullptr;
    state.thread_status = gathered ? trace.thread_status : nullptr;
    state.trace = gathered ? &trace : nullptr;
    state.creator = nullptr;
    StackTrace creator;
    if (t->creator_stack_depth_ > 0) {
      memcpy(&creator.stack, t->creator_stack_,
             sizeof(void*) * t->creator_stack_depth_);
      creator.depth = t->creator_stack_depth_;
      CopyUcontext(&creator.uc, &zero_uc);
      creator.thread_status[0] = '\0';
      state.creator = &creator;
    }
    (*arg->process_thread)(arg->process_thread_arg, state);
  }
}

// Internal routine that extracts a stack trace from each thread and
// processes it.  Threads that are filtered out by 'arg.filter' will
// not have any data gathered.  The argument "uc" is the register
// context of the calling thread.
//
// If 'arg.sigsafe' is true, and the processor's methods are
// async-signal-safe, and RunInThread::Instance() has been called
// previously, this function is itself async-signal-safe.
//
// Returns the number of threads whose stack trace could not be gathered,
// or -1 if 'arg.sigsafe' is true and no traces could be gathered.
//
// (Note that this function must be marked noinline for stack traces to work
// reliably at different optimization levels.)
ABSL_ATTRIBUTE_NOINLINE
static int ProcessStackTraces(const Thread_ProcessStackTracesArg& arg,
                              ucontext_t* uc) {
  int missed_traces = 0;
  StackTraceProcessor action(&arg, uc, &missed_traces);
  if (arg.sigsafe) {
    if (!live_threads->IterateSignalSafe(action)) {
      return -1;
    }
  } else {
    live_threads->Iterate(action);
  }
  return missed_traces;
}

int Thread_ProcessStackTraces(const Thread_ProcessStackTracesArg& arg) {
  // Avoid optimizations which could break frame counts.
  //
  // The collected stack trace skips a fixed number of stack frames at the top
  // to omit functions in this file. Even without inlining, there are
  // optimizations that could cause stack frames to be elided (e.g., tail call
  // optimization).
  int v = ProcessStackTraces(arg, nullptr /* no ucontext_t */);
  ABSL_BLOCK_TAIL_CALL_OPTIMIZATION();
  return v;
}

// Calls ProcessInactiveCoThreadTracesImpl if it is linked in.
static bool ProcessInactiveCoThreadTraces(ThreadStackWriter* writer,
                                          bool signal_safe, bool symbolize) {
#if !ABSL_HAVE_ATTRIBUTE_WEAK || defined(__APPLE__) || defined(__EMSCRIPTEN__)
  // Some toolchains (like --config=ios_x86_64) don't support weak symbols.
  static const bool kHaveWeak = false;
#else
  static const bool kHaveWeak = true;
#endif
  if constexpr (kHaveWeak) {
    if (&thread::ProcessInactiveCoThreadTracesImpl == nullptr) {
      return true;
    }
    return thread::ProcessInactiveCoThreadTracesImpl(writer, signal_safe,
                                                     symbolize);
  } else {
    return false;
  }
}

// Common logic for printing a summary after all the thread stacktraces.
void PrintStackExtractionSummary(const PrintArg& print_arg, int dropped,
                                 ThreadStackWriter* writer,
                                 bool skip_address_map) {
  OUTPUT(writer, "---- Processed %d threads ----\n", print_arg.total_threads);
  if (print_arg.skipped_threads > 0) {
    OUTPUT(writer,
           "---- Skipped printing %d uninteresting threads waiting for work "
           "----\n",
           print_arg.skipped_threads);
  }
  if (dropped == -1) {
    return;
  }
  if (dropped > 0) {
    OUTPUT(writer, "---- No stack trace for %d threads ----\n", dropped);
  }

  // Future work: make every version respect --skip_address_map.
  if (!skip_address_map) {
    DumpAddressMap(ThreadDebugWriter, writer);
  }
}

void Thread_ExtractStacks(ThreadStackWriter* writer) {
  // Heap-allocate the buffer to save stack space. (Heap allocation is ok since
  // this function is not async-signal-safe.)
  auto buf = std::make_unique<PrintArgBuf>();
  PrintArg print_arg(writer,
                     /*raw=*/true, buf.get(),
                     absl::GetFlag(FLAGS_stacktrace_skip_waiting_threads));
  Thread_ProcessStackTracesArg arg;
  arg.process_trace = &PrintStackTrace;
  arg.process_trace_arg = &print_arg;
  // Not just a call to Thread_ProcessStackTraces because we need to
  // keep a constant number of stack frames between user code and the
  // FillStackTrace call in ProcessStackTraces.
  int dropped = ProcessStackTraces(arg, nullptr /* no ucontext_t */);
  DCHECK_NE(dropped, -1);  // Since we're not async-signal-safe.
  ProcessInactiveCoThreadTraces(writer, /*signal_safe=*/false,
                                /*symbolize=*/!print_arg.raw);
  PrintStackExtractionSummary(print_arg, dropped, writer,
                              /*skip_address_map=*/false);
}

// External routine -- not safe for access from signal handler
void Thread_DumpStacks() {
  StderrThreadStackWriter writer;
  // Heap-allocate the buffer to save stack space. (Heap allocation is ok since
  // this function is not async-signal-safe.)
  auto buf = std::make_unique<PrintArgBuf>();
  PrintArg print_arg(&writer,
                     /*raw=*/true, buf.get(),
                     absl::GetFlag(FLAGS_stacktrace_skip_waiting_threads));
  Thread_ProcessStackTracesArg arg;
  arg.process_trace = &PrintStackTrace;
  arg.process_trace_arg = &print_arg;
  // Not just a call to Thread_ProcessStackTraces because we need to
  // keep a constant number of stack frames between user code and the
  // FillStackTrace call in ProcessStackTraces.
  int dropped = ProcessStackTraces(arg, nullptr /* no ucontext_t */);
  DCHECK_NE(dropped, -1);  // Since we're not async-signal-safe.
  ProcessInactiveCoThreadTraces(&writer, /*signal_safe=*/false,
                                /*symbolize=*/!print_arg.raw);
  PrintStackExtractionSummary(print_arg, dropped, &writer,
                              /*skip_address_map=*/false);
}

// TODO: This function goes to some pains to be
// async-signal-safe, but the code to print stack traces and to print
// symbolized stack traces is not async-signal-safe!
void Thread_SignalSafe_DumpStacks() {
  StderrThreadStackWriter writer;
  Thread_SignalSafe_DumpStacksTo(&writer);
}

void Thread_SignalSafe_DumpStacksTo(ThreadStackWriter* writer) {
  // Avoid heap allocation of PrintArgBuf for async-signal-safety (and don't put
  // on stack since this function may be called in stack-limited situations).
  if (!shared_print_mu.try_lock()) {
    return;
  }
  PrintArg print_arg(writer,
                     /*raw=*/true, &shared_print_buf,
                     absl::GetFlag(FLAGS_stacktrace_skip_waiting_threads));
  Thread_ProcessStackTracesArg arg;
  arg.process_trace = &PrintStackTrace;
  arg.process_trace_arg = &print_arg;
  arg.sigsafe = true;
  // Not just a call to Thread_ProcessStackTraces because we need to
  // keep a constant number of stack frames between user code and the
  // FillStackTrace call in ProcessStackTraces.
  int dropped = ProcessStackTraces(arg, nullptr /* no ucontext_t */);
  shared_print_mu.unlock();
  ProcessInactiveCoThreadTraces(writer, /*signal_safe=*/true,
                                /*symbolize=*/!print_arg.raw);
  PrintStackExtractionSummary(print_arg, dropped, writer,
                              /*skip_address_map=*/false);
}

// vuc is a ucontext_t *.  We use void* to avoid the use
// of ucontext_t on non-POSIX systems in base/google.cc.
// (This module is not used on non-POSIX systems.)
//
// TODO: This function goes to some pains to be
// async-signal-safe, but the code to print stack traces, or print
// symbolized stack traces, is not itself async-signal-safe!
static void ThreadStackDumper(void* vuc) {
  ucontext_t* uc = static_cast<ucontext_t*>(vuc);

  // Avoid heap allocation of PrintArgBuf for async-signal-safety (and don't put
  // on stack since this function may be called in stack-limited situations).
  if (!shared_print_mu.try_lock()) {
    return;
  }
  // When this is called, the current thread's stack is already printed.
  // So now the task is to try to LOG() every thread's stack.
  // Logging will have been reconfigured to include stderr as well.
  LogWarningThreadStackWriter writer;
  PrintArg print_arg(&writer,
                     /*raw=*/!absl::GetFlag(FLAGS_symbolize_stacktrace),
                     &shared_print_buf,
                     absl::GetFlag(FLAGS_stacktrace_skip_waiting_threads));
  Thread_ProcessStackTracesArg arg;
  arg.process_trace = &PrintStackTrace;
  arg.process_trace_arg = &print_arg;
  arg.sigsafe = true;
  int dropped = ProcessStackTraces(arg, uc);
  shared_print_mu.unlock();
  ProcessInactiveCoThreadTraces(&writer, /*signal_safe=*/true,
                                /*symbolize=*/!print_arg.raw);
  PrintStackExtractionSummary(print_arg, dropped, &writer,
                              absl::GetFlag(FLAGS_skip_address_map));
}

void Thread_RegisterExternalThread(absl::string_view name_prefix) {
  if (Thread_GetMyLiveThread() != nullptr) {
    return;
  }

  // No known creator from which to copy stack trace.
  LiveThread* thread = new LiveThread(name_prefix);
  thread->MakeLive();
}

namespace thread {

void InternalSetCurrentFiberName(absl::string_view fiber_name) {
  absl::string_view* ptr = tls_fiber_name.pointer();
  *ptr = fiber_name;
}

absl::string_view InternalGetCurrentFiberName() {
  absl::string_view* ptr = tls_fiber_name.safe_pointer();
  if (ptr) {
    return *ptr;
  }
  return "";
}

std::string DebugName() {
  const LiveThread* thread = Thread_GetMyLiveThread();
  std::string full_name = "undefined";

  if (thread != nullptr) {
    full_name = LiveThread_Name(thread);

    absl::string_view fiber_name = thread::InternalGetCurrentFiberName();
    if (!fiber_name.empty()) {
      full_name = absl::StrCat(fiber_name, " (", full_name, ")");
    }
  }

  return full_name;
}

Note::Note(std::string note) : note_(std::move(note)) {
  LiveThread* live_thread = my_live_thread_holder.get().get();
  SpinLockHolder l(live_thread->notes_lock_);
  live_thread->notes_.push_back(this);
  live_thread->notes_version_++;
}

Note::Note(absl::string_view note) : Note(std::string(note)) {}

Note::Note(const char* note) : Note(std::string(note)) {}

Note::~Note() {
  LiveThread* live_thread = my_live_thread_holder.get().get();
  SpinLockHolder l(live_thread->notes_lock_);
  DCHECK_EQ(this, live_thread->notes_.back());
  live_thread->notes_.pop_back();
  live_thread->notes_version_++;
}

}  // namespace thread

std::vector<std::string> LiveThread_GetNotes(const LiveThread* thread) {
  std::vector<std::string> result;
  SpinLockHolder l(thread->notes_lock_);
  result.reserve(thread->notes_.size());
  for (const thread::Note* note : thread->notes_) {
    result.push_back(std::string(note->note()));
  }
  return result;
}

ThreadNotesForTrace LiveThread_GetNotesForTrace(const LiveThread* thread,
                                                const StackTrace* trace) {
  ThreadNotesForTrace result;
  SpinLockHolder l(thread->notes_lock_);
  result.notes_changed_since_stack_trace =
      trace->notes_version != thread->notes_version_;
  result.notes.reserve(thread->notes_.size());
  for (const thread::Note* note : thread->notes_) {
    result.notes.push_back(std::string(note->note()));
  }
  return result;
}

bool LiveThread_ForEachNoteAsyncSignalSafe(
    const LiveThread* thread, const StackTrace* must_match,
    void (*fn)(void* arg, absl::string_view note), void* arg) {
  if (!thread->notes_lock_.try_lock()) return false;
  if (must_match != nullptr &&
      must_match->notes_version != thread->notes_version_) {
    thread->notes_lock_.unlock();
    return false;
  }
  for (const thread::Note* note : thread->notes_) {
    (*fn)(arg, note->note());
  }
  thread->notes_lock_.unlock();
  return true;
}

static void Thread_EnsureWatchersRunIfNeeded() {
  // Note: We use raw pthreads for watchers because we are called from within
  // `Thread::Start()`.

  if (absl::GetFlag(FLAGS_watch_pthread_manager)) {
    // Start the thread that checks for deadlocks in exit().
    [[maybe_unused]] static const bool once = [] {
      StartHelperThread(&ExitTimeoutWatcher);
      // Add an atexit() handler to set "process_is_dying".
      // The ExitTimeoutWatcher thread will kill all threads
      // some seconds after this variable becomes true.
      atexit(+[] { base::SignalThatProcessIsDying(); });
      return false;
    }();
  }

  if (absl::GetFlag(FLAGS_watch_thread_liveness)) {
    // Start the thread that checks for watchdog liveness.
    [[maybe_unused]] static const bool once = [] {
      StartHelperThread(&ThreadLivenessWatcher);
      return false;
    }();
  }
}

REGISTER_MODULE_INITIALIZER(malloc_memory_release_thread, {
  if (!::thread::DeprecatedThreadControl::BackgroundThreadsAllowed() ||
      !tcmalloc::MallocExtension::NeedsProcessBackgroundActions()) {
    return;
  }

  Thread* memory_releaser =
      new ClosureThread(thread::Options(), "MemoryReleaser",
                        tcmalloc::MallocExtension::ProcessBackgroundActions);
  memory_releaser->Start();
});
