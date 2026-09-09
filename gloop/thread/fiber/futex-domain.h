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

// An implementation of the Domain interface using native threads to host
// assigned execution and Futex interfaces to co-ordinate domain
// operations.
//
// FutexDomains are available on Linux, and are faster if FUTEX_SWAP
// is available as a kernel Futex command.

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_FUTEX_DOMAIN_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_FUTEX_DOMAIN_H_

#include "absl/strings/string_view.h"
#include "gloop/base/scheduling/domain.h"

namespace thread {

// Returns whether FutexDomains are supported by the current environment.
// This is the only FutexDomain specific API.  All other interfaces are
// equivalent to their Domain counterparts.
bool FutexDomainAvailable();

// Allocates a new Domain, implemented using Futex interfaces.
base::scheduling::Domain* NewFutexDomain(absl::string_view name_prefix,
                                         int max_concurrency);

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_FUTEX_DOMAIN_H_
