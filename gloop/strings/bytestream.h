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

// This package contains abstract interfaces for the consumption and production
// of bytes, as well as a few basic implementations. Because C++ has no native
// 'byte' type, strings often serve to store binary data (through arbitrary
// length 'char' elements).
//
// The ByteSink (for byte consumption) and ByteSource (for byte production
// from a fixed-size source) interfaces allow you to develop APIs that enable
// your code to work with a variety of input and output types.
//
// Any implementation of ByteSink must (at least) implement an Append()
// function.
//
// In addition to ByteSink and ByteSource interfaces, this file also declares
// the following implementations:.
//
//   ByteSink:
//      UncheckedArrayByteSink  Writes to an array, without bounds checking
//      CheckedArrayByteSink    Writes to an array, with bounds checking
//      GrowingArrayByteSink    Allocates and writes to a growable buffer
//      StringByteSink          Writes to an std::string
//      NullByteSink            Consumes a never-ending stream of bytes
//
//   ByteSource:
//      ArrayByteSource         Reads from an array or string/string_view
//      LimitedByteSource       Limits the number of bytes read from an
//                              underlying source

#ifndef THIRD_PARTY_GLOOP_STRINGS_BYTESTREAM_H_
#define THIRD_PARTY_GLOOP_STRINGS_BYTESTREAM_H_

#include <cstddef>
#include <memory>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/strings/string_view.h"

namespace strings {

// TypeId::For<A>() is a token which is equal to TypeId::For<B>() whenever
// A and B are the same type. TypeId() is another value not equal to any other.
class TypeId {
 public:
  TypeId() : ptr_(nullptr) {}

  template <typename T>
  static TypeId For();

  friend bool operator==(TypeId a, TypeId b) { return a.ptr_ == b.ptr_; }
  friend bool operator!=(TypeId a, TypeId b) { return a.ptr_ != b.ptr_; }

 private:
  explicit TypeId(void* absl_nonnull ptr) : ptr_(ptr) {}

  void* absl_nullable ptr_;
};

template <typename T>
TypeId TypeId::For() {
  static char token;
  return TypeId(&token);
}

// A ByteSink is an abstract interface for an object that consumes a sequence of
// bytes. This interface offers several ways to append data, a utility function
// to determine minimum block sizes to append, and a Flush() function. ByteSinks
// cannot be copied, assigned, moved, or move-assigned.
//
// Example:
//
//   std::string my_data;
//   ...
//   ByteSink* sink = ...
//   sink->Append(my_data.data(), my_data.size());
//   sink->Flush();
//
class ByteSink {
 public:
  ByteSink() {}
  ByteSink(const ByteSink&) = delete;
  ByteSink& operator=(const ByteSink&) = delete;
  virtual ~ByteSink() {}

  virtual void Append(absl::string_view data) {
    Append(data.data(), data.size());
  }

  // Appends "n" bytes from "bytes" into the ByteSink.
  ABSL_DEPRECATED("Use Append(absl::string_view).")
  virtual void Append(const char* absl_nullable bytes, size_t n) = 0;

  // Appends external "data" to the ByteSink. (The ByteSink may, but does not
  // necessarily need to, copy this data internally.) Append() will call
  // (*memory_releaser)(arg) exactly once after the passed data is appended.
  // Note that if the data is copied, (*memory_releaser)(arg) will be called
  // after the copy; if instead, the data is appended via pointer management,
  // (*memory_releaser)(arg) will be called after the memory is done being
  // shared.
  //
  // A subclass that overrides AppendExternalMemory() must override
  // MinAppendExternalMemoryLength() as well.
  //
  // Implementation Note: since a std::function would require an extra
  // allocation, AppendExternalMemory() uses raw function pointers for speed and
  // efficiency.
  //
  // Note that (*memory_releaser)(arg) may be called before
  // AppendExternalMemory() returns.
  //
  // REQUIRES: memory_releaser != nullptr
  virtual void AppendExternalMemory(
      absl::string_view data, void* absl_nullable arg,
      void (*absl_nonnull memory_releaser)(void* absl_nullable));

  // Returns the length of the smallest block that the caller should
  // pass to AppendExternalMemory(). The result is advisory; the caller is
  // free to pass in smaller blocks. This minimum block length may vary over
  // time (e.g., as the sink fills up).
  virtual size_t MinAppendExternalMemoryLength() const;

  // GetAppendBuffer() returns a writable buffer to use for an Append()
  // call. The caller specifies the buffer's min_capacity, a `scratch` buffer
  // that contains a scratch_capacity at least min_capacity in size, and writes
  // the size of the resulting buffer to *result_capacity.
  //
  // A ByteSink implementation may either return the caller's own scratch buffer
  // back, OR it may return an internal buffer that can make appending more
  // efficient. The returned buffer is only valid until the next operation on
  // the ByteSink. A buffer's *result_capacity will always be >= min_capacity.
  //
  // Some Append() implementations may be able to avoid copying bytes if
  // GetAppendBuffer() returns an internal buffer. The default implementation of
  // GetAppendBuffer() always returns the scratch buffer.
  //
  // Example:
  //   size_t capacity;
  //   char* buffer = sink->GetAppendBuffer(..., &capacity);
  //   ... Write n bytes into buffer, with n <= capacity.
  //   sink->Append(buffer, n);
  //   // In some implementations, the call to Append() will avoid copying data.
  //
  // If the ByteSink allocates or reallocates an internal buffer, it should use
  // the desired_capacity_hint, if appropriate. If a caller cannot provide a
  // reasonable guess at the internal buffer's desired capacity, pass
  // desired_capacity_hint = 0.
  //
  // NOTE: the returned buffer should be passed directly to Append(), although
  // that size may differ from *result_capacity. Do not pass an interior pointer
  // to the append buffer when calling Append().

  virtual char* absl_nonnull GetAppendBuffer(
      size_t min_capacity, size_t desired_capacity_hint,
      char* absl_nonnull scratch, size_t scratch_capacity,
      size_t* absl_nonnull result_capacity);

  // Flushes internal buffers. The default implementation does nothing. ByteSink
  // subclasses may use internal buffers that require calling Flush() at the end
  // of the stream.
  virtual void Flush();

  // Returns a token which allows to detect the class of the ByteSink at
  // runtime.
  //
  // By default returns TypeId(). In order for a class to participate in class
  // detection at runtime, it must override GetTypeId():
  //
  //   TypeId A::GetTypeId() const override { return TypeId::For<A>(); }
  //
  // Then, to actually cast:
  //
  //   if (sink->GetTypeId() == TypeId::For<A>()) {
  //     A* a = static_cast<A*>(sink);
  //     ...
  //   }
  //
  // This solution is more limited but faster than typeid or dynamic_cast.
  virtual TypeId GetTypeId() const { return TypeId(); }
};

// A ByteSource is an abstract interface for an object that produces a sequence
// of bytes from a source of fixed-size. Note that such a "source" may consist
// of multiple buffers and that not all data need be in memory at the same time.
//
// Example:
//
//   ByteSource* source = ...
//   while (source->Available() > 0) {
//     absl::string_view data = source->Peek();
//     ... do something with "data" ...
//     source->Skip(data.length());
//   }
//
class ByteSource {
 public:
  ByteSource() {}
  ByteSource(const ByteSource&) = delete;
  ByteSource& operator=(const ByteSource&) = delete;
  virtual ~ByteSource() {}

  // Returns the number of bytes left to read from the source. Available()
  // should decrease by N each time Skip(N) is called; Available() may not
  // increase. If Available() returns 0, that indicates that the ByteSource is
  // exhausted.
  //
  // Note: Size() may have been a more appropriate name Available() as it's more
  //       indicative of the fixed-size nature of a ByteSource.
  virtual size_t Available() const = 0;

  // Returns an absl::string_view of the next contiguous region of the source.
  // Does not reposition the source. The returned region is empty iff
  // Available() == 0.
  //
  // The returned region is valid until the next call to Skip() or until this
  // object is destroyed, whichever occurs first.
  //
  // The length of the returned absl::string_view will be <= Available().
  virtual absl::string_view Peek() = 0;

  // Skips the next n bytes. Invalidates any absl::string_view returned by a
  // previous call to Peek().
  //
  // REQUIRES: Available() >= n
  virtual void Skip(size_t n) = 0;

  // Writes the next n bytes in this ByteSource to the given ByteSink, and
  // advances this ByteSource past the copied bytes. The default implementation
  // of this method just copies the bytes normally, but subclasses might
  // override CopyTo to optimize certain cases.
  //
  // REQUIRES: Available() >= n
  virtual void CopyTo(ByteSink* absl_nonnull sink, size_t n);
};

//
// Some commonly used implementations of ByteSink
//

// UncheckedArrayByteSink() writes to an unsized byte array. No bounds-checking
// is performed; it is the caller's responsibility to ensure that the
// destination array is large enough.
//
// Example:
//
//   char buf[10];
//   UncheckedArrayByteSink sink(buf);
//   sink.Append("hi", 2);    // OK
//   sink.Append(data, 100);  // WOOPS! Overflows buf[10].
//
class UncheckedArrayByteSink final : public ByteSink {
 public:
  explicit UncheckedArrayByteSink(char* absl_nonnull dest) : dest_(dest) {}
  UncheckedArrayByteSink(const UncheckedArrayByteSink&) = delete;
  UncheckedArrayByteSink& operator=(const UncheckedArrayByteSink&) = delete;
  void Append(const char* absl_nonnull data, size_t n) override;
  char* GetAppendBuffer(size_t min_capacity, size_t desired_capacity_hint,
                        char* absl_nullable scratch, size_t scratch_capacity,
                        size_t* result_capacity) override;

  // Returns the current output pointer of the UncheckedArrayByteSink so that a
  // caller can see how many bytes were produced.
  char* absl_nonnull CurrentDestination() const { return dest_; }

 private:
  char* absl_nonnull dest_;
};

// CheckedArrayByteSink() writes to a sized byte array. This sink will not write
// more than "capacity" bytes to outbuf. Within an Append() call, once
// the sink's capacity is reached, subsequent bytes will be ignored and
// Overflowed() will return true.
//
// Overflowed() will not cause a runtime error (i.e., it will not CHECK fail).
//
// Example:
//
//   char buf[10];
//   CheckedArrayByteSink sink(buf, 10);
//   sink.Append("hi", 2);    // OK
//   sink.Append(data, 100);  // Will only write 8 more bytes
//
class CheckedArrayByteSink final : public ByteSink {
 public:
  CheckedArrayByteSink(char* absl_nonnull outbuf, size_t capacity);
  CheckedArrayByteSink(const CheckedArrayByteSink&) = delete;
  CheckedArrayByteSink& operator=(const CheckedArrayByteSink&) = delete;
  void Append(const char* absl_nonnull bytes, size_t n) override;
  char* GetAppendBuffer(size_t min_capacity, size_t desired_capacity_hint,
                        char* scratch, size_t scratch_capacity,
                        size_t* absl_nonnull result_capacity) override;

  // Returns the number of bytes actually written to the sink.
  size_t NumberOfBytesWritten() const { return size_; }

  // Returns true if any bytes were discarded during the Append(), i.e., if
  // Append() attempted to write more than 'capacity' bytes.
  bool Overflowed() const { return overflowed_; }

 private:
  char* absl_nonnull outbuf_;
  const size_t capacity_;
  size_t size_;
  bool overflowed_;
};

// GrowingArrayByteSink() allocates an internal buffer (a char array)
// and expands it as needed to accommodate appended data (similar to a
// std::string), and allows the caller to take ownership of the internal buffer
// via the GetBuffer() method. GetBuffer() also resets the internal buffer to be
// empty, and subsequent appends to the sink will create a new buffer. Calling
// the destructor on GrowingArrayByteSink() will free the internal buffer even
// if GetBuffer() was not called.
//
// Example:
//
//   GrowingArrayByteSink sink(10);
//   sink.Append("hi", 2);
//   sink.Append(data, n);
//   const char* buf = sink.GetBuffer();  // Ownership transferred
//   delete[] buf;
//
class GrowingArrayByteSink final : public strings::ByteSink {
 public:
  explicit GrowingArrayByteSink(size_t estimated_size);
  GrowingArrayByteSink(const GrowingArrayByteSink&) = delete;
  GrowingArrayByteSink& operator=(const GrowingArrayByteSink&) = delete;
  void Append(const char* absl_nonnull bytes, size_t n) override;
  char* absl_nonnull GetAppendBuffer(
      size_t min_capacity, size_t desired_capacity_hint,
      char* absl_nullable scratch, size_t scratch_capacity,
      size_t* absl_nonnull result_capacity) override;

  // Returns the allocated buffer, and sets nbytes to its size.
  absl_nonnull std::unique_ptr<char[]> GetBuffer(size_t* absl_nonnull nbytes);

 private:
  void Expand(size_t amount);
  void ShrinkToFit();

  size_t capacity_;
  std::unique_ptr<char[]> buf_;
  size_t size_;
};

// StringByteSink() appends bytes to the given "dest" std::string, keeping the
// original contents of "dest" within the std::string.
//
// Example:
//
//   std::string dest = "Hello ";
//   StringByteSink sink(&dest);
//   sink.Append("World", 5);
//   assert(dest == "Hello World");
//
class StringByteSink final : public ByteSink {
 public:
  explicit StringByteSink(std::string* absl_nonnull dest) : dest_(dest) {}
  ~StringByteSink() override;
  StringByteSink(const StringByteSink&) = delete;
  StringByteSink& operator=(const StringByteSink&) = delete;
  void Append(const char* absl_nonnull data, size_t n) override;
  char* absl_nonnull GetAppendBuffer(
      size_t min_capacity, size_t desired_capacity_hint,
      char* absl_nonnull scratch, size_t scratch_capacity,
      size_t* absl_nonnull result_capacity) override;

 private:
  void UndoAppendBuffer();

  std::string* absl_nonnull dest_;
  size_t last_append_buffer_pos_ = std::string::npos;
};

// NullByteSink() is a ByteSink that discards all data sent to it.
//
// Example:
//
//   NullByteSink sink;
//   sink.Append(data, data.size());  // All data ignored.
//
class NullByteSink final : public ByteSink {
 public:
  NullByteSink() {}
  NullByteSink(const NullByteSink&) = delete;
  NullByteSink& operator=(const NullByteSink&) = delete;
  void Append(const char* absl_nonnull /* data */,
              size_t) override;  // a NOP.
};

//
// Some commonly used implementations of ByteSource
//

// ArrayByteSource() reads bytes from an absl::string_view.
//
// Example:
//
//   std::string data = "Hello";
//   ArrayByteSource source(data);
//   assert(source.Available() == 5);
//   assert(source.Peek() == "Hello");
//
class ArrayByteSource : public ByteSource {
 public:
  explicit ArrayByteSource(absl::string_view s) : input_(s) {}
  ArrayByteSource(const ArrayByteSource&) = delete;
  ArrayByteSource& operator=(const ArrayByteSource&) = delete;

  size_t Available() const override;
  absl::string_view Peek() override;
  void Skip(size_t n) override;

 private:
  absl::string_view input_;
};

// LimitByteSource() wraps another ByteSource, limiting the number of bytes
// returned.
//
// The caller maintains ownership of the underlying source, and may not use the
// underlying source while using the LimitByteSource object. The underlying
// source's pointer is advanced by n bytes every time this LimitByteSource
// object is advanced by n.
//
// Example:
//
//   std::string data = "Hello World";
//   ArrayByteSource abs(data);
//   assert(abs.Available() == data.size());
//
//   LimitByteSource limit(abs, 5);
//   assert(limit.Available() == 5);
//   assert(limit.Peek() == "Hello");
//
class LimitByteSource final : public ByteSource {
 public:
  // Returns at most "limit" bytes from "source".
  LimitByteSource(ByteSource* absl_nonnull source, size_t limit);

  size_t Available() const override;
  absl::string_view Peek() override;
  void Skip(size_t n) override;

  // We override CopyTo so that we can forward to the underlying source, in
  // case it has an efficient implementation of CopyTo.
  void CopyTo(ByteSink* sink, size_t n) override;

 private:
  ByteSource* absl_nonnull source_;
  size_t limit_;
};

}  // namespace strings

#endif  // THIRD_PARTY_GLOOP_STRINGS_BYTESTREAM_H_
