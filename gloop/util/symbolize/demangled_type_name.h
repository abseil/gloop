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

#ifndef THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_DEMANGLED_TYPE_NAME_H_
#define THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_DEMANGLED_TYPE_NAME_H_

#include <algorithm>
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
//
// *** WARNING ***
// This function is EXTREMELY compiler-dependent and should not impact business
// logic! Any usage of this function should validate expected names (e.g., via
// static_assert()) to ensure they resemble the expected format.
//
// Examples of variation across compilers:
//
// * util::DemangledTypeName<decltype(nullptr)>()
//   - Clang: "std::nullptr_t"
//   - GCC:   "std::nullptr_t"
//   - MSVC:  "nullptr"
//
// * util::DemangledTypeName<std::is_void<void>>()
//   - Clang: "std::is_void<void>"
//   - GCC:   "std::is_void<void>"
//   - MSVC:  "struct std::is_void<void>"
//
template <typename T>
constexpr absl::string_view DemangledTypeName();

//==============================================================================

namespace internal_demangled_type_name {

// Returns the compiler-internal "pretty name" for this function specialized
// with the template argument(s).
//
// There are no guarantees on the actual contents of the returned string, except
// that the portion of the string corresponding to the template argument is a
// deterministic and independent substring from the rest of the string.
template <class... KeepThisName>
constexpr auto GetPrettyTypeName() {
#if defined(_MSC_VER) && !defined(__clang__)
  return __FUNCSIG__;
#else
  return __PRETTY_FUNCTION__;
#endif
}

// Computes the common prefixes and suffixes of the samples, and strips off
// characters from the corpus of corresponding lengths.
//
// NOT a general-purpose function; do not copy/reuse this elsewhere.
constexpr absl::string_view DiffNames(absl::string_view corpus,
                                      absl::string_view sample1,
                                      absl::string_view sample2) {
  const size_t min_size = (std::min)(sample1.size(), sample2.size());

  const auto sample1_end = sample1.end();
  const auto sample2_end = sample2.end();

  size_t i = 0;  // positive
  for (; i < min_size; ++i) {
    if (sample1[i] != sample2[i]) {
      break;
    }
  }

  ptrdiff_t j = -1;  // negative, to make inner loop fast in constexpr
  ptrdiff_t j_end = -static_cast<ptrdiff_t>(min_size) - 1;
  for (; j > j_end; --j) {
    if (sample1_end[j] != sample2_end[j]) {
      break;
    }
  }

  return corpus.substr(i, corpus.size() + 1 + j - i);
}

}  // namespace internal_demangled_type_name

template <typename T>
constexpr absl::string_view DemangledTypeName() {
  constexpr absl::string_view kResult = internal_demangled_type_name::DiffNames(
      internal_demangled_type_name::GetPrettyTypeName<T>(),
      internal_demangled_type_name::GetPrettyTypeName<>(),
      internal_demangled_type_name::GetPrettyTypeName<int>());
  // We keep the result in its own constexpr variable so that the result is
  // computed once at compile time, even if this function is evaluated at run
  // time.
  return kResult;
}

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_DEMANGLED_TYPE_NAME_H_
