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

#ifndef THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_TEST_ONLY_ACCESS_H_
#define THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_TEST_ONLY_ACCESS_H_

namespace perftools::tracing::testing {

// This class provides test-only access to resources protected by the passkey
// idiom (see https://abseil.io/tips/134).
struct TestOnlyAccess {
  // Returns a newly constructed instance of type T. The assumption is that T
  // has a private constructor preventing it from being constructed directly but
  // TestOnlyAccess is a friend of T so TestOnlyAccess::Create<T>() is valid.
  template <typename T>
  static constexpr T Create() {
    return T();
  }
};

}  // namespace perftools::tracing::testing

#endif  // THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_TEST_ONLY_ACCESS_H_
