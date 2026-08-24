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
// Function template streamable() allows logging objects of any type,
// including tuples. This file does *not* define operator<< for tuples.
//
//   tuple<int, string> t(42, "hello");
//   LOG(INFO) << t;               // ERROR: won't compile.
//   LOG(INFO) << streamable(t);   // This works.
//   LOG(INFO) << streamable(42);  // Works for any type, not just for tuples.
//
// The output is human readable but not necessarily reversible.
//
// streamable(obj) stores a reference to obj; be careful not to use it after
// the original object is destroyed. The safest and the most natural approach
// is to write streamable(obj) directly to the log stream without saving the
// intermediate proxy object.
//
//   auto proxy = streamable(my_object);  // DANGEROUS: DO NOT DO THIS
//   LOG(INFO) << proxy;
//
//   LOG(INFO) << streamable(my_object);  // GOOD
//
//           ====[ Customizing the streaming behaviour ]====
//
// streamable() accepts a second optional parameter called Writer. Writer is a
// polymorphic functor compatible with void(ostream&, const T&) where T can
// be any type. When streamable(obj, writer) is written to an ostream, it just
// calls writer(stream, obj).
//
// The default Writer is default_writer_t<>, which knows how to produce human
// readable representation for any type. Many types, such as STL containers or
// tuples, require recursive application of the writer.
//
// It's easy to create a custom writer that decorates default_writer_t<>.
// Here's a writer that prefixes each value with its type.
//
//   struct TypeDecoratorWriter : default_writer_t<TypeDecoratorWriter> {
//     template <class T>
//     void operator()(std::ostream& stream, const T& obj) const {
//       stream << "(" << util::Demangle(typeid(T).name()) << ") ";
//       default_writer_t<TypeDecoratorWriter>::operator()(stream, obj);
//     }
//   };
//
//   std::tuple<int> t(42);
//   // {42}
//   LOG(INFO) << streamable(t);
//   // (std::tuple<int>) {(int) 42}
//   LOG(INFO) << streamable(t, TypeDecoratorWriter());
//
// Here's another example: KeyValueWriter prints std::pair as "key = value"
// instead of the default "{key, value}".
//
//   struct KeyValueWriter : default_writer_t<KeyValueWriter> {
//     template <class Key, class Value>
//     void operator()(ostream& stream, const pair<Key, Value>& p) const {
//       default_writer_t<KeyValueWriter>::operator()(stream, p.first);
//       stream << " = ";
//       default_writer_t<KeyValueWriter>::operator()(stream, p.second);
//     }
//
//     // Use the standard algorithm for printing other (non pair) types.
//     using default_writer_t<KeyValueWriter>::operator();
//   };
//
//   std::map<int, std::string> m = {{1, "one"}, {2, "two"}};
//   // [{1, "one"}, {2, "two"}]
//   LOG(INFO) << streamable(m);
//   // [1 = "one", 2 = "two"].
//   LOG(INFO) << streamable(m, KeyValueWriter());
//
//  ====[ Specifying default streaming behaviour for user defined types ]====
//
// The simplest way to make a user defined type look nice when written by
// streamable() is to define operator<< for it. It must be defined in the same
// namespace as the type on which it operates.
//
//   struct Person {
//     std::string name;
//     int age;
//   };
//
//   std::ostream& operator<<(std::ostream& stream, const Person& person) {
//     return stream << "{" << "name = " << streamable(person.name)
//                 << ", age = " << streamable(person.age) << "}";
//   }
//
//   Person p = {"Dolores", 12};
//   // {name = "Dolores", age = 12}
//   LOG(INFO) << p;
//   // The same as above.
//   LOG(INFO) << streamable(p);
//
// A more advanced and more flexible approach is to define
// PrintTo(const T&, std::ostream*, const Writer&), where Writer is a template
// parameter. The writer should be used to print the parts of the object,
// such as its fields.
//
//   struct Person {
//     std::string name;
//     int age;
//   };
//
//   // Flexible streaming support that allows users to customize printing of
//   // individual fields.
//   template <class Writer>
// void PrintTo(const Person& person, std::ostream* stream,
//              const Writer& writer) {
//   *stream << "{"
//           << "name = " << streamable(person.name, writer)
//           << ", age = " << streamable(person.age, writer) << "}";
// }
//
//   Person p = {"Dolores", 12};
//   // {name = "Dolores", age = 12}
//   LOG(INFO) << streamable(p);
//   // (Person) {name = (std::string) "Dolores", age = (int) 12}
//   LOG(INFO) << streamable(p, TypeDecoratorWriter());
//
// It's a good practice to define operator<< in addition to PrintTo() to make it
// possible to log the instance of the type without explicit streamable().
// It will also be picked up by gunit, gmock and other libraries that need to
// produce human readable representations of objects.
//
//   // Handy default streaming behaviour.
//   std::ostream& operator<<(std::ostream& stream, const Person& person) {
//     return stream << streamable(person);
//   }
//
//       ====[ Converting an object to a human readable string ]====
//
// Use to_string(obj) to convert the object to a human readable string. This
// is a simple wrapper around streamable(obj). There is also an overload that
// accepts writer as a second parameter; see "Customizing the streaming
// behaviour" above for more info on writers.
//
//   assert(to_string(42) == "42");
//   assert(to_string(make_tuple(true, 2.5)) == "{true, 2.5}");
//
// You can use the strappend(&s, obj) variation to append to an existing string:
//
//   std::string s;
//   strappend(&s, 4);
//   strappend(&s, 2);
//   assert(s == "42");

// IWYU pragma: private, include "util/tuple/streamable.h"

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_STREAMABLE_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_STREAMABLE_H_

#include <stddef.h>

#include <array>
#include <cstddef>
#include <deque>
#include <forward_list>
#include <functional>
#include <iomanip>
#include <ios>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/casts.h"
#include "absl/base/macros.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "absl/types/source_location.h"
#include "gloop/strings/ostringstream.h"
#include "gloop/util/gtl/typeid.h"
#include "gloop/util/status/status.h"
#include "gloop/util/tuple/components/for_each.h"
#include "gloop/util/tuple/components/intrinsics.h"
#include "gloop/util/tuple/components/rank.h"

#ifdef UTIL_TUPLE_COMPONENTS_HAVE_UTIL_SYMBOLIZE
#include "gloop/util/symbolize/demangle.h"
#endif

#ifdef UTIL_TUPLE_COMPONENTS_HAVE_NET_PROTO2
#include "google/protobuf/descriptor.h"
#include "google/protobuf/generated_enum_reflection.h"
#include "google/protobuf/generated_enum_util.h"
#endif

namespace util::tuple {

// A wrapper around an object that default_writer_t<> can't print intelligently.
// User-defined writers can handle such types specially. For example, if a
// compile error is desired when attempting to print an unprintable type, one
// could do the following.
//
//   struct MyWriter : default_writer_t<MyWriter> {
//     template <class T>
//     void operator()(std::ostream& stream, const unprintable<T>& obj) const {
//       static_assert(sizeof(obj) == 0, "Type T is unprintable");
//     }
//     using default_writer_t<MyWriter>::operator();
//   };
//
//   void F() {
//     struct {} s;
//     to_string(s);  // compiles: the struct is hex dumped
//     to_string(s, MyWriter());                 // compile error
//     to_string(make_pair(42, s), MyWriter());  // compile error
//   }
//
// The MyWriter::operator() overload for unprintable<T> is also a good place to
// call user-defined extension points. For example, here's how writer can
// dispatch to Repr(std::ostream&, const T&) after default_writer_t<> has
// exhausted all its options.
//
//   struct MyWriter : default_writer_t<MyWriter> {
//     template <class T>
//     auto operator()(std::ostream& stream, const unprintable<T>& obj) const
//         -> decltype(Repr(stream, obj.value)) {
//       Repr(stream, obj.value);
//     }
//     using default_writer_t<MyWriter>::operator();
//   };
template <class T>
struct unprintable {
  static_assert(!::std::is_reference_v<T>);
  using type = T;
  const T& value;
};

namespace internal_streamable {

// All symbols defined within namespace internal_streamable are internal
// to streamable.h. Do not reference them from outside or your code can break
// without notice.

// Helper function for proper printing of values pointed to by iterators.
// Printing *iter doesn't always produce the correct result because it may
// be a proxy reference. We want to cast it to const Iter::value_type& but
// can't do it unconditionally due to plenty of iterators in the wild that
// don't define inner value_type.

template <class Iter>
const typename Iter::value_type& iterator_reference(const Iter& iter,
                                                    rank<0>);  // Not defined.

template <class Iter>
decltype(*::std::declval<const Iter&>()) iterator_reference(
    const Iter& iter, rank<1>);  // Not defined.

// Is type T an instance of class template U?
template <class T, template <class...> class U, class Enabler = void>
struct is_instance_of : ::std::false_type {};
template <template <class...> class C, class... Args,
          template <class...> class U>
struct is_instance_of<
    C<Args...>, U, ::std::enable_if_t<::std::is_same_v<C<Args...>, U<Args...>>>>
    : ::std::true_type {};
template <class T, template <class...> class U>
inline constexpr bool is_instance_of_v = is_instance_of<T, U>::value;

template <class T>
struct is_integral_constant : ::std::false_type {};
template <class T, T V>
struct is_integral_constant<::std::integral_constant<T, V>> : ::std::true_type {
};
template <class T>
inline constexpr bool is_integral_constant_v = is_integral_constant<T>::value;

// Retrieves the inner type of a smart pointer (eg: T from unique_ptr<T>),
// and facilitates SFINAE for non smart pointer types.
template <class T, class = typename T::element_type,
          class = decltype(*::std::declval<const T&>()),
          class P = decltype(::std::declval<const T&>().get()),
          class = ::std::enable_if_t<::std::is_constructible_v<
              bool, const T&>&& ::std::is_pointer_v<P>>>
using smart_ptr_element_type = typename ::std::remove_pointer_t<P>;

// When printing a user-defined type T, several user-defined extension points
// (a.k.a. hooks) are tried in order. This enumeration contains all of them.
// Note that it doesn't have the 3-argument PrintTo(), which is handled
// specially.
//
// The order of the elements is important for it matches the order in which
// the hooks are tried. The lack of gaps in numerical values is also important.
enum hook_t {
  kFirst,
  // void PrintTo(const T&, ostream*);
  kPrintTo = kFirst,
  // ostream& operator<<(ostream&, const T&);
  kOStream,
  // string AbslUnparseFlag(const T&);
  kUnparseFlag,
  // string T::DebugString() const;
  kDebugString,
  // friend void AbslStringify(Sink& sink, const T&);  //
  // https://abseil.io/tips/215
  kAbslStringify,

  kNone,
};

// The second parameter specifies which user-defined extension points (hooks)
// the printer is allowed to use. Everything in [Allowed, kNone) is a go.
template <class Writer, hook_t Allowed>
class printer {
 public:
  // The primary template kicks in when Allowed is kNone, meaning that we can't
  // use user-defined extension points. There are a bunch of specializations
  // of printer<> below for other values of hook_t.
  static_assert(Allowed == kNone);

  // used_hooks is a write-only variable. Its initial value is kNone.
  // If printer decides to call a user-defined extension point to print the
  // object passed to print(), it should write to *used_hook. Oh yeah, print()
  // is called exactly once.
  printer(::std::ostream* stream, const Writer* writer, hook_t* used_hook)
      : stream_(*stream),
        writer_(*writer),
        used_hook_(*used_hook),
        flags_(stream->flags()) {
    stream->flags(static_cast<::std::ios::fmtflags>(0));
  }

  // This type is neither copyable nor movable.
  printer(const printer&) = delete;
  printer& operator=(const printer&) = delete;

  ~printer() { stream_.flags(flags_); }

  // If there is PrintTo(const T&, ostream*, const Writer&) defined for the
  // type, call it.
  template <class T, class = decltype(PrintTo(::std::declval<const T&>(),
                                              ::std::declval<::std::ostream*>(),
                                              ::std::declval<const Writer&>()))>
  void print(const T& obj, rank<0>) const {
    PrintTo(obj, &stream_, writer_);
  }

  // nullptr_t is printed as "nullptr".
  template <class T,
            class = ::std::enable_if_t<::std::is_same_v<T, ::std::nullptr_t>>>
  void print(const T& obj, rank<1>) const {
    stream_ << "nullptr";
  }

  // Pointers to function and member pointers are printed as pointers.
  template <class T, class = ::std::enable_if_t<
                         ::std::is_function_v<::std::remove_pointer_t<T>> ||
                         ::std::is_member_pointer_v<T>>>
  void print(const T& obj, rank<2>) const {
    ::std::decay_t<T> decayed = obj;
    const auto* p = reinterpret_cast<const unsigned char*>(&decayed);
    stream_ << "0x";
    for (::size_t i = 0; i != sizeof(decayed); ++i) {
      stream_ << ::std::hex << ::std::setfill('0') << ::std::setw(2)
              << int{p[sizeof(decayed) - i - 1]};
    }
  }

  // char is printed as character: 'A'.
  template <class T, class = ::std::enable_if_t<::std::is_same_v<T, char>>>
  void print(const T& obj, rank<3>) const {
    stream_ << '\'' << ::absl::CEscape(::absl::string_view(&obj, 1)) << '\'';
  }

  // signed and unsigned char are printed as hex: 0x00, 0xFF.
  template <class T,
            class = ::std::enable_if_t<::std::is_same_v<T, signed char> ||
                                       ::std::is_same_v<T, unsigned char>>>
  void print(const T& obj, rank<4>) const {
    stream_ << "0x" << ::std::hex << ::std::uppercase << ::std::setfill('0')
            << ::std::setw(2) << int{::absl::implicit_cast<unsigned char>(obj)};
  }

  // bool is printed as "true" or "false".
  template <class T, class = ::std::enable_if_t<::std::is_same_v<T, bool>>>
  void print(const T& obj, rank<5>) const {
    stream_ << (obj ? "true" : "false");
  }

  template <class T, class = ::std::enable_if_t<is_integral_constant_v<T>>>
  void print(const T&, rank<6>) const {
    writer_(stream_, T::value);
  }

  // Pointers to volatile and to [signed|unsigned] char are printed as const
  // void*. The latter is handled by default by operator<< but can also be
  // overridden by custom writers.
  //
  // Printing char* as a pointer rather than NUL-terminated string may be
  // annoying but it's safer.
  template <class T, class = ::std::enable_if_t<::std::is_pointer_v<T>>,
            class U = ::std::remove_pointer_t<T>,
            class V = ::std::remove_cv_t<U>,
            class = ::std::enable_if_t<::std::is_volatile_v<U> ||
                                       ::std::is_same_v<V, char> ||
                                       ::std::is_same_v<V, signed char> ||
                                       ::std::is_same_v<V, unsigned char>>>
  void print(const T& obj, rank<7>) const {
    // This casts away volatile if present.
    writer_(stream_, const_cast<const void*>(
                         ::absl::implicit_cast<const volatile void*>(obj)));
  }

  // const char[N] is printed as string_view of size N-1 if the last character
  // is NUL. Otherwise it's printed as container of chars.
  template <class T, class = ::std::enable_if_t<::std::is_array_v<T>>,
            class = ::std::enable_if_t<
                ::std::is_same_v<::std::remove_all_extents_t<T>, char>>>
  void print(const T& obj, rank<8>) const {
    const ::size_t size = std::size(obj);
    if (size > 0 && obj[size - 1] == '\0')
      writer_(stream_, ::absl::string_view(obj, size - 1));
    else
      print_range(obj);
  }

  // string_view is escaped and quoted: "Hello\n".
  //
  // This overload handles all types that are implicitly convertible to
  // string_view except const char* and nullptr_t, which are explicitly handled
  // above. This overload covers: string_view itself; ::std::string; types
  // derived from string_view, ::std::string; types with implicit conversion
  // operator to string_view or types derived from string_view.
  template <class T, class = ::std::enable_if_t<::std::is_convertible_v<
                         const T&, ::absl::string_view>>>
  void print(const T& obj, rank<9>) const {
    // Use an inline lambda for safety. It's useful if T is implicitly
    // convertible to a type derived from string_view that invalidates the
    // string in its destructor.
    [&](::absl::string_view s) {
      if (s.data() == nullptr)
        writer_(stream_, nullptr);
      else
        stream_ << '"' << ::absl::CEscape(s) << '"';
    }(obj);
  }

  // Cord is printed as string_view: escaped and quoted.
  template <class T,
            class = ::std::enable_if_t<::std::is_same_v<T, ::absl::Cord>>>
  void print(const T& obj, rank<10>) const {
    writer_(stream_, ::absl::Cord(obj).Flatten());
  }

  // reference_wrapper<T> is printed as T.
  template <class T, class = ::std::enable_if_t<
                         is_instance_of_v<T, ::std::reference_wrapper>>>
  void print(const T& obj, rank<12>) const {
    writer_(stream_, obj.get());
  }

  // Tuple is printed as {v1, v2, ...} if elements have no names and as
  // {k1 = v1, k2 = v2} otherwise.
  template <class T, class = ::std::enable_if_t<has_all_elements<T>{}>>
  void print(const T& obj, rank<13>) const {
    stream_ << "{";
    ::util::tuple::for_each_index(write_element<T>{stream_, writer_}, obj);
    stream_ << "}";
  }

  // Range (anything supporting ::std::begin() and ::std::end()) is printed as
  // [v1, v2, ...].
  template <
      class T, class Begin = decltype(::std::begin(::std::declval<const T&>())),
      class = ::std::enable_if_t<::std::is_convertible_v<
          typename ::std::iterator_traits<Begin>::iterator_category,
          ::std::forward_iterator_tag>>,
      class End = decltype(::std::end(::std::declval<const T&>())),
      class = decltype(++::std::declval<Begin&>()),
      class = ::std::enable_if_t<
          ::std::is_convertible_v<decltype(::std::declval<const Begin&>() ==
                                           ::std::declval<const End&>()),
                                  bool>>,
      class D = decltype(*::std::declval<const Begin&>()),
      class R = decltype(iterator_reference(::std::declval<const Begin&>(),
                                            rank_selector)),
      class = ::std::enable_if_t<::std::is_convertible_v<D, R>>>
  void print(const T& obj, rank<14>) const {
    print_range(obj);
  }

  // Smart pointers to non-objects are printed as pointers.
  template <class T, class E = smart_ptr_element_type<T>,
            class = ::std::enable_if_t<!::std::is_object_v<E>>>
  void print(const T& obj, rank<15>) const {
    if (obj) {
      if (auto* p = obj.get(); p != nullptr) {
        writer_(stream_, p);
        return;
      }
    }
    writer_(stream_, nullptr);
  }

  // Smart pointers to objects are printed as the address followed by the
  // value.
  template <class T, class E = smart_ptr_element_type<T>,
            class = ::std::enable_if_t<::std::is_object_v<E>>>
  void print(const T& obj, rank<16>) const {
    if (obj) {
      if (auto* p = obj.get(); p != nullptr) {
        writer_(stream_, ::absl::implicit_cast<const void*>(p));
        stream_ << " pointing to ";
        writer_(stream_, *p);
        return;
      }
    }
    writer_(stream_, nullptr);
  }

#ifdef UTIL_TUPLE_COMPONENTS_HAVE_NET_PROTO2
  // Proto enums are printed as names followed by integral values: GREEN
  // (42).
  template <class T,
            class = ::std::enable_if_t<::google::protobuf::is_proto_enum<T>{}>>
  void print(const T& obj, rank<18>) const {
    const ::google::protobuf::EnumDescriptor* descriptor =
        ::google::protobuf::GetEnumDescriptor<T>();
    const ::google::protobuf::EnumValueDescriptor* value_descriptor =
        descriptor->FindValueByNumber(obj);
    auto value = static_cast<::std::underlying_type_t<T>>(obj);
    if (value_descriptor) {
      stream_ << value_descriptor->name() << " (";
      writer_(stream_, value);
      stream_ << ")";
    } else {
      writer_(stream_, value);
    }
  }
#endif

  // Print `Status` using the legacy format to keep backward-compatibility with
  // tests relying on `streamable(status)` using the legacy format.
  template <class T,
            class = ::std::enable_if_t<::std::is_same_v<T, ::absl::Status>>>
  void print(const T& obj, rank<19>) const {
    stream_ << ::util::StatusToString(obj);
  }

  // Error StatusOr is printed as status.
  // Successful StatusOr is printed as "OK" followed by the value.
  template <class T,
            class = ::std::enable_if_t<is_instance_of_v<T, ::absl::StatusOr>>>
  void print(const T& obj, rank<20>) const {
    writer_(stream_, obj.status());
    if (obj.ok()) {
      stream_ << ": ";
      writer_(stream_, *obj);
    }
  }

  // optional<T> is printed either as "nullopt" or as T.
  template <class T,
            class = ::std::enable_if_t<is_instance_of_v<T, ::std::optional> &&
                                       sizeof(T) != 0>>
  void print(const T& obj, rank<21>) const {
    if (obj) {
      stream_ << "[";
      writer_(stream_, *obj);
      stream_ << "]";
    } else {
      stream_ << "nullopt";
    }
  }

  // nullopt_t is printed as "nullopt".
  template <class T,
            class = ::std::enable_if_t<::std::is_same_v<T, ::std::nullopt_t>>>
  void print(const T& obj, rank<22>) const {
    stream_ << "nullopt";
  }

  // absl::Flag<T> is printed as T.
  template <class T,
            class = ::std::enable_if_t<is_instance_of_v<T, ::absl::Flag>>>
  void print(const T& obj, rank<23>) const {
    writer_(stream_, ::absl::GetFlag(obj));
  }

  // ::std::variant is printed as "(${type})${value}".
  template <class T,
            class = ::std::enable_if_t<is_instance_of_v<T, ::std::variant>>>
  void print(const T& obj, rank<24>) const {
    ::std::visit(write_with_type{stream_, writer_}, obj);
  }

  // rank<25> is used by printer<Writer, kPrintTo>.
  // rank<26> is used by printer<Writer, kOstream>.

#ifdef UTIL_TUPLE_COMPONENTS_HAVE_NET_PROTO2
  // Protocol buffers are printed using google::protobuf::ShortFormat() if it's
  // not too long and using absl::StrCat() otherwise.
  template <class T, class = ::std::enable_if_t<::std::is_convertible_v<
                         T*, ::google::protobuf::Message*>>>
  void print(const T& obj, rank<27>) const {
    // absl::StrCat() invokes AbslStringify() on protocol buffers, which redacts
    // fields annotated to contain sensitive information.
    // google::protobuf::ShortFormat() is like AbslStringify(), but it doesn't
    // insert new lines between fields. google::protobuf::ShortFormat() is used
    // when the output size is <= kMaxProtoOneliner.
    constexpr ::size_t kMaxProtoOneliner = 200;
    ::std::string oneliner = google::protobuf::ShortFormat(obj);
    if (oneliner.size() <= kMaxProtoOneliner && obj.ByteSizeLong() == 0) {
      // Empty protocol buffers are special-cased to avoid non-useful debug
      // annotations.
      stream_ << "<>";
    } else if (oneliner.size() <= kMaxProtoOneliner) {
      stream_ << "<" << oneliner << ">";
    } else {
      stream_ << absl::StrCat("<\n", obj, ">");
    }
  }
#endif

  // rank<28> is used by printer<Writer, kUnparseFlag>.

  // Enum class is printed as integer.
  template <class T, class = ::std::enable_if_t<::std::is_enum_v<T>>>
  void print(const T& obj, rank<29>) const {
    writer_(stream_, static_cast<::std::underlying_type_t<T>>(obj));
  }

  // rank<30> is used by printer<Writer, kDebugString>.
  // rank<31> is used by printer<Writer, kAbslStringify>.

  // ::std::monostate holds no value, so print nothing.
  // This works well when printing a variant with a monostate member.
  template <class T, class = ::std::enable_if_t<
                         ::std::is_same_v<typename T::type, ::std::monostate>>>
  void print(const T& obj, rank<32>) const {}

  // SourceLocation printed as file:line - becomes clickable in test outputs.
  template <class T, class = ::std::enable_if_t<
                         ::std::is_same_v<T, absl::SourceLocation>>>
  void print(const T& obj, rank<33>) const {
    stream_ << obj.file_name() << ":" << obj.line();
  }

  // Complete unprintable types are hex-dumped.
  template <class T,
            class = ::std::enable_if_t<is_instance_of_v<T, unprintable> &&
                                       sizeof(typename T::type) != 0>>
  void print(const T& obj, rank<34>) const {
    using V = typename T::type;
    stream_ << sizeof(V) << "-byte object <";

    const ::size_t kThreshold = 132;
    const ::size_t kChunkSize = 64;
    // If the object size is bigger than kThreshold, we'll have to omit
    // some details by printing only the first and the last kChunkSize
    // bytes.
    if (sizeof(V) < kThreshold) {
      print_bytes(&obj.value, 0, sizeof(V));
    } else {
      print_bytes(&obj.value, 0, kChunkSize);
      stream_ << " ... ";
      // Rounds up to 2-byte boundary.
      const ::size_t resume_pos = (sizeof(V) - kChunkSize + 1) / 2 * 2;
      print_bytes(&obj.value, resume_pos, sizeof(V) - resume_pos);
    }
    stream_ << ">";
  }

  // Incomplete unprintable types are printed as "<incomplete>".
  template <class T,
            class = ::std::enable_if_t<is_instance_of_v<T, unprintable>>>
  void print(const T& obj, rank<35>) const {
    stream_ << "<incomplete>";
  }

  // rank<36> is used by printer<Writer, kPrintTo>.

  // This overload triggers if T has user-defined hooks and all of them
  // recursively call into util::tuple::to_string(obj) or
  // util::tuple::streamable(obj).
  template <class T>
  void print(const T& obj, rank<37>) const {
    stream_ << "<recursive>";
  }

 private:
  // Writes one element of tuple T.
  template <class T>
  struct write_element {
    template <::size_t I, class Elem>
    void operator()(const Elem& elem) const {
      if (I > 0) stream << ", ";
      if (const char* key = name<I, T>(); key != nullptr)
        stream << key << " = ";
      writer(stream, elem);
    }

    ::std::ostream& stream;
    const Writer& writer;
  };

  struct write_with_type {
    template <class T>
    void operator()(const T& obj) const {
#if defined(UTIL_TUPLE_COMPONENTS_HAVE_UTIL_SYMBOLIZE) && defined(__GXX_RTTI)
      stream << "(" << ::util::Demangle(typeid(T).name()) << ")";
#endif
      writer(stream, obj);
    }
    ::std::ostream& stream;
    const Writer& writer;
  };

  // Range is an object that supports std::begin() and std::end().
  template <class Range>
  void print_range(const Range& range) const {
    auto begin = ::std::begin(range);
    const auto end = ::std::end(range);
    using reference = decltype(iterator_reference(begin, rank_selector));
    stream_ << "[";
    if (begin != end) {
      writer_(stream_, ::absl::implicit_cast<reference>(*begin));
      while (++begin != end) {
        stream_ << ", ";
        writer_(stream_, ::absl::implicit_cast<reference>(*begin));
      }
    }
    stream_ << "]";
  }

  // Writes raw bytes as hex.
  ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS ABSL_ATTRIBUTE_NO_SANITIZE_MEMORY void
  print_bytes(const void* p, ::size_t start, ::size_t count) const {
    for (::size_t i = 0; i != count; ++i) {
      const ::size_t j = start + i;
      if (i != 0) {
        // Organizes the bytes into groups of 2 for easy parsing by human.
        stream_ << (((j % 2) == 0) ? ' ' : '-');
      }
      stream_ << ::std::hex << ::std::uppercase << ::std::setfill('0')
              << ::std::setw(2)
              << ::absl::implicit_cast<int>(
                     reinterpret_cast<const unsigned char*>(p)[j]);
    }
  }

 protected:
  ::std::ostream& stream_;
  const Writer& writer_;
  hook_t& used_hook_;
  ::std::ios::fmtflags flags_;
};

// A sink for AbslStringify which redirects everything to a stream.
//
// b/312706653 - This sink was formerly a private inner class in
// `printer<Writer, kAbslStringify>`, but it was moved out to workaround a
// suspected compiler warning bug. It is only meant to be used by that printer.
class PrinterStringifySink {
 public:
  explicit PrinterStringifySink(::std::ostream& os) : os_(os) {}

  void Append(::absl::string_view text) { os_ << text; }
  void Append(::size_t length, char ch) {
    for (::size_t i = 0; i < length; ++i) os_ << ch;
  }
  friend void AbslFormatFlush(PrinterStringifySink* sink,
                              ::absl::string_view text) {
    sink->Append(text);
  }

 private:
  ::std::ostream& os_;
};

template <class Writer>
class printer<Writer, kAbslStringify> : public printer<Writer, kNone> {
 public:
  using printer<Writer, kNone>::printer;
  using printer<Writer, kNone>::print;

  // If the type implements AbslStringify, use it to convert to string.
  template <class T,
            class = decltype(AbslStringify(
                ::std::declval<PrinterStringifySink&>(), ::std::declval<T>()))>
  void print(const T& obj, rank<31>) const {
    this->used_hook_ = kAbslStringify;
    PrinterStringifySink sink(this->stream_);
    AbslStringify(sink, obj);
  }
};

template <class Writer>
class printer<Writer, kDebugString> : public printer<Writer, kAbslStringify> {
 public:
  using printer<Writer, kAbslStringify>::printer;
  using printer<Writer, kAbslStringify>::print;

  // If there is `StringType T::DebugString() const` (where StringType converts
  // to string_view), use it.
  template <class T, class = ::std::enable_if_t<::std::is_convertible_v<
                         decltype(::std::declval<const T&>().DebugString()),
                         ::absl::string_view>>>
  void print(const T& obj, rank<30>) const {
    this->used_hook_ = kDebugString;
    this->stream_ << ::absl::string_view(obj.DebugString());
  }
};

template <class Writer>
class printer<Writer, kUnparseFlag> : public printer<Writer, kDebugString> {
 public:
  using printer<Writer, kDebugString>::printer;
  using printer<Writer, kDebugString>::print;

  // If there is UnparseFlag(const T&) defined for the type, call it.
  template <class T, class = ::std::enable_if_t<::std::is_convertible_v<
                         decltype(AbslUnparseFlag(::std::declval<const T&>())),
                         ::absl::string_view>>>
  void print(const T& obj, rank<28>) const {
    this->used_hook_ = kUnparseFlag;
    this->stream_ << ::absl::string_view(AbslUnparseFlag(obj));
  }
};

template <class Writer>
class printer<Writer, kOStream> : public printer<Writer, kUnparseFlag> {
 public:
  using printer<Writer, kUnparseFlag>::printer;
  using printer<Writer, kUnparseFlag>::print;

  // Ostreamable types are ostreamed.
  template <class T, class = decltype(::std::declval<::std::ostream&>()
                                      << ::std::declval<const T&>())>
  void print(const T& obj, rank<26>) const {
    this->used_hook_ = kOStream;
    this->stream_ << obj;
  }
};

template <class Writer>
class printer<Writer, kPrintTo> : public printer<Writer, kOStream> {
 public:
  using printer<Writer, kOStream>::printer;
  using printer<Writer, kOStream>::print;

  // If there is PrintTo(const T&, ostream*) defined for the type, call it.
  template <class T,
            class = decltype(PrintTo(::std::declval<const T&>(),
                                     ::std::declval<::std::ostream*>()))>
  void print(const T& obj, rank<25>) const {
    this->used_hook_ = kPrintTo;
    PrintTo(obj, &this->stream_);
  }

  // We've exhausted all our options and didn't find a way to print the object.
  // All we can do now is wrap it in unprintable<T> and perform overload
  // resolution once again.
  template <class T>
  void print(const T& obj, rank<36>) const {
    this->writer_(this->stream_, unprintable<T>{obj});
  }
};

// To avoid infinite recursion, we track the objects on the stack that are
// currently being written. We store them in a linked list in TLS. Each element
// is keyed by {typeid, pointer} pair. We can't use just the pointer because two
// objects of different type can have the same address (e.g., a struct and its
// first field).
class recursion_tracker {
 public:
  recursion_tracker(::size_t type_id, const void* obj);

  // This type is neither copyable nor movable.
  recursion_tracker(const recursion_tracker&) = delete;
  recursion_tracker& operator=(const recursion_tracker&) = delete;

  ~recursion_tracker();

  // Null means recursion is too deep. Must not print this object.
  // Otherwise initially it points to kNone.
  hook_t* hook() { return obj_ ? &hook_ : nullptr; }

  // Null means there is no node with the same object up the stack. This is the
  // most common case. Otherwise *prev_hook() says which user-defined hook we
  // used the last time we attempted to print this object (it can be kNone).
  const hook_t* prev_hook() const { return prev_hook_; }

 private:
  ::size_t type_id_;
  // Null means recursion is too deep.
  const void* obj_;
  hook_t hook_;
  // Can be null.
  const hook_t* prev_hook_;
  // The next object in the singly-lined list. Null if we are the last node.
  const recursion_tracker* next_;
};

}  // namespace internal_streamable

// The default writer capable of producing human readable representation for all
// types.
template <class Derived = void>
struct default_writer_t {
  template <class T>
  void operator()(::std::ostream& stream, const T& value) const {
    // Apply decay in case T is a function (can't take its address and cast it
    // to void*) and in case T is a scalar. The latter is to enable printing of
    // volatile scalars.
    using R =
        ::std::conditional_t<::std::is_object_v<T> && !::std::is_scalar_v<T>,
                             const T&, ::std::decay_t<T>>;
    using Obj = ::std::remove_reference_t<R>;
    // Printing of non-scalar volatile objects is disallowed.
    static_assert(!::std::is_volatile_v<Obj>, "Cannot print this type");
    R obj = value;
    internal_streamable::recursion_tracker rec(gtl::FastTypeId<T>(),
                                               ::std::addressof(obj));
    if (!rec.hook()) {
      // There are too many instances of default_writer_t::operator() on the
      // stack. Probably infinite recursion.
      stream << "<recursion-depth-limit>";
      return;
    }
    if (rec.prev_hook() && *rec.prev_hook() == internal_streamable::kNone) {
      // This happens in two cases:
      //
      //   1. `value` is a recursive data structure.
      //   2. PrintTo(value, &stream, writer) evaluates to writer(stream,
      //      value).
      stream << "<recursive>";
      return;
    }

    const Derived* writer = static_cast<const Derived*>(this);
    using internal_streamable::printer;
    switch (rec.prev_hook() ? *rec.prev_hook() + 1
                            : internal_streamable::kFirst) {
      case internal_streamable::kPrintTo:
        printer<Derived, internal_streamable::kPrintTo>(&stream, writer,
                                                        rec.hook())
            .print(obj, rank_selector);
        break;
      case internal_streamable::kOStream:
        printer<Derived, internal_streamable::kOStream>(&stream, writer,
                                                        rec.hook())
            .print(obj, rank_selector);
        break;
      case internal_streamable::kUnparseFlag:
        printer<Derived, internal_streamable::kUnparseFlag>(&stream, writer,
                                                            rec.hook())
            .print(obj, rank_selector);
        break;
      case internal_streamable::kDebugString:
        printer<Derived, internal_streamable::kDebugString>(&stream, writer,
                                                            rec.hook())
            .print(obj, rank_selector);
        break;
      case internal_streamable::kAbslStringify:
        printer<Derived, internal_streamable::kAbslStringify>(&stream, writer,
                                                              rec.hook())
            .print(obj, rank_selector);
        break;
      case internal_streamable::kNone:
        printer<Derived, internal_streamable::kNone>(&stream, writer,
                                                     rec.hook())
            .print(obj, rank_selector);
        break;
    }
  }
};

template <>
struct default_writer_t<> : default_writer_t<default_writer_t<>> {};

extern const default_writer_t<> default_writer;

template <class T, class Writer>
class streamable_t {
 public:
  streamable_t(const T* obj, const Writer* writer)
      : obj_(*obj), writer_(*writer) {}

  friend ::std::ostream& operator<<(::std::ostream& stream,
                                    const streamable_t& val) {
    val.writer_(stream, val.obj_);
    return stream;
  }

 private:
  const T& obj_;
  const Writer& writer_;
};

// Adaptor for logging objects of any type.
//
//   tuple<int, std::string> t(42, "hello");
//   LOG(INFO) << streamable(t);  // Prints: {42, "hello"}.
//
template <class T>
streamable_t<T, default_writer_t<>> streamable(const T& obj) {
  return {&obj, &default_writer};
}

// This version supports custom serialization. Writer should be compatible with
// void (ostream&, const T&) const, where T is any type. In other words, writer
// should be callable with two arguments: the first argument of type ostream&
// and the second argument of any type.
//
// See comments at the top of the file for examples.
template <class T, class Writer>
streamable_t<T, Writer> streamable(const T& obj, const Writer& writer) {
  return {&obj, &writer};
}

// Returns a human readable string representation of the object.
template <class T>
::std::string to_string(const T& obj) {
  ::std::string res;
  ::strings::OStringStream stream(&res);
  stream << ::util::tuple::streamable(obj);
  return res;
}

// This version supports custom serialization. Writer should be compatible with
// void (ostream&, const T&) const, where T is any type. In other words, writer
// should be callable with two arguments: the first argument of type ostream&
// and the second argument of any type.
//
// See comments at the top of the file for examples.
template <class T, class Writer>
::std::string to_string(const T& obj, const Writer& writer) {
  ::std::string res;
  ::strings::OStringStream stream(&res);
  stream << ::util::tuple::streamable(obj, writer);
  return res;
}

struct strappend_t {
  template <class T>
  void operator()(::std::string* out, const T& t) const {
    ::strings::OStringStream stream(out);
    stream << ::util::tuple::streamable(t);
  }
};

// This version appends to an existing string:
//   std::string s;
//   strappend(&s, 4);
//   strappend(&s, 2);
//   assert(s == "42");
extern const strappend_t strappend;

}  // namespace util::tuple

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_STREAMABLE_H_
