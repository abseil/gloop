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

#ifndef THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_FORMAT_TO_BUFFER_SINK_H_
#define THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_FORMAT_TO_BUFFER_SINK_H_

#include <algorithm>
#include <cstddef>
#include <cstring>

#include "absl/base/nullability.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace perftools::tracing {

// `FormatToBufferSink` is an `absl` sink implementation for use with the
// `absl::Format()` function, capturing formatted output into a caller owned
// buffer of a given size. While its intended use is Tracer / Trace Buffer
// implementations, it's a candidate for being a general purpose class.
class FormatToBufferSink {
 public:
  // Creates a sink that tracks the formatted size only.
  constexpr FormatToBufferSink() = default;

  // Creates a sink accepting up to `span.size()` bytes into the buffer
  // referenced by `span`. The buffer referenced by `span` must outlive this
  // instance. `span` is allowed to be empty in which case this sink only
  // tracks the formatted size which can be obtained through `total_size()`.
  explicit FormatToBufferSink(absl::Span<char> span) noexcept : span_(span) {}

  // FormatToBufferSink is not copyable or assignable.
  FormatToBufferSink(const FormatToBufferSink&) = delete;
  FormatToBufferSink& operator=(const FormatToBufferSink&) = delete;

  // Returns the total formatted size, i.e., the required 'limit` value
  // to avoid the output being truncated.
  size_t total_size() const { return size_; }

  // Appends `s` to this sink up to the available span limit.
  void Append(absl::string_view s);

 private:
  friend void AbslFormatFlush(FormatToBufferSink* absl_nonnull sink,
                              absl::string_view v) {
    sink->Append(v);
  }

  size_t size_ = 0;
  const absl::Span<char> span_;
};

inline void FormatToBufferSink::Append(absl::string_view s) {
  if (!s.empty() && size_ < span_.size()) {
    const size_t available = span_.size() - size_;
    memcpy(span_.data() + size_, s.data(), std::min(s.length(), available));
  }
  size_ += s.length();
}

}  // namespace perftools::tracing

#endif  // THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_FORMAT_TO_BUFFER_SINK_H_
