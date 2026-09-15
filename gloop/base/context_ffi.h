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

#ifndef THIRD_PARTY_GLOOP_BASE_CONTEXT_FFI_H_
#define THIRD_PARTY_GLOOP_BASE_CONTEXT_FFI_H_

#include "absl/base/attributes.h"
#include "base/with_lifetime_bound_context.h"
#include "gloop/base/context.h"

namespace base::internal::context_rs {

class WithLifetimeBoundContextForRust {
 public:
  explicit WithLifetimeBoundContextForRust(
      Context& context ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : with_(context) {}

 private:
  ::base::subtle::WithLifetimeBoundContext with_;
};

}  // namespace base::internal::context_rs

#endif  // THIRD_PARTY_GLOOP_BASE_CONTEXT_FFI_H_
