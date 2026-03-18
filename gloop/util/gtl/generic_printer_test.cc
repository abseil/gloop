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

#include "gloop/util/gtl/generic_printer.h"

#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/internal/generic_printer.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/optional_ref.h"
#include "gloop/util/gtl/extend/extend.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace generic_logging_test {
struct NotStreamable {};
}  // namespace generic_logging_test

static std::ostream& operator<<(std::ostream& os,
                                const generic_logging_test::NotStreamable&) {
  return os << "This overload should NOT be found by GenericPrint.";
}

// Types to test selection logic for streamable and non-streamable types.
namespace generic_logging_test {
struct Streamable {
  int x;
  friend std::ostream& operator<<(std::ostream& os, const Streamable& l) {
    return os << "Streamable{" << l.x << "}";
  }
};
}  // namespace generic_logging_test

namespace {

using ::testing::AllOf;
using ::testing::ContainsRegex;
using ::testing::HasSubstr;
using ::testing::MatchesRegex;

struct AbslStringifiable {
  template <typename S>
  friend void AbslStringify(S& sink, const AbslStringifiable&) {
    sink.Append("AbslStringifiable!");
  }
};

template <typename T>
std::string GenericPrintToString(const T& v) {
  std::stringstream ss;
  ss << gtl::GenericPrint(v);
  {
    std::stringstream ss2;
    ss2 << gtl::GenericPrint() << v;
    EXPECT_EQ(ss.str(), ss2.str());
  }
  return ss.str();
}

TEST(GenericPrinterTest, NotStreamableWithoutGenericPrint) {
  generic_logging_test::NotStreamable x;
  std::stringstream ss;
  ss << x;
  EXPECT_EQ(ss.str(), "This overload should NOT be found by GenericPrint.");
}

TEST(GenericPrinterTest, NotStreamableLvalue) {
  generic_logging_test::NotStreamable x;
  EXPECT_THAT(
      GenericPrintToString(x),
      MatchesRegex(
          "\\[unprintable value of size [[:digit:]]+ @0x[[:xdigit:]]+\\]"));
}

TEST(GenericPrinterTest, NotStreamableXvalue) {
  EXPECT_THAT(
      GenericPrintToString(generic_logging_test::NotStreamable{}),
      MatchesRegex(
          "\\[unprintable value of size [[:digit:]]+ @0x[[:xdigit:]]+\\]"));
}

TEST(GenericPrinterTest, StreamAdapter) {
  std::stringstream ss;
  static_assert(
      std::is_same<typename std::remove_reference<
                       decltype(ss << gtl::GenericPrint())>::type,
                   absl::internal_generic_printer::GenericPrintStreamAdapter::
                       Impl<std::stringstream>>::value,
      "expected ostream << gtl::GenericPrint() to yield adapter impl");

  ss << gtl::GenericPrint() << "again, " << "back-up, " << "cue, "
     << "double-u, " << "eye, "
     << "four: " << generic_logging_test::NotStreamable{};
  EXPECT_THAT(
      ss.str(),
      MatchesRegex(
          "again, back-up, cue, double-u, eye, four: .unprintable value.*"));
}

TEST(GenericPrinterTest, OptionalRef) {
  EXPECT_EQ("nullopt", GenericPrintToString(absl::optional_ref<int>()));
  EXPECT_EQ("nullopt",
            GenericPrintToString(absl::optional_ref<int>(std::nullopt)));
  EXPECT_EQ("<3>", GenericPrintToString(absl::optional_ref(3)));
  EXPECT_EQ("<Streamable{3}>", GenericPrintToString(absl::optional_ref(
                                   generic_logging_test::Streamable{3})));
}

}  // namespace
