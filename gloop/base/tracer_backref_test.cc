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

#include <string>

#include "absl/base/internal/raw_logging.h"
#include "absl/strings/string_view.h"
#include "absl/types/source_location.h"
#include "gloop/base/tracecontext.h"
#include "gloop/base/tracer.h"
#include "gloop/base/tracing_types.h"

namespace {

class BackrefTestTracer : public base::Tracer {
 public:
  using base::Tracer::kInitiatorNotSetByParent;
  explicit BackrefTestTracer(const TraceContext& tc) : base::Tracer(tc) {}
  ~BackrefTestTracer() override = default;

  void Attach(base::TraceEntrySource*) override {}
  void Detach(base::TraceEntrySource*, bool) override {}
  void EmitTraceEntrySources(base::TraceEntrySink*, bool) const override {}
  void SetMaxBytesToKeep(int) override {}
  void SetMaxBytesToKeepPerEntry(int) override {}
  std::string ToString() const override { return std::string(); }
  std::string name() const override { return std::string(); }
  void AttachTraceConsumer(base::TraceConsumer*) override {}

 protected:
  void ChannelPrintFormattedStringImpl(perftools::tracing::channels::ChannelID,
                                       base::TraceStringFormatter,
                                       absl::SourceLocation) override {}
  void ChannelPrintLiteralImpl(perftools::tracing::channels::ChannelID,
                               const char*, absl::SourceLocation) override {}
  void ChannelPrintStringViewImpl(perftools::tracing::channels::ChannelID,
                                  absl::string_view,
                                  absl::SourceLocation) override {}
  perftools::tracing::TraceBuffer* GetAnnotationMap() override {
    return nullptr;
  }
  void NotifyTraceConsumers() override {}
};

}  // namespace

int main() {
  ABSL_RAW_CHECK(base::GetNoopTracer() != nullptr,
                 "GetNoopTracer() returned null");
#if BASE_HAVE_TRACECONTEXT
  TraceContext tc(1234, 5678, 9012, TraceContext::kTraceMaskRPCTracingOn);
  BackrefTestTracer tracer(tc);
  ABSL_RAW_CHECK(tracer.span_id() == 1234, "span_id mismatch");
  ABSL_RAW_CHECK(tracer.parent_span_id() == 5678, "parent_span_id mismatch");
  ABSL_RAW_CHECK(tracer.trace_id() == 9012, "trace_id mismatch");
  ABSL_RAW_CHECK(tracer.is_traced(), "expected traced state");
  ABSL_RAW_CHECK(
      tracer.initiator_id() == BackrefTestTracer::kInitiatorNotSetByParent,
      "expected kInitiatorNotSetByParent");

  TRACELITERAL("literal");
  TRACESTRING("string");
  TRACEPRINTF("value=%d", 1);
#endif
  return 0;
}
