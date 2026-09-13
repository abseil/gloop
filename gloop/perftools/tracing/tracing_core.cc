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

#include "gloop/perftools/tracing/tracing_core.h"

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/tracing.h"
#include "gloop/perftools/tracing/string_label.h"

#if ABSL_HAVE_ATTRIBUTE_WEAK

namespace perftools::tracing::core {

namespace {

using ObjectKind = absl::base_internal::ObjectKind;  // NOLINT

inline StringRef ToString(ObjectKind kind) {
  switch (kind) {
    case ObjectKind::kBlockingCounter:
      return "absl::BlockingCounter";
    case ObjectKind::kNotification:
      return "absl::Notification";
    case ObjectKind::kUnknown:
    default:
      return "absl::Unknown";
  }
}

}  // namespace

extern "C" {

void ABSL_INTERNAL_C_SYMBOL(AbslInternalTraceWait)(const void* object,
                                                   ObjectKind kind) {
  TraceWait(object, ToString(kind));
}

void ABSL_INTERNAL_C_SYMBOL(AbslInternalTraceContinue)(const void* object,
                                                       ObjectKind kind) {
  TraceContinue(object);
}

void ABSL_INTERNAL_C_SYMBOL(AbslInternalTraceSignal)(const void* object,
                                                     ObjectKind kind) {
  TraceSignal(object, ToString(kind));
}

void ABSL_INTERNAL_C_SYMBOL(AbslInternalTraceObserved)(const void* object,
                                                       ObjectKind kind) {
  TraceObserved(object, ToString(kind));
}

}  // extern "C"

}  // namespace perftools::tracing::core

#endif  // ABSL_HAVE_ATTRIBUTE_WEAK
