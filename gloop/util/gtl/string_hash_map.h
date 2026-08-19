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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_STRING_HASH_MAP_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_STRING_HASH_MAP_H_

#include <cstdint>
#include <cstring>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/container/internal/container_memory.h"
#include "absl/container/internal/hash_function_defaults.h"
#include "absl/container/internal/layout.h"
#include "absl/container/internal/node_slot_policy.h"
#include "absl/container/internal/raw_hash_map.h"  // IWYU pragma: export
#include "absl/container/node_hash_map.h"
#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"

namespace gtl {
namespace internal_string_hash_map {
template <class Value>
struct Policy;
}  // namespace internal_string_hash_map

// Do not use string_hash_map unless benchmarks/loadtests show it is justified.
// See <link> & https://abseil.io/tips/136 for advice about choosing unordered
// containers.
//
// string_hash_map<V> is an unordered container that serves the same purpose as
// std::unordered_map<string, V>. It has an API closer to
// std::unordered_map<string_view, V> while owning the strings inserted into it.
// See cppreference.com for documentation:
//
// http://en.cppreference.com/w/cpp/container/unordered_map
//
// In addition the API deviates from that of std::unordered_map<string_view, V>
// to allow for a more efficient implementation. The primary difference in the
// API is that string_hash_map<V>::value_type isn't an instance of std::pair.
// Instead, it's a non-copyable type with the following public member functions:
//
//   absl::string_view key() const;
//   const V& value() const;
//   V& value();
//
// The key and value can also be accessed via a tuple-like interface:
//
//   using std::get;
//   absl::string_view key = get<0>(kv);
//   V& val = get<1>(kv);
//
// A more subtle difference is that insert(x) and emplace(x) don't work if x
// is neither an instance of std::pair nor string_hash_map<V>::value_type.
//
// MEMORY USAGE
//
// Each element of string_hash_map<V> resides in its own allocation with the
// following layout:
//
//   +------------------------+-------+--------------+
//   | size_t (string length) |   V   | string bytes |
//   +------------------------+-------+--------------+
//
// In addition to the above string_hash_map<V> incurs an overhead of
// (sizeof(void*) + 1)*bucket_count(), or about (sizeof(void*)+1) * 1.5 per
// entry on average.
//
// On a 64-bit platform string_hash_map<V> uses 25 to 40 bytes less than
// std::unordered_map<string, V> per element, depending on the string sizes,
// assuming load_factor() for both maps is the same.
//
// RECOMMENDATIONS
//
// Prefer absl::flat_hash_map by default (see <link>). This container
// should only be used when benchmarks and profiles indicate that it is a
// performance win.
template <class Value,
          class Hash = internal_string_hash_map::Policy<Value>::DefaultHash,
          class Eq = internal_string_hash_map::Policy<Value>::DefaultEq,
          class Allocator =
              internal_string_hash_map::Policy<Value>::DefaultAlloc>
class string_hash_map
    : public absl::container_internal::InstantiateRawHashMap<
          internal_string_hash_map::Policy<Value>, Hash, Eq, Allocator>::type {
  using Base = typename string_hash_map::raw_hash_map;

 public:
  string_hash_map() = default;
  using Base::Base;

  // TODO: Reexport all members that take a value to take
  // absl::string_view directly to avoid redundant calls strlen when passing a
  // const char*.
};

namespace internal_string_hash_map {

template <class Value>
class Node {
 public:
  static_assert(!std::is_reference_v<Value>, "");

  using first_type = absl::string_view;
  using second_type = Value;
  using key_type = absl::string_view;
  using mapped_type = Value;

  explicit Node(size_t size) : size_(size) {}
  ~Node() = delete;
  Node(Node&&) = delete;

  absl::string_view key() const {
    // The assume enables the compiler to elide an unnecessary hardening check
    // from libc++ (http://shortn/_HYJBVTI2oN).
    ABSL_ASSUME(size_ <= static_cast<size_t>(PTRDIFF_MAX));
    return absl::string_view(data() + NodeOffsets::kKeyOffset, size_);
  }

  Value& value() & {
    return *reinterpret_cast<Value*>(data() + NodeOffsets::kValueOffset);
  }
  const Value& value() const& {
    return *reinterpret_cast<const Value*>(data() + NodeOffsets::kValueOffset);
  }
  Value&& value() && { return std::move(value()); }
  const Value&& value() const&& { return std::move(value()); }

  friend bool operator==(const Node& lhs, const Node& rhs) {
    return Node::Equal(lhs.key(), lhs.value(), rhs.key(), rhs.value());
  }
  template <class K, class U>
  friend bool operator==(const Node& lhs, const std::pair<K, U>& rhs) {
    return Node::Equal(lhs.key(), lhs.value(), rhs.first, rhs.second);
  }
  template <class K, class U>
  friend bool operator==(const std::pair<K, U>& lhs, const Node& rhs) {
    return Node::Equal(lhs.first, lhs.second, rhs.key(), rhs.value());
  }
  friend bool operator==(const Node& lhs,
                         const std::pair<absl::string_view, Value>& rhs) {
    return Node::Equal(lhs.key(), lhs.value(), rhs.first, rhs.second);
  }
  friend bool operator==(const std::pair<absl::string_view, Value>& lhs,
                         const Node& rhs) {
    return Node::Equal(lhs.first, lhs.second, rhs.key(), rhs.value());
  }

  friend bool operator<(const Node& lhs, const Node& rhs) {
    return Node::Less(lhs.key(), lhs.value(), rhs.key(), rhs.value());
  }
  template <class K, class U>
  friend bool operator<(const Node& lhs, const std::pair<K, U>& rhs) {
    return Node::Less(lhs.key(), lhs.value(), rhs.first, rhs.second);
  }
  template <class K, class U>
  friend bool operator<(const std::pair<K, U>& lhs, const Node& rhs) {
    return Node::Less(lhs.first, lhs.second, rhs.key(), rhs.value());
  }
  friend bool operator<(const Node& lhs,
                        const std::pair<absl::string_view, Value>& rhs) {
    return Node::Less(lhs.key(), lhs.value(), rhs.first, rhs.second);
  }
  friend bool operator<(const std::pair<absl::string_view, Value>& lhs,
                        const Node& rhs) {
    return Node::Less(lhs.first, lhs.second, rhs.key(), rhs.value());
  }

  friend bool operator!=(const Node& lhs, const Node& rhs) {
    return !(lhs == rhs);
  }
  template <class K, class U>
  friend bool operator!=(const Node& lhs, const std::pair<K, U>& rhs) {
    return !(lhs == rhs);
  }
  template <class K, class U>
  friend bool operator!=(const std::pair<K, U>& lhs, const Node& rhs) {
    return !(lhs == rhs);
  }
  friend bool operator!=(const Node& lhs,
                         const std::pair<absl::string_view, Value>& rhs) {
    return !(lhs == rhs);
  }
  friend bool operator!=(const std::pair<absl::string_view, Value>& lhs,
                         const Node& rhs) {
    return !(lhs == rhs);
  }

  friend bool operator>(const Node& lhs, const Node& rhs) { return rhs < lhs; }
  template <class K, class U>
  friend bool operator>(const Node& lhs, const std::pair<K, U>& rhs) {
    return rhs < lhs;
  }
  template <class K, class U>
  friend bool operator>(const std::pair<K, U>& lhs, const Node& rhs) {
    return rhs < lhs;
  }
  friend bool operator>(const Node& lhs,
                        const std::pair<absl::string_view, Value>& rhs) {
    return rhs < lhs;
  }
  friend bool operator>(const std::pair<absl::string_view, Value>& lhs,
                        const Node& rhs) {
    return rhs < lhs;
  }

  friend bool operator<=(const Node& lhs, const Node& rhs) {
    return !(rhs < lhs);
  }
  template <class K, class U>
  friend bool operator<=(const Node& lhs, const std::pair<K, U>& rhs) {
    return !(rhs < lhs);
  }
  template <class K, class U>
  friend bool operator<=(const std::pair<K, U>& lhs, const Node& rhs) {
    return !(rhs < lhs);
  }
  friend bool operator<=(const Node& lhs,
                         const std::pair<absl::string_view, Value>& rhs) {
    return !(rhs < lhs);
  }
  friend bool operator<=(const std::pair<absl::string_view, Value>& lhs,
                         const Node& rhs) {
    return !(rhs < lhs);
  }

  friend bool operator>=(const Node& lhs, const Node& rhs) {
    return !(lhs < rhs);
  }
  template <class K, class U>
  friend bool operator>=(const Node& lhs, const std::pair<K, U>& rhs) {
    return !(lhs < rhs);
  }
  template <class K, class U>
  friend bool operator>=(const std::pair<K, U>& lhs, const Node& rhs) {
    return !(lhs < rhs);
  }
  friend bool operator>=(const Node& lhs,
                         const std::pair<absl::string_view, Value>& rhs) {
    return !(lhs < rhs);
  }
  friend bool operator>=(const std::pair<absl::string_view, Value>& lhs,
                         const Node& rhs) {
    return !(lhs < rhs);
  }

 private:
  template <class V>
  friend struct Policy;

  using Layout = absl::container_internal::Layout<Node, Value, char>;

  // NodeOffsets exists to make kValueOffset constexpr (it can't be in Node
  // because it depends on Node being complete).  This lets the debugger
  // evaluate kValueOffset during pretty-printing.
  class NodeOffsets {
   public:
    static constexpr size_t kValueOffset =
        Layout::Partial(1).template Offset<1>();
    static constexpr size_t kKeyOffset =
        Layout::Partial(1, 1).template Offset<2>();
  };

  const char* data() const { return reinterpret_cast<const char*>(this); }
  char* data() { return reinterpret_cast<char*>(this); }

  // Args is a tuple holding the constructor arguments of Value.
  template <class Alloc, class Args>
  static Node* Construct(Alloc* alloc, absl::string_view key, Args&& args) {
    auto layout = MakeLayout(key.size());
    void* mem = absl::container_internal::Allocate<Layout::Alignment()>(
        alloc, layout.AllocSize());
    Node* node = new (mem) Node(key.size());
    absl::container_internal::ConstructFromTuple(alloc, &node->value(),
                                                 std::forward<Args>(args));
    memcpy(layout.template Pointer<2>(node->data()), key.data(), key.size());
    return node;
  }

  template <class Alloc>
  static void Destroy(Alloc* alloc, Node* node) {
    absl::allocator_traits<Alloc>::destroy(*alloc, &node->value());
    absl::container_internal::Deallocate<Layout::Alignment()>(
        alloc, node, MakeLayout(node->size_).AllocSize());
  }

  template <class K1, class V1, class K2, class V2>
  static bool Equal(const K1& k1, const V1& v1, const K2& k2, const V2& v2) {
    return k1 == k2 && v1 == v2;
  }

  template <class K1, class V1, class K2, class V2>
  static bool Less(const K1& k1, const V1& v1, const K2& k2, const V2& v2) {
    return k1 < k2 || (!(k2 < k1) && v1 < v2);
  }

  static Layout MakeLayout(size_t size) { return Layout(1, 1, size); }

  size_t size_;
};

template <size_t I>
using Int = std::integral_constant<size_t, I>;

template <class N>
auto NodeGet(Int<0>, N&& n) -> decltype(std::forward<N>(n).key()) {
  return std::forward<N>(n).key();
}

template <class N>
auto NodeGet(Int<1>, N&& n) -> decltype(std::forward<N>(n).value()) {
  return std::forward<N>(n).value();
}

template <std::size_t I, class T>
auto get(Node<T>& n) -> decltype(NodeGet(Int<I>(), n)) {  // NOLINT
  return NodeGet(Int<I>(), n);
}

template <std::size_t I, class T>
auto get(const Node<T>& n) -> decltype(NodeGet(Int<I>(), n)) {
  return NodeGet(Int<I>(), n);
}

template <std::size_t I, class T>
auto get(Node<T>&& n) -> decltype(NodeGet(Int<I>(), std::move(n))) {
  return NodeGet(Int<I>(), std::move(n));
}

template <std::size_t I, class T>
auto get(const Node<T>&& n) -> decltype(NodeGet(Int<I>(), std::move(n))) {
  return NodeGet(Int<I>(), std::move(n));
}

template <class... Ts>
decltype(absl::container_internal::PairArgs(std::declval<Ts>()...)) NodeArgs(
    Ts&&... ts) {
  return absl::container_internal::PairArgs(std::forward<Ts>(ts)...);
}

template <class Value>
std::pair<std::tuple<absl::string_view>, std::tuple<const Value&>> NodeArgs(
    const Node<Value>& node) {
  return {std::piecewise_construct, std::forward_as_tuple(node.key()),
          std::forward_as_tuple(node.value())};
}

template <class Value>
std::pair<std::tuple<absl::string_view>, std::tuple<Value&&>> NodeArgs(
    Node<Value>&& node) {
  return {std::make_tuple(node.key()),
          std::forward_as_tuple(std::move(node.value()))};
}

template <class Value>
struct Policy
    : absl::container_internal::node_slot_policy<Node<Value>&, Policy<Value>> {
  using key_type = absl::string_view;
  using mapped_type = Value;
  using init_type = std::pair</*non const*/ key_type, mapped_type>;
  using element_is_owner = std::true_type;

  using DefaultHash =
      absl::container_internal::hash_default_hash<absl::string_view>;
  using DefaultEq =
      absl::container_internal::hash_default_eq<absl::string_view>;
  using DefaultAlloc =
      std::allocator<std::pair<const absl::string_view, Value>>;

  template <class Alloc, class... Args>
  static Node<Value>* new_element(Alloc* alloc, Args&&... args) {
    auto p = internal_string_hash_map::NodeArgs(std::forward<Args>(args)...);
    return absl::container_internal::WithConstructed<absl::string_view>(
        std::move(p.first), [&](absl::string_view key) {
          return Node<Value>::Construct(alloc, key, std::move(p.second));
        });
  }

  template <class Alloc>
  static void delete_element(Alloc* alloc, Node<Value>* node) {
    Node<Value>::Destroy(alloc, node);
  }

  // Note: not SFINAE-friendly because the non-deconstructing overload of
  // emplace() won't work anyway (new_element() will fail).
  template <class F, class... Args>
  static decltype(std::declval<F>()(absl::string_view())) apply(
      F&& f, Args&&... args) {
    auto p = internal_string_hash_map::NodeArgs(std::forward<Args>(args)...);
    return absl::container_internal::WithConstructed<absl::string_view>(
        std::move(p.first), [&](absl::string_view key) {
          return std::forward<F>(f)(key, std::piecewise_construct,
                                    std::make_tuple(key), std::move(p.second));
        });
  }

  template <class Hash, bool kIsDefault, size_t kSeedShift = 0>
  static constexpr absl::container_internal::HashSlotFn get_hash_slot_fn() {
    return nullptr;
  }

  static size_t element_space_used(const Node<Value>* node) {
    if (node == nullptr) return ~size_t{};
    return Node<Value>::MakeLayout(node->size_).AllocSize();
  }

  static Value& value(Node<Value>* node) { return node->value(); }
  static const Value& value(const Node<Value>* node) { return node->value(); }
};

}  // namespace internal_string_hash_map

namespace subtle {

// This is string_hash_map<V, ...>::value_type.
template <class V>
using string_hash_map_node = internal_string_hash_map::Node<V>;

}  // namespace subtle
}  // namespace gtl

namespace std {

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
// TODO: workaround for mismatched tags in libstdc++.
#endif

template <typename V>
struct tuple_size<gtl::internal_string_hash_map::Node<V>>
    : std::integral_constant<std::size_t, 2> {};

template <typename V>
struct tuple_element<0, gtl::internal_string_hash_map::Node<V>> {
  using type = absl::string_view;
};

template <typename V>
struct tuple_element<1, gtl::internal_string_hash_map::Node<V>> {
  using type = V;
};

#ifdef __clang__
#pragma clang diagnostic pop
#endif

}  // namespace std

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_STRING_HASH_MAP_H_
