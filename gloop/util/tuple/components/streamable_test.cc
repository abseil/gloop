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

#include "gloop/util/tuple/components/streamable.h"

#include <stddef.h>

#include <cstdint>
#include <functional>
#include <ios>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/casts.h"
#include "absl/base/macros.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/util/gtl/extend/extend.h"
#include "gloop/util/tuple/components/streamable_test.pb.h"
#include "gloop/util/tuple/struct.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

ABSL_FLAG(std::string, streamable_test_flag, "hel\"lo", "Test flag");

namespace util::tuple {
namespace {

using ::testing::HasSubstr;
using ::testing::MatchesRegex;
using ::testing::StrEq;

struct Recursive {
  Recursive() : self(this, [](Recursive*) {}) {}

  TUPLE_DEFINE_STRUCT(Recursive, (), (::std::shared_ptr<Recursive>, self));
};

struct Incomplete;

struct Unprintable {
  int32_t value;
};

struct Class {
  void Method() {}

  int data;
};

struct HasPadding {
  const int i = 0;
  const bool b = false;
};

void Function() {}

enum class Color { kBlack = 42 };

enum Shape { kSquare = 42 };

struct WithAdvancedPrintTo {
  template <class Writer>
  friend void PrintTo(const WithAdvancedPrintTo&, ::std::ostream* stream,
                      const Writer& writer) {
    writer(*stream, "WithAdvancedPrintTo");
  }
};

struct WithRecursiveAdvancedPrintTo {
  template <class Writer>
  friend void PrintTo(const WithRecursiveAdvancedPrintTo& obj,
                      ::std::ostream* stream, const Writer& writer) {
    writer(*stream, obj);
  }
};

struct WithInfiniteDepth {
  template <class Writer>
  friend void PrintTo(WithInfiniteDepth obj, ::std::ostream* stream,
                      const Writer& writer) {
    // Note that we accept obj by value, which means we are passing writer a
    // different object on every invocation.
    writer(*stream, obj);
  }
};

struct WithPrintTo {
  friend void PrintTo(const WithPrintTo&, ::std::ostream* stream) {
    *stream << "WithPrintTo";
  }

  friend ::std::ostream& operator<<(::std::ostream& stream,
                                    const WithPrintTo& obj) {
    return stream << streamable(obj);
  }
  friend ::std::string AbslUnparseFlag(const WithPrintTo& obj) {
    return to_string(obj);
  }
  ::std::string DebugString() const { return to_string(*this); }
};

struct WithOstream {
  friend ::std::ostream& operator<<(::std::ostream& stream,
                                    const WithOstream&) {
    return stream << "WithOstream";
  }

  friend void PrintTo(const WithOstream& obj, ::std::ostream* stream) {
    *stream << streamable(obj);
  }
  friend ::std::string AbslUnparseFlag(const WithOstream& obj) {
    return to_string(obj);
  }
  ::std::string DebugString() const { return to_string(*this); }
};

struct WithUnparseFlag {
  friend ::std::string AbslUnparseFlag(const WithUnparseFlag&) {
    return "WithUnparseFlag";
  }

  friend void PrintTo(const WithUnparseFlag& obj, ::std::ostream* stream) {
    *stream << streamable(obj);
  }
  friend ::std::ostream& operator<<(::std::ostream& stream,
                                    const WithUnparseFlag& obj) {
    return stream << streamable(obj);
  }
  ::std::string DebugString() const { return to_string(*this); }
};

struct WithDebugString {
  ::std::string DebugString() const { return "WithDebugString"; }

  friend void PrintTo(const WithDebugString& obj, ::std::ostream* stream) {
    *stream << streamable(obj);
  }
  friend ::std::ostream& operator<<(::std::ostream& stream,
                                    const WithDebugString& obj) {
    return stream << streamable(obj);
  }
  friend ::std::string AbslUnparseFlag(const WithDebugString& obj) {
    return to_string(obj);
  }
};

struct WithImaginaryHook {
  friend void PrintTo(const WithImaginaryHook& obj, ::std::ostream* stream) {
    *stream << streamable(obj);
  }
  friend ::std::ostream& operator<<(::std::ostream& stream,
                                    const WithImaginaryHook& obj) {
    return stream << streamable(obj);
  }
  friend ::std::string AbslUnparseFlag(const WithImaginaryHook& obj) {
    return to_string(obj);
  }
  ::std::string DebugString() const { return to_string(*this); }
};

struct OstreamableTuple {
  TUPLE_DEFINE_STRUCT(OstreamableTuple, ());

  // Streams a hardcoded string. Marked unused because tuple printing should
  // take priority over this operator.
  friend ::std::ostream& operator<<
      [[maybe_unused]] (::std::ostream& stream, const OstreamableTuple& obj) {
    return stream << "OstreamableTuple";
  }
};

class WeirdString : public absl::string_view {
 public:
  explicit WeirdString(absl::string_view s) : s_(s) { Fix(); }
  WeirdString(const WeirdString& other) : s_(other.s_) { Fix(); }
  WeirdString& operator=(const WeirdString& other) {
    s_ = other.s_;
    Fix();
    return *this;
  }

 private:
  void Fix() { absl::string_view::operator=(s_); }

  std::string s_;
};

struct ConvertibleToWeirdString {
  operator WeirdString() const { return WeirdString(s); }
  ::std::string s;
};

class WithAbslStringify {
 public:
  // Not copyable, not movable, not default-constructible, just to make sure
  // streamable type deduction doesn't rely on any of these.
  explicit WithAbslStringify(int x) : x_(x) {}
  WithAbslStringify(const WithAbslStringify&) = delete;
  WithAbslStringify& operator=(const WithAbslStringify&) = delete;
  WithAbslStringify(WithAbslStringify&&) = delete;
  WithAbslStringify& operator=(WithAbslStringify&&) = delete;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const WithAbslStringify& x) {
    absl::Format(&sink, "Stingified=%d", x.x_);
  }

 private:
  int x_;
};

// Some code uses the length, char overflow of the sink's Append.
class WithAbslStringifyRepeatStars {
 public:
  explicit WithAbslStringifyRepeatStars(int length) : length_(length) {}

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const WithAbslStringifyRepeatStars& x) {
    sink.Append(x.length_, '*');
  }

 private:
  int length_;
};

TEST(Tuple, Empty) { EXPECT_EQ(to_string(::std::tuple()), "{}"); }

TEST(Tuple, OneElement) { EXPECT_EQ(to_string(::std::tuple(42)), "{42}"); }

TEST(Tuple, TwoElements) {
  EXPECT_EQ(to_string(::std::tuple(42, 24)), "{42, 24}");
}

TEST(Tuple, OstreamableTuple) {
  // Verify that user defined operator<< has lower priority than tuple
  // printing.
  EXPECT_EQ(to_string(OstreamableTuple()), "{}");
}

TEST(Char, Basic) {
  EXPECT_EQ(to_string('A'), R"('A')");
  EXPECT_EQ(to_string('\n'), R"('\n')");
  EXPECT_EQ(to_string('\377'), R"('\377')");
}

TEST(SignedChar, Basic) {
  EXPECT_EQ(to_string(absl::implicit_cast<signed char>(0x00)), R"(0x00)");
  EXPECT_EQ(to_string(absl::implicit_cast<signed char>(0x07)), R"(0x07)");
  EXPECT_EQ(to_string(absl::implicit_cast<signed char>(-1)), R"(0xFF)");
}

TEST(UnsignedChar, Basic) {
  EXPECT_EQ(to_string(absl::implicit_cast<unsigned char>(0x00)), R"(0x00)");
  EXPECT_EQ(to_string(absl::implicit_cast<unsigned char>(0x07)), R"(0x07)");
  EXPECT_EQ(to_string(absl::implicit_cast<unsigned char>(0xFF)), R"(0xFF)");
}

TEST(Bool, Basic) {
  EXPECT_EQ(to_string(true), "true");
  EXPECT_EQ(to_string(false), "false");
}

TEST(StringLiteral, Basic) {
  EXPECT_EQ(to_string("hel\000lo"), R"("hel\000lo")");
  {
    char c[] = "hello";
    EXPECT_EQ(to_string(c), R"("hello")");
  }
  {
    const char c[] = "hello";
    EXPECT_EQ(to_string(c), R"("hello")");
  }
  {
    char c[] = {'h', 'e', 'l', 'l', 'o'};
    EXPECT_EQ(to_string(c), "['h', 'e', 'l', 'l', 'o']");
  }
}

TEST(StringPiece, Basic) {
  const char c[] = "hel\000lo";
  EXPECT_EQ(to_string(absl::string_view(c, ABSL_ARRAYSIZE(c) - 1)),
            R"("hel\000lo")");
  EXPECT_EQ(to_string(absl::string_view("")), R"("")");
  EXPECT_EQ(to_string(absl::string_view()), R"(nullptr)");
}

TEST(StringPiece, NonAscii) {
  EXPECT_EQ(to_string(absl::string_view("\377")), R"("\377")");
}

TEST(String, Basic) {
  const char c[] = "hel\000lo";
  EXPECT_EQ(to_string(::std::string(c, ABSL_ARRAYSIZE(c) - 1)),
            R"("hel\000lo")");
}

TEST(StdString, Basic) {
  const char c[] = "hel\000lo";
  EXPECT_EQ(to_string(::std::string(c, ABSL_ARRAYSIZE(c) - 1)),
            R"("hel\000lo")");
}

TEST(Cord, Basic) {
  const char c[] = "hel\000lo";
  absl::string_view s(c, ABSL_ARRAYSIZE(c) - 1);
  EXPECT_EQ(to_string(absl::Cord(s)), R"("hel\000lo")");
}

TEST(CharPtr, Basic) {
  char a = 42;
  unsigned char b = 42;
  signed char c = 42;
  EXPECT_THAT(to_string(&a), MatchesRegex("0x[0-9a-f]+"));
  EXPECT_THAT(to_string(&b), MatchesRegex("0x[0-9a-f]+"));
  EXPECT_THAT(to_string(&c), MatchesRegex("0x[0-9a-f]+"));
}

TEST(SmartPointer, Basic) {
  auto p = ::std::make_unique<int>(42);
  EXPECT_THAT(to_string(p), MatchesRegex("0x[0-9a-f]+ pointing to 42"));
}

TEST(SmartPointer, ToVoid) {
  ::std::shared_ptr<void> p(new int(42));
  EXPECT_THAT(to_string(p), MatchesRegex("0x[0-9a-f]+"));
}

TEST(SmartPointer, ToFunction) {
  struct D {
    void operator()(void (*)()) const {}
  };
  ::std::unique_ptr<void(), D> p(+[] {});
  EXPECT_THAT(to_string(p), MatchesRegex("0x[0-9a-f]+"));
}

TEST(Recursion, Basic) {
  EXPECT_THAT(to_string(Recursive()),
              MatchesRegex("[{]self = 0x[0-9a-f]+ pointing to <recursive>[}]"));
  EXPECT_EQ(to_string(WithRecursiveAdvancedPrintTo()), "<recursive>");
  EXPECT_EQ(to_string(WithInfiniteDepth()), "<recursion-depth-limit>");
}

TEST(EnumClass, Basic) { EXPECT_EQ(to_string(Color::kBlack), "42"); }

TEST(Enum, Basic) { EXPECT_EQ(to_string(kSquare), "42"); }

TEST(Proto, Empty) {
  TestProto obj;
  auto s = to_string(obj);
  EXPECT_THAT(to_string(obj), StrEq("<>"));
}

TEST(Proto, RedactShortFormat) {
  // The format of redacted fields is not specified. Here we test that the
  // string contains the relevant information but not the redacted information
  // without testing the format of the string.
  TestProto obj;
  obj.set_foo("hello");
  obj.set_bar(42);
  obj.set_redact("SECRET");
  std::string s = to_string(obj);
  EXPECT_THAT(s, HasSubstr("foo: \"hello\""));
  EXPECT_THAT(s, HasSubstr("bar: 42"));
  EXPECT_THAT(s, Not(HasSubstr("SECRET")));
}

TEST(Proto, RedactLongFormat) {
  // The format of redacted fields is not specified. Here we test that the
  // string contains the relevant information but not the redacted information
  // without testing the format of the string.
  TestProto obj;
  obj.set_foo(std::string(200, 'x'));
  obj.set_bar(42);
  obj.set_redact("SECRET");
  std::string s = to_string(obj);
  EXPECT_THAT(s, HasSubstr(std::string(200, 'x')));
  EXPECT_THAT(s, HasSubstr("bar: 42"));
  EXPECT_THAT(s, Not(HasSubstr("SECRET")));
}

TEST(Optional, Basic) {
  using ::std::nullopt;
  using ::std::optional;
  EXPECT_EQ(to_string(optional<std::string>("hello")), R"(["hello"])");
  EXPECT_EQ(to_string(optional<int>()), R"(nullopt)");
  EXPECT_EQ(to_string(nullopt), R"(nullopt)");
  EXPECT_EQ(to_string(optional<optional<int>>()), R"(nullopt)");
  EXPECT_EQ(to_string(optional<optional<int>>(optional<int>())),
            R"([nullopt])");
}

TEST(ReferenceWrapper, Basic) {
  const char c[] = "hel\000lo";
  EXPECT_EQ(to_string(::std::ref(c)), R"("hel\000lo")");
  EXPECT_EQ(to_string(::std::cref(c)), R"("hel\000lo")");
}

struct Foo {
  TUPLE_DEFINE_STRUCT(Foo, (ostream), (::std::reference_wrapper<int>, value));
};

TEST(ReferenceWrapper, OfTupleDefineStruct) {
  int v = 1;
  Foo foo = {::std::ref(v)};
  EXPECT_EQ(to_string(::std::cref(foo)), "{value = 1}");
}

TEST(Flag, Basic) {
  EXPECT_EQ(to_string(FLAGS_streamable_test_flag), R"("hel\"lo")");
}

TEST(Nullptr, Basic) { EXPECT_EQ(to_string(nullptr), "nullptr"); }

TEST(PrintTo, Basic) { EXPECT_EQ(to_string(WithPrintTo()), "WithPrintTo"); }

TEST(AdvancedPrintTo, Basic) {
  EXPECT_EQ(to_string(WithAdvancedPrintTo()), R"("WithAdvancedPrintTo")");
}

TEST(DefaultPolicy, VectorOfBool) {
  ::std::vector<bool> v = {true, false};
  EXPECT_EQ(to_string(v), "[true, false]");
}

struct EmotionalWriter : default_writer_t<EmotionalWriter> {
  template <class Elem>
  void operator()(::std::ostream& stream, const Elem& elem) const {
    default_writer_t<EmotionalWriter>::operator()(stream, elem);
    stream << "!";
  }
};

TEST(CustomWriter, TupleOfTuples) {
  // Verify that the policy is propagated recursively to inner tuples.
  EXPECT_EQ(to_string(::std::make_tuple(::std::tuple(42)), EmotionalWriter()),
            "{{42!}!}!");
}

TEST(Function, Basic) {
  EXPECT_THAT(to_string(Function), MatchesRegex("0x[0-9a-f]+"));
}

TEST(FunctionPtr, Basic) {
  EXPECT_THAT(to_string(&Function), MatchesRegex("0x[0-9a-f]+"));
}

TEST(MemberFunction, Basic) {
  EXPECT_THAT(to_string(&Class::Method), MatchesRegex("0x[0-9a-f]+"));
}

TEST(DataMember, Basic) {
  EXPECT_THAT(to_string(&Class::data), MatchesRegex("0x[0-9a-f]+"));
}

TEST(DebugString, Basic) {
  EXPECT_EQ(to_string(WithDebugString()), "WithDebugString");
}

TEST(ExternalFlags, Basic) {
  ::std::ostringstream stream;
  stream << ::std::hex << streamable(42) << " " << 42;
  EXPECT_EQ(stream.str(), "42 2a");
}

TEST(UnparseFlag, Basic) {
  EXPECT_EQ(to_string(WithUnparseFlag()), "WithUnparseFlag");
}

TEST(Incomplete, Basic) {
  char c = 0;
  EXPECT_EQ(to_string(reinterpret_cast<const Incomplete&>(c)), "<incomplete>");
}

TEST(Unprintable, Basic) {
  static_assert(sizeof(Unprintable) == 4, "");
  EXPECT_EQ(to_string(Unprintable{0x012345678}), "4-byte object <78-56 34-12>");
  EXPECT_EQ(to_string(WithImaginaryHook()), "<recursive>");
}

TEST(ProtoEnum, Basic) {
  EXPECT_EQ(to_string(TestProto::COLOR_BLACK), "COLOR_BLACK (0)");
  EXPECT_EQ(to_string(TestProto::COLOR_WHITE), "COLOR_WHITE (2)");
  EXPECT_EQ(to_string(static_cast<TestProto::Color>(1)), "1");
}

TEST(Oneof, Basic) {
  ::std::variant<::std::monostate, int, double> a = 42;
  ::std::variant<::std::monostate, int, double> b = 1.5;
  ::std::variant<::std::monostate, int, double> c;
#ifdef UTIL_TUPLE_COMPONENTS_HAVE_UTIL_SYMBOLIZE
  EXPECT_EQ(to_string(a), "(int)42");
  EXPECT_EQ(to_string(b), "(double)1.5");
  EXPECT_THAT(to_string(c), MatchesRegex(R"regexp(\(.*::monostate\))regexp"));
#else
  EXPECT_EQ(to_string(a), "42");
  EXPECT_EQ(to_string(b), "1.5");
  EXPECT_EQ(to_string(c), "");
#endif
}

TEST(DerivedFromStringPiece, Basic) {
  const char c[] = "hel\000lo";
  WeirdString s(::std::string(c, ABSL_ARRAYSIZE(c) - 1));
  EXPECT_EQ(to_string(s), R"("hel\000lo")");
}

TEST(ConvertibleToStringPiece, Basic) {
  const char c[] = "hel\000lo";
  ConvertibleToWeirdString s{::std::string(c, ABSL_ARRAYSIZE(c) - 1)};
  EXPECT_EQ(to_string(s), R"("hel\000lo")");
}

TEST(IntegralConstant, Basic) {
  EXPECT_EQ(to_string(::std::true_type()), "true");
  EXPECT_EQ(to_string(::std::integral_constant<int, 42>()), "42");
}

TEST(Volatile, Basic) {
  volatile int n = 42;
  volatile int* p = &n;
  EXPECT_THAT(to_string(n), MatchesRegex("42"));
  EXPECT_THAT(to_string(p), MatchesRegex("0x[0-9a-f]+"));
}

TEST(StrAppend, Basic) {
  ::std::string output = "prefix ";
  const char c[] = "hel\000lo";
  strappend(&output, c);
  EXPECT_EQ(output, R"(prefix "hel\000lo")");
}

struct Endl {
  friend ::std::ostream& operator<<(::std::ostream& stream, Endl) {
    return stream << ::std::endl;
  }
};

TEST(StrAppend, Endl) {
  ::std::string output;
  strappend(&output, Endl());
  EXPECT_EQ(output, "\n");
}

struct CustomUnprintableWriter : default_writer_t<CustomUnprintableWriter> {
  template <class T>
  void operator()(::std::ostream& stream, const unprintable<T>&) const {
    stream << "gotcha";
  }
  using default_writer_t<CustomUnprintableWriter>::operator();
};

TEST(Unprintable, CustomWriter) {
  char c = 0;
  EXPECT_EQ(to_string(reinterpret_cast<const Incomplete&>(c),
                      CustomUnprintableWriter()),
            "gotcha");
  EXPECT_EQ(to_string(Unprintable(), CustomUnprintableWriter()), "gotcha");
  EXPECT_EQ(
      to_string(::std::make_pair(42, Unprintable()), CustomUnprintableWriter()),
      "{42, gotcha}");
}

TEST(Unprintable, StructWithPaddingPassesMsan) {
  LOG(INFO) << to_string(HasPadding());
}

TEST(WithAbslStringify, Basic) {
  WithAbslStringify foo(111);
  EXPECT_EQ(to_string(foo), "Stingified=111");
}

TEST(WithAbslStringify, RepeatStars) {
  WithAbslStringifyRepeatStars foo(5);
  EXPECT_EQ(to_string(foo), "*****");
}

}  // namespace
}  // namespace util::tuple
