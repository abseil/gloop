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

// SymbolizedStackWriter is a sub class of ThreadStackWriter.
// This class is used for symbolizing hexadecimal addresses
// in stack traces using SymbolMap.  This is particularly
// useful for /threadz of HTTPServer.
//
// Experiments with a real-world server shows that extracting a
// stacktrace takes ~250ms. Extracting symbol information adds ~75ms
// and is typically cached.

#ifndef THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_SYMBOLIZED_STACK_WRITER_H_
#define THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_SYMBOLIZED_STACK_WRITER_H_

#include <memory>
#include <string>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "gloop/thread/thread.h"
namespace util {
class SymbolMap;
class SymbolizedStackWriterImpl;

class SymbolizedStackWriter : public ThreadStackWriter {
 public:
  // Use this constructor for general cases.  It internally uses a symbol map
  // obtained by `SymbolMap::GetCached()`.  Stack traces will be appended to
  // `output`.
  explicit SymbolizedStackWriter(std::string* output);

  // Use this if you want to use a particular instance of `SymbolMap`.
  // This constructor is also used for unit testing.
  explicit SymbolizedStackWriter(const SymbolMap& symbol_map,
                                 std::string* output);

  // Use this if you want to have a callback run for every thread
  // instead of getting stacks for all threads in a single string.
  explicit SymbolizedStackWriter(
      absl::AnyInvocable<void(absl::string_view)>&& per_thread_cb);
  explicit SymbolizedStackWriter(
      const SymbolMap& symbol_map,
      absl::AnyInvocable<void(absl::string_view)>&& per_thread_cb);

  SymbolizedStackWriter(const SymbolizedStackWriter&) = delete;
  SymbolizedStackWriter& operator=(const SymbolizedStackWriter&) = delete;

  ~SymbolizedStackWriter() override;

  // Set whether to symbolize threads after the "creator:" marker.
  // This must be called before `Write()` for it to take effect.
  // If `symbolize_creator_thread` is `true`, then the creator's thread frames
  // will be symbolized.  Otherwise, the creator thread's frames will not be
  // symbolized and will be printed on a single line.
  void set_symbolize_creator_thread(bool symbolize_creator_thread);

  // This function symbolizes hexadecimal addresses in stack traces
  // with pretty formatting before writing them into `output`. Does
  // not clear `output`.
  // Basic formatting is just like this:
  //
  //   "0x00000040" => "  0x00000040: foo\n"
  //
  // White spaces between addresses will be neatly handled.
  // Example:
  //
  // == With StderrThreadStackWriter
  // --- Thread 4000 stack: ---
  //        0x80c0a87  0x80c1e37  0x80c1b2f  0x80c17a3  0x80c0c22 0x400b82a1
  //        0x80c0781
  //
  // == With SymbolizedStackWriter
  // --- Thread 4000 stack: ---
  //   0x080c0a87: Stacktrace(char const *)
  //   0x080c1e37: StacktraceTest::func2(void)
  //   0x080c1b2f: StacktraceTest::func1(void)
  //   0x080c17a3: StacktraceTest::func(void)
  //   0x080c0c22: main
  //   0x400b82a1: (unknown)
  //   0x080c0781: (unknown)
  //
  void Write(const char* buf, int len) override;

  void Write(const char* buf, int len,
             const ThreadStackWriterOptions& options) override;

 private:
  // `thread_output_` is only used when logging stacks with a callback.
  std::string thread_output_;
  std::unique_ptr<SymbolizedStackWriterImpl> impl_;
};
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_SYMBOLIZED_STACK_WRITER_H_
