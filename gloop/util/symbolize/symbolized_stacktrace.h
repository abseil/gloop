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

// Functions for obtaining symbolized stack trace.  This
// works by using `absl::GetStackTrace()` in
// https://github.com/abseil/abseil-cpp/tree/master/absl/debugging/stacktrace.h
// and `SymbolMap` in
// //gloop/util/symbolize/symbolize.h.
//
// This library is particularly useful for debugging and
// understanding a program.  It's not safe to use this library
// as a program degrades (ex. during SEGV signal handling).
//

#ifndef THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_SYMBOLIZED_STACKTRACE_H__
#define THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_SYMBOLIZED_STACKTRACE_H__

#include <string>
#include <vector>

namespace util {
// Get the symbolized stack trace at most "max_depth" frames
// while skipping "skip_count" frames, as a vector of string.
//
// Example:
//      int main() { foo(); }
//      void foo() { bar(); }
//      void bar() {
//        vector<string> stack;
//        util::GetSymbolizedStackTrace(&stack, 10, 1);
//      }
//
// It will produce "stack" as follows:
//                                         // bar skipped due to skip_count=1
//      stack[0] "0xb7f318b2: void foo()"  // demangled if possible
//      stack[1] "0x0804c7d9: main"        // "main" isn't a C++ symbol
//
// (Actually, there may be a few more entries after "main" to account for
// startup procedures.)
//
// Symbol names are demangled if possible (requires GCC 3.4 or newer).
// If a symbol name isn't found, "(unknown)" will be used.
//
void GetSymbolizedStackTrace(std::vector<std::string>* result, int max_depth,
                             int skip_count, bool demangle = true);

// Get the symbolized stack trace at most "max_depth" frames, skipping
// innermost "skip_count" frames, as a string. All symbol names will be
// simply connected with "\n". Useful for simple debug output.
//
// Example:
//     LOG(INFO) << "@@stacktrace\n"
//               << util::GetSymbolizedStackTraceAsString(10);
//
std::string GetSymbolizedStackTraceAsString(int max_depth, int skip_count = 0,
                                            bool demangle = true);

// Convert a previously obtained stack trace into symbolized form. Use
// `absl::GetStackTrace`
// (https://github.com/abseil/abseil-cpp/tree/master/absl/debugging/stacktrace.h)
// to get the trace and depth.
void SymbolizeStackTrace(void* const* stack, int depth,
                         std::vector<std::string>* result,
                         bool demangle = true);

// Convert a previously obtained stack trace into a symbolized string.
// Return always contains a trailing newline (even when depth==0).
// Use `absl::GetStackTrace`
// (https://github.com/abseil/abseil-cpp/tree/master/absl/debugging/stacktrace.h)
// to get the trace and depth.
std::string SymbolizeStackTraceAsString(void* const* stack, int depth,
                                        bool demangle = true);

// A faster replacement for ::CurrentStackTrace. See b/145200613.
// Return the current stack trace as a string (on multiple lines, beginning with
// "Stack trace:\n")
// Differences from ::CurrentStackTrace:
// 1. Full __cxa_demangle is used, therefore templates are expanded.
// 2. In debug builds, file/line info is not provided for tests or binaries that
// depend on signalsafe_addr2line_installer.
std::string CurrentStackTrace(bool with_url = false);

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_SYMBOLIZED_STACKTRACE_H__
