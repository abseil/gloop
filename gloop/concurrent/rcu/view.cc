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

#include "gloop/concurrent/rcu/view.h"

#include "gloop/concurrent/rcu/global_domain.h"

namespace rcu {

void CleanUpAllViews() {
  // When Synchronize returns, all callbacks on that Domain have run, so
  // any old View data has been cleaned up.
  internal::GlobalDomain::d.Synchronize();
}

}  // namespace rcu
