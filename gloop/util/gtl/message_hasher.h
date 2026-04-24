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

// Equality and hash functions for protocol buffers that are faster (almost)
// drop-in replacements for MessageDifferencer::Equals. See caveats below.
//
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// The protobuf team recommends against doing generic hashing/fingerprinting
// on messages in general case due to the impossibility to implement it
// correctly in the face of unknown fields. Instead, treating a message
// the same as if it was any other struct and writing out a hash that names
// the fields explicitly is recommended.
//
// There are some cases where a generic hash fn is ok, but if you are handling
// messages that were in any way received from another server, you _will_ have
// unknown fields at some point and it is unfortunately rare to have test
// coverage of that case. Use at your own risk.
//
// See <link> for more details.
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//
// Also defines type aliases for hash containers with protocol buffers as keys.
//
// Usage:
// gtl::pb_flat_hash_set<MyProto> my_hash_set;
// gtl::pb_flat_hash_map<MyProto, int> my_hash_map;
// gtl::pb_node_hash_set<MyProto> my_hash_set;
// gtl::pb_node_hash_map<MyProto, int> my_hash_map;
// gtl::pb_linked_hash_set<MyProto> my_hash_set;
// gtl::pb_linked_hash_map<MyProto, int> my_hash_map;
//
// Caveats:
// 1) You probably shouldn't use this library if your protocol buffers might
// contain unknown fields, because unknown fields are serialized in an
// undefined order and the hash function below relies on serialization.
//
// 2) The equality function used by the pb_hash_* containers is Equals,
// not Equivalent.  (See message_differencer.h for definitions of Equals and
// Equivalent).  This may not be what you want/need.  (The comparison is
// actually accomplished by comparing serialized forms byte-for-byte as this
// is faster than MessageDifferencer::Equals() but produces the same result.)
//
// 3) Because these are based on serialization, there are some cases involving
// doubles where they may differ from MessageDifferencer::Equals. Specifically,
// comparisons involving NaN and -0.0 may return different results from
// MessageDifferencer::Equals.
//
// Note: for performance, if you never inspect the hash map keys, consider using
// `absl::flat_hash_map<OpaquePbHashKey<MyProto>, Value>` instead of
// `gtl::pb_flat_hash_map<MyProto, Value>`. The former is faster and more cache
// friendly.

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_MESSAGE_HASHER_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_MESSAGE_HASHER_H_

#include <stddef.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-W#warnings"
#include <ext/hash_map>
#pragma clang diagnostic pop
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/linked_hash_map.h"
#include "absl/container/linked_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"
#include "absl/hash/hash.h"
#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/message_lite.h"

namespace gtl {

namespace internal_message_hasher {
std::string SerializeDeterministically(const google::protobuf::MessageLite& pb);

bool DeterministicSerializationEq(const google::protobuf::MessageLite& pb,
                                  absl::string_view serialized);
}  // namespace internal_message_hasher

struct pb_equals {
  bool operator()(const google::protobuf::MessageLite& x,
                  const google::protobuf::MessageLite& y) const;
};

// Deprecated. Use the more accurate name gtl::pb_equals instead.
using pb_equiv ABSL_DEPRECATE_AND_INLINE() = pb_equals;

struct pb_hash {
  size_t operator()(const google::protobuf::MessageLite& pb) const;
};

template <class ProtoBuff>
using pb_flat_hash_set = ::absl::flat_hash_set<ProtoBuff, pb_hash, pb_equals>;

template <class ProtoBuff>
using pb_node_hash_set = ::absl::node_hash_set<ProtoBuff, pb_hash, pb_equals>;

template <class ProtoBuff, class Data>
using pb_flat_hash_map =
    ::absl::flat_hash_map<ProtoBuff, Data, pb_hash, pb_equals>;

template <class ProtoBuff, class Data>
using pb_node_hash_map =
    ::absl::node_hash_map<ProtoBuff, Data, pb_hash, pb_equals>;

template <class ProtoBuff>
using pb_linked_hash_set = absl::linked_hash_set<ProtoBuff, pb_hash, pb_equals>;

template <class ProtoBuff, class Data>
using pb_linked_hash_map =
    absl::linked_hash_map<ProtoBuff, Data, pb_hash, pb_equals>;

// This class can be used as an opaque hash table key when you don't need to
// inspect the keys (i.e., the hash table is only used for lookups or
// insertions of values).
// Instead of storing the original proto, it stores a serialized version of the
// proto, which has two advantages:
//   - It's typically much faster as it avoids having to serialize keys in the
//     equality checking phase of hash table lookups/insertions, which can save
//     up to 1 - 3 serializations per look-up.
//   - Keys are stored in a contiguous block of memory, which is more cache
//     friendly.
//
// Usage:
//   absl::flat_hash_map<OpaquePbHashKey<MyProto>, Value> my_map;
//
//   // Heterogeneous lookups make the code redable.
//   MyProto my_proto;
//   my_map[my_proto] = 3;
//   if (const auto it = my_map.find(my_proto); it != my_map.end()) {
//     // Do something with it->second.
//   }
template <typename Proto>
class OpaquePbHashKey {
 public:
  // Note: this class does not require `pb` to outlive it.
  explicit OpaquePbHashKey(const Proto& pb)
      : serialized_(internal_message_hasher::SerializeDeterministically(pb)),
        cached_hash_(absl::HashOf(serialized_)) {}

  struct absl_container_hash {
    using is_transparent = void;

    size_t operator()(const OpaquePbHashKey& k) const { return k.cached_hash_; }

    size_t operator()(const Proto& k) const { return pb_hash{}(k); }
  };

  struct absl_container_eq {
    using is_transparent = void;

    bool operator()(const OpaquePbHashKey& l, const OpaquePbHashKey& r) const {
      return l == r;
    }

    bool operator()(const Proto& l, const OpaquePbHashKey& r) const {
      return internal_message_hasher::DeterministicSerializationEq(
          l, r.serialized_);
    }

    bool operator()(const OpaquePbHashKey& l, const Proto& r) const {
      return internal_message_hasher::DeterministicSerializationEq(
          r, l.serialized_);
    }
  };

  // Returns a copy of the original proto.
  Proto Deserialize() const {
    Proto pb;
    CHECK(pb.ParseFromString(serialized_));
    return pb;
  }

  // AbslHashValue and operator== are needed to support OpaquePbHashKey as part
  // of a compound key in hashtables, e.g. flat_hash_set<std::pair<int,
  // OpaquePbHashKey<MyProto>>>.
  template <typename H>
  friend H AbslHashValue(H h, const OpaquePbHashKey& key) {
    return H::combine(std::move(h), key.cached_hash_);
  }
  friend bool operator==(const OpaquePbHashKey& lhs,
                         const OpaquePbHashKey& rhs) {
    return lhs.serialized_ == rhs.serialized_;
  }

 private:
  std::string serialized_;
  size_t cached_hash_;
};

template <typename Proto>
OpaquePbHashKey(Proto) -> OpaquePbHashKey<Proto>;

#if !defined(_LIBCPP_VERSION) && defined(_WIN32)
template <class ProtoBuff, class Data>
class ABSL_DEPRECATED(
    "Use gtl::pb_flat_hash_map or gtl::pb_node_hash_map instead") pb_hash_map
    : public std::hash_map<ProtoBuff, Data, pb_hash, pb_equals> {
  using std::hash_map<ProtoBuff, Data, pb_hash, pb_equals>::hash_map;
};
#else
template <class ProtoBuff, class Data>
class ABSL_DEPRECATED(
    "Use gtl::pb_flat_hash_map or gtl::pb_node_hash_map instead") pb_hash_map
    : public __gnu_cxx::hash_map<ProtoBuff, Data, pb_hash, pb_equals> {
  using __gnu_cxx::hash_map<ProtoBuff, Data, pb_hash, pb_equals>::hash_map;
};
#endif

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_MESSAGE_HASHER_H_
