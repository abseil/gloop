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

#include "gloop/strings/cord_builder.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/strings/cord.h"
#include "absl/strings/cord_buffer.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace strings {

CordBuilder::CordBuilder(CordBuilder&& rhs) noexcept
    : cord_(std::move(rhs.cord_)),
      buffer_(std::move(rhs.buffer_)),
      state_(rhs.state_),
      size_hint_(rhs.size_hint_) {
  rhs.cord_.Clear();
  rhs.state_ = kEmpty;
  rhs.size_hint_ = 0;
}

CordBuilder& CordBuilder::operator=(CordBuilder&& rhs) noexcept {
  cord_ = std::move(rhs.cord_);
  buffer_ = std::move(rhs.buffer_);
  state_ = rhs.state_;
  size_hint_ = rhs.size_hint_;
  rhs.cord_.Clear();
  rhs.state_ = kEmpty;
  rhs.size_hint_ = 0;
  return *this;
}

absl::Span<char> CordBuilder::ExpandBuffer(size_t n) {
  absl::Span<char> span = buffer_.available_up_to(n);
  assert(!span.empty());
  buffer_.IncreaseLengthBy(span.size());
  size_hint_ -= (std::min)(span.size(), size_hint_);
  state_ = buffer_.length() < buffer_.capacity() ? kPartial : kFull;
  return span;
}

void CordBuilder::MaybeNewBuffer(size_t n) {
  switch (state_) {
    case kFull:
      cord_.Append(std::move(buffer_));
      ABSL_FALLTHROUGH_INTENDED;
    case kEmpty:
      buffer_ = absl::CordBuffer::CreateWithCustomLimit(block_size_, n);
      break;
    case kSteal:
      buffer_ = cord_.GetAppendBuffer(n);
      break;
    case kPartial:
      break;
  }
}

absl::Span<char> CordBuilder::GetAppendRegion(size_t n) {
  static char dummy;
  if (ABSL_PREDICT_FALSE(n == 0)) return {&dummy, 0};
  MaybeNewBuffer((std::max)(size_hint_, n));
  return ExpandBuffer(n);
}

absl::Span<char> CordBuilder::GetAppendBuffer(size_t n) {
  static char dummy;
  if (ABSL_PREDICT_FALSE(n == 0)) return {&dummy, 0};
  MaybeNewBuffer((std::max)(size_hint_, n));
  return ExpandBuffer(std::numeric_limits<size_t>::max());
}

void CordBuilder::Append(absl::string_view sv) {
  if (ABSL_PREDICT_FALSE(sv.empty())) return;

  if (sv.size() > size_hint_) {
    // Amortized growth by ~12.5%
    size_hint_ = (std::max)((cord_.size() + buffer_.length()) / 8, sv.size());
  }

  MaybeNewBuffer(size_hint_);
  absl::Span<char> span = ExpandBuffer(sv.size());
  memcpy(span.data(), sv.data(), span.size());
  while (sv.size() > span.size()) {
    sv.remove_prefix(span.size());
    cord_.Append(std::move(buffer_));
    buffer_ = absl::CordBuffer::CreateWithCustomLimit(block_size_, size_hint_);
    span = ExpandBuffer(sv.size());
    memcpy(span.data(), sv.data(), span.size());
  }
}

void CordBuilder::Append(absl::Cord cord) {
  if (ABSL_PREDICT_TRUE(!cord.empty())) {
    cord_.Append(std::move(buffer_));
    cord_.Append(std::move(cord));

    // We may be able to steal from the donated cord.
    state_ = kSteal;
  }
}

void CordBuilder::ShrinkAppendBufferBy(size_t length) {
  assert(length <= buffer_.length());
  if (ABSL_PREDICT_FALSE(length == 0)) return;
  if (ABSL_PREDICT_FALSE(length > buffer_.length())) return;
  buffer_.SetLength(buffer_.length() - length);
  state_ = kPartial;
}

absl::Cord CordBuilder::Build() {
  cord_.Append(std::move(buffer_));
  absl::Cord cord = std::move(cord_);
  cord_.Clear();
  state_ = kEmpty;
  size_hint_ = 0;
  return cord;
}

}  // namespace strings
