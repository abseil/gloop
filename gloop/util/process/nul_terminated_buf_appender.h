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

#ifndef THIRD_PARTY_GLOOP_UTIL_PROCESS_NUL_TERMINATED_BUF_APPENDER_H_
#define THIRD_PARTY_GLOOP_UTIL_PROCESS_NUL_TERMINATED_BUF_APPENDER_H_

#include <cstddef>

#include "absl/strings/string_view.h"

namespace util_process_internal {

// Manages appending to a buffer when the contents should be nul-terminated.
// The functions are async-signal safe.
class NulTerminatedBufAppender {
 public:
  // Create a NulTerminatedBufAppender that will append bytes to `buf`, starting
  // with the beginning of the buffer. The buffer should have size > 0 and a nul
  // byte will terminate the appended bytes.
  NulTerminatedBufAppender(char* buf, size_t len) : ptr_(buf), size_left_(len) {
    if (size_left_ > 0) {  // Don't exhibit UB if buffer has size 0.
      ptr_[0] = '\0';
    }
  }

  bool IsFull() const { return size_left_ <= 1; }

  size_t SizeLeft() const { return size_left_; }

  // Appends as much of `str` that fits in the buffer, making sure the result is
  // nul-terminated. `str` should not have internal nul characters, nor should
  // the memory it references overlap the target buffer. Async-signal safe.
  void Append(absl::string_view str);

  // Appends as much of the decimal representation of `number` that fits in the
  // buffer, making sure the result is nul-terminated. Async-signal safe.
  void Append(int number);

 private:
  char* ptr_;         // current write pointer
  size_t size_left_;  // number of bytes remaining in the buffer.
};

}  // namespace util_process_internal

#endif  // THIRD_PARTY_GLOOP_UTIL_PROCESS_NUL_TERMINATED_BUF_APPENDER_H_
