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
// An internal helper for implementing foo() on top of foo_index().
// For example, transform(f, t) is equivalent to
// transform_index(ignore_index(f), t).

// IWYU pragma: private, include "util/tuple/ignore_index.h"

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_IGNORE_INDEX_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_IGNORE_INDEX_H_

#include <stddef.h>

#include <utility>

namespace util {
namespace tuple {

namespace internal_ignore_index {

// All symbols defined within namespace internal_ignore_index are internal
// to ignore_index.h. Do not reference them from outside or your code can break
// without notice.

template <::size_t N, class F>
const F& mk_dependent();

}  // namespace internal_ignore_index

template <class F>
struct ignore_index_no_args_t {
  ignore_index_no_args_t() : f_(nullptr) {}
  constexpr explicit ignore_index_no_args_t(const F* f) : f_(*f) {}

  // We can't just put these two methods in ignore_index_t because GCC
  // has a tendency to instantiate functions as soon as all template parameters
  // are known even if the number of arguments doesn't match.
  // See <internal thread>.
  //
  // This causes problems if instantiating the result type causes hard failure
  // and that's exactly what happens when users pass std::reference_wrapper to
  // algorithms in util::tuple.
  //
  // For example, the following code would fail to compile with GCC if
  // ignore_index_no_args_t and ignore_index_t were merged into a single class.
  //
  //   struct F {
  //     template <class T> void operator()(const T& elem) {}
  //   };
  //
  //   F f;
  //   util::tuple::for_each(std::ref(f), std::make_tuple(0));
  template <::size_t N>
  constexpr decltype(internal_ignore_index::mk_dependent<N, F>()()) operator()()
      const {
    return f_();
  }

  template <::size_t N, class T>
  constexpr decltype(internal_ignore_index::mk_dependent<N, F>()
                         .template operator()<T>()) operator()() const {
    return f_.template operator()<T>();
  }

 private:
  F& f_;
};

template <class F>
struct ignore_index_t {
  ignore_index_t() : f_(nullptr) {}
  constexpr explicit ignore_index_t(const F* f) : f_(*f) {}

  template <::size_t N, class... Ts>
  constexpr decltype(internal_ignore_index::mk_dependent<N, F>()(
      ::std::declval<Ts>()...))
  operator()(Ts&&... ts) const {
    return f_(::std::forward<Ts>(ts)...);
  }

 private:
  F& f_;
};

template <class F>
constexpr ignore_index_no_args_t<F> ignore_index_no_args(F* f) {
  return ignore_index_no_args_t<F>(f);
}

template <class F>
constexpr ignore_index_t<F> ignore_index(F* f) {
  return ignore_index_t<F>(f);
}

template <class Pred>
struct ignore_predicate_index {
  template <::size_t I, class T>
  struct apply : Pred::template apply<T> {};
};

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_IGNORE_INDEX_H_
