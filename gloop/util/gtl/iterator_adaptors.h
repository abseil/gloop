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

// Provides some iterator adaptors and views.
//

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_ITERATOR_ADAPTORS_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_ITERATOR_ADAPTORS_H_

#include <cstddef>
#include <functional>
#include <iterator>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/base/throw_delegate.h"
#include "absl/meta/type_traits.h"
#include "gloop/util/gtl/compressed_tuple.h"
#include "gloop/util/gtl/requires.h"

namespace gtl {
namespace internal {

// Helper for extractors that want to conditionally preserve constness.
template <bool PropagateConst = true>
struct Derefencer {
  template <typename T>
  static constexpr auto& deref(T&& x) {
    if constexpr (PropagateConst &&
                  std::is_const_v<std::remove_reference_t<T>>) {
      const auto& tmp = *std::forward<T>(x);
      return tmp;
    } else {
      return *std::forward<T>(x);
    }
  }
};

// A helper class to support operator->() for iterators that return
// non-reference type (value/proxy) elements. It holds the value
// by value and returns a pointer to its internal storage, ensuring
// client calls like iter->method() do not trigger address-of-temporary
// warnings or lead to dangling reference crashes.
template <typename T>
class ArrowProxy {
 public:
  constexpr explicit ArrowProxy(T val) : val_(std::move(val)) {}

  // &&-qualified, since this class should only ever exist as rvalue.
  constexpr const T* operator->() const&& ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return &val_;
  }
  constexpr T* operator->() && ABSL_ATTRIBUTE_LIFETIME_BOUND { return &val_; }

 private:
  ArrowProxy(const ArrowProxy&) = delete;
  ArrowProxy(ArrowProxy&&) = delete;
  ArrowProxy& operator=(ArrowProxy&&) = delete;
  ArrowProxy& operator=(const ArrowProxy&) = delete;

  T val_;
};

// CRTP base class for generating iterator adaptors.
// `Sub` is the derived type, `Iterator` is the underlying iterator type and
// `Extractor` is an invokable that takes dereferenced `Iterator` and returns a
// reference of a new type.
// Note: CRTP is needed to provide CTAD to iterator_first and similar. Once type
// aliases support deduction guides, most of the boilerplate can be removed.
template <typename Sub, typename Iterator, typename Extractor>
class ABSL_ATTRIBUTE_VIEW ExtractingIteratorBase {
 private:
  using iterator_traits = std::iterator_traits<Iterator>;
  using inner_iterator_category = typename iterator_traits::iterator_category;
  using extracted_reference =
      std::invoke_result_t<const Extractor&,
                           typename iterator_traits::reference>;
  using cv_value_type = std::remove_reference_t<extracted_reference>;

 public:
  using reference = extracted_reference;
  using value_type = std::remove_cv_t<cv_value_type>;
  using pointer = std::conditional_t<std::is_lvalue_reference_v<reference>,
                                     std::add_pointer_t<cv_value_type>,
                                     ArrowProxy<cv_value_type>>;
  using difference_type = typename iterator_traits::difference_type;
  using iterator_category =
      std::conditional_t<!std::is_lvalue_reference_v<reference>,
                         std::input_iterator_tag, inner_iterator_category>;

  constexpr explicit ExtractingIteratorBase(Extractor e)
      : ExtractingIteratorBase({}, std::move(e)) {}
  constexpr explicit ExtractingIteratorBase(Iterator it = {}, Extractor e = {})
      : iterator_and_extractor_(std::move(it), std::move(e)) {}
  template <typename Sub2, typename It2,
            typename = std::enable_if_t<std::is_convertible_v<It2, Iterator>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr ExtractingIteratorBase(
      const ExtractingIteratorBase<Sub2, It2, Extractor>& o)
      : ExtractingIteratorBase(o.base(), o.extractor()) {}

  constexpr Iterator& base() & { return iterator(); }
  constexpr const Iterator& base() const& { return iterator(); }
  constexpr Iterator&& base() && { return std::move(iterator()); }
  constexpr const Iterator&& base() const&& { return std::move(iterator()); }

  constexpr const Extractor& extractor() const {
    return iterator_and_extractor_.template get<1>();
  }

  constexpr reference get() const { return std::invoke(extractor(), *base()); }
  constexpr reference operator*() const { return get(); }
  constexpr pointer operator->() const {
    if constexpr (std::is_lvalue_reference_v<reference>) {
      return &get();
    } else {
      return pointer(get());
    }
  }
  constexpr reference operator[](difference_type d) const {
    return *(sub() + d);
  }

  constexpr Sub& operator++() {
    ++base();
    return sub();
  }
  constexpr Sub& operator--() {
    --base();
    return sub();
  }
  constexpr Sub operator++(int /*unused*/) {
    return ExtractingIteratorBase(base()++, extractor()).sub();
  }
  constexpr Sub operator--(int /*unused*/) {
    return ExtractingIteratorBase(base()--, extractor()).sub();
  }

  constexpr Sub& operator+=(difference_type d) {
    base() += d;
    return sub();
  }
  constexpr Sub& operator-=(difference_type d) {
    base() -= d;
    return sub();
  }

  friend constexpr bool operator==(const Sub& a, const Sub& b) {
    return a.base() == b.base();
  }
  friend constexpr bool operator!=(const Sub& a, const Sub& b) {
    return !(a == b);
  }

  // These shouldn't be necessary, as implicit conversion from 'Iterator'
  // should be enough to make such comparisons work.
  friend constexpr bool operator==(const Sub& a, const Iterator& b) {
    return a.base() == b;
  }
  friend constexpr bool operator==(const Iterator& a, const Sub& b) {
    return a == b.base();
  }
  friend constexpr bool operator!=(const Sub& a, const Iterator& b) {
    return !(a == b);
  }
  friend constexpr bool operator!=(const Iterator& a, const Sub& b) {
    return !(a == b);
  }

  friend constexpr Sub operator+(const Sub& it, difference_type d) {
    return ExtractingIteratorBase(it.base() + d, it.extractor()).sub();
  }
  friend constexpr Sub operator+(difference_type d, const Sub& it) {
    return it + d;
  }
  friend constexpr Sub operator-(const Sub& it, difference_type d) {
    return ExtractingIteratorBase(it.base() - d, it.extractor()).sub();
  }
  friend constexpr difference_type operator-(const Sub& a, const Sub& b) {
    return a.base() - b.base();
  }

  friend constexpr bool operator<(const Sub& a, const Sub& b) {
    return a.base() < b.base();
  }
  friend constexpr bool operator>(const Sub& a, const Sub& b) {
    return a.base() > b.base();
  }
  friend constexpr bool operator<=(const Sub& a, const Sub& b) {
    return a.base() <= b.base();
  }
  friend constexpr bool operator>=(const Sub& a, const Sub& b) {
    return a.base() >= b.base();
  }

 private:
  constexpr Sub& sub() { return static_cast<Sub&>(*this); }
  constexpr const Sub& sub() const { return static_cast<const Sub&>(*this); }

  constexpr Iterator& iterator() {
    return iterator_and_extractor_.template get<0>();
  }
  constexpr const Iterator& iterator() const {
    return iterator_and_extractor_.template get<0>();
  }

  gtl::CompressedTuple<Iterator, Extractor> iterator_and_extractor_;
};

template <typename It, typename Extractor>
struct ExtractingIterator
    : ExtractingIteratorBase<ExtractingIterator<It, Extractor>, It, Extractor> {
  using Base = typename ExtractingIterator::ExtractingIteratorBase;
  using Base::Base;
};

// Determines if the subscript operator is supported.
template <typename Iterator>
struct HasSubscript {
 private:
  using IsRandomAccess = std::is_base_of<
      std::random_access_iterator_tag,
      typename std::iterator_traits<Iterator>::iterator_category>;

  // If this is an extracting iterator, check the inner iterator.
  template <typename Sub, typename InnerIterator, typename Extractor>
  static HasSubscript<InnerIterator> CheckHasSubscript(
      const volatile ExtractingIteratorBase<Sub, InnerIterator, Extractor>*);

  // Otherwise, check if the iterator is random access.
  static IsRandomAccess CheckHasSubscript(const volatile void*);

 public:
  static constexpr bool value =
      decltype(CheckHasSubscript(std::declval<Iterator*>()))::value;
};

// This is a helper type that defines common constructors needed for
// iterator_first and others.
template <template <typename> typename Sub, typename It, typename Extractor>
struct CommonCtors : internal::ExtractingIteratorBase<Sub<It>, It, Extractor> {
  using Base = typename CommonCtors::ExtractingIteratorBase;
  CommonCtors() = default;
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr CommonCtors(It it) : Base(std::move(it)) {}
  template <typename It2,
            typename = std::enable_if_t<std::is_convertible_v<It2, It>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr CommonCtors(Sub<It2> o) : Base(std::move(o).base()) {}
  template <typename It2,
            typename = std::enable_if_t<std::is_convertible_v<It2, It>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr CommonCtors(ExtractingIterator<It2, Extractor> o)
      : Base(std::move(o).base()) {}
};

struct FirstExtractor {
  template <typename T>
  constexpr auto& operator()(T&& x) const {
    return std::forward<T>(x).first;
  }
};
struct SecondExtractor {
  template <typename T>
  constexpr auto& operator()(T&& x) const {
    return std::forward<T>(x).second;
  }
};
template <bool PropagateConst>
struct DereferencingExtractor {
  template <typename T>
  constexpr auto& operator()(T&& x) const {
    return internal::Derefencer<PropagateConst>::deref(std::forward<T>(x));
  }
};
struct DereferencingSecondExtractor {
  template <typename T>
  constexpr auto& operator()(T&& x) const {
    return internal::Derefencer<>::deref(std::forward<T>(x).second);
  }
};
struct NoopExtractor {
  template <typename T>
  constexpr auto& operator()(T&& x) const {
    return std::forward<T>(x);
  }
};

}  // namespace internal

// In both iterator adaptors, iterator_first<> and iterator_second<>,
// we build a new iterator based on a parameterized iterator type, "It".
// The value type, "Val" is determined by "It::value_type::first" or
// "It::value_type::second", respectively.

// iterator_first<> adapts an iterator to return the first value of a pair.
// It is equivalent to calling it->first on every value.
// Example:
//
// hash_map<string, int> values;
// values["foo"] = 1;
// values["bar"] = 2;
// for (iterator_first<hash_map<string, int>::iterator> x = values.begin();
//      x != values.end(); ++x) {
//   printf("%s", x->c_str());
// }
template <typename It>
struct ABSL_ATTRIBUTE_VIEW iterator_first
    : internal::CommonCtors<iterator_first, It, internal::FirstExtractor> {
  using iterator_first::CommonCtors::CommonCtors;
};

template <class It>
iterator_first(It) -> iterator_first<It>;

template <typename It>
constexpr iterator_first<It> make_iterator_first(It it) {
  return iterator_first<It>(std::move(it));
}

// iterator_second<> adapts an iterator to return the second value of a pair.
// It is equivalent to calling it->second on every value.
// Example:
//
// hash_map<string, int> values;
// values["foo"] = 1;
// values["bar"] = 2;
// for (iterator_second<hash_map<string, int>::iterator> x = values.begin();
//      x != values.end(); ++x) {
//   int v = *x;
//   printf("%d", v);
// }
template <typename It>
struct ABSL_ATTRIBUTE_VIEW iterator_second
    : internal::CommonCtors<iterator_second, It, internal::SecondExtractor> {
  using iterator_second::CommonCtors::CommonCtors;
};

template <class It>
iterator_second(It) -> iterator_second<It>;

template <typename It>
constexpr iterator_second<It> make_iterator_second(It it) {
  return iterator_second<It>(std::move(it));
}

// iterator_second_ptr<> adapts an iterator to return the dereferenced second
// value of a pair.
// It is equivalent to calling *it->second on every value.
// The same result can be achieved by composition
// iterator_ptr<iterator_second<>>
// Can be used with maps where values are regular pointers or smart pointers.
// This iterator adaptor can be used by classes to give their
// clients access to some of their internal data without exposing too much of
// it.
//
// Example:
// class MyClass {
//  public:
//   explicit MyClass(absl::string_view s);
//   std::string DebugString() const;
// };
// using MyMap = absl::flat_hash_map<std::string, std::unique_ptr<MyClass>>;
// using MyMapValuesIterator = iterator_second_ptr<MyMap::iterator>;
// MyMap values;
// values["foo"] = std::make_unique<MyClass>("foo");
// values["bar"] = std::make_unique<MyClass("bar");
// for (MyMapValuesIterator it = values.begin(); it != values.end(); ++it) {
//   absl::PrintF("%s", it->DebugString());
// }
template <typename It>
struct ABSL_ATTRIBUTE_VIEW iterator_second_ptr
    : internal::CommonCtors<iterator_second_ptr, It,
                            internal::DereferencingSecondExtractor> {
  using iterator_second_ptr::CommonCtors::CommonCtors;
};

template <class It>
iterator_second_ptr(It) -> iterator_second_ptr<It>;

template <typename It>
constexpr iterator_second_ptr<It> make_iterator_second_ptr(It it) {
  return iterator_second_ptr<It>(std::move(it));
}

// iterator_ptr<>/mutable_iterator_ptr<> adapt an iterator to return the
// dereferenced value. With these adaptors you can write *it instead of **it, or
// it->something instead of (*it)->something. Can be used with vectors and lists
// where values are raw pointers or smart pointers. This iterator adaptor can be
// used by classes to give their clients access to some of their internal data
// without exposing too much of it.
//
// The difference between mutable_iterator_ptr and iterator_ptr is that
// iterator_ptr will propagate const to the pointed-to objects, and the mutable
// version won't do so.
//
// Example:
// class MyClass {
//  public:
//   explicit MyClass(absl::string_view s);
//   std::string DebugString() const;
//   Increment();  // non-const
// };
// using MyVector = std::vector<std::unique_ptr<MyClass>>;
// using DereferencingIterator = gtl::iterator_ptr<MyVector::const_iterator>;
// using MutableDerefIterator =
//     gtl::mutable_iterator_ptr<MyVector::const_iterator>;
// const MyVector& values = ...;
// for (DereferencingIterator it = values.begin(); it != values.end(); ++it) {
//   LogDebug(it->DebugString());
// }
// for (MutableDerefIterator it = values.begin(); it != values.end(); ++it) {
//   it->Increment();
// }
//
// Without iterator_ptr you would have to do (*it)->DebugString().
template <typename It>
struct ABSL_ATTRIBUTE_VIEW mutable_iterator_ptr
    : internal::CommonCtors<
          mutable_iterator_ptr, It,
          internal::DereferencingExtractor</*PropagateConst=*/false>> {
  using mutable_iterator_ptr::CommonCtors::CommonCtors;
};

template <class It>
mutable_iterator_ptr(It) -> mutable_iterator_ptr<It>;

template <typename It>
constexpr mutable_iterator_ptr<It> make_mutable_iterator_ptr(It it) {
  return mutable_iterator_ptr<It>(std::move(it));
}

template <typename It>
struct ABSL_ATTRIBUTE_VIEW iterator_ptr
    : internal::CommonCtors<
          iterator_ptr, It,
          internal::DereferencingExtractor</*PropagateConst=*/true>> {
  using iterator_ptr::CommonCtors::CommonCtors;

  template <typename It2,
            typename = std::enable_if_t<std::is_convertible_v<It2, It>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr iterator_ptr(mutable_iterator_ptr<It2> o)
      : iterator_ptr(std::move(o).base()) {}
};

template <class It>
iterator_ptr(It) -> iterator_ptr<It>;

template <typename It>
constexpr iterator_ptr<It> make_iterator_ptr(It it) {
  return iterator_ptr<It>(std::move(it));
}

namespace internal {

// Template that provides C::size_type (if exists) or falls back to size_t.
template <typename U, typename = void>
struct container_size {
  using type = size_t;
};
template <typename U>
struct container_size<U, std::void_t<typename U::size_type>> {
  using type = typename U::size_type;
};

struct IterGenerator {
  template <typename C>
  static constexpr auto begin(C& c) {
    return c.begin();
  }
  template <typename C>
  static constexpr auto end(C& c) {
    return c.end();
  }
};

struct ReverseIterGenerator {
  struct RequiresBeginLambda {
    template <typename T>
    constexpr auto operator()(T&& x) -> decltype(x.rbegin()) {}
  };
  struct RequiresEndLambda {
    template <typename T>
    constexpr auto operator()(T&& x) -> decltype(x.rend()) {}
  };
  template <typename C>
  static constexpr auto begin(C& c) {
    // A workaround for x86_64-elf-gcc (it does not allow actual lambda here).
    if constexpr (gtl::Requires<C&>(RequiresBeginLambda{})) {
      return c.rbegin();
    } else {
      return std::make_reverse_iterator(c.end());
    }
  }
  template <typename C>
  static constexpr auto end(C& c) {
    // A workaround for x86_64-elf-gcc (it does not allow actual lambda here).
    if constexpr (gtl::Requires<C&>(RequiresEndLambda{})) {
      return c.rend();
    } else {
      return std::make_reverse_iterator(c.begin());
    }
  }
};

// C:              the container type. When const qualified, view has only
//                 const methods.
// IteratorPolicy: a policy type that wraps native iterators from a C
// IterGenerator:  a policy type that returns native iterators from a C
template <typename C, typename IteratorPolicy,
          typename IterGenerator = gtl::internal::IterGenerator>
class ABSL_ATTRIBUTE_VIEW container_view {
  using source_container = std::remove_const_t<C>;
  using container_iterator =
      decltype(IterGenerator::begin(std::declval<source_container&>()));
  using container_const_iterator =
      decltype(IterGenerator::begin(std::declval<const source_container&>()));
  // Recursive traversal down the hierarchy of nested views. Constness exposed
  // by outer views must be determined by the innermost (non-view) container.
  // Nested views match the recursive ConstTrait template specialization (by the
  // presence of a .container() method) and dive deeper into the hierarchy. The
  // first non-view container mismatches and resorts to the default ConstTrait.
  template <typename X, typename = void>  // default for non-view containers
  struct ConstTrait : public std::is_const<X> {};
  template <typename V>  // for nested views with a .container() method
  struct ConstTrait<V, std::void_t<decltype(&V::container)>>
      : public ConstTrait<std::remove_reference_t<
            std::invoke_result_t<decltype(&V::container), const V&>>> {};
  static constexpr bool kMutable = !ConstTrait<C>::value;

 public:
  using container_type = source_container;
  using iterator = decltype(std::declval<IteratorPolicy>().Adapt(
      std::declval<container_iterator>()));
  using const_iterator = decltype(std::declval<IteratorPolicy>().Adapt(
      std::declval<container_const_iterator>()));
  using value_type = typename std::iterator_traits<iterator>::value_type;
  using reference = typename std::iterator_traits<iterator>::reference;
  using const_reference =
      typename std::iterator_traits<const_iterator>::reference;
  using size_type = typename internal::container_size<C>::type;
  using absl_internal_is_view = std::true_type;

  constexpr explicit container_view(C& c ABSL_ATTRIBUTE_LIFETIME_BOUND,
                                    IteratorPolicy policy = {})
      : container_and_policy_(&c, std::move(policy)) {}
  constexpr explicit container_view(C&& c ABSL_ATTRIBUTE_LIFETIME_BOUND,
                                    IteratorPolicy policy = {})
      : container_view(c, std::move(policy)) {}

  // Allow implicit conversion from the corresponding non-const container_view.
  // Erring on the side of constness should be allowed. E.g.:
  //    MyMap m;
  //    key_view_t<MyMap> keys = key_view(m);  // ok
  //    key_view_t<const MyMap> const_keys = key_view(m);  // ok
  template <bool is_mutable = kMutable,
            typename = std::enable_if_t<!is_mutable>>
  constexpr container_view(  // NOLINT(google-explicit-constructor)
      const container_view<source_container, IteratorPolicy, IterGenerator>& v)
      : container_view(v.container(), v.policy()) {}

  // Methods used for both const and mutable containers
  constexpr const_iterator cbegin() const { return begin(); }
  constexpr const_iterator cend() const { return end(); }
  constexpr bool empty() const { return begin() == end(); }
  constexpr size_type size() const { return container().size(); }
  constexpr const IteratorPolicy& policy() const {
    return container_and_policy_.template get<1>();
  }

  // Methods used for const containers only
  template <int&... ExplicitArgumentBarrier, bool is_mutable = kMutable>
  constexpr std::enable_if_t<!is_mutable, const_iterator> begin() const {
    return policy().Adapt(IterGenerator::begin(container()));
  }
  template <int&... ExplicitArgumentBarrier, bool is_mutable = kMutable>
  constexpr std::enable_if_t<!is_mutable, const_iterator> end() const {
    return policy().Adapt(IterGenerator::end(container()));
  }
  template <int&... ExplicitArgumentBarrier, bool is_mutable = kMutable,
            bool has_subscript = HasSubscript<const_iterator>::value>
  constexpr std::enable_if_t<!is_mutable && has_subscript, const_reference>
  operator[](size_type i) const {
    return begin()[i];
  }
  template <int&... ExplicitArgumentBarrier, bool is_mutable = kMutable,
            bool has_subscript = HasSubscript<const_iterator>::value>
  constexpr std::enable_if_t<!is_mutable && has_subscript, const_reference> at(
      size_type i) const {
    if (ABSL_PREDICT_FALSE(i >= size())) {
      absl::ThrowStdOutOfRange(
          "`container_view::at(size_type)` failed bounds check");
    }
    return begin()[i];
  }
  template <int&... ExplicitArgumentBarrier, bool is_mutable = kMutable>
  constexpr std::enable_if_t<!is_mutable, const container_type&> container()
      const {
    return *container_and_policy_.template get<0>();
  }

  // Methods used for mutable containers only
  template <int&... ExplicitArgumentBarrier, bool is_mutable = kMutable>
  constexpr std::enable_if_t<is_mutable, iterator> begin() const {
    return policy().Adapt(IterGenerator::begin(container()));
  }
  template <int&... ExplicitArgumentBarrier, bool is_mutable = kMutable>
  constexpr std::enable_if_t<is_mutable, iterator> end() const {
    return policy().Adapt(IterGenerator::end(container()));
  }
  template <int&... ExplicitArgumentBarrier, bool is_mutable = kMutable,
            bool has_subscript = HasSubscript<iterator>::value>
  constexpr std::enable_if_t<is_mutable && has_subscript, reference> operator[](
      size_type i) const {
    return begin()[i];
  }
  template <int&... ExplicitArgumentBarrier, bool is_mutable = kMutable,
            bool has_subscript = HasSubscript<iterator>::value>
  constexpr std::enable_if_t<is_mutable && has_subscript, reference> at(
      size_type i) const {
    if (ABSL_PREDICT_FALSE(i >= size())) {
      absl::ThrowStdOutOfRange(
          "`container_view::at(size_type)` failed bounds check");
    }
    return begin()[i];
  }
  template <int&... ExplicitArgumentBarrier, bool is_mutable = kMutable>
  constexpr std::enable_if_t<is_mutable, container_type&> container() const {
    return *container_and_policy_.template get<0>();
  }

 private:
  gtl::CompressedTuple<C*, IteratorPolicy> container_and_policy_;
};

// This policy wraps original iterator with `Iterator`.
template <template <typename> typename Iterator>
struct PolicyFor {
  template <typename It>
  constexpr auto Adapt(It&& it) const {
    return Iterator<It>(std::forward<It>(it));
  }
};

// This policy forwards original iterator without any modifications.
struct ForwardPolicy {
  template <typename It>
  constexpr auto Adapt(It&& it) const {
    return std::forward<It>(it);
  }
};

template <typename Extractor>
struct ExtractorPolicy : private gtl::CompressedTuple<Extractor> {
 private:
  using Base = gtl::CompressedTuple<Extractor>;

  constexpr const Extractor& AsExtractor() const {
    return Base::template get<0>();
  }

 public:
  constexpr explicit ExtractorPolicy(Extractor e) : Base(std::move(e)) {}

  template <typename source_iterator>
  constexpr auto Adapt(source_iterator it) const {
    return ExtractingIterator<source_iterator, Extractor>(std::move(it),
                                                          AsExtractor());
  }
};

template <typename C, template <typename> typename Iterator>
using container_view_t = container_view<C, PolicyFor<Iterator>, IterGenerator>;

}  // namespace internal

// Traits to provide a typedef abstraction for the return value
// of the `key_view`, `value_view` and similar functions.
// This abstraction allows callers of these functions to use readable
// type names (as an alternative to using auto), and allows the maintainers of
// iterator_adaptors.h to change the return types if needed without updating
// callers.
template <typename C>
using key_view_t = internal::container_view_t<C, iterator_first>;
template <typename C>
using value_view_t = internal::container_view_t<C, iterator_second>;
template <typename C>
using deref_view_t = internal::container_view_t<C, iterator_ptr>;
template <typename C>
using deref_second_view_t = internal::container_view_t<C, iterator_second_ptr>;
template <typename C>
using mutable_deref_view_t =
    internal::container_view_t<C, mutable_iterator_ptr>;
template <typename C>
using reversed_view_t =
    internal::container_view<C, internal::ForwardPolicy,
                             internal::ReverseIterGenerator>;

template <typename C, typename E>
using projection_view_t =
    internal::container_view<C, internal::ExtractorPolicy<E>>;

// The key_view and value_view functions provide pretty ways to iterate either
// the keys or the values of a map using range based for loops.
//
// Example:
//    hash_map<int, string> my_map;
//    ...
//    for (string val : value_view(my_map)) {
//      ...
//    }
//
// Note: If you pass a temporary container to key_view or value_view, be careful
// that the temporary container outlives the wrapper view to avoid dangling
// references.
// This is fine:  PublishAll(value_view(Make());
// This is not:   for (const auto& v : value_view(Make())) Publish(v);
template <int&... ExplicitArgumentBarrier, typename C>
constexpr auto key_view(C&& map ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return key_view_t<std::remove_reference_t<C>>(std::forward<C>(map));
}
template <int&... ExplicitArgumentBarrier, typename C>
constexpr auto value_view(C&& map ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return value_view_t<std::remove_reference_t<C>>(std::forward<C>(map));
}

// Abstract container view that dereferences the pointer-like .second member
// of a container's std::pair elements, such as the elements of std::map<K,V*>
// or of std::vector<std::pair<K,V*>>.
//
// Example:
//   map<int, string*> elements;
//   for (const string& element : deref_second_view(elements)) {
//     ...
//   }
//
// Note: If you pass a temporary container to deref_second_view, be careful that
// the temporary container outlives the deref_second_view to avoid dangling
// references.
// This is fine:  PublishAll(deref_second_view(Make());
// This is not:   for (const auto& v : deref_second_view(Make())) {
//                  Publish(v);
//                }
template <int&... ExplicitArgumentBarrier, typename C>
constexpr auto deref_second_view(C&& map) {
  return deref_second_view_t<std::remove_reference_t<C>>(std::forward<C>(map));
}

// Abstract container view that dereferences pointer elements.
//
// Example:
//   vector<string*> elements;
//   for (const string& element : deref_view(elements)) {
//     ...
//   }
//
// Note: If you pass a temporary container to deref_view, be careful that the
// temporary container outlives the deref_view to avoid dangling references.
// This is fine:  PublishAll(deref_view(Make());
// This is not:   for (const auto& v : deref_view(Make())) { Publish(v); }
template <int&... ExplicitArgumentBarrier, typename C>
constexpr auto deref_view(C&& c) {
  return deref_view_t<std::remove_reference_t<C>>(std::forward<C>(c));
}

// mutable_deref_view is like deref_view, except that it allows for mutable
// access to pointed to objects from const iterators.
//
// Example:
//   const std::vector<std::string*> elements = ...;
//   for (std::string& element : mutable_deref_view(elements)) {
//     ...
//   }
template <int&... ExplicitArgumentBarrier, typename C>
constexpr auto mutable_deref_view(C& c) {
  return mutable_deref_view_t<std::remove_reference_t<C>>(c);
}

// Abstract container view that iterates backwards.
//
// Example:
//   vector<string> elements;
//   for (const string& element : reversed_view(elements)) {
//     ...
//   }
//
// Note: If you pass a temporary container to reversed_view, be careful
// that the temporary container outlives the reversed_view to avoid dangling
// references.
// This is fine:  PublishAll(reversed_view(Make());
// This is not:   for (const auto& v : reversed_view(Make())) { Publish(v); }
template <int&... ExplicitArgumentBarrier, typename C>
constexpr auto reversed_view(C&& c ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return reversed_view_t<std::remove_reference_t<C>>(std::forward<C>(c));
}

// Returns a view to the given container `C` (preserving order of the elements)
// that applies given `Extractor` to each element.
//
// The first argument `C` is the source container. The resulting view keeps a
// pointer to it, hence all the usual dangers of dangling pointers apply here
// too.
// The second argument `Extractor` is any Callable type that takes
// `C::reference` and returns "extracted" type: this could be a lambda, a
// function pointer, a pointer to member (see example below) and more (see
// https://en.cppreference.com/w/cpp/named_req/Callable). Please keep in mind
// https://abseil.io/tips/133 when choosing an extractor for a type that you do
// not own.
//
// Note that projection_view preserves iterator category of the source container
// (i.e. if source container has random access iterators, then the view will
// have them too) only if `Extractor` returns a reference. Otherwise, the view
// will have the input iterator category regardless of the source container.
//
// Example:
//
//   struct Googler {
//     int64_t id;
//     std::string username;
//   };
//   std::vector<Googler> googlers = {{.id = 1, .username = "sergey"},
//                                    {.id = 2, .username = "page"}};
//   for (const std::string& username :
//        gtl::projection_view(googlers, &Googler::username)) {
//     absl::PrintF("username %s = ", username);
//   }
//
// Please see tests for more usage examples.
template <int&... ExplicitArgumentBarrier, typename C, typename Extractor>
constexpr auto projection_view(C&& c ABSL_ATTRIBUTE_LIFETIME_BOUND,
                               Extractor&& e) {
  using E = std::decay_t<Extractor>;
  return projection_view_t<std::remove_reference_t<C>, E>(
      std::forward<C>(c),
      internal::ExtractorPolicy<E>(std::forward<Extractor>(e)));
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_ITERATOR_ADAPTORS_H_
