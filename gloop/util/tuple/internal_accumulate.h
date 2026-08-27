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

// NOLINT(build/header_guard)
#ifndef IN_UTIL_TUPLE_ACCUMULATE_H
// This is an auxilary header for util/tuple/accumulate.h. It must not be
// included from anywhere else.
#error This header is internal. Include "util/tuple/accumulate.h" instead.
#endif

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_INTERNAL_ACCUMULATE_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_INTERNAL_ACCUMULATE_H_

#include <stddef.h>

#include <utility>

#include "gloop/util/tuple/intrinsics.h"

namespace util {
namespace tuple {
namespace internal_accumulate {

// All symbols defined within namespace internal_accumulate are internal
// to accumulate.h. Do not reference them from outside or your code can break
// without notice.

// index_folder<0> is specialized to terminate the recursion.
//
// index_folder<1> is specialized to correctly differentiate between
// reference and value states. Given that index_folder<0>::operator() always
// returns by reference, it's not safe to use it as recursion termination.
//
// index_folder<2-9> are specialized for efficiency. They let us elide
// copies of the state. Number 9 is chosen arbitrary and can be increased
// if needed. accumulate_index<N> copies the initial state once and then
// does floor(N / M) extra copies, where M is the maximum index for which
// index_folder is specialized (currently, 9).

// See comment for index_folder above, for value_folder is very similar.
//
// The only reason value_folder with all its specializations exists is
// to make accumulate() efficient w.r.t. state copies. If that wasn't a concern,
// accumulate() could be trivially implemented on top of accumulate_index().

template <::size_t N>
struct value_folder_impl;

template <::size_t N, ::size_t I, class T, class F>
struct value_folder_functor {
  T&& t;
  const F& f;
};

template <::size_t N, ::size_t I, class T, class F, class S>
constexpr auto operator*(S&& s, const value_folder_functor<N, I, T, F>& funct)
    -> decltype(value_folder_impl<N>{}.template operator()<I>(
        funct.f, ::std::forward<T>(funct.t), ::std::forward<S>(s))) {
  return value_folder_impl<N>{}.template operator()<I>(
      funct.f, ::std::forward<T>(funct.t), ::std::forward<S>(s));
}

template <::size_t I, ::size_t R, class T, class F, class S,
          ::size_t... fold_steps>
constexpr auto value_folder_reduce_fold(
    const F& f, T&& t, S&& s, ::std::integer_sequence<::size_t, fold_steps...>)
    -> decltype((value_folder_impl<R>{}.template operator()<I>(
                     f, ::std::forward<T>(t), ::std::forward<S>(s)) *
                 ... *
                 value_folder_functor<9, I + R + fold_steps * 9, T, F>{
                     ::std::forward<T>(t), f})) {
  return (value_folder_impl<R>{}.template operator()<I>(f, ::std::forward<T>(t),
                                                        ::std::forward<S>(s)) *
          ... *
          value_folder_functor<9, I + R + fold_steps * 9, T, F>{
              ::std::forward<T>(t), f});
}

template <::size_t N, ::size_t R = (N % 9 == 0 && N > 0) ? 9 : N % 9>
struct value_folder {
  using IntSeq = ::std::make_index_sequence<(N == 0 ? 0 : N - 1) / 9>;
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(const F& f, T&& t, S&& s)
      -> decltype(value_folder_reduce_fold<I, R>(f, ::std::forward<T>(t),
                                                 ::std::forward<S>(s),
                                                 IntSeq{})) const {
    return value_folder_reduce_fold<I, R>(f, ::std::forward<T>(t),
                                          ::std::forward<S>(s), IntSeq{});
  }
};

template <>
struct value_folder<static_cast<::size_t>(-1)> {};

template <>
struct value_folder_impl<0> {
  template <::size_t I, class F, class T, class S>
  constexpr S&& operator()(const F& f, T&& t, S&& s) const {
    return ::std::forward<S>(s);
  }
};

template <>
struct value_folder_impl<1> {
  template <::size_t I, class F, class T, class S>
  constexpr auto operator()(const F& f, T&& t, S&& s) const
      -> decltype(f(::std::forward<S>(s), get<I>(::std::forward<T>(t)))) {
    return f(::std::forward<S>(s), get<I>(::std::forward<T>(t)));
  }
};

template <>
struct value_folder_impl<2> {
  template <::size_t I, class F, class T, class S>
  constexpr auto operator()(const F& f, T&& t, S&& s) const
      -> decltype(f(f(::std::forward<S>(s), get<I>(::std::forward<T>(t))),
                    get<I + 1>(::std::forward<T>(t)))) {
    return f(f(::std::forward<S>(s), get<I>(::std::forward<T>(t))),
             get<I + 1>(::std::forward<T>(t)));
  }
};

template <>
struct value_folder_impl<3> {
  template <::size_t I, class F, class T, class S>
  constexpr auto operator()(const F& f, T&& t, S&& s) const
      -> decltype(f(f(f(::std::forward<S>(s), get<I>(::std::forward<T>(t))),
                      get<I + 1>(::std::forward<T>(t))),
                    get<I + 2>(::std::forward<T>(t)))) {
    return f(f(f(::std::forward<S>(s), get<I>(::std::forward<T>(t))),
               get<I + 1>(::std::forward<T>(t))),
             get<I + 2>(::std::forward<T>(t)));
  }
};

template <>
struct value_folder_impl<4> {
  template <::size_t I, class F, class T, class S>
  constexpr auto operator()(const F& f, T&& t, S&& s) const
      -> decltype(f(f(f(f(::std::forward<S>(s), get<I>(::std::forward<T>(t))),
                        get<I + 1>(::std::forward<T>(t))),
                      get<I + 2>(::std::forward<T>(t))),
                    get<I + 3>(::std::forward<T>(t)))) {
    return f(f(f(f(::std::forward<S>(s), get<I>(::std::forward<T>(t))),
                 get<I + 1>(::std::forward<T>(t))),
               get<I + 2>(::std::forward<T>(t))),
             get<I + 3>(::std::forward<T>(t)));
  }
};

template <>
struct value_folder_impl<5> {
  template <::size_t I, class F, class T, class S>
  constexpr auto operator()(const F& f, T&& t, S&& s) const
      -> decltype(f(f(f(f(f(::std::forward<S>(s), get<I>(::std::forward<T>(t))),
                          get<I + 1>(::std::forward<T>(t))),
                        get<I + 2>(::std::forward<T>(t))),
                      get<I + 3>(::std::forward<T>(t))),
                    get<I + 4>(::std::forward<T>(t)))) {
    return f(f(f(f(f(::std::forward<S>(s), get<I>(::std::forward<T>(t))),
                   get<I + 1>(::std::forward<T>(t))),
                 get<I + 2>(::std::forward<T>(t))),
               get<I + 3>(::std::forward<T>(t))),
             get<I + 4>(::std::forward<T>(t)));
  }
};

template <>
struct value_folder_impl<6> {
  template <::size_t I, class F, class T, class S>
  constexpr auto operator()(const F& f, T&& t, S&& s) const -> decltype(f(
      f(f(f(f(f(::std::forward<S>(s), get<I>(::std::forward<T>(t))),
              get<I + 1>(::std::forward<T>(t))),
            get<I + 2>(::std::forward<T>(t))),
          get<I + 3>(::std::forward<T>(t))),
        get<I + 4>(::std::forward<T>(t))),
      get<I + 5>(::std::forward<T>(t)))) {
    return f(f(f(f(f(f(::std::forward<S>(s), get<I>(::std::forward<T>(t))),
                     get<I + 1>(::std::forward<T>(t))),
                   get<I + 2>(::std::forward<T>(t))),
                 get<I + 3>(::std::forward<T>(t))),
               get<I + 4>(::std::forward<T>(t))),
             get<I + 5>(::std::forward<T>(t)));
  }
};

template <>
struct value_folder_impl<7> {
  template <::size_t I, class F, class T, class S>
  constexpr auto operator()(const F& f, T&& t, S&& s) const -> decltype(f(
      f(f(f(f(f(f(::std::forward<S>(s), get<I>(::std::forward<T>(t))),
                get<I + 1>(::std::forward<T>(t))),
              get<I + 2>(::std::forward<T>(t))),
            get<I + 3>(::std::forward<T>(t))),
          get<I + 4>(::std::forward<T>(t))),
        get<I + 5>(::std::forward<T>(t))),
      get<I + 6>(::std::forward<T>(t)))) {
    return f(f(f(f(f(f(f(::std::forward<S>(s), get<I>(::std::forward<T>(t))),
                       get<I + 1>(::std::forward<T>(t))),
                     get<I + 2>(::std::forward<T>(t))),
                   get<I + 3>(::std::forward<T>(t))),
                 get<I + 4>(::std::forward<T>(t))),
               get<I + 5>(::std::forward<T>(t))),
             get<I + 6>(::std::forward<T>(t)));
  }
};

template <>
struct value_folder_impl<8> {
  template <::size_t I, class F, class T, class S>
  constexpr auto operator()(const F& f, T&& t, S&& s) const -> decltype(f(
      f(f(f(f(f(f(f(::std::forward<S>(s), get<I>(::std::forward<T>(t))),
                  get<I + 1>(::std::forward<T>(t))),
                get<I + 2>(::std::forward<T>(t))),
              get<I + 3>(::std::forward<T>(t))),
            get<I + 4>(::std::forward<T>(t))),
          get<I + 5>(::std::forward<T>(t))),
        get<I + 6>(::std::forward<T>(t))),
      get<I + 7>(::std::forward<T>(t)))) {
    return f(f(f(f(f(f(f(f(::std::forward<S>(s), get<I>(::std::forward<T>(t))),
                         get<I + 1>(::std::forward<T>(t))),
                       get<I + 2>(::std::forward<T>(t))),
                     get<I + 3>(::std::forward<T>(t))),
                   get<I + 4>(::std::forward<T>(t))),
                 get<I + 5>(::std::forward<T>(t))),
               get<I + 6>(::std::forward<T>(t))),
             get<I + 7>(::std::forward<T>(t)));
  }
};

template <>
struct value_folder_impl<9> {
  template <::size_t I, class F, class T, class S>
  constexpr auto operator()(const F& f, T&& t, S&& s) const -> decltype(f(
      f(f(f(f(f(f(f(f(::std::forward<S>(s), get<I>(::std::forward<T>(t))),
                    get<I + 1>(::std::forward<T>(t))),
                  get<I + 2>(::std::forward<T>(t))),
                get<I + 3>(::std::forward<T>(t))),
              get<I + 4>(::std::forward<T>(t))),
            get<I + 5>(::std::forward<T>(t))),
          get<I + 6>(::std::forward<T>(t))),
        get<I + 7>(::std::forward<T>(t))),
      get<I + 8>(::std::forward<T>(t)))) {
    return f(
        f(f(f(f(f(f(f(f(::std::forward<S>(s), get<I>(::std::forward<T>(t))),
                      get<I + 1>(::std::forward<T>(t))),
                    get<I + 2>(::std::forward<T>(t))),
                  get<I + 3>(::std::forward<T>(t))),
                get<I + 4>(::std::forward<T>(t))),
              get<I + 5>(::std::forward<T>(t))),
            get<I + 6>(::std::forward<T>(t))),
          get<I + 7>(::std::forward<T>(t))),
        get<I + 8>(::std::forward<T>(t)));
  }
};

template <>
struct value_folder_impl<static_cast<::size_t>(-1)> {};

template <::size_t N>
struct value_index_folder_impl {};

template <::size_t N, ::size_t I, class T, class F>
struct value_index_folder_functor {
  T&& t;
  const F& f;
};

template <::size_t N, ::size_t I, class T, class F, class S>
constexpr decltype(auto) operator*(
    S&& s, const value_index_folder_functor<N, I, T, F>& funct) {
  return value_index_folder_impl<N>{}.template operator()<I>(
      funct.f, ::std::forward<T>(funct.t), ::std::forward<S>(s));
}

template <::size_t I, ::size_t R, class T, class F, class S,
          ::size_t... fold_steps>
constexpr decltype(auto) value_index_folder_reduce_fold(
    const F& f, T&& t, S&& s,
    ::std::integer_sequence<::size_t, fold_steps...>) {
  return (value_index_folder_impl<R>{}.template operator()<I>(
              f, ::std::forward<T>(t), ::std::forward<S>(s)) *
          ... *
          value_index_folder_functor<9, I + R + fold_steps * 9, T, F>{
              ::std::forward<T>(t), f});
}

template <::size_t N, ::size_t R = (N % 9 == 0 && N > 0) ? 9 : N % 9>
struct value_index_folder {
  using IntSeq = ::std::make_index_sequence<(N == 0 ? 0 : N - 1) / 9>;
  template <::size_t I, class T, class F, class S>
  constexpr decltype(auto) operator()(const F& f, T&& t, S&& s) const {
    return value_index_folder_reduce_fold<I, R>(f, ::std::forward<T>(t),
                                                ::std::forward<S>(s), IntSeq{});
  }
};

template <>
struct value_index_folder<static_cast<::size_t>(-1)> {};

template <>
struct value_index_folder_impl<0> {
  template <::size_t I, class F, class T, class S>
  constexpr S&& operator()(const F& f, T&& t, S&& s) const {
    return ::std::forward<S>(s);
  }
};

template <>
struct value_index_folder_impl<1> {
  template <::size_t I, class F, class T, class S>
  constexpr auto operator()(const F& f, T&& t, S&& s) const
      -> decltype(f.template operator()<I>(::std::forward<S>(s),
                                           get<I>(::std::forward<T>(t)))) {
    return f.template operator()<I>(::std::forward<S>(s),
                                    get<I>(::std::forward<T>(t)));
  }
};

template <>
struct value_index_folder_impl<2> {
  template <::size_t I, class F, class T, class S>
  constexpr auto operator()(const F& f, T&& t, S&& s) const
      -> decltype(f.template operator()<I + 1>(
          f.template operator()<I>(::std::forward<S>(s),
                                   get<I>(::std::forward<T>(t))),
          get<I + 1>(::std::forward<T>(t)))) {
    return f.template operator()<I + 1>(
        f.template operator()<I>(::std::forward<S>(s),
                                 get<I>(::std::forward<T>(t))),
        get<I + 1>(::std::forward<T>(t)));
  }
};

template <>
struct value_index_folder_impl<3> {
  template <::size_t I, class F, class T, class S>
  constexpr auto operator()(const F& f, T&& t, S&& s) const
      -> decltype(f.template operator()<I + 2>(
          f.template operator()<I + 1>(
              f.template operator()<I>(::std::forward<S>(s),
                                       get<I>(::std::forward<T>(t))),
              get<I + 1>(::std::forward<T>(t))),
          get<I + 2>(::std::forward<T>(t)))) {
    return f.template operator()<I + 2>(
        f.template operator()<I + 1>(
            f.template operator()<I>(::std::forward<S>(s),
                                     get<I>(::std::forward<T>(t))),
            get<I + 1>(::std::forward<T>(t))),
        get<I + 2>(::std::forward<T>(t)));
  }
};

template <>
struct value_index_folder_impl<4> {
  template <::size_t I, class F, class T, class S>
  constexpr auto operator()(const F& f, T&& t, S&& s) const
      -> decltype(f.template operator()<I + 3>(
          f.template operator()<I + 2>(
              f.template operator()<I + 1>(
                  f.template operator()<I>(::std::forward<S>(s),
                                           get<I>(::std::forward<T>(t))),
                  get<I + 1>(::std::forward<T>(t))),
              get<I + 2>(::std::forward<T>(t))),
          get<I + 3>(::std::forward<T>(t)))) {
    return f.template operator()<I + 3>(
        f.template operator()<I + 2>(
            f.template operator()<I + 1>(
                f.template operator()<I>(::std::forward<S>(s),
                                         get<I>(::std::forward<T>(t))),
                get<I + 1>(::std::forward<T>(t))),
            get<I + 2>(::std::forward<T>(t))),
        get<I + 3>(::std::forward<T>(t)));
  }
};

template <>
struct value_index_folder_impl<5> {
  template <::size_t I, class F, class T, class S>
  constexpr auto operator()(const F& f, T&& t, S&& s) const
      -> decltype(f.template operator()<I + 4>(
          f.template operator()<I + 3>(
              f.template operator()<I + 2>(
                  f.template operator()<I + 1>(
                      f.template operator()<I>(::std::forward<S>(s),
                                               get<I>(::std::forward<T>(t))),
                      get<I + 1>(::std::forward<T>(t))),
                  get<I + 2>(::std::forward<T>(t))),
              get<I + 3>(::std::forward<T>(t))),
          get<I + 4>(::std::forward<T>(t)))) {
    return f.template operator()<I + 4>(
        f.template operator()<I + 3>(
            f.template operator()<I + 2>(
                f.template operator()<I + 1>(
                    f.template operator()<I>(::std::forward<S>(s),
                                             get<I>(::std::forward<T>(t))),
                    get<I + 1>(::std::forward<T>(t))),
                get<I + 2>(::std::forward<T>(t))),
            get<I + 3>(::std::forward<T>(t))),
        get<I + 4>(::std::forward<T>(t)));
  }
};

template <>
struct value_index_folder_impl<6> {
  template <::size_t I, class F, class T, class S>
  constexpr auto operator()(const F& f, T&& t, S&& s) const
      -> decltype(f.template operator()<I + 5>(
          f.template operator()<I + 4>(
              f.template operator()<I + 3>(
                  f.template operator()<I + 2>(
                      f.template operator()<I + 1>(
                          f.template operator()<I>(
                              ::std::forward<S>(s),
                              get<I>(::std::forward<T>(t))),
                          get<I + 1>(::std::forward<T>(t))),
                      get<I + 2>(::std::forward<T>(t))),
                  get<I + 3>(::std::forward<T>(t))),
              get<I + 4>(::std::forward<T>(t))),
          get<I + 5>(::std::forward<T>(t)))) {
    return f.template operator()<I + 5>(
        f.template operator()<I + 4>(
            f.template operator()<I + 3>(
                f.template operator()<I + 2>(
                    f.template operator()<I + 1>(
                        f.template operator()<I>(::std::forward<S>(s),
                                                 get<I>(::std::forward<T>(t))),
                        get<I + 1>(::std::forward<T>(t))),
                    get<I + 2>(::std::forward<T>(t))),
                get<I + 3>(::std::forward<T>(t))),
            get<I + 4>(::std::forward<T>(t))),
        get<I + 5>(::std::forward<T>(t)));
  }
};

template <>
struct value_index_folder_impl<7> {
  template <::size_t I, class F, class T, class S>
  constexpr auto operator()(const F& f, T&& t, S&& s) const
      -> decltype(f.template operator()<I + 6>(
          f.template operator()<I + 5>(
              f.template operator()<I + 4>(
                  f.template operator()<I + 3>(
                      f.template operator()<I + 2>(
                          f.template operator()<I + 1>(
                              f.template operator()<I>(
                                  ::std::forward<S>(s),
                                  get<I>(::std::forward<T>(t))),
                              get<I + 1>(::std::forward<T>(t))),
                          get<I + 2>(::std::forward<T>(t))),
                      get<I + 3>(::std::forward<T>(t))),
                  get<I + 4>(::std::forward<T>(t))),
              get<I + 5>(::std::forward<T>(t))),
          get<I + 6>(::std::forward<T>(t)))) {
    return f.template operator()<I + 6>(
        f.template operator()<I + 5>(
            f.template operator()<I + 4>(
                f.template operator()<I + 3>(
                    f.template operator()<I + 2>(
                        f.template operator()<I + 1>(
                            f.template operator()<I>(
                                ::std::forward<S>(s),
                                get<I>(::std::forward<T>(t))),
                            get<I + 1>(::std::forward<T>(t))),
                        get<I + 2>(::std::forward<T>(t))),
                    get<I + 3>(::std::forward<T>(t))),
                get<I + 4>(::std::forward<T>(t))),
            get<I + 5>(::std::forward<T>(t))),
        get<I + 6>(::std::forward<T>(t)));
  }
};

template <>
struct value_index_folder_impl<8> {
  template <::size_t I, class F, class T, class S>
  constexpr auto operator()(const F& f, T&& t, S&& s) const
      -> decltype(f.template operator()<I + 7>(
          f.template operator()<I + 6>(
              f.template operator()<I + 5>(
                  f.template operator()<I + 4>(
                      f.template operator()<I + 3>(
                          f.template operator()<I + 2>(
                              f.template operator()<I + 1>(
                                  f.template operator()<I>(
                                      ::std::forward<S>(s),
                                      get<I>(::std::forward<T>(t))),
                                  get<I + 1>(::std::forward<T>(t))),
                              get<I + 2>(::std::forward<T>(t))),
                          get<I + 3>(::std::forward<T>(t))),
                      get<I + 4>(::std::forward<T>(t))),
                  get<I + 5>(::std::forward<T>(t))),
              get<I + 6>(::std::forward<T>(t))),
          get<I + 7>(::std::forward<T>(t)))) {
    return f.template operator()<I + 7>(
        f.template operator()<I + 6>(
            f.template operator()<I + 5>(
                f.template operator()<I + 4>(
                    f.template operator()<I + 3>(
                        f.template operator()<I + 2>(
                            f.template operator()<I + 1>(
                                f.template operator()<I>(
                                    ::std::forward<S>(s),
                                    get<I>(::std::forward<T>(t))),
                                get<I + 1>(::std::forward<T>(t))),
                            get<I + 2>(::std::forward<T>(t))),
                        get<I + 3>(::std::forward<T>(t))),
                    get<I + 4>(::std::forward<T>(t))),
                get<I + 5>(::std::forward<T>(t))),
            get<I + 6>(::std::forward<T>(t))),
        get<I + 7>(::std::forward<T>(t)));
  }
};

template <>
struct value_index_folder_impl<9> {
  template <::size_t I, class F, class T, class S>
  constexpr auto operator()(const F& f, T&& t, S&& s) const
      -> decltype(f.template operator()<I + 8>(
          f.template operator()<I + 7>(
              f.template operator()<I + 6>(
                  f.template operator()<I + 5>(
                      f.template operator()<I + 4>(
                          f.template operator()<I + 3>(
                              f.template operator()<I + 2>(
                                  f.template operator()<I + 1>(
                                      f.template operator()<I>(
                                          ::std::forward<S>(s),
                                          get<I>(::std::forward<T>(t))),
                                      get<I + 1>(::std::forward<T>(t))),
                                  get<I + 2>(::std::forward<T>(t))),
                              get<I + 3>(::std::forward<T>(t))),
                          get<I + 4>(::std::forward<T>(t))),
                      get<I + 5>(::std::forward<T>(t))),
                  get<I + 6>(::std::forward<T>(t))),
              get<I + 7>(::std::forward<T>(t))),
          get<I + 8>(::std::forward<T>(t)))) {
    return f.template operator()<I + 8>(
        f.template operator()<I + 7>(
            f.template operator()<I + 6>(
                f.template operator()<I + 5>(
                    f.template operator()<I + 4>(
                        f.template operator()<I + 3>(
                            f.template operator()<I + 2>(
                                f.template operator()<I + 1>(
                                    f.template operator()<I>(
                                        ::std::forward<S>(s),
                                        get<I>(::std::forward<T>(t))),
                                    get<I + 1>(::std::forward<T>(t))),
                                get<I + 2>(::std::forward<T>(t))),
                            get<I + 3>(::std::forward<T>(t))),
                        get<I + 4>(::std::forward<T>(t))),
                    get<I + 5>(::std::forward<T>(t))),
                get<I + 6>(::std::forward<T>(t))),
            get<I + 7>(::std::forward<T>(t))),
        get<I + 8>(::std::forward<T>(t)));
  }
};

template <>
struct value_index_folder_impl<static_cast<::size_t>(-1)> {};

template <::size_t N>
struct index_folder_impl {};

template <::size_t N, ::size_t I, class T, class F>
struct index_folder_functor {
  const F& f;
};

template <::size_t N, ::size_t I, class T, class F, class S>
constexpr auto operator*(S&& s, const index_folder_functor<N, I, T, F>& funct)
    -> decltype(index_folder_impl<N>{}.template operator()<I, T>(
        funct.f, ::std::forward<S>(s))) {
  return index_folder_impl<N>{}.template operator()<I, T>(funct.f,
                                                          ::std::forward<S>(s));
}

template <::size_t I, class T, ::size_t R, class F, class S,
          ::size_t... fold_steps>
constexpr auto index_folder_reduce_fold(
    const F& f, S&& s, ::std::integer_sequence<::size_t, fold_steps...>)
    -> decltype((index_folder_impl<R>{}.template operator()<I, T>(
                     f, ::std::forward<S>(s)) *
                 ... *
                 index_folder_functor<9, I + R + fold_steps * 9, T, F>{f})) {
  return (index_folder_impl<R>{}.template operator()<I, T>(
              f, ::std::forward<S>(s)) *
          ... * index_folder_functor<9, I + R + fold_steps * 9, T, F>{f});
}

template <::size_t N, ::size_t R = (N % 9 == 0 && N > 0) ? 9 : N % 9>
struct index_folder {
  using IntSeq = ::std::make_index_sequence<(N == 0 ? 0 : N - 1) / 9>;
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(const F& f, S&& s) const
      -> decltype(index_folder_reduce_fold<I, T, R>(f, ::std::forward<S>(s),
                                                    IntSeq{})) {
    return index_folder_reduce_fold<I, T, R>(f, ::std::forward<S>(s), IntSeq{});
  }
};

template <>
struct index_folder_impl<0> {
  template <::size_t I, class T, class F, class S>
  constexpr S&& operator()(const F& f, S&& s) const {
    return ::std::forward<S>(s);
  }
};

template <>
struct index_folder_impl<1> {
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<I, typename element<I, T>::type>(
          ::std::forward<S>(s))) {
    return f.template operator()<I, typename element<I, T>::type>(
        ::std::forward<S>(s));
  }
};

template <>
struct index_folder_impl<2> {
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(const F& f, S&& s) const
      -> decltype(f.template
                  operator()<I + 1, typename element<I + 1, T>::type>(
                      f.template operator()<I, typename element<I, T>::type>(
                          ::std::forward<S>(s)))) {
    return f.template operator()<I + 1, typename element<I + 1, T>::type>(
        f.template operator()<I, typename element<I, T>::type>(
            ::std::forward<S>(s)));
  }
};

template <>
struct index_folder_impl<3> {
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<I + 2,
                                        typename element<I + 2, T>::type>(
          f.template operator()<I + 1, typename element<I + 1, T>::type>(
              f.template operator()<I, typename element<I, T>::type>(
                  ::std::forward<S>(s))))) {
    return f.template operator()<I + 2, typename element<I + 2, T>::type>(
        f.template operator()<I + 1, typename element<I + 1, T>::type>(
            f.template operator()<I, typename element<I, T>::type>(
                ::std::forward<S>(s))));
  }
};

template <>
struct index_folder_impl<4> {
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<I + 3,
                                        typename element<I + 3, T>::type>(
          f.template operator()<I + 2, typename element<I + 2, T>::type>(
              f.template operator()<I + 1, typename element<I + 1, T>::type>(
                  f.template operator()<I, typename element<I, T>::type>(
                      ::std::forward<S>(s)))))) {
    return f.template operator()<I + 3, typename element<I + 3, T>::type>(
        f.template operator()<I + 2, typename element<I + 2, T>::type>(
            f.template operator()<I + 1, typename element<I + 1, T>::type>(
                f.template operator()<I, typename element<I, T>::type>(
                    ::std::forward<S>(s)))));
  }
};

template <>
struct index_folder_impl<5> {
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<I + 4,
                                        typename element<I + 4, T>::type>(
          f.template operator()<I + 3, typename element<I + 3, T>::type>(
              f.template operator()<I + 2, typename element<I + 2, T>::type>(
                  f.template
                  operator()<I + 1, typename element<I + 1, T>::type>(
                      f.template operator()<I, typename element<I, T>::type>(
                          ::std::forward<S>(s))))))) {
    return f.template operator()<I + 4, typename element<I + 4, T>::type>(
        f.template operator()<I + 3, typename element<I + 3, T>::type>(
            f.template operator()<I + 2, typename element<I + 2, T>::type>(
                f.template operator()<I + 1, typename element<I + 1, T>::type>(
                    f.template operator()<I, typename element<I, T>::type>(
                        ::std::forward<S>(s))))));
  }
};

template <>
struct index_folder_impl<6> {
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<I + 5,
                                        typename element<I + 5, T>::type>(
          f.template operator()<I + 4, typename element<I + 4, T>::type>(
              f.template operator()<I + 3, typename element<I + 3, T>::type>(
                  f.template
                  operator()<I + 2, typename element<I + 2, T>::type>(
                      f.template
                      operator()<I + 1, typename element<I + 1, T>::type>(
                          f.template
                          operator()<I, typename element<I, T>::type>(
                              ::std::forward<S>(s)))))))) {
    return f.template operator()<I + 5, typename element<I + 5, T>::type>(
        f.template operator()<I + 4, typename element<I + 4, T>::type>(
            f.template operator()<I + 3, typename element<I + 3, T>::type>(
                f.template operator()<I + 2, typename element<I + 2, T>::type>(
                    f.template
                    operator()<I + 1, typename element<I + 1, T>::type>(
                        f.template operator()<I, typename element<I, T>::type>(
                            ::std::forward<S>(s)))))));
  }
};

template <>
struct index_folder_impl<7> {
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<I + 6,
                                        typename element<I + 6, T>::type>(
          f.template operator()<I + 5, typename element<I + 5, T>::type>(
              f.template operator()<I + 4, typename element<I + 4, T>::type>(
                  f.template
                  operator()<I + 3, typename element<I + 3, T>::type>(
                      f.template
                      operator()<I + 2, typename element<I + 2, T>::type>(
                          f.template
                          operator()<I + 1, typename element<I + 1, T>::type>(
                              f.template
                              operator()<I, typename element<I, T>::type>(
                                  ::std::forward<S>(s))))))))) {
    return f.template operator()<I + 6, typename element<I + 6, T>::type>(
        f.template operator()<I + 5, typename element<I + 5, T>::type>(
            f.template operator()<I + 4, typename element<I + 4, T>::type>(
                f.template operator()<I + 3, typename element<I + 3, T>::type>(
                    f.template
                    operator()<I + 2, typename element<I + 2, T>::type>(
                        f.template
                        operator()<I + 1, typename element<I + 1, T>::type>(
                            f.template
                            operator()<I, typename element<I, T>::type>(
                                ::std::forward<S>(s))))))));
  }
};

template <>
struct index_folder_impl<8> {
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<I + 7,
                                        typename element<I + 7, T>::type>(
          f.template operator()<I + 6, typename element<I + 6, T>::type>(
              f.template operator()<I + 5, typename element<I + 5, T>::type>(
                  f.template
                  operator()<I + 4, typename element<I + 4, T>::type>(
                      f.template
                      operator()<I + 3, typename element<I + 3, T>::type>(
                          f.template
                          operator()<I + 2, typename element<I + 2, T>::type>(
                              f.template operator()<
                                  I + 1, typename element<I + 1, T>::type>(
                                  f.template
                                  operator()<I, typename element<I, T>::type>(
                                      ::std::forward<S>(s)))))))))) {
    return f.template operator()<I + 7, typename element<I + 7, T>::type>(
        f.template operator()<I + 6, typename element<I + 6, T>::type>(
            f.template operator()<I + 5, typename element<I + 5, T>::type>(
                f.template operator()<I + 4, typename element<I + 4, T>::type>(
                    f.template
                    operator()<I + 3, typename element<I + 3, T>::type>(
                        f.template
                        operator()<I + 2, typename element<I + 2, T>::type>(
                            f.template
                            operator()<I + 1, typename element<I + 1, T>::type>(
                                f.template
                                operator()<I, typename element<I, T>::type>(
                                    ::std::forward<S>(s)))))))));
  }
};

template <>
struct index_folder_impl<9> {
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<I + 8,
                                        typename element<I + 8, T>::type>(
          f.template operator()<I + 7, typename element<I + 7, T>::type>(
              f.template operator()<I + 6, typename element<I + 6, T>::type>(
                  f.template
                  operator()<I + 5, typename element<I + 5, T>::type>(
                      f.template
                      operator()<I + 4, typename element<I + 4, T>::type>(
                          f.template
                          operator()<I + 3, typename element<I + 3, T>::type>(
                              f.template operator()<
                                  I + 2, typename element<I + 2, T>::type>(
                                  f.template operator()<
                                      I + 1, typename element<I + 1, T>::type>(
                                      f.template operator()<
                                          I, typename element<I, T>::type>(
                                          ::std::forward<S>(s))))))))))) {
    return f.template operator()<I + 8, typename element<I + 8, T>::type>(
        f.template operator()<I + 7, typename element<I + 7, T>::type>(
            f.template operator()<I + 6, typename element<I + 6, T>::type>(
                f.template operator()<I + 5, typename element<I + 5, T>::type>(
                    f.template
                    operator()<I + 4, typename element<I + 4, T>::type>(
                        f.template
                        operator()<I + 3, typename element<I + 3, T>::type>(
                            f.template
                            operator()<I + 2, typename element<I + 2, T>::type>(
                                f.template operator()<
                                    I + 1, typename element<I + 1, T>::type>(
                                    f.template
                                    operator()<I, typename element<I, T>::type>(
                                        ::std::forward<S>(s))))))))));
  }
};

template <::size_t N>
struct folder_impl {};

template <>
struct folder_impl<0> {
  template <::size_t I, class T, class F, class S>
  constexpr S&& operator()(const F& f, S&& s) const {
    return ::std::forward<S>(s);
  }
};

template <>
struct folder_impl<1> {
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(const F& f, S&& s) const
      -> decltype(::std::declval<const F&>()
                      .template operator()<typename element<I, T>::type>(
                          ::std::declval<S&>())) {
    return f.template operator()<typename element<I, T>::type>(
        ::std::forward<S>(s));
  }
};

template <>
struct folder_impl<2> {
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<typename element<I + 1, T>::type>(
          f.template operator()<typename element<I, T>::type>(
              ::std::forward<S>(s)))) {
    return f.template operator()<typename element<I + 1, T>::type>(
        f.template operator()<typename element<I, T>::type>(
            ::std::forward<S>(s)));
  }
};

template <>
struct folder_impl<3> {
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<typename element<I + 2, T>::type>(
          f.template operator()<typename element<I + 1, T>::type>(
              f.template operator()<typename element<I, T>::type>(
                  ::std::forward<S>(s))))) {
    return f.template operator()<typename element<I + 2, T>::type>(
        f.template operator()<typename element<I + 1, T>::type>(
            f.template operator()<typename element<I, T>::type>(
                ::std::forward<S>(s))));
  }
};

template <>
struct folder_impl<4> {
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<typename element<I + 3, T>::type>(
          f.template operator()<typename element<I + 2, T>::type>(
              f.template operator()<typename element<I + 1, T>::type>(
                  f.template operator()<typename element<I, T>::type>(
                      ::std::forward<S>(s)))))) {
    return f.template operator()<typename element<I + 3, T>::type>(
        f.template operator()<typename element<I + 2, T>::type>(
            f.template operator()<typename element<I + 1, T>::type>(
                f.template operator()<typename element<I, T>::type>(
                    ::std::forward<S>(s)))));
  }
};

template <>
struct folder_impl<5> {
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<typename element<I + 4, T>::type>(
          f.template operator()<typename element<I + 3, T>::type>(
              f.template operator()<typename element<I + 2, T>::type>(
                  f.template operator()<typename element<I + 1, T>::type>(
                      f.template operator()<typename element<I, T>::type>(
                          ::std::forward<S>(s))))))) {
    return f.template operator()<typename element<I + 4, T>::type>(
        f.template operator()<typename element<I + 3, T>::type>(
            f.template operator()<typename element<I + 2, T>::type>(
                f.template operator()<typename element<I + 1, T>::type>(
                    f.template operator()<typename element<I, T>::type>(
                        ::std::forward<S>(s))))));
  }
};

template <>
struct folder_impl<6> {
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<typename element<I + 5, T>::type>(
          f.template operator()<typename element<I + 4, T>::type>(
              f.template operator()<typename element<I + 3, T>::type>(
                  f.template operator()<typename element<I + 2, T>::type>(
                      f.template operator()<typename element<I + 1, T>::type>(
                          f.template operator()<typename element<I, T>::type>(
                              ::std::forward<S>(s)))))))) {
    return f.template operator()<typename element<I + 5, T>::type>(
        f.template operator()<typename element<I + 4, T>::type>(
            f.template operator()<typename element<I + 3, T>::type>(
                f.template operator()<typename element<I + 2, T>::type>(
                    f.template operator()<typename element<I + 1, T>::type>(
                        f.template operator()<typename element<I, T>::type>(
                            ::std::forward<S>(s)))))));
  }
};

template <>
struct folder_impl<7> {
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(
      const F& f,
      S&& s) const -> decltype(f.template operator()<typename element<I + 6,
                                                                      T>::type>(
      f.template operator()<typename element<I + 5, T>::type>(
          f.template operator()<typename element<I + 4, T>::type>(
              f.template operator()<typename element<I + 3, T>::type>(
                  f.template operator()<typename element<I + 2, T>::type>(
                      f.template operator()<typename element<I + 1, T>::type>(
                          f.template operator()<typename element<I, T>::type>(
                              ::std::forward<S>(s))))))))) {
    return f.template operator()<typename element<I + 6, T>::type>(
        f.template operator()<typename element<I + 5, T>::type>(
            f.template operator()<typename element<I + 4, T>::type>(
                f.template operator()<typename element<I + 3, T>::type>(
                    f.template operator()<typename element<I + 2, T>::type>(
                        f.template operator()<typename element<I + 1, T>::type>(
                            f.template operator()<typename element<I, T>::type>(
                                ::std::forward<S>(s))))))));
  }
};

template <>
struct folder_impl<8> {
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<typename element<I + 7, T>::type>(
          f.template operator()<typename element<I + 6, T>::type>(
              f.template operator()<typename element<I + 5, T>::type>(
                  f.template operator()<typename element<I + 4, T>::type>(
                      f.template operator()<typename element<I + 3, T>::type>(
                          f.template
                          operator()<typename element<I + 2, T>::type>(
                              f.template
                              operator()<typename element<I + 1, T>::type>(
                                  f.template
                                  operator()<typename element<I, T>::type>(
                                      ::std::forward<S>(s)))))))))) {
    return f.template operator()<
        typename element<I + 7, T>::type>(f.template operator()<
                                          typename element<I + 6, T>::type>(
        f.template operator()<typename element<I + 5, T>::type>(
            f.template operator()<typename element<I + 4, T>::type>(
                f.template operator()<typename element<I + 3, T>::type>(
                    f.template operator()<typename element<I + 2, T>::type>(
                        f.template operator()<typename element<I + 1, T>::type>(
                            f.template operator()<typename element<I, T>::type>(
                                ::std::forward<S>(s)))))))));
  }
};

template <>
struct folder_impl<9> {
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<typename element<I + 8, T>::type>(
          f.template operator()<typename element<I + 7, T>::type>(
              f.template operator()<typename element<I + 6, T>::type>(
                  f.template operator()<typename element<I + 5, T>::type>(
                      f.template operator()<typename element<I + 4, T>::type>(
                          f.template
                          operator()<typename element<I + 3, T>::type>(
                              f.template
                              operator()<typename element<I + 2, T>::type>(
                                  f.template
                                  operator()<typename element<I + 1, T>::type>(
                                      f.template
                                      operator()<typename element<I, T>::type>(
                                          ::std::forward<S>(s))))))))))) {
    return f.template operator()<typename element<I + 8, T>::type>(
        f.template operator()<typename element<I + 7, T>::type>(
            f.template operator()<typename element<I + 6, T>::type>(
                f.template operator()<typename element<I + 5, T>::type>(
                    f.template operator()<typename element<I + 4, T>::type>(
                        f.template operator()<typename element<I + 3, T>::type>(
                            f.template
                            operator()<typename element<I + 2, T>::type>(
                                f.template
                                operator()<typename element<I + 1, T>::type>(
                                    f.template
                                    operator()<typename element<I, T>::type>(
                                        ::std::forward<S>(s))))))))));
  }
};

// Holds template parameters and a reference to functor f required to call
// folder_impl<N>::template operator()<I, T>.
template <::size_t N, ::size_t I, class T, class F>
struct folder_functor {
  const F& f;
};

// The C++17 fold expression only works with a subset of operators, therefore we
// put the functionality we need during fold expansion in the * operator. We
// have to use a binary left fold to consume the types in the order defined in
// the API. This being a free function instead of a member function of
// folder_functor is a result of a design choice to reduce a parameter pack on
// an expression containing an instantiation of a folder_functor struct.
template <::size_t N, ::size_t I, class T, class F, class S>
constexpr auto operator*(S&& s, const folder_functor<N, I, T, F>& funct)
    -> decltype(folder_impl<N>{}.template operator()<I, T>(
        funct.f, ::std::forward<S>(s))) {
  return folder_impl<N>{}.template operator()<I, T>(funct.f,
                                                    ::std::forward<S>(s));
}

template <::size_t I, class T, ::size_t R, class F, class S,
          ::size_t... fold_steps>
constexpr auto folder_reduce_fold(
    const F& f, S&& s, ::std::integer_sequence<::size_t, fold_steps...>)
    -> decltype((
        folder_impl<R>{}.template operator()<I, T>(f, ::std::forward<S>(s)) *
        ... * folder_functor<9, I + R + fold_steps * 9, T, F>{f})) {
  return (folder_impl<R>{}.template operator()<I, T>(f, ::std::forward<S>(s)) *
          ... * folder_functor<9, I + R + fold_steps * 9, T, F>{f});
}

template <::size_t N, ::size_t R = (N % 9 == 0 && N > 0) ? 9 : N % 9>
struct folder {
  using IntSeq = ::std::make_index_sequence<(N == 0 ? 0 : N - 1) / 9>;
  template <::size_t I, class T, class F, class S>
  constexpr auto operator()(const F& f, S&& s) const
      -> decltype(folder_reduce_fold<I, T, R>(f, ::std::forward<S>(s),
                                              IntSeq{})) {
    return folder_reduce_fold<I, T, R>(f, ::std::forward<S>(s), IntSeq{});
  }
};

}  // namespace internal_accumulate
}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_INTERNAL_ACCUMULATE_H_
