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

#include "gloop/thread/thread_name.h"

#include <string.h>

#if defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#endif
#if defined(__APPLE__)
#include <mach/thread_info.h>  // for MAXTHREADNAMESIZE
#endif
#if defined(__Fuchsia__)
#include <zircon/process.h>
#include <zircon/syscalls.h>
#endif

#include <cstddef>

#include "absl/base/dynamic_annotations.h"
#include "absl/log/log.h"

namespace thread {
namespace internal {

// Linux and Apple both have `pthread_setname_np()`, but the args are
// different ("_np" means non-portable).
#if defined(__linux__)

static int SetThreadNameNoTrunc(const char* name) {
  return pthread_setname_np(pthread_self(), name);
}
// Linux requires names (with nul) fit in 16 chars
const size_t kMaxThreadNameLen = 15;

#elif defined(__APPLE__)

static int SetThreadNameNoTrunc(const char* name) {
  // On Apple platforms, this function doesn't return an error code and it only
  // works on the current thread.
  pthread_setname_np(name);
  return 0;
}
const size_t kMaxThreadNameLen = MAXTHREADNAMESIZE - 1;

#elif defined(__Fuchsia__)

static int SetThreadNameNoTrunc(const char* name) {
  zx_status_t status = zx_object_set_property(zx_thread_self(), ZX_PROP_NAME,
                                              name, strlen(name));
  return status;
}
const size_t kMaxThreadNameLen = ZX_MAX_NAME_LEN - 1;

#else

static int SetThreadNameNoTrunc(const char* name) { return 0; }
const size_t kMaxThreadNameLen = 0;

#endif

void SetCurrentThreadName(const char* name) {
  char thread_name[kMaxThreadNameLen + 1];
  strncpy(thread_name, name, sizeof(thread_name));
  thread_name[sizeof(thread_name) - 1] = '\0';
  int res = SetThreadNameNoTrunc(thread_name);
  if (res != 0) {
    LOG_FIRST_N(INFO, 1) << "Can't set pthread names: name: \"" << name
                         << "\"; error: " << res;
  }

  // Pass full (unstripped) name to ANNOTATE_THREAD_NAME.
  // Intentionally called after pthread_setname_np so it overrides
  // the stripped name registered via pthread_setname_np.
  ABSL_ANNOTATE_THREAD_NAME(name);
}

}  // namespace internal
}  // namespace thread
