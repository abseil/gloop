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

//
// This library provides utilities for manually ordering function overloads and
// class specializations.
//
// rank<N> can be converted to rank<M> for any M >= N. The smaller the value of
// M, the better is the conversion in terms of overload resolution. This
// property is handy for resolving ambiguity among overloads.
//
// Example: A generic function for clearing objects.
//
// Let's see how we can implement function GenericClear(p) that works as
// follows:
//
//   1. If p->Clear() is a valid expression, GenericClear(p) calls that.
//   2. Else if p->clear() is a valid expression, calls that.
//   3. Else calls *p = T();
//
// This algorithm is a bit silly but it'll do as an example. To check whether
// p->Clear() is a valid expression we can add an extra template parameter with
// a special default value.
//
//   template <class T, class E = decltype(declval<T*>()->Clear())>
//   void GenericClear(T* p) {
//     p->Clear();
//   }
//
// We can do the same for lower case clear().
//
//   template <class T, class E = decltype(declval<T*>()->clear())>
//   void GenericClear(T* p) {
//     p->clear();
//   }
//
// These two overloads work fine as long as the template argument has exactly
// one of Clear() or clear(). We get a compile error if both methods are found,
// though. The solution is to employ rank<> to explicitly specify the ordering
// of these overloads.
//
// Here's the full implementation:
//
//   template <class T, class E = decltype(declval<T*>()->Clear())>
//   void GenericClear(T* p, rank<0> rank) {
//     p->Clear();
//   }
//
//   template <class T, class E = decltype(declval<T*>()->clear())>
//   void GenericClear(T* p, rank<1> rank) {
//     p->clear();
//   }
//
//   template <class T>
//   void GenericClear(T* p, rank<2> rank) {
//     *p = T();
//   }
//
// When calling GenericClear(), we need to pass the special rank_selector object
// as the second argument.
//
//   MyType obj;
//   GenericClear(&obj, rank_selector);
//
// Voilà! Totally useless but pretty cool generic function.
//
// In addition to function overloads, rank<> can be used for ordering class
// specializations. This is made possible by the fact that rank<N, T> is a
// specialization of rank<M, T> for any M >= N. Versatile!
//
// Example: Iterator of a container-like class.
//
// Let's write a metafunction that given a container-like type T will tell us
// its iterator type.
//
//   1. If T::const_iterator is a type, Iterator<T>::type is a synonym of that.
//   2. Else if T::iterator is a type, it's is a synonym of that.
//   3. Else it's undefined.
//
// Our primary template needs two extra template arguments: one for the enabler
// (this is where we can demand the existence of T::const_iterator) and another
// one for ordering with rank<>.
//
//   template <class T, class E = void, class R = rank_selector_t>
//   struct Iterator;
//
// Note the default values -- they are important. The two specializations are
// straightforward:
//
//   template <class T, class R>
//   struct Iterator<T, std::void_t<typename T::const_iterator>, rank<0, R>> {
//     using type = typename T::const_iterator;
//   };
//
//   template <class T, class R>
//   struct Iterator<T, std::void_t<typename T::iterator>, rank<1, R>> {
//     using type = typename T::iterator;
//   };
//
// Finally, we can use Iterator<> just like any other metafunction.
//
//   typedef Iterator<MyType>::type Iter;
//
// When using rank<>, make sure you aren't inadvertently creating a conflicting
// ordering via arguments other than rank<>. For example, if one function
// overload accepts const T& while another accepts const vector<T>&, there is an
// implicit ordering between the two. It's best to stick to arguments that are
// literally the same in all overloads and specializations and rely on enablers
// to express type constraints. We did this in the examples above: all overloads
// of GenericClear() work on T*, and all Iterator<> specializations are
// specialized on T.

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_RANK_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_RANK_H_

#include <stddef.h>

namespace util {
namespace tuple {

namespace internal_rank {

// Base is either void or wrapper<T>.
template <class Base>
struct wrapper : Base {};

template <>
struct wrapper<void> {};

// wrap<0>::type<T> is wrapper<T>, wrap<1>::type<T> is wrapper<wrapper<T>>,
// and so on.
template <size_t N>
struct wrap {
  template <class Base>
  using type = wrapper<typename wrap<N - 1>::template type<Base>>;
};

template <>
struct wrap<0> {
  template <class Base>
  using type = wrapper<Base>;
};

template <>
struct wrap<1> {
  template <class Base>
  using type = wrapper<wrapper<Base>>;
};

template <>
struct wrap<2> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<Base>>>;
};

template <>
struct wrap<3> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<Base>>>>;
};

template <>
struct wrap<4> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<wrapper<Base>>>>>;
};

template <>
struct wrap<5> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<Base>>>>>>;
};

template <>
struct wrap<6> {
  template <class Base>
  using type =
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<Base>>>>>>>;
};

template <>
struct wrap<7> {
  template <class Base>
  using type = wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>;
};

template <>
struct wrap<8> {
  template <class Base>
  using type = wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>;
};

template <>
struct wrap<9> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>>;
};

template <>
struct wrap<10> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>>>;
};

template <>
struct wrap<11> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>>>>;
};

template <>
struct wrap<12> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>;
};

template <>
struct wrap<13> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>>;
};

template <>
struct wrap<14> {
  template <class Base>
  using type =
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>>>;
};

template <>
struct wrap<15> {
  template <class Base>
  using type = wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<16> {
  template <class Base>
  using type = wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<17> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<18> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<19> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<20> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<21> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<22> {
  template <class Base>
  using type = wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
              wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<23> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
              wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<24> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
              wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<25> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
              wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<26> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
              wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<27> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
              wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<28> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
              wrapper<wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<29> {
  template <class Base>
  using type = wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
              wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
                  wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<30> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
              wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
                  wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<31> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
              wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
                  wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<32> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
              wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
                  wrapper<wrapper<wrapper<Base>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<33> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
              wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
                  wrapper<wrapper<Base>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<34> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
              wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
                  wrapper<wrapper<Base>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<35> {
  template <class Base>
  using type = wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
              wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
                  wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
                      wrapper<wrapper<Base>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<36> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
              wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
                  wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
                      wrapper<wrapper<Base>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<37> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
              wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
                  wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
                      wrapper<Base>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<38> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
              wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
                  wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
                      wrapper<Base>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<39> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
              wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
                  wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
                      wrapper<Base>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>;
};

template <>
struct wrap<40> {
  template <class Base>
  using type = wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
      wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
          wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
              wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
                  wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<wrapper<
                      wrapper<Base>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>;
};

template <size_t A, size_t B>
constexpr size_t minus() {
  // If this assertion triggers because you legitimately need to instantiate
  // rank<N> with N > kMaxRank, talk to the owners of this library or ask them
  // to review a change that increases kMaxRank.
  static_assert(A >= B, "N in rank<N, T> must not exceed kMaxRank");
  return A - B;
}

}  // namespace internal_rank

inline constexpr size_t kMaxRank = 40;

// N must be <= kMaxRank.
template <size_t N, class Base = void>
using rank = typename internal_rank::wrap<
    internal_rank::minus<kMaxRank, N>()>::template type<Base>;

using rank_selector_t = rank<0>;

extern const rank_selector_t rank_selector;

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_RANK_H_
