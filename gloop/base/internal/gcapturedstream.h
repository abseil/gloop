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

#ifndef THIRD_PARTY_GLOOP_BASE_INTERNAL_GCAPTUREDSTREAM_H_
#define THIRD_PARTY_GLOOP_BASE_INTERNAL_GCAPTUREDSTREAM_H_

#include <string>

namespace base_logging {
namespace logging_testing {

// Creates a file at `filename` and redirects writes to `fd` there until
// destroyed or Stopped.  This is thread-hostile vs other writes to `fd`,
// including via stdio, but it works okay in practice for tests.
class GCapturedStream {
 public:
  GCapturedStream(int fd, std::string filename);
  GCapturedStream(const GCapturedStream&) = delete;
  GCapturedStream& operator=(const GCapturedStream&) = delete;
  ~GCapturedStream();

  void Stop();

 private:
  // The file descriptor this object intercepts output to.
  const int fd_;
  // A descriptor which is copy of the original `fd_`; to be restored upon
  // destruction or `Pause()`.
  const int original_fd_;
  // The file where intercepted output is written.
  const std::string filename_;
};

}  // namespace logging_testing
}  // namespace base_logging

#endif  // THIRD_PARTY_GLOOP_BASE_INTERNAL_GCAPTUREDSTREAM_H_
