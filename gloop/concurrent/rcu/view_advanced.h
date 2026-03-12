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

#ifndef THIRD_PARTY_GLOOP_CONCURRENT_RCU_VIEW_ADVANCED_H_
#define THIRD_PARTY_GLOOP_CONCURRENT_RCU_VIEW_ADVANCED_H_

// Some tricks with View that are occasionally useful, but
// distracting, complex, or otherwise not worth exposing in the normal API.

namespace rcu {

// Wait for all previous View::Update calls to be cleaned up. More precisely:
// each call to View::Update arranges that at a safe time, the old version of
// the protected data is deleted. If a View::Update happens-before calling this,
// when we return, that old version has been deleted.
//
// It should go without saying that calling this from a thread that
// holds a Snapshot to those old versions is a very bad idea. In
// general, this is a risky function to call (prone to deadlock);
// avoid if possible outside e.g. tests.
void CleanUpAllViews();

}  // namespace rcu

#endif  // THIRD_PARTY_GLOOP_CONCURRENT_RCU_VIEW_ADVANCED_H_
