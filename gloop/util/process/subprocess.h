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

#ifndef THIRD_PARTY_GLOOP_UTIL_PROCESS_SUBPROCESS_H_
#define THIRD_PARTY_GLOOP_UTIL_PROCESS_SUBPROCESS_H_

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
//
//       Global Resource Usage and Other Warnings
//
//  Some of the options available in this package make the following
//  alterations to the global state of the process, which may not
//  coexist well with other components' assumptions:
//   1) We ignore SIGPIPE.  This is completely sensible and shouldn't
//      cause problems for anyone.
//
//  See the section "SubProcess States" for thread-safety issues.
//  If you need to manage the I/O with a SelectServer,
//  use net/process/selectprocess.h.
//
// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
//
//        SubProcess
//
//  The SubProcess class allows you to start subprocesses in flexible,
//  thread-safe ways.  Starting a UNIX process requires a lot of
//  attention to detail and can lead to subtle bugs if not done
//  correctly, especially if large numbers of subprocesses are run.
//
//  Overview
//  --------
//
//  A SubProcess will handle the usual stdin, stdout, and stderr
//  channels.  They can be connected to existing open descriptors,
//  have pipes created for them, or closed.  If needed, you can use
//  more than the three usual channels.  (In fact, we don't use pipes -
//  we use socketpairs, so communication can be bidirectional, if
//  needed.)
//
//  A SubProcess can be re-used.  Most of the settings become
//  defaults for later process executions.  This makes it convenient
//  to configure a single I/O paradigm (e.g., close stdin, map stderr
//  to stdout) and run multiple programs in the same framework - or
//  the same program over and over.
//
//  However, a SubProcess can handle only one subprocess at a
//  time.  You must have a SubProcess object for each running
//  process.
//
//  Once you've started a subprocess, you can read or write data on
//  the descriptors for which you requested ACTION_PIPE.
//
// SubProcess States
// -----------------
// A SubProcess object may be in one of these states:
//    initializing --- the client is setting up to run a process.
//                     This is the initial state, and may be reentered from
//                     finished.
//    running      --- the process is running.
//    finished     --- the process is finished.
//
// These calls are useful in any state:
//   filename(), GetDirectory(), GetEnviron(), GetArgv()
//   GetChannelAction()
// They may be called concurrently with one another,
// but not concurrently with any other instance method calls.
//
// These calls are useful in the initializing and finished states:
//   SetChannelAction(), SetChannelFD(), SetExitCallback(), SetDirectory(),
//   SetEnviron(), SetUseProcessGroup(), SetUseSession(), SetAbandoned(),
//   SetParentDeathSignal(), SetProgram(), SetShellCommand(), SetRLimit(),
//   ClearRLimits(), SetPriority(), Start()
// Any of these calls cause the SubProcess to be in the initializing state.
// They may not be called concurrently with other calls to these or other
// instance methods.
// The Start() call starts the process, and transitions the SubProcess to
// the running state when it returns.
//
// These calls are useful in the running state:
//   GetFD(), TakeoverFD(), Close()
// These calls may be made concurrently with one another and
// other calls legal in the running state.
//
// These calls are useful in the running or finished states:
//   pid(), running(), start_time(), Kill(), CheckRunning(), Wait()
// They may be called at any time by any thread while in the running
// or finished states.
// The SubProcess is in the finished state when Wait() returns,
// or if running(), or CheckRunning() return false.
//
// These calls are useful in the finished state:
//   exit_status(), error_text(), exit_normal(), exit_signum(), exit_code(),
//   finish_time(), GetResourceUsage()
// They may be called at any time by any thread while in the finished state.
//
//
// These static methods may be called at any time by any thread:
//    DoWait(), ProcessCount(), ProcessListMemoryUsage(), System(),
//    POpen(), PClose(), GetStatusOutput()

//  Using the Channels
//  ------------------
//  When you specify ACTION_PIPE for a channel, the SubProcess object
//  keeps track of the descriptor and closes it after the SubProcess
//  is destroyed or reused.
//  Just like any other object, it is important to keep track of who
//  owns it.  By default, the SubProcess owns these descriptors.
//  You can get the descriptor using the GetFD() method and use that
//  in all of the normal ways, except that you *must not* call close(fd).
//  Instead, you must use the Close() method, which will do the right
//  bookkeeping.
//
//  Alternatively, you can use TakeoverFD() to assume ownership of the
//  descriptor - the SubProcess will completely forget about it.
//
//  Other notes about the channels:
//   * They are socketpairs, not pipes, so you can use shutdown() to
//     close just one direction of data transfer.
//   * They are initially set non-blocking.
//   * They are set close-on-exec.
//
//  Exit Callback
//  -------------
//  The client can register a callback that will be executed
//  once when the process exits.  If no exit callback is specified,
//  the client can use the CheckRunning() method to check the status.
//  SubProcess takes ownership of the exit callback.  The callback will
//  be reused if the SubProcess is Run() again.
//  It is an error to delete the SubProcess within the callback routine.
//
//  In order for the exit callback to be run, *something* has to check
//  the exit status of the process.  This can happen in one of the
//  following ways:
//    * You periodically call one of:
//        * DoWait() - check all process' status,
//        * CheckRunning() - check one process,
//        * Wait() - wait for one process to exit.
//      The callback will be run before those methods return.
//    * If you're using a SelectProcess, all of this is taken care of
//      for you.
//  A thread in some other library may be using SubProcess and may call
//  DoWait() at any time.  Therefore, you must be prepared for the exit
//  callback to be called at any time after the process exits, and from any
//  thread.
//
//  Process Exit and Final Data
//  ---------------------------
//  When DoWait() finds that a process has exited, it does the
//  following things:
//    1) Stores the process's exit status
//    2) Runs the exit callback, if provided.
//  The channels will be closed when either the SubProcess is destroyed
//  or reused to start a new process.  It's your job to make sure that
//  you read the data before the channels are closed:
//    * Set the channel to block when reading.  (turn off non-blocking)
//    * Read from the channel until you get an EOF indication.
//  This is covered, in detail, in the examples.
//
//  SubProcess Object Deletion
//  --------------------------
//  When you delete a SubProcess while a process is running, the
//  process remains running.  No signals are sent.  However, any open
//  descriptors that it owns are closed.  (If you want them left open,
//  you can call TakeoverFD() to assume ownership of them.)
//
//  Note that, if you do this, it is your responsibility to wait on
//  the process, lest you accumulate zombies.
//
//  Examples
//  --------
//
//  The following are just general sketches.  If you have questions,
//  feel free to ask questions.
//
//  Run a process, no input; don't collect output, and wait until it's done.
//    SubProcess p;
//    p.SetProgram(...);
//    p.Start();
//    p.Wait();
//
//  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//
//  Run a process: no input; collect output; and wait until it's done.
//  It is typical to keep reading until the pipe from the process closes,
//  then Wait().
//    SubProcess p;
//    p.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
//    p.SetChannelAction(CHAN_STDERR, ACTION_MAPTOSTDOUT);
//    p.SetProgram(...);
//    p.Start();
//    int fd = p.GetFD(CHAN_STDOUT);
//    fcntl(fd, F_SETFL, 0);      //  Clear non-blocking.
//    char buf[nnn];
//    while (true) {
//      int bytes = read(fd, buf, sizeof(buf));
//      if (bytes == -1 && errno == EINTR)  //  Interrupted syscall - try again.
//        continue;
//      if (bytes <= 0) {
//        p.Close(CHAN_STDOUT);  //  Some other serious error, or end-of-file.
//        break;
//      }
//      ... do something with the output, in buf ...
//    }
//    p.Wait();
//
//  Note: if you just want to save output to a string, you can use the
//  Communicate() method to simplify this:
//  p.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
//  p.SetProgram(...);
//  p.Start();
//  std::string output;
//  p.Communicate(&output);
//
//  Note: when using Communicate() method, you SHOULD NOT call Wait(), as
//  Communicate() calls Wait() internally.
//  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//
//  Run a process: send some data on stdin; collect output; and wait
//  until it's done.  NOTE WELL: If there's too much data to send (the
//  stdin pipe fills up) and the process blocks waiting for you to
//  read the output, this will deadlock.  If you need to do this kind
//  of filtering operation, a simple approach is to read with
//  one thread and write with another.   If you must interwork with
//  a SelectServer, use SelectProcess and callback-driven I/O.
//
//    SubProcess p;
//    p.SetChannelAction(CHAN_STDIN, ACTION_PIPE);
//    p.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
//    p.SetChannelAction(CHAN_STDERR, ACTION_MAPTOSTDOUT);
//    p.SetProgram(...);
//    p.Start();
//    int fd_out = p.GetFD(CHAN_STDOUT);
//    fcntl(fd_out, F_SETFL, 0);            //  Clear non-blocking.
//    write(p.GetFD(CHAN_STDIN), xxx, nnn);
//    ...  Same as the "reading" example.  ...
//
//  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//
//  Run a process:  Set an exit callback, just for fun.
//    void HandleExit(SubProcess* sp) {
//      //  Note that the channels are still open.
//      LOG(INFO) << "Process Finished!";
//    }
//    SubProcess p;
//    p.SetProgram(...);
//    p.SetExitCallback(&HandleExit);
//    p.Start();
//     read all output here, if desired.
//    p.Wait();           <--- Callback is run during Wait(),
//                             or before (since any thread in any library may
//                             call DoWait() at any time).
//  As you can see, using an exit callback with SubProcess is almost
//  completely pointless.  (With SelectProcess, it certainly has
//  utility, however.)
//
//  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//
//  Run a process, from a string containing a raw executable file.  This may
//  be used with the cc_embed_data BUILD rule.
//
//    std::string filename;
//    int fd = SubProcess::StringToExecutableFile(data, size, &filename);
//    CHECK_GE(fd, 0);
//    SubProcess p;
//    p.SetProgram(filename, ...argv...);
//    p.Start();
//    p.Wait();
//    close(fd);  // Allow OS to deallocate temp file.
//
//  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//
//  Run a function in SubProcess. Unlike running a function in a thread, this
//  approach doesn't share memory with the child. Any exchange of data with
//  the subprocess needs to use file I/O, POSIX shared memory, RPCs, etc.
//  This method is useful when you want to avoid issues associated with a second
//  binary file, such as issues that packaging is trickier or MPM is much larger
//  with two binary files. There are no direct SubProcess APIs to run a function
//  in a SubProcess, but you can achieve it by using, preferably in tests,
//  testing::Process, which is based on SubProcess, or doing something similar
//  to testing::Process on your own. Basically the idea is:
//
//  ABSL_FLAG(bool, child_indicator, false, "...."); // Define it somewhere
//
//    InitGoogle(...);
//    if (absl::GetFlag(FLAGS_child_indicator)) {
//      return SomeFunction();  <--- this is the function to run in child
//    }
//    std::vector<std::string> args = {
//        base::GetArgv0(),
//        "--child_indicator",
//    };
//    SubProcess p;
//    p.SetArgv(args); <--- Use the current binary and add --child_indicator
//                          in the command line
//    p.Start();
//    p.Wait();

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <cstdint>
#include <functional>
#include <initializer_list>

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/gtl/unique_array.h"

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "gloop/base/file_toc.h"

//  We depend on these being the canonical 0, 1, 2.
enum Channel
// This enum is made into an int in the swig wrapper file.  The swig parser
// does not understand the underlying enum type specified here.
#ifndef SWIG
    : int
#endif  // SWIG
{
  CHAN_STDIN = STDIN_FILENO,
  CHAN_STDOUT = STDOUT_FILENO,
  CHAN_STDERR = STDERR_FILENO,
};

//  Define how a channel is handled.
enum ChannelAction {
  //  If SetChannelFD() was called for the channel:
  //     Attach the given file descriptor to the process's channel;
  //  Otherwise:
  //     Close the file descriptor when the process starts.
  //  ACTION_CLOSE is the default behavior.
  ACTION_CLOSE,

  //  Make a pipe for the channel.  It can be accessed with
  //  GetFD(channel).  In fact, this creates a socketpair(2), which is
  //  more flexible in some contexts.  Our end of the pipe is set up
  //  as close-on-exec and non-blocking.
  ACTION_PIPE,

  //  Valid for stderr only.  Make stderr appear on stdout.
  ACTION_MAPTOSTDOUT,

  //  Duplicate the parent's file handle. Useful if stdout/stderr should
  //  go to the same place that the parent writes it.
  ACTION_DUPPARENT,

  ACTION_NUM  //  marker
};

class SubProcess {
 public:
  // SubProcess()
  //    @param nfds The number of file descriptors to use.
  explicit SubProcess(int nfds = 3);

#ifndef SWIG
  // This type is neither copyable nor movable.
  SubProcess(const SubProcess&) = delete;
  SubProcess& operator=(const SubProcess&) = delete;
#endif

  // Virtual for backwards compatibility; do not create new subclasses.
  // It is illegal to delete the SubProcess within its exit callback.
  virtual ~SubProcess();

  virtual int num_chan() const { return CHAN_STDIN + nfds(); }

  virtual bool chan_valid(int chan) const {
    return chan >= CHAN_STDIN && chan < num_chan();
  }

  // GetFD()
  //    Get the actual file descriptor for the given channel.
  //    All of the fds will be -1 if the process isn't running.
  //
  //    Fatal error conditions:
  //      Invalid channel number;
  //
  //    @param chan Which channel?
  //    @return A file descriptor
  virtual inline int GetFD(Channel chan) const {
    if (!chan_valid(chan)) {
      LOG(FATAL) << "GetFD called with invalid channel: " << chan;
    }
    absl::MutexLock l(mu_);
    return fd_[chan];
  }

  // TakeoverFd()
  //    Same as GetFD(), except that the SubProcess will give up
  //    control of the descriptor - it won't try to close it when the
  //    process finishes.
  //
  //    Fatal error conditions:
  //      Invalid channel number;
  //
  //    @param chan Which channel?
  //    @return A file descriptor;
  //            -1 if there is no file descriptor - it was closed, etc.
  virtual inline int TakeoverFD(Channel chan) {
    if (!chan_valid(chan)) {
      LOG(FATAL) << "TakeoverFD called with invalid channel: " << chan;
    }
    absl::MutexLock l(mu_);
    int fd = fd_[chan];
    fd_[chan] = -1;
    return fd;
  }

  virtual inline pid_t pid() const { return pid_; }
  virtual inline const char* filename() const {
    return filename_.has_value() ? filename_->c_str() : nullptr;
  }

  //  Is the process running?  This just checks the most-recently-known status.
  //  See CheckRunning() for a thorough test.
  virtual bool running() const;

  //  kStatusUnknown
  //  The "exit status" if the process exited, but we weren't able to
  //  collect the exit status.  (This should only happen if some other
  //  code in this program calls wait3() and collects one of *our*
  //  processes' status.  This is out of range of the usual one-byte
  //  representation for exit status.
  //
  //  IMPORTANT: If you want to check for this condition, you must use
  //  the exit_status() method to get the raw status.  The macros used
  //  in exit_signum() and exit_code() will munge it.
  enum { kStatusUnknown = 0xffff };

  //  "exit code" if the process was killed.
  //  This is returned if you ask for exit_code(), and the process was
  //  actually killed (in which case there isn't really an exit code).
  enum { kWasKilled = -256 };

  // "exit code" if exec() never managed to start the process in the first
  // place. This is returned if you ask for exit_code(), and the process
  // was never started (in which case there isn't really an exit code).
  enum { kExecFailed = -257 };

  //  Return the raw exit status of the process.
  virtual inline int exit_status() const { return exit_status_; }

  //  Return a useful string describing why the child failed
  virtual std::string error_text() const { return error_text_; }

  //  Return true if the process exited successfully
  //  (zero return code, no signal).
  virtual inline bool exit_normal() const { return exit_status() == 0; }

  //  Return the signal number if the process was killed, or zero.
  virtual inline int exit_signum() const {
    // This function's value is undefined, if exec() failed to ever run the
    // process. But for compatibility with an older version of this class,
    // we simulate a SIGABRT signal.
    int status = exit_status();
    return status == kExecFailed ? SIGABRT
           : WIFSIGNALED(status) ? WTERMSIG(status)
                                 : 0;
  }

  //  Return the exit code, assuming that the process wasn't killed by
  //  a signal.
  virtual inline int exit_code() const {
    int status = exit_status();
    return status == kExecFailed ? kExecFailed
           : WIFEXITED(status)   ? WEXITSTATUS(status)
                                 : kWasKilled;
  }

  virtual inline absl::Time start_time() const { return start_time_; }
  virtual inline absl::Time finish_time() const { return finish_time_; }

  // SetChannelAction()
  //    Set how to handle a channel.  Note that not all actions make
  //    sense for all channels.  The default action is ACTION_CLOSE.
  //    The action is set for all subsequent processes, until
  //    SetChannel() is called again.
  //
  //    SetChannel may not be called while the process is running.
  //
  //    @param chan Which channels this applies to, values greater than nfds are
  //    invalid.
  //    @param action What to do with the channel.
  // Virtual for backwards compatibility; do not create new subclasses.
  virtual void SetChannelAction(Channel chan, ChannelAction action);

  // Returns the last action value given to SetChannelAction(chan, action),
  // or the default value if no such call has been made.
  virtual ChannelAction GetChannelAction(Channel chan) const {
    return action_[chan];
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
  //    Fatal error conditions:
  //      Invalid channel number;
  //      Calling while process is running;
  //      Calling twice before calling Start().
  //
  //    @param chan The channel being considered, must be < nfds.
  //    @param fd The file descriptor to attach to the given channel.
  //              This can be canceled by specifying fd < 0.
  //              Setting the same fd for two channels is allowed.
  virtual void SetChannelFD(Channel chan, int fd);
  void SetChannelFD(int chan, int fd) {
    SetChannelFD(static_cast<Channel>(chan), fd);
  }

  // SetChannelFD()
  //    Same as above, except rather than taking a file descriptor,
  //    use a channel from another SubProcess.  This sets the
  //    descriptor from the other SubProcess back into blocking mode,
  //    which is appropriate for a pipe between two processes.
  //    It is a convenience function for:
  //      int fd = sp->TakeoverFD(sp_chan);
  //      fcntl(fd, F_SETFL, 0);
  //      SetChannelFD(chan, fd);
  //    with better error checking.
  //
  //    Fatal error conditions:
  //      Invalid channel number (either one);
  //      Calling while process is running;
  //      Calling twice before calling Start().
  //
  //    @param chan The channel being considered.
  //    @param sp Another process to get the channel from.
  //    @param sp_chan Which channel from that process to use.
  virtual void SetChannelFD(Channel chan, SubProcess* sp, Channel sp_chan);

  // SetInheritHigherFDs()
  //    Don't close the file descriptors beyond the first nfds (usually 3). If
  //    not set, all file descriptors greater than nfds - 1 will be closed.
  //    In general, this should not be set unless you know you need it.
  void SetInheritHigherFDs(bool value) { inherit_higher_fds_ = value; }

  // GetInheritHigherFDs()
  //   Whether higher file descriptors will be left open (See
  //   SetInheritHigherFDs).
  bool GetInheritHigherFDs() const { return inherit_higher_fds_; }

  // SetExitCallback()
  //    Set a callback to be run when the process exits.
  //    See the "Exit Callback" section above for invocation details.
  //    It is illegal to delete the SubProcess within its exit callback.
  virtual void SetExitCallback(std::function<void(SubProcess*)> cb) {
    exit_cb_ = cb;
  }

  // SetDirectory()
  //    In the child process, chdir() to this directory before
  //    exec-ing.
  virtual void SetDirectory(absl::string_view dir) {
    chdir_ = std::string(dir);
  }

  // GetDirectory()
  //   the directory that the child process will chdir to before
  //   exec-ing, or NULL.
  virtual const char* GetDirectory() const {
    return chdir_.empty() ? nullptr : chdir_.c_str();
  }

  // SetChrootDirectory()
  //    In the child process, chroot() to this directory before
  //    exec-ing.  If SetDirectory() is also used, this chroot()
  //    happens AFTER the chdir().  WARNING: it's very tricky to use
  //    this correctly.  chroot() by itself is not necessarily a
  //    viable security mechanism and may introduce new security
  //    problems.  Please request a security review when using this
  //    feature.
  virtual void SetChrootDirectory(absl::string_view dir) {
    chroot_dir_ = std::string(dir);
  }

  // GetChrootDirectory()
  //   the directory that the child process will chroot to before
  //   exec-ing, or NULL.
  virtual const char* GetChrootDirectory() const {
    return chroot_dir_.empty() ? nullptr : chroot_dir_.c_str();
  }

  // SetEnviron()
  //    set the environment that the child process will exec in.
  //    If this is not called then the child will receive the environment
  //    returned by GetThisProcessEnviron().
  virtual void SetEnviron(const char* const* envp);

  typedef absl::flat_hash_map<std::string, std::string> EnvMap;
#ifndef SWIG
  virtual void SetEnviron(const EnvMap& environ);
#endif

  // GetEnviron()
  //    get the environment that the child process will exec in.
  virtual const char* const* GetEnviron() const;
  // GetEnviron()
  //    get the environment that the child process will exec in.
  //    Updates *result, but does not remove preexisting entries.
  void GetEnviron(EnvMap* result) const;

  // GetThisProcessEnviron()
  //    the current process's environment in the form of an EnvMap.
  //    This is the default environment a child process will receive if
  //    SetEnviron() is not called.
  static EnvMap GetThisProcessEnviron();

  // SetUseProcessGroup()
  //    If the client calls this, the child process will be set up as
  //    a process group leader.  Any children that it spawns will
  //    inherit that.  Further, any calls that would have been made to
  //    kill(pid, SIGXXX), will now be made to killpg(pid, SIGXXX).
  virtual void SetUseProcessGroup() {
    CHECK(!running());
    CHECK((pidmode_ == PIDMODE_PGRP) || (pidmode_ == PIDMODE_NONE));
    pidmode_ = PIDMODE_PGRP;
  }

  // SetUseSession()
  //    If the client calls this, the child process will be set up as
  //    a session leader (and hence process group leader). This has
  //    the same effects as SetUseProcessGroup(), and additionally
  //    ensures that if the calling process exits, the child process
  //    group won't get hit with a SIGHUP/SIGCONT if any of its
  //    processes are in the stopped state.
  virtual void SetUseSession() {
    CHECK(!running());
    CHECK((pidmode_ == PIDMODE_SESSION) || (pidmode_ == PIDMODE_NONE));
    pidmode_ = PIDMODE_SESSION;
  }

  // SetAbandoned()
  //    If the client calls this, and the SubProcess object is
  //    destroyed before the child process exits, then
  //    SubProcess::DoWait() will continue to try to wait() for the
  //    child process in order to reap the zombie produced when the
  //    child exits.
  virtual void SetAbandoned() { abandoned_ = true; }

// SetParentDeathSignal()
//    If the client calls this and the parent process dies before the child
//    process exits, the passed-in signal will be delivered to the child
//    process.  Only supported on Linux.
//
//    Caveat: though the Linux manual entry for prctl(2) talks about
//    processes, this feature operates on threads.  To be precise:
//    the signal will be delivered to the child when the thread that
//    called SubProcess::Start() terminates.
//
//    @param signum The signal to be delivered to the child process.
#ifdef PR_SET_PDEATHSIG
  virtual void SetParentDeathSignal(int signum) {
    parent_death_signal_ = signum;
  }
#endif

  // SetDisableThp()
  //    Disable transparent huge pages in the subprocess after calling
  //    fork and before calling exec. If the call to disable THP fails,
  //    a message is logged but the subprocess is not aborted. This sets
  //    the fork method to "fork", as doing so is required to obtain
  //    processes with different address space settings.
#ifdef PR_SET_THP_DISABLE
  virtual void SetDisableThp() { disable_thp_ = true; }
#endif

  // SetProgram()
  //    Set up a program and argument list for execution, with the full
  //    "raw" argument list passed as a vector of strings.  argv[0]
  //    should be the program name, just as in execv().
  //
  //    @param file The file containing the program.  This must be an
  //                absolute path name or a relative path in the current working
  //                directory - $PATH is not searched.  If you need this, you
  //                should consider using SetCommand().  Also, see ExecChild().
  //    @param argv The argument list.
  virtual void SetProgram(absl::string_view file,
                          const std::vector<std::string>& argv);

  // Make sure initializer list chooses the above overload.
  void SetProgram(absl::string_view file,
                  std::initializer_list<std::string> argv) {
    SetProgram(file, std::vector<std::string>(argv));
  }

  // SetProgram()
  //    Same as above, except with char* rather than string.
  //
  //    @param file The file containing the program.
  //    @param argv The argument list.
  //                The array must be terminated with a NULL pointer.
  virtual void SetProgram(absl::string_view file, const char* const argv[]);

  // SetProgram()
  //    Same as above, except with a file descriptor rather than a filename.
  //
  //    @param fd The file descriptor to file containing the program.
  //              File descriptor is not owned by this class and must be
  //              different than Channels.
  //    @param argv The argument list.
  virtual void SetProgram(int fd, const std::vector<std::string>& argv);

  // SetProgram()
  //    Same as above, except with an initializer list.
  void SetProgram(int fd, std::initializer_list<std::string> argv) {
    return SetProgram(fd, std::vector<std::string>(argv));
  }

  // SetProgram()
  //    Same as above, except with char * rather than string.
  //
  //    @param fd The file descriptor to file containing the program.
  //              File descriptor is not owned by this class and must to be
  //              different than Channels.
  //    @param argv The argument list.
  //                This must be terminated with a NULL.
  virtual void SetProgram(int fd, const char* const argv[]);

  // SetArgv()
  //    Same as above, except uses argv[0] as the file to exec. As above,
  //    argv[0] must be the absolute path to the file.
  //
  //    @param argv The argument list. Must have at least one element.
  virtual void SetArgv(const std::vector<std::string>& argv);

  // SetCommand()
  //    Convenience method to run the given command via /usr/bin/env, which will
  //    resolve argv[0] against the system $PATH. Use SetArgv if you know the
  //    full path to the binary to run.
  //
  //    @param argv The argument list. Must have at least one element.
  virtual void SetCommand(const std::vector<std::string>& argv);

  // SetShellCommand()
  //    Convenience method to run the given command, which will be
  //    interpreted by /bin/sh.  It will be executed as:
  //       /bin/sh -c {command}
  //    It copies the given command to the internal buffer.
  //
  //    Be wary of using this method as it is typically unnecessary, requires
  //    proper escaping, and can introduce security vulnerabilities:
  //    https://en.wikipedia.org/wiki/Code_injection#Shell_injection
  //    Prefer SetCommand() if you do not need a shell.
  //
  //    @param command A well-formed shell command.
  virtual void SetShellCommand(absl::string_view command);

  // GetArgv()
  //    The argv passed to SetProgram() or generated by SetShellCommand().
  virtual const char* const* GetArgv() const { return argv_; }

  // GetArgc()
  //    The amount of elements in argv passed to SetProgram() or generated by
  //    SetShellCommand().
  virtual int GetArgc() const { return argc_; }

  // SetRLimit()
  //    Add (resource, soft limit, hard limit) to an internal list.
  //    setrlimit is invoked in the child once for each of these tuples.
  //    Returns true on success, false if soft > hard or hard > kRLimitInfinity.
  //
  //    @param resource RLIMIT_CPU, RLIMIT_CORE, etc from <sys/resource.h>
  //    @param soft soft limit
  //    @param hard hard limit
  virtual bool SetRLimit(int resource, uint64_t soft, uint64_t hard);

  // ClearRLimits()
  //    Discards all rlimits set with SetRLimit().
  virtual void ClearRLimits();

  // Sets the file mode creation mask for the subprocess.
  void SetUmask(int mask) {
    CHECK(!running());
    CHECK(mask == kInheritUmask || mask >= 0);
    umask_ = mask;
  }

  enum { kInheritUmask = -1 };

  // The maximum soft/hard limit value to use in calls to SetRLimit().
  static const uint64_t kRLimitInfinity;

  enum SchedPriorityType {
    OFF,
    RELATIVE,  // this uses nice()
    ABSOLUTE   // this uses setpriority()
  };

  // SetPriority()
  //    Client calls this with an integer. This sets the scheduling
  //    priority of the child process.  If an invalid priority is
  //    given, the priority will not be changed, and the parent will
  //    not be notified.
  //
  //    @param type should we use ABSOLUTE (setpriority) or RELATIVE
  //    (nice) to set the priority, or should this be off.
  //    @param priority what should the priority be.  Ignored if type
  //    is OFF
  virtual void SetPriority(SchedPriorityType type, int priority);

  enum SigAction {
    // SIG_IGN
    SIGACTION_IGNORE,

    // SIG_DFL
    SIGACTION_DEFAULT,

    // Inherit the signal handler. exec resets any explicit handlers to
    // SIG_DFL, so this has meaning only for SIG_IGN vs. SIG_DFL. This is the
    // default for all signal handlers.
    SIGACTION_INHERIT
  };

  // SetSignalAction()
  //    Set the signal action for the child process. The signal can be ignored,
  //    set to its default action, or inherited. Note that the handler is reset
  //    to SIG_DFL by exec if the signal has an explicit handler in the parent
  //    process. This function may not be called while the process is running.
  //
  //    @param signum Which signal's action to update. The process dies if this
  //    is invalid. The signum must be between 1 and NSIG - 1, inclusive, and
  //    may not be SIGKILL or SIGSTOP.
  //    @param action The action to set for the signal in the child process.
  virtual void SetSignalAction(int signum, SigAction action);

  // SetSignalMask()
  //    Set the signal mask for the child process. If this is not called, the
  //    mask is inherited from the parent process. This function may not be
  //    called while the process is running.
  //
  //    @param sigmask The set of signals to block. See man sigsetops for more
  //    details.
  virtual void SetSignalMask(const sigset_t* sigmask);

  // Start()
  //    Run the command that was previously set up with SetProgram()
  //    or SetShellCommand().  All of the following are fatal
  //    (programming) errors:
  //       * Attempting to start when a process is already running.
  //       * Attempting to start without first setting the command.
  //    Note, however, that Start() does not try to validate that the
  //    command/binary actually does anything reasonable (e.g.,
  //    exists or can execute); as such, you can specify a
  //    non-existent binary and Start() will still return true.
  //    You will get a failure from the process, but only after
  //    Start() returns.
  //
  //    @return true, normally;
  //            false, if the program couldn't be started
  //            because of some (other) error.
  //            Iff Start() returned false, "errno" will contain
  //            more detailed information, and error_text() can be
  //            used to obtain a human-readable error string.
  // Virtual for backwards compatibility; do not create new subclasses.
  virtual bool Start();

  // Close()
  //    Close the given channel.
  //
  //    It is critical that you not use close() to close these
  //    descriptors, because any open file descriptors will be closed
  //    when we clean up.  Between the time you close the descriptor
  //    and when things are cleaned up, it's completely likely that
  //    the file descriptor will have been re-used.
  //
  //    Closing filehandles from a thread other than the one that created
  //    this object is not supported.
  //
  //    It's OK to call this more than once - later calls are ignored.
  //    @param chan Which channel to close.
  // Virtual for backwards compatibility; do not create new subclasses.
  virtual void Close(Channel chan);

  //    Send the given signal to the process.  No special handling
  //    will occur -- if the process exits, the usual things will happen.
  //    It is safe to call this method from arbitrary threads in the
  //    application.
  //    @return true, normally;
  //            false, if we couldn't send the signal - likely because
  //            the process doesn't exist.
  //            Sets errno on failure.
  virtual bool Kill(int signal);

  // CheckRunning()
  //    Check to see if the process is still running.
  //
  //    This calls wait4() to check for sure.  This may cause us to
  //    notice that the process has exited, in which case the process
  //    will be cleaned up (and any exit callback run) immediately -
  //    before this returns.  See Reap(), below.
  //
  //    If this reaps the process, a warning will be printed.  If the
  //    signal handling is delayed (especially if a SelectServer is
  //    handling signals), Reap(pid,...) will be called, producing
  //    another warning message.
  //
  //    @return false, if the process has exited;
  //            true, if the process is still running.
  virtual bool CheckRunning();

  // GetResourceUsage()
  //    Get the resource usage for the process and put it into the
  //    given structure.  If a process hasn't yet started, zero the
  //    structure.  If a process is currently running, return the
  //    current usage.  If the process has exited, return the usage as
  //    of process exit.
  //    *** At present, this doesn't work on a running process.
  //
  //    @param usage The resource usage structure to write to.
  virtual void GetResourceUsage(struct rusage* usage);

  // DoWait()
  //    Update the status of all of the processes.
  //    For each exited process, call Reap(), above.
  //
  //    @return The number of processes still in the "running" list.
  static int DoWait();

  // Wait()
  //    Block until the process exits.  When it does, call Reap() on
  //    it, which will run the exit callback, if one is set.
  //
  //    @return true, normally;
  //            false, if the process wasn't running
  //                   (or had already exited and this fact had been reported
  //                    in the return value of another call of Wait() or
  //                    CheckRunning()).
  virtual bool Wait();

  // SetCallbackOnChange()
  //   Sets a callback that is invoked when the return value from
  //   wait4() indicates that the process did not exit.  The supplied
  //   wait_flags are also OR'd with the flags given to wait4.  For
  //   example, SetCallbackOnChange(WUNTRACED, ...) can be used to
  //   receive callbacks on WIFSTOPPED and WIFCONTINUED.  The supplied
  //   callback must be repeatable.
  virtual void SetCallbackOnChange(
      int wait_flags,
      util::functional::CallbackFunctor<SubProcess*, int> callback);

  // ProcessCount()
  //    Find out how many processes are currently running.  This is
  //    different from (and cheaper than) DoWait() because it doesn't reap
  //    recently exited processes. Because of that, it may count processes that
  //    are actually finished.
  //    @return The number of processes in the "running" list.
  static int ProcessCount();

  // ProcessListMemoryUsage()
  //    Collects real memory usage (RSS) for the currently running processes.
  //    There is a race condition between calling this method and subprocesses
  //    exit - if a process gets reaped "about now", we may or may not include
  //    data for it.
  //    @param The RSS (in bytes) of currently running processes for which
  //    memory usage was successfully retrieved.
  //    @return The number of processes for which RSS was successfully
  //    retrieved.
  static int ProcessListMemoryUsage(int64_t* rss);

  // System()
  //    Emulate the system() library function
  static int System(absl::string_view cmd);

  // POpen(), PClose
  //    Emulate the popen()/pclose() library functions
  static FILE* POpen(absl::string_view cmd, absl::string_view mode);
  static int PClose(FILE* fp);

  // StringToExecutableFile()
  //   Given a string with raw executable data, create an unlinked temporary
  //   file which can be directly executed.  This is particularly useful in
  //   conjunction with the cc_embed_data BUILD rule.
  //
  //   Returns a file descriptor with no permissions.  The caller should call
  //   close() on this file descriptor when finished executing.
  //   If errors occur, returns a negative number.
  //
  //   If filename is non-NULL, it will be filled with an exec-able path to
  //   the file, of the form "/proc/*/fd/*"
  static int StringToExecutableFile(const char* data, size_t len,
                                    std::string* filename);

  // CachedResourceToExecutableFile
  //   Convert an embedded file from a cc_embed_data rule into an
  //   executable file; repeated calls with the same FileToc will
  //   return the same temporary file. Returns an error on failure.
  static absl::StatusOr<std::string> CachedResourceToExecutableFile(
      const FileToc* toc);

  // As above, but crashes instead of returning an error on failure.
  static std::string CachedResourceToExecutableFileOrDie(const FileToc* toc);

  // Logs after fork and before execve in subprocess while setting up
  // environment and channel fds for subprocess.
  void EnableChildSetupLogs(bool enable);

 protected:  // DEPRECATED
  // Please do NOT subclass this class.
  // This "protected" section exists for backwards compatibility.
  // New classes should not use it.
  //
  // Subclassing is almost _never_ a good idea unless the subclass accesses
  // no non-public state of the superclass and all overridden methods
  // are either virtual-abstract or trivial (e.g. null).
  // Subclassing has already caused this class to be almost unmaintainable
  // because the invariants of the various routines are unclear; you cannot
  // tell which method is being called when, what locks it must hold or will
  // acquire, nor what it will do.  If you must implement the same interface,
  // use an abstract virtual superclass for this class (creating one if no one
  // has done it previously) and the class you wish to write; for an example of
  // this, see AbstractThreadpool, the abstract version of ThreadPool.
  //
  // If you own a subclass of this class, please convert it to use the same
  // interface, rather than using subclassing.

  // The errno associated with the last child system call.
  int child_errno_;

  //    Run the exit callback.
  // L < subproc_mu
  // Virtual for backwards compatibility; do not create new subclasses.
  virtual void RunExitCallback();

  // Return whether there is an exit callback.
  bool HasExitCallback() const { return exit_cb_ != nullptr; }

  // Return the number of file descriptors used by this Subprocess:
  // the nfds parameter that was passed to the constructor.
  int nfds() const { return nfds_; }

  //  Cause the implementation not to do anything that will step on the toes of
  //  SelectServer:
  //     * Don't touch the SIGPIPE actions.
  //  To be effective, this must be set before the first SubProcess
  //  object is created.
  static void UsingSelectServer();

  // Add/remove a reference to this object---usually indicating a callback is
  // going to be run in another thread.
  void Ref();
  void Unref();

  // Return the reference count. Used mainly for assertions.
  int Refcount() const;

  // DoExit()
  //    Run the exit callback and Close() all open descriptors.
  // L < subproc_mu
  // Virtual for backwards compatibility; do not create new subclasses.
  virtual void DoExit();

  // ExecChild()
  //    Actually exec() the child.  Start() has already done the
  //    following before calling this:
  //         chdir() to the directory specified by SetDirectory()
  //         Set up command_ and argv_;
  //         Set up stdin/stdout/stderr;
  //         Closed all other file descriptors.
  //    This implementation calls execve() on the stored command.
  //
  //    The likely things that a subclass might do here are:
  //        Clean up the environment;
  //        Call a different flavor of execv, such as execvp().
  // Called in child process.
  // Virtual for backwards compatibility; do not create new subclasses.
  virtual void ExecChild();

  // Reap()
  //    Called to clean up after the given process exits, include
  //    running the exit callback.  This is currently called in two
  //    situations:
  //    1) By SubProcess::Wait(), when wait4() indicates that the
  //        process has exited, doesn't exist, or some other problem.
  //    2) By SelectProcess::KillKillKill(), if it was not possible to
  //        kill() the process after trying real hard.  This is an
  //        error condition - shouldn't happen.
  //
  //    If an exit callback has been set, it will be called from
  //    within this method.  All cleanup will be finished by the time
  //    Reap() returns.  (Unless SubProcess has been subclassed, in
  //    which case DoExit() might do anything at all.)
  //
  //    @param pid The process that has exited.
  //    @param status The status returned by wait4()
  //    @param usage The resource usage structure for the process.
  //                 May be NULL.
  //    @return The SubProcess that had that pid, or NULL if there was
  //            no SubProcess with that pid.
  // L < subproc_mu
  static SubProcess* Reap(pid_t pid, int status, const struct rusage* usage);

  // SendFatalError()
  //    Return a fatal error to the parent process. Includes the current
  //    value of errno in the message.
  // Called in child process.
#ifndef SWIG
  [[noreturn]]
#endif
  void SendFatalError(absl::string_view msg) {
    SendFatalError(msg, child_errno_);
  }

  // SendFatalError()
  //    Return a fatal error to the parent process with the given errno.
  // Called in child process.
#ifndef SWIG
  [[noreturn]]
#endif
  void SendFatalError(absl::string_view msg, int arg_errno);

  // Empty virtual methods overridden by unit tests to inject faults.
  virtual void TEST_AfterForkBeforeExecEarly(int child_to_parent_fd) {}
  virtual void TEST_AfterForkBeforeExecLate(int child_to_parent_fd) {}
  virtual void TEST_AfterForkBeforeSetParentDeathSignal() {}

 private:
  //  Sequence of events at process exit
  //  ----------------------------------
  //  When a process exits, we find out about it because someone
  //  noticed (i.e., they wait()-ed on the process), because they
  //  periodically call DoWait().  In any case, either Reap() or
  //  DoWait() will call HandleExit() with the child's exit status and
  //  rusage information.  HandleExit is private and not virtual, so
  //  it is the only entry point to this process.  HandleExit() starts
  //  the following sequence of events in motion:
  //
  //  1) HandleExit records the exit status and calls DoExit().
  //  2) DoExit(), which *is* virtual clears running_ and calls
  //     RunExitCallback().
  //  3) RunExitCallback() runs the exit callback, if one has been set.
  //  4) Back in DoExit() - All of the channels are closed.
  //  -- done --
  //
  //  Subclasses can customize by redefining the following:
  //    DoExit() - to do other subclass-specific cleanup.  At the end,
  //               though, SubClass::DoExit() should be called.
  //    RunExitCallback() - if some other exit callback should
  //                        happen.  However, the subclass shouldn't
  //                        call RunExitCallback(), except from its
  //                        own implementation.

  // HandleExit()
  //    Called by Reap() to do all exit cleanup,
  //    including running the exit callback.  The only things that
  //    must be done are:
  //     * Sanity checks: process is running, pid matches
  //     * Set exit_status_
  //     * Call DoExit()
  //
  //    @param pid The pid that has exited.
  //    @param status The exit status, as returned by wait4().
  //    @param usage The resource usage, as reported by wait4().
  //                 This may be NULL.
  // L < subproc_mu
  void HandleExit(pid_t pid, int status, const struct rusage* usage);

  // SendChildToParentMessage
  //    This message performs all the communication from the child to the
  //    parent. It can send the value of errno, an error message, an
  //    informational message, and any number of file descriptors. All of
  //    these parameters are optional.
  // Called in child process.
  int SendChildToParentMessage(int child_errno, absl::string_view errstr,
                               const char* infostr, int nfd, int* fds);

  // FlushInfoMessages()
  //    Flushes any pending info messages by sending them to the parent.
  // Called in child process.
  void FlushInfoMessages();

  // InternalStrError()
  //    We'd really like to call something like strerror_r() whenever we
  //    notice an error in the child process, but we cannot reliably make
  //    library calls that could acquire locks. Use a stupid little
  //    hand-coded function instead.
  // Called in child process.
  void InternalStrError(int child_errno, absl::string_view msg, char* buf,
                        size_t len);

  static const char* NUM;

#ifndef SWIG
  // ChildLogInfo()
  //     Append a message to the child's info log. The info buffer will
  //     eventually be sent to the parent, and might get printed, if the
  //     parent thinks outputting extra debugging information is going to
  //     be helpful.
  //     Prints a EOL terminated sequence of strings. Instead of a string,
  //     it can also print numbers, if the argument is preceded by the NUM tag.
  // Called in child process.
  void ChildLogInfo(const char* msg, ...) __attribute__((sentinel));
#endif

  // Private accessors
  void set_running(bool running);
  void set_reported_not_running(bool reported_not_running);

  // Internal versions of public calls.
  void LockAndWaitInternal(pid_t sp_pid, int flags);
  static void WaitInternal(SubProcess* sp, pid_t sp_pid, int flags);
  static void DoWaitInternal();

  struct ChildArgs;
  struct ChildBuffers;

  // GlobalInit()
  //    Perform global initialization.  This will be called once, when
  //    the first SubProcess is created.
  // L < subproc_mu
  static void GlobalInit();

  // CloseNonchannelFileDescriptors()
  //    Close all file descriptors >= nfds_.
  //    If we can't get the set of fds from /proc, attempt to close all file
  //    descriptors < openmax.
  // Called in child process.
  void CloseNonchannelFileDescriptors(int openmax);

  // ForkedChild()
  //    The code that runs in the child thread after fork()'ing, but before
  //    exec()'ing.
  // Called in child process.
  void ForkedChild(struct ChildArgs* args);

  // CloneFnc()
  //    Helper function called from clone().
  // Called in child process.
  static int CloneFnc(void* void_args);

  // ForkAndExec()
  //    Set up file descriptors, fork, and exec.  This does all of the
  //    intricate work.  At the very end, in the child process, it
  //    calls ExecChild() to actually do the exec().
  //
  //    Subclasses needing to do more setup should overload Start()
  //    and/or ExecChild().
  //
  //    @success : set *success to true on success, false otherwise.
  // L < subproc_mu
  void ForkAndExec(bool* success);

  // FreeCommand()
  //    Free the storage associated with argv_ and filename_.
  void FreeCommand();

  // FreeEnviron()
  //    Free the storage associated with envp_.
  void FreeEnviron();

  void RefLocked();  // Ref(), but requires L >= subproc_mu

  // GetRunningPids()
  //    Helper function to collect list of pids of currently running processes.
  static void GetRunningPids(std::vector<pid_t>* pids);

  struct RLimitTuple {
    int resource;
    uint64_t soft_limit;
    uint64_t hard_limit;
  };

  std::vector<RLimitTuple> rlimits_;  // populated by SetRLimit

  int umask_;

  int priority_;
  SchedPriorityType priority_type_;

  //  Number of file descriptors to make available.  Default = 3.
  int nfds_;

  // In the child process, close file descriptors greater than nfds_.
  bool inherit_higher_fds_;

  //  The configured behavior for each channel.
  //  This is an array, sized for the number of descriptors we're handling.
  gtl::UniqueArray<ChannelAction> action_;

  //  The exit callback, if configured.
  std::function<void(SubProcess*)> exit_cb_;

  pid_t exit_cb_tid_;  // ID of thread running exit_cb_, or -1
                       // under subproc_mu

  //  Counts the number of external threads of control that are going
  //  to return into this object (i.e., running callbacks) or call
  //  into this object (e.g., RunInSelectLoop()).
  //  This MUST BE ZERO when the object is deleted.
  int extcount_;  // under subproc_mu

  // Mutex to prevent two threads calling wait4() on the same process
  // at the same time.   See the comment on the implementation
  // of WaitInternal().
  absl::Mutex wait_mu_;  // protects wait(pid_, ...) and Reap(pid_, ...)
                         // < subproc_mu

  //  State kept while the process is running.
  mutable absl::Mutex mu_;  // protects fd[...] after Start()

  gtl::UniqueArray<int> fd_;  //  The file descriptor; -1 if closed.
                              //  Array content is under mu_ after Start().

  pid_t pid_;  //  pid of child process.  This remains set after exit.
  absl::Time start_time_;
  absl::Time finish_time_;
  bool running_;               //  If it's running - pid_ will be valid.
                               //  Under subproc_mu.
  bool reported_not_running_;  //  The client has been told the process is
                               //  no longer running via the return value of
                               //  CheckRunning() or Wait().
                               //  Under subproc_mu.
  int exit_status_;            //  Exit status from wait4().  0 before exit.
  std::string error_text_;     //  Diagnostic failure message
  struct rusage final_usage_;  //  Resource usage - after it exits.

  std::string chdir_;       //  Directory to chdir() to before exec().
  std::string chroot_dir_;  //  Directory to chroot() to, after chdir().

  gtl::UniqueArray<int> set_fd_;  //  The descriptors set by SetChannelFD().

  enum PidMode {
    PIDMODE_NONE,     // No session or pgrp needed
    PIDMODE_PGRP,     // Run child as a pgrp leader
    PIDMODE_SESSION,  // Run child as a session leader
  };
  PidMode pidmode_;          //  Whether a new session or pgrp is needed
  bool disable_thp_;         //  Disable transparent huge pages before exec.
  bool abandoned_;           //  Leave a NULL pointer in process map
                             //  if destroyed before the child exits.
  int parent_death_signal_;  //  Send this signal to the child process if the
                             //  parent process dies.

  // Store filename_ in an optional because of the contract that Start() will
  // not check its validity as long as it has been set.
  std::optional<std::string>
      filename_;  //  Saved from the most recent StartProgram().

  // fd for execveat.
  int execve_fd_ = -1;

  //  Each argv_[i] is allocated with strdup().
  //  argv_ is allocated with new[].
  char** argv_;
  int argc_;

  // the environment to run the process in
  char** envp_;

  // fd to use for reporting errors, debug info, and file handles to the parent
  int child_to_parent_fd_;

  // buffers for the child to use
  std::unique_ptr<ChildBuffers> child_buffers_;

  // If true, we should use sigmask_ for the child's signal mask.
  bool set_sigmask_;

  // The child's signal mask. Used only if set_sigmask_ is true.
  sigset_t sigmask_;

  // An array of actions to perform by signal.
  SigAction sigactions_[NSIG];

  int additional_wait_flags_;
  util::functional::CallbackFunctor<SubProcess*, int> change_callback_;

  // child logs before execve subprocess.
  bool child_setup_logs_enabled_;
};

#endif  // THIRD_PARTY_GLOOP_UTIL_PROCESS_SUBPROCESS_H_
