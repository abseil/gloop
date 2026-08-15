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

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_FUSE_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_FUSE_H_

#include <type_traits>
#include <utility>

#include "gloop/util/tuple/apply.h"

namespace util {
namespace tuple {

// Function combinator that turns any function into a function taking
// one argument of type tuple.
//
// Given
//   F f;
//   fused<F> u;
//
// Expression
//   u(std::forward_as_tuple(arg...))
// reduces to
//   f(arg...)
//
// Note that fused is the inverse of unfused.
template <class F>
struct fused {
  fused() : f_() {}
  explicit fused(const F& f) : f_(f) {}
  explicit fused(F&& f) : f_(::std::move(f)) {}

  const F& base() const { return f_; }
  F& base() { return f_; }

  template <class T>
  decltype(::util::tuple::apply(::std::declval<const F&>(),
                                ::std::declval<T>()))
  operator()(T&& t) const {
    return ::util::tuple::apply(f_, ::std::forward<T>(t));
  }

  template <class T>
  decltype(::util::tuple::apply(::std::declval<F&>(), ::std::declval<T>()))
  operator()(T&& t) {
    return ::util::tuple::apply(f_, ::std::forward<T>(t));
  }

 private:
  F f_;
};

template <class F>
fused<typename ::std::decay<F>::type> fuse(F&& f) {
  return fused<typename ::std::decay<F>::type>(::std::forward<F>(f));
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_FUSE_H_
