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

// Interfaces providing access to a program's default domains.  Domains are
// groupings of co-operatively scheduled threads which may be used to
// efficiently host Fibers.
//
// CAUTION: Users should rarely need to interact with domains.  Prefer higher
// level interfaces such as Fibers.

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_INIT_DOMAIN_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_INIT_DOMAIN_H_

#include <memory>
#include <string>

#include "absl/flags/declare.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/scheduler.h"

ABSL_DECLARE_FLAG(bool, fibers_experimental_use_swg_domain);

namespace thread {

// Returns the default execution domain.  Should host most program execution.
//
// WARNING: Subject to change.  The domain topology has not yet been finalized.
base::scheduling::Domain* DefaultDomain();

struct CreateCustomDomainOptions {
  // 'name' is used to construct thread names for this domain's fibers. Empty
  // name and name "default" is reserved for DefaultDomain above.
  std::string name;
  // max_concurrency defines concurrency of created fiber. Negative values
  // behaves the same as DefaultDomain() above w.r.t. setting up concurrency.
  int max_concurrency = -1;
};

// Creates new domain with same settings as default domain, but with given name
// and max_concurrency. Returned domain is owned by the caller. See Domain's
// documentation on synchronization aspects of Domain's deletion.
//
// Using this function might be preferable to constructing specific domain
// implementation kind (e.g. NewSwitchToDomain). Creating custom domain could be
// useful e.g. in order to construct custom root scheduler instance.
std::unique_ptr<base::scheduling::Domain> CreateCustomDomain(
    const CreateCustomDomainOptions& options);

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_INIT_DOMAIN_H_
