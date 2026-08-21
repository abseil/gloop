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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_STRING_HASH_SET_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_STRING_HASH_SET_H_

#include <cstring>
#include <memory>
#include <ostream>
#include <type_traits>

#include "absl/container/internal/container_memory.h"
#include "absl/container/internal/hash_function_defaults.h"
#include "absl/container/internal/node_slot_policy.h"
#include "absl/container/internal/raw_hash_set.h"  // IWYU pragma: export
#include "absl/strings/string_view.h"

namespace gtl {
namespace internal_string_hash_set {
struct Policy;
}  // namespace internal_string_hash_set

// string_hash_set<> is an unordered container that serves the same purpose as
// std::unordered_set<string>. It has an API closer to
// std::unordered_set<string_view> while owning the strings inserted into it.
// See cppreference.com for documentation:
//
// http://en.cppreference.com/w/cpp/container/unordered_set
//
// In addition the API deviates from that of std::unordered_set<string_view> to
// allow for a more efficient implementation. The primary difference in the API
// is that string_hash_set<>::value_type isn't an instance of string or
// string_view. Instead, it's a non-copyable type with the following public
// member functions:
//
//   absl::string_view get() const;
//   operator absl::string_view() const;
//
//
// MEMORY USAGE
//
// Each element of string_hash_set<> resides in its own allocation with the
// following layout:
//
//   +--------+---------------+
//   | size_t |  string bytes |
//   +--------+---------------+
//
// In addition to the above string_hash_set<> incurs an overhead of
// sizeof(void*) * (load_factor() + 1) bytes per element, or on average
// sizeof(void*) * 1.8.
//
// On a 64-bit platform string_hash_set uses 25 to 40 bytes less than
// std::unordered_set<string> per element, depending on the string sizes,
// assuming load_factor() for both maps is the same.
//
// RECOMMENDATIONS
//
// Prefer absl::flat_hash_set by default (see <link>). This container
// should only be used when benchmarks and profiles indicate that it is a
// performance win.
template <
    class Hash = absl::container_internal::hash_default_hash<absl::string_view>,
    class Eq = absl::container_internal::hash_default_eq<absl::string_view>,
    class Allocator = std::allocator<absl::string_view>>
class string_hash_set
    : public absl::container_internal::InstantiateRawHashSet<
          internal_string_hash_set::Policy, Hash, Eq, Allocator>::type {
  using Base = typename string_hash_set::raw_hash_set;

 public:
  string_hash_set() = default;
  using Base::Base;

  // TODO: Reexport all members that take a value to take
  // absl::string_view directly to avoid redundant calls strlen when passing a
  // const char*.
};

namespace internal_string_hash_set {

class Node {
  static absl::string_view get(const Node& n) { return n.get(); }
  static absl::string_view get(absl::string_view s) { return s; }

 public:
  explicit Node(size_t size) : size_(size) {}
  ~Node() = delete;
  Node(Node&&) = delete;

  operator absl::string_view() const { return get(); }
  absl::string_view get() const { return absl::string_view(data(), size_); }

#define GTL_DEFINE_NODE_COMPARISON_OP(OP)                           \
  friend bool operator OP(const Node& lhs, const Node& rhs) {       \
    return get(lhs) OP get(rhs);                                    \
  }                                                                 \
  friend bool operator OP(const Node& lhs, absl::string_view rhs) { \
    return get(lhs) OP get(rhs);                                    \
  }                                                                 \
  friend bool operator OP(absl::string_view lhs, const Node& rhs) { \
    return get(lhs) OP get(rhs);                                    \
  }
  GTL_DEFINE_NODE_COMPARISON_OP(==)  // NOLINT
  GTL_DEFINE_NODE_COMPARISON_OP(!=)  // NOLINT
  GTL_DEFINE_NODE_COMPARISON_OP(<=)  // NOLINT
  GTL_DEFINE_NODE_COMPARISON_OP(>=)  // NOLINT
  GTL_DEFINE_NODE_COMPARISON_OP(<)   // NOLINT
  GTL_DEFINE_NODE_COMPARISON_OP(>)   // NOLINT
#undef GTL_DEFINE_NODE_COMPARISON_OP
  friend std::ostream& operator<<(std::ostream& out, const Node& rhs) {
    return out << rhs.get();
  }

 private:
  friend struct Policy;

  const char* data() const {
    return reinterpret_cast<const char*>(this) + sizeof(Node);
  }
  char* data() { return reinterpret_cast<char*>(this) + sizeof(Node); }

  template <class Alloc>
  static Node* Construct(Alloc* alloc, absl::string_view str) {
    void* mem = absl::container_internal::Allocate<alignof(Node)>(
        alloc, sizeof(Node) + str.size());
    Node* node = new (mem) Node(str.size());
    memcpy(node->data(), str.data(), node->size_);
    return node;
  }

  template <class Alloc>
  static void Destroy(Alloc* alloc, Node* node) {
    size_t size = sizeof(Node) + node->size_;
    absl::container_internal::Deallocate<alignof(Node)>(alloc, node, size);
  }

  size_t size_;
  // Followed by string bytes.
};

struct Policy : absl::container_internal::node_slot_policy<Node&, Policy> {
  using key_type = absl::string_view;
  using init_type = absl::string_view;
  using constant_iterators = std::true_type;
  using element_is_owner = std::true_type;

  using DefaultHash =
      absl::container_internal::hash_default_hash<absl::string_view>;
  using DefaultEq =
      absl::container_internal::hash_default_eq<absl::string_view>;
  using DefaultAlloc = std::allocator<absl::string_view>;

  template <class Allocator, class... Args>
  static Node* new_element(Allocator* alloc, Args&&... args) {
    return Node::Construct(alloc,
                           absl::string_view(std::forward<Args>(args)...));
  }

  template <class Allocator>
  static void delete_element(Allocator* alloc, Node* node) {
    Node::Destroy(alloc, node);
  }

  template <class F>
  static auto apply(F&& f, absl::string_view s)
      -> decltype(std::forward<F>(f)(s, s)) {
    return std::forward<F>(f)(s, s);
  }

  template <class Hash, bool kIsDefault, size_t kSeedShift = 0>
  static constexpr absl::container_internal::HashSlotFn get_hash_slot_fn() {
    return nullptr;
  }

  static size_t element_space_used(const Node* node) {
    if (node == nullptr) return ~size_t{};
    return sizeof(Node) + node->size_;
  }
};

}  // namespace internal_string_hash_set

namespace subtle {

// This is string_hash_set<...>::value_type.
using string_hash_set_node = internal_string_hash_set::Node;

}  // namespace subtle
}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_STRING_HASH_SET_H_
