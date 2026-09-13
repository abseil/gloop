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

#include "gloop/util/process/nul_terminated_buf_appender.h"

#include <string.h>

#include <algorithm>
#include <cstddef>

#include "absl/strings/string_view.h"

namespace util_process_internal {

// This routine must be async-signal safe and not call any libc routines
// which might acquire locks and/or touch errno.
void NulTerminatedBufAppender::Append(absl::string_view str) {
  if (IsFull()) {
    return;
  }
  const size_t copylen = std::min(str.size(), size_left_ - 1);
  memcpy(ptr_, str.data(), copylen);
  ptr_[copylen] = '\0';
  // Position ptr_ at the first nul in the string (i.e. allow for internal
  // nuls in `str`, even though it should not have them -- avoid UB).
  const size_t num_appended = strlen(ptr_);
  size_left_ -= num_appended;
  ptr_ += num_appended;
}

// This routine must be async-signal safe and not call any libc routines
// which might acquire locks and/or touch errno.
void NulTerminatedBufAppender::Append(int number) {
  if (IsFull()) {
    return;
  }

  // Since there is no available integer->decimal conversion function that
  // guarantees async-signal safety, we're rolling our own.
  if (number < 0) {
    *ptr_++ = '-';
    if (--size_left_ == 1) {
      *ptr_ = '\0';
      return;
    }
    number = -number;
  }

  // Put the digits in reverse order into a scratch buffer.
  char scratch[80];
  char* digit_ptr = scratch;
  do {
    *digit_ptr++ = (number % 10) + '0';
    number /= 10;
  } while (number > 0);

  // Copy the digits in reverse order to the buffer.
  while (size_left_ > 1 && digit_ptr > scratch) {
    *ptr_++ = *--digit_ptr;
    --size_left_;
  }
  *ptr_ = '\0';
}

}  // namespace util_process_internal
