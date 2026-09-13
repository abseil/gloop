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
// This defines the Thread class, and also defines useful subclasses
// (e.g. for running periodic tasks).  Thread should be used instead of
// std::thread, which is banned: <link>
// Thread works on Android and should also be used there.
//
// See also: <link>++-concurrency#threads_fibers
//
// The simplest example of executing a function in a separate thread is:
//
//   static void ThreadBody() {
//     // do something here...
//   }
//   ...
//   thread::Options options;
//   options.set_joinable(true);
//   ClosureThread t(options, "MyThread", &ThreadBody);
//   t.Start();
//   ...
//   t.Join();

#ifndef THIRD_PARTY_GLOOP_THREAD_THREAD_H_
#define THIRD_PARTY_GLOOP_THREAD_THREAD_H_

#include <assert.h>
#include <pthread.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/types/source_location.h"
#include "gloop/base/thread-identity.h"
#include "gloop/util/functional/from_callback.h"

#if !defined(__APPLE__)
#include <ucontext.h>  // for ucontext_t
#endif

#include <functional>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/base/callback.h"
#include "gloop/thread/os_semaphore.h"
#include "gloop/thread/thread_options.h"

namespace thread {
class CpuSubContainer;
}  // end namespace thread

/** The Thread class can be subclassed to create an object whose
    "Run" method is invoked in a separate thread.

    <p>
    Example:
        <pre>
        class MyThread : public Thread {
         protected:
          void Run() override {
            // Do something here...
          }
        };
        ...
        thread::Options options;
        options.set_joinable(true);
        MyThread t(options, "MyThread");
        t.Start();
        ...
        t.Join();
        </pre>
*/
class Thread {
 public:
  /** Create thread object.  The actual runtime thread is not created
      until "Start" is called.  Note that calls to initialization functions,
      such as SetStackSize, and to Start are not thread-safe. */
  Thread();
  /** Release all thread resources. */
  virtual ~Thread();

  /** Create thread object, initializing its options with the values
      provided.  "name_prefix" specifies the thread's name
      prefix. Thread names consist of two parts: a prefix supplied by
      the creator of the thread, and the suffix "/PID" which is
      supplied by the Thread class to generate a unique thread name.
      Thread name prefixes may contain only the characters a-z, A-Z,
      0-9, -, and _, and may not start with a digit.  For more
      information about thread naming guidelines and the use of thread
      names, see: <link>  The underlying
      platform will receive the "name_prefix" as the thread name, and it
      may be silently truncated (typically to 15 [as in Linux] or more
      characters).

      The actual runtime thread is not created until "Start" is called.
      Start cannot be called from this constructor because the started thread
      would then race with initialization of any subclasses of Thread.

      To more easily pass the options argument to the constructor, you
      can use the fact that the thread::Options setter methods return
      a reference to *this and combine settings:

        MyThread() : Thread(thread::Options().set_stack_size(1024 * 1024).
                                              set_joinable(true),
                            "MyWorker") {
          ...
        }

      This avoids creating separate static functions to construct the
      thread::Options object to pass to the constructor.
  */
  Thread(const thread::Options& options, absl::string_view name_prefix);

#ifndef SWIG
  // This type is neither copyable nor movable.
  Thread(const Thread&) = delete;
  Thread& operator=(const Thread&) = delete;
#endif

  /** Return the thread option settings */
  const thread::Options& options() const { return options_; }

  /** Return the thread's name prefix */
  const std::string& name_prefix() const { return name_prefix_; }

  /** Return the thread's pthread id.
      Note: This is NOT the kernel tid (as returned by GetTID()). */
  pthread_t tid() const { return tid_; }

  /** Create a runtime thread and invoke "this->Run()" as its body. */
  void Start(absl::SourceLocation loc = absl::SourceLocation::current());

  /** Join with the running thread. Blocks until the thread terminates,
      unless it has already terminated.  The results of multiple simultaneous
      calls to Join are undefined.

      The thread must have been started, and must have been marked as joinable
      before being started. */
  void Join();

  // DEPRECATED options methods. Pass thread::Options to constructor instead.

  /** DEPRECATED: Use the constructor that takes the thread::Options object.
      Mark the thread as joinable.  This operation must be called
      before "Start".  By default threads are not joinable. */
  void SetJoinable(bool joinable);

  /** DEPRECATED: Use the constructor that takes the thread::Options object.
      Set the thread stack size (in bytes).  This operation must be called
      before "Start".   Passing bytes==0 resets the stack size to the
      default value for the system.  */
  void SetStackSize(size_t bytes);

  /** DEPRECATED: Use the constructor that takes the thread::Options object.
      Set high-priority fifo scheduling. This operation must be called
      before "Start" .*/
  void SetFIFOScheduling();

  /** DEPRECATED: Use the constructor that takes the thread::Options object.
      Set the thread's name prefix.  This operation must be called
      before "Start".

      Thread names consist of two parts: a prefix supplied by the
      creator of the thread, and the suffix "/PID" which is supplied
      by the Thread class to generate a unique thread name.  Thread
      name prefixes may contain only the characters a-z, A-Z, 0-9, and
      _, and may not start with a digit.

      For more information about thread naming guidelines and the use
      of thread names, see: <link>
      */
  void SetNamePrefix(absl::string_view name_prefix);

  /** DEPRECATED: Use the constructor that takes the thread::Options object.
      Advanced usage: Set the thread nice level.  Use of this flag is not
      recommended in most situations.  Processes and threads using nice
      are susceptible to priority inversion bugs.

      The exact effect varies with different versions of the Linux kernel
      and glibc.  Refer to system documentation for more details.

      This operation must be called before "Start".
    */
  void set_nice_priority_level(int level);

  // CAUTION: For internal use only. Set the CPU subcontainer to be used for
  // scheduling.  "subcontainer" must outlive the thread object.
  void SetInitialCpuSubContainer(thread::CpuSubContainer* subcontainer);

  // Register a function to be called when a thread exits abnormally
  // (e.g. via pthread_exit).
  static void RegisterExitHandler(std::function<void()> handler);

 protected:
  virtual void Run() = 0;

 private:
  thread::Options options_;                    // Thread configuration settings
  pthread_t tid_;                              // Thread id
  thread::internal::OsSemaphore tid_set_sem_;  // Parent thread setup tid_?
  // Stack trace of creator
  void* creator_stack_[32];
  thread::CpuSubContainer* subcontainer_;
  std::string name_prefix_;  // Thread name prefix
  bool created_;             // Thread created yet?
  bool needs_join_;          // Thread joinable but not joined yet?

  int creator_stack_depth_;  // Creator stack depth

  static void* ThreadBody(void* arg);

  // to be invoked whenever a thread exits or got cancelled
  // Currently, it only dumps stack trace.
  static void ThreadExitHandler(void* unused);

  // CreatePthread wraps the setup and retry logic surrounding creating a
  // pthread. If we cannot create a pthread, then we will crash via
  // ABSL_RAW_LOG(FATAL).
  void CreatePthread(pthread_attr_t& attr);
};

namespace thread {
// Return true if name_prefix is a valid thread name prefix.
bool IsValidThreadNamePrefix(const absl::string_view name_prefix);

// Sanitize a given string to make it a valid thread name prefix.
std::string SanitizeThreadNamePrefix(std::string name_prefix);

// Return a string that identifies the currently executing thread and the fiber
// (if any). Useful for debugging. Callers should not assume anything about the
// format of the returned string.
std::string DebugName();
}  // end namespace thread

/** The PeriodicThread class is usually part of another class that
    wants to do periodic tasks in another thread.  To use it,
    you define a method called RunInThread().

    Example:
    <p>
        <pre>
        class MyClass {
         public:
          MyClass() : write_thread_(this), read_thread_(this) { }
          void RunInThread(PeriodicThread<MyClass>* thr) {
             if ( thr == &write_thread_ ) { }
             if ( thr == &read_thread_ ) { }
          }
         protected:
          PeriodicThread<MyClass> write_thread_, read_thread_;
        };
        ...
        MyClass c;

        c.write_thread_.Signal(true);
        ...
        c.write_thread_.Signal(true);
        ...
        c.write_thread_.Exit();
        </pre>

    You can also use a Google permanent callback of type Callback1<Thread*>
    if you don't want to bother with defining your own class.

    Example:
    <p>
        <pre>
        void MyFunction(Thread* thr) { }
        ...
        Callback1<Thread*>* cb = NewPermanentCallback(&MyFunction);

        PeriodicThread<Callback1<Thread*> > thread(cb);
        ...
        thread.Signal(true);
        ...
        thread.Exit();
        </pre>

    Finally, you can use a Google permanent closure which accepts no parameters.

    Example:
    <p>
        <pre>
        void MyFunction() { }
        ...
        Closure* c = NewPermanentCallback(&MyFunction);

        PeriodicThread<Closure> thread(c);
        ...
        thread.Signal(true);
        ...
        thread.Exit();
        </pre>
*/
// T can have any value, it only exists for legacy reasons.
template <class T = void>
class PeriodicThread : public Thread {
 public:
  explicit PeriodicThread(
      ::util::functional::CallbackFunctor<PeriodicThread*> callback,
      const thread::Options& options = thread::Options(),
      absl::string_view name_prefix = "")
      : Thread(options, name_prefix),
        callback_(std::move(callback)),
        signalled_(false),
        should_exit_(false) {
    SetJoinable(true);  // need to be joinable for Exit to work
    Start();            // immediately spawn the new thread
  }
  explicit PeriodicThread(Closure* closure,
                          const thread::Options& options = thread::Options(),
                          absl::string_view name_prefix = "")
      : PeriodicThread([closure](PeriodicThread*) { closure->Run(); }, options,
                       name_prefix) {}
  template <typename U>
  explicit PeriodicThread(U* object,
                          const thread::Options& options = thread::Options(),
                          absl::string_view name_prefix = "")
      : PeriodicThread(
            [object](PeriodicThread* thr) { object->RunInThread(thr); },
            options, name_prefix) {}

  ~PeriodicThread() override {}  // We could call Exit(), but we don't

  /** Wake up the thread and have it run RunInThread again.
      Return true if the thread was idle, false if it was already working.
      If wait is true, we'll wait for it to be idle, and always return true */
  bool Signal(bool wait) {
    CHECK(!should_exit_) << ": Can't call Signal() after calling Exit()";
    return SignalRunOrExit(wait, false);  // false means "run, don't exit"
  }

  /** Wake up the thread and have it exit.  We'll wait for it to finish
      if it's currently busy */
  void Exit() {
    CHECK(!should_exit_) << ": Can't call Exit() twice on the same thread";
    SignalRunOrExit(true, true);  // true: wait, true: time to exit
    Join();                       // make sure the thread dies
  }

 protected:
  // Override of Thread::Run().  This function's access has been changed from
  // public to protected because it is an implementation detail.  If this change
  // breaks your code, you can work around it temporarily by defining a local
  // class which derives from PeriodicThread and has a public Run() function.
  void Run() override {
    absl::MutexLock l(mutex_);
    while (1) {
      while (!signalled_) busy_.Wait(&mutex_);
      assert(signalled_);
      signalled_ = false;
      if (should_exit_) break;

      callback_(this);
      idle_.Signal();
    }
    idle_.Signal();
    // once we leave this function, the thread will exit
  }

 private:
  // This does the actual work of signalling, both normally and for exit
  bool SignalRunOrExit(bool wait, bool should_exit) {
    if (wait) {
      mutex_.lock();
    } else {
      if (!mutex_.try_lock()) return false;
      if (signalled_) {
        mutex_.unlock();
        return false;
      }
    }
    while (signalled_) idle_.Wait(&mutex_);
    assert(!signalled_);
    signalled_ = true;
    should_exit_ = should_exit;
    busy_.Signal();
    mutex_.unlock();
    return true;
  }

  ::util::functional::CallbackFunctor<PeriodicThread*> callback_;
  absl::Mutex mutex_;
  absl::CondVar idle_;
  absl::CondVar busy_;
  bool signalled_;
  bool should_exit_;
};

#ifndef SWIG
/***** Class for creating a thread which invokes a closure *****/

// Note: If you're searching for a variant with std::function, consider using
// thread::Fiber.

class ClosureThread : public Thread {
 public:
  // Start() on this thread will invoke the given functor.
  explicit ClosureThread(absl::AnyInvocable<void() &&> functor)
      : closure_(std::move(functor)) {}

  // Any standard thread options, such as stack size, should
  // be passed via "thread_options". "name_prefix" specifies the
  // thread name prefix (see the description in class Thread).
  ClosureThread(const thread::Options& thread_options,
                const absl::string_view name_prefix,
                absl::AnyInvocable<void() &&> functor)
      : Thread(thread_options, name_prefix), closure_(std::move(functor)) {}

  // Even if you don't call set_joinable/Join, you must ensure that the thread
  // has finished running (or was not started at all) before destroying it.
  ~ClosureThread() override {}

 protected:
  void Run() override { std::move(closure_)(); }

 private:
  absl::AnyInvocable<void() &&> closure_;
};
#endif

/***** Class for creating a thread which invokes a member func *****/

template <class CL>
class MemberThread : public Thread {
 public:
  typedef void (CL::*PtrToMember)();
  MemberThread(CL* ptr, PtrToMember m) : this_pointer_(ptr), member_(m) {}
  // Any standard thread options, such as stack size, should
  // be passed via "thread_options". "name_prefix" specifies the
  // thread name prefix (see the description in class Thread).
  MemberThread(const thread::Options& thread_options,
               absl::string_view name_prefix, CL* ptr, PtrToMember m)
      : Thread(thread_options, name_prefix), this_pointer_(ptr), member_(m) {}

 protected:
  // Override of Thread::Run().  This function's access has been changed from
  // public to protected because it is an implementation detail.  If this change
  // breaks your code, you can work around it temporarily by defining a local
  // class which derives from MemberThread and has a public Run() function.
  void Run() override { (this_pointer_->*member_)(); }

 private:
  CL* this_pointer_;
  PtrToMember member_;
};

/***** Methods for creating a thread which invokes a closure *****/
/***** and deletes itself once it is done.                   *****/

// Starts a new thread that runs the specified closure as its body.
// If the specified closure never returns, the thread will run forever.
// The closure is executed in the Context active when StartDetachedThread is
// called.  Thread resources are released once the closure returns.
// Unlike ClosureThread, this thread deletes itself. Any standard thread
// options, such as stack size, should be passed via "options". "name_prefix"
// specifies the thread name prefix (see the description in class Thread).
#ifndef SWIG
void StartDetachedThread(absl::string_view name_prefix,
                         absl::AnyInvocable<void() &&> closure);
void StartDetachedThread(const thread::Options& options,
                         absl::string_view name_prefix,
                         absl::AnyInvocable<void() &&> closure);
#endif

// ---------------------------------------------------------------------
// Support for iterating over all running threads.
// ---------------------------------------------------------------------

// Opaque structure which holds attributes related to a running
// thread.
class LiveThread;

// Get the operating system's thread ID of a particular running
// thread.  For a given thread, this is the same value that would be
// returned by a call to GetTID in that thread.  This function is
// async-signal-safe.
pid_t LiveThread_OS_TID(const LiveThread* thread);

// Get the pthread_t of a particular running thread.  For a given
// thread, this is the same value that would be returned by a call to
// pthread_self in that thread.  This function is async-signal-safe.
pthread_t LiveThread_Pthread_TID(const LiveThread* thread);

// Get the name prefix of a particular running thread.  This function
// is async-signal-safe.  Note that the returned pointer is valid only
// while 'thread' is still alive.
const char* LiveThread_NamePrefix(const LiveThread* thread);

// Get the name of a particular running thread.  This function is
// async-signal-safe.  Note that the returned pointer is valid only
// while 'thread' is still alive.
const char* LiveThread_Name(const LiveThread* thread);

// Get the ThreadIdentity of a particular thread.
const absl::base_internal::ThreadIdentity* LiveThread_Identity(
    const LiveThread* thread);

// Get the stacktrace of a running thread's creator, at the point that
// this thread was created.  Copies up to max_pcs PC values into the
// pc_buffer.  The number of PC values actually copied is returned.
int LiveThread_CreatorStack(const LiveThread* thread, int max_pcs,
                            const void** pc_buffer);

// Run a function for each thread, and optionally run a function in
// the context of each thread.
//
// For each thread 't' known to the Thread system, if for_each is
// non-NULL, call
//
//   (*for_each)(for_each_arg, t)
//
// from the context of the thread which invoked Thread_ForEach.  If
// in_each is non-NULL and either for_each is NULL or returns true,
// call
//
//   (*in_each)(in_each_arg, t)
//
// from the context of thread 't'.  in_each must be async-signal-safe.
// All of these functions are invoked sequentially, i.e., their
// executions will not overlap.
//
// In certain circumstances, such as --install_signal_handlers being
// set to false, it is not possible to run in_each in the target
// thread.  In other circumstances, such as very high system load,
// in_each may fail to start on the target thread quickly.  If it's
// possible to run in_each on the target at all, this function will
// wait for up to in_each_timeout_ms milliseconds for in_each to start
// in the target thread. If in_each cannot be started or fails
// to start for a thread within the timeout, it will not be run for
// that thread at all.  The return value of this function is the
// number of threads in which in_each was supposed to run but did not.
//
// Note that for_each and in_each may be called with the thread system
// locked, and so should not create or destroy any Thread objects,
// call Thread object methods, call any Thread_* functions, or attempt
// to cause threads to exit.
//
// NOT SAFE FOR USE FROM SIGNAL HANDLERS
extern int Thread_ForEach(bool (*for_each)(void* arg, const LiveThread* thread),
                          void* for_each_arg,
                          void (*in_each)(void* arg, ucontext_t* uc,
                                          const LiveThread* thread),
                          void* in_each_arg, int in_each_timeout_ms);

// Get the current thread's LiveThread pointer.  If the thread is not
// known to the thread system (e.g., is an external thread which
// hasn't been registered using Thread_RegisterExternalThread), then
// this function will return nullptr.
//
// This function is async-signal-safe.
extern const LiveThread* Thread_GetMyLiveThread();

// ---------------------------------------------------------------------
// Support for dumping stack traces of all threads.
// ---------------------------------------------------------------------

// Opaque structure which holds a stack trace.
struct StackTrace;

// Copy up to max_pcs PC values from the given trace into the memory
// pointed to by pc_buffer.  pc_buffer[0] will contain the PC of the
// most recently called function.  The number of PC values actually
// copied is returned.
int StackTrace_GetPCs(const StackTrace* trace, int max_pcs,
                      const void** pc_buffer);

// Print a stack trace for a live thread to a ThreadStackWriter passed
// in through thread_stack_writer_arg.  Applications can use this to get
// a formatted stack trace for a particular thread or set of threads, by
// passing &StackTrace_Print to Thread_ProcessStackTraces() and writing
// a filter.
//
// NOT SAFE FOR USE FROM SIGNAL HANDLERS
extern void StackTrace_Print(void* thread_stack_writer_arg,
                             const LiveThread* thread, const StackTrace* trace);

// Get the thread's register values at the moment the stacktrace was taken.
// The returned value is valid only as long as trace is live.
extern const ucontext_t* StackTrace_GetUContext(const StackTrace* trace);

// Get the trace ID of the TraceContext that was installed in the thread at the
// moment the stacktrace was recorded.
uint64_t StackTrace_GetTraceId(const StackTrace* trace);

// Get the root ID of the CensusHandle that was installed in the thread at the
// moment the stacktrace was recorded.
uint64_t StackTrace_GetCensusRootId(const StackTrace* trace);

// Get the time at which the request being served by the thread from which this
// stacktrace was started. If this thread was not running any request, then this
// returns the unix epoch.
absl::Time StackTrace_GetRequestStartTime(const StackTrace* trace);

// Get the stack size of the thread (or 0 if it cannot be determined).
size_t StackTrace_GetStackSize(const StackTrace* trace);

// Get the stack usage of the thread at the time of the stacktrace collection
// (or 0 if it cannot be determined).
size_t StackTrace_GetStackUsage(const StackTrace* trace);

// Returns whether the thread is holding the Python GIL. Always returns
// false if the process does not link a Python runtime.
bool StackTrace_IsHoldingPythonGil(const StackTrace* trace);

// Extract all thread stacks, convert them to printable form, and write
// the printable form to STDERR.
//
// This routine may not extract any information if the current
// architecture does not support stack extraction.
//
// This routine may not extract information for all threads.  In
// particular, if --install_signal_handlers (see google.cc) was
// changed from its default value of true to false.
//
// NOT SAFE FOR USE FROM SIGNAL HANDLERS
extern void Thread_DumpStacks();

// Version of Thread_DumpStacks() that is safe for use from signal
// handlers but may not print much information in certain situations.
// All data is written to STDERR.
extern void Thread_SignalSafe_DumpStacks();

class ThreadStackWriter;

// Version of Thread_DumpStacks() that is safe for use from signal
// handlers but may not print much information in certain situations.
// All data is written to the writer provided which must be non-nullptr.
// The writer must be async signal safe.
extern void Thread_SignalSafe_DumpStacksTo(ThreadStackWriter* writer);

// Version of Thread_DumpStacks() that passes the printable output to
// a supplied "writer" object instead of writing directly to stderr.
//
// Note that writer->Write() may be called with the thread system locked,
// and so should not create or destroy any Thread objects, call Thread
// object methods, call any Thread_* functions, or attempt to cause
// threads to exit.
//
// NOT SAFE FOR USE FROM SIGNAL HANDLERS
extern void Thread_ExtractStacks(ThreadStackWriter* writer);

// Gather and process thread stack traces.
//
// For each thread 't' known to the thread system, if filter is
// non-nullptr, call
//
//   (*filter)(filter_arg, t)
//
// If process_trace is non-nullptr and filter is nullptr or returns true,
// gather a stack trace 's' from thread 't' and call
//
//   (*process_trace)(process_trace_arg, t, s)
//
// If process_thread is non-nullptr and filter is nullptr or returns true,
// gather information about the thread in a LiveThreadState s and call
//
//   (*process_thread)(process_thread_arg, s)
//
// All functions are called from the context of the thread which
// invoked Thread_ProcessStackTraces and are invoked sequentially,
// i.e., their executions will not overlap.
//
// This routine may fail to gather stack traces for some threads
// (e.g., if --install_signal_handlers is set to false, most threads
// will not have a stack trace captured), and cannot gather traces on
// certain architectures.  When stack trace gathering fails,
// process_trace and process_thread will be invoked with a nullptr StackTrace
// pointer so that it can note the missed trace. For process_thread
// creator stacks will be filled in if available or a nullptr StackTrace
// pointer passed otherwise.
//
// The return value of this function is the number of missed traces,
// or -1 if no traces were able to be gathered at all. This latter
// condition can only occur in signal safe mode and indicates the
// thread library could not be locked.
//
// Note that filter and process_trace may be called with the thread
// system locked, and so should not create or destroy any Thread
// objects, call Thread object methods, call any Thread_* functions,
// or attempt to cause threads to exit. All data returned should only
// be assumed to be valid during the lifetime of the
// process_trace/process_thread call.
//
// NOT SAFE FOR USE FROM SIGNAL HANDLERS unless sigsafe is set to
// true below. All operations in this mode must be completely
// async safe (no LOG invocations, no memory allocation, etc).
struct LiveThreadState {
  const LiveThread* thread;
  const char* fiber_name;
  const char* thread_status;
  const StackTrace* trace;
  const StackTrace* creator;
};

struct Thread_ProcessStackTracesArg {
  Thread_ProcessStackTracesArg()
      : process_trace(nullptr),
        process_trace_arg(nullptr),
        process_thread(nullptr),
        process_thread_arg(nullptr),
        filter(nullptr),
        filter_arg(nullptr),
        per_thread_timeout_ms(10),
        sigsafe(false) {}

  void (*process_trace)(void* arg, const LiveThread* thread,
                        const StackTrace* trace);
  void* process_trace_arg;
  void (*process_thread)(void* arg, const LiveThreadState& state);
  void* process_thread_arg;

  bool (*filter)(void* arg, const LiveThread* thread);
  void* filter_arg;

  // Number of milliseconds to wait for each thread to run process_trace.
  // Default is 10 milliseconds.
  int per_thread_timeout_ms;

  // Indicate if should be called in a signal safe way. Defaults to false.
  bool sigsafe;
};

// Note that this function must be marked noinline for stack traces to work
// reliably across different optimization levels.
ABSL_ATTRIBUTE_NOINLINE
extern int Thread_ProcessStackTraces(const Thread_ProcessStackTracesArg& arg);

// Initialize stack trace extraction code if necessary. Called automatically
// when a Thread is started, and therefore most applications won't need to
// call this.
extern void Thread_InitStacksIfNecessary();

// Register an externally created thread with the thread system.  This
// must be called from within the thread to register.  Calling this
// for an already-registered thread (including a thread created by the
// Thread class) has no effect.
//
// The thread will be automatically unregistered when it exits.
//
// NOT SAFE FOR USE FROM SIGNAL HANDLERS.
extern void Thread_RegisterExternalThread(absl::string_view name_prefix);

// Options for ThreadStackWriter::Write().
struct ThreadStackWriterOptions {
  // If true, perform symbolization.
  // This option is used to communicate with SymbolizedStackWriter, which
  // tries to replace anything that looks like an address with a symbol.
  // In some cases, we know that the addresses we pass are not symbolizable,
  // so we disable it altogether.
  bool symbolize;
};

// Base class that can handle the data generated by Thread_ExtractStacks()
// Use one of the subclasses defined below, or make your own subclass:
//      StderrThreadStackWriter
class ThreadStackWriter {
 public:
  virtual ~ThreadStackWriter();
  virtual void Write(const char* data, int data_length) = 0;
  virtual void Write(const char* data, int data_length,
                     const ThreadStackWriterOptions& options);
};

// A subclass that writes to stderr
class StderrThreadStackWriter : public ThreadStackWriter {
 public:
  void Write(const char* data, int data_length) override;
};

// A subclass that writes to LOG(ERROR)
class LogErrorThreadStackWriter : public ThreadStackWriter {
 public:
  void Write(const char* data, int data_length) override;
};

// ---------------------------------------------------------------------
// Support for attaching debugging notes to threads
// ---------------------------------------------------------------------

namespace thread {

// A thread note is a piece of debugging information attached to a thread.
// Constructing a Note object attaches a note to the current thread. Destructing
// a Note detaches it. The LiveThread_GetNotes functions return the notes
// attached to a given thread.
//
// THREAD NOTES MUST NOT CONTAIN PII. Avoiding PII enables unrestricted access
// to thread notes in Coroner (and prevents notes from being in scope for
// various data governance policies).
//
// REQUIRES: Notes must always be destructed in the opposite order that they
// were constructed. Constructing them on the stack satisfies this requirement.
class Note {
 public:
  // WARNING: The string `note` must not contain PII.
  explicit Note(std::string note);
  explicit Note(absl::string_view note);
  explicit Note(const char* note);
  ~Note();

#ifndef SWIG
  // This type is neither copyable nor movable.
  Note(const Note&) = delete;
  Note& operator=(const Note&) = delete;
#endif  // !SWIG

  absl::string_view note() const { return note_; }

 private:
  std::string note_;
};

// Prints asynchronous backtraces for inactive CoThreads that would otherwise
// would not be included in any individual threads' stack traces.
// The full set is processed (process-wide).
extern "C" bool ProcessInactiveCoThreadTracesImpl(
    ThreadStackWriter* writer, bool signal_safe,
    bool symbolize) ABSL_ATTRIBUTE_WEAK;

};  // namespace thread

// Returns the notes for a thread in an arbitrary order.
std::vector<std::string> LiveThread_GetNotes(const LiveThread* thread);

struct ThreadNotesForTrace {
  std::vector<std::string> notes;
  bool notes_changed_since_stack_trace = false;
};

// Returns the notes for a thread in an arbitrary order. Also returns a boolean
// indicating whether the notes have changed since `trace` was captured.
ThreadNotesForTrace LiveThread_GetNotesForTrace(const LiveThread* thread,
                                                const StackTrace* trace);

// Async signal safe version of LiveThread_GetNotes.
// Calls fn(arg, note) for each note in an unspecified order.
// Returns false if notes could not be fetched (usually due to concurrent
// activity). If `must_match` is non-null, then false is also returned if the
// notes have changed since `must_match` was captured.
// REQUIRES: fn is async-signal-safe.
// REQUIRES: fn does not call back into the thread module or into any other code
// that might call a thread API. Internal locks may be held while fn is being
// invoked.
bool LiveThread_ForEachNoteAsyncSignalSafe(
    const LiveThread* thread, const StackTrace* must_match,
    void (*fn)(void* arg, absl::string_view note), void* arg);

#endif  // THIRD_PARTY_GLOOP_THREAD_THREAD_H_
