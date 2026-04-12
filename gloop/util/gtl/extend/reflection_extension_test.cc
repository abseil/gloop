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

#include "gloop/util/gtl/extend/reflection_extension.h"

#include <string>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gloop/gloop_test.h"
#include "gloop/util/gtl/extend/equality.h"
#include "gloop/util/gtl/extend/extend.h"
#include "gloop/util/gtl/extend/internal/reflection.h"

namespace gtl {
namespace internal_extend {

// Simply exposes the private GetFieldNames() static method.
template <typename T>
struct ReflectionTestingExtension
    : public Extension<ReflectionTestingExtension, T> {
 private:
  using deps = void(ReflectionExtension<T>);

 public:
  static const auto& GetFieldNames(const T& value) {
    // We can access this because ReflectionTestingExtension is a friend.
    return ReflectionExtension<T>::GetFieldNames(value);
  }
};

}  // namespace internal_extend

namespace {

template <typename T>
using PublicReflection = internal_extend::ReflectionTestingExtension<T>;

template <typename T, typename U>
struct FooTemplate
    : Extend<FooTemplate<T, U>>::template With<PublicReflection> {
  T t;
  U u;
};

struct Simple : Extend<Simple>::With<PublicReflection> {
  int some_int;
  const double some_double;
};

struct Initialized : Extend<Initialized>::With<PublicReflection> {
  int some_int = 4 + 5;
  std::string some_string = ("foo");
};

struct Templated : Extend<Templated>::With<PublicReflection> {
  FooTemplate<FooTemplate<int, std::string>, bool> footemplate;
};

struct InlineType : Extend<InlineType>::With<PublicReflection> {
  enum class SomeEnum { kOne, kTwo } some_enum = SomeEnum::kOne;
  class Foo {
   private:
    std::string x_;
    int y_;  // NOLINT

   public:
  } foo;
};

struct Empty : Extend<Empty>::With<PublicReflection> {};

struct MultipleExtends
    : gtl::Extend<MultipleExtends>::With<EqualityExtension, PublicReflection> {
  int x;
  int y;
};

// When running with GTL_EXTEND_INTERNAL_TURN_OFF_FIELD_NAMES_FOR_TEST we won't
// have access to field names. So we stub out field name specific assertions.

auto FieldNamesAre(absl::Span<const absl::string_view> expected_field_names) {
#if GTL_EXTEND_PARSE_FIELD_NAMES
  return ::testing::FieldsAre(::testing::ElementsAreArray(expected_field_names),
                              /*success=*/true);
#else
  return ::testing::FieldsAre(/*success=*/false);
#endif
}

TEST(ReflectFieldNames, Simple) {
  auto fields = PublicReflection<Simple>::GetFieldNames(Simple{{}, 4, 5.0});
  EXPECT_THAT(fields, FieldNamesAre({"some_int", "some_double"}));
  static_assert(decltype(fields)::kFieldCount == 2);
}

TEST(ReflectFieldNames, Initialized) {
  auto fields = PublicReflection<Initialized>::GetFieldNames(Initialized());
  EXPECT_THAT(fields, FieldNamesAre({"some_int", "some_string"}));
  static_assert(decltype(fields)::kFieldCount == 2);
}

TEST(ReflectFieldNames, Templated) {
  auto fields = PublicReflection<Templated>::GetFieldNames(Templated());
  EXPECT_THAT(fields, FieldNamesAre({"footemplate"}));
  static_assert(decltype(fields)::kFieldCount == 1);
}

TEST(ReflectFieldNames, InlineType) {
  auto fields = PublicReflection<InlineType>::GetFieldNames(InlineType());
  EXPECT_THAT(fields, FieldNamesAre({"some_enum", "foo"}));
  static_assert(decltype(fields)::kFieldCount == 2);
}

TEST(ReflectFieldNames, Empty) {
  auto fields = PublicReflection<Empty>::GetFieldNames(Empty());
  EXPECT_THAT(fields, FieldNamesAre({}));
  static_assert(decltype(fields)::kFieldCount == 0);
}

TEST(ReflectFieldNames, MultipleExtensions) {
  auto fields =
      PublicReflection<MultipleExtends>::GetFieldNames(MultipleExtends());
  EXPECT_THAT(fields, FieldNamesAre({"x", "y"}));

  static_assert(decltype(fields)::kFieldCount == 2);
}

TEST(ReflectFieldNames, Const) {
  const Simple const_simple{{}, 4, 5.0};
  auto fields = PublicReflection<Simple>::GetFieldNames(const_simple);
  EXPECT_THAT(fields, FieldNamesAre({"some_int", "some_double"}));
  static_assert(decltype(fields)::kFieldCount == 2);
}

}  // namespace
}  // namespace gtl
