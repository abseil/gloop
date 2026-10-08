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

#include "gloop/strings/bytestream.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/internal/raw_logging.h"
#include "absl/base/nullability.h"
#include "absl/memory/memory.h"
#include "absl/strings/internal/resize_uninitialized.h"
#include "absl/strings/string_view.h"

namespace strings {

void ByteSource::CopyTo(ByteSink* absl_nonnull sink, size_t n) {
  assert(n <= Available());
  while (n > 0) {
    absl::string_view fragment = Peek();
    if (fragment.empty()) {
      // TODO: handle this better.
      ABSL_RAW_LOG(ERROR, "ByteSource::CopyTo() overran input.");
      break;
    }
    size_t fragment_size = std::min<size_t>(n, fragment.size());
    sink->Append(fragment.data(), fragment_size);
    Skip(fragment_size);
    n -= fragment_size;
  }
}

void ByteSink::AppendExternalMemory(
    absl::string_view data, void* absl_nullable arg,
    void (*absl_nonnull memory_releaser)(void* absl_nullable)) {
  Append(data.data(), data.size());
  (*memory_releaser)(arg);
}

size_t ByteSink::MinAppendExternalMemoryLength() const {
  return std::numeric_limits<size_t>::max();
}

char* absl_nonnull ByteSink::GetAppendBuffer(size_t min_capacity,
                                             size_t /* desired_capacity_hint */,
                                             char* absl_nonnull scratch,
                                             size_t scratch_capacity,
                                             size_t* result_capacity) {
  ABSL_RAW_CHECK(min_capacity >= 1, "");
  ABSL_RAW_CHECK(scratch_capacity >= min_capacity, "");
  *result_capacity = scratch_capacity;
  return scratch;
}

void ByteSink::Flush() {}

void UncheckedArrayByteSink::Append(const char* absl_nonnull data, size_t n) {
  if (data != dest_) {
    // Catch cases where the pointer returned by GetAppendBuffer() was modified.
    assert(!(dest_ <= data && data < (dest_ + n)));
    memcpy(dest_, data, n);
  }
  dest_ += n;
}

char* absl_nonnull UncheckedArrayByteSink::GetAppendBuffer(
    size_t min_capacity, size_t desired_capacity_hint,
    char* absl_nullable /* scratch */, size_t scratch_capacity,
    size_t* absl_nonnull result_capacity) {
  ABSL_RAW_CHECK(min_capacity >= 1, "");
  ABSL_RAW_CHECK(scratch_capacity >= min_capacity, "");
  *result_capacity = std::max(min_capacity, desired_capacity_hint);
  return dest_;
}

CheckedArrayByteSink::CheckedArrayByteSink(char* absl_nonnull outbuf,
                                           size_t capacity)
    : outbuf_(outbuf), capacity_(capacity), size_(0), overflowed_(false) {}

void CheckedArrayByteSink::Append(const char* bytes, size_t n) {
  size_t available = capacity_ - size_;
  if (n > available) {
    n = available;
    overflowed_ = true;
  }
  if (n > 0 && bytes != (outbuf_ + size_)) {
    // Catch cases where the pointer returned by GetAppendBuffer() was modified.
    assert(!(outbuf_ <= bytes && bytes < (outbuf_ + capacity_)));
    memcpy(outbuf_ + size_, bytes, n);
  }
  size_ += n;
}

char* absl_nonnull CheckedArrayByteSink::GetAppendBuffer(
    size_t min_capacity, size_t /* desired_capacity_hint */,
    char* absl_nonnull scratch, size_t scratch_capacity,
    size_t* absl_nonnull result_capacity) {
  ABSL_RAW_CHECK(min_capacity >= 1, "");
  ABSL_RAW_CHECK(scratch_capacity >= min_capacity, "");
  size_t available = capacity_ - size_;
  if (available >= min_capacity) {
    *result_capacity = available;
    return outbuf_ + size_;
  } else {
    *result_capacity = scratch_capacity;
    return scratch;
  }
}

GrowingArrayByteSink::GrowingArrayByteSink(size_t estimated_size)
    : capacity_(estimated_size),
      buf_(absl::make_unique_for_overwrite<char[]>(estimated_size)),
      size_(0) {}

void GrowingArrayByteSink::Append(const char* absl_nonnull bytes, size_t n) {
  size_t available = capacity_ - size_;
  if (bytes != (buf_.get() + size_)) {
    // Catch cases where the pointer returned by GetAppendBuffer() was modified.
    // We need to test for this before calling Expand() which may reallocate.
    assert(!(buf_.get() <= bytes && bytes < (buf_.get() + capacity_)));
  }
  if (n > available) {
    Expand(n - available);
  }
  if (n > 0 && bytes != (buf_.get() + size_)) {
    memcpy(buf_.get() + size_, bytes, n);
  }
  size_ += n;
}

char* absl_nonnull GrowingArrayByteSink::GetAppendBuffer(
    size_t min_capacity, size_t desired_capacity_hint,
    char* absl_nullable /* scratch */, size_t scratch_capacity,
    size_t* absl_nonnull result_capacity) {
  ABSL_RAW_CHECK(min_capacity >= 1, "");
  ABSL_RAW_CHECK(scratch_capacity >= min_capacity, "");
  size_t available = capacity_ - size_;
  if (available < min_capacity) {
    Expand(std::max(min_capacity, desired_capacity_hint) - available);
    available = capacity_ - size_;
  }
  *result_capacity = available;
  return buf_.get() + size_;
}

absl_nonnull std::unique_ptr<char[]> GrowingArrayByteSink::GetBuffer(
    size_t* absl_nonnull nbytes) {
  ShrinkToFit();
  auto ret = std::move(buf_);
  *nbytes = size_;
  size_ = capacity_ = 0;
  return ret;
}

void GrowingArrayByteSink::Expand(size_t amount) {  // Expand by at least 50%.
  size_t new_capacity = std::max(capacity_ + amount, (3 * capacity_) / 2);
  auto bigger = absl::make_unique_for_overwrite<char[]>(new_capacity);
  memcpy(bigger.get(), buf_.get(), size_);
  buf_ = std::move(bigger);
  capacity_ = new_capacity;
}

void GrowingArrayByteSink::ShrinkToFit() {
  // Shrink only if the buffer is large and size_ is less than 3/4
  // of capacity_.
  if (capacity_ > 256 && size_ < (3 * capacity_) / 4) {
    auto just_enough = absl::make_unique_for_overwrite<char[]>(size_);
    memcpy(just_enough.get(), buf_.get(), size_);
    buf_ = std::move(just_enough);
    capacity_ = size_;
  }
}

StringByteSink::~StringByteSink() {
  // Rollback the last GetAppendBuffer
  UndoAppendBuffer();
}

void StringByteSink::UndoAppendBuffer() {
  if (last_append_buffer_pos_ != std::string::npos) {
    dest_->erase(last_append_buffer_pos_);
    last_append_buffer_pos_ = std::string::npos;
  }
}

void StringByteSink::Append(const char* absl_nonnull data, size_t n) {
  if (last_append_buffer_pos_ != std::string::npos &&
      data == dest_->data() + last_append_buffer_pos_) {
    // This is the internal buffer returned by a past call to
    // GetAppendBuffer.
    dest_->erase(last_append_buffer_pos_ + n);
    last_append_buffer_pos_ = std::string::npos;
    return;
  }

  // In case there is an unused append buffer.
  UndoAppendBuffer();

  dest_->append(data, n);
}

char* absl_nonnull StringByteSink::GetAppendBuffer(
    size_t min_capacity, size_t desired_capacity_hint,
    char* absl_nonnull scratch, size_t scratch_capacity,
    size_t* absl_nonnull result_capacity) {
  if (!absl::strings_internal::STLStringSupportsNontrashingResize(dest_)) {
    *result_capacity = scratch_capacity;
    return scratch;
  }

  // In case there is an unused append buffer.
  UndoAppendBuffer();

  const size_t increase_by = std::max(min_capacity, desired_capacity_hint);
  const size_t current_size = dest_->size();
  const size_t new_size = current_size + increase_by;

  ABSL_RAW_CHECK(last_append_buffer_pos_ == std::string::npos, "");
  last_append_buffer_pos_ = dest_->size();

  if (new_size > dest_->capacity()) {
    // Use amortized exponential growth.
    // Ask it to reallocate first...
    dest_->reserve(std::max(new_size, 2 * dest_->capacity()));
  }

  // then ask it to resize to its current capacity to take advantage of it.
  absl::strings_internal::STLStringResizeUninitialized(dest_,
                                                       dest_->capacity());

  *result_capacity = dest_->capacity() - current_size;

  // If string size is zero, then string_as_array() returns nullptr, so
  // we need to use data() instead
  return const_cast<char*>(dest_->data()) + current_size;
}

void NullByteSink::Append(const char* absl_nonnull /* data */, size_t) {}

size_t ArrayByteSource::Available() const { return input_.size(); }

absl::string_view ArrayByteSource::Peek() { return input_; }

void ArrayByteSource::Skip(size_t n) {
  assert(n <= input_.size());
  input_.remove_prefix(n);
}

LimitByteSource::LimitByteSource(ByteSource* absl_nonnull source, size_t limit)
    : source_(source), limit_(limit) {}

size_t LimitByteSource::Available() const {
  size_t available = source_->Available();
  if (available > limit_) {
    available = limit_;
  }

  return available;
}

absl::string_view LimitByteSource::Peek() {
  absl::string_view piece(source_->Peek());
  if (piece.size() > limit_) {
    piece = absl::string_view(piece.data(), limit_);
  }

  return piece;
}

void LimitByteSource::Skip(size_t n) {
  assert(n <= limit_);
  source_->Skip(n);
  limit_ -= n;
}

void LimitByteSource::CopyTo(ByteSink* sink, size_t n) {
  assert(n <= limit_);
  source_->CopyTo(sink, n);
  limit_ -= n;
}

}  // namespace strings
