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

// IWYU pragma: private, include "base/callback.h"
// IWYU pragma: friend base/callback.*

#ifndef THIRD_PARTY_GLOOP_BASE_CALLBACK_TYPES_H_
#define THIRD_PARTY_GLOOP_BASE_CALLBACK_TYPES_H_

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "gloop/base/context.h"
#include "gloop/base/tracecontext.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/trace_source_location.h"

namespace base {
namespace internal {

// Base class for all callback types, exists to centralize the definition of
// virtual methods to easily identify them all for removal.
class
#ifndef SWIG
    ABSL_DEPRECATED(
        "Access to non-run methods on callbacks is deprecated. Please migrate "
        "your code to use absl::AnyInvocable instead.")
#endif
        CallbackBase {
 public:
  virtual ~CallbackBase() = default;
  virtual bool IsRepeatable() const { return false; }

#ifndef SWIG
  ABSL_DEPRECATED("Access to legacy callback captured context is deprecated.")
#endif
  ::base::Context* context_ptr() { return &context_; }

 protected:
  CallbackBase() : context_() {}
  // The `DefaultInitType` and `ThreadInitType` constructors must have the same
  // signature as they are called from a template `ToCallback()` implementation.
  // The label is only used in the (possibly traced) thread context constructor.
  explicit CallbackBase(::base::Context::DefaultInitType /*initializer*/,
                        perftools::tracing::StringRef = /*unused*/ {})
      : context_() {}
  explicit CallbackBase(::base::Context::ThreadInitType initializer,
                        perftools::tracing::StringRef label =
                            perftools::tracing::TraceSourceLocation::current())
      : context_(initializer, label) {}
  ::base::Context context_;

 private:
  virtual void UnusedKeyMethod();  // <link>
};

}  // namespace internal
}  // namespace base

class Closure : public ::base::internal::CallbackBase {
 public:
  virtual void Run() = 0;

 protected:
  using ::base::internal::CallbackBase::CallbackBase;
};

template <class A1>
class Callback1 : public ::base::internal::CallbackBase {
 public:
  virtual void Run(A1) = 0;

 protected:
  using ::base::internal::CallbackBase::CallbackBase;
};

#endif  // THIRD_PARTY_GLOOP_BASE_CALLBACK_TYPES_H_
