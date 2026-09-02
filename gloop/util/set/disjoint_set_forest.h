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

// A disjoint set forest (often called Union/Find) is a data structure for
// keeping track of a set of elements partitioned into a collection of disjoint
// subsets. It provides the ability to efficiently perform operations such as:
// - Which subset does element x belong to? (the Find operation)
// - Do elements x and y belong to the same subset? (a simple variation of Find)
// - Combine subsets S1 and S2 into a single subset (the Union operation)
//
// This data structure is also known as "union-find" or "merge-find". It is
// useful for a variety of problems, such as efficiently finding the connected
// components of a graph. For more information, see
// http://en.wikipedia.org/wiki/Disjoint-set_data_structure or read the section
// on Union-Find in the book "Algorithms" by Cormen, Leiserson, Rivest, Stein.
//
// The FindSet() operation requires a mechanism for naming subsets. The
// mechanism used here is to select one member of the subset to act as its
// "representative". That is, given some subset S1, one of the elements of S1
// will be chosen to be its representative. When any of the elements of S1 are
// searched for (e.g. via FindSet()), the result will be the element that is the
// representative for S1.
//
// The Union() operation merges two subsets together. Given two elements x and
// y--members of subsets S1 and S2 respectively--the Union() operation merges S1
// and S2 into a single subset, though note that x and y could already reside in
// the same subset, in which case the merge operation is a no-op. x and y don't
// have to be the representatives of S1 and S2; any members of S1 and S2 will
// do. The consequence of the Union() operation is that for every element e of
// the former subsets S1 or S2, FindSet(e) will now return the same value. In
// particular FindSet(x)==FindSet(y) will hold for all future calls (unless the
// data structure is cleared).
//
// Note that the representative is a property of a subset and not of an element.
// This means that the particular value of FindSet(x) might change over time, if
// the subset that x belongs to is merged with another subset. This is a
// necessary consequence of the fact that each subset has a unique
// representative, and that the Union operation creates a new subset out of two
// smaller subsets. (Conversely, the value of FindSet(x) is guaranteed to *not*
// change if the subset that x belongs to does *not* participate in a Union
// operation).
//
// Elements are introduced to the data structure via the MakeSet() operation.
// MakeSet(x) adds a new subset of size 1, denoted as {x}, to the forest. Larger
// sets of arbitrary size can be formed by repeatedly performing the Union()
// operation on smaller sets, including the singleton sets created by MakeSet().
//
// This implementation requires that the caller choose a special signal value
// which plays the role of a "does not exist" indicator. This value is specified
// at construction time, and is used as the return value of FindSet(x) in the
// case when x does not belong to any subset. Users of this library must ensure
// to never call MakeSet() with this signal value.
//
// Example:
//  const int kNotFound = -1;
//  DisjointSetForest<int> forest(kNotFound);
//  for (int i = 0; i < 6; ++i) {
//    forest.MakeSet(i);
//  }
//  // Current composition of forest: {0} {1} {2} {3} {4} {5}
//  forest.Union(0, 1);  // {0,1} {2} {3} {4} {5}
//  forest.Union(0, 2);  // {0,1,2} {3} {4} {5}
//  // The below is also equivalent to forest.Union(0, 3) or forest.Union(1, 3).
//  forest.Union(2, 3);  // {0,1,2,3} {4} {5}
//  forest.Union(4, 5);  // {0,1,2,3} {4,5}
//  EXPECT_EQ(forest.FindSet(1), forest.FindSet(2));
//  EXPECT_NE(forest.FindSet(3), forest.FindSet(4));
//  EXPECT_EQ(kNotFound, forest.FindSet(6));
//
// THREAD SAFETY
//
// This class is technically thread-compatible, because its const methods can
// safely be called concurrently without synchronization. However, as a
// practical matter thread compatibility is not particularly useful here,
// because the methods one might expect to be read-only (such as FindSet() and
// IsSingleton()) are not const. These methods are required to be non-const due
// to performance guarantees of this data structure, which require that the
// methods perform certain internal housekeeping operations even though the
// externally visible value doesn't change. As with all thread-compatible
// objects, concurrent calls to non-const methods should be synchronized (e.g.
// with a Mutex).
//
// LIMITATIONS
//
// No convenient way exists to enumerate the subsets managed by this data
// structure, nor to enumerate the members of a given subset. On the other hand,
// it is possible (though not currently implemented) to enumerate the set of
// elements belonging to the whole forest.
//
// If enumerating subsets or their members is desired, the caller will need to
// implement code like the following. This code performs the GROUP BY operation;
// it requires that the caller already know the set of all elements contained in
// the forest.
//
// map<T, vector<T>> grouping;
// for (const auto& element : all_elements) {
//   grouping[forest.FindSet(element)].push_back(element);
// }

#ifndef THIRD_PARTY_GLOOP_UTIL_SET_DISJOINT_SET_FOREST_H_
#define THIRD_PARTY_GLOOP_UTIL_SET_DISJOINT_SET_FOREST_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/container/node_hash_map.h"
#include "absl/log/check.h"
#include "absl/types/span.h"

template <class T>
struct ObjectData {
  T parent;
  int rank;

  ObjectData() = default;

  template <typename X>
  explicit ObjectData(const X& x) : parent(x), rank(0) {}
};

template <
    typename T,
    typename HashFcn = typename absl::node_hash_map<T, ObjectData<T>>::hasher,
    typename EqualKey =
        typename absl::node_hash_map<T, ObjectData<T>, HashFcn>::key_equal,
    typename Alloc = std::allocator<T>>
class DisjointSetForest {
 public:
  // 'notfound_value' is the value that will be returned by FindSet when the
  // sought-for element does not exist in any subset. 'notfound_value' should
  // be a value which is outside the population of the data to be inserted into
  // the set. It is an error to pass 'notfound_value' to MakeSet() or Union().
  explicit DisjointSetForest(T notfound_value);

  // Provides a size hint to the underlying element storage container,
  // indicating that it should be prepared to store at least 'hint' number of
  // elements. This is only a hint and might be ignored.
  void resize(int32_t hint) { objects_.reserve(hint); }

  // Clears the DisjointSetForest.
  void clear();

  // Returns the number of subsets in the forest. The subsets are disjoint by
  // definition.
  int num_disjoint_sets() const { return num_disjoint_sets_; }

  // Returns the total number of objects over all subsets in the forest.
  int num_objects() const { return objects_.size(); }

  // Finds the subset that 'x' belongs to and returns that set's representative
  // element. If 'x' does not belong to any subset in the forest, returns
  // 'notfound_value_'.
  template <typename X>
  T FindSet(const X& x);

  // Finds the subset that 'x' belongs to and returns true if that subset
  // is a singleton (i.e. if the subset consists solely of element 'x') or if
  // 'x' does not belong to any subset in the forest. Otherwise, returns
  // false. Note: the rationale for returning true in the latter case of 'x'
  // not being found is that "'x' is a singleton that you haven't added yet."
  template <typename X>
  bool IsSingleton(const X& x);

  // If 'x' is already an element of some subset in the forest, returns false.
  // Otherwise, adds the singleton subset {x} to the forest and returns true.
  template <typename X>
  bool MakeSet(const X& x);

  // If 'x' and 'y' are both already members of the same subset in the forest,
  // return false. Otherwise do the following:
  // 1. If either 'x', 'y', or both are absent from the forest, add singletons
  //    {x} and/or {y} as appropriate to the forest.
  // 2. Find the sets to which 'x' and 'y' belong, merge those sets together,
  //    and return true.
  // Note: 'x' and 'y' can be any elements of the subsets which are to be
  // merged (it is not necessary for them to be representatives).
  // After this method returns, it will forevermore hold true (unless the data
  // structure is reset by an operation like clear()) that
  // FindSet(x)==FindSet(y).
  bool Union(const T& x, const T& y);
  // Heterogeneous lookup version of Union.
  template <typename X, typename Y>
  bool Union(const X& x, const Y& y);

  // Unions together the sets of many elements, adding them like MakeSet() if
  // necessary. The result is equivalent to (though somewhat faster than)
  // (1) calling MakeSet(x) on every x in the range [begin, end) and then
  // (2) calling Union(*begin, x) on every x in the range [begin+1, end).
  // The performance advantage is especially noticeable when the elements in the
  // sequence are likely to already be present in the forest. The method does
  // nothing if begin == end.
  template <typename Iterator>
  void UnionManyIterator(Iterator begin, Iterator end);

  // Invokes 'UnionManyIterator' on all the elements in the vector.
  void UnionMany(absl::Span<const T> elements) {
    UnionManyIterator(elements.begin(), elements.end());
  }

 private:
  using ObjectMap =
      absl::node_hash_map<T, ObjectData<T>, HashFcn, EqualKey, Alloc>;
  // Locates the ObjectData<T> corresponding to element 'x' in the underlying
  // container and returns a reference to it. This method must only be called
  // on elements known to already be in the forest.
  ObjectData<T>& FindData(const T& x);

  // A helper method for Union, used when it is known that 'x' is already in the
  // forest.
  template <typename Y>
  bool UnionHelper(const T& x, ObjectData<T>* absl_nonnull x_data_ptr,
                   const Y& y);

  // Returns a pointer to the ObjectData of the representative of the subset
  // containing 'x'. Additionally, while searching for the representative,
  // rewires each element on the search path so that it points directly to the
  // representative. This rewiring tends to make subsequent searches faster and
  // is required in order for this data structure to meet its performance
  // guarantees. This method must only be called on elements known to already be
  // in the forest. 'x_data_ptr' is required to point to the ObjectData<T>
  // corresponding to element 'x' in the underlying container (such as returned
  // by FindData(x)).
  ObjectData<T>* absl_nonnull InternalFindSet(
      const T& x, ObjectData<T>* absl_nonnull x_data_ptr);

  // Combines two disjoint subsets into a larger subset. This is a helper method
  // for Union. 'x_repr_data_ptr' and 'y_repr_data_ptr' must be pointers to the
  // ObjectData of representatives (not just elements) of two distinct subsets.
  void Link(ObjectData<T>* absl_nonnull x_repr_data_ptr,
            ObjectData<T>* absl_nonnull y_repr_data_ptr);

  // The old version of UnionMany. This method will be kept around for a while
  // (but made private) just for the purposes of running the benchmarks.
  void UnionManyOld(absl::Span<const T> elements);

  ObjectMap objects_;
  int num_disjoint_sets_;
  const T notfound_value_;

  // A friend declaration, provided solely for the purpose of benchmarking
  // UnionManyOld().
  friend void InvokeUnionManyOld(const std::vector<int>& data,
                                 DisjointSetForest<int>& forest);
};

template <typename T, typename HashFcn, typename EqualKey, typename Alloc>
DisjointSetForest<T, HashFcn, EqualKey, Alloc>::DisjointSetForest(
    T notfound_value)
    : num_disjoint_sets_(0), notfound_value_(std::move(notfound_value)) {}

template <typename T, typename HashFcn, typename EqualKey, typename Alloc>
void DisjointSetForest<T, HashFcn, EqualKey, Alloc>::clear() {
  objects_.clear();
  num_disjoint_sets_ = 0;
}

template <typename T, typename HashFcn, typename EqualKey, typename Alloc>
template <typename X>
T DisjointSetForest<T, HashFcn, EqualKey, Alloc>::FindSet(const X& x) {
  typename ObjectMap::iterator iter = objects_.find(x);
  if (iter == objects_.end()) {
    return notfound_value_;
  }
  return InternalFindSet(iter->first, &(iter->second))->parent;
}

template <typename T, typename HashFcn, typename EqualKey, typename Alloc>
template <typename X>
bool DisjointSetForest<T, HashFcn, EqualKey, Alloc>::IsSingleton(const X& x) {
  typename ObjectMap::iterator iter = objects_.find(x);
  if (iter == objects_.end()) {
    return true;
  }
  return EqualKey()(iter->first, iter->second.parent) && iter->second.rank == 0;
}

template <typename T, typename HashFcn, typename EqualKey, typename Alloc>
template <typename X>
bool DisjointSetForest<T, HashFcn, EqualKey, Alloc>::MakeSet(const X& x) {
  if (objects_.try_emplace(x, x).second) {
    ++num_disjoint_sets_;
    return true;
  } else {
    return false;
  }
}

template <typename T, typename HashFcn, typename EqualKey, typename Alloc>
bool DisjointSetForest<T, HashFcn, EqualKey, Alloc>::Union(const T& x,
                                                           const T& y) {
  auto [iter, inserted] = objects_.try_emplace(x, x);
  if (inserted) ++num_disjoint_sets_;
  return UnionHelper(x, &iter->second, y);
}

template <typename T, typename HashFcn, typename EqualKey, typename Alloc>
template <typename X, typename Y>
bool DisjointSetForest<T, HashFcn, EqualKey, Alloc>::Union(const X& x,
                                                           const Y& y) {
  auto [iter, inserted] = objects_.try_emplace(x, x);
  if (inserted) ++num_disjoint_sets_;
  return UnionHelper(iter->first, &iter->second, y);
}

template <typename T, typename HashFcn, typename EqualKey, typename Alloc>
template <typename Y>
bool DisjointSetForest<T, HashFcn, EqualKey, Alloc>::UnionHelper(
    const T& x, ObjectData<T>* absl_nonnull x_data_ptr, const Y& y) {
  auto [iter, inserted] = objects_.try_emplace(y, y);
  if (inserted) ++num_disjoint_sets_;

  ObjectData<T>* x_repr_data_ptr = InternalFindSet(x, x_data_ptr);
  ObjectData<T>* y_repr_data_ptr = InternalFindSet(iter->first, &iter->second);
  if (!EqualKey()(x_repr_data_ptr->parent, y_repr_data_ptr->parent)) {
    Link(x_repr_data_ptr, y_repr_data_ptr);
    --num_disjoint_sets_;
    return true;
  } else {
    return false;
  }
}

template <typename T, typename HashFcn, typename EqualKey, typename Alloc>
template <typename Iterator>
void DisjointSetForest<T, HashFcn, EqualKey, Alloc>::UnionManyIterator(
    Iterator begin, Iterator end) {
  if (begin == end) {
    return;
  }
  MakeSet(*begin);
  ObjectData<T>* begin_data_ptr = &objects_[*begin];

  auto next = begin;
  ++next;
  while (next != end) {
    auto ip = objects_.find(*next);
    if (ip == objects_.end()) {
      // *next is new, so there is definitely a need to perform the Union.
      UnionHelper(*begin, begin_data_ptr, *next);
    } else {
      // *next is in the forest already. If the parent pointers already match,
      // then there is no need to perform the Union operation.
      if (begin_data_ptr->parent != ip->second.parent) {
        UnionHelper(*begin, begin_data_ptr, *next);
      }
    }
    ++next;
  }
}

template <typename T, typename HashFcn, typename EqualKey, typename Alloc>
void DisjointSetForest<T, HashFcn, EqualKey, Alloc>::UnionManyOld(
    absl::Span<const T> elements) {
  std::vector<ObjectData<T>* absl_nonnull> object_cache;

  // Insert all vertices into objects_, or verify that they are
  // already there. At the same time, cache the pointers to the
  // ObjectData structs for all vertices in object_cache.
  for (size_t i = 0; i < elements.size(); ++i) {
    if (objects_.count(elements[i]) == 0) {
      // This code mimics MakeSet, but holds on to the iterator for
      // the new object, saving us a lookup.  We then cache a
      // pointer to the ObjectData itself in object_cache.
      typename ObjectMap::iterator new_val =
          ((objects_.insert(
                std::make_pair(elements[i], ObjectData<T>(elements[i]))))
               .first);
      ++num_disjoint_sets_;
      object_cache.push_back(&(new_val->second));
    } else {
      object_cache.push_back(&(objects_[elements[i]]));
    }
  }

  // Pairwise union the first vertex with each subsequent
  // vertex. Only Union if the parent pointers of the vertices do
  // not match.
  for (size_t i = 1; i < object_cache.size(); ++i) {
    if ((*(object_cache[i])).parent != (*(object_cache[0])).parent) {
      Union(elements[i], elements[0]);
    }
  }
}

template <typename T, typename HashFcn, typename EqualKey, typename Alloc>
ObjectData<T>& DisjointSetForest<T, HashFcn, EqualKey, Alloc>::FindData(
    const T& x) {
  typename ObjectMap::iterator x_iter = objects_.find(x);
  CHECK(x_iter != objects_.end());
  return (*x_iter).second;
}

template <typename T, typename HashFcn, typename EqualKey, typename Alloc>
ObjectData<T>* absl_nonnull
DisjointSetForest<T, HashFcn, EqualKey, Alloc>::InternalFindSet(
    const T& x, ObjectData<T>* absl_nonnull x_data_ptr) {
  if (!EqualKey()(x, x_data_ptr->parent)) {
    ObjectData<T>& parent_data = FindData(x_data_ptr->parent);
    ObjectData<T>* repr_data_ptr =
        InternalFindSet(x_data_ptr->parent, &parent_data);
    x_data_ptr->parent = repr_data_ptr->parent;
    return repr_data_ptr;
  } else {
    return x_data_ptr;
  }
}

template <typename T, typename HashFcn, typename EqualKey, typename Alloc>
void DisjointSetForest<T, HashFcn, EqualKey, Alloc>::Link(
    ObjectData<T>* absl_nonnull x_repr_data_ptr,
    ObjectData<T>* absl_nonnull y_repr_data_ptr) {
  if (x_repr_data_ptr->rank < y_repr_data_ptr->rank) {
    x_repr_data_ptr->parent = y_repr_data_ptr->parent;
  } else {
    y_repr_data_ptr->parent = x_repr_data_ptr->parent;

    if (x_repr_data_ptr->rank == y_repr_data_ptr->rank) {
      ++x_repr_data_ptr->rank;
    }
  }
}

#endif  // THIRD_PARTY_GLOOP_UTIL_SET_DISJOINT_SET_FOREST_H_
