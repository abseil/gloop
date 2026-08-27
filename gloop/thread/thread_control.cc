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

#include "gloop/thread/thread_control.h"

#include <atomic>

#include "absl/log/log.h"
#include "gloop/base/commandlineflags.h"
#include "gloop/thread/config.h"

namespace thread {

// is_safe indicates whether it is safe to create background threads.
static std::atomic<bool> is_safe{true};
// has_been_checked indicates whether we have gone past the point at
// which we can stop background threads from being created.
static std::atomic<bool> has_been_checked{false};

bool DeprecatedThreadControl::BackgroundThreadsAllowed() {
  has_been_checked.store(true);
  return is_safe.load();
}

void SetFlagIfDefault(const char* name, const char* value) {
  SetCommandLineOptionWithMode(name, value, SET_FLAG_IF_DEFAULT);
}

void DeprecatedThreadControl::AvoidBackgroundThreads() {
  if (has_been_checked.load()) {
    LOG(FATAL) << "Error: Attempt to make single threaded after"
                  " threads started";
  }
  is_safe.store(false);
#if THREAD_HAVE_THREAD_CONTROL
  // TODO Remove SetCommandLine when alternative mechanism in place.
  SetFlagIfDefault("malloc_release_bytes_per_sec", "0");
  SetFlagIfDefault("threaded_logging", "false");
  SetFlagIfDefault("watch_pthread_manager", "false");
  SetFlagIfDefault("watch_thread_liveness", "false");
#endif
}

}  // end namespace thread
