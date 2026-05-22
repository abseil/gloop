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

// This is just a very thin wrapper over sparsehashtable.h, just
// like sgi stl's stl_hash_set is a very thin wrapper over
// stl_hashtable.
//
// This is more different from sparse_hash_map than you might think,
// because all iterators for sets are const (you obviously can't
// change the key, and for sets there is no value).
//
// We adhere mostly to the STL semantics for hash-map.  One important
// exception is that insert() may invalidate iterators entirely -- STL
// semantics are that insert() may reorder iterators, but they all
// still refer to something valid in the hashtable.  Not so for us.
// Likewise, insert() may invalidate pointers into the hashtable.
// (Whether insert invalidates iterators and pointers depends on
// whether it results in a hashtable resize, but that's an implementation
// detail that may change in the future.) On the plus side, delete()
// doesn't invalidate iterators or pointers at all, or even change the
// ordering of elements.
//
// Also please note:
//
//    1) set_deleted_key():
//         Unlike STL's hash_set, if you want to use erase() you
//         *must* call set_deleted_key() after construction.
//
//    2) Keys equal to the deleted key (if any) cannot be
//         used as keys for find(), count(), insert(), etc.
//
//    3) min_load_factor():
//         Setting the minimum load factor controls how aggressively the
//         table is shrunk when keys are erased.  Setting it to 0.0
//         guarantees that the hash table will never shrink.
//
//    4) resize(0):
//         When an item is deleted, its memory isn't freed right
//         away.  This allows you to iterate over a hashtable,
//         and call erase(), without invalidating the iterator.
//         To force the memory to be freed, call resize(0).
//         For tr1 compatibility, this can also be called as rehash(0).
//
// <link> is a guide to selecting a hash_set.
// Roughly speaking:
//   (1) dense_hash_set: fastest, uses the most memory unless entries are small
//   (2) sparse_hash_set: slowest, uses the least memory
//   (3) hash_set / unordered_set (STL): in the middle
//
// Typically I use sparse_hash_set when I care about space and/or when
// I need to save the hashtable on disk.  I use hash_set otherwise.  I
// don't personally use dense_hash_set ever; some people use it for
// small sets with lots of lookups.
//
// - dense_hash_set has, typically, about 78% memory overhead (if your
//   data takes up X bytes, the hash_set uses .78X more bytes in overhead).
// - sparse_hash_set has about 4 bits overhead per entry.
// - sparse_hash_set can be 3-7 times slower than the others for lookup and,
//   especially, inserts.  See time_hash_map.cc for details.
//

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_SPARSE_HASH_SET_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_SPARSE_HASH_SET_H_

#include <stdio.h>

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/hash/hash.h"
#include "gloop/util/gtl/sparsehashtable.h"  // IWYU pragma: export

// Some files test for this symbol.
#define _SPARSE_HASH_SET_H_

template <class Value,
          class HashFcn = typename gtl::internal_sparsetable::SelectDefaultHash<
              absl::Hash<Value>>::type,
          class EqualKey = std::equal_to<Value>,
          class Alloc = std::allocator<Value>>
class sparse_hash_set {
 private:
  // Apparently identity is not stl-standard, so we define our own
  struct Identity {
    typedef const Value& result_type;
    const Value& operator()(const Value& v) const { return v; }
  };
  struct SetKey {
    void operator()(Value* value, const Value& new_key) const {
      *value = new_key;
    }
  };

  typedef sparse_hashtable<Value, Value, HashFcn, Identity, SetKey, EqualKey,
                           Alloc>
      ht;
  ht rep;

 public:
  typedef typename ht::key_type key_type;
  typedef typename ht::value_type value_type;
  typedef typename ht::hasher hasher;
  typedef typename ht::key_equal key_equal;
  typedef Alloc allocator_type;

  typedef typename ht::size_type size_type;
  typedef typename ht::difference_type difference_type;
  typedef typename ht::const_pointer pointer;
  typedef typename ht::const_pointer const_pointer;
  typedef typename ht::const_reference reference;
  typedef typename ht::const_reference const_reference;

  typedef typename ht::const_iterator iterator;
  typedef typename ht::const_iterator const_iterator;
  typedef typename ht::const_local_iterator local_iterator;
  typedef typename ht::const_local_iterator const_local_iterator;

  // Iterator functions -- recall all iterators are const
  iterator begin() const { return rep.begin(); }
  iterator end() const { return rep.end(); }

  // These come from tr1's unordered_set. For us, a bucket has 0 or 1 elements.
  local_iterator begin(size_type i) const { return rep.begin(i); }
  local_iterator end(size_type i) const { return rep.end(i); }

  // Accessor functions
  allocator_type get_allocator() const { return rep.get_allocator(); }
  hasher hash_funct() const { return rep.hash_funct(); }
  hasher hash_function() const { return hash_funct(); }  // tr1 name
  key_equal key_eq() const { return rep.key_eq(); }

  // Constructors
  sparse_hash_set() = default;

  explicit sparse_hash_set(size_type expected_max_items_in_table,
                           const hasher& hf = hasher(),
                           const key_equal& eql = key_equal(),
                           const allocator_type& alloc = allocator_type())
      : rep(expected_max_items_in_table, hf, eql, Identity(), SetKey(), alloc) {
  }

  template <class InputIterator>
  sparse_hash_set(InputIterator f, InputIterator l,
                  size_type expected_max_items_in_table = 0,
                  const hasher& hf = hasher(),
                  const key_equal& eql = key_equal(),
                  const allocator_type& alloc = allocator_type())
      : rep(expected_max_items_in_table, hf, eql, Identity(), SetKey(), alloc) {
    rep.insert(f, l);
  }
  // We use the default copy constructor
  // We use the default operator=()
  // We use the default destructor

  void clear() { rep.clear(); }
  void swap(sparse_hash_set& hs) noexcept { rep.swap(hs.rep); }

  // Functions concerning size
  size_type size() const { return rep.size(); }
  size_type max_size() const { return rep.max_size(); }
  bool empty() const { return rep.empty(); }
  size_type bucket_count() const { return rep.bucket_count(); }

  // These are tr1 methods.  bucket() is the bucket the key is or would be in.
  float load_factor() const { return size() * 1.0f / bucket_count(); }
  float max_load_factor() const {
    float shrink, grow;
    rep.get_resizing_parameters(&shrink, &grow);
    return grow;
  }
  void max_load_factor(float new_grow) {
    float shrink, grow;
    rep.get_resizing_parameters(&shrink, &grow);
    rep.set_resizing_parameters(shrink, new_grow);
  }
  // These aren't tr1 methods but perhaps ought to be.
  float min_load_factor() const {
    float shrink, grow;
    rep.get_resizing_parameters(&shrink, &grow);
    return shrink;
  }
  void min_load_factor(float new_shrink) {
    float shrink, grow;
    rep.get_resizing_parameters(&shrink, &grow);
    rep.set_resizing_parameters(new_shrink, grow);
  }
  // Deprecated; use min_load_factor() or max_load_factor() instead.
  void set_resizing_parameters(float shrink, float grow) {
    rep.set_resizing_parameters(shrink, grow);
  }

  void resize(size_type hint) { rep.resize(hint); }
  void rehash(size_type hint) { resize(hint); }  // the tr1 name

  // Lookup routines
  iterator find(const key_type& key) const { return rep.find(key); }

  size_type count(const key_type& key) const { return rep.count(key); }

  std::pair<iterator, iterator> equal_range(const key_type& key) const {
    return rep.equal_range(key);
  }

  // Insertion routines
  std::pair<iterator, bool> insert(const value_type& obj) {
    std::pair<typename ht::iterator, bool> p = rep.insert(obj);
    return std::pair<iterator, bool>(p.first, p.second);  // const to non-const
  }
  template <class InputIterator>
  void insert(InputIterator f, InputIterator l) {
    rep.insert(f, l);
  }
  // Required for std::insert_iterator; the passed-in iterator is ignored.
  iterator insert(iterator, const value_type& obj) { return insert(obj).first; }

  // Unlike std::set, we cannot construct an element in place, as we do not have
  // a layer of indirection like std::set nodes. Therefore, emplace* methods do
  // not provide a performance advantage over insert + move.
  template <typename... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    return rep.insert(value_type(std::forward<Args>(args)...));
  }
  // The passed-in const_iterator is ignored.
  template <typename... Args>
  iterator emplace_hint(const_iterator, Args&&... args) {
    return rep.insert(value_type(std::forward<Args>(args)...)).first;
  }

  // Deletion routines
  // THESE ARE NON-STANDARD!  I make you specify an "impossible" key
  // value to identify deleted buckets.  You can change the key as
  // time goes on, or get rid of it entirely to be insert-only.
  void set_deleted_key(const key_type& key) { rep.set_deleted_key(key); }
  void clear_deleted_key() { rep.clear_deleted_key(); }
  key_type deleted_key() const { return rep.deleted_key(); }

  // These are standard
  size_type erase(const key_type& key) { return rep.erase(key); }
  void erase(iterator it) { rep.erase(it); }
  void erase(iterator f, iterator l) { rep.erase(f, l); }

  // Comparison
  bool operator==(const sparse_hash_set& hs) const { return rep == hs.rep; }
  bool operator!=(const sparse_hash_set& hs) const { return rep != hs.rep; }

  // I/O -- this is an add-on for writing metainformation to disk
  //
  // For maximum flexibility, this does not assume a particular
  // file type (though it will probably be a FILE *).  We just pass
  // the fp through to rep.
  //
  // See util/gtl/legacy_sparsetable_wire_format.h for how to read this format.
  template <typename OUTPUT>
  ABSL_MUST_USE_RESULT bool write_metadata(OUTPUT* fp) {
    static_assert(
        std::is_same_v<HashFcn, gtl::SparseHashTableLegacyHash<Value>>, "");
    return rep.write_metadata(fp);
  }

  template <typename OUTPUT>
  ABSL_MUST_USE_RESULT bool write_nopointer_data(OUTPUT* fp) {
    static_assert(
        std::is_same_v<HashFcn, gtl::SparseHashTableLegacyHash<Value>>, "");
    return rep.write_nopointer_data(fp);
  }
};

template <class Val, class HashFcn, class EqualKey, class Alloc>
inline void swap(sparse_hash_set<Val, HashFcn, EqualKey, Alloc>& hs1,
                 sparse_hash_set<Val, HashFcn, EqualKey, Alloc>& hs2) noexcept {
  hs1.swap(hs2);
}

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_SPARSE_HASH_SET_H_
