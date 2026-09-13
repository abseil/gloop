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

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPILE_STRING_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPILE_STRING_H_

#include <cstddef>

#include "absl/utility/utility.h"

namespace util {
namespace tuple {

// A compile_string is a sequence-char templated struct that can be used as a
// tag-like type for defining overloads, template specializations or
// metaprogramming techniques.
//
// It has a static member char array `value` containing the sequence of
// characters followed by a null-character.
//
//   using hello_t = compile_string<'h', 'e', 'l', 'l', 'o'>;
//   hello_t hello;
//   std::cout << hello.value;  // Outputs "hello"
//
// compile_string objects can be created with the macro TUPLE_COMPILE_STRING.
//
//   auto hello = TUPLE_COMPILE_STRING("hello");
//   using hello_t = decltype(TUPLE_COMPILE_STRING("hello"));
template <char... Chars>
struct compile_string {
  static constexpr char value[sizeof...(Chars) + 1] = {Chars..., 0};
};

template <char... Chars>
constexpr char compile_string<Chars...>::value[sizeof...(Chars) + 1];

// Macro TUPLE_COMPILE_STRING allows to easily create objects of compile_string
// template instantiations.
//
//   auto hello = TUPLE_COMPILE_STRING("hello");
//   using hello_t = decltype(TUPLE_COMPILE_STRING("hello"));
//   static_assert(std::is_same<hello_t,
//                              compile_string<'h','e','l','l','o'>>{}, "");
//   CHECK_EQ(hello_t::value, string("hello"));
//
// The compile_string's created with TUPLE_COMPILE_STRING cannot have more than
// 80 characters.
#define TUPLE_COMPILE_STRING(contents) \
  (TUPLE_INTERNAL_COMPILE_STRING(contents){})

///////////////////////////////////////////////////////////////////////////////
//             EVERYTHING BELOW THIS LINE IS IMPLEMENTATION DETAIL           //
///////////////////////////////////////////////////////////////////////////////

namespace internal_compile_string {

template <class Str, class Seq>
struct string_builder;

template <class Str, ::size_t... Indices>
struct string_builder<Str, absl::index_sequence<Indices...>> {
  using type = compile_string<Str::value[Indices]...>;
};

// make_string is used to generate an error at compile time if a struct
// field is longer than the maximum supported length.
template <::size_t Size, char... Chars>
struct make_string {
  static_assert(Size < 81,
                "Struct field's name exceeds the maximum supported length");
  static constexpr char value[sizeof...(Chars)] = {Chars...};
};

template <::size_t Size, char... Chars>
constexpr char make_string<Size, Chars...>::value[sizeof...(Chars)];

template <::size_t N>
constexpr inline char char_at(const char (&s)[N], ::size_t idx) {
  return idx < N ? s[idx] : '\0';
}

template <::size_t N>
constexpr inline ::size_t char_size(const char (& /*s*/)[N]) {
  return N - 1;
}

}  // namespace internal_compile_string

#define TUPLE_INTERNAL_COMPILE_STRING(S)                           \
  typename ::util::tuple::internal_compile_string::string_builder< \
      ::util::tuple::internal_compile_string::make_string<         \
          ::util::tuple::internal_compile_string::char_size(S),    \
          ::util::tuple::internal_compile_string::char_at(S, 0),   \
          ::util::tuple::internal_compile_string::char_at(S, 1),   \
          ::util::tuple::internal_compile_string::char_at(S, 2),   \
          ::util::tuple::internal_compile_string::char_at(S, 3),   \
          ::util::tuple::internal_compile_string::char_at(S, 4),   \
          ::util::tuple::internal_compile_string::char_at(S, 5),   \
          ::util::tuple::internal_compile_string::char_at(S, 6),   \
          ::util::tuple::internal_compile_string::char_at(S, 7),   \
          ::util::tuple::internal_compile_string::char_at(S, 8),   \
          ::util::tuple::internal_compile_string::char_at(S, 9),   \
          ::util::tuple::internal_compile_string::char_at(S, 10),  \
          ::util::tuple::internal_compile_string::char_at(S, 11),  \
          ::util::tuple::internal_compile_string::char_at(S, 12),  \
          ::util::tuple::internal_compile_string::char_at(S, 13),  \
          ::util::tuple::internal_compile_string::char_at(S, 14),  \
          ::util::tuple::internal_compile_string::char_at(S, 15),  \
          ::util::tuple::internal_compile_string::char_at(S, 16),  \
          ::util::tuple::internal_compile_string::char_at(S, 17),  \
          ::util::tuple::internal_compile_string::char_at(S, 18),  \
          ::util::tuple::internal_compile_string::char_at(S, 19),  \
          ::util::tuple::internal_compile_string::char_at(S, 20),  \
          ::util::tuple::internal_compile_string::char_at(S, 21),  \
          ::util::tuple::internal_compile_string::char_at(S, 22),  \
          ::util::tuple::internal_compile_string::char_at(S, 23),  \
          ::util::tuple::internal_compile_string::char_at(S, 24),  \
          ::util::tuple::internal_compile_string::char_at(S, 25),  \
          ::util::tuple::internal_compile_string::char_at(S, 26),  \
          ::util::tuple::internal_compile_string::char_at(S, 27),  \
          ::util::tuple::internal_compile_string::char_at(S, 28),  \
          ::util::tuple::internal_compile_string::char_at(S, 29),  \
          ::util::tuple::internal_compile_string::char_at(S, 30),  \
          ::util::tuple::internal_compile_string::char_at(S, 31),  \
          ::util::tuple::internal_compile_string::char_at(S, 32),  \
          ::util::tuple::internal_compile_string::char_at(S, 33),  \
          ::util::tuple::internal_compile_string::char_at(S, 34),  \
          ::util::tuple::internal_compile_string::char_at(S, 35),  \
          ::util::tuple::internal_compile_string::char_at(S, 36),  \
          ::util::tuple::internal_compile_string::char_at(S, 37),  \
          ::util::tuple::internal_compile_string::char_at(S, 38),  \
          ::util::tuple::internal_compile_string::char_at(S, 39),  \
          ::util::tuple::internal_compile_string::char_at(S, 40),  \
          ::util::tuple::internal_compile_string::char_at(S, 41),  \
          ::util::tuple::internal_compile_string::char_at(S, 42),  \
          ::util::tuple::internal_compile_string::char_at(S, 43),  \
          ::util::tuple::internal_compile_string::char_at(S, 44),  \
          ::util::tuple::internal_compile_string::char_at(S, 45),  \
          ::util::tuple::internal_compile_string::char_at(S, 46),  \
          ::util::tuple::internal_compile_string::char_at(S, 47),  \
          ::util::tuple::internal_compile_string::char_at(S, 48),  \
          ::util::tuple::internal_compile_string::char_at(S, 49),  \
          ::util::tuple::internal_compile_string::char_at(S, 50),  \
          ::util::tuple::internal_compile_string::char_at(S, 51),  \
          ::util::tuple::internal_compile_string::char_at(S, 52),  \
          ::util::tuple::internal_compile_string::char_at(S, 53),  \
          ::util::tuple::internal_compile_string::char_at(S, 54),  \
          ::util::tuple::internal_compile_string::char_at(S, 55),  \
          ::util::tuple::internal_compile_string::char_at(S, 56),  \
          ::util::tuple::internal_compile_string::char_at(S, 57),  \
          ::util::tuple::internal_compile_string::char_at(S, 58),  \
          ::util::tuple::internal_compile_string::char_at(S, 59),  \
          ::util::tuple::internal_compile_string::char_at(S, 60),  \
          ::util::tuple::internal_compile_string::char_at(S, 61),  \
          ::util::tuple::internal_compile_string::char_at(S, 62),  \
          ::util::tuple::internal_compile_string::char_at(S, 63),  \
          ::util::tuple::internal_compile_string::char_at(S, 64),  \
          ::util::tuple::internal_compile_string::char_at(S, 65),  \
          ::util::tuple::internal_compile_string::char_at(S, 66),  \
          ::util::tuple::internal_compile_string::char_at(S, 67),  \
          ::util::tuple::internal_compile_string::char_at(S, 68),  \
          ::util::tuple::internal_compile_string::char_at(S, 69),  \
          ::util::tuple::internal_compile_string::char_at(S, 70),  \
          ::util::tuple::internal_compile_string::char_at(S, 71),  \
          ::util::tuple::internal_compile_string::char_at(S, 72),  \
          ::util::tuple::internal_compile_string::char_at(S, 73),  \
          ::util::tuple::internal_compile_string::char_at(S, 74),  \
          ::util::tuple::internal_compile_string::char_at(S, 75),  \
          ::util::tuple::internal_compile_string::char_at(S, 76),  \
          ::util::tuple::internal_compile_string::char_at(S, 77),  \
          ::util::tuple::internal_compile_string::char_at(S, 78),  \
          ::util::tuple::internal_compile_string::char_at(S, 79),  \
          ::util::tuple::internal_compile_string::char_at(S, 80)>, \
      ::absl::make_index_sequence<                                 \
          ::util::tuple::internal_compile_string::char_size(S)>>::type

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPILE_STRING_H_
