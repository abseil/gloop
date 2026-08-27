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

// It has the list of annotations which are used by XRay to store
// data during profiling for use during analysis. When not profiling, they
// are just a call and a return (to incur as low an overhead as possible).

#ifndef THIRD_PARTY_GLOOP_BASE_XRAY_TRACING_ANNOTATIONS_H_
#define THIRD_PARTY_GLOOP_BASE_XRAY_TRACING_ANNOTATIONS_H_

namespace base {
namespace xray {

// We use these EventId constants to differentiate events that pertain to RPC
// and context-related events that XRay tracks.
enum EventId {
  kRpcStart = 0,
  kRpcStop = 1,
  kRpcUnref = 2,
  kContextChange = 3,
};

}  // namespace xray
}  // namespace base

#if defined(GOOGLE_XRAY_EXPERIMENTAL) || defined(GOOGLE_XRAY_STABLE)
// These macros use the clang intrinsic __xray_customevent(...) which gets
// lowered into nothing when not building with XRay instrumentation (clang's
// -fxray-instrument flag is not specified). When built with XRay
// instrumentation, calls to __xray_customevent(...) are lowered into an XRay
// custom event sled that when enabled at runtime will invoke the custom event
// handler.
//
// More details on the mechanics of this are documented in
// <link>.
//
// When being built with GCC in diagnostics only mode, we should define the
// __xray_customevent function that's a Clang-only intrinsic to be a no-op. This
// is only a work-around until GCC is no longer used for diagnostics in -c opt
// mode, as part of <link>.
#if defined(__GNUC__) && !defined(__clang__)
inline void __xray_customevent(...) {}
#endif

// The annotation to capture information from the current thread's
// TraceContext when it changes.
#define CAPTURE_TRACECONTEXT_CHANGE_ANNOTATION(unused)                 \
  do {                                                                 \
    static constexpr int kContextChange =                              \
        ::base::xray::EventId::kContextChange;                         \
    __xray_customevent(reinterpret_cast<const char*>(&kContextChange), \
                       sizeof(kContextChange));                        \
  } while (false)

// Annotation to capture when a RPC starts.
#define XRAY_CAPTURE_RPC_START(unused)                                 \
  do {                                                                 \
    static constexpr int kRpcStart = ::base::xray::EventId::kRpcStart; \
    __xray_customevent(reinterpret_cast<const char*>(&kRpcStart),      \
                       sizeof(kRpcStart));                             \
  } while (false)

// Annotation to capture when a RPC stops.
#define XRAY_CAPTURE_RPC_STOP(unused)                                \
  do {                                                               \
    static constexpr int kRpcStop = ::base::xray::EventId::kRpcStop; \
    __xray_customevent(reinterpret_cast<const char*>(&kRpcStop),     \
                       sizeof(kRpcStop));                            \
  } while (false)

// Annotation to capture when the tracer object is unref'ed.
#define XRAY_CAPTURE_RPC_UNREF(unused)                                 \
  do {                                                                 \
    static constexpr int kRpcUnref = ::base::xray::EventId::kRpcUnref; \
    __xray_customevent(reinterpret_cast<const char*>(&kRpcUnref),      \
                       sizeof(kRpcUnref));                             \
  } while (false)

#else

#define CAPTURE_TRACECONTEXT_CHANGE_ANNOTATION(unused) \
  do {                                                 \
  } while (false)
#define XRAY_CAPTURE_RPC_START(unused) \
  do {                                 \
  } while (false)
#define XRAY_CAPTURE_RPC_STOP(unused) \
  do {                                \
  } while (false)
#define XRAY_CAPTURE_RPC_UNREF(unused) \
  do {                                 \
  } while (false)

#endif  // GOOGLE_XRAY_EXPERIMENTAL || GOOGLE_XRAY_STABLE

#endif  // THIRD_PARTY_GLOOP_BASE_XRAY_TRACING_ANNOTATIONS_H_
