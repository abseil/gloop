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

#ifndef THIRD_PARTY_GLOOP_CONCURRENT_BARRIER_BARRIER_INTERNAL_H_
#define THIRD_PARTY_GLOOP_CONCURRENT_BARRIER_BARRIER_INTERNAL_H_

#include <atomic>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/log/check.h"

namespace concurrent {
namespace internal {

// Functor is an optionally cv qualified, lvalue or non-reference functor type,
// which is callable with no arguments through a temporary.
template <typename Functor>
struct FixedBarrierState {
  std::atomic<int> ref_count;
  typename std::decay<Functor>::type functor;

  FixedBarrierState(int n, Functor&& functor)
      : ref_count(n), functor(std::forward<Functor>(functor)) {
    DCHECK_GT(n, 0);
  }

  void Decrement() {
    int v = ref_count.fetch_sub(1);
    DCHECK_GT(v, 0);
    if (v == 1) {
      std::move(functor)();
    }
  }
};

template <typename Functor>
class FixedBarrier {
 public:
  explicit FixedBarrier(int n, Functor&& functor) {
    DCHECK_GE(n, 0);
    if (n == 0) {
      // Make a copy of the functor and call it via non-const rvalue in order
      // to provide consistent behaviour for n == 0 and n > 0.
      auto f = std::forward<Functor>(functor);
      std::move(f)();
    } else {
      state_ = std::make_shared<FixedBarrierState<Functor>>(
          n, std::forward<Functor>(functor));
    }
  }

  // Prerequisite: N > 0.
  void operator()() const {
    DCHECK(state_);
    state_->Decrement();
  }

 private:
  std::shared_ptr<FixedBarrierState<Functor>> state_;
};

}  // namespace internal
}  // namespace concurrent

#endif  // THIRD_PARTY_GLOOP_CONCURRENT_BARRIER_BARRIER_INTERNAL_H_
