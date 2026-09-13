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

#ifndef THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_DEMANGLED_TYPE_NAME_H_
#define THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_DEMANGLED_TYPE_NAME_H_

#include <cstddef>

#include "absl/strings/string_view.h"

namespace util {

// Returns a demangled name of T (there can be several possible demangled
// representations for some types).
// Because it does this purely at compile time, this saves machine code, cycles
// and does not pollute the CPU instruction cache.
//
// Warning: This does not always align with ::util::Demangle, e.g. if there are
// default type parameters which are at the default value, they will not be part
// of the returned string.
// There are also differences in the handling of types in the C++ standard
// library.
//
// clang-format off
// NOLINTBEGIN
/*

Examples (the output can change over time for both functions):

namespace a {
template <typename T = int, T I = 2>
struct Struct {
  static constexpr T i = I;
};
}  // namespace a


T                    | util::DemangledTypeName<T>()     | util::Demangle(typeid(T).name())
---------------------|----------------------------------|-------------------------------------
a::Struct<>          | a::Struct<>                      | a::Struct<int, 2>
a::Struct<int, 2>    | a::Struct<>                      | a::Struct<int, 2>
a::Struct<char, 2>   | a::Struct<char>                  | a::Struct<char, (char)2>
a::Struct<char, 1>   | a::Struct<char, '\\x01'>         | a::Struct<char, (char)1>
std::string          | std::string                      | std::__u::basic_string<char, std::__u::char_traits<char>, std::__u::allocator<char>>
std::vector<int64_t> | std::vector<long>                | std::__u::vector<long, std::__u::allocator<long>>

*/
// NOLINTEND
// clang-format on

template <typename T>
consteval absl::string_view DemangledTypeName() {
  constexpr absl::string_view fun = __PRETTY_FUNCTION__;
  constexpr absl::string_view type_indicator = "DemangledTypeName() [T = ";
  constexpr size_t start = fun.find(type_indicator);
  static_assert(
      start != absl::string_view::npos,
      "Unexpected format, this was probably caused by a compiler change.");
  absl::string_view result = fun;
  result.remove_prefix(start + type_indicator.size());
  static_assert(
      fun.ends_with("]"),
      "Unexpected format, this was probably caused by a compiler change.");
  result.remove_suffix(1);
  return result;
}

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_DEMANGLED_TYPE_NAME_H_
