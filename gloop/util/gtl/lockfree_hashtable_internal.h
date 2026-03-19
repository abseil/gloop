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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_LOCKFREE_HASHTABLE_INTERNAL_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_LOCKFREE_HASHTABLE_INTERNAL_H_

// IWYU pragma: private
// IWYU pragma: friend util/gtl/lockfree_hashmap.h
// IWYU pragma: friend util/gtl/lockfree_hashset.h

#include <stdlib.h>

#include <atomic>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/meta/type_traits.h"
#include "absl/synchronization/mutex.h"
#include "gloop/util/bits/bits.h"
#include "gloop/util/gtl/compressed_tuple.h"
#include "gloop/util/gtl/hashtable_common.h"  // for sh_is_transparent

namespace gtl {
namespace internal_lockfree_hashtable {

template <typename PolicyType, typename Hasher, typename EqualTo>
class alignas(ABSL_CACHELINE_SIZE) LockFreeHashTable {
  using ExtractKey = typename PolicyType::SelectKey;

 public:
  using value_type = typename PolicyType::value_type;
  using key_type = typename PolicyType::key_type;
  using reference = value_type&;
  using const_reference = const value_type&;
  using pointer = value_type*;
  using const_pointer = const value_type*;
  using hasher = Hasher;
  using key_equal = EqualTo;
  using size_type = size_t;
  using difference_type = ptrdiff_t;

 private:
  // Implementation strategy:
  //
  // LockFreeHashTable is a chained hash table with an internal mutex.  Mutating
  // operations, such as insert() and erase() are serialized using the mutex,
  // just like a normal hash table. find() runs without a lock.  It instead
  // synchronizes with background mutations using atomic memory operations.
  //
  // When a <key,value> pair is first inserted in the table, a ValueNode is
  // inserted.  Two nodes hashing to the same hash bucket will be chained
  // using Node::link.
  //
  // When the hash array becomes too loaded, we create a new array twice the
  // size of the original, and add the existing nodes to to the new array.
  // However, because a concurrent find() may be still accessing the old array,
  // we cannot remove nodes from the old array. Thus, for each node in the
  // original array:
  //
  //   - If node->link==nullptr and the node is the last element in the new
  //     array (that is, node->link can remain a null pointer also in the new
  //     array), we just share the node with the old and the new array.
  //     Node::num_refs keeps the number of times the node is shared and is only
  //     accessed under the table's mutex.
  //
  //   - Otherwise, we create a new ForwardingNode that points to the node,
  //     and add it to the new array.
  //
  //   Assuming hash conflicts are infrequent, most nodes will fall into the
  //   first category.
  //
  // This design provides the following properties:
  //
  //  - The address of a particular <key,value> pair remains constant once it
  //    is inserted into the hash table. That is, an iterator remains valid
  //    until the object is erased.
  //
  //  - Node::link is only written before insert or on erase.  This allows
  //    readers to do a relaxed read and always get something valid.
  struct Node {
    Node(Node* link, bool v) : link(link), has_value(v), num_refs(0) {}
    // Chains nodes in the same hash bucket.
    std::atomic<Node*> link;

    // has_value==1 <=> this node is ValueNode.
    // has_value==0 <=> this node is ForwardingNode
    const bool has_value;

    // Number of references from Arrays.
    int num_refs;
  };

  struct ValueNode : public Node {
    template <typename... Args>
    explicit ValueNode(Args&&... args)
        : Node(nullptr, true), value(std::forward<Args>(args)...) {}

    value_type value;
  };

  struct ForwardingNode : public Node {
    ForwardingNode(Node* n, ValueNode* vn) : Node(n, false), value_node(vn) {
      DCHECK(vn->has_value);
    }
    ValueNode* const value_node;
  };

  static void UnrefNode(Node* n) {
    DCHECK_GT(n->num_refs, 0);
    if (n->num_refs > 1) {
      --n->num_refs;
    } else if (n->has_value) {
      delete static_cast<ValueNode*>(n);
    } else {
      UnrefNode(GetValueNode(n));
      delete static_cast<ForwardingNode*>(n);
    }
  }

  static ValueNode* GetValueNode(Node* n) {
    return (n->has_value ? static_cast<ValueNode*>(n)
                         : static_cast<ForwardingNode*>(n)->value_node);
  }

  static const ValueNode* GetValueNode(const Node* n) {
    return (n->has_value ? static_cast<const ValueNode*>(n)
                         : static_cast<const ForwardingNode*>(n)->value_node);
  }

  // Elements are placed in Array, which is a chained hash table, When an
  // Array becomes full, we allocate a new Array with twice the size and copy
  // Nodes over. The old Array is kept in old_arrays_ to prevent threads from
  // accessing deleted memory.
  struct Array {
    size_t max_size;             // always a power of two
    size_t hash_mask;            // max_size - 1
    std::atomic<Node*> data[1];  // Variable-size array of max_size.
  };

 public:
  struct supports_heterogeneous
      : std::integral_constant<bool, sh_is_transparent<hasher>::value &&
                                         sh_is_transparent<key_equal>::value> {
  };

  class iterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = typename LockFreeHashTable::value_type;
    using reference = absl::conditional_t<PolicyType::constant_iterators::value,
                                          const value_type&, value_type&>;
    using pointer = absl::remove_reference_t<reference>*;
    using difference_type = typename LockFreeHashTable::difference_type;

    friend class LockFreeHashTable;
    iterator() { array_ = nullptr; }
    iterator(Array* a, size_t i, Node* n) : array_(a), index_(i), node_(n) {}

    bool operator==(const iterator& iter) const { return node_ == iter.node_; }
    bool operator!=(const iterator& iter) const { return node_ != iter.node_; }

    iterator& operator++() {
      // The link list is installed into Array at its maximum length (with all
      // nodes completely visible).  Erase operations can trim nodes from the
      // list, but new nodes are never inserted into the middle of the list,
      // so we can safely use relaxed operation and always see a valid ordering
      // (either before or after the erase).
      Node* link = node_->link.load(std::memory_order_relaxed);
      if (link != nullptr) {
        node_ = link;
        return *this;
      }

      // Find the next nonempty Array entry.
      for (;;) {
        ++index_;
        if (index_ >= array_->max_size) {
          node_ = nullptr;
          return *this;
        }
        Node* p = array_->data[index_].load(std::memory_order_acquire);
        if (p != nullptr) {
          node_ = p;
          return *this;
        }
      }
    }

    iterator operator++(int) {
      iterator result(*this);
      ++(*this);
      return result;
    }

    pointer operator->() const { return &GetValueNode(node_)->value; }

    reference operator*() const { return GetValueNode(node_)->value; }

   private:
    Array* array_;
    size_t index_;
    Node* node_;
  };

  class const_iterator {
   public:
    using iterator_category = typename iterator::iterator_category;
    using value_type = typename LockFreeHashTable::value_type;
    using reference = typename LockFreeHashTable::const_reference;
    using pointer = typename LockFreeHashTable::const_pointer;
    using difference_type = typename LockFreeHashTable::difference_type;
    friend class LockFreeHashTable;
    const_iterator() { array_ = nullptr; }
    const_iterator(const Array* a, size_t i, Node* n)
        : array_(a), index_(i), node_(n) {}
    const_iterator(const typename LockFreeHashTable::iterator& src) {  // NOLINT
      array_ = src.array_;
      index_ = src.index_;
      node_ = src.node_;
    }

    bool operator==(const const_iterator& iter) const {
      return node_ == iter.node_;
    }
    bool operator!=(const const_iterator& iter) const {
      return node_ != iter.node_;
    }

    const_iterator& operator++() {
      Node* link = node_->link.load(std::memory_order_relaxed);
      if (link != nullptr) {
        node_ = link;
        return *this;
      }

      // Find the next nonempty Array entry.
      for (;;) {
        ++index_;
        if (index_ >= array_->max_size) {
          node_ = nullptr;
          return *this;
        }
        const Node* p = array_->data[index_].load(std::memory_order_acquire);
        if (p != nullptr) {
          node_ = p;
          return *this;
        }
      }
    }

    const_iterator operator++(int) {
      const_iterator result(*this);
      ++(*this);
      return result;
    }

    pointer operator->() const { return &GetValueNode(node_)->value; }

    reference operator*() const { return GetValueNode(node_)->value; }

   private:
    const Array* array_;
    size_t index_;
    const Node* node_;
  };

  LockFreeHashTable()
      : LockFreeHashTable(kInitialArraySize, Hasher(), EqualTo()) {}

  explicit LockFreeHashTable(size_t initial_size)
      : LockFreeHashTable(initial_size, Hasher(), EqualTo()) {}

  LockFreeHashTable(size_t initial_array_size, const Hasher& h,
                    const EqualTo& e)
      : size_(0),
        array_hasher_equal_to_and_extract_key_(
            AllocateArray(RoundUpToPowerOfTwo(initial_array_size)), h, e,
            ExtractKey{}) {}
  LockFreeHashTable(const LockFreeHashTable&) = delete;
  LockFreeHashTable& operator=(const LockFreeHashTable&) = delete;

  // The destructor isn't thread safe (obviously). The caller must ensure that
  // there's no other thread accessing the hash table.
  ~LockFreeHashTable() {
    Array* arr = AcquireArray();
    FreeArray(arr, arr->max_size);
    for (size_t i = 0; i < old_arrays_.size(); ++i) {
      FreeArray(old_arrays_[i], old_arrays_[i]->max_size);
    }
    for (size_t i = 0; i < old_nodes_.size(); ++i) {
      UnrefNode(old_nodes_[i]);
    }
  }

 protected:
  hasher hash_function() const {
    return array_hasher_equal_to_and_extract_key_.template get<1>();
  }
  key_equal key_eq() const {
    return array_hasher_equal_to_and_extract_key_.template get<2>();
  }

  iterator begin() { return Begin(AcquireArray()); }
  iterator end() { return End(AcquireArray()); }

  const_iterator begin() const { return ConstBegin(AcquireArray()); }
  const_iterator end() const { return ConstEnd(AcquireArray()); }

  size_t size() const { return size_.load(std::memory_order_acquire); }
  bool empty() const { return size() == 0; }

  iterator find(const key_type& key) { return FindInternal(key); }
  template <class K, typename = typename std::enable_if<
                         supports_heterogeneous::value, K>::type>
  iterator find(const K& key) {
    return FindInternal(key);
  }

  const_iterator find(const key_type& key) const { return FindInternal(key); }
  template <class K, typename = typename std::enable_if<
                         supports_heterogeneous::value, K>::type>
  const_iterator find(const K& key) const {
    return FindInternal(key);
  }

  template <class K = key_type>
  bool contains(const K& key) const {
    return find(key) != end();
  }

  std::pair<iterator, bool> insert(const value_type& entry) {
    return InsertInternal(GetExtractKey()(entry),
                          [&entry] { return new ValueNode(entry); });
  }
  std::pair<iterator, bool> insert(value_type&& entry) {
    return InsertInternal(GetExtractKey()(entry),
                          [&entry] { return new ValueNode(std::move(entry)); });
  }

  template <typename... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    auto* n = new ValueNode(std::forward<Args>(args)...);
    auto ret = InsertInternal(GetExtractKey()(n->value), [n] { return n; });
    if (!ret.second) delete n;
    return ret;
  }

  template <typename... Args>
  std::pair<iterator, bool> try_emplace(const key_type& k, Args&&... args) {
    return TryEmplaceInternal(k, std::forward<Args>(args)...);
  }
  template <typename... Args>
  std::pair<iterator, bool> try_emplace(key_type&& k, Args&&... args) {
    return TryEmplaceInternal(std::move(k), std::forward<Args>(args)...);
  }
  template <typename K, typename... Args,
            typename =
                typename std::enable_if<supports_heterogeneous::value, K>::type>
  std::pair<iterator, bool> try_emplace(K&& k, Args&&... args) {
    return TryEmplaceInternal(std::forward<K>(k), std::forward<Args>(args)...);
  }

  // erase() takes non-const ref to clear iter and help debugging.
  void erase(iterator& iter) {  // NOLINT
    absl::MutexLock l(lock_);
    EraseLocked(&iter);
  }

  size_t erase(const key_type& key) { return EraseInternal(key); }
  template <class K, typename = typename std::enable_if<
                         supports_heterogeneous::value, K>::type>
  size_t erase(const K& key) {
    return EraseInternal(key);
  }

  std::pair<iterator, iterator> equal_range(const key_type& key) {
    return EqualRangeInternal(key);
  }
  template <class K, typename = typename std::enable_if<
                         supports_heterogeneous::value, K>::type>
  std::pair<iterator, iterator> equal_range(const K& key) {
    return EqualRangeInternal(key);
  }

  std::pair<const_iterator, const_iterator> equal_range(
      const key_type& key) const {
    return EqualRangeInternal(key);
  }
  template <class K, typename = typename std::enable_if<
                         supports_heterogeneous::value, K>::type>
  std::pair<const_iterator, const_iterator> equal_range(const K& key) const {
    return EqualRangeInternal(key);
  }

  void clear() {
    absl::MutexLock l(lock_);
    for (iterator iter = begin(); iter != end();) {
      iterator this_iter = iter;
      ++iter;
      EraseLocked(&this_iter);
    }
  }

  absl::AnyInvocable<void() &&> CreateGC() {
    absl::MutexLock l(lock_);
    std::vector<Array*>* old_arrays = nullptr;
    if (!old_arrays_.empty()) {
      old_arrays = new std::vector<Array*>;
      old_arrays->swap(old_arrays_);
    }
    std::vector<Node*>* old_nodes = nullptr;
    if (!old_nodes_.empty()) {
      old_nodes = new std::vector<Node*>;
      old_nodes->swap(old_nodes_);
    }
    if (old_arrays == nullptr && old_nodes == nullptr) {
      return nullptr;
    }
    return [this, old_arrays, old_nodes]() {
      ReleaseMemory(old_arrays, old_nodes);
    };
  }

  void RunGC() {
    if (auto gc = CreateGC()) {
      std::move(gc)();
    }
  }

  ABSL_DEPRECATE_AND_INLINE()
  std::optional<absl::AnyInvocable<void() &&>> CreateGCIfNecessary() {
    auto gc = CreateGC();
    return gc ? std::make_optional(std::move(gc)) : std::nullopt;
  }

 private:
  std::atomic<Array*>& RefToArray() {
    return array_hasher_equal_to_and_extract_key_.template get<0>();
  }
  const std::atomic<Array*>& RefToArray() const {
    return array_hasher_equal_to_and_extract_key_.template get<0>();
  }
  Array* AcquireArray() const {
    return RefToArray().load(std::memory_order_acquire);
  }
  void ReleaseArray(Array* array) {
    RefToArray().store(array, std::memory_order_release);
  }

  const hasher& GetHashFunction() const {
    return array_hasher_equal_to_and_extract_key_.template get<1>();
  }

  const key_equal& GetKeyEq() const {
    return array_hasher_equal_to_and_extract_key_.template get<2>();
  }

  ExtractKey& GetExtractKey() {
    return array_hasher_equal_to_and_extract_key_.template get<3>();
  }

  const ExtractKey& GetExtractKey() const {
    return array_hasher_equal_to_and_extract_key_.template get<3>();
  }

  static void FreeArray(Array* array, size_t max_size) {
    for (iterator iter = Begin(array); iter != End(array);) {
      Node* node = iter.node_;
      ++iter;
      UnrefNode(node);
    }
    ::operator delete(array, ArraySize(max_size));
  }

  static size_t ArraySize(size_t max_size) {
    return sizeof(Array) + sizeof(std::atomic<Node*>) * (max_size - 1);
  }

  // Create an iterator that points to the end of "a".
  static iterator End(Array* a) { return iterator(a, a->max_size, nullptr); }

  static const_iterator ConstEnd(const Array* a) {
    return const_iterator(a, a->max_size, nullptr);
  }

  // Create an iterator that points to the beginning of "a".
  static iterator Begin(Array* a) {
    for (size_t i = 0; i < a->max_size; ++i) {
      Node* p = a->data[i].load(std::memory_order_acquire);
      if (p != nullptr) {
        return iterator(a, i, p);
      }
    }
    return End(a);
  }

  static const_iterator ConstBegin(const Array* a) {
    return Begin(const_cast<Array*>(a));
  }

  static size_t RoundUpToPowerOfTwo(size_t size) {
    if (size == 0) {
      return 1;
    }
    const int log2_ceil = Bits::Log2Ceiling64(size);
    // Conversion to `int` suppresses a warning for some iOS builds.
    assert(log2_ceil < int{8 * sizeof(size_t)});
    return size_t{1} << log2_ceil;
  }

  size_t HashKey(const key_type& key) const { return GetHashFunction()(key); }

  template <class K, typename = typename std::enable_if<
                         supports_heterogeneous::value, K>::type>
  size_t HashKey(const K& key) const {
    return GetHashFunction()(key);
  }

  template <class K>
  iterator FindInternal(const K& key) {
    return FindInArray(AcquireArray(), HashKey(key), key);
  }

  template <class K>
  const_iterator FindInternal(const K& key) const {
    return FindInArray(AcquireArray(), HashKey(key), key);
  }

  template <typename K, typename... Args>
  std::pair<iterator, bool> TryEmplaceInternal(K&& k, Args&&... args) {
    return InsertInternal(k, [&] {
      return new ValueNode(std::piecewise_construct,
                           std::forward_as_tuple(std::forward<K>(k)),
                           std::forward_as_tuple(std::forward<Args>(args)...));
    });
  }

  template <class K>
  iterator FindInArray(Array* array, size_t hash, const K& key) const {
    size_t h = hash & array->hash_mask;
    // `p` was installed with a `memory_order_release` under the mutex.
    // `p->link` was also installed under the mutex *before* the release of `p`
    // (for all p in our chain), so this single acquire is sufficient to ensure
    // all the reads in our linked list are valid.  A concurrent erase can
    // remove a link from the chain, so we will see either the shorter chain or
    // the full one, but we will still have a *happens before* relationship to
    // the memory at `*p`.
    Node* p = array->data[h].load(std::memory_order_acquire);
    while (p != nullptr &&
           !GetKeyEq()(GetExtractKey()(GetValueNode(p)->value), key)) {
      p = p->link.load(std::memory_order_relaxed);
    }
    if (p != nullptr) return iterator(array, h, p);
    return End(array);
  }

  iterator InsertInArray(Array* array, size_t hash, ValueNode* vn) {
    size_t h = hash & array->hash_mask;

    Node* to_insert;
    Node* p = array->data[h].load(std::memory_order_relaxed);
    if (vn->num_refs == 0) {
      // Nobody else shares this node, which means it was constructed when it
      // was handed to us.  Thus we can write without a barrier and the store
      // when we put it into the array will cover us.
      vn->link.store(p, std::memory_order_relaxed);
      to_insert = vn;
    } else if (p == nullptr &&
               vn->link.load(std::memory_order_relaxed) == nullptr) {
      // Since we don't need to modify *vn, we can share it with other arrays.
      to_insert = vn;
    } else {
      // Create a forwarding pointer to avoid modifying vn->link.
      to_insert = new ForwardingNode(p, vn);
      ++vn->num_refs;
    }
    ++to_insert->num_refs;

    array->data[h].store(to_insert, std::memory_order_release);
    return iterator(array, h, to_insert);
  }

  // `func` is allowed to consume `k` when called.
  template <typename K, typename NodeFunc>
  std::pair<iterator, bool> InsertInternal(const K& k, NodeFunc func) {
    // Handle the case where the key is already in the map without taking the
    // lock. This will slow down the insert case, but since that case will need
    // to do expensive things like acquire the lock and allocate memory, we
    // accept the cost.
    size_t hash = HashKey(k);
    Array* array = AcquireArray();
    iterator iter = FindInArray(array, hash, k);
    if (iter.index_ < array->max_size) {
      return std::make_pair(iter, false);
    }

    absl::MutexLock l(lock_);
    // We could have had a racing insert, so recheck whether the item was
    // inserted before we got the lock.
    array = AcquireArray();
    iter = FindInArray(array, hash, k);
    if (iter.index_ < array->max_size) {
      return std::make_pair(iter, false);
    }

    if (size() >= array->max_size * kMaxLoadFactor) {
      Resize();
      array = AcquireArray();
    }

    // k is now potentially dead to us.
    iter = InsertInArray(array, hash, func());
    size_.fetch_add(1, std::memory_order_release);
    return std::make_pair(iter, true);
  }

  template <class K>
  size_t EraseInternal(const K& key) {
    absl::MutexLock l(lock_);
    iterator iter = find(key);
    if (iter != end()) {
      EraseLocked(&iter);
      return 1;
    }
    return 0;
  }

  template <class K>
  std::pair<iterator, iterator> EqualRangeInternal(const K& key) {
    iterator pos = FindInternal(key);  // either an iterator or end
    if (pos == end()) {
      return std::pair<iterator, iterator>(pos, pos);
    } else {
      const iterator startpos = pos;
      ++pos;
      return std::pair<iterator, iterator>(startpos, pos);
    }
  }

  template <class K>
  std::pair<const_iterator, const_iterator> EqualRangeInternal(
      const K& key) const {
    const_iterator pos = FindInternal(key);  // either an iterator or end
    if (pos == end()) {
      return std::pair<const_iterator, const_iterator>(pos, pos);
    } else {
      const_iterator startpos = pos;
      ++pos;
      return std::pair<const_iterator, const_iterator>(startpos, pos);
    }
  }

  // Removes the item in the map for the corresponding iterator. This works
  // similarly to erase(iterator) but allows for different locking granularity.
  // lock must be held when calling this method. The passed-in iterator is
  // cleared before returning to aid debugging.
  void EraseLocked(iterator* iter) ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    DCHECK(*iter != end());
    CHECK(iter->array_ == AcquireArray()) << "Invalid iterator";
    Array* array = iter->array_;

    // We hold the lock, any previous write to the array also held the lock, we
    // can use a relaxed read here.
    Node* prev = array->data[iter->index_].load(std::memory_order_relaxed);
    if (prev == iter->node_) {
      // The assignment here should not require memory_order_release, since we
      // aren't modifying the Node object.
      array->data[iter->index_].store(
          iter->node_->link.load(std::memory_order_relaxed),
          std::memory_order_relaxed);
    } else {
      // We hold the lock, so there are no racing writers.  We don't delete the
      // removed node and the removed node still points into this list, so a
      // racy read can safely see either state.
      while (prev->link.load(std::memory_order_relaxed) != iter->node_) {
        prev = prev->link.load(std::memory_order_relaxed);
      }
      prev->link.store(iter->node_->link.load(std::memory_order_relaxed),
                       std::memory_order_relaxed);
    }
    old_nodes_.push_back(iter->node_);
    size_.fetch_sub(1, std::memory_order_release);
    // Clear the iterator so that a future use will crash the program.
    *iter = end();
  }

  void Resize() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    Array* old_array = AcquireArray();
    Array* new_array = AllocateArray(old_array->max_size * 2);
    iterator iter = begin();

    // Add Nodes in the old array to the new array.
    while (iter != end()) {
      Node* node = iter.node_;
      ++iter;
      ValueNode* vn = GetValueNode(node);
      size_t hash = HashKey(GetExtractKey()(vn->value));
      InsertInArray(new_array, hash, vn);
    }
    old_arrays_.push_back(old_array);
    ReleaseArray(new_array);
  }

  // Allocate a new empty Array of max_size.
  Array* AllocateArray(size_t max_size) {
    CHECK_GE(max_size, 1u);
    // Make sure that max_size is a power of two.
    CHECK_EQ((max_size - 1) & max_size, 0u);
    auto* p =
        static_cast<Array*>(::operator new(ArraySize(max_size)));  // NOLINT
    p->max_size = max_size;
    p->hash_mask = max_size - 1;
    std::uninitialized_fill_n(p->data, max_size, nullptr);
    return p;
  }

  static void DoNothing() {}

  void ReleaseMemory(std::vector<Array*>* old_arrays,
                     std::vector<Node*>* old_nodes) {
    absl::MutexLock l(lock_);
    if (old_nodes) {
      for (Node* node : *old_nodes) {
        UnrefNode(node);
      }
      delete old_nodes;
    }
    if (old_arrays) {
      for (Array* arr : *old_arrays) {
        FreeArray(arr, arr->max_size);
      }
      delete old_arrays;
    }
  }

  static constexpr size_t kInitialArraySize = 4;
  static constexpr float kMaxLoadFactor = 0.7f;

  //
  // Fields modified by writers
  //
  absl::Mutex lock_;
  std::vector<Array*> old_arrays_;
  std::vector<Node*> old_nodes_;
  std::atomic<size_t> size_;

  //
  // Fields accessed by readers
  //
  alignas(ABSL_CACHELINE_SIZE)
      gtl::CompressedTuple<std::atomic<Array*>, Hasher, EqualTo,
                           ExtractKey> array_hasher_equal_to_and_extract_key_;
};

}  // namespace internal_lockfree_hashtable
}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_LOCKFREE_HASHTABLE_INTERNAL_H_
