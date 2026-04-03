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

// A printf() wrapper that writes into a fixed length buffer.
// Useful in low-level code that does not want to use allocating
// routines like StringPrintf().
//
// The implementation currently uses vsnprintf().  This seems to
// be fine for use in many low-level contexts, but we may need to
// rethink this decision if we hit a problem with it calling
// down into malloc() etc.

#ifndef THIRD_PARTY_GLOOP_BASE_RAW_PRINTER_H_
#define THIRD_PARTY_GLOOP_BASE_RAW_PRINTER_H_

#include "absl/strings/str_format.h"

namespace base {

class RawPrinter {
 public:
  // REQUIRES: "length > 0"
  // Will printf any data added to this into "buf[0,length-1]" and
  // will arrange to always keep buf[] null-terminated.
  RawPrinter(char* buf, int length);

  // Return the number of bytes that have been appended to the string
  // so far.  Does not count any bytes that were dropped due to overflow.
  int length() const { return static_cast<int>(ptr_ - base_); }

  // Return the number of bytes that can be added to this.
  int space_left() const { return static_cast<int>(limit_ - ptr_); }

  // Format the supplied arguments according to the "format" string
  // and append to this.  Will silently truncate the output if it does
  // not fit.
  template <typename... Args>
  void Printf(const absl::FormatSpec<Args...>& format, const Args&... args) {
    if (limit_ > ptr_) {
      int avail = space_left();
      // We pass avail+1 to vsnprintf() since that routine needs room
      // to store the trailing \0.
      const int r = absl::SNPrintF(ptr_, avail + 1, format, args...);
      if (r < 0) {
        // Perhaps an old glibc that returns -1 on truncation?
        ptr_ = limit_;
      } else if (r > avail) {
        // Truncation
        ptr_ = limit_;
      } else {
        ptr_ += r;
      }
    }
  }

  void reset();

 private:
  // We can write into [ptr_ .. limit_-1].
  // *limit_ is also writable, but reserved for a terminating \0
  // in case we overflow.
  //
  // Invariants: *ptr_ == \0
  // Invariants: *limit_ == \0
  char* base_;   // Initial pointer
  char* ptr_;    // Where should we write next
  char* limit_;  // One past last non-\0 char we can write

  RawPrinter(const RawPrinter&) = delete;
  RawPrinter& operator=(const RawPrinter&) = delete;
};

}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_RAW_PRINTER_H_
