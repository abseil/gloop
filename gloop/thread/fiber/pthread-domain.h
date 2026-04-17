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

// An implementation of the Domain interface using native threads to host
// assigned execution and posix semaphores to co-ordinate domain operations.
//
// kWorkItem schedulables will always have a 1:1 correspondence with a thread
// owned by this domain for their lifetime.  A PthreadDomain may be used to
// back Fibers.
//
// PthreadDomains are portable; they should work on any system where POSIX
// semaphores are supported.
//
// See: https://github.com/abseil/gloop/tree/main/gloop/base/scheduling/domain.h

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_PTHREAD_DOMAIN_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_PTHREAD_DOMAIN_H_

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"
#include "gloop/base/scheduling/domain.h"

namespace thread {

// Allocates a new Domain, implemented using POSIX semaphores.
ABSL_DEPRECATED("Use NewFutexDomain instead.")
base::scheduling::Domain* NewPthreadDomain(absl::string_view name_prefix,
                                           int max_concurrency);

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_PTHREAD_DOMAIN_H_
