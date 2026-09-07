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

// Support for collecting useful information when crashing.

#ifndef THIRD_PARTY_GLOOP_BASE_CRASH_H_
#define THIRD_PARTY_GLOOP_BASE_CRASH_H_

#include "gloop/base/config.h"

#if BASE_HAVE_CRASHREASON

class TraceContext;

#include "absl/strings/string_view.h"

namespace base {

struct CrashReason {
  absl::string_view filename;
  int line_number = 0;
  absl::string_view message;

  // We'll also store a bit of stack trace context at the time of crash as
  // it may not be available later on.
  void* stack[128] = {nullptr};
  int depth = 0;

  // We'll try to store some trace information if it's available - this should
  // reflect information from TraceContext::Current()->tracer()->ToString().
  // This field should probably not be set from within a signal handler or
  // low-level code unless absolutely safe to do so.
  char trace_info[512] = {'\0'};

  // Trace context override to use (instead of the current trace context) when
  // reporting the crash.
  const TraceContext* tc = nullptr;
};

// Stores "reason" as an explanation for why the process is about to
// crash.  The reason and its contents must remain live for the life
// of the process.  Only the first reason is kept.
void SetCrashReason(const CrashReason* reason);

// Returns first reason passed to SetCrashReason(), or null.
const CrashReason* GetCrashReason();

}  // namespace base

#endif  // BASE_HAVE_CRASHREASON

#endif  // THIRD_PARTY_GLOOP_BASE_CRASH_H_
