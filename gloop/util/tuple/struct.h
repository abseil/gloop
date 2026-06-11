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

//
// NOTE: prefer gtl::Extend over TUPLE_DEFINE_STRUCT
//
// This library provides a handful of macros for defining structs with
// properties of tuples such as generic access to the fields, relational
// operators, operator<<, and hashing via AbslHashValue.
//
//                   DEFINING A STRUCT WITH OPERATORS
//
// Suppose we need to define a type that describes a person. The type needs to
// be usable as a key in std::map and with gunit macros such as EXPECT_EQ.
//
// We can either use std::tuple or define our own struct.
//
// 1. Using std::tuple as representation.
//
//   // The first field is the person's name, the second is age in years.
//   typedef std::tuple<std::string, int> Person;
//
// The pros of this approach is that it's short and easy to implement. By
// sticking with std::tuple we automatically get operator< and operator==. GUnit
// is capable of printing the content of std::tuple, thus Person can be used
// with EXPECT_EQ.
//
// The drawback of this approach is readability of the code that uses Person.
// get<0>(person) and get<1>(person) aren't descriptive. Type safety is also
// compromised because any std::tuple<std::string, int> can be used as Person
// and vice versa.
//
// 2. Using struct as representation.
//
//   struct Person {
//     std::string name;
//     int age = 0;
//
//
//     friend bool operator==(const Person& a, const Person& b) {
//       return a.name == b.name && a.age == b.age;
//     }
//
//     friend bool operator!=(const Person& a, const Person& b) {
//       return !(a == b);
//     }
//
//     friend ostream& operator<<(ostream& strm, const Person& person) {
//       return strm << "{" << person.name << ", " << person.age << "}";
//     }
//
//     template <typename H>
//     friend H AbslHashValue(H h, const Person& p) {
//       return H::combine(std::move(h), p.name, p.age);
//     }
//   };
//
// This version of Person is easier to use: expressions such as person.name and
// person.age are descriptive. Unfortunately, we had to write a lot of
// boilerplate and hopefully didn't introduce any bugs there. Future maintainers
// must not forget to extend all the operators each time they add a new field.
//
// 3. Using TUPLE_DEFINE_STRUCT macro.
//
// There is a third way that combines the succinctness of std::tuple and
// usability of user defined struct.
//
//   struct Person {
//     TUPLE_DEFINE_STRUCT(Person,
//                         (eq, ne, ostream, absl_hash),
//                         (std::string, name),
//                         (int, age, 0));
//   };
//
// The macro expands to the same code as the struct approach above (not
// literally but it has the same effect).
//
// When compiled in C++17 and clang, TUPLE_DEFINE_STRUCT statically asserts that
// it includes all members of the struct. If for some reason you need to have
// members defined outside the tuple representation, you can use
// TUPLE_DEFINE_STRUCT_PERMISSIVE, which disables the check.
//
// See the reference section below for the detailed explanation of
// TUPLE_DEFINE_STRUCT.
//
//               DEFINING OPERATORS FOR AN EXISTING STRUCT
//
// TUPLE_ADAPT_STRUCT is deprecated. See <link>.
//
// If for some reason you can't use TUPLE_DEFINE_STRUCT to define a struct,
// use macros TUPLE_ADAPT_STRUCT and TUPLE_DEFINE_OP in the namespace scope to
// define the operators you need.
//
// Hand-coded version:
//
//   struct Person {
//     std::string name;
//     int age = 0;
//   };
//
//   inline bool operator==(const Person& a, const Person& b) {
//     return a.name == b.name && a.age == b.age;
//   }
//
//   inline bool operator!=(const Person& a, const Person& b) {
//     return !(a == b);
//   }
//   inline ostream& operator<<(ostream& strm, const Person& person) {
//     return strm << "{" << person.name << ", " << person.age << "}";
//   }
//
//   template <typename H>
//   H AbslHashValue(H h, const Person& p) {
//     return H::combine(std::move(h), p.name, p.age);
//   }
//
// Version with macros:
//
//   struct Person {
//     std::string name;
//     int age = 0;
//   };
//
//   TUPLE_ADAPT_STRUCT(Person, name, age);
//   TUPLE_DEFINE_OP(Person, eq);
//   TUPLE_DEFINE_OP(Person, ne);
//   TUPLE_DEFINE_OP(Person, ostream);
//   TUPLE_DEFINE_OP(Person, absl_hash);
//
// TUPLE_ADAPT_STRUCT(Person, name, age) enables tuple-like access to the fields
// of Person: util::tuple::size<Person>::value is 2, util::tuple::get<0>(person)
// is a synonym for person.name, etc. TUPLE_DEFINE_OP macros below make use of
// the provided generic access to the fields to implement the requested
// operators.
//
// When compiled in C++17 and clang, TUPLE_ADAPT_STRUCT statically asserts that
// it includes all members in the correct sequence, if you need to change the
// sequence or the member components of the tuple representation, you can
// disable the check using TUPLE_ADAPT_STRUCT_PERMISSIVE.
//
// See the reference section below for the detailed explanation of
// TUPLE_ADAPT_STRUCT and TUPLE_DEFINE_OP.
//
//               GENERIC TUPLE-LIKE ACCESS TO STRUCT FIELDS
//
// Suppose we have a struct that contains text statistics.
//
//   struct TextStats {
//     double verbs;
//     double nouns;
//     double pronouns;
//     double adverbs;
//     double adjectives;
//     double prepositions;
//     double conjunctions;
//     double interjections;
//   };
//
// After parsing a poem and counting the words, we have the number of
// occurrences of verbs, nouns, etc. in the text.
//
// The task is to normalize all numbers by the total number of words.
//
// Hand-coded solution:
//
//   void Normalize(TextStats* stats) {
//     double words = stats->verbs +
//                    stats->nouns +
//                    stats->pronouns +
//                    stats->adverbs +
//                    stats->adjectives +
//                    stats->prepositions +
//                    stats->conjunctions +
//                    stats->interjections;
//     if (words > 0) {
//       stats->verbs /= words;
//       stats->nouns /= words;
//       stats->pronouns /= words;
//       stats->adverbs /= words;
//       stats->adjectives /= words;
//       stats->prepositions /= words;
//       stats->conjunctions /= words;
//       stats->interjections /= words;
//     }
//   }
//
// If TUPLE_DEFINE_STRUCT or TUPLE_ADAPT_STRUCT are applied to TextStats,
// it's possible to use util::tuple algorithms with TextStats as if it were
// an instance of tuple.
//
//   void Normalize(TextStats* stats) {
//     double words = util::tuple::accumulate(std::plus<double>(), *stats);
//     if (words > 0) {
//       struct Divide {
//         void operator()(double& dividend) const { dividend /= divisor; }
//         double divisor;
//       };
//       util::tuple::for_each(Divide{words}, *stats);
//     }
//   }
//
// In this example Divide::operator() works on doubles because all fields in
// TextStats are of type double. If there were fields of different types,
// operator() should have been overloaded or templated.
//
// Here's another example that verifies that a struct has no padding.
//
//   struct IoMappedStruct {
//     TUPLE_DEFINE_STRUCT(IoMappedStruct,
//                         (),
//                         (int32, a),
//                         (int32, b),
//                         (int64, c));
//   };
//
//   struct PlusSizeOf {
//     template <class T>
//     size_t operator()(size_t acc) const { return acc + sizeof(T); }
//   };
//
//   TEST(IoMappedStructTest, NoPadding) {
//     size_t all_fields_size =
//         util::tuple::accumulate<IoMappedStruct>(PlusSizeOf(), 0);
//     EXPECT_EQ(sizeof(IoMappedStruct), all_fields_size);
//   }
//
//                GENERIC COMPILE STRING ACCESS TO STRUCT FIELDS
//
// If TUPLE_DEFINE_STRUCT or TUPLE_ADAPT_STRUCT are applied to struct S, it's
// possible to use util::tuple::get_field with compile_string and S instances.
//
// Example:
//
//   struct IoMappedStruct {
//     TUPLE_DEFINE_STRUCT(IoMappedStruct,
//                         (),
//                         (int32, a),
//                         (int32, b),
//                         (int64, c));
//   };
//
//   TEST(IoMappedStructTest, GetField) {
//     IoMappedStructTest s{3, 4, 6};
//     EXPECT_EQ(get_field(TUPLE_FIELD(c), s), 6);
//     EXPECT_EQ(get_field(TUPLE_FIELD(b), s), 4);
//   }
//
// get_field(field, tuple) allows us to construct modifiers to tuple-structs
// that modify a set of fields defined in terms of function arguments or even
// template arguments. In particular, util::tuple::Build allows us to modify
// multiple fields in the same expression.
//
//   TEST(IoMappedStructTest, GetField) {
//     IoMappedStructTest s{3, 4, 6};
//     auto s2 = Build(std::move(s), TUPLE_FIELD(c), 1,
//                                   TUPLE_FIELD(b), -3);
//     EXPECT_THAT(s2, Tuple(3, -3, 1));
//   }
//
// Macro TUPLE_FIELD(field) <-> TUPLE_COMPILE_STRING("field").
//
//                               HASHING
//
// If you want to use a tuple as a key in a SwissTable container (e.g.
// absl::flat_hash_map), use the `eq` and `absl_hash` operators.
//
//   struct Person {
//     TUPLE_DEFINE_STRUCT(Person,
//                         (eq, ne, absl_hash),
//                         (std::string, name),
//                         (int, age));
//   };
//
//   absl::flat_hash_map<Person, JobTitle> employees;
//
// This absl_hash operator generates an AbslHashValue definition functionally
// equivalent to:
//
//   template <typename H>
//   H AbslHashValue(H h, const Person& person) {
//     H::combine(std::move(h), person.name, person.age);
//   };
//
//
//                               TESTING
//
// Structs with operator==, operator!= and operator<< can be used with EXPECT_EQ
// and EXPECT_NE macros.
//
//   struct Person {
//     TUPLE_DEFINE_STRUCT(Person,
//                         (eq, ne, ostream),
//                         (std::string, name),
//                         (int, birth_year));
//   };
//
//   Person Author(absl::string_view book);
//
//   TEST(CppPLTest, CorrectAuthor) {
//     EXPECT_EQ((Person{"Bjarne Stroustrup", 1950}),
//               Author("The C++ Programming Language"));
//   }
//
// GMock matcher util::tuple::testing::Tuple() defined in util/tuple/matchers.h
// works with all tuple-like types, including tuple-structs.
//
//   using ::util::tuple::testing::Tuple;
//   using ::testing::Lt;
//
//   TEST(CppPLTest, BornBeforeCpp98) {
//     EXPECT_THAT(Author("The C++ Programming Language"),
//                 Tuple("Bjarne Stroustrup", Lt(1998)));
//   }

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_STRUCT_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_STRUCT_H_

#include <cstddef>
#include <tuple>
#include <type_traits>

#include "absl/base/attributes.h"
#include "gloop/util/tuple/apply.h"  // IWYU pragma: keep
#include "gloop/util/tuple/bindings/bindings.h"
#include "gloop/util/tuple/compile_string.h"
#include "gloop/util/tuple/components/internal_preprocessor.h"  // IWYU pragma: export
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/relational.h"  // IWYU pragma: keep
#include "gloop/util/tuple/streamable.h"  // IWYU pragma: keep
#include "gloop/util/tuple/swap.h"        // IWYU pragma: keep

namespace util {
namespace tuple {

// Defines struct fields, enables tuple-like access to them and defines the
// requested operators on the struct.
//
// See file comments for details.
//
// Implementation note: the definition of get_tuple_tag() is provided to
// suppress the GCC's -Werror=non-template-friend. __attribute__((unused))
// suppresses the clang's -Wunneeded-internal-declaration.
#define TUPLE_DEFINE_STRUCT(S, OPS, ...) \
  TUPLE_DEFINE_STRUCT_INTERNAL_IMPL(true, S, OPS, __VA_ARGS__)

#define TUPLE_DEFINE_STRUCT_PERMISSIVE(S, OPS, ...) \
  TUPLE_DEFINE_STRUCT_INTERNAL_IMPL(true, S, OPS, __VA_ARGS__)

#define TUPLE_DEFINE_STRUCT_INTERNAL_IMPL(permissive, S, OPS, ...)            \
  TUPLE_INTERNAL_FOR_EACH(                                                    \
      TUPLE_STRUCT_INTERNAL_DEFINE_OP,                                        \
      (friend, TUPLE_INTERNAL_PARENTHESIZE(S), (__VA_ARGS__)), OPS)           \
  TUPLE_INTERNAL_FOR_EACH(TUPLE_STRUCT_INTERNAL_DEFINE_STRUCT_FIELD, ~,       \
                          (__VA_ARGS__))                                      \
  friend ::util::tuple::internal_struct::struct_tag<                          \
      permissive, TUPLE_INTERNAL_UNPARENTHESIZE(S) TUPLE_INTERNAL_FOR_EACH(   \
                      TUPLE_STRUCT_INTERNAL_MAKE_STRUCT_FIELD_2,              \
                      TUPLE_INTERNAL_PARENTHESIZE(S), (__VA_ARGS__))>         \
  get_tuple_tag [[maybe_unused]] (const TUPLE_INTERNAL_UNPARENTHESIZE(S) &) { \
    ::util::tuple::internal_struct::undefined();                              \
  }                                                                           \
  static_assert(true, "")

// Enables tuple-like access to struct S.
//
// See file comments for details.
//
// Implementation note: the definition of get_tuple_tag() is provided to
// suppress the GCC's -Werror=non-template-friend.
#define TUPLE_ADAPT_STRUCT(S, ...) \
  TUPLE_ADAPT_STRUCT_INTERNAL_IMPL(true, S, __VA_ARGS__)

#define TUPLE_ADAPT_STRUCT_PERMISSIVE(S, ...) \
  TUPLE_ADAPT_STRUCT_INTERNAL_IMPL(true, S, __VA_ARGS__)

#define TUPLE_ADAPT_STRUCT_INTERNAL_IMPL(permissive, S, ...)                  \
  inline ::util::tuple::internal_struct::struct_tag<                          \
      permissive, TUPLE_INTERNAL_UNPARENTHESIZE(S) TUPLE_INTERNAL_FOR_EACH(   \
                      TUPLE_STRUCT_INTERNAL_MAKE_STRUCT_FIELD,                \
                      TUPLE_INTERNAL_PARENTHESIZE(S), (__VA_ARGS__))>         \
  get_tuple_tag [[maybe_unused]] (const TUPLE_INTERNAL_UNPARENTHESIZE(S) &) { \
    ::util::tuple::internal_struct::undefined();                              \
  }                                                                           \
  static_assert(true, "")

// Defines an operator for the tuple-like type S.
//
// See file comments for details.
#define TUPLE_DEFINE_OP(...)                                                \
  TUPLE_DEFINE_OP_INTERNAL_OVERLOAD(__VA_ARGS__,                            \
                                    TUPLE_DEFINE_OP_INTERNAL_WITH_MODIFIER, \
                                    TUPLE_DEFINE_OP_INTERNAL_SANS_MODIFIER) \
  (__VA_ARGS__)

#define TUPLE_DEFINE_OP_INTERNAL_OVERLOAD(_1, _2, _3, NAME, ...) NAME
#define TUPLE_DEFINE_OP_INTERNAL_WITH_MODIFIER(M, S, OP)                       \
  TUPLE_STRUCT_INTERNAL_DEFINE_OP((M, TUPLE_INTERNAL_PARENTHESIZE(S), ()), OP) \
  static_assert(true, "")
#define TUPLE_DEFINE_OP_INTERNAL_SANS_MODIFIER(S, OP)                         \
  TUPLE_STRUCT_INTERNAL_DEFINE_OP((, TUPLE_INTERNAL_PARENTHESIZE(S), ()), OP) \
  static_assert(true, "")

#define TUPLE_FIELD(field) TUPLE_COMPILE_STRING(TUPLE_INTERNAL_STRINGIZE(field))

///////////////////////////////////////////////////////////////////////////////
//             EVERYTHING BELOW THIS LINE IS IMPLEMENTATION DETAIL           //
///////////////////////////////////////////////////////////////////////////////

namespace internal_struct {

// Describes a single field in a struct. Name must be an instance of
// compile_string.
//
// For example, given this struct:
//
//   struct Foo {
//     int bar;
//   };
//
// Its field can be described with:
//
//   struct_field<int, int Foo::*, &Foo::bar, compile_string<'b', 'a', 'r'>>
//
template <class Field, class MemPtr, MemPtr Ptr, class Name>
struct struct_field {
  typedef Field type;
  static constexpr MemPtr ptr = Ptr;
  typedef Name name;
};

template <class Field, class MemPtr, MemPtr Ptr, class Name>
constexpr MemPtr struct_field<Field, MemPtr, Ptr, Name>::ptr;

// Encodes all necessary information about struct S to enable tuple-like
// access to it.
template <bool permissive, class S, class... Fields>
struct struct_tag {};

// This function isn't defined. If you get a linker error because of this,
// you are somehow calling it even though you shouldn't.
void undefined() __attribute__((__noreturn__));

// When a struct has a field of type T and ctor capability is requested, the
// generated constructor will have an argument of type param_type<T>::type.
template <class T>
struct param_type {
  using type =
      typename std::conditional<std::is_reference<T>::value, T,
                                typename std::remove_cv<T>::type>::type;
};

struct HashCombine {
  template <typename H, typename... Ts>
  H operator()(H h, const Ts&... ts) const {
    return H::combine(std::move(h), ts...);
  }
};

template <class Map, class Element, class T>
constexpr auto get_field(T&& t)
    -> decltype(tuple::get<internal_intrinsics::index_of<Element, Map>{}>(
        std::forward<T>(t))) {
  return tuple::get<internal_intrinsics::index_of<Element, Map>{}>(
      std::forward<T>(t));
}

}  // namespace internal_struct

namespace internal_struct_adl_barrier {

template <class T, char... Chars>
constexpr auto get_field(compile_string<Chars...>, T&& t)
    -> decltype(internal_struct::get_field<
                typename intrinsics<typename tag<T>::type>::CompileStringMap,
                compile_string<Chars...>>(std::forward<T>(t))) {
  return internal_struct::get_field<
      typename intrinsics<typename tag<T>::type>::CompileStringMap,
      compile_string<Chars...>>(std::forward<T>(t));
}

}  // namespace internal_struct_adl_barrier

namespace internal_struct {

template <class Struct>
constexpr void Build(Struct* s) {}

template <class Struct, class Field, class Expr, class... Ts>
constexpr void Build(Struct* s, Field f, Expr&& expr, Ts&&... ts) {
  internal_struct_adl_barrier::get_field(f, *s) = std::forward<Expr>(expr);
  internal_struct::Build(s, std::forward<Ts>(ts)...);
}

template <class S, class T>
constexpr bool CheckTypes(std::true_type) {
  return true;
}

template <class S, class T>
constexpr bool CheckTypes(std::false_type) {
  using bindings_t =
      bindings::bindings_traits_with_size<S, std::tuple_size<T>::value>;
  return !bindings_t::has_bindings ||
         std::is_same_v<T, typename bindings_t::field_types>;
}

}  // namespace internal_struct

namespace internal_struct_adl_barrier {

template <class Struct, class... Ts>
constexpr Struct Build(Struct s, Ts&&... ts) {
  internal_struct::Build(&s, std::forward<Ts>(ts)...);
  return s;
}

}  // namespace internal_struct_adl_barrier

using namespace internal_struct_adl_barrier;

template <bool permissive, class S, class... Fields>
struct intrinsics<internal_struct::struct_tag<permissive, S, Fields...>> {
  using has_all_elements = std::true_type;

  template <class... T>
  struct assemble {
    static_assert(::std::is_same<::std::tuple<T...>,
                                 ::std::tuple<typename Fields::type...>>::value,
                  "Type mismatch");
    typedef S type;
  };

  template <::size_t N, class T>
  struct element : ::std::tuple_element<N, ::std::tuple<Fields...>>::type {
    static_assert(::std::is_base_of<S, T>::value, "Type mismatch");
  };

  template <class T>
  struct size : ::std::integral_constant<::size_t, sizeof...(Fields)> {};

  template <::size_t N, class T>
  static constexpr auto get(T&& t)
      -> decltype(::std::forward<T>(t).*::util::tuple::element<N, T>::ptr) {
    static_assert(::std::is_base_of<S, typename ::std::decay<T>::type>::value,
                  "Type mismatch");
    static_assert(
        permissive ||
        internal_struct::CheckTypes<S, ::std::tuple<typename Fields::type...>>(
            std::integral_constant<bool, permissive>{}));
    return ::std::forward<T>(t).*::util::tuple::element<N, T>::ptr;
  }

  template <::size_t N, class T>
  static constexpr const char* name() {
    return util::tuple::element<N, T>::name::value;
  }

  using CompileStringMap = std::tuple<typename Fields::name...>;
};

// The first argument is a triplet: (M, S, FIELDS). M is short for modifier.
// It's either nothing or "friend". S is the struct type. FIELDS is a tuple
// of struct fields, e.g., ((int, x), (double, y, 3.5)).
#define TUPLE_STRUCT_INTERNAL_DEFINE_OP(T, OP) \
  TUPLE_INTERNAL_CAT(TUPLE_STRUCT_INTERNAL_DEFINE_OP_, OP) T

#define TUPLE_STRUCT_INTERNAL_DEFINE_OP_lt(M, S, FIELDS)        \
  M inline constexpr bool operator<(                            \
      const TUPLE_INTERNAL_REMOVE_PARENS(S) & _tuple_op_lhs_,   \
      const TUPLE_INTERNAL_REMOVE_PARENS(S) & _tuple_op_rhs_) { \
    return ::util::tuple::less(_tuple_op_lhs_, _tuple_op_rhs_); \
  }

#define TUPLE_STRUCT_INTERNAL_DEFINE_OP_gt(M, S, FIELDS)           \
  M inline constexpr bool operator>(                               \
      const TUPLE_INTERNAL_REMOVE_PARENS(S) & _tuple_op_lhs_,      \
      const TUPLE_INTERNAL_REMOVE_PARENS(S) & _tuple_op_rhs_) {    \
    return ::util::tuple::greater(_tuple_op_lhs_, _tuple_op_rhs_); \
  }

#define TUPLE_STRUCT_INTERNAL_DEFINE_OP_le(M, S, FIELDS)              \
  M inline constexpr bool operator<=(                                 \
      const TUPLE_INTERNAL_REMOVE_PARENS(S) & _tuple_op_lhs_,         \
      const TUPLE_INTERNAL_REMOVE_PARENS(S) & _tuple_op_rhs_) {       \
    return ::util::tuple::less_equal(_tuple_op_lhs_, _tuple_op_rhs_); \
  }

#define TUPLE_STRUCT_INTERNAL_DEFINE_OP_ge(M, S, FIELDS)                 \
  M inline constexpr bool operator>=(                                    \
      const TUPLE_INTERNAL_REMOVE_PARENS(S) & _tuple_op_lhs_,            \
      const TUPLE_INTERNAL_REMOVE_PARENS(S) & _tuple_op_rhs_) {          \
    return ::util::tuple::greater_equal(_tuple_op_lhs_, _tuple_op_rhs_); \
  }

#define TUPLE_STRUCT_INTERNAL_DEFINE_OP_eq(M, S, FIELDS)         \
  M inline constexpr bool operator==(                            \
      const TUPLE_INTERNAL_REMOVE_PARENS(S) & _tuple_op_lhs_,    \
      const TUPLE_INTERNAL_REMOVE_PARENS(S) & _tuple_op_rhs_) {  \
    return ::util::tuple::equal(_tuple_op_lhs_, _tuple_op_rhs_); \
  }

#define TUPLE_STRUCT_INTERNAL_DEFINE_OP_ne(M, S, FIELDS)             \
  M inline constexpr bool operator!=(                                \
      const TUPLE_INTERNAL_REMOVE_PARENS(S) & _tuple_op_lhs_,        \
      const TUPLE_INTERNAL_REMOVE_PARENS(S) & _tuple_op_rhs_) {      \
    return ::util::tuple::not_equal(_tuple_op_lhs_, _tuple_op_rhs_); \
  }

#define TUPLE_STRUCT_INTERNAL_DEFINE_OP_ostream(M, S, FIELDS)           \
  M inline ::std::ostream& operator<< [[maybe_unused]] (                \
      ::std::ostream & _tuple_op_lhs_,                                  \
      const TUPLE_INTERNAL_REMOVE_PARENS(S) & _tuple_op_rhs_) {         \
    return _tuple_op_lhs_ << ::util::tuple::streamable(_tuple_op_rhs_); \
  }

#define TUPLE_STRUCT_INTERNAL_DEFINE_OP_absl_format(M, S, FIELDS)              \
  M inline ::absl::FormatConvertResult<                                        \
      ::absl::FormatConversionCharSet::kString>                                \
  AbslFormatConvert                                                            \
      [[maybe_unused]] (const TUPLE_INTERNAL_REMOVE_PARENS(S) & _tuple_arg_,   \
                        const ::absl::FormatConversionSpec& /*_format_spec_*/, \
                        ::absl::FormatSink* _format_sync_) {                   \
    _format_sync_->Append(::util::tuple::to_string(_tuple_arg_));              \
    return {true};                                                             \
  }

#define TUPLE_STRUCT_INTERNAL_DEFINE_OP_swap(M, S, FIELDS)               \
  M inline void swap(TUPLE_INTERNAL_REMOVE_PARENS(S) & _tuple_op_lhs_,   \
                     TUPLE_INTERNAL_REMOVE_PARENS(S) & _tuple_op_rhs_) { \
    ::util::tuple::swap(_tuple_op_lhs_, _tuple_op_rhs_);                 \
  }

#define TUPLE_STRUCT_INTERNAL_DEFINE_OP_absl_hash(M, S, FIELDS)                \
  template <typename H>                                                        \
  M inline H AbslHashValue(                                                    \
      H h, const TUPLE_INTERNAL_REMOVE_PARENS(S) & _tuple_arg_) {              \
    static_assert(                                                             \
        std::is_same<bool, decltype(_tuple_arg_ == _tuple_arg_)>::value,       \
        "A TUPLE_STRUCT defining op absl_hash must also define op "            \
        "eq");                                                                 \
    return ::util::tuple::apply(::util::tuple::internal_struct::HashCombine{}, \
                                std::move(h), _tuple_arg_);                    \
  }

// A special operation 'rel' is a shortcut for a set of relational operations:
// lt, gt, le, ge, eq and ne. It can only be used in the struct scope, that's
// why we pass 'friend' to all ops except the first one.
// clang-format off
#define TUPLE_STRUCT_INTERNAL_DEFINE_OP_rel(M, S, FIELDS)                      \
  ABSL_ATTRIBUTE_UNUSED TUPLE_STRUCT_INTERNAL_DEFINE_OP_lt(M, S, FIELDS)       \
  ABSL_ATTRIBUTE_UNUSED TUPLE_STRUCT_INTERNAL_DEFINE_OP_gt(friend, S, FIELDS)  \
  ABSL_ATTRIBUTE_UNUSED TUPLE_STRUCT_INTERNAL_DEFINE_OP_le(friend, S, FIELDS)  \
  ABSL_ATTRIBUTE_UNUSED TUPLE_STRUCT_INTERNAL_DEFINE_OP_ge(friend, S, FIELDS)  \
  ABSL_ATTRIBUTE_UNUSED TUPLE_STRUCT_INTERNAL_DEFINE_OP_eq(friend, S, FIELDS)  \
  ABSL_ATTRIBUTE_UNUSED TUPLE_STRUCT_INTERNAL_DEFINE_OP_ne(friend, S, FIELDS)
// clang-format on

#define TUPLE_STRUCT_INTERNAL_DEFINE_OP_ctor(M, S, FIELDS)         \
  TUPLE_INTERNAL_IF(TUPLE_INTERNAL_IS_EMPTY FIELDS,                \
                    TUPLE_STRUCT_INTERNAL_ZERO_CONSTRUCTOR,        \
                    TUPLE_STRUCT_INTERNAL_ONE_OR_MORE_CONSTRUCTOR) \
  (M, S, FIELDS)

#define TUPLE_STRUCT_INTERNAL_ZERO_CONSTRUCTOR(_, S, _1) \
  TUPLE_INTERNAL_REMOVE_PARENS(S)() {}

#define TUPLE_STRUCT_INTERNAL_ONE_OR_MORE_CONSTRUCTOR(_, S, FIELDS)           \
  TUPLE_INTERNAL_IF(TUPLE_STRUCT_INTERNAL_ONE_FIELD FIELDS, explicit,         \
                    TUPLE_INTERNAL_EMPTY())                                   \
  TUPLE_INTERNAL_REMOVE_PARENS(S)                                             \
  (TUPLE_INTERNAL_LIST_FOR_EACH(TUPLE_STRUCT_INTERNAL_CTOR_PARAM, ~, FIELDS)) \
      : TUPLE_INTERNAL_LIST_FOR_EACH(TUPLE_STRUCT_INTERNAL_CTOR_INITIALIZER,  \
                                     ~, FIELDS) {}

#define TUPLE_STRUCT_INTERNAL_CTOR_PARAM(_, FIELD)                   \
  typename ::util::tuple::internal_struct::param_type<               \
      TUPLE_INTERNAL_UNPARENTHESIZE(TUPLE_INTERNAL_1ST FIELD)>::type \
      TUPLE_INTERNAL_2ND FIELD

#define TUPLE_STRUCT_INTERNAL_CTOR_INITIALIZER(_, FIELD)                      \
  TUPLE_INTERNAL_2ND FIELD(static_cast<decltype(TUPLE_INTERNAL_2ND FIELD)&&>( \
      TUPLE_INTERNAL_2ND FIELD))

#define TUPLE_STRUCT_INTERNAL_DEFINE_STRUCT_FIELD(_, FIELD) \
  TUPLE_STRUCT_INTERNAL_DEFINE_STRUCT_FIELD_I FIELD

#define TUPLE_STRUCT_INTERNAL_DEFINE_STRUCT_FIELD_I(TYPE, NAME, ...)        \
  TUPLE_INTERNAL_IF(TUPLE_INTERNAL_IS_EMPTY(__VA_ARGS__),                   \
                    TUPLE_STRUCT_INTERNAL_DEFINE_STRUCT_FIELD_WITHOUT_INIT, \
                    TUPLE_STRUCT_INTERNAL_DEFINE_STRUCT_FIELD_WITH_INIT)    \
  (TYPE, NAME, __VA_ARGS__)

#define TUPLE_STRUCT_INTERNAL_DEFINE_STRUCT_FIELD_WITHOUT_INIT(TYPE, NAME, \
                                                               ...)        \
  TUPLE_INTERNAL_UNPARENTHESIZE(TYPE) NAME;

#define TUPLE_STRUCT_INTERNAL_DEFINE_STRUCT_FIELD_WITH_INIT(TYPE, NAME, ...) \
  TUPLE_INTERNAL_UNPARENTHESIZE(TYPE) NAME = __VA_ARGS__;

#define TUPLE_STRUCT_INTERNAL_MAKE_STRUCT_FIELD(S, NAME)  \
  , ::util::tuple::internal_struct::struct_field<         \
        decltype(TUPLE_INTERNAL_REMOVE_PARENS(S)::NAME),  \
        decltype(&TUPLE_INTERNAL_REMOVE_PARENS(S)::NAME), \
        &TUPLE_INTERNAL_REMOVE_PARENS(S)::NAME, decltype(TUPLE_FIELD(NAME))>

#define TUPLE_STRUCT_INTERNAL_MAKE_STRUCT_FIELD_2(S, FIELD) \
  TUPLE_STRUCT_INTERNAL_MAKE_STRUCT_FIELD(S, TUPLE_INTERNAL_2ND FIELD)

#define TUPLE_STRUCT_INTERNAL_ONE_FIELD(x, ...) \
  TUPLE_INTERNAL_IS_EMPTY(__VA_ARGS__)

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_STRUCT_H_
