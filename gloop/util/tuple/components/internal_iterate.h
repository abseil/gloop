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

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_INTERNAL_ITERATE_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_INTERNAL_ITERATE_H_

#ifndef IN_UTIL_TUPLE_COMPONENTS_ITERATE_H
// This is an auxilary header for util/tuple/iterate.h. It must not be
// included from anywhere else.
#error This header is internal. Include "util/tuple/iterate.h" instead.
#endif

#include <stddef.h>

#include <utility>

#include "gloop/util/tuple/components/array.h"
#include "gloop/util/tuple/components/intrinsics.h"
#include "gloop/util/tuple/components/pair.h"
#include "gloop/util/tuple/components/std_tuple.h"

namespace util {
namespace tuple {
namespace internal_iterate {

// All symbols defined within namespace internal_iterate are internal
// to iterate.h. Do not reference them from outside or your code can break
// without notice.

// Call f<start + i> for all i in the std::integer_sequence.
template <size_t start, class F, size_t... index>
void iterate_index_fold_base(const F& f,
                             std::integer_sequence<size_t, index...>) {
  (f.template operator()<start + index>(), ...);
}

// Equivalent to f<start>(), f<start + 1>(), ... f<start + N - 1>().
template <size_t start, size_t N, class F>
void iterate_index_fold(const F& f) {
  // As of 2024-05-16, clang throws an error if the fold expression has more
  // than 256 terms. See also https://github.com/llvm/llvm-project/issues/48973.
  // We leave ourselves some headroom by splitting at 128 instead.
  constexpr size_t kMaxFoldExpressionSize = 128;
  if constexpr (N < kMaxFoldExpressionSize) {
    iterate_index_fold_base<start>(f, std::make_integer_sequence<size_t, N>());
  } else {
    iterate_index_fold<start, N / 2>(f);
    iterate_index_fold<start + N / 2, N - N / 2>(f);
  }
}

// index_folder<0> is specialized to terminate the recursion.
//
// index_folder<1> is specialized to correctly differentiate between
// reference and value states. Given that index_folder<0>::operator() always
// returns by reference, it's not safe to use it as recursion termination.
//
// index_folder<2-9> are specialized for efficiency. They let us elide
// copies of the state. Number 9 is chosen arbitrary and can be increased
// if needed. iterate_index<N> copies the initial state once and then
// does floor((N - 1) / M) extra copies, where M is the maximum index for which
// index_folder is specialized (currently, 9).

template <::size_t N>
struct index_folder {
  template <::size_t I, class F, class S>
  auto operator()(const F& f, S&& s) const
      -> decltype(index_folder<N - 9>().template operator()<I + 9>(
          f,
          f.template operator()<I + 8>(
              f.template operator()<I + 7>(f.template operator()<I + 6>(
                  f.template operator()<I + 5>(f.template operator()<I + 4>(
                      f.template operator()<I + 3>(f.template operator()<I + 2>(
                          f.template operator()<I + 1>(f.template operator()<I>(
                              ::std::forward<S>(s)))))))))))) {
    return index_folder<N - 9>().template operator()<I + 9>(
        f,
        f.template operator()<I + 8>(f.template operator()<I + 7>(
            f.template operator()<I + 6>(f.template operator()<I + 5>(
                f.template operator()<I + 4>(f.template operator()<I + 3>(
                    f.template operator()<I + 2>(f.template operator()<I + 1>(
                        f.template operator()<I>(::std::forward<S>(s)))))))))));
  }
};

template <>
struct index_folder<0> {
  template <::size_t I, class F, class S>
  S&& operator()(const F& f, S&& s) const {
    return ::std::forward<S>(s);
  }
};

template <>
struct index_folder<1> {
  template <::size_t I, class F, class S>
  auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<I>(::std::forward<S>(s))) {
    return f.template operator()<I>(::std::forward<S>(s));
  }
};

template <>
struct index_folder<2> {
  template <::size_t I, class F, class S>
  auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<I + 1>(
          f.template operator()<I>(::std::forward<S>(s)))) {
    return f.template operator()<I + 1>(
        f.template operator()<I>(::std::forward<S>(s)));
  }
};

template <>
struct index_folder<3> {
  template <::size_t I, class F, class S>
  auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<I + 2>(f.template operator()<I + 1>(
          f.template operator()<I>(::std::forward<S>(s))))) {
    return f.template operator()<I + 2>(f.template operator()<I + 1>(
        f.template operator()<I>(::std::forward<S>(s))));
  }
};

template <>
struct index_folder<4> {
  template <::size_t I, class F, class S>
  auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<I + 3>(
          f.template operator()<I + 2>(f.template operator()<I + 1>(
              f.template operator()<I>(::std::forward<S>(s)))))) {
    return f.template operator()<I + 3>(
        f.template operator()<I + 2>(f.template operator()<I + 1>(
            f.template operator()<I>(::std::forward<S>(s)))));
  }
};

template <>
struct index_folder<5> {
  template <::size_t I, class F, class S>
  auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<I + 4>(f.template operator()<I + 3>(
          f.template operator()<I + 2>(f.template operator()<I + 1>(
              f.template operator()<I>(::std::forward<S>(s))))))) {
    return f.template operator()<I + 4>(f.template operator()<I + 3>(
        f.template operator()<I + 2>(f.template operator()<I + 1>(
            f.template operator()<I>(::std::forward<S>(s))))));
  }
};

template <>
struct index_folder<6> {
  template <::size_t I, class F, class S>
  auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<I + 5>(
          f.template operator()<I + 4>(f.template operator()<I + 3>(
              f.template operator()<I + 2>(f.template operator()<I + 1>(
                  f.template operator()<I>(::std::forward<S>(s)))))))) {
    return f.template operator()<I + 5>(
        f.template operator()<I + 4>(f.template operator()<I + 3>(
            f.template operator()<I + 2>(f.template operator()<I + 1>(
                f.template operator()<I>(::std::forward<S>(s)))))));
  }
};

template <>
struct index_folder<7> {
  template <::size_t I, class F, class S>
  auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<I + 6>(f.template operator()<I + 5>(
          f.template operator()<I + 4>(f.template operator()<I + 3>(
              f.template operator()<I + 2>(f.template operator()<I + 1>(
                  f.template operator()<I>(::std::forward<S>(s))))))))) {
    return f.template operator()<I + 6>(f.template operator()<I + 5>(
        f.template operator()<I + 4>(f.template operator()<I + 3>(
            f.template operator()<I + 2>(f.template operator()<I + 1>(
                f.template operator()<I>(::std::forward<S>(s))))))));
  }
};

template <>
struct index_folder<8> {
  template <::size_t I, class F, class S>
  auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<I + 7>(
          f.template operator()<I + 6>(f.template operator()<I + 5>(
              f.template operator()<I + 4>(f.template operator()<I + 3>(
                  f.template operator()<I + 2>(f.template operator()<I + 1>(
                      f.template operator()<I>(::std::forward<S>(s)))))))))) {
    return f.template operator()<I + 7>(
        f.template operator()<I + 6>(f.template operator()<I + 5>(
            f.template operator()<I + 4>(f.template operator()<I + 3>(
                f.template operator()<I + 2>(f.template operator()<I + 1>(
                    f.template operator()<I>(::std::forward<S>(s)))))))));
  }
};

template <>
struct index_folder<9> {
  template <::size_t I, class F, class S>
  auto operator()(const F& f, S&& s) const
      -> decltype(f.template operator()<I + 8>(f.template operator()<I + 7>(
          f.template operator()<I + 6>(f.template operator()<I + 5>(
              f.template operator()<I + 4>(f.template operator()<I + 3>(
                  f.template operator()<I + 2>(f.template operator()<I + 1>(
                      f.template operator()<I>(::std::forward<S>(s))))))))))) {
    return f.template operator()<I + 8>(f.template operator()<I + 7>(
        f.template operator()<I + 6>(f.template operator()<I + 5>(
            f.template operator()<I + 4>(f.template operator()<I + 3>(
                f.template operator()<I + 2>(f.template operator()<I + 1>(
                    f.template operator()<I>(::std::forward<S>(s))))))))));
  }
};

// See comment for index_folder above, for folder is very similar.
//
// The only reason folder with all its specializations exists is
// to make iterate() efficient w.r.t. state copies. If that wasn't a concern,
// iterate() could be trivially implemented on top of iterate_index().

template <::size_t N>
struct folder {
  template <class F, class S>
  auto operator()(const F& f, S&& s) const -> decltype(folder<N - 9>()(
      f, f(f(f(f(f(f(f(f(f(::std::forward<S>(s)))))))))))) {
    return folder<N - 9>()(f, f(f(f(f(f(f(f(f(f(::std::forward<S>(s)))))))))));
  }
};

template <>
struct folder<0> {
  template <class F, class S>
  S&& operator()(const F& f, S&& s) const {
    return ::std::forward<S>(s);
  }
};

template <>
struct folder<1> {
  template <class F, class S>
  auto operator()(const F& f, S&& s) const
      -> decltype(f(::std::forward<S>(s))) {
    return f(::std::forward<S>(s));
  }
};

template <>
struct folder<2> {
  template <class F, class S>
  auto operator()(const F& f, S&& s) const
      -> decltype(f(f(::std::forward<S>(s)))) {
    return f(f(::std::forward<S>(s)));
  }
};

template <>
struct folder<3> {
  template <class F, class S>
  auto operator()(const F& f, S&& s) const
      -> decltype(f(f(f(::std::forward<S>(s))))) {
    return f(f(f(::std::forward<S>(s))));
  }
};

template <>
struct folder<4> {
  template <class F, class S>
  auto operator()(const F& f, S&& s) const
      -> decltype(f(f(f(f(::std::forward<S>(s)))))) {
    return f(f(f(f(::std::forward<S>(s)))));
  }
};

template <>
struct folder<5> {
  template <class F, class S>
  auto operator()(const F& f, S&& s) const
      -> decltype(f(f(f(f(f(::std::forward<S>(s))))))) {
    return f(f(f(f(f(::std::forward<S>(s))))));
  }
};

template <>
struct folder<6> {
  template <class F, class S>
  auto operator()(const F& f, S&& s) const
      -> decltype(f(f(f(f(f(f(::std::forward<S>(s)))))))) {
    return f(f(f(f(f(f(::std::forward<S>(s)))))));
  }
};

template <>
struct folder<7> {
  template <class F, class S>
  auto operator()(const F& f, S&& s) const
      -> decltype(f(f(f(f(f(f(f(::std::forward<S>(s))))))))) {
    return f(f(f(f(f(f(f(::std::forward<S>(s))))))));
  }
};

template <>
struct folder<8> {
  template <class F, class S>
  auto operator()(const F& f, S&& s) const
      -> decltype(f(f(f(f(f(f(f(f(::std::forward<S>(s)))))))))) {
    return f(f(f(f(f(f(f(f(::std::forward<S>(s)))))))));
  }
};

template <>
struct folder<9> {
  template <class F, class S>
  auto operator()(const F& f, S&& s) const
      -> decltype(f(f(f(f(f(f(f(f(f(::std::forward<S>(s))))))))))) {
    return f(f(f(f(f(f(f(f(f(::std::forward<S>(s))))))))));
  }
};

}  // namespace internal_iterate
}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_INTERNAL_ITERATE_H_
