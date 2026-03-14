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

// Copyright 2010 Google Inc. All Rights Reserved.

//
// Contains functions to dump the execution state (pc, stack frame,
// and malloc statistics) of a process.
//
// The functions in here are thread-safe unless specified otherwise,
// but they must be called after InitGoogle() (or the
// InitGoogleExceptChangeRootAndUser/ChangeRootAndUser pair).

#ifndef THIRD_PARTY_GLOOP_BASE_EXAMINE_STACK_H_
#define THIRD_PARTY_GLOOP_BASE_EXAMINE_STACK_H_

#include <cstdint>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/port.h"  // IWYU pragma: keep
#include "absl/debugging/internal/examine_stack.h"
#include "absl/flags/declare.h"
#include "absl/functional/function_ref.h"
#include "absl/log/log_streamer.h"
#include "absl/strings/string_view.h"

// Include all mappings from /proc/self/maps in a failure dump
ABSL_DECLARE_FLAG(bool, dump_all_maps_on_failure);

// Symbolize the stack trace in the tombstone
ABSL_DECLARE_FLAG(bool, symbolize_stacktrace);

// Skip the address map in the tombstone
ABSL_DECLARE_FLAG(bool, skip_address_map);

// Maximum number of stack frames to print on fatal signal.
ABSL_DECLARE_FLAG(int, dump_stack_frames_limit);

namespace base {

// Type of function used for printing in stack trace dumping, etc.
// We avoid closures to keep things simple.
using DebugWriter = absl::debugging_internal::OutputWriter;

// A few useful output writers:

// Prefer `DebugWriteToFile` with `stderr` as the argument unless you need
// async-signal-safety; `DebugWriteToStderr` may interleave its output with
// other data written to stderr.
void DebugWriteToStderr(const char* data, void* unused);

// `file` must point to a `FILE`.
void DebugWriteToFile(const char* data, void* file);

// `str` must point to a `string`.
void DebugWriteToString(const char* data, void* str);

// `os` must point to a `std::ostream`.
// Use `absl::LogStreamer` to stream into `LOG`.
void DebugWriteToStream(const char* data, void* os);

// Dump current stack trace omitting the topmost 'skip_count' stack frames.
void DumpStackTrace(int skip_count, DebugWriter* writer, void* writer_arg);

// Dump given pc and stack trace.
void DumpPCAndStackTrace(void* const pc, void* const stack[], int depth,
                         DebugWriter* writer, void* writer_arg);

// Convenient program-counter and stack dump wrapper for signal handlers.
void DumpPCAndStackTraceForSignalHandler(void* const uc, DebugWriter* writer,
                                         void* writer_arg);

// Return the current stack trace as a string (on multiple lines, beginning with
// "Stack trace:\n").
//
// This function is VERY slow, because it performs async-signal safe
// symbolization. But it can not itself be used in async-signal contexts,
// since it returns a string. Call ::util::CurrentStackTrace() from
// //gloop/util/symbolize/symbolized_stacktrace.h instead.
ABSL_DEPRECATED("Use ::util::CurrentStackTrace")
std::string CurrentStackTrace();

// A helper to save a stack trace and then symbolize and/or print it
// only when that is actually needed.
// One use is making error/crash messages more helpful with stack traces
// of some past events that contributed to the current error.
// It has value semantics: can be copied and assigned.
class SavedStackTrace {
 public:
  SavedStackTrace() : depth_(0) {}

  // skip_count gives the count of extra stack frames to skip;
  // the usual values would be 1 or 0.
  void CreateCurrent(int skip_count);

  void Reset() { depth_ = 0; }

  // Dump the stack trace via writer(_, arg).
  void Dump(DebugWriter* writer, void* writer_arg) const {
    DumpPCAndStackTrace(nullptr, stack_, depth_, writer, writer_arg);
  }

  int depth() const { return depth_; }
  void* const* stack() const { return stack_; }

 private:
  int depth_;
  void* stack_[32];
};

// Returns the program counter from signal context, null if unknown.
// vuc is a ucontext_t *. We use void* to avoid the use
// of ucontext_t on non-POSIX systems.
inline void* GetPC(void* const vuc) {
  return absl::debugging_internal::GetProgramCounter(vuc);
}

// Returns the stack pointer from signal context, null if unknown.
// vuc is a ucontext_t *.  We use void* to avoid the use
// of ucontext_t on non-POSIX systems.
uintptr_t GetSP(void* const vuc);

// Dump current address map.
void DumpAddressMap(DebugWriter* writer, void* writer_arg);

// Dump the register context as directed by writer.
void DumpRegisterContext(void* const uc, DebugWriter* writer, void* writer_arg);

// Dump the register context and invoke the provided callback for each register.
// Note: the `fn` must be async-signal safe.
#if defined(__linux__)
void DumpRegisterContext(
    void* vuc, absl::FunctionRef<void(absl::string_view, uintptr_t)> fn);
#endif  // defined(__linux__)

}  // namespace base

using base::CurrentStackTrace;                    // NOLINT
using base::DebugWriteToStderr;                   // NOLINT
using base::DebugWriteToString;                   // NOLINT
using base::DumpAddressMap;                       // NOLINT
using base::DumpPCAndStackTrace;                  // NOLINT
using base::DumpPCAndStackTraceForSignalHandler;  // NOLINT
using base::DumpRegisterContext;                  // NOLINT
using base::GetPC;                                // NOLINT
using base::GetSP;                                // NOLINT
using base::SavedStackTrace;                      // NOLINT
using DebugWriter = base::DebugWriter;            // NOLINT
using base::DumpStackTrace;                       // NOLINT

#endif  // THIRD_PARTY_GLOOP_BASE_EXAMINE_STACK_H_
