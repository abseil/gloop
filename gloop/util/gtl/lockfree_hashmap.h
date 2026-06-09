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

// `LockFreeHashMap<Key, Value>` is a thread-safe hash map that executes
// `find()` without requiring a lock, though mutating operations such as
// `insert()`[*] and `erase()` are serialized using a mutex, just like a normal
// hash table. The downside is that `LockFreeHashMap` does not by default free
// any memory or destroy any keys or values until the destructor is called. You
// can use EpochGC or RCU (<link>++-concurrency#garbage_collection) to free
// memory earlier than that.
//
// The API is a subset of hash_map<>'s.
//
// [*] Insert operations (e.g. insert, emplace, try_emplace) are lockfree if the
// key is already present in the map.
//
// Threading rules:
//
// - find() executes without a lock. find() is linearizable wrt. mutating
//   operations such as insert() or erase(). That is, if insert(key) happens
//   before find(key), then the find is guaranteed to find that value.
//
//   Caution: this class does not protect against a write race after
//   insertion. For example, the below code is wrong, since the value for
//   "key1" is updated concurrently.
//
//   thread1:
//     LockFreeHashMap<int, int> map;
//     iter1 = map.find(key1);
//     if (iter1 != map.end()) ++iter1->second;
//   thread2:
//     iter2 = map.find(key1);
//     if (iter2 != map.end()) ++iter2->second;
//
//   To fix, you must either create a mutex that protects the value of the
//   map or use std::atomic<int> for your value.
//
// - Iteration using begin(), end(), and iterator::operator++ is linearizable
//   wrt mutating operations such as insert() or erase().  That is, it will
//   observe values inserted before begin(). Insertions or deletions made
//   during iteration may or may not be observed by the iterator.
//
// Iterator invalidation semantics:
//
// - insert() and erase() will  invalidate reader-side iterators, in that way
//   the reader may miss elements that otherwise exist or see the elements that
//   do not exist anymore. However, the reader will never see corrupt data.

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_LOCKFREE_HASHMAP_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_LOCKFREE_HASHMAP_H_

#include <type_traits>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "gloop/util/gtl/lockfree_hashtable_internal.h"  // IWYU pragma: export

namespace gtl {
namespace internal_lockfree_hashmap {
template <typename Key, typename Value>
struct LockFreeHashMapPolicy;
}  // namespace internal_lockfree_hashmap

template <typename Key, typename Value,
          typename Hasher = typename absl::flat_hash_map<Key, Value>::hasher,
          typename EqualTo =
              typename absl::flat_hash_map<Key, Value, Hasher>::key_equal>
class LockFreeHashMap
    : public internal_lockfree_hashtable::LockFreeHashTable<
          internal_lockfree_hashmap::LockFreeHashMapPolicy<Key, Value>, Hasher,
          EqualTo> {
  using Base = typename LockFreeHashMap::LockFreeHashTable;

 public:
  using mapped_type = Value;

  // Default constructor creates an empty map.
  LockFreeHashMap() = default;

  // LockFreeHashMap(size_t initial_size);
  //
  // Constructs a map with reserved capacity for at least initial_size elements.
  //
  // LockFreeHashMap(size_t initial_size, Hasher hasher, EqualTo equal_to);
  //
  // Constructs a map with reserved capacity for at least initial_size elements,
  // hasher and equal_to functors.
  using Base::Base;

  // Not copyable or movable.
  LockFreeHashMap(const LockFreeHashMap&) = delete;
  LockFreeHashMap& operator=(const LockFreeHashMap&) = delete;

  // iterator begin();
  // iterator end();
  // const_iterator begin() const;
  // const_iterator end() const;
  //
  // Iterators pointing to `begin` and `end` of the container respectively.
  using Base::begin;
  using Base::end;

  // iterator find(const Key& key);
  // const_iterator find(const Key& key) const;
  //
  // In a lockfree way finds element with specific key.
  using Base::find;

  // bool contains(const Key& key);
  //
  // In a lockfree way check if an element with a key comparing equal to `key`
  // is present in the container. Return `true` if so, or `false` otherwise.
  using Base::contains;

  // void erase(iterator& pos);
  //
  // Erases the element at `position` of the `LockFreeHashMap`, returning
  // void and clearing the object pointing to pos.
  using Base::erase;

  // Erases the element with `key` as a key. If the element was found and
  // deleted, returns 1, otherwise 0.
  // TODO: upgrade erase(k) from int to size_t.
  int erase(const Key& key) { return static_cast<int>(Base::erase(key)); }
  template <class K,
            typename = std::enable_if_t<Base::supports_heterogeneous::value, K>>
  int erase(const K& key) {
    return static_cast<int>(Base::erase(key));
  }

  // void clear();
  //
  // Clears the container.
  using Base::clear;

  // Returns the number of elements in the container at some point in time.
  //
  // NOTE: Usage of size() is error-prone in a multi-threaded context and can
  // lead to time of check vs. time of use inconsistencies.
  // TODO: upgrade size() from int to size_t.
  int size() const { return static_cast<int>(Base::size()); }

  // bool empty() const;
  //
  // Returns if the container is empty.
  using Base::empty;

  // std::pair<iterator, bool> insert(const std::pair<const Key, Value>& value);
  // std::pair<iterator, bool> insert(std::pair<const Key, Value>&& value);
  //
  // Inserts a value into the `LockFreeHashMap`. Returns a pair consisting of
  // an iterator to the inserted element (or to the element that prevented the
  // insertion) and a bool denoting whether the insertion took place.
  using Base::insert;

  // std::pair<iterator, bool> emplace(Args&&... args);
  // std::pair<iterator, bool> try_emplace(const Key& k, Args&&... args);
  // std::pair<iterator, bool> try_emplace(Key&& k, Args&&... args);
  // Inserts an element of the specified value by constructing it in-place
  // within the `LockFreeHashMap`, provided that no element with the given key
  // already exists.
  //
  // For `emplace()`, the element may be constructed even if there already is an
  // element with the key in the container, in which case the newly constructed
  // element will be destroyed immediately. Prefer `try_emplace()` unless your
  // key is not copyable or moveable.
  using Base::emplace;
  using Base::try_emplace;

  // hasher hash_function() const;
  //
  // Returns function used to hash the keys.
  using Base::hash_function;

  // key_equal key_eq() const;
  //
  // Returns the function used to compare keys for equality.
  using Base::key_eq;

  // NOTE: Usage of this operator is error-prone for the following reason:
  // An assignment of the form:
  // "map[key] = value" will create two operations:
  //  1. Value-initialize the value if the key is not present.
  //  2. Assign into the value.
  // This creates the possibility of find() being able to see
  // value-initialized intermediate state or even worse: trigger data race
  // if assignment is not thread safe.
  //
  // It is advised to use the insert() function instead of this.
  Value& operator[](const Key& key) { return try_emplace(key).first->second; }
  Value& operator[](Key&& key) {
    return try_emplace(std::move(key)).first->second;
  }
  template <typename K,
            typename = std::enable_if_t<Base::supports_heterogeneous::value, K>>
  Value& operator[](K&& k) {
    return try_emplace(std::forward<K>(k)).first->second;
  }

  // std::pair<iterator, iterator> equal_range(const Key& key);
  // std::pair<const_iterator, const_iterator> equal_range(const Key& key)
  // const;
  //
  // Returns range of elements matching a specific key.
  using Base::equal_range;

  // absl::AnyInvocable<void()&&> CreateGC();
  //
  // Returns a "garbage collector" invocable or nullptr if no GC is necessary.
  // All accesses to the hash table started prior to the creation of said
  // invocable must have completed before it is safe to invoke the invocable.
  // Please refer to the unittest for an example of how to use this with
  // EpochGC. In general, CreateGC() transfers ownership to the callback of some
  // the "dead" memory being held by the hash table.
  using Base::CreateGC;

  // void RunGC();
  // Runs a garbage collector if necessary. All accesses to the hash table
  // must have completed before it is safe to invoke this method.
  using Base::RunGC;

  // std::optional<absl::AnyInvocable<void()&&>> CreateGCIfNecessary();
  //
  // Returns a "garbage collector" callback if there is any work to do.
  // The callback is rvalue-reference-qualified to ensure that it be called
  // at most once.
  //
  // Otherwise, returns std::nullopt.
  using Base::CreateGCIfNecessary;
};

namespace internal_lockfree_hashmap {
template <typename Key, typename Value>
struct LockFreeHashMapPolicy {
  using key_type = Key;
  using value_type = std::pair<const Key, Value>;
  using constant_iterators = std::false_type;
  struct SelectKey {
    const Key& operator()(const std::pair<const Key, Value>& e) const {
      return e.first;
    }
  };
};
}  // namespace internal_lockfree_hashmap

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_LOCKFREE_HASHMAP_H_
