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

// small_map is *NOT* a direct replacement for map or hash_map.
//
// small_map uses an inline array to store up to a certain number of
// unique key-value-pair elements, but upgrades itself automatically to
// be backed by a user-specified map when it runs out of space. For maps
// that are typically small, this can be considerably faster than using
// something like hash_map directly, as hash_map is optimized for large data
// sets.
//
// Of course, in order for this to be a significant win, you have to have
// a situation where you are using lots and lots of these small maps.  One
// such situation is MessageSet:  A set of search results may contain
// thousands of MessageSets, each containing only a couple items.
//
// The small_map API is very minimal, and was originally written for a
// very specific use (MessageSet). It only implements a few core methods
// of the STL associative container interface.
//
// WARNINGS:
//   * You should assume that small_map might invalidate all the iterators
//     on any call to extract(), erase(), insert() and operator[].
//
//   * It is potentially unordered, even if you have a std::map underlying
//     the small_map. While the small_map remains small, it is not ordered.
//
//   * Cannot be used with a multi-associative map such as multimap for its
//     NormalMap type.
//
//   * It does not confer pointer stability when it upgrades from from an
//     inline array even if a std::map underlies the small_map.
//

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_SMALL_MAP_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_SMALL_MAP_H_

#include <assert.h>

#include <cstddef>
#include <initializer_list>
#include <optional>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "gloop/util/gtl/compare.h"
#include "gloop/util/gtl/heterogeneous_lookup.h"
#include "gloop/util/gtl/manual_constructor.h"
#include "gloop/util/gtl/stl_util.h"

namespace gtl {

// An STL-like associative container which starts out backed by a simple
// array but switches to some other container type if it grows beyond a
// fixed size.
//
// NormalMap:  The map type to fall back to.  This also defines the key
//             and value types for the small_map.
// kArraySize:  The size of the initial array of results.  Once the map
//              grows beyond this size, the map type will be used instead.
// EqualKey:  A functor which tests two keys for equality.  If the wrapped
//            map type has a "key_equal" member (hash_map does), then that will
//            be used by default. If the wrapped map type has a strict weak
//            ordering "key_compare" (std::map does), that will be used to
//            implement equality by default.
// MapInit: A functor that takes a ManualConstructor<NormalMap>* and uses it to
//          initialize the map. This functor will be called at most once per
//          small_map, when the map exceeds the threshold of kArraySize and we
//          are about to copy values from the array to the map. The functor
//          *must* call one of the Init() methods provided by
//          ManualConstructor, since after it runs we assume that the NormalMap
//          has been initialized.
//
// example:
//   small_map<hash_map<string, int>> days ({
//      {"sunday", 0},
//      {"monday", 1},
//      {"tuesday", 2},
//      {"wednesday", 3}
//   });
//   days["thursday" ] = 4;
//   days["friday"   ] = 5;
//   days["saturday" ] = 6;
template <typename NormalMap>
class small_map_default_init {
 public:
  void operator()(ManualConstructor<NormalMap>* map) const { map->Init(); }
};

// has_key_equal<M>::value is true iff there exists a type M::key_equal. This is
// used to dispatch to one of the select_equal_key<> metafunctions below.
template <typename M>
struct has_key_equal {
  typedef char smaller;
  typedef struct {
    char dummy[2];
  } larger;
  // Two functions, one accepts types that have a key_equal member, and one that
  // accepts anything. They each return a value of a different size, so we can
  // determine at compile-time which function would have been called.
  template <typename U>
  static larger test(typename U::key_equal*);
  template <typename>
  static smaller test(...);
  // Determines if M::key_equal exists by looking at the size of the return
  // value of the compiler-chosen test() function.
  static const bool value = (sizeof(test<M>(nullptr)) == sizeof(larger));
};
// See <link> for a discussion of why this definition is here.
template <typename M>
const bool has_key_equal<M>::value;

// Implements equality in terms of a strict weak ordering comparator.
template <typename C, typename LK, typename RK>
bool equal_key_by_three_way_comparison(const LK& left, const RK& right) {
  C comparator;
  return gtl::do_three_way_comparison(comparator, left, right) == 0;
}

// Base template used for map types that do NOT have an M::key_equal member,
// e.g., std::map<>. These maps have a strict weak ordering comparator rather
// than an equality functor, so equality will be implemented in terms of that
// comparator. Also it defines "is_transparent" member type in case of it's
// presence in M::key_compare.
//
// There's a partial specialization of this template below for map types that do
// have an M::key_equal member.
template <typename M, bool, typename = void>
struct select_equal_key {
  static_assert(!gtl::IsTransparent<typename M::key_compare>::value,
                "this specialization is for non transparent comparators");

  struct equal_key {
    bool operator()(const typename M::key_type& left,
                    const typename M::key_type& right) const {
      return equal_key_by_three_way_comparison<typename M::key_compare>(left,
                                                                        right);
    }
  };
};

// Partial specialization for transparent M::key_compare
template <typename M>
struct select_equal_key<M, false,
                        std::void_t<typename M::key_compare::is_transparent>> {
  struct equal_key {
    using is_transparent = typename M::key_compare::is_transparent;

    template <typename LK, typename RK>
    bool operator()(const LK& left, const RK& right) const {
      return equal_key_by_three_way_comparison<typename M::key_compare>(left,
                                                                        right);
    }
  };
};

// Partial template specialization handles case where M::key_equal exists, e.g.,
// hash_map<>.
template <typename M>
struct select_equal_key<M, true> {
  typedef typename M::key_equal equal_key;
};

template <typename NormalMap, int kArraySize = 4,
          typename EqualKey = typename select_equal_key<
              NormalMap, has_key_equal<NormalMap>::value>::equal_key,
          typename MapInit = small_map_default_init<NormalMap>>
class small_map {
  // We cannot rely on the compiler to reject array of size 0.  In
  // particular, gcc 2.95.3 does it but later versions allow 0-length
  // arrays.  Therefore, we explicitly reject non-positive kArraySize
  // here.
  static_assert(kArraySize > 0, "default initial size must be positive");

  template <class K>
  using key_arg =
      gtl::HeterogeneousLookupKeyArg<K, typename NormalMap::key_type, EqualKey>;

 public:
  typedef typename NormalMap::key_type key_type;
  typedef typename NormalMap::mapped_type data_type;
  typedef typename NormalMap::mapped_type mapped_type;
  typedef typename NormalMap::value_type value_type;
  typedef EqualKey key_equal;

  small_map() : size_(0), functor_(MapInit()) {}

  explicit small_map(const MapInit& functor) : size_(0), functor_(functor) {}

  // Allow copy-constructor and assignment, since STL allows them too.
  small_map(const small_map& src) {
    // size_ and functor_ are initted in InitFrom()
    InitFrom(src);
  }
  small_map& operator=(const small_map& src) {
    if (&src == this) return *this;

    // This is not optimal. If src and dest are both using the small
    // array, we could skip the teardown and reconstruct. One problem
    // to be resolved is that the value_type itself is pair<const K,
    // V>, and const K is not assignable.
    Destroy();
    InitFrom(src);
    return *this;
  }

  // Move construction: moves in the contents of 'src'. Upon return, the
  // value of 'src' is valid, but unspecified.
  small_map(small_map&& src) noexcept {  // NOLINT(build/c++11)
    // size_ and functor_ are initted in InitFrom()
    InitFrom(std::move(src));
  }

  // Move assignment: destroys entries in this map and moves the contents
  // of 'src'. Upon return, the value of 'src' is valid, but unspecified.
  small_map& operator=(small_map&& src) noexcept {  // NOLINT(build/c++11)
    // This is not optimal. If src and dest are both using the small
    // array, we could skip the teardown and reconstruct.
    Destroy();
    InitFrom(std::move(src));
    return *this;
  }

  small_map(std::initializer_list<value_type> data) : small_map() {
    insert(data.begin(), data.end());
  }

  ~small_map() { Destroy(); }

  class node_type {
   public:
    using key_type = small_map::key_type;
    using mapped_type = small_map::mapped_type;

    constexpr node_type() = default;
    constexpr node_type(const node_type&) = delete;
    constexpr node_type(node_type&&) noexcept = default;
    constexpr node_type& operator=(const node_type&) = delete;
    constexpr node_type& operator=(node_type&&) noexcept = default;

    constexpr bool empty() const noexcept { return !node_.has_value(); }
    constexpr explicit operator bool() const noexcept { return !empty(); }
    const key_type& key() const { return node_.value().first; }
    mapped_type& mapped() const { return node_.value().second; }

   private:
    explicit node_type(std::pair<const key_type, mapped_type> value)
        : node_(std::move(value)) {}

    // We considered using absl::variant to hold a NormalMap::node_type when
    // possible, but it was less efficient. We can revisit this decision as the
    // implementation matures.
    mutable std::optional<std::pair<const key_type, mapped_type>> node_;

    friend class small_map;
  };

  class const_iterator;

  class iterator {
   public:
    typedef typename NormalMap::iterator::iterator_category iterator_category;
    typedef typename NormalMap::iterator::value_type value_type;
    typedef typename NormalMap::iterator::difference_type difference_type;
    typedef typename NormalMap::iterator::pointer pointer;
    typedef typename NormalMap::iterator::reference reference;

    iterator() = default;

    iterator& operator++() {
      if (array_iter_ != nullptr) {
        ++array_iter_;
      } else {
        ++hash_iter_;
      }
      return *this;
    }
    iterator operator++(int /*unused*/) {
      iterator result(*this);
      ++(*this);
      return result;
    }
    iterator& operator--() {
      if (array_iter_ != nullptr) {
        --array_iter_;
      } else {
        --hash_iter_;
      }
      return *this;
    }
    iterator operator--(int /*unused*/) {
      iterator result(*this);
      --(*this);
      return result;
    }
    value_type* operator->() const {
      if (array_iter_ != nullptr) {
        return array_iter_->get();
      } else {
        return hash_iter_.operator->();
      }
    }

    value_type& operator*() const {
      if (array_iter_ != nullptr) {
        return **array_iter_;
      } else {
        return *hash_iter_;
      }
    }

    bool operator==(const iterator& other) const {
      if (array_iter_ != nullptr) {
        return array_iter_ == other.array_iter_;
      } else {
        return other.array_iter_ == nullptr && hash_iter_ == other.hash_iter_;
      }
    }

    bool operator!=(const iterator& other) const { return !(*this == other); }

   private:
    friend class small_map;
    friend class const_iterator;
    explicit iterator(ManualConstructor<value_type>* init)
        : array_iter_(init) {}
    explicit iterator(const typename NormalMap::iterator& init)
        : hash_iter_(init) {}

    ManualConstructor<value_type>* absl_nullable array_iter_ = nullptr;
    typename NormalMap::iterator hash_iter_;
  };

  class const_iterator {
   public:
    typedef
        typename NormalMap::const_iterator::iterator_category iterator_category;
    typedef typename NormalMap::const_iterator::value_type value_type;
    typedef typename NormalMap::const_iterator::difference_type difference_type;
    typedef typename NormalMap::const_iterator::pointer pointer;
    typedef typename NormalMap::const_iterator::reference reference;

    const_iterator() = default;
    // Non-explicit ctor lets us convert regular iterators to const iterators
    const_iterator(const iterator& other)
        : array_iter_(other.array_iter_), hash_iter_(other.hash_iter_) {}

    const_iterator& operator++() {
      if (array_iter_ != nullptr) {
        ++array_iter_;
      } else {
        ++hash_iter_;
      }
      return *this;
    }
    const_iterator operator++(int /*unused*/) {
      const_iterator result(*this);
      ++(*this);
      return result;
    }

    const_iterator& operator--() {
      if (array_iter_ != nullptr) {
        --array_iter_;
      } else {
        --hash_iter_;
      }
      return *this;
    }
    const_iterator operator--(int /*unused*/) {
      const_iterator result(*this);
      --(*this);
      return result;
    }

    const value_type* operator->() const {
      if (array_iter_ != nullptr) {
        return array_iter_->get();
      } else {
        return hash_iter_.operator->();
      }
    }

    const value_type& operator*() const {
      if (array_iter_ != nullptr) {
        return **array_iter_;
      } else {
        return *hash_iter_;
      }
    }

    bool operator==(const const_iterator& other) const {
      if (array_iter_ != nullptr) {
        return array_iter_ == other.array_iter_;
      } else {
        return other.array_iter_ == nullptr && hash_iter_ == other.hash_iter_;
      }
    }

    bool operator!=(const const_iterator& other) const {
      return !(*this == other);
    }

   private:
    friend class small_map;
    explicit const_iterator(const ManualConstructor<value_type>* init)
        : array_iter_(init) {}
    explicit const_iterator(const typename NormalMap::const_iterator& init)
        : hash_iter_(init) {}

    const ManualConstructor<value_type>* absl_nullable array_iter_ = nullptr;
    typename NormalMap::const_iterator hash_iter_;
  };

  template <typename K = key_type>
  iterator find(const key_arg<K>& key) {
    key_equal compare;
    if (size_ >= 0) {
      for (int i = 0; i < size_; i++) {
        if (compare(array_[i]->first, key)) {
          return iterator(&array_[i]);
        }
      }
      return iterator(array_ + size_);
    } else {
      return iterator(map()->find(key));
    }
  }

  template <typename K = key_type>
  const_iterator find(const key_arg<K>& key) const {
    key_equal compare;
    if (size_ >= 0) {
      for (int i = 0; i < size_; i++) {
        if (compare(array_[i]->first, key)) {
          return const_iterator(&array_[i]);
        }
      }
      return const_iterator(array_ + size_);
    } else {
      return const_iterator(map()->find(key));
    }
  }

  // Invalidates iterators.
  template <typename K = key_type>
  data_type& operator[](const key_arg<K>& key) {
    key_equal compare;

    if (size_ >= 0) {
      // operator[] searches backwards, favoring recently-added
      // elements.
      for (int i = size_ - 1; i >= 0; --i) {
        if (compare(array_[i]->first, key)) {
          return array_[i]->second;
        }
      }
      if (size_ == kArraySize) {
        ConvertToRealMap();
        return (*map_)[key];
      } else {
        array_[size_].Init(key, data_type());
        return array_[size_++]->second;
      }
    } else {
      return (*map_)[key];
    }
  }

  // Invalidates iterators.
  std::pair<iterator, bool> insert(const value_type& x) {
    return InsertInternal(x);
  }

  // Invalidates iterators.
  std::pair<iterator, bool> insert(value_type&& x) {  // NOLINT(build/c++11)
    return InsertInternal(std::move(x));
  }

  // Invalidates iterators.
  template <typename... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {  // NOLINT(build/c++11)
    if (using_full_map()) {
      auto ret = map_->emplace(std::forward<Args>(args)...);
      return {iterator(ret.first), ret.second};
    }
    // TODO: Change InsertInternal() to EmplaceInternal().
    return InsertInternal(value_type(std::forward<Args>(args)...));
  }

  // Invalidates iterators.
  template <class InputIterator>
  void insert(InputIterator f, InputIterator l) {
    while (f != l) {
      insert(*f);
      ++f;
    }
  }

  iterator begin() {
    if (size_ >= 0) {
      return iterator(array_);
    } else {
      return iterator(map_->begin());
    }
  }
  const_iterator begin() const {
    if (size_ >= 0) {
      return const_iterator(array_);
    } else {
      return const_iterator(map_->begin());
    }
  }

  iterator end() {
    if (size_ >= 0) {
      return iterator(array_ + size_);
    } else {
      return iterator(map_->end());
    }
  }
  const_iterator end() const {
    if (size_ >= 0) {
      return const_iterator(array_ + size_);
    } else {
      return const_iterator(map_->end());
    }
  }

  void clear() {
    if (size_ >= 0) {
      for (int i = 0; i < size_; i++) {
        array_[i].Destroy();
      }
    } else {
      map_.Destroy();
    }
    size_ = 0;
  }

  // Returns an iterator that points to the element following position.  May
  // invalidate other iterators.
  iterator erase(const iterator& position) {
    if (size_ >= 0) {
      ArrayErase(position.array_iter_ - array_);
      return position;
    } else {
      return iterator(MapEraseAndIncrement(map_.get(), position.hash_iter_));
    }
  }

  const_iterator erase(const const_iterator& position) {
    if (size_ >= 0) {
      ArrayErase(position.array_iter_ - array_);
      return position;
    } else {
      return const_iterator(
          MapEraseAndIncrement(map_.get(), position.hash_iter_));
    }
  }

  template <typename K = key_type>
  int erase(const key_arg<K>& key) {
    iterator iter = find(key);
    if (iter == end()) return 0;
    erase(iter);
    return 1;
  }

  template <typename K = key_type>
  node_type extract(const key_arg<K>& key) {
    iterator iter = find(key);
    if (iter == end()) return {};

    return extract(iter);
  }

  node_type extract(const const_iterator& it) {
    return extract(static_cast<iterator>(it));
  }

  node_type extract(const iterator& it) {
    if (size_ >= 0) {
      node_type ret({std::move(it->first), std::move(it->second)});
      erase(it);
      return ret;
    }
    auto node = map()->extract(it.hash_iter_);
    return node_type({std::move(node.key()), std::move(node.mapped())});
  }

  void swap(small_map& that) noexcept {
    using std::swap;
    if (using_full_map() && that.using_full_map()) {
      swap(*map_, *that.map_);
      swap(functor_, that.functor_);
    } else if (using_full_map() || that.using_full_map()) {
      small_map& full_map = using_full_map() ? *this : that;
      small_map& inlined = !using_full_map() ? *this : that;

      ManualConstructor<NormalMap> scratch;
      full_map.functor_(&scratch);
      *scratch = std::move(*full_map.map_);
      full_map.map_.Destroy();

      for (size_t i = 0; i < inlined.size(); ++i) {
        full_map.array_[i].Init(std::move(*inlined.array_[i]));
        inlined.array_[i].Destroy();
      }

      full_map.functor_(&inlined.map_);
      *inlined.map_ = std::move(*scratch);

      swap(full_map.size_, inlined.size_);
      swap(full_map.functor_, inlined.functor_);
    } else {
      small_map& larger = size() > that.size() ? *this : that;
      small_map& smaller = size() <= that.size() ? *this : that;

      size_t i = 0;
      for (; i < smaller.size(); ++i) {
        value_type scratch = std::move(*smaller.array_[i]);
        smaller.array_[i].Destroy();
        smaller.array_[i].Init(std::move(*larger.array_[i]));
        larger.array_[i].Destroy();
        larger.array_[i].Init(std::move(scratch));
      }
      for (; i < larger.size(); ++i) {
        smaller.array_[i].Init(std::move(*larger.array_[i]));
        larger.array_[i].Destroy();
      }
      swap(smaller.size_, larger.size_);
      swap(smaller.functor_, larger.functor_);
    }
  }

  friend void swap(small_map& a, small_map& b) noexcept { a.swap(b); }

  template <typename K = key_type>
  bool contains(const key_arg<K>& key) const {
    return find(key) != end();
  }

  template <typename K = key_type>
  int count(const key_arg<K>& key) const {
    return (find(key) == end()) ? 0 : 1;
  }

  // Invalidates all iterators.
  void reserve(size_t n) {
    if (n <= kArraySize && !using_full_map()) return;

    if (!using_full_map()) {
      // Allow conversion to real map if we are told it's going to be bigger
      // anyway.
      ConvertToRealMap();
    }
    map()->reserve(n);
  }

  int size() const {
    if (size_ >= 0) {
      return size_;
    } else {
      return static_cast<int>(map_->size());
    }
  }

  bool empty() const {
    if (size_ >= 0) {
      return (size_ == 0);
    } else {
      return map_->empty();
    }
  }

  // This regards maps as equal if their value_types can be permuted
  // into equivalent sequences.
  friend bool operator==(const small_map& left, const small_map& right) {
    if (&left == &right) return true;
    if (left.size() != right.size()) return false;
    // If the arrays were sorted we could optimise the array vs array
    // comparison case. If the arrays were sorted and the underlying
    // map type were ordered as well, we could do iterator traversals
    // directly. We have no such guarantees here.
    // If neither map is represented as a small array, we should
    // delegate to the NormalMap operator==. Unfortunately, we have to
    // deal with hash_map which has a broken operator==.
    if (left.using_full_map() && right.using_full_map())
      return HashMapEquality(*left.map_, *right.map_);
    // The maps are now guaranteed to be the same size and one is
    // known to be represented as part of a small array. It suffices
    // to check each item in the array against the other small_map.
    const auto size = left.size();
    const auto* an_array = left.using_full_map() ? right.array_ : left.array_;
    const auto& a_small_map = left.using_full_map() ? left : right;
    for (int i = 0; i < size; ++i) {
      const auto& array_entry = an_array[i];
      const auto small_map_it = a_small_map.find(array_entry->first);
      if (small_map_it == a_small_map.end() ||
          !(small_map_it->second == array_entry->second))
        return false;
    }
    return true;
  }

  friend bool operator!=(const small_map& left, const small_map& right) {
    return !(left == right);
  }

  // Returns true if we have fallen back to using the underlying map
  // representation.
  bool using_full_map() const { return size_ < 0; }

  NormalMap* map() {
    assert(using_full_map());
    return map_.get();
  }
  const NormalMap* map() const {
    assert(using_full_map());
    return map_.get();
  }

 private:
  template <class U>
  std::pair<iterator, bool> InsertInternal(U&& x) {
    key_equal compare;

    if (using_full_map()) {
      auto ret = map_->insert(std::forward<U>(x));
      return {iterator(ret.first), ret.second};
    }

    for (int i = 0; i < size_; ++i) {
      if (compare(array_[i]->first, x.first)) {
        return {iterator(&array_[i]), false};
      }
    }

    if (size_ == kArraySize) {
      ConvertToRealMap();  // Invalidates all iterators!
      auto ret = map_->insert(std::forward<U>(x));
      return {iterator(ret.first), ret.second};
    }

    array_[size_].Init(std::forward<U>(x));
    return {iterator(&array_[size_++]), true};
  }

  template <typename M, typename Iter>
  auto MapEraseAndIncrement(M* m, Iter iter)
      -> std::enable_if_t<std::is_void<decltype(m->erase(m->begin()))>::value,
                          Iter> {
    m->erase(iter++);
    return iter;
  }

  template <typename M, typename Iter>
  auto MapEraseAndIncrement(M* m, Iter iter) -> std::enable_if_t<
      std::is_same<decltype(m->begin()), decltype(m->erase(m->begin()))>::value,
      Iter> {
    return map_->erase(iter);
  }

  void ArrayErase(int i) {
    array_[i].Destroy();
    --size_;
    if (i != size_) {
      array_[i].Init(std::move(*array_[size_]));
      array_[size_].Destroy();
    }
  }

  void ConvertToRealMap() {
    // Move the current elements into a temporary array.
    ManualConstructor<value_type> temp_array[kArraySize];

    for (int i = 0; i < size_; i++) {
      temp_array[i].Init(std::move(*array_[i]));
      array_[i].Destroy();
    }
    int temp_size = size_;

    // Initialize the map.
    size_ = -1;
    functor_(&map_);

    // Insert elements into it.
    for (int i = 0; i < temp_size; i++) {
      map_->insert(std::move(*temp_array[i]));
      temp_array[i].Destroy();
    }
  }

  // Helpers for constructors and destructors.
  void InitFrom(const small_map& src) {
    functor_ = src.functor_;
    size_ = src.size_;
    if (src.size_ >= 0) {
      for (int i = 0; i < size_; i++) {
        array_[i].Init(*src.array_[i]);
      }
    } else {
      functor_(&map_);
      *map_ = *src.map_;
    }
  }

  void InitFrom(small_map&& src) {  // NOLINT(build/c++11)
    functor_ = std::move(src.functor_);
    size_ = src.size_;
    if (src.size_ >= 0) {
      for (int i = 0; i < size_; i++) {
        array_[i].Init(std::move(*src.array_[i]));
      }
    } else {
      functor_(&map_);
      *map_ = std::move(*src.map_);
    }
  }

  void Destroy() {
    if (size_ >= 0) {
      for (int i = 0; i < size_; i++) {
        array_[i].Destroy();
      }
    } else {
      map_.Destroy();
    }
  }

  int size_;  // negative = using hash_map

  ABSL_ATTRIBUTE_NO_UNIQUE_ADDRESS MapInit functor_;

  // We want to call constructors and destructors manually, but we don't
  // want to allocate and deallocate the memory used for them separately.
  // So, we use a union.
  union {
    ManualConstructor<value_type> array_[kArraySize];
    ManualConstructor<NormalMap> map_;
  };
};

// Teach the compiler how to compare const iterators to non-const iterators.
template <typename NormalMap, int kArraySize, typename EqualKey,
          typename Functor>
bool operator==(const typename small_map<NormalMap, kArraySize, EqualKey,
                                         Functor>::iterator& a,
                const typename small_map<NormalMap, kArraySize, EqualKey,
                                         Functor>::const_iterator& b) {
  return const_iterator(a) == b;
}
template <typename NormalMap, int kArraySize, typename EqualKey,
          typename Functor>
bool operator!=(const typename small_map<NormalMap, kArraySize, EqualKey,
                                         Functor>::iterator& a,
                const typename small_map<NormalMap, kArraySize, EqualKey,
                                         Functor>::const_iterator& b) {
  return const_iterator(a) != b;
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_SMALL_MAP_H_
