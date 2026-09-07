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

#include "gloop/util/symbolize/symbolized_stacktrace.h"

#include <stdint.h>
#include <stdio.h>

#include <cstdint>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/debugging/stacktrace.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "gloop/util/symbolize/symbolize.h"

namespace util {

namespace {
void PerSymbolizedStackFrame(
    void* const* stack, int depth, bool demangle,
    absl::FunctionRef<void(uint64_t, absl::string_view)> on_symbol) {
  const SymbolMap& symbol_map = SymbolMap::GetCached();
  for (int i = 0; i < depth; ++i) {
    // Use uintptr_t to convert void * to uint64_t safely.
    const uint64_t pc = reinterpret_cast<uintptr_t>(stack[i]);
    // The pc is a return address which points to the next instruction
    // after the function call.  If the last instruction in a function
    // is a call (to noreturn routine), the pc may point to code in the next
    // function, or into no-man's land.  See also comments in
    // DumpPCAndFrameSizeAndSymbol in base/examine_stack.cc.
    std::string symbol;
    if (demangle) {
      symbol = symbol_map.GetDemangledSymbolAtPosition(pc - 1);
    } else if (const char* mangled_symbol =
                   symbol_map.GetSymbolAtPosition(pc - 1)) {
      symbol = mangled_symbol;
    }
    if (symbol.empty()) {
      symbol = "(unknown)";
    }
    on_symbol(pc, symbol);
  }
}

ABSL_ATTRIBUTE_ALWAYS_INLINE
std::vector<void*> GetStackTraceInternal(int max_depth, int skip_count) {
  std::vector<void*> result(max_depth);
  int depth = absl::GetStackTrace(result.data(), max_depth, skip_count);
  result.resize(depth);
  return result;
}
}  // namespace

std::string SymbolizeStackTraceAsString(void* const* stack, int depth,
                                        bool demangle) {
  std::string ret;
  // Assume average resulting line length is 80.
  ret.reserve(depth * 80);
  PerSymbolizedStackFrame(stack, depth, demangle,
                          [&ret](uint64_t pc, absl::string_view symbol_name) {
                            absl::StrAppendFormat(&ret, "0x%08x: %s\n", pc,
                                                  symbol_name);
                          });
  return ret;
}

void SymbolizeStackTrace(void* const* stack, int depth,
                         std::vector<std::string>* result, bool demangle) {
  result->reserve(result->size() + depth);
  PerSymbolizedStackFrame(
      stack, depth, demangle,
      [result](uint64_t pc, absl::string_view symbol_name) {
        result->push_back(absl::StrFormat("0x%08x: %s", pc, symbol_name));
      });
}

ABSL_ATTRIBUTE_NOINLINE  // Inlining interferes with skip_count.
    std::string GetSymbolizedStackTraceAsString(int max_depth, int skip_count,
                                                bool demangle) {
  // +1 for skipping this function itself.
  const auto stack = GetStackTraceInternal(max_depth, skip_count + 1);
  return SymbolizeStackTraceAsString(stack.data(), stack.size(), demangle);
}

ABSL_ATTRIBUTE_NOINLINE  // Inlining interferes with skip_count.
    void GetSymbolizedStackTrace(std::vector<std::string>* result,
                                 int max_depth, int skip_count, bool demangle) {
  // +1 for skipping this function itself.
  const auto stack = GetStackTraceInternal(max_depth, skip_count + 1);
  SymbolizeStackTrace(stack.data(), stack.size(), result, demangle);
}

ABSL_ATTRIBUTE_NOINLINE  // Inlining interferes with skip_count.
    std::string CurrentStackTrace(bool with_url) {
  static constexpr int kMaxDepth = 1024;  // Arbitrary large enough limit.
  std::string ret = "Stack trace:\n";
  // +1 for skipping this function itself.
  const auto stack = GetStackTraceInternal(kMaxDepth, /*skip_count=*/1);
  PerSymbolizedStackFrame(
      stack.data(), stack.size(), true,
      [&ret](uint64_t pc, absl::string_view symbol_name) {
        // The format should match the one used in
        // //gloop/base/examine_stack.cc (DumpPCAndSymbol).
        constexpr unsigned pointer_field_width = 2 + 2 * sizeof(void*);
        absl::StrAppendFormat(&ret, "    @ %*p  %s\n", pointer_field_width,
                              reinterpret_cast<void*>(pc), symbol_name);
      });
  return ret;
}

}  // namespace util
