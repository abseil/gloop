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

// Some of these functions are faster than their built-in alternatives. Some
// have a more Google-friendly API and are easier to use.
//

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_STL_UTIL_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_STL_UTIL_H_

#include <stddef.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/resize_and_overwrite.h"
#include "gloop/util/gtl/requires.h"

namespace gtl {
namespace internal {
template <typename LessFunc>
class Equiv {
 public:
  explicit Equiv(const LessFunc& f) : f_(f) {}
  template <typename T>
  bool operator()(const T& a, const T& b) const {
    return !f_(b, a) && !f_(a, b);
  }

 private:
  LessFunc f_;
};
}  // namespace internal

// Sorts and removes duplicates from a sequence container.
// If specified, the 'less_func' is used to compose an
// equivalence comparator for the sorting and uniqueness tests.
template <typename T, typename LessFunc>
inline void STLSortAndRemoveDuplicates(T* v, const LessFunc& less_func) {
  std::sort(v->begin(), v->end(), less_func);
  v->erase(std::unique(v->begin(), v->end(),
                       gtl::internal::Equiv<LessFunc>(less_func)),
           v->end());
}
template <typename T>
inline void STLSortAndRemoveDuplicates(T* v) {
  std::sort(v->begin(), v->end());
  v->erase(std::unique(v->begin(), v->end()), v->end());
}

// Stable sorts and removes duplicates from a sequence container, retaining
// the first equivalent element for each equivalence set.
// The 'less_func' is used to compose an equivalence comparator for the sorting
// and uniqueness tests.
template <typename T, typename LessFunc>
inline void STLStableSortAndRemoveDuplicates(T* v, const LessFunc& less_func) {
  std::stable_sort(v->begin(), v->end(), less_func);
  v->erase(std::unique(v->begin(), v->end(),
                       gtl::internal::Equiv<LessFunc>(less_func)),
           v->end());
}
// Stable sorts and removes duplicates from a sequence container, retaining
// the first equivalent element for each equivalence set, using < comparison and
// == equivalence testing.
template <typename T>
inline void STLStableSortAndRemoveDuplicates(T* v) {
  std::stable_sort(v->begin(), v->end());
  v->erase(std::unique(v->begin(), v->end()), v->end());
}

// Remove every occurrence of element e in v.
template <typename T, typename E>
void STLEraseAllFromSequence(T* v, const E& e) {
  if constexpr (gtl::Requires<T, E>(
                    [](auto&& c, auto&& x) -> decltype(c.remove(x)) {})) {
    v->remove(e);
  } else {
    // See http://en.wikipedia.org/wiki/Erase-remove_idiom.
    v->erase(std::remove(v->begin(), v->end(), e), v->end());
  }
}

// Remove each element e in v satisfying pred(e).
template <typename T, typename P>
void STLEraseAllFromSequenceIf(T* v, P&& pred) {
  if constexpr (gtl::Requires<T, P&&>(
                    [](auto&& c, auto&& p) -> decltype(c.remove_if(
                                               std::forward<P>(p))) {})) {
    v->remove_if(std::forward<P>(pred));
  } else {
    v->erase(std::remove_if(v->begin(), v->end(), std::forward<P>(pred)),
             v->end());
  }
}

// Clears internal memory of an STL object by swapping the argument with a new,
// empty object. STL clear()/reserve(0) does not always free internal memory
// allocated.
template <typename T>
void STLClearObject(T* obj) {
  T tmp;
  tmp.swap(*obj);
  // This reserve(0) is needed because "T tmp" sometimes allocates memory (arena
  // implementation?), even though this may not always work.
  obj->reserve(0);
}
// STLClearObject overload for deque, which is missing reserve().
template <typename T, typename A>
void STLClearObject(std::deque<T, A>* obj) {
  std::deque<T, A> tmp;
  tmp.swap(*obj);
}

// Calls STLClearObject() if the object is bigger than the specified limit,
// otherwise calls the object's clear() member. This can be useful if you want
// to allow the object to hold on to its allocated memory as long as it's not
// too much.
//
// Note: The name is misleading since the object is always cleared, regardless
// of its size.
template <typename T>
inline void STLClearIfBig(T* obj, size_t limit = 1 << 20) {
  if (obj->capacity() >= limit) {
    STLClearObject(obj);
  } else {
    obj->clear();
  }
}
// STLClearIfBig overload for deque, which is missing capacity().
template <typename T, typename A>
inline void STLClearIfBig(std::deque<T, A>* obj, size_t limit = 1 << 20) {
  if (obj->size() >= limit) {
    STLClearObject(obj);
  } else {
    obj->clear();
  }
}

// Removes all elements and reduces the number of buckets in a hash_set or
// hash_map back to the default if the current number of buckets is "limit" or
// more.
//
// Adding items to a hash container may add buckets, but removing items or
// calling clear() does not necessarily reduce the number of buckets. Having
// lots of buckets is good if you insert comparably many items in every
// iteration because you'll reduce collisions and table resizes. But having lots
// of buckets is bad if you insert few items in most subsequent iterations,
// because repeatedly clearing out all those buckets can get expensive.
//
// One solution is to call STLClearHashIfBig() with a "limit" value that is a
// small multiple of the typical number of items in your table. In the common
// case, this is equivalent to an ordinary clear. In the rare case where you
// insert a lot of items, the number of buckets is reset to the default to keep
// subsequent clear operations cheap. Note that the default number of buckets is
// 193 in the Gnu library implementation as of Jan '08.
template <typename T>
inline void STLClearHashIfBig(T* obj, size_t limit) {
  if (obj->bucket_count() >= limit) {
    T tmp;
    tmp.swap(*obj);
  } else {
    obj->clear();
  }
}

// Like str->resize(new_size), except any new characters added to "*str" as a
// result of resizing may be left uninitialized, rather than being filled with
// '0' bytes. Typically used when code is then going to overwrite the backing
// store of the string with known data.
template <typename T, typename Traits, typename Alloc>
[[deprecated("Use absl::StringResizeAndOverwrite() instead")]]
inline void STLStringResizeUninitialized(std::basic_string<T, Traits, Alloc>* s,
                                         size_t new_size) {
  // This calls absl::strings_internal::StringResizeAndOverwriteImpl() because
  // the public API absl::StringResizeAndOverwrite() verifies that the required
  // range has been initialized. No other code should be calling
  // absl::strings_internal::StringResizeAndOverwriteImpl(). Instead it should
  // be implemented correctly with absl::StringResizeAndOverwrite().
  //
  // TODO: b/446221957 - It is undefined behavior if any character in the range
  // [0, return_value) is uninitialized, but we rely on this here to implement
  // the old STLStringResizeUninitialized() API. Fix this by migrating to
  // absl::StringResizeAndOverwrite().
  absl::strings_internal::StringResizeAndOverwriteImpl(
      *s, new_size, [](T*, size_t n) { return n; });
}

// Like STLStringResizeUninitialized(str, new_size), except guaranteed to use
// exponential growth so that the amortized complexity of increasing the string
// size by a small amount is O(1), in contrast to O(str->size()) in the case of
// precise growth.
//
// TODO: b/446221957 - Find a recommended replacement and deprecate this
// function.
template <typename T, typename Traits, typename Alloc>
void STLStringResizeUninitializedAmortized(
    std::basic_string<T, Traits, Alloc>* s, size_t new_size) {
  if (new_size > s->size()) {
    if (new_size > s->capacity()) {
      // Make sure to always grow by at least a factor of 2x. Change min_growth
      // if you want to experiment with other growth strategies.
      const auto min_growth = s->capacity();
      if (ABSL_PREDICT_FALSE(s->capacity() > s->max_size() - min_growth)) {
        s->reserve(s->max_size());
      } else if (new_size < s->capacity() + min_growth) {
        s->reserve(s->capacity() + min_growth);
      }
    }
    // This calls absl::strings_internal::StringResizeAndOverwriteImpl() because
    // the public API absl::StringResizeAndOverwrite() verifies that the
    // required range has been initialized. No other code should be calling
    // absl::strings_internal::StringResizeAndOverwriteImpl(). Instead it should
    // be implemented correctly with absl::StringResizeAndOverwrite().
    absl::strings_internal::StringResizeAndOverwriteImpl(
        *s, new_size, [](T*, size_t buf_size) {
          // TODO: b/446221957 - It is undefined behavior if any character in
          // the range [0, return_value) is uninitialized, but we rely on this
          // here to implement the old STLStringResizeUninitializedAmortized()
          // API.
          return buf_size;
        });
  } else {
    s->erase(new_size);
  }
}

// Like str->resize(new_size), except guaranteed to use exponential growth so
// that the amortized complexity of increasing the string size by a small amount
// is O(1), in contrast to O(str->size()) in the case of precise growth.
template <typename T, typename Traits, typename Alloc>
void STLStringResizeAmortized(std::basic_string<T, Traits, Alloc>* s,
                              size_t new_size) {
  const size_t size = s->size();
  if (new_size > size) {
    s->append(new_size - size, T());
  } else {
    s->erase(new_size);
  }
}

// DEPRECATED: Assigns the n bytes starting at ptr to the given string.
ABSL_DEPRECATE_AND_INLINE()
inline void STLAssignToString(std::string* str, const char* ptr, size_t n) {
  str->assign(ptr, n);
}

// Returns a mutable char* pointing to a string's internal buffer (which is
// null-terminated). Returns nullptr for an empty string (whereas
// string("").data() returns a pointer to a single null character).
// If non-null, writing through this pointer will modify the string.
//
// string_as_array(&str)[i] is valid for 0 <= i < str.size() until the
// next call to a string method that invalidates iterators.
[[deprecated("Use `str->data()` instead.")]]
inline char* absl_nullable string_as_array(std::string* absl_nonnull str) {
  return str->empty() ? nullptr : str->data();
}

// Tests two hash maps/sets for equality. This exists because operator== in the
// STL can return false when the maps/sets contain identical elements. This is
// because it compares the internal hash tables which may be different if the
// order of insertions and deletions differed.
template <typename HashSet>
inline bool HashSetEquality(const HashSet& set_a, const HashSet& set_b) {
  if (set_a.size() != set_b.size()) return false;
  for (typename HashSet::const_iterator i = set_a.begin(); i != set_a.end();
       ++i)
    if (set_b.find(*i) == set_b.end()) return false;
  return true;
}

// WARNING: Using HashMapEquality for multiple-associative containers like
// multimap and hash_multimap will result in wrong behavior.

template <typename HashMap, typename BinaryPredicate>
inline bool HashMapEquality(const HashMap& map_a, const HashMap& map_b,
                            BinaryPredicate mapped_type_equal) {
  if (map_a.size() != map_b.size()) return false;
  for (typename HashMap::const_iterator i = map_a.begin(); i != map_a.end();
       ++i) {
    typename HashMap::const_iterator j = map_b.find(i->first);
    if (j == map_b.end()) return false;
    if (!mapped_type_equal(i->second, j->second)) return false;
  }
  return true;
}

// We overload for 'map' without a specialized functor and simply use its
// operator== function.
template <typename K, typename V, typename C, typename A>
inline bool HashMapEquality(const std::map<K, V, C, A>& map_a,
                            const std::map<K, V, C, A>& map_b) {
  return map_a == map_b;
}

template <typename HashMap>
inline bool HashMapEquality(const HashMap& a, const HashMap& b) {
  using Mapped = typename HashMap::mapped_type;
  return HashMapEquality(a, b, std::equal_to<Mapped>());
}

// Calls delete (non-array version) on pointers in the range [begin, end).
//
// Note: If you're calling this on an entire container, you probably want to
// call STLDeleteElements(&container) instead (which also clears the container),
// or use an ElementDeleter.
template <typename ForwardIterator>
void STLDeleteContainerPointers(ForwardIterator begin, ForwardIterator end) {
  while (begin != end) {
    auto temp = begin;
    ++begin;
    delete *temp;
  }
}

// Calls delete (non-array version) on BOTH items (pointers) in each pair in the
// range [begin, end).
template <typename ForwardIterator>
void STLDeleteContainerPairPointers(ForwardIterator begin,
                                    ForwardIterator end) {
  while (begin != end) {
    auto temp = begin;
    ++begin;
    delete temp->first;
    delete temp->second;
  }
}

// Calls delete (non-array version) on the FIRST item (pointer) in each pair in
// the range [begin, end).
template <typename ForwardIterator>
void STLDeleteContainerPairFirstPointers(ForwardIterator begin,
                                         ForwardIterator end) {
  while (begin != end) {
    auto temp = begin;
    ++begin;
    delete temp->first;
  }
}

// Calls delete (non-array version) on the SECOND item (pointer) in each pair in
// the range [begin, end).
//
// Note: If you're calling this on an entire container, you probably want to
// call STLDeleteValues(&container) instead, or use ValueDeleter.
template <typename ForwardIterator>
void STLDeleteContainerPairSecondPointers(ForwardIterator begin,
                                          ForwardIterator end) {
  while (begin != end) {
    auto temp = begin;
    ++begin;
    delete temp->second;
  }
}

// Deletes all the elements in an STL container and clears the container. This
// function is suitable for use with a vector, set, hash_set, or any other STL
// container which defines sensible begin(), end(), and clear() methods.
//
// If container is nullptr, this function is a no-op.
//
// As an alternative to calling STLDeleteElements() directly, consider
// ElementDeleter (defined below), which ensures that your container's elements
// are deleted when the ElementDeleter goes out of scope.
template <typename T>
std::enable_if_t<
    std::is_pointer_v<std::decay_t<decltype(*std::declval<T>().begin())>>>
STLDeleteElements(T* container) {
  if (!container) return;
  STLDeleteContainerPointers(container->begin(), container->end());
  container->clear();
}

// Overload for migration of container value types to smart pointers.
template <typename T>
ABSL_DEPRECATE_AND_INLINE()
std::enable_if_t<!std::is_pointer_v<std::decay_t<
    decltype(*std::declval<T>().begin())>>> STLDeleteElements(T* absl_nonnull
                                                                  container) {
  container->clear();
}

// Given an STL container consisting of (key, value) pairs, STLDeleteValues
// deletes all the "value" components and clears the container. Does nothing in
// the case it's given a nullptr.
template <typename T>
std::enable_if_t<std::is_pointer_v<
    std::decay_t<decltype(std::declval<T>().begin()->second)>>>
STLDeleteValues(T* v) {
  if (!v) return;
  (STLDeleteContainerPairSecondPointers)(v->begin(), v->end());
  v->clear();
}

// Overload for migration of map value types to smart pointers.
template <typename T>
ABSL_DEPRECATE_AND_INLINE()
std::enable_if_t<!std::is_pointer_v<std::decay_t<
    decltype(std::declval<T>()
                 .begin()
                 ->second)>>> STLDeleteValues(T* absl_nonnull container) {
  container->clear();
}

// ElementDeleter is an RAII (<link>) object that deletes the elements in the
// given container when it goes out of scope. This is similar to
// std::unique_ptr<> except that a container's elements will be deleted rather
// than the container itself.
//
// Example:
//   std::vector<MyProto*> tmp_proto;
//   ElementDeleter d(&tmp_proto);
//   if (...) return false;
//   ...
//   return success;
//
// Since C++11, consider using containers of std::unique_ptr instead.
class ElementDeleter {
 public:
  template <typename STLContainer>
  explicit ElementDeleter(STLContainer* ptr)
      : ptr_(ptr), deleter_([](void* container) {
          STLDeleteElements(static_cast<STLContainer*>(container));
        }) {}

  ~ElementDeleter() { deleter_(ptr_); }

  ElementDeleter(const ElementDeleter&) = delete;
  void operator=(const ElementDeleter&) = delete;

 private:
  void* ptr_;
  void (*deleter_)(void*);
};

// ValueDeleter is an RAII (<link>) object that deletes the 'second' member in
// the given container of std::pair<>s when it goes out of scope.
//
// Example:
//   std::map<string, Foo*> foo_map;
//   ValueDeleter d(&foo_map);
//   if (...) return false;
//   ...
//   return success;
class ValueDeleter {
 public:
  template <typename STLContainer>
  explicit ValueDeleter(STLContainer* ptr)
      : ptr_(ptr), deleter_([](void* container) {
          STLDeleteValues(static_cast<STLContainer*>(container));
        }) {}

  ~ValueDeleter() { deleter_(ptr_); }

  ValueDeleter(const ValueDeleter&) = delete;
  void operator=(const ValueDeleter&) = delete;

 private:
  void* ptr_;
  void (*deleter_)(void*);
};

// RAII (<link>) object that deletes elements in the given container when it
// goes out of scope. Like ElementDeleter (above) except that this class is
// templated and doesn't have a virtual destructor.
//
// New code should prefer ElementDeleter.
template <typename STLContainer>
class STLElementDeleter {
 public:
  STLElementDeleter(STLContainer* ptr) : container_ptr_(ptr) {}
  ~STLElementDeleter() { STLDeleteElements(container_ptr_); }

 private:
  STLContainer* container_ptr_;
};

// RAII (<link>) object that deletes the values in the given container of
// std::pair<>s when it goes out of scope. Like ValueDeleter (above) except that
// this class is templated and doesn't have a virtual destructor.
//
// New code should prefer ValueDeleter.
template <typename STLContainer>
class STLValueDeleter {
 public:
  STLValueDeleter(STLContainer* ptr) : container_ptr_(ptr) {}
  ~STLValueDeleter() { STLDeleteValues(container_ptr_); }

 private:
  STLContainer* container_ptr_;
};

// Sets the referenced pointer to nullptr and returns its original value. This
// can be a convenient way to remove a pointer from a container to avoid the
// eventual deletion by an ElementDeleter.
//
// Example:
//
//   std::vector<Foo*> v{new Foo, new Foo, new Foo};
//   ElementDeleter d(&v);
//   Foo* safe = release_ptr(&v[1]);
//   // v[1] is now nullptr and the Foo it previously pointed to is now
//   // stored in "safe"
template <typename T>
ABSL_MUST_USE_RESULT T* release_ptr(T** ptr) {
  assert(ptr);
  return std::exchange(*ptr, nullptr);
}

namespace stl_util_internal {

// Trait to detect whether a type T is an hash table.
// The heuristic used is that the type contains an inner type `hasher` and does
// not contain an inner type `reverse_iterator`.
// If the container is iterable in reverse, then order might actually matter.
template <typename, typename = void, typename = void>
struct Unordered : std::false_type {};

template <typename T>
struct Unordered<T, absl::void_t<typename T::hasher>> : std::true_type {};

template <typename T>
struct Unordered<T, absl::void_t<typename T::hasher>,
                 absl::void_t<typename T::reverse_iterator>> : std::false_type {
};

// TODO: Replace with the proper type trait.
template <typename T>
inline constexpr bool is_forward_iterator =
    std::is_base_of_v<std::forward_iterator_tag,
                      typename std::iterator_traits<T>::iterator_category>;

}  // namespace stl_util_internal

// STLSetDifference:
//
//     In1 STLSetDifference(a, b);
//     In1 STLSetDifference(a, b, compare);
//     void STLSetDifference(a, b, &out);
//     void STLSetDifference(a, b, &out, compare);
//     Out STLSetDifferenceAs<Out>(a, b);
//     Out STLSetDifferenceAs<Out>(a, b, compare);
//
// Appends the elements in "a" that are not in "b" to an output container.
// Optionally specify a comparator, or '<' is used by default.  Both input
// containers must be sorted with respect to the comparator.  If specified,
// the output container must be distinct from both "a" and "b".
//
// If an output container pointer is not given, a container will be returned
// by value. The return type can be explicitly specified by calling
// STLSetDifferenceAs, but it defaults to the type of argument "a".
//
// See std::set_difference() for details on how set difference is computed.
//
// The form taking 4 arguments. All other forms call into this one.
// Explicit comparator, append to output container.
template <typename In1, typename In2, typename Out, typename Compare>
void STLSetDifference(const In1& a, const In2& b, Out* out, Compare compare) {
  static_assert(!gtl::stl_util_internal::Unordered<In1>::value,
                "In1 must be an ordered set");
  static_assert(!gtl::stl_util_internal::Unordered<In2>::value,
                "In2 must be an ordered set");
  assert(std::is_sorted(a.begin(), a.end(), compare));
  assert(std::is_sorted(b.begin(), b.end(), compare));
  assert(static_cast<const void*>(&a) != static_cast<const void*>(out));
  assert(static_cast<const void*>(&b) != static_cast<const void*>(out));
  std::set_difference(a.begin(), a.end(), b.begin(), b.end(),
                      std::inserter(*out, out->end()), compare);
}
// Append to output container, Implicit comparator.
// Note: The 'enable_if' keeps this overload from participating in
// overload resolution if 'out' is a function pointer, gracefully forcing
// the 3-argument overload that treats the third argument as a comparator.
template <typename In1, typename In2, typename Out>
std::enable_if_t<!std::is_function_v<Out>, void> STLSetDifference(const In1& a,
                                                                  const In2& b,
                                                                  Out* out) {
  STLSetDifference(a, b, out, std::less<>{});
}
// Explicit comparator, explicit return type.
template <typename Out, typename In1, typename In2, typename Compare>
Out STLSetDifferenceAs(const In1& a, const In2& b, Compare compare) {
  Out out;
  STLSetDifference(a, b, &out, compare);
  return out;
}
// Implicit comparator, explicit return type.
template <typename Out, typename In1, typename In2>
Out STLSetDifferenceAs(const In1& a, const In2& b) {
  return STLSetDifferenceAs<Out>(a, b, std::less<>{});
}
// Explicit comparator, implicit return type.
template <typename In1, typename In2, typename Compare>
In1 STLSetDifference(const In1& a, const In2& b, Compare compare) {
  return STLSetDifferenceAs<In1>(a, b, compare);
}
// Implicit comparator, implicit return type.
template <typename In1, typename In2>
In1 STLSetDifference(const In1& a, const In2& b) {
  return STLSetDifference(a, b, std::less<>{});
}
template <typename In1>
In1 STLSetDifference(const In1& a, const In1& b) {
  return STLSetDifference(a, b, std::less<>{});
}

// STLSetUnion:
//
//     In1 STLSetUnion(a, b);
//     In1 STLSetUnion(a, b, compare);
//     void STLSetUnion(a, b, &out);
//     void STLSetUnion(a, b, &out, compare);
//     Out STLSetUnionAs<Out>(a, b);
//     Out STLSetUnionAs<Out>(a, b, compare);
// Appends the elements in one or both of the input containers to output
// container "out". Both input containers must be sorted with operator '<',
// or with the comparator if specified. "out" must be distinct from both "a"
// and "b".
//
// See std::set_union() for how set union is computed.
template <typename In1, typename In2, typename Out, typename Compare>
void STLSetUnion(const In1& a, const In2& b, Out* out, Compare compare) {
  static_assert(!gtl::stl_util_internal::Unordered<In1>::value,
                "In1 must be an ordered set");
  static_assert(!gtl::stl_util_internal::Unordered<In2>::value,
                "In2 must be an ordered set");
  assert(std::is_sorted(a.begin(), a.end(), compare));
  assert(std::is_sorted(b.begin(), b.end(), compare));
  assert(static_cast<const void*>(&a) != static_cast<const void*>(out));
  assert(static_cast<const void*>(&b) != static_cast<const void*>(out));
  std::set_union(a.begin(), a.end(), b.begin(), b.end(),
                 std::inserter(*out, out->end()), compare);
}
// Note: The 'enable_if' keeps this overload from participating in
// overload resolution if 'out' is a function pointer, gracefully forcing
// the 3-argument overload that treats the third argument as a comparator.
template <typename In1, typename In2, typename Out>
std::enable_if_t<!std::is_function_v<Out>, void> STLSetUnion(const In1& a,
                                                             const In2& b,
                                                             Out* out) {
  return STLSetUnion(a, b, out, std::less<>{});
}
template <typename Out, typename In1, typename In2, typename Compare>
Out STLSetUnionAs(const In1& a, const In2& b, Compare compare) {
  Out out;
  STLSetUnion(a, b, &out, compare);
  return out;
}
template <typename Out, typename In1, typename In2>
Out STLSetUnionAs(const In1& a, const In2& b) {
  return STLSetUnionAs<Out>(a, b, std::less<>{});
}
template <typename In1, typename In2, typename Compare>
In1 STLSetUnion(const In1& a, const In2& b, Compare compare) {
  return STLSetUnionAs<In1>(a, b, compare);
}
template <typename In1, typename In2>
In1 STLSetUnion(const In1& a, const In2& b) {
  return STLSetUnion(a, b, std::less<>{});
}
template <typename In1>
In1 STLSetUnion(const In1& a, const In1& b) {
  return STLSetUnion(a, b, std::less<>{});
}

// STLSetSymmetricDifference:
//
//     In1 STLSetSymmetricDifference(a, b);
//     In1 STLSetSymmetricDifference(a, b, compare);
//     void STLSetSymmetricDifference(a, b, &out);
//     void STLSetSymmetricDifference(a, b, &out, compare);
//     Out STLSetSymmetricDifferenceAs<Out>(a, b);
//     Out STLSetSymmetricDifferenceAs<Out>(a, b, compare);
//
// Appends the elements in "a" that are not in "b", and the elements in "b"
// that are not in "a", to output container "out". Both input containers
// must be sorted with operator '<', or with the comparator if specified.
// "out" must be distinct from both "a" and "b".
//
// See std::set_symmetric_difference() for how these elements are selected.
template <typename In1, typename In2, typename Out, typename Compare>
void STLSetSymmetricDifference(const In1& a, const In2& b, Out* out,
                               Compare compare) {
  static_assert(!gtl::stl_util_internal::Unordered<In1>::value,
                "In1 must be an ordered set");
  static_assert(!gtl::stl_util_internal::Unordered<In2>::value,
                "In2 must be an ordered set");
  assert(std::is_sorted(a.begin(), a.end(), compare));
  assert(std::is_sorted(b.begin(), b.end(), compare));
  assert(static_cast<const void*>(&a) != static_cast<const void*>(out));
  assert(static_cast<const void*>(&b) != static_cast<const void*>(out));
  std::set_symmetric_difference(a.begin(), a.end(), b.begin(), b.end(),
                                std::inserter(*out, out->end()), compare);
}
// Note: The 'enable_if' keeps this overload from participating in
// overload resolution if 'out' is a function pointer, gracefully forcing
// the 3-argument overload that treats the third argument as a comparator.
template <typename In1, typename In2, typename Out>
std::enable_if_t<!std::is_function_v<Out>, void> STLSetSymmetricDifference(
    const In1& a, const In2& b, Out* out) {
  return STLSetSymmetricDifference(a, b, out, std::less<>{});
}
template <typename Out, typename In1, typename In2, typename Compare>
Out STLSetSymmetricDifferenceAs(const In1& a, const In2& b, Compare comp) {
  Out out;
  STLSetSymmetricDifference(a, b, &out, comp);
  return out;
}
template <typename Out, typename In1, typename In2>
Out STLSetSymmetricDifferenceAs(const In1& a, const In2& b) {
  return STLSetSymmetricDifferenceAs<Out>(a, b, std::less<>{});
}
template <typename In1, typename In2, typename Compare>
In1 STLSetSymmetricDifference(const In1& a, const In2& b, Compare comp) {
  return STLSetSymmetricDifferenceAs<In1>(a, b, comp);
}
template <typename In1, typename In2>
In1 STLSetSymmetricDifference(const In1& a, const In2& b) {
  return STLSetSymmetricDifference(a, b, std::less<>{});
}
template <typename In1>
In1 STLSetSymmetricDifference(const In1& a, const In1& b) {
  return STLSetSymmetricDifference(a, b, std::less<>{});
}

// Calculates the three categories of the input sets in a single pass. These are
// - left set only (std::set_difference(left, right))
// - both sets (std::set_intersection(left, right))
// - right set only (std::set_difference(right, left))
//
// Both containers must be sorted by the specified comparator, or by '<' if none
// is given.
//
// Output destinations are iterators, not containers. If you do not need one of
// the results (such as the intersection) you can pass
// gtl::NoOpOutputIterator to ignore it. If you do not need two of the
// results, you should call the relevant algorithm listed above instead.
template <int... barrier, typename Left, typename Right, typename LeftOnlyOut,
          typename BothOut, typename RightOnlyOut, typename Compare>
void STLSetSymmetricDifference(const Left& left, const Right& right,
                               LeftOnlyOut left_only, BothOut both,
                               RightOnlyOut right_only, Compare compare) {
  static_assert(!gtl::stl_util_internal::Unordered<Left>::value,
                "Left must be an ordered set");
  static_assert(!gtl::stl_util_internal::Unordered<Right>::value,
                "Right must be an ordered set");
  // TODO: Check that LeftOnlyOut, BothOut and RightOnlyOut are
  // output iterators.
  static_assert(
      std::is_same_v<std::decay_t<typename Left::value_type>,
                     std::decay_t<typename Right::value_type>>,
      "Left and Right collections must have the same value_type, since one "
      "value will be arbitrarily chosen for the intersecting set.");
  auto left_it = left.begin();
  auto right_it = right.begin();
  // Can only perform is_sorted asserts if you can iterate multiple times.
  if (stl_util_internal::is_forward_iterator<decltype(left_it)>) {
    assert(std::is_sorted(left.begin(), left.end(), compare));
  }
  if (stl_util_internal::is_forward_iterator<decltype(right_it)>) {
    assert(std::is_sorted(right.begin(), right.end(), compare));
  }

  while (left_it != left.end() && right_it != right.end()) {
    if (compare(*left_it, *right_it)) {
      *left_only = *left_it;
      ++left_only;
      ++left_it;
      continue;
    }
    if (compare(*right_it, *left_it)) {
      *right_only = *right_it;
      ++right_only;
      ++right_it;
      continue;
    }
    // Values compare equivalent. Pick left value arbitrarily in non-debug
    // builds, pick a random value in debug builds to avoid dependence on the
    // choice.
#ifdef NDEBUG
    *both = *left_it;
#else
    char stack_char;
    *both = std::hash<const void*>{}(&stack_char) % 2 ? *left_it : *right_it;
#endif
    ++both;
    ++left_it;
    ++right_it;
  }
  // Copy remaining (from either left or right) to respective output.
  std::copy(left_it, left.end(), left_only);
  std::copy(right_it, right.end(), right_only);
}

template <int... barrier, typename Left, typename Right, typename LeftOnlyOut,
          typename BothOut, typename RightOnlyOut>
void STLSetSymmetricDifference(const Left& left, const Right& right,
                               LeftOnlyOut left_only, BothOut both,
                               RightOnlyOut right_only) {
  return STLSetSymmetricDifference(left, right, left_only, both, right_only,
                                   std::less<>{});
}

// STLSetIntersection:
//
//     In1 STLSetIntersection(a, b);
//     In1 STLSetIntersection(a, b, compare);
//     void STLSetIntersection(a, b, &out);
//     void STLSetIntersection(a, b, &out, compare);
//     Out STLSetIntersectionAs<Out>(a, b);
//     Out STLSetIntersectionAs<Out>(a, b, compare);
//
// Appends the elements that are in both "a" and "b" to output container
// "out".  Both input containers must be sorted with operator '<' or with
// "compare" if specified. "out" must be distinct from both "a" and "b".
//
// See std::set_intersection() for how set intersection is computed.
template <typename In1, typename In2, typename Out, typename Compare>
void STLSetIntersection(const In1& a, const In2& b, Out* out, Compare compare) {
  static_assert(!gtl::stl_util_internal::Unordered<In1>::value,
                "In1 must be an ordered set");
  static_assert(!gtl::stl_util_internal::Unordered<In2>::value,
                "In2 must be an ordered set");
  assert(std::is_sorted(a.begin(), a.end(), compare));
  assert(std::is_sorted(b.begin(), b.end(), compare));
  assert(static_cast<const void*>(&a) != static_cast<const void*>(out));
  assert(static_cast<const void*>(&b) != static_cast<const void*>(out));
  std::set_intersection(a.begin(), a.end(), b.begin(), b.end(),
                        std::inserter(*out, out->end()), compare);
}
// Note: The 'enable_if' keeps this overload from participating in
// overload resolution if 'out' is a function pointer, gracefully forcing
// the 3-argument overload that treats the third argument as a comparator.
template <typename In1, typename In2, typename Out>
std::enable_if_t<!std::is_function_v<Out>, void> STLSetIntersection(
    const In1& a, const In2& b, Out* out) {
  return STLSetIntersection(a, b, out, std::less<>{});
}
template <typename Out, typename In1, typename In2, typename Compare>
Out STLSetIntersectionAs(const In1& a, const In2& b, Compare compare) {
  Out out;
  STLSetIntersection(a, b, &out, compare);
  return out;
}
template <typename Out, typename In1, typename In2>
Out STLSetIntersectionAs(const In1& a, const In2& b) {
  return STLSetIntersectionAs<Out>(a, b, std::less<>{});
}
template <typename In1, typename In2, typename Compare>
In1 STLSetIntersection(const In1& a, const In2& b, Compare compare) {
  return STLSetIntersectionAs<In1>(a, b, compare);
}
template <typename In1, typename In2>
In1 STLSetIntersection(const In1& a, const In2& b) {
  return STLSetIntersection(a, b, std::less<>{});
}
template <typename In1>
In1 STLSetIntersection(const In1& a, const In1& b) {
  return STLSetIntersection(a, b, std::less<>{});
}

// Returns true iff every element in "b" is also in "a". Both containers
// must be sorted by the specified comparator, or by '<' if none is given.
template <typename In1, typename In2, typename Compare>
bool STLIncludes(const In1& a, const In2& b, Compare compare) {
  static_assert(!gtl::stl_util_internal::Unordered<In1>::value,
                "In1 must be an ordered set");
  static_assert(!gtl::stl_util_internal::Unordered<In2>::value,
                "In2 must be an ordered set");
  assert(std::is_sorted(a.begin(), a.end(), compare));
  assert(std::is_sorted(b.begin(), b.end(), compare));
  return std::includes(a.begin(), a.end(), b.begin(), b.end(), compare);
}
template <typename In1, typename In2>
bool STLIncludes(const In1& a, const In2& b) {
  return STLIncludes(a, b, std::less<>{});
}

// SortedRangesHaveIntersection:
//
//     bool SortedRangesHaveIntersection(begin1, end1, begin2, end2);
//     bool SortedRangesHaveIntersection(begin1, end1, begin2, end2,
//                                       comparator);
//
// Returns true iff any element in the sorted range [begin1, end1) is
// equivalent to any element in the sorted range [begin2, end2). The iterators
// themselves do not have to be the same type, but the value types must be
// sorted either by the specified comparator, or by '<' if no comparator is
// given.
// [Two elements a,b are considered equivalent if !(a < b) && !(b < a) ].
template <typename InputIterator1, typename InputIterator2, typename Comp>
bool SortedRangesHaveIntersection(InputIterator1 begin1, InputIterator1 end1,
                                  InputIterator2 begin2, InputIterator2 end2,
                                  Comp comparator) {
  assert(std::is_sorted(begin1, end1, comparator));
  assert(std::is_sorted(begin2, end2, comparator));
  while (begin1 != end1 && begin2 != end2) {
    if (comparator(*begin1, *begin2)) {
      ++begin1;
      continue;
    }
    if (comparator(*begin2, *begin1)) {
      ++begin2;
      continue;
    }
    return true;
  }
  return false;
}
template <typename InputIterator1, typename InputIterator2>
bool SortedRangesHaveIntersection(InputIterator1 begin1, InputIterator1 end1,
                                  InputIterator2 begin2, InputIterator2 end2) {
  return SortedRangesHaveIntersection(begin1, end1, begin2, end2,
                                      std::less<>{});
}

// Returns true iff the ordered containers 'in1' and 'in2' have a non-empty
// intersection. The container elements do not have to be the same type, but the
// elements must be sorted either by the specified comparator, or by '<' if no
// comparator is given.
template <typename In1, typename In2, typename Comp>
bool SortedContainersHaveIntersection(const In1& in1, const In2& in2,
                                      Comp comparator) {
  return SortedRangesHaveIntersection(in1.begin(), in1.end(), in2.begin(),
                                      in2.end(), comparator);
}
template <typename In1, typename In2>
bool SortedContainersHaveIntersection(const In1& in1, const In2& in2) {
  return SortedContainersHaveIntersection(in1, in2, std::less<>{});
}

// An std::allocator<T> subclass that keeps count of the active bytes allocated
// by this class of allocators.
//
// With the default template parameterization, this allocator is thread
// compatible (<link>). It should only be used in situations
// where you can ensure that only a single thread performs allocation and
// deallocation.
//
// If allocations or deallocations may happen concurrently, use
// `STLAtomicCountingAllocator`.
//
// Example:
//   using MyAlloc = STLCountingAllocator<string>;
//   int64 bytes = 0;
//   std::vector<string, MyAlloc> v(MyAlloc(&bytes));
//   v.push_back("hi");
//   LOG(INFO) << "Bytes allocated " << bytes;
//
template <typename T, typename Alloc = std::allocator<T>,
          typename CounterType = int64_t>
class STLCountingAllocator : public Alloc {
 public:
  using Base = Alloc;
  using pointer = typename std::allocator_traits<Alloc>::pointer;
  using size_type = typename std::allocator_traits<Alloc>::size_type;
  using is_always_equal = std::false_type;

  STLCountingAllocator() : bytes_used_(nullptr) {}
  explicit STLCountingAllocator(CounterType* b) : bytes_used_(b) {}

  // Constructor used for rebinding
  template <typename U, typename B, typename C>
  STLCountingAllocator(const STLCountingAllocator<U, B, C>& x)
      : Alloc(x), bytes_used_(x.bytes_used()) {}

  pointer allocate(
      size_type n,
      typename std::allocator_traits<Alloc>::const_pointer hint = nullptr) {
    assert(bytes_used_ != nullptr);
    AdjustCount(n * sizeof(T));
    return std::allocator_traits<Alloc>::allocate(*this, n, hint);
  }

  void deallocate(pointer p, size_type n) {
    Alloc::deallocate(p, n);
    assert(bytes_used_ != nullptr);
    AdjustCount(-static_cast<int64_t>(n) * sizeof(T));
  }

  // Rebind allows an std::allocator<T> to be used for a different type
  template <typename U>
  class rebind {
    using OtherA =
        typename std::allocator_traits<Alloc>::template rebind_alloc<U>;

   public:
    using other = STLCountingAllocator<U, OtherA, CounterType>;
  };

  CounterType* bytes_used() const { return bytes_used_; }

 private:
  void AdjustCount(int64_t delta) {
    if constexpr (gtl::Requires<CounterType>(
                      [](auto&& c) -> decltype(c.fetch_add(
                                       0, std::memory_order_relaxed)) {})) {
      bytes_used_->fetch_add(delta, std::memory_order_relaxed);
    } else {
      static_assert(std::is_integral_v<CounterType>);
      *bytes_used_ += delta;
    }
  }

  CounterType* bytes_used_;
};

// Variant of `STLCountingAllocator` that uses an atomic counter to track the
// allocated bytes. All memory accesses to this counter will use
// `std::memory_order_relaxed`.
template <typename T, typename Alloc = std::allocator<T>>
using STLAtomicCountingAllocator =
    STLCountingAllocator<T, Alloc, std::atomic<int64_t>>;

template <typename A>
class STLCountingAllocator<void, A> : public A {
 public:
  using is_always_equal = std::false_type;

  STLCountingAllocator() : bytes_used_(nullptr) {}
  explicit STLCountingAllocator(int64_t* b) : bytes_used_(b) {}

  // Constructor used for rebinding
  template <typename U, typename B>
  STLCountingAllocator(const STLCountingAllocator<U, B>& x)
      : A(x), bytes_used_(x.bytes_used()) {}

  template <typename U>
  class rebind {
    using OtherA = typename std::allocator_traits<A>::template rebind_alloc<U>;

   public:
    using other = STLCountingAllocator<U, OtherA>;
  };
  int64_t* bytes_used() const { return bytes_used_; }

 private:
  int64_t* bytes_used_;
};

template <typename T, typename A>
bool operator==(const STLCountingAllocator<T, A>& a,
                const STLCountingAllocator<T, A>& b) {
  using Base = typename STLCountingAllocator<T, A>::Base;
  return static_cast<const Base&>(a) == static_cast<const Base&>(b) &&
         a.bytes_used() == b.bytes_used();
}

template <typename T, typename A>
bool operator!=(const STLCountingAllocator<T, A>& a,
                const STLCountingAllocator<T, A>& b) {
  return !(a == b);
}

// An output iterator that ignores all results passed to it. Useful for
// algorithms with multiple outputs where you only want to keep one.
class NoOpOutputIterator {
  // Unused is implicitly constructible from any value.
  struct Unused {
    template <class T>
    ABSL_ATTRIBUTE_ALWAYS_INLINE Unused& operator=(T&&) {
      return *this;
    }
  };

 public:
  using iterator_category = std::output_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = Unused;
  using pointer = void;
  using reference = void;

  Unused operator*() { return {}; }
  NoOpOutputIterator& operator++() { return *this; }
  NoOpOutputIterator& operator++(int /* unused */) { return *this; }
};

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_STL_UTIL_H_
