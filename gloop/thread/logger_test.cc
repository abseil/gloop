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
//
// Test log wrapper that forwards log commands to a logging thread

#include "gloop/thread/logger.h"

#include <cstddef>
#include <ctime>
#include <functional>
#include <iostream>
#include <ostream>

#include "absl/base/log_severity.h"
#include "absl/base/thread_annotations.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/log/log_sink_registry.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/base/init_google.h"
#include "gloop/base/logger.h"

static const int kNumMessagesToSend = 100;

// Special testing logger that counts messages and can be delayed
// by testing code.
class TestLogger : public base::Logger {
 public:
  TestLogger()
      : counter_(0), flush_counter_(0), delivery_thread_(pthread_self()) {}

  void Write(bool force_flush, time_t timestamp, const char* message,
             int message_len) override {
    absl::MutexLock lock(mutex_);
    counter_++;
    delivery_thread_ = pthread_self();
  }

  void Flush() override {
    absl::MutexLock lock(mutex_);
    flush_counter_++;
    delivery_thread_ = pthread_self();
  }

  size_t LogSize() override {
    absl::MutexLock lock(mutex_);
    return counter_;
  }

  int flush_counter() const {
    absl::MutexLock lock(mutex_);
    return flush_counter_;
  }

  pthread_t delivery_thread() const {
    absl::MutexLock lock(mutex_);
    return delivery_thread_;
  }

  // Waits for the log to reach the specified size. Returns true if
  // (counter_ >= min_size) becomes true within the specified timeout period.
  // Otherwise, returns false.
  bool WaitForLogSizeAtLeast(int min_size, absl::Duration timeout) const {
    std::function<bool()> callback =
        absl::bind_front(&GreaterEqual, &counter_, min_size);
    absl::MutexLock lock(mutex_);
    return mutex_.AwaitWithTimeout(absl::Condition(&callback), timeout);
  }

 private:
  static bool GreaterEqual(const int* value, int target) {
    return *value >= target;
  }

  mutable absl::Mutex mutex_;
  int counter_ ABSL_GUARDED_BY(mutex_);
  int flush_counter_ ABSL_GUARDED_BY(mutex_);
  pthread_t delivery_thread_ ABSL_GUARDED_BY(mutex_);
};

int main(int argc, char** argv) {
  absl::SetFlag(&FLAGS_disable_threaded_logging, false);

  // Use a special logger so we can check whether it got the message.
  TestLogger* logger = new TestLogger;

  // SetLogger takes ownership of logger.
  base::SetLogger(base_logging::INFO, logger);

  InitGoogle(argv[0], &argc, &argv, true);
  threadlogger::EnableThreadedLogging(base_logging::WARNING);

  // Send a couple of flush commands
  absl::FlushLogSinks();
  absl::FlushLogSinks();

  // Send bunch of log messages that should get delayed
  for (int i = 0; i < kNumMessagesToSend; i++) {
    LOG(INFO) << "Test message";
  }

  // Wait for the messages to be delivered, but time out after 2 sec.
  CHECK(logger->WaitForLogSizeAtLeast(kNumMessagesToSend, absl::Seconds(2)))
      << "Log wasn't flushed even after waiting 2 seconds.";

  // All messages must have been delivered
  CHECK_GE(logger->LogSize(), kNumMessagesToSend);
  CHECK_GE(logger->flush_counter(), 2);

  // Check that last delivery must have been from another thread
  CHECK(pthread_self() != logger->delivery_thread());

  std::cout << "PASS" << std::endl;
  return 0;
}
