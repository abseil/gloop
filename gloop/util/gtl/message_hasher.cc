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

#include "gloop/util/gtl/message_hasher.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/container/fixed_array.h"
#include "absl/hash/hash.h"
#include "absl/strings/cord.h"
#include "absl/strings/resize_and_overwrite.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "google/protobuf/message_lite.h"

namespace gtl {

using google::protobuf::io::ArrayOutputStream;
using google::protobuf::io::CodedOutputStream;
using google::protobuf::io::ZeroCopyOutputStream;

namespace internal_message_hasher {

std::string SerializeDeterministically(
    const google::protobuf::MessageLite& pb) {
  const size_t size = pb.ByteSizeLong();
  std::string s;
  absl::StringResizeAndOverwrite(s, size, [&pb](char* buf, size_t buf_size) {
    ArrayOutputStream array_stream(buf, buf_size);
    CodedOutputStream output_stream(&array_stream);
    // Avoid unspecified behaviour, esp. ordering of map<...> fields.
    output_stream.SetSerializationDeterministic(true);
    pb.SerializeWithCachedSizes(&output_stream);
    return buf_size;
  });
  return s;
}

}  // namespace internal_message_hasher

namespace {

// REQUIRES: The size is cached (i.e., pb.ByteSizeLong() has been called since
// pb's last mutation).  Returns true if successful.
bool SerializeProtoTo(const google::protobuf::MessageLite& pb,
                      ZeroCopyOutputStream* out) {
  CodedOutputStream stream(out);
  stream.EnableAliasing(out->AllowsAliasing());
  stream.SetSerializationDeterministic(true);
  pb.SerializeWithCachedSizes(&stream);
  return !stream.HadError();
}

// Note that by setting the HashBuffer to this exact size, I believe we sidestep
// the use of PiecewiseCombiner in H::combine_contiguous, making this code run
// faster.
constexpr size_t kBufSize = 1024;
using HashBuffer = std::array<char, kBufSize>;

// We're templatized on the type parameter of the hash (the same opaque type
// given to AbslHashValue overloads).
//
// The approach here is similar to that taken by the internal PiecewiseCombiner
// class in Abseil, but ZeroCopyOutputStream requires writable buffers, which
// PiecewiseCombiner does not provide.
template <typename H>
class HashingOutputStream final : public ZeroCopyOutputStream {
 public:
  explicit HashingOutputStream(HashBuffer* buf, H* h)
      : buf_(buf->data()), h_(h) {}

  H finish() && {
    if (i_ != 0) {
      Mix(buf_, i_);
    }
    return std::move(*h_);
  }

 private:
  // Mixes the given bytes into h_.
  void Mix(const char* p, size_t n) {
    byte_count_ += n;
    *h_ = H::combine_contiguous(std::move(*h_), p, n);
  }

  bool Next(void** data, int* size) override {
    switch (i_) {
      case kBufSize:
        Mix(buf_, kBufSize);
        [[fallthrough]];
      case 0:
        *data = buf_;
        *size = kBufSize;
        break;
      default:
        *data = buf_ + i_;
        *size = kBufSize - i_;
    }
    i_ = kBufSize;
    return true;
  }

  void BackUp(int count) override { i_ -= count; }

  int64_t ByteCount() const override { return byte_count_; }

  bool WriteAliasedRaw(const void* void_data, int size) override {
    const char* data = static_cast<const char*>(void_data);
    // If we won't even fill up the remaining buffer, copy the bytes into the
    // buffer and return early.
    if (i_ + size < kBufSize) {
      std::memcpy(buf_ + i_, data, size);
      i_ += size;
      return true;
    }

    // If we have a partial buffer, we need to fill it and Mix it first.
    if (i_ != 0) {
      // We have a partial buf_ and need to fill it from data.  We don't do this
      // if i_ == 0 because with a completely empty buf_, there's no reason to
      // memcpy into it rather than just mixing directly from data.
      const auto remaining = kBufSize - i_;
      std::memcpy(buf_ + i_, data, remaining);
      Mix(buf_, kBufSize);
      i_ = 0;
      data += remaining;
      size -= remaining;
    }

    // We continue to hash the aliased characters in chunks of kBufSize, in
    // order to ensure that the hash we produce is identical to the hash that
    // would be produced by hashing the serialized string in chunks of
    // kBufSize.
    //
    // CodedOutputStream has some higher-level logic (more complex than "I have
    // a std::string and I support aliasing, call WriteAliasedRaw) affecting its
    // decision to memcpy or call WriteAliasedRaw, so I don't feel comfortable
    // relying on the unspecified behavior that all protos are treated equally
    // in terms of what calls are made on the underlying ZeroCopyOutputStream.
    while (size >= kBufSize) {
      Mix(data, kBufSize);
      data += kBufSize;
      size -= kBufSize;
    }

    // Any remaining bytes will be copied into the buffer.
    std::memcpy(buf_, data, size);
    i_ = size;
    return true;
  }

  bool WriteCord(const absl::Cord& cord) override {
    for (absl::string_view chunk : cord.Chunks()) {
      WriteAliasedRaw(chunk.data(), chunk.size());
    }
    return true;
  }

  bool AllowsAliasing() const override { return true; }

  // This is the buffer we serialize into and hash.  It is kBufSize in size.
  char* buf_;

  // This is the number of bytes of buf_ that are relevant.  buf_ may be
  // partially filled if we received a small aliased buffer, or if BackUp gets
  // called.
  int i_ = 0;
  // The number of bytes we've been given.
  int64_t byte_count_ = 0;
  H* const h_;
};

class HashableProto {
 public:
  explicit HashableProto(const google::protobuf::MessageLite& pb)
      : pb_(pb), size_(pb.ByteSizeLong()) {}

  template <typename H>
  friend H AbslHashValue(H h, const HashableProto& hp) {
    HashBuffer buf;
    if (hp.size_ <= buf.size()) {
      if (hp.size_ == 0) {
        return H::combine_contiguous(std::move(h), "", 0);
      }
      ArrayOutputStream to_buf(buf.data(), buf.size());
      // We could optimize for very small messages that fit into a single
      // integer (we'll call it "tiny") by setting the first sizeof(tiny) bytes
      // of buf to zero, then memcpy'ing the serialized bytes into tiny, and
      // then just hashing tiny.
      SerializeProtoTo(hp.pb_, &to_buf);
      return H::combine_contiguous(std::move(h), buf.data(), hp.size_);
    }
    HashingOutputStream<H> hasher(&buf, &h);
    SerializeProtoTo(hp.pb_, &hasher);
    return std::move(hasher).finish();
  }

 private:
  const google::protobuf::MessageLite& pb_;
  const size_t size_;
};

}  // namespace

size_t pb_hash::operator()(const google::protobuf::MessageLite& pb) const {
  HashableProto hp(pb);
  return absl::Hash<HashableProto>{}(hp);
}

namespace {

// BufferedSerialization holds a serialized protocol message with an extra
// "buffer" of contiguous bytes prior to the serialization.  This allows us to
// use the buffer as "scratch space" before comparing bytes.  As bytes are
// compared, we'll advance the cursor through the serialized proto, giving us
// increasingly more bytes to use for comparison.
class BufferedSerializedProto {
  static constexpr size_t kInitialBuf = 512;

 public:
  BufferedSerializedProto(const google::protobuf::MessageLite& pb,
                          size_t pb_size)
      : bytes_(kInitialBuf + pb_size), cmp_(bytes_.data() + kInitialBuf) {
    ArrayOutputStream to_bytes(bytes_.data() + kInitialBuf, pb_size);
    SerializeProtoTo(pb, &to_bytes);
  }

  explicit BufferedSerializedProto(absl::string_view serialized)
      : bytes_(kInitialBuf + serialized.size()),
        cmp_(bytes_.data() + kInitialBuf) {
    memcpy(bytes_.data() + kInitialBuf, serialized.data(), serialized.size());
  }

  // Returns a pointer to the writable buffer area prior to the serialized
  // message.  Bytes may only be written up to but not including cmp().
  char* buf() { return bytes_.data(); }

  // Returns the pointer to the next byte of the serialized message that must be
  // compared.
  const char* cmp() const { return cmp_; }

  // Compares the given bytes and returns true if they're equal to the bytes of
  // the serialized message.  Advances the cmp() pointer to the next byte that
  // must be compared (thus increasing buf() space).  Returns true if the bytes
  // were equal.
  bool Compare(const char* p, size_t n) {
    if (std::memcmp(cmp_, p, n) != 0) {
      return false;
    }
    cmp_ += n;
    return true;
  }

  // Returns the number of bytes compared so far.
  int64_t ByteCount() const { return cmp_ - bytes_.data() - kInitialBuf; }

 private:
  absl::FixedArray<char, 2 * kInitialBuf> bytes_;
  const char* cmp_;
};

// ComparingOutputStream serializes a given message, leaving a little spare room
// in front so it can use the same buffer to serialize both messages.  The
// second message is serialized in increasingly large chunks as it is compared
// to the first message (and the bytes of the first message become unnecessary).
class ComparingOutputStream final : public ZeroCopyOutputStream {
 public:
  explicit ComparingOutputStream(BufferedSerializedProto* buf) : buf_(buf) {}

  // REQUIRES: pb.ByteSizeLong() is the same as the message that was serialized
  // into buf_.  We do no additional bounds checking.
  bool Equals(const google::protobuf::MessageLite& pb) {
    // ZeroCopyOutputStream has no Flush/Close API, so SerializeProtoTo doesn't
    // know whether the last operation succeeded; we may still have outstanding
    // bytes to compare when it returns.
    return SerializeProtoTo(pb, this) && CompareOutstanding();
  }

 private:
  // We don't need access to any of these overridden methods in this file
  // directly, so there's no reason to make them public.
  bool Next(void** data, int* size) override {
    if (!CompareOutstanding()) {
      return false;
    }
    *data = buf_->buf();
    *size = outstanding_ = buf_->cmp() - buf_->buf();
    return true;
  }

  void BackUp(int count) override { outstanding_ -= count; }

  int64_t ByteCount() const override { return buf_->ByteCount(); }

  bool WriteAliasedRaw(const void* void_data, int size) override {
    return CompareOutstanding() &&
           buf_->Compare(static_cast<const char*>(void_data), size);
  }

  bool AllowsAliasing() const override { return true; }

  bool WriteCord(const absl::Cord& cord) override {
    if (!CompareOutstanding()) {
      return false;
    }
    for (absl::string_view chunk : cord.Chunks()) {
      if (!buf_->Compare(chunk.data(), chunk.size())) {
        return false;
      }
    }
    return true;
  }

  // Compares outstanding bytes, if any.
  bool CompareOutstanding() {
    if (outstanding_ == 0) {
      return true;
    }
    const size_t n = outstanding_;
    outstanding_ = 0;
    return buf_->Compare(buf_->buf(), n);
  }

  BufferedSerializedProto* const buf_;
  // The number of bytes outstanding (i.e., handed out via Next()).
  size_t outstanding_ = 0;
};

}  // namespace

bool pb_equals::operator()(const google::protobuf::MessageLite& x,
                           const google::protobuf::MessageLite& y) const {
  const size_t size = x.ByteSizeLong();
  if (size != y.ByteSizeLong()) {
    return false;
  }
  BufferedSerializedProto buf(x, size);
  ComparingOutputStream comparer(&buf);
  return comparer.Equals(y);
}

namespace internal_message_hasher {

bool DeterministicSerializationEq(const google::protobuf::MessageLite& pb,
                                  absl::string_view serialized) {
  const size_t size = pb.ByteSizeLong();
  if (size != serialized.size()) {
    return false;
  }
  BufferedSerializedProto buf(serialized);
  ComparingOutputStream comparer(&buf);
  return comparer.Equals(pb);
}

}  // namespace internal_message_hasher

}  // namespace gtl
