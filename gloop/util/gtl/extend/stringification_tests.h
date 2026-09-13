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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_STRINGIFICATION_TESTS_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_STRINGIFICATION_TESTS_H_

#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gloop/util/gtl/extend/equality.h"
#include "gloop/util/gtl/extend/extend.h"
#include "gloop/util/gtl/extend/internal/reflection.h"
#include "gtest/gtest.h"

namespace gtl {
namespace internal_extend {

struct FieldSpec {
  std::string name;
  std::variant<std::string, std::vector<FieldSpec>> value;
};

template <bool EnableNames>
void ConstructRegularExpressionImpl(absl::Span<const FieldSpec> specs,
                                    std::string& output) {
  absl::StrAppend(&output, R"({\s*)");

  absl::string_view separator = "";

  for (const auto& spec : specs) {
    absl::StrAppend(&output, std::exchange(separator, R"(,\s*)"));
    if constexpr (EnableNames) {
      absl::StrAppend(&output, spec.name, R"(\s*=\s*)");
    }
    std::visit(
        [&](const auto& value) {
          using type = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<type, std::string>) {
            absl::StrAppend(&output, value, R"(\s*)");
          } else {
            ConstructRegularExpressionImpl<EnableNames>(value, output);
          }
        },
        spec.value);
  }
  absl::StrAppend(&output, R"(}\s*)");
}

inline auto MatchesFieldSpec(absl::Span<const FieldSpec> specs) {
  std::string result;
#if GTL_EXTEND_PARSE_FIELD_NAMES
  ConstructRegularExpressionImpl<true>(specs, result);
#else
  ConstructRegularExpressionImpl<false>(specs, result);
#endif
  return ::testing::MatchesRegex(std::move(result));
}

constexpr bool MatchesUsingRE2() {
#ifdef GTEST_USES_RE2
  return true;
#else
  return false;
#endif
}

struct Streamer {
  template <typename T>
  std::string operator()(const T& val) {
    std::stringstream ss;
    ss << val;
    return ss.str();
  }
};

struct StrFormatter {
  template <typename T>
  std::string operator()(const T& val) {
    return absl::StrFormat("%v", val);
  }
};

struct StrCatter {
  template <typename T>
  std::string operator()(const T& val) {
    return absl::StrCat(val);
  }
};

template <template <typename> typename Extension>
struct OneField : gtl::Extend<OneField<Extension>>::template With<Extension> {
  int num;
};

template <template <typename> typename Extension>
struct ManyFields
    : gtl::Extend<ManyFields<Extension>>::template With<Extension> {
  int num = 3;
  bool b = true;
  std::string message = "hello";
};

template <template <typename> typename Ext>
struct Nested : gtl::Extend<Nested<Ext>>::template With<Ext> {
  int num = 3;
  ManyFields<Ext> fields;
};

template <typename T, template <typename> typename Extension>
struct Template
    : gtl::Extend<Template<T, Extension>>::template With<Extension> {
  T val;
};

template <template <typename> typename Extension>
struct MultipleExtends : gtl::Extend<MultipleExtends<Extension>>::template With<
                             Extension, gtl::EqualityExtension> {
  int x;
  int y;
};

}  // namespace internal_extend
}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_STRINGIFICATION_TESTS_H_
