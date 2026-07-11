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

#include "gloop/util/csv/parser.h"

#include <math.h>
#include <stddef.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "fuzztest/fuzztest.h"
#include "gloop/strings/bytestream.h"
#include "gloop/strings/numbers.h"
#include "gloop/strings/split.h"
#include "gloop/util/functional/to_callback.h"
#include "gloop/util/gtl/unique_array.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::absl_testing::IsOk;
using strings::ArrayByteSource;
using testing::ElementsAre;
using util::csv::ParseLine;
using util::csv::Parser;
using util::csv::Record;

// See <link> for more info on this macro,
// for which there is not yet a common implementation.
#define TRACED_CALL(stmt)             \
  do {                                \
    SCOPED_TRACE("Called from here"); \
    {                                 \
      stmt;                           \
    }                                 \
  } while (0)

namespace {

constexpr absl::string_view kMultTable =
    "0,0,0,0,0,0,0,0,0,0,0\n"
    "0,1,2,3,4,5,6,7,8,9,10\n"
    "0,2,4,6,8,10,12,14,16,18,20\n"
    "0,3,6,9,12,15,18,21,24,27,30\n"
    "0,4,8,12,16,20,24,28,32,36,40\n"
    "0,5,10,15,20,25,30,35,40,45,50\n"
    "0,6,12,18,24,30,36,42,48,54,60\n"
    "0,7,14,21,28,35,42,49,56,63,70\n"
    "0,8,16,24,32,40,48,56,64,72,80\n"
    "0,9,18,27,36,45,54,63,72,81,90\n"
    "0,10,20,30,40,50,60,70,80,90,100\n";

constexpr absl::string_view kMultTableTabs =
    "0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\n"
    "0\t1\t2\t3\t4\t5\t6\t7\t8\t9\t10\n"
    "0\t2\t4\t6\t8\t10\t12\t14\t16\t18\t20\n"
    "0\t3\t6\t9\t12\t15\t18\t21\t24\t27\t30\n"
    "0\t4\t8\t12\t16\t20\t24\t28\t32\t36\t40\n"
    "0\t5\t10\t15\t20\t"
    "25\t30\t35\t40\t45\t50\n"
    "0\t6\t12\t18\t24\t30\t36\t42\t48\t54\t60\n"
    "0\t7\t14\t21\t28\t35\t42\t49\t56\t63\t70\n"
    "0\t8\t16\t24\t32\t40\t48\t56\t64\t72\t80\n"
    "0\t9\t18\t27\t36\t45\t54\t63\t72\t81\t90\n"
    "0\t10\t20\t30\t40\t50\t60\t70\t80\t90\t100\n";

constexpr absl::string_view kCsvEmptyLeadingLines =
    "\n\n"
    "George W. Bush,\"January 20, 2001\",Republican\n"
    "Barack Obama,\"January 20, 2009\",Democratic\n";

constexpr absl::string_view kQuoteInField =
    "Name,Color,Personality\n"
    "Elmo,Red,Friendly\n"
    "Oscar,Gre\"e\"n,Grouchy\n"
    "Cookie Monster,Blue,\"Friendly, Hungry\"\n"
    "Big Bird,\"Yel\"low,Large\n"
    "Snuffy,\"Br\"\"own\",Melancholy\n";

constexpr absl::string_view kNewlineFileContent =
    "Google,Rocks\r"
    "Google,Rocks\r\n"
    "Google,Rocks\r"
    "Google,Rocks\n";

constexpr absl::string_view kNewlineFileContent1 = "Google,Rocks\r";

constexpr absl::string_view kNewlineFileContent2 = "Google,Rocks\r\n";

constexpr absl::string_view kMissingNewlineContent =
    "Google,Rocks\n"
    "Google,Rocks\n"
    "Google,Rocks\n"
    "Google,Rocks";

constexpr absl::string_view kQuotedNewlineContent =
    "There,is,a,newline,here\n"
    "\"There,is\na\",newline,here\n"
    "\"There,is\r\na\",newline,here\n"
    "\"There,is\ra\",newline,here\n";

constexpr absl::string_view kVariableFields =
    "Dopey,Doc,Sneezy,Sleepy,Happy,Grumpy,Bashful\n"
    "Larry,Curly,Moe\n";

constexpr absl::string_view kQuotedQuotes =
    "One,Two,Th\"\"\"\"ree\n"
    "Four,Fi\"\",\"\"ve,Six\n"
    "Seven,Ei\"\"gh\"\"t,Nine\n";

constexpr absl::string_view kMisc =
    ",,,,,\n"  // Empty fields
    "\n"
    "\n"
    "asdfsd\"asdfasdf";  // A quote in a field without a newline at the end

// We can use this class instead of ArrayByteSource to simulate a ByteSource
// that contains multiple fragments.  ArrayByteSource returns the entire array
// in one fragment.
class MockChunkedByteSource : public strings::ByteSource {
 public:
  MockChunkedByteSource(const absl::string_view data, size_t block_size)
      : data_(data), block_size_(block_size) {}

  size_t Available() const override { return data_.size(); }
  absl::string_view Peek() override {
    return data_.substr(0, std::min<size_t>(block_size_, data_.size()));
  }
  void Skip(size_t n) override { data_.remove_prefix(n); }

 private:
  absl::string_view data_;
  size_t block_size_;
};

static void CheckMultiplicationTable(const Parser& parser) {
  // We can programmatically check the validity of a multiplication table.
  int i = 0;
  for (const Record& record : parser) {
    ASSERT_THAT(record.status(), IsOk());

    const std::vector<std::string>& fields = record.fields();
    EXPECT_EQ(11, fields.size());

    int64_t num = record.number();
    EXPECT_EQ(i, num);

    int j = 0;
    for (const std::string& field : fields) {
      int32_t val;

      absl::SimpleAtoi(field, &val);
      EXPECT_EQ(num * j, val);
      ++j;
    }
    ++i;
  }
}

TEST(Parser, SimpleVecRangeBasedLoopFromRawPointer) {
  Parser parser(new ArrayByteSource(kMultTable));
  TRACED_CALL(CheckMultiplicationTable(parser));
}

TEST(Parser, SimpleVecRangeBasedLoop) {
  Parser parser(std::make_unique<ArrayByteSource>(kMultTable));
  TRACED_CALL(CheckMultiplicationTable(parser));
}

TEST(Parser, ChunkedByteSource) {
  Parser parser(std::make_unique<MockChunkedByteSource>(kMultTable, 5));
  TRACED_CALL(CheckMultiplicationTable(parser));
}

TEST(Parser, TabDelimiter) {
  // The same multiplication table, but with tabs separating it
  Parser parser(std::make_unique<ArrayByteSource>(kMultTableTabs), '\t');
  TRACED_CALL(CheckMultiplicationTable(parser));
}

TEST(Parser, BreakFromLoop) {
  // Breaking out of the range-based for loop, we shouldn't leak any memory
  Parser parser(std::make_unique<ArrayByteSource>(kMultTable));

  for (const Record& record : parser) {
    ASSERT_THAT(record.status(), IsOk());

    const std::vector<std::string>& fields = record.fields();
    EXPECT_EQ(11, fields.size());

    int64_t num = record.number();

    if (num == 4)  // Early termination
      break;
  }
}

TEST(Parser, VectorIterator) {
  Parser parser(std::make_unique<ArrayByteSource>(kMultTable));

  Parser::Iterator it = parser.begin();

  // This should advance *all* begin iterators, so the loop will start at
  // row 1 of the actual content.
  EXPECT_EQ(11, it->fields().size());
  ++it;

  int i = 1;
  for (Parser::Iterator it = parser.begin(); it != parser.end(); ++it) {
    const Record& record = *it;
    ASSERT_THAT(record.status(), IsOk());

    const std::vector<std::string>& fields = record.fields();
    EXPECT_EQ(11, fields.size());

    int64_t num = record.number();
    EXPECT_EQ(i, num);

    int j = 0;
    for (const std::string& field : fields) {
      int32_t val;

      absl::SimpleAtoi(field, &val);
      EXPECT_EQ(num * j, val);
      ++j;
    }
    ++i;
  }

  EXPECT_EQ(parser.end(), it);
}

TEST(Parser, IteratorEquality) {
  Parser parser(std::make_unique<ArrayByteSource>(kMultTable));

  Parser::Iterator it1 = parser.begin();
  Parser::Iterator it2 = parser.begin();
  Parser::Iterator it_end = parser.end();

  EXPECT_EQ(11, it1->fields().size());
  EXPECT_EQ(it1->fields()[1], "0");
  EXPECT_EQ(it1, it2);
  EXPECT_NE(it1, it_end);
  EXPECT_NE(it2, it_end);

  // Advance one iterator, it should advance the other, but not to the end.
  ++it1;
  EXPECT_EQ(it1->fields()[1], "1");
  EXPECT_EQ(it2->fields()[1], "1");
  EXPECT_EQ(it1, it2);
  EXPECT_NE(it1, it_end);
  EXPECT_NE(it2, it_end);

  // Advance the other iterator, and advance them all.
  ++it2;
  EXPECT_EQ(it1->fields()[1], "2");
  EXPECT_EQ(it2->fields()[1], "2");
  EXPECT_EQ(it1, it2);
  EXPECT_NE(it1, it_end);
  EXPECT_NE(it2, it_end);

  // Now advance one of them to the end
  while (it2 != it_end) ++it2;

  // The iterators should equal each other, and they should equal the
  // end iterator
  EXPECT_EQ(it1, it2);
  EXPECT_EQ(it1, it_end);
  EXPECT_EQ(it2, it_end);
}

TEST(Parser, DifferentRecordLengths) {
  // Test input which has a couple of empty lines
  Parser parser(std::make_unique<ArrayByteSource>(kCsvEmptyLeadingLines));

  Parser::Iterator it = parser.begin();

  // A record with just a newline is technically a record with one field
  // which has length 0.
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_EQ(1, it->fields().size());
  EXPECT_THAT(it->fields(), ElementsAre(""));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_EQ(1, it->fields().size());
  EXPECT_THAT(it->fields(), ElementsAre(""));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_EQ(3, it->fields().size());
  EXPECT_THAT(it->fields(),
              ElementsAre("George W. Bush", "January 20, 2001", "Republican"));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_EQ(3, it->fields().size());
  EXPECT_THAT(it->fields(),
              ElementsAre("Barack Obama", "January 20, 2009", "Democratic"));

  ++it;
  EXPECT_EQ(parser.end(), it);
}

TEST(Parser, QuoteInNonQuotedField) {
  // Test parsing input which has an illegal quote in a field
  Parser parser(std::make_unique<ArrayByteSource>(kQuoteInField));

  Parser::Iterator it = parser.begin();

  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("Name", "Color", "Personality"));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("Elmo", "Red", "Friendly"));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("Oscar", "Green", "Grouchy"));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(),
              ElementsAre("Cookie Monster", "Blue", "Friendly, Hungry"));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("Big Bird", "Yellow", "Large"));

  ++it;
  EXPECT_THAT(it->fields(), ElementsAre("Snuffy", "Br\"own", "Melancholy"));

  ++it;
  EXPECT_EQ(parser.end(), it);
}

TEST(Parser, LiteralQuotes) {
  Parser parser(std::make_unique<ArrayByteSource>(kQuoteInField), ',',
                Parser::Mode::LITERAL_QUOTES);

  Parser::Iterator it = parser.begin();

  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("Name", "Color", "Personality"));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("Elmo", "Red", "Friendly"));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("Oscar", R"(Gre"e"n)", "Grouchy"));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("Cookie Monster", "Blue",
                                        R"("Friendly)", R"( Hungry")"));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("Big Bird", R"("Yel"low)", "Large"));

  ++it;
  EXPECT_THAT(it->fields(),
              ElementsAre("Snuffy", R"("Br""own")", "Melancholy"));

  ++it;
  EXPECT_EQ(parser.end(), it);
}

TEST(Parser, CompleteParsing) {
  // Test to see if our iterator implementation will allow us to parse
  // the entire file directly to a vector.
  Parser parser(std::make_unique<ArrayByteSource>(kMultTable));
  std::vector<Record> results(parser.begin(), parser.end());

  int i = 0;
  for (const Record& record : results) {
    ASSERT_TRUE(record.status().ok());

    const std::vector<std::string>& fields = record.fields();
    EXPECT_EQ(11, fields.size());

    int64_t num = record.number();
    EXPECT_EQ(i, num);

    int j = 0;
    for (const std::string& field : fields) {
      int32_t val;

      absl::SimpleAtoi(field, &val);
      EXPECT_EQ(num * j, val);
      ++j;
    }
    ++i;
  }
}

TEST(Parser, AssortedNewlines) {
  // Test a few newline and EOF scenarios
  std::vector<absl::string_view> cases;
  cases.push_back(kNewlineFileContent);
  cases.push_back(kNewlineFileContent1);
  cases.push_back(kNewlineFileContent2);

  for (absl::string_view c : cases) {
    Parser parser(std::make_unique<ArrayByteSource>(c));

    for (const Record& record : parser) {
      EXPECT_TRUE(record.status().ok());
      const std::vector<std::string>& fields = record.fields();

      EXPECT_EQ(2, fields.size());
      EXPECT_THAT(fields, ElementsAre("Google", "Rocks"));
    }
  }
}

TEST(Parser, NoTrailingNewline) {
  Parser parser(std::make_unique<ArrayByteSource>(kMissingNewlineContent));
  std::vector<Record> results(parser.begin(), parser.end());

  ASSERT_EQ(4, results.size());

  for (const Record& record : results) {
    EXPECT_TRUE(record.status().ok());
    const std::vector<std::string>& fields = record.fields();

    EXPECT_EQ(2, fields.size());
    EXPECT_THAT(fields, ElementsAre("Google", "Rocks"));
  }
}

TEST(Parser, QuotedNewlines) {
  Parser parser(std::make_unique<ArrayByteSource>(kQuotedNewlineContent));

  Parser::Iterator it = parser.begin();

  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("There", "is", "a", "newline", "here"));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("There,is\na", "newline", "here"));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("There,is\na", "newline", "here"));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("There,is\na", "newline", "here"));

  ++it;
  EXPECT_EQ(parser.end(), it);
}

TEST(Parser, VariableFieldWidth) {
  Parser parser(std::make_unique<ArrayByteSource>(kVariableFields));

  Parser::Iterator it = parser.begin();

  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("Dopey", "Doc", "Sneezy", "Sleepy",
                                        "Happy", "Grumpy", "Bashful"));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("Larry", "Curly", "Moe"));

  ++it;
  EXPECT_EQ(parser.end(), it);
}

TEST(Parser, QuotedQuotes) {
  Parser parser(std::make_unique<ArrayByteSource>(kQuotedQuotes));

  Parser::Iterator it = parser.begin();

  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("One", "Two", "Th\"ree"));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("Four", "Fi", "ve", "Six"));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("Seven", "Eight", "Nine"));

  ++it;
  EXPECT_EQ(parser.end(), it);
}

TEST(Parser, SlashEscaped) {
  const char kSlashEscaped[] = R"(One,Two,Three
"Four","Fi\ve","Si\
x"
"Sev\"en","Ei\"\"ght","Nine")";

  Parser parser(std::make_unique<ArrayByteSource>(kSlashEscaped), ',',
                Parser::Mode::MYSQL_ESCAPING);

  Parser::Iterator it = parser.begin();

  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("One", "Two", "Three"));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("Four", "Five", "Si\nx"));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("Sev\"en", "Ei\"\"ght", "Nine"));

  ++it;
  EXPECT_EQ(parser.end(), it);
}

TEST(Parser, SlashEscapedDoubleQuotes) {
  const char kSlashEscaped[] = R"("One","Tw""o","Three")";

  Parser parser(std::make_unique<ArrayByteSource>(kSlashEscaped), ',',
                Parser::Mode::MYSQL_ESCAPING);

  Parser::Iterator it = parser.begin();

  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("One", "Tw\"o,Three"));

  ++it;
  EXPECT_EQ(parser.end(), it);
}

TEST(Parser, Misc) {
  // Test a couple of scenarios which don't really apply elsewhere
  Parser parser(std::make_unique<ArrayByteSource>(kMisc));

  Parser::Iterator it = parser.begin();

  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("", "", "", "", "", ""));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre(""));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre(""));

  ++it;
  EXPECT_THAT(it->status(), IsOk());
  EXPECT_THAT(it->fields(), ElementsAre("asdfsdasdfasdf"));

  ++it;
  EXPECT_EQ(parser.end(), it);
}

TEST(Parser, EmptyFile) {
  Parser parser(std::make_unique<ArrayByteSource>(""));
  Parser::Iterator it = parser.begin();

  // If the file is empty, the begin iterator should equal the end iterator
  EXPECT_EQ(parser.end(), it);
}

TEST(Parser, SingleLine) {
  Record rec = ParseLine("a,b,c,d,e");
  EXPECT_TRUE(rec.status().ok());
  EXPECT_THAT(rec.fields(), ElementsAre("a", "b", "c", "d", "e"));

  rec = ParseLine("a\tb\tc\td\te\n", '\t');
  EXPECT_TRUE(rec.status().ok());
  EXPECT_THAT(rec.fields(), ElementsAre("a", "b", "c", "d", "e"));

  rec = ParseLine(",");
  EXPECT_TRUE(rec.status().ok());
  EXPECT_THAT(rec.fields(), ElementsAre("", ""));

  // A string with a single space
  rec = ParseLine(" ");
  EXPECT_TRUE(rec.status().ok());
  EXPECT_THAT(rec.fields(), ElementsAre(" "));

  // The empty string
  rec = ParseLine("");
  EXPECT_TRUE(rec.status().ok());
  EXPECT_THAT(rec.fields(), ElementsAre(""));

  // A string with a single newline; same behavior as the empty string
  rec = ParseLine("\n");
  EXPECT_TRUE(rec.status().ok());
  EXPECT_THAT(rec.fields(), ElementsAre(""));
}

TEST(Parser, SingleLineDifferentModes) {
  Record rec = ParseLine("a,b,\"c,d,e\"", ',');
  EXPECT_TRUE(rec.status().ok());
  EXPECT_THAT(rec.fields(), ElementsAre("a", "b", "c,d,e"));

  rec = ParseLine("a,b,\"c,d,e\"", ',', Parser::Mode::LITERAL_QUOTES);
  EXPECT_TRUE(rec.status().ok());
  EXPECT_THAT(rec.fields(), ElementsAre("a", "b", "\"c", "d", "e\""));

  rec =
      ParseLine(R"("One","Tw""o","Three")", ',', Parser::Mode::MYSQL_ESCAPING);
  EXPECT_TRUE(rec.status().ok());
  EXPECT_THAT(rec.fields(), ElementsAre("One", "Tw\"o,Three"));
}

TEST(Parser, Extract) {
  Record rec = ParseLine("a,b,c");
  ASSERT_TRUE(rec.status().ok());
  EXPECT_THAT(std::move(rec).fields(), ElementsAre("a", "b", "c"));
}

TEST(Parser, TrailingEmptyFieldNoNewline) {
  Record rec = ParseLine(
      "owner,job_x,0,100.0000,0.0000,0.0000,1000,"
      "10,100,\"PENDING,RUN\",");
  EXPECT_TRUE(rec.status().ok());
  std::vector<std::string> fields = rec.fields();
  ASSERT_EQ(11, fields.size());
  EXPECT_EQ(fields[9], "PENDING,RUN");
  EXPECT_EQ(fields[10], "");
}

static std::string CreateBMInput(int len) {
  const int kAverageValueLen = 25;
  std::string val(len * kAverageValueLen, 'x');
  for (int i = 1; i < val.size(); i += kAverageValueLen) val[i] = ',';

  return val;
}

static std::string Create2DBMInput(int len) {
  const int kAverageValueLen = 25;
  const int kRows = sqrt(len) + 1;

  std::string val = CreateBMInput(len);
  for (int i = 1; i < val.size(); i += kAverageValueLen * kRows) val[i] = '\n';

  return val;
}

// Microbenchmark for parsing a single line with
// SplitCSVLineWithDelimiterForStrings().  If/when that API disappears, this
// benchmark can too.
void BM_SplitCSVSingleLine(benchmark::State& state) {
  std::string input = CreateBMInput(state.range(0));
  for (auto _ : state) {
    std::vector<std::string> fields;
    strings::SplitCSVLineWithDelimiterForStrings(input, ',', &fields);
    benchmark::DoNotOptimize(fields);
  }
}
BENCHMARK(BM_SplitCSVSingleLine)->Range(1, 1 << 20);

// Microbenchmark for parsing a single line with util::csv::ParseLine().
void BM_ParseLine(benchmark::State& state) {
  std::string input = CreateBMInput(state.range(0));
  for (auto _ : state) {
    std::vector<std::string> fields = ParseLine(input).fields();
    benchmark::DoNotOptimize(fields);
  }
}
BENCHMARK(BM_ParseLine)->Range(1, 1 << 20);

// Microbenchmark for parsing a single line with absl::StrSplit().
void BM_StringSplitCSVSingleLine(benchmark::State& state) {
  std::string input = CreateBMInput(state.range(0));
  for (auto _ : state) {
    std::vector<std::string> fields = absl::StrSplit(input, ',');
    benchmark::DoNotOptimize(fields);
  }
}
BENCHMARK(BM_StringSplitCSVSingleLine)->Range(1, 1 << 20);

// Microbenchmark for parsing a multi-line File from memory using an
// InputBuffer.

// Microbenchmark for parsing a multi-line File from memory using
// util::csv::Parser.
static void BM_ParseFile(benchmark::State& state) {
  std::string input = Create2DBMInput(state.range(0));
  for (auto _ : state) {
    Parser parser(std::make_unique<ArrayByteSource>(input));

    for (const Record& record : parser) {
      benchmark::DoNotOptimize(record.fields());
    }
  }
}
BENCHMARK(BM_ParseFile)->Range(1, 1 << 20);

// Microbenchmark for parsing a multi-line File from memory using
// FileLineReader

void FuzzParser(absl::string_view csv_input, char delim, Parser::Mode mode) {
  Parser parser(std::make_unique<ArrayByteSource>(csv_input), delim, mode);
  int64_t record_order = 0;
  bool last_was_error = false;
  for (const Record& rec : parser) {
    EXPECT_FALSE(last_was_error);
    EXPECT_EQ(rec.number(), record_order);
    ++record_order;
    if (!rec.status().ok()) {
      EXPECT_EQ(rec.status().code(), absl::StatusCode::kInternal);
      last_was_error = true;
      continue;
    }
    const std::vector<std::string> fields_copy = rec.fields();
    const std::vector<std::string> fields_move = std::move(rec).fields();
    EXPECT_EQ(fields_copy, fields_move);
  }
}

FUZZ_TEST(ParserFuzzer, FuzzParser)
    .WithDomains(fuzztest::Arbitrary<absl::string_view>(),
                 fuzztest::Arbitrary<char>(),
                 fuzztest::Arbitrary<Parser::Mode>())
    .WithSeeds({{kMultTable, ',', Parser::Mode::RFC4180},
                {kMultTableTabs, '\t', Parser::Mode::MYSQL_ESCAPING},
                {kMultTableTabs, '\t', Parser::Mode::MYSQL_ESCAPING},
                {kCsvEmptyLeadingLines, ',', Parser::Mode::RFC4180},
                {kQuoteInField, ',', Parser::Mode::RFC4180},
                {kNewlineFileContent, ',', Parser::Mode::RFC4180},
                {kNewlineFileContent1, ',', Parser::Mode::RFC4180},
                {kNewlineFileContent2, ',', Parser::Mode::RFC4180},
                {kMissingNewlineContent, ',', Parser::Mode::RFC4180},
                {kQuotedNewlineContent, ',', Parser::Mode::RFC4180},
                {kVariableFields, ',', Parser::Mode::RFC4180},
                {kQuotedQuotes, ',', Parser::Mode::RFC4180},
                {kMisc, ',', Parser::Mode::RFC4180},
                {absl::string_view("a,\0;'", 5), '\0',
                 Parser::Mode::LITERAL_QUOTES}});

}  // namespace
