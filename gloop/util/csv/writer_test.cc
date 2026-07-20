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

#include "gloop/util/csv/writer.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/fixed_array.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/strings/bytestream.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace util {
namespace csv {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using std::list;
using std::map;
using std::vector;
using strings::ByteSink;
using strings::StringByteSink;
using ::testing::IsEmpty;
using ::util::csv::DefaultOrder;
using ::util::csv::HeaderOrder;
using ::util::csv::Writer;
using ::util::csv::WriteRecordToString;

// None of the common impls of ByteSink use flushing, but we need to
// verify proper flushing.
class FlushableStringByteSink : public ByteSink {
 public:
  explicit FlushableStringByteSink(std::string* dest) : dest_(dest) {}

  // This type is neither copyable nor movable.
  FlushableStringByteSink(const FlushableStringByteSink&) = delete;
  FlushableStringByteSink& operator=(const FlushableStringByteSink&) = delete;
  void Append(const char* data, size_t n) override { buffer_.append(data, n); }
  void Flush() override {
    dest_->append(buffer_);
    buffer_.clear();
  }

 private:
  std::string buffer_;
  std::string* dest_;
};

template <typename W, typename R>
absl::Status WriteRecord(W& writer, const R& record) {
  absl::Status s = writer.WriteRecord(record);
  if (s != writer.status()) {
    return absl::InternalError(absl::StrCat(
        "WriteRecord status (", s.ToString(),
        ") does not match writer.status() (", writer.status().ToString(), ")"));
  }
  return s;
}

template <class P>
class WriterTest : public ::testing::Test {
 protected:
  using sequence_type = typename P::sequence_type;
  using map_type = typename P::map_type;
};

template <class S>
struct StandardTypes {
  using sequence_type = std::vector<S>;
  using map_type = std::map<S, S>;
};

template <class S>
struct DifferentStandardTypes {
  using sequence_type = std::list<S>;
  using map_type = absl::flat_hash_map<S, S>;
};

template <class S>
struct AbslTypes {
  using sequence_type = absl::FixedArray<S>;
  using map_type = absl::flat_hash_map<S, S>;
};

template <class... S>
using AllTypes =
    ::testing::Types<StandardTypes<S>..., DifferentStandardTypes<S>...,
                     AbslTypes<S>...>;

using WriterParams = AllTypes<std::string,
                              // All test data consists of static strings,
                              // so this does not cause memory issues.
                              absl::string_view>;

TYPED_TEST_SUITE(WriterTest, WriterParams);

// test basic cases
TYPED_TEST(WriterTest, BasicCaseWithRawPointer) {
  vector<typename TestFixture::sequence_type> input{{"a", "b", "c"},
                                                    {"d", "e", "f"}};
  std::string expected(
      "a,b,c\n"
      "d,e,f\n");
  std::string actual;
  Writer<DefaultOrder> writer(3, DefaultOrder(), new StringByteSink(&actual));
  for (const auto& record : input) {
    EXPECT_THAT(WriteRecord(writer, record), IsOk());
  }
  EXPECT_EQ(actual, expected);
}

// test basic cases
TYPED_TEST(WriterTest, BasicCase) {
  vector<typename TestFixture::sequence_type> input{{"a", "b", "c"},
                                                    {"d", "e", "f"}};
  std::string expected(
      "a,b,c\n"
      "d,e,f\n");
  std::string actual;
  Writer<DefaultOrder> writer(3, DefaultOrder(),
                              std::make_unique<StringByteSink>(&actual));
  for (const auto& record : input) {
    EXPECT_THAT(WriteRecord(writer, record), IsOk());
  }
  EXPECT_EQ(actual, expected);
}

// test that quotes are escaped correctly
TYPED_TEST(WriterTest, QuoteEscaping) {
  vector<typename TestFixture::sequence_type> input(
      {{"\"quote\" at start", "end with \"quote\"", "a \"quote\" in mid"},
       {"\"consecutive\"\"quotes\"", "\"", "no quotes"}});
  std::string expected(
      "\"\"\"quote\"\" at start\","
      "\"end with \"\"quote\"\"\",\"a \"\"quote\"\" in mid\"\n"
      "\"\"\"consecutive\"\"\"\"quotes\"\"\",\"\"\"\",no quotes\n");
  std::string actual;
  Writer<DefaultOrder> writer(
      3, DefaultOrder(), std::make_unique<FlushableStringByteSink>(&actual));
  for (const auto& record : input) {
    EXPECT_THAT(WriteRecord(writer, record), IsOk());
  }
  EXPECT_EQ(actual, expected);
}

// test that empty fields are ok
TYPED_TEST(WriterTest, EmptyFields) {
  vector<typename TestFixture::sequence_type> input{{"", "b", "c"},
                                                    {"d", "e", ""}};
  std::string expected(
      ",b,c\n"
      "d,e,\n");
  std::string actual;
  Writer<DefaultOrder> writer(
      3, DefaultOrder(), std::make_unique<FlushableStringByteSink>(&actual));
  for (const auto& record : input) {
    EXPECT_THAT(WriteRecord(writer, record), IsOk());
  }
  EXPECT_EQ(actual, expected);
}

// test comma delimiter field quoting
TYPED_TEST(WriterTest, CommaFieldQuoting) {
  vector<typename TestFixture::sequence_type> input(
      {{"simple", "with, comma", " with white space"},
       {"one field on\ntwo lines", "with \" quote", "end of record 2"}});
  std::string expected(
      "simple,\"with, comma\", with white space\n"
      "\"one field on\n"
      "two lines\",\"with \"\" quote\",end of record 2\n");
  std::string actual;
  Writer<DefaultOrder> writer(
      3, DefaultOrder(), std::make_unique<FlushableStringByteSink>(&actual));
  for (const auto& record : input) {
    EXPECT_THAT(WriteRecord(writer, record), IsOk());
  }
  EXPECT_EQ(actual, expected);
}

// test tab delimiter field quoting
TYPED_TEST(WriterTest, TabFieldQuoting) {
  vector<typename TestFixture::sequence_type> input(
      {{"simple", "with\ttab", " with white space, and comma"},
       {"one field on\ntwo lines", "with \" quote", "end of record 2"}});
  std::string expected(
      "simple\t\"with\ttab\"\t with white space, and comma\n"
      "\"one field on\n"
      "two lines\"\t\"with \"\" quote\"\tend of record 2\n");
  std::string actual;
  Writer<DefaultOrder> writer(
      3, DefaultOrder(), '\t',
      std::make_unique<FlushableStringByteSink>(&actual));
  for (const auto& record : input) {
    EXPECT_THAT(WriteRecord(writer, record), IsOk());
  }
  EXPECT_EQ(actual, expected);
}

// test header order (header not written)
TYPED_TEST(WriterTest, HeaderOrderNoHeader) {
  vector<std::string> headers{"A", "B", "C"};
  vector<typename TestFixture::map_type> input(
      {{{"A", "a1"}, {"B", "b1"}, {"C", "c1"}},
       {{"A", "a2"}, {"B", "b2"}, {"C", "c2"}}});
  std::string expected(
      "a1,b1,c1\n"
      "a2,b2,c2\n");
  std::string actual;
  Writer<HeaderOrder> writer(
      3, HeaderOrder(headers),
      std::make_unique<FlushableStringByteSink>(&actual));
  for (const auto& record : input) {
    EXPECT_THAT(WriteRecord(writer, record), IsOk());
  }
  EXPECT_EQ(actual, expected);
}

// test header order (with header written)
TYPED_TEST(WriterTest, HeaderOrderWithHeader) {
  vector<std::string> headers{"A", "B", "C"};
  vector<typename TestFixture::map_type> input(
      {{{"A", "a1"}, {"B", "b1"}, {"C", "c1"}},
       {{"A", "a2"}, {"B", "b2"}, {"C", "c2"}}});
  std::string expected(
      "A,B,C\n"
      "a1,b1,c1\n"
      "a2,b2,c2\n");
  std::string actual;
  Writer<HeaderOrder> writer(
      3, HeaderOrder(headers),
      std::make_unique<FlushableStringByteSink>(&actual));
  EXPECT_THAT(WriteRecord(writer, headers), IsOk());
  for (const auto& record : input) {
    EXPECT_THAT(WriteRecord(writer, record), IsOk());
  }
  EXPECT_EQ(actual, expected);
}

// test default order write container diversity
TYPED_TEST(WriterTest, DefaultOrderDiversity) {
  typename TestFixture::sequence_type input1{"av", "bv", "cv"};
  list<std::string> input2{"al", "bl", "cl"};
  vector<absl::string_view> input3{"asp", "bsp", "csp"};
  vector<const char*> input4{"acp", "bcp", "ccp"};
  const char* input5[] = {"aacp", "bacp", "cacp"};
  std::string expected(
      "av,bv,cv\n"
      "al,bl,cl\n"
      "asp,bsp,csp\n"
      "acp,bcp,ccp\n"
      "aacp,bacp,cacp\n");
  std::string actual;
  Writer<DefaultOrder> writer(
      3, DefaultOrder(), std::make_unique<FlushableStringByteSink>(&actual));
  EXPECT_THAT(WriteRecord(writer, input1), IsOk());
  EXPECT_THAT(WriteRecord(writer, input2), IsOk());
  EXPECT_THAT(WriteRecord(writer, input3), IsOk());
  EXPECT_THAT(WriteRecord(writer, input4), IsOk());
  EXPECT_THAT(WriteRecord(writer, input5), IsOk());
  EXPECT_EQ(actual, expected);
}

// test header order write container diversity
TYPED_TEST(WriterTest, HeaderOrderDiversity) {
  vector<std::string> headers{"A", "B", "C"};
  typename TestFixture::sequence_type input1{"av", "bv", "cv"};
  list<std::string> input2{"al", "bl", "cl"};
  vector<absl::string_view> input3{"asp", "bsp", "csp"};
  vector<const char*> input4{"acp", "bcp", "ccp"};
  const char* input5[] = {"aacp", "bacp", "cacp"};
  typename TestFixture::map_type input6{{"A", "am"}, {"B", "bm"}, {"C", "cm"}};
  std::string expected(
      "av,bv,cv\n"
      "al,bl,cl\n"
      "asp,bsp,csp\n"
      "acp,bcp,ccp\n"
      "aacp,bacp,cacp\n"
      "am,bm,cm\n");
  std::string actual;
  Writer<HeaderOrder> writer(
      3, HeaderOrder(headers),
      std::make_unique<FlushableStringByteSink>(&actual));
  EXPECT_THAT(WriteRecord(writer, input1), IsOk());
  EXPECT_THAT(WriteRecord(writer, input2), IsOk());
  EXPECT_THAT(WriteRecord(writer, input3), IsOk());
  EXPECT_THAT(WriteRecord(writer, input4), IsOk());
  EXPECT_THAT(WriteRecord(writer, input5), IsOk());
  EXPECT_THAT(WriteRecord(writer, input6), IsOk());
  EXPECT_EQ(actual, expected);
}

// test duplicate header names
TYPED_TEST(WriterTest, DuplicateHeader) {
  vector<std::string> headers{"A", "B", "A"};
  typename TestFixture::map_type input1{{"A", "a1"}, {"B", "b1"}};
  typename TestFixture::sequence_type input2{"a2", "b2", "a2"};
  std::string expected(
      "a1,b1,a1\n"
      "a2,b2,a2\n");
  std::string actual;
  Writer<HeaderOrder> writer(
      3, HeaderOrder(headers),
      std::make_unique<FlushableStringByteSink>(&actual));
  EXPECT_THAT(WriteRecord(writer, input1), IsOk());
  EXPECT_THAT(WriteRecord(writer, input2), IsOk());
  EXPECT_EQ(actual, expected);
}

// test custom order with temporary strings
TYPED_TEST(WriterTest, CustomOrderTempStringsNoHeader) {
  struct MyDataType {
    std::string key;
    int64_t count;
  };
  std::vector<MyDataType> input = {{"abc", 101}, {"def", 202}};
  // Use a custom FieldOrder to format the above struct. In particular, a
  // temporary string is created to hold the text form of the int64.
  class CustomOrder {
   public:
    std::vector<std::string> operator()(const MyDataType& record) {
      return std::vector<std::string>({record.key, absl::StrCat(record.count)});
    }
  };
  std::string expected(
      "abc,101\n"
      "def,202\n");
  std::string actual;
  // Test that the Writer template will support the std::strings in
  // CustomOrder's return type.
  Writer<CustomOrder> writer(
      2, CustomOrder(), std::make_unique<FlushableStringByteSink>(&actual));
  for (const auto& record : input) {
    EXPECT_THAT(WriteRecord(writer, record), IsOk());
  }
  EXPECT_EQ(actual, expected);
}

// test empty record
TYPED_TEST(WriterTest, EmptyRecord) {
  typename TestFixture::sequence_type input = {};
  std::string expected("\n\n");
  std::string actual;
  Writer<DefaultOrder> writer(
      0, DefaultOrder(), std::make_unique<FlushableStringByteSink>(&actual));
  EXPECT_THAT(WriteRecord(writer, input), IsOk());
  EXPECT_THAT(WriteRecord(writer, input), IsOk());
  EXPECT_EQ(actual, expected);
}

// test bad record size
TYPED_TEST(WriterTest, ErrorRecordSize) {
  typename TestFixture::sequence_type input{"a"};
  std::string actual;
  Writer<DefaultOrder> writer(
      2, DefaultOrder(), std::make_unique<FlushableStringByteSink>(&actual));
  EXPECT_THAT(WriteRecord(writer, input),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

// test empty header
TYPED_TEST(WriterTest, ErrorEmptyHeader) {
  vector<std::string> headers;
  typename TestFixture::map_type input1{{"A", "a"}, {"B", "b"}};
  typename TestFixture::sequence_type input2{"a", "b"};
  std::string expected("a,b\n");
  std::string actual;
  Writer<HeaderOrder> writer(
      2, HeaderOrder(headers),
      std::make_unique<FlushableStringByteSink>(&actual));
  EXPECT_THAT(WriteRecord(writer, input1),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(WriteRecord(writer, input2), IsOk());
  EXPECT_EQ(actual, expected);
}

// test map record keys do not match header
TYPED_TEST(WriterTest, ErrorHeaderAndRecordKeyDiff) {
  vector<std::string> headers{"A", "B", "C"};
  typename TestFixture::map_type input{{"A", "a"}, {"B", "b"}, {"Z", "c"}};
  std::string actual;
  Writer<HeaderOrder> writer(
      3, HeaderOrder(headers),
      std::make_unique<FlushableStringByteSink>(&actual));
  EXPECT_THAT(WriteRecord(writer, input),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

// test disable auto flush
TYPED_TEST(WriterTest, DisableAutoFlush) {
  vector<typename TestFixture::sequence_type> input{{"A", "a"}, {"B", "b"}};
  std::string expected(
      "A,a\n"
      "B,b\n");
  std::string actual;
  auto sink = std::make_unique<FlushableStringByteSink>(&actual);
  FlushableStringByteSink* sink_ptr = sink.get();
  Writer<DefaultOrder> writer(2, DefaultOrder(), std::move(sink));
  writer.SetAutoFlush(false);
  for (const auto& record : input) {
    EXPECT_THAT(WriteRecord(writer, record), IsOk());
    EXPECT_THAT(actual, IsEmpty());
  }
  // sink_ptr is guaranteed to remain valid while writer exists.
  sink_ptr->Flush();
  EXPECT_EQ(actual, expected);
}

// test WriteRecordToString helper function
TEST(WriteRecordToString, Basic) {
  // just need a basic case for helper as Writer is fully tested above
  vector<std::string> input{"A", "B,C", "D"};
  std::string expected("A,\"B,C\",D");
  std::string actual = WriteRecordToString(input);
  EXPECT_EQ(actual, expected);

  // test for customized delimiter
  expected = "A\tB,C\tD";
  actual = WriteRecordToString(input, '\t');
  EXPECT_EQ(actual, expected);

  // test empty record
  input.clear();
  actual = WriteRecordToString(input);
  EXPECT_THAT(actual, IsEmpty());
}

// test CRLF record delimiter
TYPED_TEST(WriterTest, CRLFRecordDelimiter) {
  vector<typename TestFixture::sequence_type> input{{"A", "a"}, {"B", "b"}};
  std::string expected(
      "A,a\r\n"
      "B,b\r\n");
  std::string actual;
  Writer<DefaultOrder> writer(
      2, DefaultOrder(), std::make_unique<FlushableStringByteSink>(&actual));
  writer.SetCRLFRecordDelimiter(true);
  for (const auto& record : input) {
    EXPECT_THAT(WriteRecord(writer, record), IsOk());
  }
  EXPECT_EQ(actual, expected);
}

// test \r\n record delimiter with \r\n, \r, and \n fields
TYPED_TEST(WriterTest, CRLFFieldQuoting) {
  vector<typename TestFixture::sequence_type> input(
      {{"simple", "with \r CR", " with CRLF \r\n"},
       {"\r\n", "with LF \n", "done \n"}});
  std::string expected(
      "simple,\"with \r CR\",\" with CRLF \r\n\"\r\n"
      "\"\r\n\",\"with LF \n\",\"done \n\"\r\n");
  VLOG(1) << "Sample output:\n" << expected;
  std::string actual;
  Writer<DefaultOrder> writer(
      3, DefaultOrder(), std::make_unique<FlushableStringByteSink>(&actual));
  writer.SetCRLFRecordDelimiter(true);
  for (const auto& record : input) {
    EXPECT_THAT(WriteRecord(writer, record), IsOk());
  }
  EXPECT_EQ(actual, expected);
}

}  // namespace
}  // namespace csv
}  // namespace util
