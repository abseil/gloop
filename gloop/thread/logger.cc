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

#include "gloop/thread/logger.h"

#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/flags.h"
#include "absl/log/log.h"
#include "absl/log/log_sink_registry.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/base/googleinit.h"
#include "gloop/base/init_google_flags.h"
#include "gloop/base/log_file_flags.h"
#include "gloop/base/logger.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/wait_state.h"

ABSL_FLAG(bool, disable_threaded_logging, false,
          "If this flag is true, we will disable threaded logging feature "
          "regardless of the --threaded_logging flag value");

namespace threadlogger {

namespace {

// Block size for SimpleBuffer.  If it is too small we may fragment
// memory.  If it is too big, we may end up wasting memory.
// The -16 makes the blocks fit nicely in tcmalloc's internal allocations.
const int kBlockSize = (32 << 10) - 16;

// ------------------------------------------------------------------------
// A SimpleBuffer is like a simplified IOBuffer; it has only only Read() and
// Write() operations, and requires that each Read() operation process only
// bytes that were provided in a single Write().  We use it instead of IOBuffer
// to avoid pulling the dependencies of //iobuffer into //thread.
class SimpleBuffer {
 public:
  SimpleBuffer() : next_read_offset_(0) {}

  // This type is neither copyable nor movable.
  SimpleBuffer(const SimpleBuffer&) = delete;
  SimpleBuffer& operator=(const SimpleBuffer&) = delete;
  ~SimpleBuffer() { LOG(FATAL) << "You may not destroy a SimpleBuffer"; }

  // put bytes ptr[0, .., len-1] at the end of the buffer.
  void Write(const void* ptr, int len) {
    // If data doesn't fit in last block, add a block that's big enough.
    if (block_.empty() || len > kBlockSize - block_.back()->used) {
      block_.push_back(static_cast<Block*>(
          malloc(offsetof(Block, data) + std::max(kBlockSize, len))));
      block_.back()->used = 0;
    }
    memcpy(&block_.back()->data[block_.back()->used], ptr, len);
    block_.back()->used += len;
  }

  // Read the next "len" unread bytes from the buffer and return a pointer to
  // the bytes within in the buffer, or NULL if no bytes remain in the buffer.
  // Requires that the bytes being read were written in a single Write()
  // operation.  Ensures that the bytes addressed by the return value will be
  // valid until the next Read().
  char* Read(int len) {
    char* result = nullptr;
    if (!block_.empty() && next_read_offset_ == block_.front()->used) {
      free(block_.front());  // block at front is no longer needed
      block_.pop_front();
      next_read_offset_ = 0;
    }
    if (!block_.empty() && next_read_offset_ < block_.front()->used) {
      result = &block_.front()->data[next_read_offset_];
      next_read_offset_ += len;
      CHECK_LE(next_read_offset_, block_.front()->used);
    }
    return result;
  }

 private:
  struct Block {
    int used;      // number of bytes used in data[]
    char data[1];  // size is max(used, kBlockSize); must be last field
  };
  std::deque<Block*> block_;  // The data buffer is composed of this deque of
                              // blocks.
  int next_read_offset_;  // Offset of first byte of last Read() in block_[0]
};

// ------------------------------------------------------------------------
// We encode commands into a SimpleBuffer to reduce fragmentation
// caused by allocating variable-sized strings.
ABSL_CONST_INIT absl::Mutex pending_lock(absl::kConstInit);
SimpleBuffer* pending_commands = nullptr;
ABSL_CONST_INIT absl::CondVar* wait_nonempty = nullptr;
ABSL_CONST_INIT absl::CondVar* wait_flusher = nullptr;
int64_t flushes_requested;  // counts flushes requested; under pending_lock
int64_t flushes_done;       // counts flushes performed; under pending_lock
Thread* logging_thread = nullptr;
void InitModule();

// Returns whether the number of flushes done is at least *ptr_to_flush_number.
// Used as an argument to Condition().
// L >= pending_lock
bool FlushDone(int64_t* ptr_to_flush_number) {
  return flushes_done >= *ptr_to_flush_number;
}

// header of command sent to logging thread
struct Command {
  base::Logger* logger;
  bool flush;        // Should logger be flushed?
  time_t timestamp;  // Timestamp of this command
  int length;        // Length of following message
};

class ThreadLogWrapper : public base::Logger {
 private:
  base::Logger* logger_;

 public:
  explicit ThreadLogWrapper(base::Logger* logger) : logger_(logger) {}

  void Write(bool force_flush, time_t timestamp, const char* message,
             int message_len) override {
    // Check message length
    assert(message_len >= 0);
    message_len = std::min(message_len, kMaxLogLength);

    absl::MutexLock l(pending_lock);

    if (logger_ == nullptr) {
      // The logger can be nullptr if someone sets it to nullptr.  Just ignore
      // writes to it in this case.
      return;
    }

    // Send command to logging thread
    Command command;
    command.logger = logger_;
    command.flush = force_flush;
    command.timestamp = timestamp;
    command.length = message_len;
    pending_commands->Write(&command, sizeof(command));
    pending_commands->Write(message, message_len);

    wait_nonempty->Signal();
    if (force_flush) {
      flushes_requested++;  // requested a flush
      wait_flusher->Signal();
      if (command.length == 0) {  // flush is synchronous iff Flush was called
        int64_t flush_number = flushes_requested;
        pending_lock.Await(absl::Condition(&FlushDone, &flush_number));
      }
    }
  }

  void Flush() override {
    // Just enqueue a zero-length command to force a flush
    Write(true, 0, "", 0);
  }

  // The returned value is approximate since some
  // logged data may not have been flushed to disk yet.
  size_t LogSize() override {
    absl::MutexLock l(pending_lock);
    if (logger_ == nullptr) {
      return 0;
    }
    return logger_->LogSize();
  }
};

class LoggingThread : public Thread {
 public:
  LoggingThread() : Thread(thread::Options(), "Logger") {}
  virtual void Run();
};

base::Logger* NewThreadedLogWrapper(base::Logger* logger) {
  return new ThreadLogWrapper(logger);
}

void LoggingThread::Run() {
  absl::MutexLock l(pending_lock);

  while (true) {
    // Read the header
    Command command;
    char* ptr;
    if ((ptr = pending_commands->Read(sizeof(Command))) == nullptr) {
      {
        thread::WaitStateScope scope(
            thread::WaitStateScope::WaitState::kWaitingForWork);
        // Wait for a flush, or for 1s, whichever happens sooner.  We do this
        // rather than wait on wait_nonempty to reduce the number of context
        // switches (and hence overhead) for apps that do a lot of logging.
        wait_flusher->WaitWithTimeout(&pending_lock, absl::Seconds(1));
      }

      while ((ptr = pending_commands->Read(sizeof(Command))) == nullptr) {
        thread::WaitStateScope scope(
            thread::WaitStateScope::WaitState::kWaitingForWork);
        // We've already waited 1 second.  This time wait on the condition
        // variable so new log requests will wake the logging thread right
        // away.
        wait_nonempty->Wait(&pending_lock);
      }
    }

    memcpy(&command, ptr, sizeof(command));

    // Read the body
    if (command.length > 0) {
      while ((ptr = pending_commands->Read(command.length)) == nullptr) {
        thread::WaitStateScope scope(
            thread::WaitStateScope::WaitState::kWaitingForWork);
        wait_nonempty->Wait(&pending_lock);
      }
    }

    // Unlock while executing logging command
    pending_lock.unlock();
    {
      if (command.flush && (command.length == 0)) {
        command.logger->Flush();
      } else {
        command.logger->Write(command.flush, command.timestamp, ptr,
                              command.length);
      }
    }
    pending_lock.lock();
    if (command.flush) {
      flushes_done++;
    }
  }
}

// Flush all log files on exit().  This is called as an atexit() handler if
// threaded-logging is enabled at any severity level.  It's unnecessary when
// threaded logging is not in use because logs buffer via stdio, which has its
// own atexit() handler.  The failure path has its own flush calls in the
// signal handler---this affects only processes that call exit().
void FlushLogsAtExit() {
  if (absl::GetFlag(FLAGS_threaded_logging)) {
    absl::FlushLogSinks();
  }
}

void InitModule() {
  if (pending_commands != nullptr) return;
  pending_commands = new SimpleBuffer();
  wait_nonempty = new absl::CondVar;
  wait_flusher = new absl::CondVar;
  atexit(&FlushLogsAtExit);
  logging_thread = new LoggingThread;
  logging_thread->Start();
}

// Has threaded logging been switched on yet?
ABSL_CONST_INIT absl::Mutex enable_lock(absl::kConstInit);
bool module_initializer_called = false;
base_logging::LogSeverity end_severity =
    base_logging::INFO;  // Lowest severity to enable
base_logging::LogSeverity end_severity_requested =
    base_logging::INFO;  // Upper severity bound

}  // namespace

void EnableThreadedLogging(base_logging::LogSeverity max_severity) {
  absl::MutexLock l(enable_lock);

  // Record that user wants threaded logging
  absl::SetFlag(&FLAGS_threaded_logging, true);
  // Safe way of determining max severity level
  const int kMinSeverity = static_cast<int>(absl::LogSeverityAtLeast::kInfo);
  int max_severity_int = std::clamp(kMinSeverity, max_severity,
                                    static_cast<int>(base_logging::FATAL));
  max_severity = max_severity_int;

  end_severity_requested =
      std::max(end_severity_requested,
               static_cast<base_logging::LogSeverity>(max_severity + 1));

  // It's important to call this BEFORE adding the threaded wrappers below
  // because if thread-creation fails here, the logged FATAL will never
  // appear to be FlushDone and the main thread won't exit.
  InitModule();

  // Disable loggers, but keep the logger thread for now. Disabling the logging
  // thread causes unexplained TSAN failures, so we still start the thread for
  // now.
  // TODO: Figure out why disabling the logging thread causes
  // TSAN failures.
  if (absl::GetFlag(FLAGS_disable_threaded_logging)) return;

  if (module_initializer_called && end_severity < end_severity_requested) {
    if (!absl::GetFlag(FLAGS_silent_init)) {
      LOG(INFO) << "Enabling threaded logging for severity "
                << absl::LogSeverityName(static_cast<absl::LogSeverity>(
                       end_severity_requested - 1));
    }
    for (base_logging::LogSeverity s = end_severity; s < end_severity_requested;
         ++s) {
      base::SetLogger(s, NewThreadedLogWrapper(base::GetLogger(s)));
    }
    end_severity = end_severity_requested;
  }
}

REGISTER_MODULE_INITIALIZER(threadlogger, {
  enable_lock.lock();
  module_initializer_called = true;
  enable_lock.unlock();
  if (absl::GetFlag(FLAGS_threaded_logging)) {
    EnableThreadedLogging(base_logging::WARNING);
  }
});

}  // namespace threadlogger
