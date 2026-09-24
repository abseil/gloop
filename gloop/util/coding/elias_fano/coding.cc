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

#include "gloop/util/coding/elias_fano/coding.h"

#include <cstdint>
#include <ios>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/config.h"
#include "absl/base/optimization.h"
#include "absl/container/internal/layout.h"  // NOLINT(build/include)
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/numeric/bits.h"
#include "absl/numeric/internal/bits.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/source_location.h"
#include "absl/types/span.h"
#include "gloop/util/coding/coder.h"
#include "gloop/util/coding/elias_fano/options.h"
#include "gloop/util/coding/varint.h"
#include "gloop/util/endian/endian.h"

#ifdef ABSL_HAVE_ADDRESS_SANITIZER
// Uncomment to enable asan poisoning.
// #define ENABLE_ASAN_POISONING
#endif

#ifdef ENABLE_ASAN_POISONING
#include <sanitizer/asan_interface.h>
#endif

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <array>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/strings/str_format.h"
#include "gloop/util/status/ret_check.h"
#include "gloop/util/status/status_builder.h"
#include "gloop/util/status/status_macros.h"

namespace elias_fano {

using ::absl::container_internal::Aligned;
using ::absl::container_internal::Layout;

namespace {

// The layout of the elias fano encoding.
//
// The low and high bits need 7 bytes of trailing addressable space. Version 1
// put sparse indexes at the end of the encoding in the hope to have those 7
// bytes without allocating extra. Later on the encoding was optimized to remove
// unused sparse index entries and it turns out that sparse entries are rarely
// used. Version 2 puts all indexes at the end, which pretty much guarantees
// the existence of the 7 trailing bytes.
//
using L1 = Layout<uint8_t,                           // metadata
                  Aligned<internal::DenseEntry, 4>,  // one_dense
                  Aligned<internal::DenseEntry, 4>,  // zero_dense
                  uint8_t,                           // high
                  uint8_t,                           // low
                  Aligned<internal::SparseEntry, 4>  // sparse
                  >;

using L2 = Layout<uint8_t,               // metadata
                  uint8_t,               // high
                  uint8_t,               // low
                  internal::DenseEntry,  // one_dense
                  internal::DenseEntry,  // zero_dense
                  internal::SparseEntry  // sparse
                  >;

L1 MakeL1(const Options& opts, size_t sparse_size) {
  DCHECK_EQ(opts.version(), 1);

  return L1(Options::EncodingLength(opts), opts.one_dense_size(),
            opts.zero_dense_size(), opts.high_bytes(), opts.low_bytes(),
            sparse_size);
}

L2 MakeL2(const Options& opts, size_t sparse_size) {
  DCHECK_GE(opts.version(), 2);

  return L2(Options::EncodingLength(opts), opts.high_bytes(), opts.low_bytes(),
            opts.one_dense_size(), opts.zero_dense_size(), sparse_size);
}

size_t AllocSizeWithSlop(const L1& layout) {
  // high and low array need 7 bytes of addressable memory past their end. low
  // array comes after high so we need 7 bytes past low. If sparse index is
  // longer than 7 bytes we don't need padding. Otherwise add enough to have 7
  // bytes.
  return std::max(layout.AllocSize(),
                  layout.Offset<4>() + layout.Size<4>() + 7);
}

size_t AllocSizeWithSlop(const L2& layout) {
  // high and low array need 7 bytes of addressable memory past their end. low
  // array comes after high so we need 7 bytes past low. If the total amount of
  // index space is more than 7 bytes we don't need padding. Otherwise add
  // enough to have 7 bytes.
  return std::max(layout.AllocSize(),
                  layout.Offset<2>() + layout.Size<2>() + 7);
}

// Find the number of sparse entries of the elias-fano encoding. Each sparse
// block of Options::kD1 is fully loaded, so we find the highest base and add
// Options::kD1.
size_t NumSparseEntries(const Options& opts, const internal::DenseEntry* dense1,
                        const internal::DenseEntry* dense0) {
  auto num_sparse = [](const internal::DenseEntry* dense, size_t num_dense) {
    // Because of the way we build indexes the last sparse entry will be found
    // as the last entry in either zeros or ones indexes. To speed things up we
    // scan the indexes backwards and bail out on first hit.
    while (num_dense-- > 0) {
      if (dense[num_dense].delta[0].Load() != 0) {
        return dense[num_dense].base.Load() + Options::kD1;
      }
    }
    return 0u;
  };
  return std::max(num_sparse(dense1, opts.one_dense_size()),
                  num_sparse(dense0, opts.zero_dense_size()));
}

size_t EncodedSize(const Options& opts, size_t num_sparse) {
  return opts.version() == 1 ? AllocSizeWithSlop(MakeL1(opts, num_sparse))
                             : AllocSizeWithSlop(MakeL2(opts, num_sparse));
}

const char* EncodedStart(const Options& opts,
                         const internal::DenseEntry* dense1) {
  return reinterpret_cast<const char*>(dense1) -
         (opts.version() == 1 ? MakeL1(opts, 0).Offset<1>()
                              : MakeL2(opts, 0).Offset<3>());
}

bool IsProperlyAligned(absl::Span<const char> span) {
  if (span.empty()) return true;
  size_t align =
      std::min(size_t{1} << (absl::bit_width(span.size()) - 1), size_t{8});
  return reinterpret_cast<uintptr_t>(span.data()) % align == 0;
}

size_t MinEncodingBytes(const Options& opts) {
  auto layout = MakeL2(opts, /*sparse_size=*/0);
  VLOG(2) << layout.DebugString();
  return AllocSizeWithSlop(layout);
}

size_t MaxEncodingBytes(const Options& opts) {
  auto layout = MakeL2(opts, opts.max_sparse_size());
  VLOG(2) << layout.DebugString();
  return AllocSizeWithSlop(layout);
}

Options GetOptions(uint32_t size, uint64_t max_val, Options::Index index,
                   Options::Version version) {
  return Options(size, max_val).version(version).indexes(index);
}

}  // namespace

size_t EncodedSize(absl::Span<const char> span) {
  auto info_or = GetInfo(span);
  return info_or.ok() ? info_or->actual_bytes : 0;
}

std::string Info::DebugString() const {
  auto percent = [=](size_t s) { return 100. * s / actual_bytes; };
  return absl::StrFormat(
      "%s [bytes:%d (meta:%d(%.1f%%) dense:%d(%.1f%%) sparse:%d(%.1f%%) "
      "high:%d(%.1f%%) low:%d(%.1f%%) max:%d(%.1f%%))]",
      opts.DebugString(), actual_bytes, meta_bytes, percent(meta_bytes),
      dense_index_bytes, percent(dense_index_bytes), sparse_index_bytes,
      percent(sparse_index_bytes), high_bytes, percent(high_bytes), low_bytes,
      percent(low_bytes), max_bytes, percent(max_bytes));
}

absl::StatusOr<Info> GetInfo(absl::Span<const char> span) {
  Info res;
  ASSIGN_OR_RETURN(res.opts, Options::Decode(span));
  res.max_bytes = Encoder::MaxEncodingBytes(res.opts);
  switch (res.opts.version()) {
    case 1: {
      auto pointers =
          MakeL1(res.opts, res.opts.max_sparse_size()).Pointers(span.data());
      size_t num_sparse_entries = NumSparseEntries(
          res.opts, std::get<1>(pointers), std::get<2>(pointers));
      auto layout = MakeL1(res.opts, num_sparse_entries);
      res.actual_bytes = AllocSizeWithSlop(layout);
      auto sizes = layout.Sizes();
      res.meta_bytes = sizes[0];
      res.high_bytes = sizes[3];
      res.low_bytes = sizes[4];
      res.dense_index_bytes =
          (sizes[1] + sizes[2]) * sizeof(L1::ElementType<1>);
      res.sparse_index_bytes = sizes[5] * sizeof(L1::ElementType<5>);
      break;
    }
    case 2:
    case 3: {
      auto pointers =
          MakeL2(res.opts, res.opts.max_sparse_size()).Pointers(span.data());
      size_t num_sparse_entries = NumSparseEntries(
          res.opts, std::get<3>(pointers), std::get<4>(pointers));
      auto layout = MakeL2(res.opts, num_sparse_entries);
      res.actual_bytes = AllocSizeWithSlop(layout);
      auto sizes = layout.Sizes();
      res.meta_bytes = sizes[0];
      res.high_bytes = sizes[1];
      res.low_bytes = sizes[2];
      res.dense_index_bytes =
          (sizes[3] + sizes[4]) * sizeof(L2::ElementType<3>);
      res.sparse_index_bytes = sizes[5] * sizeof(L2::ElementType<5>);
      break;
    }
    default:
      return absl::UnimplementedError(absl::StrCat(
          "Version ", res.opts.version(), " does not support GetInfo() yet"));
  }
  return res;
}

class Encoder::HighLowBitsGuard {
 public:
#ifdef ENABLE_ASAN_POISONING
  explicit HighLowBitsGuard(Encoder* encoder) : encoder_(encoder) {
    ASAN_POISON_MEMORY_REGION(encoder_->high_, encoder_->opts_.high_bytes());
    ASAN_POISON_MEMORY_REGION(encoder_->low_, encoder_->opts_.low_bytes());
  }
  ~HighLowBitsGuard() {
    ASAN_UNPOISON_MEMORY_REGION(encoder_->high_, encoder_->opts_.high_bytes());
    ASAN_UNPOISON_MEMORY_REGION(encoder_->low_, encoder_->opts_.low_bytes());
  }
#else
  explicit HighLowBitsGuard(Encoder* encoder) {}
#endif  // ENABLE_ASAN_POISONING
 private:
  Encoder* encoder_;
};

size_t Encoder::MinEncodingBytes(const Options& opts) {
  return elias_fano::MinEncodingBytes(opts);
}

size_t Encoder::MaxEncodingBytes(const Options& opts) {
  auto r = elias_fano::MaxEncodingBytes(opts);
  DCHECK_LE(MinEncodingBytes(opts), r);
  return r;
}

Encoder::Encoder(const Options& opts, absl::Span<char> span) : opts_(opts) {
  CHECK_GE(span.size(), MaxEncodingBytes(opts))  // Crash OK
      << "span must be at least MaxEncodingLength(opts) long";
  // We zero initialize everything because memory sanitizers are not smart
  // enough to check how many bits we use from each loaded word. In a better
  // world we can zero initialize the high bytes only.
  memset(span.data(), 0, span.size());

  low_mask_ = (uint64_t{1} << opts_.width()) - 1;
  uint8_t* meta;
  std::tie(meta, high_, low_, dense1_, dense0_, sparse_) =
      MakeL2(opts_, opts_.max_sparse_size()).Pointers(span.data());
  DCHECK_EQ(static_cast<void*>(meta), static_cast<void*>(span.data()));
  Options::Encode(opts_, span);
  DCHECK_LE(static_cast<void*>(meta), static_cast<void*>(dense1_));
}

void Encoder::Encode56(Index i, uint64_t value) {
  if (opts_.width() == 0) return;
  DCHECK_EQ(0, value & ~low_mask_)
      << "Value " << std::hex << value << " cannot be represented in "
      << std::dec << opts_.width() << " bits";
  size_t pos = size_t{i.value()} * opts_.width();
  uint8_t* ptr = &low_[pos / 8];
  uint64_t word = LittleEndian::Load64(ptr);
  word &= ~(low_mask_ << (pos % 8));
  word |= value << (pos % 8);
  LittleEndian::Store64(ptr, word);
}

bool Encoder::GetBit(BitPos pos) const {
  return high_[pos.value() / 8] & (1 << (pos.value() % 8));
}

void Encoder::SetBit(BitPos pos) {
  high_[pos.value() / 8] |= 1 << (pos.value() % 8);
}

void Encoder::UnsetBit(BitPos pos) {
  high_[pos.value() / 8] &= ~(1 << (pos.value() % 8));
}

void Encoder::Append(uint64_t v) {
  DCHECK_GE(v, last_value_);
  DCHECK_LE(v, opts_.max_value());
  DCHECK_NE(size_, opts_.size());
  BitPos pos(size_ + (v >> opts_.width()));
  SetBit(pos);
  Encode56(Index(size_++), v & low_mask_);
  last_value_ = v;
}

template <bool One>
absl::Status Encoder::BuildIndex(internal::DenseEntry* dense,
                                 uint32_t* sparse_size) {
  if (!One) {
    if (opts_.zero_bits() == 0) return absl::OkStatus();
    // Unset last bit. Now this zero bit is a sentinel after which we never
    // look.
    UnsetBit(BitPos(opts_.bits()));
  }

  uint32_t i = 0;  // dense index
  std::vector<uint64_t> deltas;
  deltas.reserve(Options::kD1);
  uint64_t byte = 0;
  uint64_t high = One ? LittleEndian::Load64(&high_[byte])
                      : ~LittleEndian::Load64(&high_[byte]);
  auto next_pos = [&]() {
    while (!high) {
      byte += 8;
      high = One ? LittleEndian::Load64(&high_[byte])
                 : ~LittleEndian::Load64(&high_[byte]);
    }
    auto res = BitPos(
        byte * 8 + absl::numeric_internal::CountTrailingZeroesNonzero64(high));
    high &= high - 1;
    return res;
  };

  BitPos base = next_pos();
  for (BitPos pos = base; pos != BitPos(opts_.bits()); pos = next_pos()) {
    HighLowBitsGuard guard(this);
    if (deltas.size() == Options::kD1) {
      RETURN_IF_ERROR(WriteDeltas(base, deltas, dense, i, sparse_size));
      ++i;
      base = pos;
      deltas.clear();
    }
    deltas.push_back((pos - base).value());
  }
  HighLowBitsGuard guard(this);
  // Write the last D1 deltas.
  deltas.resize(Options::kD1, opts_.bits() - base.value());
  // Set last bit again.
  if (!One) SetBit(BitPos(opts_.bits()));
  return WriteDeltas(base, deltas, dense, i, sparse_size);
}

absl::Status Encoder::WriteDeltas(BitPos base,
                                  const std::vector<uint64_t>& deltas,
                                  internal::DenseEntry* dense,
                                  uint32_t dense_idx, uint32_t* sparse_size) {
  DCHECK_EQ(deltas.size(), Options::kD1);
  internal::DenseEntry* entry = &dense[dense_idx];
  DCHECK_GE(base.value(), dense_idx * Options::kD1);
  if (deltas.back() < Options::kR) {
    base -= BitPos{dense_idx * Options::kD1};
    RET_CHECK_LE(base.value(), std::numeric_limits<uint32_t>::max())
        << opts_.DebugString();
    entry->base.Store(base.value());
    DVLOG(2) << "writing dense: base=" << entry->base.Load() << " deltas=["
             << absl::StrJoin(deltas, ", ") << "]";
    // Write each D2-th occurrence.
    for (int i = 0; i < deltas.size() / Options::kD2; i++)
      // TODO: In v4 write deltas[i * Options::kD2] - i * Options::kD2.
      // Then we can cutoff at more than Options::kR.
      entry->delta[i].Store(deltas[i * Options::kD2]);
  } else {
    entry->base.Store(*sparse_size);
    DVLOG(2) << "writing sparse: base=" << entry->base.Load() << " deltas=["
             << absl::StrJoin(deltas, ", ") << "]";
    // Marking the first delta as non-zero to denote we are using the sparse
    // index.
    entry->delta[0].Store(std::numeric_limits<uint16_t>::max());
    // Write position of each occurrence in the sparse index.
    auto max_sparse_size = opts_.max_sparse_size();
    for (int i = 0; i != Options::kD1; i++) {
      // We're currently describing rank'th one or zero (depending on index).
      uint64_t rank = dense_idx * Options::kD1 + i;
      RET_CHECK_LE(base.value() + deltas[i] - rank,
                   std::numeric_limits<uint32_t>::max())
          << opts_.DebugString();
      sparse_[(*sparse_size)++].Store(base.value() + deltas[i] - rank);
      DCHECK_LE(*sparse_size, max_sparse_size);
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<size_t> Encoder::Finalize() {
  RET_CHECK_EQ(actual_bytes_, 0) << "Finalize() called more than once";
  RET_CHECK_EQ(last_value_, opts_.max_value());
  RET_CHECK_EQ(size_, opts_.size());
  // Add an extra one bit past the end of the unary stream. This makes
  // iterator implementation simpler.
  SetBit(BitPos(opts_.bits()));
  uint32_t sparse_size = 0;
  if (opts_.has_index(Options::ONES))
    RETURN_IF_ERROR(BuildIndex<true>(dense1_, &sparse_size));
  if (opts_.has_index(Options::ZEROS))
    RETURN_IF_ERROR(BuildIndex<false>(dense0_, &sparse_size));
  actual_bytes_ = EncodedSize(opts_, sparse_size);
  if (VLOG_IS_ON(1)) {
    const absl::StatusOr<Info> info =
        GetInfo({EncodedStart(opts_, dense1_), actual_bytes_});
    if (info.ok()) {
      VLOG(1) << info->DebugString();
    } else {
      VLOG(1) << "GetInfo failed: " << info.status();
    }
  }
  return actual_bytes_;
}

Encoder2::Encoder2(Options::Index index, Options::Version version)
    : version_(version), index_(index) {
  CHECK_EQ(version, Options::VERSION3);
  CHECK_GE(index, Options::NONE);
  CHECK_LE(index, Options::ALL);
}

void Encoder2::Reset() {
  vals_enc_.reset();
  num_vals_ = 0;
  min_delta_ = std::numeric_limits<uint64_t>::max();
  max_val_ = 0;
}

bool Encoder2::Append(absl::Span<const uint64_t> values) {
  if (values.empty()) return true;

  auto enc_val = [this](uint64_t val) {
    vals_enc_.Ensure(Varint::Length64(val));
    vals_enc_.put_varint64(val);
  };

  if (ABSL_PREDICT_FALSE(num_vals_ == 0)) {
    max_val_ = values.front();
    enc_val(max_val_);
    ++num_vals_;
    values = values.subspan(1);
  }

  for (uint64_t v : values) {
    if (v < max_val_) return false;
    const auto delta = v - max_val_;
    min_delta_ = std::min(min_delta_, delta);
    enc_val(delta);
    max_val_ = v;
  }
  num_vals_ += values.size();

  return true;
}

absl::StatusOr<std::string> Encoder2::FinalizeToStr(size_t max_wasted_space) {
  auto opts = GetOptions(num_vals_, max_val_, index_, version_);

  std::string encoded;
  absl::strings_internal::STLStringResizeUninitialized(
      &encoded, elias_fano::Encoder::MaxEncodingBytes(opts));
  Encoder encoder(opts, absl::MakeSpan(encoded));

  uint64_t value = 0;
  ::Decoder dec(vals_enc_.base(), vals_enc_.length());
  for (size_t i = 0; i < num_vals_; ++i) {
    uint64_t delta = 0;
    RET_QCHECK(dec.get_varint64(&delta));
    value += delta;

    encoder.Append(value);
  }
  vals_enc_.reset();

  ASSIGN_OR_RETURN(size_t size, encoder.Finalize());
  encoded.resize(size);

  if (encoded.capacity() - size > max_wasted_space) {
    encoded.shrink_to_fit();
  }

  return encoded;
}

size_t Encoder2::MaxEncodingBytes(uint32_t size, uint64_t max_value,
                                  Options::Index index,
                                  Options::Version version) {
  return elias_fano::MaxEncodingBytes(
      GetOptions(size, max_value, index, version));
}

std::string Encoder2::DebugString() const {
  return GetOptions(size(), max_value(), index_, version_).DebugString();
}

Decoder::Decoder(absl::Span<const char> span, Options opts) : opts_(opts) {
  const uint8_t* meta;
  if (opts_.version() == 1) {
    std::tie(meta, dense1_, dense0_, high_, low_, sparse_) =
        MakeL1(opts_, opts_.max_sparse_size()).Pointers(span.data());
  } else {
    std::tie(meta, high_, low_, dense1_, dense0_, sparse_) =
        MakeL2(opts_, opts_.max_sparse_size()).Pointers(span.data());
  }
}

absl::StatusOr<Decoder> Decoder::Make(absl::Span<const char> span) {
  ASSIGN_OR_RETURN(auto opts, Options::Decode(span));
  if (opts.version() <= 1 && !IsProperlyAligned(span))
    return util::InvalidArgumentErrorBuilder(ABSL_LOC)
           << "span is not properly aligned";
  return Decoder(span, opts);
}

std::pair<BitPos, BitPos> Decoder::SelectSparse(uint32_t base, uint32_t rank,
                                                bool next,
                                                BitPos hint_pos) const {
  uint32_t offset = rank % Options::kD1;
  uint32_t s_i = base + offset;
  if (opts_.version() <= 2) {
    return {BitPos{sparse_[s_i].Load()},
            next ? BitPos{sparse_[s_i + 1].Load()} : BitPos{0}};
  } else {
    uint32_t s_i = base + offset;
    return {BitPos{uint64_t{sparse_[s_i].Load()} + rank},
            next ? BitPos{uint64_t{sparse_[s_i + 1].Load()} + rank + 1}
                 : BitPos{0}};
  }
}

ForwardDecoder::ForwardDecoder(absl::Span<const char> span, Options opts)
    : opts_(opts) {
  low_mask_ = (uint64_t{1} << opts_.width()) - 1;
  const uint8_t* meta;
  if (opts_.version() == 1) {
    std::tie(meta, dense1_, dense0_, high_, low_, sparse_) =
        MakeL1(opts_, opts_.max_sparse_size()).Pointers(span.data());
  } else {
    std::tie(meta, high_, low_, dense1_, dense0_, sparse_) =
        MakeL2(opts_, opts_.max_sparse_size()).Pointers(span.data());
  }
  Reset();
}

absl::StatusOr<ForwardDecoder> ForwardDecoder::Make(
    absl::Span<const char> span) {
  ASSIGN_OR_RETURN(auto opts, Options::Decode(span));
  if (opts.version() <= 1 && !IsProperlyAligned(span))
    return util::InvalidArgumentErrorBuilder(ABSL_LOC)
           << "span is not properly aligned";
  return ForwardDecoder(span, opts);
}

absl::Span<const char> Decoder::RawBytes() const {
  return {EncodedStart(opts_, dense1_),
          EncodedSize(opts_, NumSparseEntries(opts_, dense1_, dense0_))};
}

absl::Span<const char> ForwardDecoder::RawBytes() const {
  return {EncodedStart(opts_, dense1_),
          EncodedSize(opts_, NumSparseEntries(opts_, dense1_, dense0_))};
}

}  // namespace elias_fano
