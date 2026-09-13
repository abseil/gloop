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

#include "gloop/strings/cord_bytestream.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/numeric/bits.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "gloop/strings/bytestream.h"

ABSL_FLAG(int, copy_sharing_threshold, 512,
          "DEPRECATED flag for diagnosing memory problems. "
          "When copying from a databuffer/cord to another databuffer/cord "
          "blindly copy if the size is less than this flag. "
          "Otherwise, try to share blocks between the source and "
          "destination.");

namespace strings {

void CordByteSink::AppendExternalMemory(
    absl::string_view data, void* absl_nullable arg,
    void (*absl_nonnull releaser)(void* absl_nullable)) {
  dest_->Append(
      absl::MakeCordFromExternal(data, [arg, releaser]() { releaser(arg); }));
}

size_t CordByteSink::MinAppendExternalMemoryLength() const {
  return absl::cord_internal::kMaxBytesToCopy + 1;
}

strings::TypeId CordByteSink::GetTypeId() const {
  return strings::TypeId::For<CordByteSink>();
}

CordReader::~CordReader() {}

bool CordReader::BtreeAdvance() {
  size_t available = length_ - used_;
  if (available) {
    absl::string_view data = btree_reader_.Next();
    size_t sz = (std::min)(available, data.size());
    used_ += sz;
    current_chunk_ = {data.data(), sz};
    return true;
  }
  return false;
}

void CordReader::BtreeSkipSlowPath(size_t n) {
  assert(n >= current_chunk_.size());
  current_chunk_ = btree_reader_.Skip(n - current_chunk_.size());
  used_ = length_ - btree_reader_.remaining();
}

absl::Cord CordReader::BtreeReadCord(size_t n) {
  using absl::cord_internal::CordRep;
  using absl::cord_internal::CordRepBtree;
  absl::Cord cord;
  auto constexpr method = absl::Cord::CordzUpdateTracker::kCordReader;

  assert(n <= Available());

  CordRep* tree;
  current_chunk_ = btree_reader_.Read(n, current_chunk_.size(), tree);
  cord.contents_.EmplaceTree(tree, method);
  used_ = length_ - btree_reader_.remaining();
  return cord;
}

bool CordReader::ReadFragment(absl::string_view* absl_nonnull result) {
  if (current_chunk_.empty()) {
    if (!Advance()) return false;
  }
  *result = current_chunk_;
  current_chunk_ = {};
  return true;
}

void CordReader::ReadNSlowPath(size_t n, char* absl_nonnull dst) {
  assert(n <= Available());

  do {
    absl::string_view fragment = CordReader::Peek();
    size_t avail = std::min<size_t>(n, fragment.size());
    memcpy(dst, fragment.data(), avail);
    dst += avail;
    n -= avail;
    current_chunk_.remove_prefix(avail);
  } while (n != 0);
}

bool CordReader::Read32SlowPath(uint32_t* absl_nonnull result) {
  if (Available() < sizeof(*result)) {
    return false;
  }

  ReadN(sizeof(*result), reinterpret_cast<char*>(result));
  if constexpr (absl::endian::native != absl::endian::little) {
    *result = absl::byteswap(*result);
  }
  return true;
}

bool CordReader::Read64(uint64_t* absl_nonnull result) {
  if (current_chunk_.size() >= sizeof(*result)) {
    memcpy(result, current_chunk_.data(), sizeof(*result));
    if constexpr (absl::endian::native != absl::endian::little) {
      *result = absl::byteswap(*result);
    }
    current_chunk_.remove_prefix(sizeof(*result));
    return true;
  } else if (Available() < sizeof(*result)) {
    return false;
  } else {
    ReadN(sizeof(*result), reinterpret_cast<char*>(result));
    if constexpr (absl::endian::native != absl::endian::little) {
      *result = absl::byteswap(*result);
    }
    return true;
  }
}

absl::Cord CordReader::ReadCord(size_t n) {
  if (ABSL_PREDICT_FALSE(n == 0)) {
    return absl::Cord();
  }
  const size_t available_length = Available();
  if (n > available_length) {
    LOG(ERROR) << "CordReader::ReadCord() overran input.";
    n = available_length;
  }

  // Fast path if the entire source Cord is returned.
  if (n == length_) {
    // Advance to the end.
    used_ = length_;
    current_chunk_ = {};
    return cord();
  }

  // sub_cord is to store Subcord(Position(), n). Instead of directly
  // calling cord_->Subcord(Position(), n), the following implementation
  // uses CordReader internals to speed up the process.
  absl::Cord sub_cord;

  if (n <= absl::Cord::InlineRep::kMaxInline) {
    ReadN(n, sub_cord.contents_.set_data(n));
    return sub_cord;
  }

  if (btree_reader_) {
    return BtreeReadCord(n);
  }

  assert(current_edge_ != nullptr);
  const size_t offset = length_ - available_length;
  CordRep* tree = absl::cord_internal::CordRepSubstring::Substring(
      current_edge_, offset, n);
  sub_cord.contents_.EmplaceTree(tree,
                                 absl::Cord::CordzUpdateTracker::kCordReader);
  current_chunk_.remove_prefix(n);
  return sub_cord;
}

void CordReader::CopyToCord(CordByteSink* absl_nonnull sink, size_t n) {
  sink->cord()->Append(ReadCord(n));
}

void CordReader::CopyToWithSharing(strings::ByteSink* absl_nonnull sink,
                                   size_t n) {
  // We can share memory instead of copying via sink->AppendExternalMemory.
  while (n > 0) {
    if (current_chunk_.empty()) {
      if (!Advance()) {
        LOG(ERROR) << "CordReader::CopyTo() overran input.";
        break;
      }
    }

    // For CordRepBtree we obtain the `CordRepBtreeReader::node` property
    // which holds the current data edge inside the reader. Otherwise, we use
    // `current_edge_` which will be non null if we read from a Cord with a
    // single data edge, or nullptr if we are dealing with an inlined cord.
    const size_t fragment_size = std::min(n, current_chunk_.size());
    CordRep* node = btree_reader_ ? btree_reader_.node() : current_edge_;
    if (fragment_size >= sink->MinAppendExternalMemoryLength() &&
        node != nullptr) {
      // Reading from flat memory, and it is a candidate for
      // passing to AppendExternalMemory.
      assert(absl::cord_internal::IsDataEdge(node));

      absl::string_view mem(current_chunk_.data(), fragment_size);
      sink->AppendExternalMemory(
          mem, absl::Cord::CordRep::Ref(node), [](void* arg) {
            absl::Cord::CordRep::Unref(static_cast<absl::Cord::CordRep*>(arg));
          });
    } else {
      sink->Append(current_chunk_.data(), fragment_size);
    }
    current_chunk_.remove_prefix(fragment_size);
    n -= fragment_size;
  }
}

void CordReader::CopyTo(strings::ByteSink* absl_nonnull sink, size_t n) {
  assert(n <= Available());
  if (static_cast<int64_t>(n) >= absl::GetFlag(FLAGS_copy_sharing_threshold)) {
    if (sink->GetTypeId() == strings::TypeId::For<CordByteSink>()) {
      CopyToCord(static_cast<CordByteSink*>(sink), n);
    } else {
      CopyToWithSharing(sink, n);
    }
  } else {
    strings::ByteSource::CopyTo(sink, n);
  }
}

}  // namespace strings
