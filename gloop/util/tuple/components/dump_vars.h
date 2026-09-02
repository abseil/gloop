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

// DUMP_VARS() is a convenience macro for writing objects to text logs. It
// prints all of its arguments as key-value pairs.
//
// Example:
//
//   int foo = 42;
//   vector<int> bar = {1, 2, 3};
//   // Prints: foo = 42, bar.size() = 3
//   LOG(INFO) << DUMP_VARS(foo, bar.size());
//
// DUMP_VARS() uses util::tuple::streamable() for printing values. It produces
// high quality human-readable output for most types used in google3: builtin
// types, strings, protocol buffers, containers, tuples, smart pointers, Status,
// StatusOr, optional, anything with operator<<, and more. If everything else
// fails, objects are hex-dumped. See //gloop/util/tuple/streamable.h for
// details.
//
//                     ====[ Semantics ]====
//
// The following line:
//
//   LOG(INFO) << DUMP_VARS(expr1, ..., exprN);
//
// Is equivalent to:
//
//   LOG(INFO) << "expr1" << " = " << util::tuple::streamable(expr1) << ", "
//                               ...
//             << "exprN" << " = " << util::tuple::streamable(exprN);
//
// Member function as() can be used to override field names.
//
//   LOG(INFO) << DUMP_VARS(expr1, ..., exprN).as("name1", ..., "nameN");
//
// Is equivalent to:
//
//   LOG(INFO) << "name1" << " = " << util::tuple::streamable(expr1) << ", "
//                               ...
//             << "nameN" << " = " << util::tuple::streamable(exprN);
//
//                   ====[ Best Practices ]====
//
// Use DUMP_VARS with LOG and CHECK messages to provide extra context. To
// implement debug printing for your types, prefer
// `gtl::DebugPrintingExtension` from //gloop/util/gtl/extend/debug_printing.h.
//
//   LOG(INFO) << "Request processed: " << DUMP_VARS(request, response);
//
//   CHECK_OK(status) << "RPC request to Frobnicator.Frobnicate failed: "
//                    << DUMP_VARS(FLAGS_remote_service_bns, request);
//
// It also works well with the status macros from
// //gloop/util/task/status_macros.h
//
//   ABSL_RETURN_IF_ERROR(DoStuff(in_dirs, out_dirs))
//       << "DoStuff() failed: "
//       << DUMP_VARS(in_dirs.size(), out_dirs.size(), in_dirs, out_dirs);
//
// Since DUMP_VARS uses util::tuple::streamable() under the hood, it can print
// values of any type. Do use it in generic functions and macros where the types
// of the arguments are unknown. Best-effort printing is better than none.
//
//   template <class K, class V>
//   const V& FindOrDie(const std::map<K, V>& map, const K& key) {
//     auto it = map.find(key);
//     CHECK(it != map.end()) << "Key not found: " << DUMP_VARS(key, map);
//     return it->second;
//   }
//
// By default, DUMP_VARS uses `util::tuple::default_writer`. This can be changed
// with `.set_writer()`.
//
// Use the following template for all your messages:
//
//   1. Start with a literal message in plain English. If this message simply
//      restates the content of a CHECK macro, it can be omitted.
//   2. Use a single DUMP_VARS() expression at the end of the message with all
//      relevant variables. Remember that you can use arbitrary expressions as
//      arguments.
//   3. Avoid writing variables other than via DUMP_VARS().
//
// There are several advantages to this style:
//
//   * You don't have to waste mental power on creative intermixing of literal
//     text and variables, quoting and conditional pluralization.
//   * It'll always be clear when one variable ends and another starts. Empty
//     strings, and strings with spaces and quotes in them won't change the
//     meaning of your message.
//   * Regardless of the size of the variables, the primary message will always
//     fit in the log and will be easy to read.
//
//   // BAD. The message will be hard to read if src contains a sentence or
//   // special characters, or is empty or very long.
//   CHECK(CopyFile(src, dst)) << "Can't copy " << src << " to " << dst;
//
//   // GOOD. Easy to write and easy to read when the CHECK triggers.
//   CHECK(CopyFile(src, dst)) << "Can't copy file: " << DUMP_VARS(src, dst);
//
//   // BEST. The message wasn't useful.
//   CHECK(CopyFile(src, dst)) << DUMP_VARS(src, dst);
//
// If arguments to DUMP_VARS() aren't descriptive enough to be understood by
// the readers of the logs, override them with the member function as().
//
//    LOG(INFO) << "Opening: " << DUMP_VARS(it->second.value).as("filename");
//
// By default fields are separated with ", " and keys are separated from values
// with " = ". Different separators can be specified by calling the member
// function sep(field_separator, kv_separator). The second argument is optional.
// If it's not specified, the key-value separator stays unchanged.
//
//    VLOG(3) << "Internal state:\n"
//            << DUMP_VARS(a_, b_, c_, d_, e_, f_, g_, h_, i_).sep("\n", ":=");
//
// The arguments of DUMP_VARS get evaluated during streaming. If the same
// DUMP_VARS instance is streamed several times, the arguments are also
// evaluated several times. This allows you to factor out DUMP_VARS() calls
// that are repeated many times in the same function.
//
//   string Configuration::FindBackend(const string& service) {
//     // this->DebugString() isn't called here yet.
//     auto context = DUMP_VARS(service, this->DebugString());
//     // Calls this->DebugString() on CHECK failure.
//     CHECK(IsKnownService(service)) << context;
//     CHECK(IsAccessAllowed(service)) << context;
//     ...
//   }
//
//               ====[ String Representation ]====
//
// The streaming approach described above is generally recommended, for
// efficiency and readability, but some situations may require a string rather
// than a streamable object. The object returned by DUMP_VARS provides a
// convenient str() method for such cases:
//
//   void RpcMethod(RPC*, const Request& req, Response*, Closure*) {
//     VLOG_LINES(1, DUMP_VARS(req).str());
//   }
//
//                    ====[ Limitations ]====
//
// DUMP_VARS() accepts at most 64 arguments.
//
// All arguments to DUMP_VARS() must be perfect-forwardable. Brace-expressions,
// bit fields, and other esoterics are not supported.
//
//   // Compile error: {42} can't be perfect-forwarded.
//   LOG(INFO) << DUMP_VARS({42});
//
// Arguments with unparenthesized commas confuse and frighten DUMP_VARS,
// leading to compile errors. Either parenthesize problematic arguments or
// explicitly provide names with as() to avoid this problem.
//
//   // Compile error. DUMP_VARS gets confused: it thinks we are passing it two
//   // arguments.
//   LOG(INFO) << DUMP_VARS(pair<int, int>());
//
//   // This works! Note the parentheses around the argument.
//   LOG(INFO) << DUMP_VARS((pair<int, int>()));
//
//   // Also works.
//   LOG(INFO) << DUMP_VARS(pair<int, int>()).as("p");
//
// Values of type const char* are printed as pointers, not as strings. This is
// a safety measure because not all pointers to char are null-terminated
// strings. Construct an absl::string_view if string printing is desired.
//
//   const char* s = "hello";
//   // s = 0x1122334455667788
//   LOG(INFO) << DUMP_VARS(s);
//   // absl::string_view(s) = "hello"
//   LOG(INFO) << DUMP_VARS(absl::string_view(s));

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_DUMP_VARS_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_DUMP_VARS_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/log/structured.h"
#include "gloop/util/tuple/components/internal_preprocessor.h"  // IWYU pragma: export
#include "gloop/util/tuple/components/streamable.h"

// Returns an ostreamable type that prints all passed arguments as key-value
// pairs. Primarily used for logging.
//
//   int foo = 42;
//   vector<int> bar = {1, 2, 3};
//   LOG(INFO) << DUMP_VARS(foo, bar.size());
//
// Prints:
//
//   foo = 42, bar.size() = 3
//
// NOTE: if you get a compile error saying "no matching function for call to
// 'make_fields'", you need to surround some of the arguments with parentheses.
//
//   // error: no matching function for call to 'make_fields'.
//   // DUMP_VARS gets confused: it thinks we are passing it two arguments.
//   LOG(INFO) << DUMP_VARS(pair<int, int>());
//
//   // This works! Note the parentheses around the argument.
//   LOG(INFO) << DUMP_VARS((pair<int, int>()));
//
// See comments at the top of the file for details.
#define DUMP_VARS(...)                                                       \
  ::util::tuple::internal_dump_vars::make_dump_vars<                         \
      decltype(::util::tuple::internal_dump_vars::variadic_size(             \
          __VA_ARGS__))::value>(                                             \
      [&](auto* _dump_vars_strm_, const auto& _dump_vars_writer,             \
          const char* _dump_vars_field_sep_, const char* _dump_vars_kv_sep_, \
          const char* const* _dump_vars_names_)                              \
          ABSL_NO_THREAD_SAFETY_ANALYSIS {                                   \
            ::util::tuple::internal_dump_vars::print_fields<                 \
                std::remove_pointer_t<decltype(_dump_vars_strm_)>,           \
                std::remove_cv_t<                                            \
                    std::remove_reference_t<decltype(_dump_vars_writer)>>>{  \
                _dump_vars_strm_, _dump_vars_field_sep_, _dump_vars_kv_sep_, \
                _dump_vars_names_, &_dump_vars_writer}(__VA_ARGS__);         \
          })                                                                 \
      .as(TUPLE_INTERNAL_LIST_FOR_EACH(TUPLE_INTERNAL_DUMP_VARS_GEN_NAME, ~, \
                                       (__VA_ARGS__)))

namespace util {
namespace tuple {

namespace internal_dump_vars {

// All symbols defined within namespace internal_dump_vars are internal
// to dump_vars.h. Do not reference them from outside or your code can break
// without notice.

template <class... Ts>
::std::integral_constant<::size_t, sizeof...(Ts)> variadic_size(Ts&&...);

template <typename Stream, typename Writer>
struct print_fields {
  void operator()() {}

  template <class T>
  void operator()(const T& t) {
    // It is safe to log the names and separators as literal because we check
    // they are constexpr.
    *strm << absl::LogAsLiteral(*names) << absl::LogAsLiteral(kv_sep)
          << ::util::tuple::streamable(t, *writer);
  }

  template <class T1, class T2, class... Ts>
  void operator()(const T1& t1, const T2& t2, const Ts&... ts) {
    // It is safe to log the names and separators as literal because we check
    // they are constexpr.
    *strm << absl::LogAsLiteral(*names++) << absl::LogAsLiteral(kv_sep)
          << ::util::tuple::streamable(t1, *writer)
          << absl::LogAsLiteral(field_sep);
    (*this)(t2, ts...);
  }

  Stream* strm;
  const char* field_sep;
  const char* kv_sep;
  const char* const* names;
  const Writer* const writer;
};

template <class... Ts>
constexpr bool accept(Ts&&...) {
  return true;
}

#define TUPLE_INTERNAL_DUMP_VARS_REQUIRE_CONSTEXPR_STRING(s)
#define TUPLE_INTERNAL_DUMP_VARS_REQUIRE_CONSTEXPR_STRINGS(s)

#if defined(__clang__)
#if __has_attribute(enable_if)

#undef TUPLE_INTERNAL_DUMP_VARS_REQUIRE_CONSTEXPR_STRING
#define TUPLE_INTERNAL_DUMP_VARS_REQUIRE_CONSTEXPR_STRING(s)          \
  __attribute__((enable_if(                                           \
      ::util::tuple::internal_dump_vars::accept(__builtin_strlen(s)), \
      "the argument must be a constexpr string")))

#undef TUPLE_INTERNAL_DUMP_VARS_REQUIRE_CONSTEXPR_STRINGS
#define TUPLE_INTERNAL_DUMP_VARS_REQUIRE_CONSTEXPR_STRINGS(s)            \
  __attribute__((enable_if(                                              \
      ::util::tuple::internal_dump_vars::accept(__builtin_strlen(s)...), \
      "the arguments must be constexpr strings")))

#endif
#endif

template <::size_t kNumVals, ::size_t kNumNames, class F, class Writer>
class dump_vars {
 public:
  template <class... Names>
  explicit dump_vars(F f, Writer writer, const char* field_sep,
                     const char* kv_sep, const Names&... names)
      : f_(std::move(f)),
        writer_(std::move(writer)),
        field_sep_(field_sep),
        kv_sep_(kv_sep),
        names_{{names...}} {
    static_assert(sizeof...(Names) == kNumNames, "");
  }

  explicit dump_vars(F f, Writer writer, const char* field_sep,
                     const char* kv_sep,
                     ::std::array<const char*, kNumNames> names)
      : f_(std::move(f)),
        writer_(std::move(writer)),
        field_sep_(field_sep),
        kv_sep_(kv_sep),
        names_{names} {}

  ::std::string str() const { return ::util::tuple::to_string(*this); }

  // All arguments must be compile time strings. This restriction allows us to
  // store pointers instead of copying all bytes.
  template <class... Ts>
  dump_vars<kNumVals, sizeof...(Ts), F, Writer> as(const Ts&... ts) const
      TUPLE_INTERNAL_DUMP_VARS_REQUIRE_CONSTEXPR_STRINGS(ts) {
    return dump_vars<kNumVals, sizeof...(Ts), F, Writer>(
        f_, writer_, field_sep_, kv_sep_, ts...);
  }

  dump_vars& sep(const char* field_sep)
      TUPLE_INTERNAL_DUMP_VARS_REQUIRE_CONSTEXPR_STRING(field_sep) {
    field_sep_ = field_sep ? field_sep : "";
    return *this;
  }

  dump_vars& sep(const char* field_sep, const char* kv_sep)
      TUPLE_INTERNAL_DUMP_VARS_REQUIRE_CONSTEXPR_STRING(field_sep)
          TUPLE_INTERNAL_DUMP_VARS_REQUIRE_CONSTEXPR_STRING(kv_sep) {
    field_sep_ = field_sep ? field_sep : "";
    kv_sep_ = kv_sep ? kv_sep : "";
    return *this;
  }

  template <class NewWriter>
  dump_vars<kNumVals, kNumNames, F, NewWriter> set_writer(
      NewWriter new_writer) const {
    return dump_vars<kNumVals, kNumNames, F, NewWriter>(
        f_, std::move(new_writer), field_sep_, kv_sep_, names_);
  }

  // GError domain streaming extension.
  friend dump_vars&& GStreamable(dump_vars&& d) { return std::move(d); }

  friend ::std::ostream& operator<<(::std::ostream& strm,
                                    const dump_vars& lazy) {
    return lazy.DoStream(strm);
  }

  // We use an overload of `LogMessage` to support structured logs. We need
  // an explicit overload rather than a template to avoid call site ambiguities.
  // The abseil team gave their green light in cr/829076736.
  // NOLINTNEXTLINE(abseil-no-internal-dependencies)
  friend absl::log_internal::LogMessage& operator<<(
      // NOLINTNEXTLINE(abseil-no-internal-dependencies)
      absl::log_internal::LogMessage& strm, const dump_vars& lazy) {
    return lazy.DoStream(strm);
  }

 private:
  template <typename Stream>
  Stream& DoStream(Stream& strm) const {
    // This assertion triggers in two cases:
    //
    // 1. In DUMP_VARS(a1, ..., aN).as(b1, ..., bM), N != M. Pass the right
    // number of arguments to as() to fix.
    //
    // 2. In DUMP_VARS(a1, ..., aN), one of the arguments has unparenthesizes
    // commas and thus confuses the preprocessor. Parenthesize the problematic
    // arguments to DUMP_VARS() to fix.
    static_assert(kNumVals == kNumNames,
                  "Either pass the right number of arguments to "
                  "DUMP_VARS().as() or, if not calling as(), "
                  "parenthesize arguments to DUMP_VARS()");
    f_(&strm, writer_, field_sep_, kv_sep_, names_.data());
    return strm;
  }

  F f_;
  Writer writer_;
  const char* field_sep_;
  const char* kv_sep_;
  ::std::array<const char*, kNumNames> names_;
};

template <::size_t kNumVals, class F>
dump_vars<kNumVals, 0, F, default_writer_t<>> make_dump_vars(F f) {
  return dump_vars<kNumVals, 0, F, default_writer_t<>>(
      std::move(f), default_writer, ", ", " = ");
}

#undef TUPLE_INTERNAL_DUMP_VARS_REQUIRE_CONSTEXPR_STRINGS

template <class T>
struct is_dump_vars : ::std::false_type {};

template <::size_t kNumVals, ::size_t kNumNames, class F, class Writer>
struct is_dump_vars<dump_vars<kNumVals, kNumNames, F, Writer>>
    : ::std::true_type {};

}  // namespace internal_dump_vars

// Is T the result of DUMP_VARS(...)?
template <class T>
struct is_dump_vars
    : internal_dump_vars::is_dump_vars<typename ::std::decay<T>::type> {};

#define TUPLE_INTERNAL_DUMP_VARS_GEN_NAME(data, FIELD) \
  TUPLE_INTERNAL_STRINGIZE(FIELD)

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_DUMP_VARS_H_
