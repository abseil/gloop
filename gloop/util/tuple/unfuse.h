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

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_UNFUSE_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_UNFUSE_H_

#include <tuple>
#include <type_traits>
#include <utility>

namespace util {
namespace tuple {

// Function combinator that turns a function taking a tuple as an argument
// into a function taking N arguments.
//
// Given
//   F f;
//   unfused<F> u(f);
//
// Expression
//   u(arg...)
// reduces to
//   f(std::forward_as_tuple(arg...))
//
// Note that unfused is the inverse of fused.
template <class F>
struct unfused {
  unfused() : f_() {}
  explicit unfused(const F& f) : f_(f) {}
  explicit unfused(F&& f) : f_(::std::move(f)) {}

  const F& base() const { return f_; }
  F& base() { return f_; }

  template <class... Args>
  decltype(::std::declval<F&>()(
      ::std::forward_as_tuple(::std::declval<Args>()...)))
  operator()(Args&&... args) {
    return f_(::std::forward_as_tuple(::std::forward<Args>(args)...));
  }

  template <class... Args>
  decltype(::std::declval<const F&>()(
      ::std::forward_as_tuple(::std::declval<Args>()...)))
  operator()(Args&&... args) const {
    return f_(::std::forward_as_tuple(::std::forward<Args>(args)...));
  }

 private:
  F f_;
};

template <class F>
unfused<typename ::std::decay<F>::type> unfuse(F&& f) {
  return unfused<typename ::std::decay<F>::type>(::std::forward<F>(f));
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_UNFUSE_H_
