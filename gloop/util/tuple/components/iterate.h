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
// Function template iterate() applies the provided functor specified number
// of times.
//
// iterate<N>(f, val) is equivalent to f(...f(f(val))...) where f() is called N
// times.
//
// iterate<N>(f) is equivalent to f(), f(), ..., f(). This version is
// essentially a specialization of iterate<N>(f, val) for functions that
// accept and return void.
//
// iterate_index() is similar to iterate(). The only difference is that
// it passes the indices of the elements to the functor as the first template
// parameter of type size_t.
//
// iterate_index<N>(f, val) is equivalent to
// f.operator()<N-1>(...f.operator()<1>(f.operator()<0>(val))...).
//
// iterate_index<N>(f) is equivalent to f<0>(), f<1>(), ..., f<N-1>().

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_ITERATE_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_ITERATE_H_

#include <stddef.h>

#include <utility>

#include "gloop/util/tuple/components/ignore_index.h"

#define IN_UTIL_TUPLE_COMPONENTS_ITERATE_H
#include "gloop/util/tuple/components/internal_iterate.h"
#undef IN_UTIL_TUPLE_COMPONENTS_ITERATE_H

namespace util {
namespace tuple {

// iterate_index<N>(f, val) is equivalent to
// f.operator()<N-1>(...f.operator()<1>(f.operator()<0>(val))...).
template <::size_t N, class F, class S>
auto iterate_index(const F& f, S&& state)
    -> decltype(internal_iterate::index_folder<N>().template operator()<0>(
        f, ::std::forward<S>(state))) {
  return ::util::tuple::internal_iterate::index_folder<N>()
      .template operator()<0>(f, ::std::forward<S>(state));
}

// iterate<N>(f, val) is equivalent to f(...f(f(val))...) where f() is called N
// times.
template <::size_t N, class F, class S>
auto iterate(const F& f, S&& state)
    -> decltype(internal_iterate::folder<N>()(f, ::std::forward<S>(state))) {
  return ::util::tuple::internal_iterate::folder<N>()(f,
                                                      ::std::forward<S>(state));
}

// iterate_index<N>(f) is equivalent to f<0>(), f<1>(), ..., f<N-1>().
template <::size_t N, class F>
void iterate_index(const F& f) {
  ::util::tuple::internal_iterate::iterate_index_fold<0, N>(f);
}

// iterate<N>(f) is equivalent to f(), f(), ..., f().
template <::size_t N, class F>
void iterate(const F& f) {
  ::util::tuple::iterate_index<N>(ignore_index_no_args(&f));
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_ITERATE_H_
