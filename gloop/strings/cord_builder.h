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

#ifndef THIRD_PARTY_GLOOP_STRINGS_CORD_BUILDER_H_
#define THIRD_PARTY_GLOOP_STRINGS_CORD_BUILDER_H_

#include <cassert>
#include <cstddef>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/numeric/bits.h"
#include "absl/strings/cord.h"
#include "absl/strings/cord_buffer.h"

namespace strings {

// CordBuilder is a simple builder class allowing applications to efficiently
// compose cord data. This class is aimed at two main use cases:
// - Zero copy initialization
// - Incremental append of pre-existing data with size prediction.
//
// Zero copy initialization
// ------------------------
// In this use case, the application requires some pre-allocated buffer where
// data can be initialized and/or read. For example, if we'd want to initialize
// a cord with 5000 'z' characters, we could write:
//
//   std::string string_value(5000, 'z');
//   absl::Cord cord(string_value);
//
// This is however wasteful, as we need to allocate and fill the string, only
// to then have the cord copy the string contents again in its constructor.
// Using a cord builder, we can write the same code more efficiently as:
//
//   strings::CordBuilder builder;
//   for (size_t remaining = 5000; remaining > 0; ) {
//     absl::span<char> span = builder.GetAppendRegion(remaining);
//     std::fill_n(span.data(), span.size(), 'z');
//     remaining -= span.size();
//   }
//   absl::Cord cord = builder.Build();
//
// Likewise, we can also use a CordBuilder to append to an existing cord. Such
// an existing cord can be 'donated' by moving the existing value into the
// constructor. Donating a cord is useful if the cord is not shared, and there
// is potential pre-existing spare capacity inside the cord that can be utilized
// for additional data:
//
//   absl::Cord AppendCharactersToCord(absl::Cord cord, size_t n, char c) {
//     strings::CordBuilder builder(std::move(cord));
//     while (n > 0) {
//       absl::span<char> span = builder.GetAppendRegion(n);
//       std::fill_n(span.data(), span.size(), c);
//       n -= span.size();
//     }
//     return builder.Build();
//   }
//
// Append buffers and validity of returned memory references
// ---------------------------------------------------------
// The spans returned by the `GetAppendRegion` and `GetAppendBuffer` methods
// reference a `CordBuffer` instance managed by the `CordBuilder` instance.
// This append buffer (and the memory referenced by it through the span) may
// get invalidated by various operations.
//
// For example, any call to `GetAppendRegion` will flush the previous append
// buffer, and allocate a new instance, invalidating the previous buffer.
// Likewise, functions like `Append(string_view)` may flush and invalidate any
// pending append buffer. Functions that invalidate the active append buffer are
// clearly documented as such.
//
// Applications must make sure that all contents in the active append buffer
// referenced by the span returned from a call to `GetAppendRegion` or
// `GetAppendBuffer` is fully initialized. Unused capacity in the returned
// span can be removed by shrinking the append buffer length through the
// `ShrinkAppendBufferBy()` method. Failing to initialize all data in the
// append buffer will lead to undefined behavior.
//
// Incremental append of pre-existing data with size prediction
// ------------------------------------------------------------
// In this use case, the application already has the data available in some form
// (typically one or more string values), and wants to build a cord from these
// individual fragments. A naive example of doing this with regular Cord API
// functions (such as Append(string_view)) could look as per below:
//
//   absl::Cord CreateCord(absl::Span<const absl::string_view> data) {
//     absl::Cord cord;
//     for (absl::string_view sv : data) {
//       cord.Append(sv);
//     }
//     return cord;
//   }
//
// The above code can be inefficient if the provided data is fragmented. Cord
// uses a conservative amortized growth strategy, which means that in a case
// where there are many small fragments, the initial cord buffers will all be
// relatively small which results in a considerable memory overhead and reduced
// efficiency accessing the created cord. Additionally, the last buffer will
// typically be under-utilized.
//
// Using a CordBuilder, the application can explicitly set a 'size hint' which
// will result in the builder fitting all the data using optimized buffer sizes.
// For example, we can rewrite our earlier example as:
//
//   absl::Cord CreateCord(absl::Span<const absl::string_view> data) {
//     strings::CordBuilder builder;
//     for (absl::string_view sv : data) {
//       builder.IncreaseSizeHintBy(sv.size());
//     }
//     for (absl::string_view sv : data) {
//       builder.Append(sv);
//     }
//     return builder.Build();
//   }
//
// Again, we can append data to an existing cord by providing the cord at
// construction. Note that the size hint defines the additional size the
// application intends to add to the cord, not the end size of the cord:
//
//   absl::Cord AppendCordAndData(const absl::Cord& cord,
//                                absl::Span<const absl::string_view> data) {
//     strings::CordBuilder builder(cord);
//     for (absl::string_view sv : data) {
//       builder.IncreaseSizeHintBy(sv.size());
//     }
//     for (absl::string_view sv : data) {
//       builder.Append(sv);
//     }
//     return builder.Build();
//   }
//
// Using size hints with GetAppendRegion()
// ---------------------------------------
// The GetAppendRegion() function assumes that the application will always
// request the maximum (final) amount of bytes it desires to write as in our
// earlier examples. However, this may not always be the case. For example,
// assume that we have an existing span of fragments which we want to copy
// converting all data to upper case. The easiest way to write this would be:
//
//   absl::Cord CreateUpperCaseCord(absl::Span<const absl::string_view> data) {
//     strings::CordBuilder builder;
//     for (absl::string_view sv : data) {
//       while (!sv.empty()) {
//         absl::Span<char> span = builder.GetAppendRegion(sv.size());
//         for (size_t i = 0; i < span.size(); ++i) {
//           span[i] = toupper(sv[i]);
//         }
//         sv.remove_prefix(span.size());
//       }
//     }
//     return builder.Build();
//   }
//
// Each GetAppendRegion() call will now become an individual buffer in the cord,
// as the builder assumes the requested amount is the exact (final) size you
// desire. This will create a fragmented cord, which is undesirable. However, we
// can use size hints with GetAppendRegion() use cases as well: the builder will
// then manage buffers internally according to the desired size hint, and return
// incremental partial spans inside these buffers to the application matching
// the requested size. Our code will then look as follows:
//
//   absl::Cord CreateUpperCaseCord(absl::Span<const absl::string_view> data) {
//     strings::CordBuilder builder;
//     for (absl::string_view sv : data) {
//       builder.IncreaseSizeHintBy(sv.size());
//     }
//     for (absl::string_view sv : data) {
//       while (!sv.empty()) {
//         absl::Span<char> span = builder.GetAppendRegion(sv.size());
//         for (size_t i = 0; i < span.size(); ++i) {
//           span[i] = toupper(sv[i]);
//         }
//         sv.remove_prefix(span.size());
//       }
//     }
//     return builder.Build();
//   }
//
// Append(absl::Cord cord)
// -----------------------
// The builder provides an `Append(absl::Cord)` method for convenience.
// However, care should be taken when mixing `GetAppendRegion(size_t)` and
// `Append(string_view)` calls with `Append(absl::Cord)` calls: the builder
// allocates buffers internally based on the size hints as provided by the
// application, or when absent or exceeded, uses amortized growth.
//
// In cases where applications have a mix of string fragments and cord data, the
// application should attempt to set size hints dynamically for each span of
// string fragments preceding a cord value, or otherwise minimize potential
// waste of partially filled buffers inside the builder leading to waste.
// Exploring this problem in depth goes beyond the scope and purpose of this
// documentation and intended use case of the CordBuilder, but one solution for
// such code could look roughly as follows:
//
//   using StringOrCord = std::variant<absl::string_view, absl::Cord>;
//
//   absl::Cord BuildCord(absl::Span<const StringOrCord> data) {
//     strings::CordBuilder builder;
//     auto it = data.begin();
//     while (it != data.end()) {
//       auto [end, size] = FindEndOfSameType(it, data.end());
//       if (std::holds_alternative<absl::string_view>(*it)) {
//         builder.SetSizeHint(size);
//         while (it != end) {
//           builder.Append(std::get<absl::string_view>(*it++));
//         }
//       } else {
//         while (it != end) {
//           builder.Append(std::get<absl::Cord>(*it++));
//         }
//       }
//     }
//     return builder.Build();
//   }
//
// Using ShrinkAppendBufferBy() to reduce the pending buffer length
// ----------------------------------------------------------------
// In our previous examples, the application knows the exact size of the output
// in advance, but this may not always be the case. For example, for cases such
// as reading from a stream or compressing data, we often do not know the final
// size in advance. Typically, applications should pick a reasonable buffer size
// based on some approximated output size or use case, and use the
// `GetAppendBuffer()` method to get fully utilized buffers.
//
// The application can then use the `ShrinkAppendBufferBy()` method to reduce
// the length of the pending append buffer by the number of bytes we did not
// initialize (i.e., over-allocate) from the span returned by the last call to
// `GetAppendBuffer()` as demonstrated in the example below:
//
//   absl::Cord StreamToCord(InputOnlyStream& stream) {
//     strings::CordBuilder builder;
//     for (;;) {
//       absl::Span span = builder.GetAppendBuffer(kStreamBlockSize);
//       size_t bytes_read = stream.Read(span.data(), span.size());
//       if (bytes_read < span.size()) {
//         builder.ShrinkAppendBufferBy(span.size() - bytes_read);
//         return builder.Build();
//       }
//     }
//   }
//
// Alternative block sizes
// -----------------------
// CordBuilder by default uses the default block size and limits as defined by
// the underlying CordBuffer implementation. However, applications can select a
// custom block size that is either larger or smaller than the default if they
// have a compelling use case to do so using the `SetBlockSize()` method.
//
// In general, applications should only use custom block sizes based on
// objective data and performance metrics establishing the need for such.
// For example, a compress function may work faster and consume less CPU when
// using larger buffers. Such an application should pick a block size offering
// a reasonable trade-off between expected data size, compute savings with
// larger buffers, and the cost or fragmentation effect of larger buffers.
class CordBuilder {
 public:
  // Creates a CordBuilder constructing a new Cord value.
  explicit CordBuilder(size_t size_hint = 0) noexcept;

  // Creates a cord builder appending to the specified cord.
  explicit CordBuilder(absl::Cord cord, size_t size_hint = 0) noexcept;

  // CordBuilder is move only.
  // Moving a CordBuilder invalidates any active append buffer.
  // A 'moved from' instance will have a valid empty state, identical to 'as
  // if' it were newly created with 'size_hint = 0`.
  CordBuilder(CordBuilder&&) noexcept;
  CordBuilder& operator=(CordBuilder&&) noexcept;

  // Returns an uninitialized buffer to be initialized by the application.
  // Applications typically request some desired 'total size' of data they need
  // to initialize, and receive a buffer less than, or equal to that size.
  // Returns an empty span with a non-null data pointer if `n` is zero.
  // This method invalidates any active append buffer.
  absl::Span<char> GetAppendRegion(size_t n);

  // Returns an uninitialized buffer to be initialized by the application.
  // This function is similar to `GetAppendRegion()` except that the returned
  // span may be both larger or smaller than `n`. This function is intended for
  // use cases where the final size of the output is not known such as
  // compression and streaming operations, and applications want an efficient
  // output buffer of approximately `n` bytes.
  // This method invalidates any active append buffer.
  absl::Span<char> GetAppendBuffer(size_t n);

  // Appends the provided string data to this instance. Applications adding
  // multiple string fragments to a builder should use size hints to guarantee
  // optimum internal buffer sizes and utilization of those buffers. Absent
  // explicit size hints, the builder will use a conservative amortized growth
  // which will lead to less efficient and somewhat more wasteful cords.
  // This method invalidates any active append buffer.
  // This function has no effect if `sv` is empty.
  void Append(absl::string_view sv);

  // Appends the provided cord to this instance.
  // This method invalidates any active append buffer.
  void Append(absl::Cord cord);

  // Increases the size hint for this builder by `n', indicating that the
  // application has the intent to add 'n' more bytes beyond the already
  // intended (or current) data growth.
  void IncreaseSizeHintBy(size_t n);

  // Set an explicit new size hint value.
  void SetSizeHint(size_t size_hint);

  // Set a custom block size for this builder. Applications should prefer to
  // use the default block size. See class comments for more information.
  // Requires `block_size` to be a power of 2 and to be no less than 128.
  void SetBlockSize(size_t block_size);

  // Reduces the length of the current append buffer by `length` bytes.
  // Applications can perform multiple `ShrinkAppendBufferBy()` calls, but
  // the cumulative length of these calls can not exceed the length of the
  // append buffer. For example:
  //
  //   // Returns a span of size 100, referencing uninitialized data.
  //   absl::Span<char> span = builder.GetAppendRegion(100);
  //   memset(span.data(), 30, 'x');
  //
  //   // Shrinks the length of the last append buffer by 70 to a new length
  //   // of 30. After this call, only the first 30 bytes referenced by the
  //   // span can legally be referenced.
  //   builder.ShrinkAppendBufferBy(70);
  //
  //   // The following call will lead to undefined behavior, as the
  //   // requested shrinkage exceeds the length of the append buffer.
  //   builder.ShrinkAppendBufferBy(40);
  //
  // This method has no effect if `length` is zero.
  void ShrinkAppendBufferBy(size_t length);

  // Builds and returns the cord. This call resets the current instance to an
  // empty builder 'as if' it was freshly created with `size_hint = 0`.
  absl::Cord Build();

 private:
  enum State { kEmpty, kSteal, kPartial, kFull };

  // Creates a new buffer unless the state is `kPartial`
  void MaybeNewBuffer(size_t n);

  // Returns a span of up to `n` available capacity from the current buffer.
  // Requires that the current buffer is not fully utilized.
  absl::Span<char> ExpandBuffer(size_t n);

  absl::Cord cord_;
  absl::CordBuffer buffer_;
  State state_ = kEmpty;
  size_t size_hint_;
  size_t block_size_ = absl::cord_internal::kMaxFlatSize;
};

inline CordBuilder::CordBuilder(size_t size_hint) noexcept
    : size_hint_(size_hint) {}

inline CordBuilder::CordBuilder(absl::Cord cord, size_t size_hint) noexcept
    : cord_(std::move(cord)), state_(kSteal), size_hint_(size_hint) {}

inline void CordBuilder::IncreaseSizeHintBy(size_t n) { size_hint_ += n; }

inline void CordBuilder::SetSizeHint(size_t size_hint) {
  size_hint_ = size_hint;
}

inline void CordBuilder::SetBlockSize(size_t block_size) {
  assert(block_size >= 128);
  assert(absl::has_single_bit(block_size));
  block_size_ = block_size;
}

}  // namespace strings

#endif  // THIRD_PARTY_GLOOP_STRINGS_CORD_BUILDER_H_
