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

#include "gloop/util/gtl/extend/stream_printing.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "absl/types/span.h"
#include "gloop/util/gtl/extend/extend.h"
#include "gloop/util/gtl/extend/stringification_tests.h"
#include "gloop/util/gtl/generic_printer.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using gtl::internal_extend::FieldSpec;
using gtl::internal_extend::MatchesFieldSpec;
using gtl::internal_extend::MatchesUsingRE2;

using OneField = gtl::internal_extend::OneField<gtl::StreamPrintingExtension>;
using ManyFields =
    gtl::internal_extend::ManyFields<gtl::StreamPrintingExtension>;
using MultipleExtends =
    gtl::internal_extend::MultipleExtends<gtl::StreamPrintingExtension>;
using Nested = gtl::internal_extend::Nested<gtl::StreamPrintingExtension>;
using Streamer = gtl::internal_extend::Streamer;
template <typename T>
using Template =
    gtl::internal_extend::Template<T, gtl::StreamPrintingExtension>;

TEST(StreamPrinting, OneField) {
  if constexpr (!MatchesUsingRE2()) {
    GTEST_SKIP() << "Test assumes RE2 syntax, but RE2 is not being used";
  }

  Streamer printer;
  EXPECT_THAT(printer(OneField{{}, 3}), MatchesFieldSpec({{"num", "3"}}));
  EXPECT_THAT(printer(OneField{{}, 4}), MatchesFieldSpec({{"num", "4"}}));
}

TEST(StreamPrinting, ManyFields) {
  if constexpr (!MatchesUsingRE2()) {
    GTEST_SKIP() << "Test assumes RE2 syntax, but RE2 is not being used";
  }

  Streamer printer;
  EXPECT_THAT(printer(ManyFields{}), MatchesFieldSpec({
                                         {"num", "3"},
                                         {"b", "true"},
                                         {"message", R"("hello")"},
                                     }));
  EXPECT_THAT(printer(ManyFields{{}, 3, true, "some-\"other\"-message"}),
              MatchesFieldSpec({
                  {"num", "3"},
                  {"b", "true"},
                  {"message", R"("some-\\"other\\"-message")"},
              }));
  EXPECT_THAT(printer(ManyFields{{}, 3, true, "\"hello\""}),
              MatchesFieldSpec({
                  {"num", "3"},
                  {"b", "true"},
                  {"message", R"("\\"hello\\"")"},
              }));
}

TEST(StreamPrinting, Nested) {
  if constexpr (!MatchesUsingRE2()) {
    GTEST_SKIP() << "Test assumes RE2 syntax, but RE2 is not being used";
  }

  Streamer printer;
  EXPECT_THAT(printer(Nested{{}, 1, {}}), MatchesFieldSpec({
                                              {"num", "1"},
                                              {"fields",
                                               std::vector<FieldSpec>{
                                                   {"num", "3"},
                                                   {"b", "true"},
                                                   {"message", R"("hello")"},
                                               }},
                                          }));
  EXPECT_THAT(printer(Nested{{}, 3, {{}, 3, true}}),
              MatchesFieldSpec({
                  {"num", "3"},
                  {"fields",
                   std::vector<FieldSpec>{
                       {"num", "3"},
                       {"b", "true"},
                       {"message", R"("hello")"},
                   }},
              }));

  EXPECT_THAT(printer(Nested{{}, 3, {{}, 4, false, "hello"}}),
              MatchesFieldSpec({
                  {"num", "3"},
                  {"fields",
                   std::vector<FieldSpec>{
                       {"num", "4"},
                       {"b", "false"},
                       {"message", R"("hello")"},
                   }},
              }));
}

TEST(StreamPrinting, Template) {
  if constexpr (!MatchesUsingRE2()) {
    GTEST_SKIP() << "Test assumes RE2 syntax, but RE2 is not being used";
  }

  Streamer printer;
  EXPECT_THAT(printer(Template<int>{{}, 3}), MatchesFieldSpec({{"val", "3"}}));

  EXPECT_THAT(printer(Template<int>{{}, 4}), MatchesFieldSpec({{"val", "4"}}));

  EXPECT_THAT(printer(Template<double>{{}, 3.1}),
              MatchesFieldSpec({{"val", "3.1"}}));
  EXPECT_THAT(printer(Template<double>{{}, 4.1}),
              MatchesFieldSpec({{"val", "4.1"}}));
}

TEST(StreamPrinting, MultipleExtends) {
  if constexpr (!MatchesUsingRE2()) {
    GTEST_SKIP() << "Test assumes RE2 syntax, but RE2 is not being used";
  }

  Streamer printer;
  EXPECT_THAT(printer(MultipleExtends{{}, 7, 11}),
              MatchesFieldSpec({{"x", "7"}, {"y", "11"}}));
}

template <template <typename> typename T>
struct TemplateTemplate : gtl::Extend<TemplateTemplate<T>>::template With<
                              gtl::StreamPrintingExtension> {
  T<int> n;
  T<bool> b;
};

TEST(StreamPrinting, TemplateTemplate) {
  if constexpr (!MatchesUsingRE2()) {
    GTEST_SKIP() << "Test assumes RE2 syntax, but RE2 is not being used";
  }

  Streamer printer;
  EXPECT_THAT(printer(TemplateTemplate<Template>{{}, {{}, 3}, {{}, true}}),
              MatchesFieldSpec({
                  {"n", std::vector<FieldSpec>{{"val", "3"}}},
                  {"b", std::vector<FieldSpec>{{"val", "true"}}},
              }));
  EXPECT_THAT(printer(TemplateTemplate<Template>{{}, {{}, 4}, {{}, false}}),
              MatchesFieldSpec({
                  {"n", std::vector<FieldSpec>{{"val", "4"}}},
                  {"b", std::vector<FieldSpec>{{"val", "false"}}},
              }));
}

class Class : public gtl::Extend<Class, 1>::With<gtl::StreamPrintingExtension> {
 public:
  explicit Class(int n) : n_(n) {}

 private:
  friend gtl::EnableExtensions;
  int n_;
};

TEST(StreamPrinting, Class) {
  if constexpr (!MatchesUsingRE2()) {
    GTEST_SKIP() << "Test assumes RE2 syntax, but RE2 is not being used";
  }

  Streamer printer;
  EXPECT_THAT(printer(Class(3)), MatchesFieldSpec({{"n_", "3"}}));
  EXPECT_THAT(printer(Class(4)), MatchesFieldSpec({{"n_", "4"}}));
}

struct Complicated
    : gtl::Extend<Complicated, 1>::With<gtl::StreamPrintingExtension> {
  std::unique_ptr<int> n;
};

TEST(StreamPrinting, Complicated) {
  if constexpr (!MatchesUsingRE2()) {
    GTEST_SKIP() << "Test assumes RE2 syntax, but RE2 is not being used";
  }

  Streamer printer;
  EXPECT_THAT(printer(Complicated{}), MatchesFieldSpec({{"n", ".*nullptr.*"}}));

  Complicated to_print{{}, std::make_unique<int>(321)};
  std::stringstream expected;
  expected << gtl::GenericPrint(to_print.n);

  EXPECT_THAT(printer(to_print), MatchesFieldSpec({{"n", expected.str()}}));
}

struct EmptyStruct
    : gtl::Extend<EmptyStruct>::With<gtl::StreamPrintingExtension> {};

TEST(StreamPrinting, EmptyStruct) {
  if constexpr (!MatchesUsingRE2()) {
    GTEST_SKIP() << "Test assumes RE2 syntax, but RE2 is not being used";
  }

  Streamer printer;
  EXPECT_THAT(printer(EmptyStruct{}), testing::MatchesRegex(R"(\s*{\s*}\s*)"));
}

}  // namespace
