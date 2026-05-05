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

#include "gloop/base/internal/gcapturedstream.h"

#include <fcntl.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <io.h>
#endif

#include <cstdio>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "gloop/base/port.h"  // IWYU pragma: keep

namespace base_logging {
namespace logging_testing {

GCapturedStream::GCapturedStream(int fd, std::string filename)
    : fd_(fd), original_fd_(dup(fd_)), filename_(std::move(filename)) {
  PCHECK(original_fd_ != -1);

  const int cap_fd =
      open(filename_.c_str(), O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);
  PCHECK(cap_fd != -1);

  fflush(nullptr);
  PCHECK(dup2(cap_fd, fd_) != -1);
  PCHECK(close(cap_fd) != -1);
}

GCapturedStream::~GCapturedStream() {
  // This might be the second call to `Stop`, but it's idempotent, so that's ok.
  Stop();
  PCHECK(close(original_fd_) != -1);
}

void GCapturedStream::Stop() {
  fflush(nullptr);
  PCHECK(dup2(original_fd_, fd_) != -1);
}

}  // namespace logging_testing
}  // namespace base_logging
