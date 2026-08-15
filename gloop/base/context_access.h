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

#ifndef THIRD_PARTY_GLOOP_BASE_CONTEXT_ACCESS_H_
#define THIRD_PARTY_GLOOP_BASE_CONTEXT_ACCESS_H_

#include <cstdint>

// This file contains accessors to Context state intended only in
// implementations of individual Context components (TraceContext, etc.)

class CensusHandle;
class CurrentTraceContext;
class TraceContext;
class TracerTest;

namespace perftools {
namespace tracing {
template <typename T>
class TraceSpan;
class WithTraceEventListener;
class LinkContextsImpl;
}  // namespace tracing
}  // namespace perftools

namespace stats_census {
class CensusHandleManager;
}  // namespace stats_census

namespace security::context {
class WithSecurityContext;
}  // namespace security::context

namespace privacy::context {
class DdtBaseAccess;
}  // namespace privacy::context

namespace base {

class Context;
class WithContext;
class WithTraceContext;

namespace subtle {
class WithLifetimeBoundContext;
}  // namespace subtle

class ContextAccess;
ContextAccess ContextAccessForTesting();

void SwapCurrentContext(Context* c);
void RestoreCurrentContext(Context* c);
void SwapCurrentTraceContext(TraceContext* tc);
void RestoreCurrentTraceContextFrom(TraceContext* tc);

namespace internal {

class MutableCurrentContext {
 private:
  // Returns the current mutable trace context.
  static TraceContext* MutableCurrentTrace();

  friend class ::CurrentTraceContext;
  friend class ::TraceContext;
  friend class ::TracerTest;

  template <typename T>
  friend class ::perftools::tracing::TraceSpan;
};

class SharedContext;

}  // namespace internal

// Only stats_census implementation should be using this.
class CensusAccess {
 private:
  // Returns a reference to the current Context's handle.
  // Take special care to respect the lifetime of the returned reference -- it
  // is a pointer into thread local memory. It should not be stored or used in
  // other threads.
  static const CensusHandle& GetCurrentHandle();

  // Swaps the current Context's handle with the one provided.
  static void SwapCurrentHandle(CensusHandle* handle);

  // Returns a const reference to the CensusHandle in the given Context.
  static const CensusHandle& GetHandleFromContext(const Context& c);

  friend class ContextTest;
  friend class ::stats_census::CensusHandleManager;
};

class SharedContextAccess {
 private:
  // Passkey token access for direct manipulation of SharedContext within the
  // base::Context. This facilitates the implementation of other public-facing
  // RAII-style setters for SharedContext sub-fields (e.g.,
  // WithSecurityContext).
  friend class Context;
  friend class ContextBuilder;
  friend class security::context::WithSecurityContext;
  friend class PrivacyContextAccess;

  // For refcounting testing.
  friend class ContextTest;
  // Returns true if the SharedContext is non-null/non-default and is only
  // referenced once.
  static bool RefCountIsOneForDebugging(const base::Context& context);
};

// Only privacy_context implementation should be using this.
class PrivacyContextAccess {
 private:
  // Returns the captured snapshot of the RootScopedData address.
  // The PrivacyContextRsdAddress field is used to redundantly store the address
  // of the RootScopedData from the current CensusHandle. Please see
  // <link> and <link> for more
  // details. This will be replaced by a pointer to PrivacyContext in the
  // future.
  static uintptr_t GetPrivacyContextRsdAddress(const Context& context);

  // Sets the captured snapshot of the RootScopedData address. Only used to
  // optimize the setting of the saved RSD address in a mutable context during
  // context setup.
  // The PrivacyContextRsdAddress field is used to redundantly store the address
  // of the RootScopedData from the current CensusHandle. Please see
  // <link> and <link> for more
  // details. This will be replaced by a pointer to PrivacyContext in the
  // future.
  static void SetPrivacyContextRsdAddress(uintptr_t address, Context& context);

  friend class ContextTest;
  friend class privacy::context::DdtBaseAccess;
};

// Access token for `base::Context` and `TraceContext` limited access methods.
class ContextAccess {
 private:
  constexpr ContextAccess() noexcept;

  friend class Context;
  friend class WithContext;
  friend class WithTraceContext;
  friend class subtle::WithLifetimeBoundContext;
  friend class ContextTest;
  friend class ::TraceContext;
  friend class ::CurrentTraceContext;

  template <typename T>
  friend class ::perftools::tracing::TraceSpan;
  friend class ::perftools::tracing::WithTraceEventListener;
  friend class ::perftools::tracing::LinkContextsImpl;

  friend void SwapCurrentContext(Context* c);
  friend void RestoreCurrentContext(Context* c);
  friend void SwapCurrentTraceContext(TraceContext* tc);
  friend void RestoreCurrentTraceContextFrom(TraceContext* tc);

  friend ContextAccess ContextAccessForTesting();
};

constexpr ContextAccess::ContextAccess() noexcept = default;

}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_CONTEXT_ACCESS_H_
