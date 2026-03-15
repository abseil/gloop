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

#ifndef THIRD_PARTY_GLOOP_BASE_CONTEXT_ORIGIN_H_
#define THIRD_PARTY_GLOOP_BASE_CONTEXT_ORIGIN_H_

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "gloop/util/atomic_danger/refcount.h"

namespace base {

// Stores a trace captured when a context is created.
//
// Because instances of this class are stored inside TraceContext and copied
// along with it, the implementation minimizes the size of each instance by
// using reference-counting.
//
// Usage example:
//
//   TraceContext tc(...);
//   std::vector<void*> stack_trace(32);
//   int size = absl::GetStackTrace(stack_trace.data(), stack_trace.size(), 1);
//   stack_trace.resize(size);
//   tc.set_origin(base::ContextOrigin{stack_trace});
//   perftools::tracing::TraceContextSwitcher tcs(tc);
//
// Let the TraceContext propagate normally and somewhere else check the origin:
//
//   auto origin = TraceContext::Current()->origin();
//   if (origin.has_value())
//     LOG(INFO) << util::SymbolizeStackTraceAsString(origin->data(),
//                                                    origin->size());
class ABSL_ATTRIBUTE_TRIVIAL_ABI ContextOrigin final {
 public:
  constexpr ContextOrigin() = default;

  explicit ContextOrigin(std::vector<void*> stack_trace) {
    stack_trace_ref_ = new StackTraceRef();
    stack_trace_ref_->stack_trace = std::move(stack_trace);
  }

  ContextOrigin(const ContextOrigin& other)
      : stack_trace_ref_(other.stack_trace_ref_) {
    if (stack_trace_ref_ != nullptr) stack_trace_ref_->Ref();
  }

  ContextOrigin(ContextOrigin&& other) noexcept {
    stack_trace_ref_ = other.stack_trace_ref_;
    other.stack_trace_ref_ = nullptr;
  }

  ContextOrigin& operator=(const ContextOrigin& other) {
    if (this->stack_trace_ref_ == other.stack_trace_ref_) return *this;
    if (stack_trace_ref_ != nullptr) stack_trace_ref_->Unref();
    stack_trace_ref_ = other.stack_trace_ref_;
    if (other.stack_trace_ref_ != nullptr) other.stack_trace_ref_->Ref();
    return *this;
  }

  ContextOrigin& operator=(ContextOrigin&& other) noexcept {
    if (stack_trace_ref_ != nullptr) stack_trace_ref_->Unref();
    stack_trace_ref_ = other.stack_trace_ref_;
    other.stack_trace_ref_ = nullptr;
    return *this;
  }

  ~ContextOrigin() {
    if (stack_trace_ref_ != nullptr) stack_trace_ref_->Unref();
  }

  std::optional<std::vector<void*>> stack_trace() const {
    if (stack_trace_ref_ != nullptr) {
      return stack_trace_ref_->stack_trace;
    }
    return std::nullopt;
  }

 private:
  struct StackTraceRef {
    void Ref() { refcount.Inc(); }
    void Unref() {
      if (refcount.Dec()) delete this;
    }

    atomic_danger::RefCount<intptr_t> refcount;
    std::vector<void*> stack_trace;
  };

  StackTraceRef* stack_trace_ref_ = nullptr;
};

}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_CONTEXT_ORIGIN_H_
