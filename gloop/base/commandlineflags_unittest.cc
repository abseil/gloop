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

#include "gloop/base/commandlineflags.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cstdint>

#ifndef _WIN32
#include <unistd.h>  // for unlink(), access()
#endif

#include <string>
#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "absl/container/fixed_array.h"
#include "absl/container/flat_hash_map.h"
#include "absl/flags/config.h"
#include "absl/flags/flag.h"
#include "absl/flags/internal/parse.h"
#include "absl/flags/marshalling.h"
#include "absl/flags/reflection.h"
#include "absl/log/check.h"
#include "absl/log/die_if_null.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "benchmark/benchmark.h"
#include "gloop/base/config.h"
#include "gloop/base/init_google.h"
#include "gloop/base/log_file_flags.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

// TODO: This stanza should disappear once gunit extends
// support for portable builds.
#if GUNIT_NO_GOOGLE3
// ::testing::SrcDir(), ::testing::TempDir() not supported.
#define FILESYSTEM_SUPPORTED false
// Benchmark testing not supported.
#define BENCHMARK_SUPPORTED false
// The CaptureTestStderr function is not implemented.
#define CAPTURE_TEST_STDERR_SUPPORTED false
#else
#define FILESYSTEM_SUPPORTED true
#define BENCHMARK_SUPPORTED true
#define CAPTURE_TEST_STDERR_SUPPORTED true
#endif

// GoogleTest does not identify that we are in google3 on some platforms (MSVC,
// OSX). In these cases we have to manually deal with restoring flag values.
#if GTEST_GOOGLE3_MODE_
#define FLAG_VALUES_RESTORED_AUTOMATICALLY true
#else
#define FLAG_VALUES_RESTORED_AUTOMATICALLY false
#endif

using testing::ElementsAre;

static const char* kTestArgv[] = {"/test/argv/for/commandlineflags_unittest",
                                  "argv 2", "3rd argv", "argv #4"};

ABSL_FLAG(bool, test_bool, false, "tests bool-ness");
ABSL_FLAG(int32_t, test_int32, -1, "");
ABSL_FLAG(int64_t, test_int64, -2, "");
ABSL_FLAG(uint64_t, test_uint64, 2, "");
ABSL_FLAG(double, test_double, -1.0, "");
ABSL_FLAG(std::string, test_string, "initial", "");
ABSL_FLAG(int32_t, r, 0, "For reordering test");

//
// The below ugliness gets some additional code coverage in the -helpxml
// and -helpmatch test cases having to do with string lengths and formatting
//
ABSL_FLAG(
    bool,
    test_bool_with_quite_quite_quite_quite_quite_quite_quite_quite_quite_quite_quite_quite_quite_quite_long_name,
    false,
    "extremely_extremely_extremely_extremely_extremely_extremely_extremely_"
    "extremely_long_meaning");

ABSL_FLAG(std::string, test_str1, "initial", "");
ABSL_FLAG(std::string, test_str2, "initial", "");
ABSL_FLAG(std::string, test_str3, "initial", "");

// This is used to test setting tryfromenv manually
ABSL_FLAG(std::string, test_tryfromenv, "initial", "");

// Don't try this at home!
static int changeable_var = 12;
ABSL_FLAG(int32_t, changeable_var, ++changeable_var, "");

static int changeable_bool_var = 8008;
ABSL_FLAG(bool, changeable_bool_var, ++changeable_bool_var == 8009, "");

static int changeable_string_var = 0;
static std::string ChangeableString() {
  char r[] = {static_cast<char>('0' + ++changeable_string_var), '\0'};
  return r;
}
ABSL_FLAG(std::string, changeable_string_var, ChangeableString(), "");

#if !PORTABLE_BASE
#define MAKEFLAG(x) ABSL_FLAG(int32_t, test_flag_num##x, x, "Test flag")

// Define 10 flags
#define MAKEFLAG10(x) \
  MAKEFLAG(x##0);     \
  MAKEFLAG(x##1);     \
  MAKEFLAG(x##2);     \
  MAKEFLAG(x##3);     \
  MAKEFLAG(x##4);     \
  MAKEFLAG(x##5);     \
  MAKEFLAG(x##6);     \
  MAKEFLAG(x##7);     \
  MAKEFLAG(x##8);     \
  MAKEFLAG(x##9)

// Define 100 flags
#define MAKEFLAG100(x) \
  MAKEFLAG10(x##0);    \
  MAKEFLAG10(x##1);    \
  MAKEFLAG10(x##2);    \
  MAKEFLAG10(x##3);    \
  MAKEFLAG10(x##4);    \
  MAKEFLAG10(x##5);    \
  MAKEFLAG10(x##6);    \
  MAKEFLAG10(x##7);    \
  MAKEFLAG10(x##8);    \
  MAKEFLAG10(x##9)

// Define a bunch of command-line flags.  Each occurrence of the MAKEFLAG100
// macro defines 100 integer flags.  This lets us test the effect of having
// many flags on startup time.
MAKEFLAG100(1);
MAKEFLAG100(2);
MAKEFLAG100(3);
MAKEFLAG100(4);
MAKEFLAG100(5);
MAKEFLAG100(6);
MAKEFLAG100(7);
MAKEFLAG100(8);
MAKEFLAG100(9);
MAKEFLAG100(10);
MAKEFLAG100(11);
MAKEFLAG100(12);
MAKEFLAG100(13);
MAKEFLAG100(14);
MAKEFLAG100(15);

#undef MAKEFLAG100
#undef MAKEFLAG10
#undef MAKEFLAG
#endif  // !PORTABLE_BASE

ABSL_RETIRED_FLAG(bool, legacy_bool, , );
ABSL_RETIRED_FLAG(int32_t, legacy_int32, , );
ABSL_RETIRED_FLAG(std::string, legacy_string, , );
ABSL_RETIRED_FLAG(int32_t, legacy_encap, , );

namespace base {
class FlagTest : public ::testing::Test {
 public:
  ~FlagTest() override {
    absl::flags_internal::SetFlagsHelpMode(
        absl::flags_internal::HelpMode::kNone);
  }

 private:
#if !FLAG_VALUES_RESTORED_AUTOMATICALLY
  absl::FlagSaver flag_saver_;
#endif
};
}  // namespace base

#if GTEST_HAS_DEATH_TEST
namespace base {
// Tests that inherit from this class can use real death assertions.
class FlagDeathTest : public ::testing::Test {
 public:
  FlagDeathTest() {
    testing::internal::SetInjectableArgvs(*ABSL_DIE_IF_NULL(original_argv_));
  }
  ~FlagDeathTest() override {
    testing::internal::ClearInjectableArgvs();
    absl::flags_internal::SetFlagsHelpMode(
        absl::flags_internal::HelpMode::kNone);
  }

  // Store the original argv to allow death tests. Should be called once before
  // SetArgv().
  static void SaveArgv(int argc, char** argv) {
    CHECK(original_argv_ == nullptr);
    original_argv_ = new std::vector<std::string>(argv, argv + argc);
  }

 private:
#if !FLAG_VALUES_RESTORED_AUTOMATICALLY
  absl::FlagSaver flag_saver_;
#endif
  static const std::vector<std::string>* original_argv_;
};

const std::vector<std::string>* FlagDeathTest::original_argv_ = nullptr;
}  // namespace base
#endif  // GTEST_HAS_DEATH_TEST

namespace {

// Separate macros for these make porting (eg to MacOS) a bit easier
#define EXPECT_INF(arg) EXPECT_TRUE(isinf(arg))
#define EXPECT_NAN(arg) EXPECT_TRUE(isnan(arg))

#if FILESYSTEM_SUPPORTED
static std::string TmpFile(const std::string& basename) {
#ifdef _MSC_VER
  return testing::TempDir() + "\\" + basename;
#else
  return testing::TempDir() + "/" + basename;
#endif
}

// Returns the definition of the --flagfile flag to be used in the tests.
// Must be called after InitGoogle().
static const char* GetFlagFileFlag() {
  static const std::string flagfile =
      ::testing::SrcDir() +
      "/_main/gloop/base/commandlineflags_unittest_flagfile";
  static const std::string flagfile_flag =
      std::string("--flagfile=") + flagfile;
  return flagfile_flag.c_str();
}
#endif  // FILESYSTEM_SUPPORTED

template <typename Type>
void ExpectFlagParseRoundTrip(absl::string_view text, Type expected_value) {
  std::string error;
  Type value;
  SCOPED_TRACE(absl::StrCat("input text is: ", text));
  ASSERT_TRUE(::absl::ParseFlag(text, &value, &error))
      << "Retured error: " << error;
  EXPECT_EQ(value, expected_value);
  std::string unparsed_text = ::absl::UnparseFlag(value);
  SCOPED_TRACE(absl::StrCat("unparsed text is: ", unparsed_text));
  Type value_from_unparsed;
  ASSERT_TRUE(::absl::ParseFlag(text, &value_from_unparsed, &error))
      << "Retured error: " << error;
  EXPECT_EQ(value, value_from_unparsed);
}

TEST(ParseTest, ParseCoreTypes) {
  ExpectFlagParseRoundTrip("y", true);
  ExpectFlagParseRoundTrip("1", int32_t{1});
  ExpectFlagParseRoundTrip("2", int64_t{2});
  ExpectFlagParseRoundTrip("3", uint64_t{3});
  ExpectFlagParseRoundTrip("4.0", double{4});
  ExpectFlagParseRoundTrip("value", std::string("value"));
  std::vector<std::string> value = {"a", "b", "c"};
  ExpectFlagParseRoundTrip("a,b,c", value);
}

// This technique is sometimes used to achieve a custom type for flag parsing.
// It is not encouraged.  Preferred is a struct with an embedded std::vector<T>.
// But we test this to ensure the core support of std::vector<string> does not
// interfere.
struct DerivedVectorString : public std::vector<std::string> {};

bool AbslParseFlag(absl::string_view text, DerivedVectorString* value,
                   std::string* /* error */) {
  value->clear();
  value->push_back(std::string(text));
  return true;
}

std::string AbslUnparseFlag(const DerivedVectorString& value) {
  if (value.empty()) return "";
  return value.front();
}

TEST(ParseTest, ParseDerivedTypes) {
  DerivedVectorString value;
  value.push_back("value");
  ExpectFlagParseRoundTrip("value", value);
}

#if GOOGLE_COMMANDLINEFLAGS_FULL_API
using ReadFlagsFromStringDeathTest = base::FlagDeathTest;

class FlagFileTest : public base::FlagTest {
 protected:
  // Calls ReadFlagsFromString and verifies the given expected values.
  void TestFlagString(const std::string& flags,
                      absl::string_view expected_string, bool expected_bool,
                      int32_t expected_int32, double expected_double) {
    LOG(INFO) << __func__ << " calling ReadFlagsFromString ...";
    EXPECT_TRUE(ReadFlagsFromString(flags, base::GetArgv0(),
                                    // errors are fatal
                                    true));
    LOG(INFO) << __func__ << " ... ReadFlagsFromString returned.";

    EXPECT_EQ(expected_string, absl::GetFlag(FLAGS_test_string));
    EXPECT_EQ(expected_bool, absl::GetFlag(FLAGS_test_bool));
    EXPECT_EQ(expected_int32, absl::GetFlag(FLAGS_test_int32));
    EXPECT_DOUBLE_EQ(expected_double, absl::GetFlag(FLAGS_test_double));
  }
};

// Tests reading flags from a string.
TEST_F(FlagFileTest, ReadFlagsFromString) {
  TestFlagString(
      // Flag string
      "-test_string=continued\n"
      "# some comments are in order\n"
      "# some\n"
      "  # comments\n"
      "#are\n"
      "                  #trickier\n"
      "# than others\n"
      "-test_bool=true\n"
      "     -test_int32=1\n"
      "-test_double=0.0\n",
      // Expected values
      "continued", true, 1, 0.0);

  TestFlagString(
      // Flag string
      "# let's make sure it can update values\n"
      "-test_string=initial\n"
      "-test_bool=false\n"
      "-test_int32=123\n"
      "-test_double=123.0\n",
      // Expected values
      "initial", false, 123, 123.0);
}

// Tests reading unexpected flags from a string (should log, but not fail).
TEST_F(FlagFileTest, ReadUnexpectedFlagsFromString) {
  TestFlagString(
      // Flag string
      "-test_unknown_string=continued\n"
      "-test_string=expected\n"
      "-test_bool=true\n"
      "     -test_int32=1\n"
      "-test_double=0.0\n",
      // Expected values
      "expected", true, 1, 0.0);
}

#if FILESYSTEM_SUPPORTED
// As above, but verifies that the non-fatal error is not written to STATUS.
TEST_F(FlagFileTest, UnknownFlagFromStringNotWrittenToStatusFile) {
  std::string filename(TmpFile("STATUS"));
  unlink(filename.c_str());

  // Location of the 'STATUS' file is set via environment variable.
  setenv("GOOGLE_STATUS_DIR", ::testing::TempDir().c_str(), 1);

  // Verify that the 'STATUS' file does not exist:
  EXPECT_NE(0, access(filename.c_str(), F_OK));

  TestFlagString(
      // Flag string
      "-test_unknown_string=continued\n"
      "-test_string=expected\n"
      "-test_bool=true\n"
      "     -test_int32=1\n"
      "-test_double=0.0\n",
      // Expected values
      "expected", true, 1, 0.0);

  // Verify that the 'STATUS' file still does not exist:
  EXPECT_NE(0, access(filename.c_str(), F_OK));
}
#endif  // FILESYSTEM_SUPPORTED

// Tests the filename part of the flagfile
TEST_F(FlagFileTest, FilenamesOurfileLast) {
  absl::SetFlag(&FLAGS_test_string, "initial");
  absl::SetFlag(&FLAGS_test_bool, false);
  absl::SetFlag(&FLAGS_test_int32, -1);
  absl::SetFlag(&FLAGS_test_double, -1.0);
  TestFlagString(
      // Flag string
      "-test_string=continued\n"
      "# some comments are in order\n"
      "# some\n"
      "  # comments\n"
      "#are\n"
      "                  #trickier\n"
      "# than others\n"
      "not_our_filename\n"
      "-test_bool=true\n"
      "     -test_int32=1\n"
      "commandlineflags_unittest\n"
      "-test_double=1000.0\n",
      // Expected values
      "continued", false, -1, 1000.0);
}

TEST_F(FlagFileTest, FilenamesOurfileFirst) {
  absl::SetFlag(&FLAGS_test_string, "initial");
  absl::SetFlag(&FLAGS_test_bool, false);
  absl::SetFlag(&FLAGS_test_int32, -1);
  absl::SetFlag(&FLAGS_test_double, -1.0);
  TestFlagString(
      // Flag string
      "-test_string=continued\n"
      "# some comments are in order\n"
      "# some\n"
      "  # comments\n"
      "#are\n"
      "                  #trickier\n"
      "# than others\n"
      "commandlineflags_unittest\n"
      "-test_bool=true\n"
      "     -test_int32=1\n"
      "not_our_filename\n"
      "-test_double=1000.0\n",
      // Expected values
      "continued", true, 1, -1.0);
}

#ifdef GOOGLE_HAVE_FNMATCH  // otherwise glob isn't supported */
TEST_F(FlagFileTest, FilenamesOurfileGlob) {
  CHECK(GOOGLE_HAVE_FNMATCH);
  absl::SetFlag(&FLAGS_test_string, "initial");
  absl::SetFlag(&FLAGS_test_bool, false);
  absl::SetFlag(&FLAGS_test_int32, -1);
  absl::SetFlag(&FLAGS_test_double, -1.0);
  TestFlagString(
      // Flag string
      "-test_string=continued\n"
      "# some comments are in order\n"
      "# some\n"
      "  # comments\n"
      "#are\n"
      "                  #trickier\n"
      "# than others\n"
      "*flags*\n"
      "-test_bool=true\n"
      "     -test_int32=1\n"
      "flags\n"
      "-test_double=1000.0\n",
      // Expected values
      "continued", true, 1, -1.0);
}

TEST_F(FlagFileTest, FilenamesOurfileInBigList) {
  absl::SetFlag(&FLAGS_test_string, "initial");
  absl::SetFlag(&FLAGS_test_bool, false);
  absl::SetFlag(&FLAGS_test_int32, -1);
  absl::SetFlag(&FLAGS_test_double, -1.0);
  TestFlagString(
      // Flag string
      "-test_string=continued\n"
      "# some comments are in order\n"
      "# some\n"
      "  # comments\n"
      "#are\n"
      "                  #trickier\n"
      "# than others\n"
      "*first* *flags* *third*\n"
      "-test_bool=true\n"
      "     -test_int32=1\n"
      "flags\n"
      "-test_double=1000.0\n",
      // Expected values
      "continued", true, 1, -1.0);
}
#endif  // GOOGLE_HAVE_FNMATCH

// Tests that a failed flag-from-string read keeps flags at default values
TEST_F(FlagFileTest, FailReadFlagsFromString) {
  absl::SetFlag(&FLAGS_test_int32, 119);
  std::string flags(
      "# let's make sure it can update values\n"
      "-test_string=non_initial\n"
      "-test_bool=false\n"
      "-test_int32=123\n"
      "-test_double=illegal\n");

  EXPECT_FALSE(ReadFlagsFromString(flags, base::GetArgv0(),
                                   // errors are fatal
                                   false));

  EXPECT_EQ(119, absl::GetFlag(FLAGS_test_int32));
  EXPECT_EQ("initial", absl::GetFlag(FLAGS_test_string));
}

TEST_F(FlagFileTest, NonExistentFlagFileNotFatal) {
  // If errors_are_fatal is false, it should return false but not crash.
  EXPECT_FALSE(ReadFlagsFromString(
      "--flagfile=/nonexistent_dir_for_repro/definitely_not_here.flags",
      base::GetArgv0(),
      /*errors_are_fatal=*/false));
}

#if GTEST_HAS_DEATH_TEST
TEST_F(ReadFlagsFromStringDeathTest, NonExistentFlagFileFatal) {
  // If errors_are_fatal is true, it should crash.
  EXPECT_DEATH_IF_SUPPORTED(
      ReadFlagsFromString(
          "--flagfile=/nonexistent_dir_for_repro/definitely_not_here.flags",
          base::GetArgv0(),
          /*errors_are_fatal=*/true),
      "Failed to open flagfile");
}
#endif

#if FILESYSTEM_SUPPORTED
TEST_F(FlagFileTest, UnboundedRecursion) {
  std::string filename(TmpFile("flagfile_repro_test.txt"));
  FILE* f = fopen(filename.c_str(), "w");
  ASSERT_TRUE(f != nullptr);
  fprintf(f, "--flagfile=%s\n", filename.c_str());
  fclose(f);

  std::string flags = absl::StrCat("--flagfile=", filename);
  // This should return gracefully, printing an error internally.
  EXPECT_FALSE(ReadFlagsFromString(flags, base::GetArgv0(), false));

  unlink(filename.c_str());
}
#endif

using SetFlagValueTest = base::FlagTest;

// Tests that flags can be set to ordinary values.
TEST_F(SetFlagValueTest, OrdinaryValues) {
  EXPECT_EQ("initial", absl::GetFlag(FLAGS_test_str1));

  SetCommandLineOptionWithMode("test_str1", "second", SET_FLAG_IF_DEFAULT);
  EXPECT_EQ("second", absl::GetFlag(FLAGS_test_str1));  // set; was default

  SetCommandLineOptionWithMode("test_str1", "third", SET_FLAG_IF_DEFAULT);
  EXPECT_EQ("second", absl::GetFlag(FLAGS_test_str1));  // already set once

  absl::SetFlag(&FLAGS_test_str1, "initial");
  SetCommandLineOptionWithMode("test_str1", "third", SET_FLAG_IF_DEFAULT);
  EXPECT_EQ("initial", absl::GetFlag(FLAGS_test_str1));  // already set before

  SetCommandLineOptionWithMode("test_str1", "third", SET_FLAGS_VALUE);
  EXPECT_EQ("third", absl::GetFlag(FLAGS_test_str1));  // changed value

  SetCommandLineOptionWithMode("test_str1", "fourth", SET_FLAGS_DEFAULT);
  EXPECT_EQ("third", absl::GetFlag(FLAGS_test_str1));
  // value not changed (already set before)

  EXPECT_EQ("initial", absl::GetFlag(FLAGS_test_str2));

  SetCommandLineOptionWithMode("test_str2", "second", SET_FLAGS_DEFAULT);
  EXPECT_EQ("second", absl::GetFlag(FLAGS_test_str2));  // changed (was default)

  absl::SetFlag(&FLAGS_test_str2, "extra");
  EXPECT_EQ("extra", absl::GetFlag(FLAGS_test_str2));

  absl::SetFlag(&FLAGS_test_str2, "second");
  SetCommandLineOptionWithMode("test_str2", "third", SET_FLAGS_DEFAULT);

  EXPECT_EQ("initial", absl::GetFlag(FLAGS_test_str3));

  SetCommandLineOptionWithMode("test_str3", "second", SET_FLAGS_DEFAULT);
  EXPECT_EQ("second", absl::GetFlag(FLAGS_test_str3));  // changed

  absl::SetFlag(&FLAGS_test_str3, "third");
  SetCommandLineOptionWithMode("test_str3", "fourth", SET_FLAGS_DEFAULT);
  EXPECT_EQ("third", absl::GetFlag(FLAGS_test_str3));  // not changed (was set)

  SetCommandLineOptionWithMode("test_str3", "fourth", SET_FLAG_IF_DEFAULT);
  EXPECT_EQ("third", absl::GetFlag(FLAGS_test_str3));  // not changed (was set)

  SetCommandLineOptionWithMode("test_str3", "fourth", SET_FLAGS_VALUE);
  EXPECT_EQ("fourth", absl::GetFlag(FLAGS_test_str3));  // changed value

  // Restore defaults, since flag saver does not do it.
  absl::SetFlag(&FLAGS_test_str1, "initial");
  absl::SetFlag(&FLAGS_test_str2, "initial");
  absl::SetFlag(&FLAGS_test_str3, "initial");
  SetCommandLineOptionWithMode("test_str1", "initial", SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("test_str2", "initial", SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("test_str3", "initial", SET_FLAGS_DEFAULT);
}

// Tests that flags can be set to exceptional values.
// Note: apparently MINGW doesn't parse inf and nan correctly:
//    <link>.html
TEST_F(SetFlagValueTest, ExceptionalValues) {
#if defined(isinf) && !defined(__MINGW32__)
  EXPECT_EQ("test_double set to inf\n",
            SetCommandLineOption("test_double", "inf"));
  EXPECT_INF(FLAGS_test_double);

  EXPECT_EQ("test_double set to inf\n",
            SetCommandLineOption("test_double", "INF"));
  EXPECT_INF(FLAGS_test_double);
#endif

  EXPECT_EQ("", SetCommandLineOption("test_double", "0.1xxx"));

  EXPECT_EQ("", SetCommandLineOption("test_double", " "));
  EXPECT_EQ("", SetCommandLineOption("test_double", ""));
#if defined(isinf) && !defined(__MINGW32__)
  EXPECT_EQ("test_double set to -inf\n",
            SetCommandLineOption("test_double", "-inf"));
  EXPECT_INF(FLAGS_test_double);
  EXPECT_GT(0, FLAGS_test_double);
#endif

#if defined(isnan) && !defined(__MINGW32__)
  EXPECT_EQ("test_double set to nan\n",
            SetCommandLineOption("test_double", "NaN"));
  EXPECT_NAN(FLAGS_test_double);
#endif
}

// Tests that integer flags can be specified in many ways
TEST_F(SetFlagValueTest, DifferentRadices) {
  EXPECT_EQ("test_int32 set to 12\n", SetCommandLineOption("test_int32", "12"));

  EXPECT_EQ("test_int32 set to 16\n",
            SetCommandLineOption("test_int32", "0x10"));
  EXPECT_EQ("test_int32 set to 16\n",
            SetCommandLineOption("test_int32", " 0X10"));

  EXPECT_EQ("test_int64 set to 32\n",
            SetCommandLineOption("test_int64", "0x20"));
  EXPECT_EQ("test_int64 set to 32\n",
            SetCommandLineOption("test_int64", " 0X20"));

  EXPECT_EQ("test_uint64 set to 34\n",
            SetCommandLineOption("test_uint64", "  0x22"));
  EXPECT_EQ("test_uint64 set to 34\n",
            SetCommandLineOption("test_uint64", "0X22"));

  // Leading 0 is *not* octal; it's still decimal
  EXPECT_EQ("test_int32 set to 10\n",
            SetCommandLineOption("test_int32", "010"));

  EXPECT_EQ("test_int32 set to 10\n",
            SetCommandLineOption("test_int32", "  010"));
}

// Tests what happens when you try to set a flag to an illegal value
TEST_F(SetFlagValueTest, IllegalValues) {
  absl::SetFlag(&FLAGS_test_bool, true);
  absl::SetFlag(&FLAGS_test_int32, 119);
  absl::SetFlag(&FLAGS_test_int64, 1191);
  absl::SetFlag(&FLAGS_test_uint64, 11911);
  absl::SetFlag(&FLAGS_test_double, 1.1);

  // Test invalid values.
  EXPECT_EQ("", SetCommandLineOption("test_bool", "12"));
  EXPECT_EQ("", SetCommandLineOption("test_int32", "7000000000000"));
  EXPECT_EQ("", SetCommandLineOption("test_int64", "not a number!"));
  EXPECT_EQ("", SetCommandLineOption("test_uint64", "-1"));

  EXPECT_TRUE(absl::GetFlag(FLAGS_test_bool));
  EXPECT_EQ(119, absl::GetFlag(FLAGS_test_int32));
  EXPECT_EQ(1191, absl::GetFlag(FLAGS_test_int64));
  EXPECT_EQ(11911, absl::GetFlag(FLAGS_test_uint64));

  // Test the empty string with each type of input.
  EXPECT_EQ("", SetCommandLineOption("test_bool", ""));
  EXPECT_EQ("", SetCommandLineOption("test_int32", ""));
  EXPECT_EQ("", SetCommandLineOption("test_int64", ""));
  EXPECT_EQ("", SetCommandLineOption("test_uint64", ""));
  EXPECT_EQ("", SetCommandLineOption("test_double", ""));
  EXPECT_NE("", SetCommandLineOption("test_string", ""));

  EXPECT_TRUE(absl::GetFlag(FLAGS_test_bool));
  EXPECT_EQ(119, absl::GetFlag(FLAGS_test_int32));
  EXPECT_EQ(1191, absl::GetFlag(FLAGS_test_int64));
  EXPECT_EQ(11911, absl::GetFlag(FLAGS_test_uint64));
  EXPECT_DOUBLE_EQ(1.1, absl::GetFlag(FLAGS_test_double));

  // Whitespace only input is not valid.
  EXPECT_EQ("", SetCommandLineOption("test_bool", " "));
  EXPECT_EQ("", SetCommandLineOption("test_int32", " "));
  EXPECT_EQ("", SetCommandLineOption("test_int64", " "));
  EXPECT_EQ("", SetCommandLineOption("test_uint64", " "));
  EXPECT_EQ("", SetCommandLineOption("test_double", " "));

  // Trailing spaces are fine for everything.
  EXPECT_NE("", SetCommandLineOption("test_bool", "0 "));
  EXPECT_NE("", SetCommandLineOption("test_int32", "1 "));
  EXPECT_NE("", SetCommandLineOption("test_int64", "2 "));
  EXPECT_NE("", SetCommandLineOption("test_uint64", "3 "));
  EXPECT_NE("", SetCommandLineOption("test_double", "4.5 "));

  // Leading spaces are fine for everything.
  EXPECT_NE("", SetCommandLineOption("test_bool", " 0"));
  EXPECT_NE("", SetCommandLineOption("test_int32", " 1"));
  EXPECT_NE("", SetCommandLineOption("test_int64", " 2"));
  EXPECT_NE("", SetCommandLineOption("test_uint64", " 3"));
  EXPECT_NE("", SetCommandLineOption("test_double", " 4.5"));

  EXPECT_FALSE(absl::GetFlag(FLAGS_test_bool));
  EXPECT_EQ(1, absl::GetFlag(FLAGS_test_int32));
  EXPECT_EQ(2, absl::GetFlag(FLAGS_test_int64));
  EXPECT_EQ(3, absl::GetFlag(FLAGS_test_uint64));
  EXPECT_DOUBLE_EQ(4.5, absl::GetFlag(FLAGS_test_double));
}

using MacroArgsTest = base::FlagTest;

// Tests that we only evaluate macro args once
TEST_F(MacroArgsTest, EvaluateOnce) {
  EXPECT_EQ(13, absl::GetFlag(FLAGS_changeable_var));
  // Make sure we don't ++ the value somehow, when evaluating the flag.
  EXPECT_EQ(13, absl::GetFlag(FLAGS_changeable_var));
  // Make sure the macro only evaluated this var once.
  EXPECT_EQ(13, changeable_var);
  // Make sure the actual value and default value are the same
  SetCommandLineOptionWithMode("changeable_var", "21", SET_FLAG_IF_DEFAULT);
  EXPECT_EQ(21, absl::GetFlag(FLAGS_changeable_var));
}

TEST_F(MacroArgsTest, EvaluateOnceBool) {
  EXPECT_TRUE(absl::GetFlag(FLAGS_changeable_bool_var));
  EXPECT_TRUE(absl::GetFlag(FLAGS_changeable_bool_var));
  EXPECT_EQ(8009, changeable_bool_var);
  SetCommandLineOptionWithMode("changeable_bool_var", "false",
                               SET_FLAG_IF_DEFAULT);
  EXPECT_FALSE(absl::GetFlag(FLAGS_changeable_bool_var));
}

TEST_F(MacroArgsTest, EvaluateOnceStrings) {
  EXPECT_EQ("1", absl::GetFlag(FLAGS_changeable_string_var));
  EXPECT_EQ("1", absl::GetFlag(FLAGS_changeable_string_var));
  EXPECT_EQ(1, changeable_string_var);
  SetCommandLineOptionWithMode("changeable_string_var", "different",
                               SET_FLAG_IF_DEFAULT);
  EXPECT_EQ("different", absl::GetFlag(FLAGS_changeable_string_var));
}

using FromEnvTest = base::FlagTest;

#if !PORTABLE_BASE
// Tests that the FooFromEnv does the right thing
TEST_F(FromEnvTest, LegalValues) {
  setenv("BOOL_VAL1", "true", 1);
  setenv("BOOL_VAL2", "false", 1);
  setenv("BOOL_VAL3", "1", 1);
  setenv("BOOL_VAL4", "F", 1);
  EXPECT_TRUE(BoolFromEnv("BOOL_VAL1", false));
  EXPECT_FALSE(BoolFromEnv("BOOL_VAL2", true));
  EXPECT_TRUE(BoolFromEnv("BOOL_VAL3", false));
  EXPECT_FALSE(BoolFromEnv("BOOL_VAL4", true));
  EXPECT_TRUE(BoolFromEnv("BOOL_VAL_UNKNOWN", true));
  EXPECT_FALSE(BoolFromEnv("BOOL_VAL_UNKNOWN", false));

  setenv("INT_VAL1", "1", 1);
  setenv("INT_VAL2", "-1", 1);
  EXPECT_EQ(1, Int32FromEnv("INT_VAL1", 10));
  EXPECT_EQ(-1, Int32FromEnv("INT_VAL2", 10));
  EXPECT_EQ(10, Int32FromEnv("INT_VAL_UNKNOWN", 10));

  setenv("INT_VAL3", "1099511627776", 1);
  EXPECT_EQ(1, Int64FromEnv("INT_VAL1", 20));
  EXPECT_EQ(-1, Int64FromEnv("INT_VAL2", 20));
  EXPECT_EQ(int64_t{1099511627776}, Int64FromEnv("INT_VAL3", 20));
  EXPECT_EQ(20, Int64FromEnv("INT_VAL_UNKNOWN", 20));

  EXPECT_EQ(1, Uint64FromEnv("INT_VAL1", 30));
  EXPECT_EQ(uint64_t{1099511627776}, Uint64FromEnv("INT_VAL3", 30));
  EXPECT_EQ(30, Uint64FromEnv("INT_VAL_UNKNOWN", 30));

  // I pick values here that can be easily represented exactly in floating-point
  setenv("DOUBLE_VAL1", "0.0", 1);
  setenv("DOUBLE_VAL2", "1.0", 1);
  setenv("DOUBLE_VAL3", "-1.0", 1);
  EXPECT_EQ(0.0, DoubleFromEnv("DOUBLE_VAL1", 40.0));
  EXPECT_EQ(1.0, DoubleFromEnv("DOUBLE_VAL2", 40.0));
  EXPECT_EQ(-1.0, DoubleFromEnv("DOUBLE_VAL3", 40.0));
  EXPECT_EQ(40.0, DoubleFromEnv("DOUBLE_VAL_UNKNOWN", 40.0));

  setenv("STRING_VAL1", "", 1);
  setenv("STRING_VAL2", "my happy string!", 1);
  EXPECT_STREQ("", StringFromEnv("STRING_VAL1", "unknown"));
  EXPECT_STREQ("my happy string!", StringFromEnv("STRING_VAL2", "unknown"));
  EXPECT_STREQ("unknown", StringFromEnv("STRING_VAL_UNKNOWN", "unknown"));
}

using FromEnvExitTest = base::FlagDeathTest;

// Tests that the FooFromEnv dies on parse-error
TEST_F(FromEnvExitTest, IllegalValues) {
  setenv("BOOL_BAD1", "so true!", 1);
  setenv("BOOL_BAD2", "", 1);
  EXPECT_DEATH(BoolFromEnv("BOOL_BAD1", false),
               "ERROR: error parsing env variable 'BOOL_BAD1'");
  EXPECT_DEATH(BoolFromEnv("BOOL_BAD2", true),
               "ERROR: error parsing env variable 'BOOL_BAD2'");

  setenv("INT_BAD1", "one", 1);
  setenv("INT_BAD2", "100000000000000000", 1);
  setenv("INT_BAD3", "0xx10", 1);
  setenv("INT_BAD4", "", 1);
  EXPECT_DEATH(Int32FromEnv("INT_BAD1", 10),
               "ERROR: error parsing env variable 'INT_BAD1'");
  EXPECT_DEATH(Int32FromEnv("INT_BAD2", 10),
               "ERROR: error parsing env variable 'INT_BAD2'");
  EXPECT_DEATH(Int32FromEnv("INT_BAD3", 10),
               "ERROR: error parsing env variable 'INT_BAD3'");
  EXPECT_DEATH(Int32FromEnv("INT_BAD4", 10),
               "ERROR: error parsing env variable 'INT_BAD4'");

  setenv("BIGINT_BAD1", "18446744073709551616000", 1);
  EXPECT_DEATH(Int64FromEnv("INT_BAD1", 20),
               "ERROR: error parsing env variable 'INT_BAD1'");
  EXPECT_DEATH(Int64FromEnv("INT_BAD3", 20),
               "ERROR: error parsing env variable 'INT_BAD3'");
  EXPECT_DEATH(Int64FromEnv("INT_BAD4", 20),
               "ERROR: error parsing env variable 'INT_BAD4'");
  EXPECT_DEATH(Int64FromEnv("BIGINT_BAD1", 200),
               "ERROR: error parsing env variable 'BIGINT_BAD1'");

  setenv("BIGINT_BAD2", "-1", 1);
  EXPECT_DEATH(Uint64FromEnv("INT_BAD1", 30),
               "ERROR: error parsing env variable 'INT_BAD1'");
  EXPECT_DEATH(Uint64FromEnv("INT_BAD3", 30),
               "ERROR: error parsing env variable 'INT_BAD3'");
  EXPECT_DEATH(Uint64FromEnv("INT_BAD4", 30),
               "ERROR: error parsing env variable 'INT_BAD4'");
  EXPECT_DEATH(Uint64FromEnv("BIGINT_BAD1", 30),
               "ERROR: error parsing env variable 'BIGINT_BAD1'");
  EXPECT_DEATH(Uint64FromEnv("BIGINT_BAD2", 30),
               "ERROR: error parsing env variable 'BIGINT_BAD2'");

  setenv("DOUBLE_BAD1", "0.0.0", 1);
  setenv("DOUBLE_BAD2", "", 1);
  EXPECT_DEATH(DoubleFromEnv("DOUBLE_BAD1", 40.0),
               "ERROR: error parsing env variable 'DOUBLE_BAD1'");
  EXPECT_DEATH(DoubleFromEnv("DOUBLE_BAD2", 40.0),
               "ERROR: error parsing env variable 'DOUBLE_BAD2'");
}
#endif  // !PORTABLE_BASE
#endif  // GOOGLE_COMMANDLINEFLAGS_FULL_API

using FlagFileNameTest = base::FlagTest;

using ShowUsageWithFlagsTest = base::FlagTest;

TEST_F(ShowUsageWithFlagsTest, BaseTest) {
  // TODO: test this by allowing output other than to stdout.
  // Not urgent since this functionality is tested via
  // commandlineflags_unittest.sh, though only through use of --help.
}

using ShowUsageWithFlagsRestrictTest = base::FlagTest;

TEST_F(ShowUsageWithFlagsRestrictTest, BaseTest) {
  // TODO: test this by allowing output other than to stdout.
  // Not urgent since this functionality is tested via
  // commandlineflags_unittest.sh, though only through use of --helpmatch.
}

using GetArgvsTest = base::FlagTest;

// Note: all these argv-based tests depend on SetArgv being called
// before InitGoogle() in main(), below.
TEST_F(GetArgvsTest, BaseTest) {
  std::vector<std::string> argvs = GetArgvs();
  EXPECT_EQ(4, argvs.size());
  EXPECT_EQ("/test/argv/for/commandlineflags_unittest", argvs[0]);
  EXPECT_EQ("argv 2", argvs[1]);
  EXPECT_EQ("3rd argv", argvs[2]);
  EXPECT_EQ("argv #4", argvs[3]);
}

using GetArgvTest = base::FlagTest;

TEST_F(GetArgvTest, BaseTest) {
  EXPECT_EQ(
      "/test/argv/for/commandlineflags_unittest "
      "argv 2 3rd argv argv #4",
      base::GetArgv());
}

using GetArgv0Test = base::FlagTest;

TEST_F(GetArgv0Test, BaseTest) {
  EXPECT_STREQ("/test/argv/for/commandlineflags_unittest", base::GetArgv0());
}

using GetArgvSumTest = base::FlagTest;

TEST_F(GetArgvSumTest, BaseTest) {
  // This number is just the sum of the ASCII values of all the chars
  // in GetArgv().
  EXPECT_EQ(5960, base::GetArgvSum());
}

using ProgramInvocationNameTest = base::FlagTest;

TEST_F(ProgramInvocationNameTest, BaseTest) {
  EXPECT_STREQ("/test/argv/for/commandlineflags_unittest",
               base::ProgramInvocationName());
}

using ProgramInvocationShortNameTest = base::FlagTest;

TEST_F(ProgramInvocationShortNameTest, BaseTest) {
  EXPECT_STREQ("commandlineflags_unittest", base::ProgramInvocationShortName());
}

using ResetArgvTest = base::FlagTest;

TEST_F(ResetArgvTest, BaseTest) {
  // Test that a call to ResetArgv() modifies argv even after SetArgv() and
  // InitGoogle() have been called below in main().
  const char* test_argv1[] = {"/test/reset/argv", "foo", "bar", "baz"};
  ResetArgv(ABSL_ARRAYSIZE(test_argv1), test_argv1);
  EXPECT_THAT(base::GetArgvs(),
              ElementsAre("/test/reset/argv", "foo", "bar", "baz"));
  EXPECT_EQ("/test/reset/argv foo bar baz", base::GetArgv());
  EXPECT_STREQ("/test/reset/argv", base::GetArgv0());
  EXPECT_EQ(2614, base::GetArgvSum());

  // Test that a second call to ResetArgv() works as expected.
  const char* test_argv2[] = {"/test/reset/argv/again", "foo2", "bar2", "baz2"};
  ResetArgv(ABSL_ARRAYSIZE(test_argv2), test_argv2);
  EXPECT_THAT(base::GetArgvs(),
              ElementsAre("/test/reset/argv/again", "foo2", "bar2", "baz2"));
  EXPECT_EQ("/test/reset/argv/again foo2 bar2 baz2", base::GetArgv());
  EXPECT_STREQ("/test/reset/argv/again", base::GetArgv0());
  EXPECT_EQ(3323, base::GetArgvSum());

  // Reset the state of argv to what the other tests expect.
  ResetArgv(ABSL_ARRAYSIZE(kTestArgv), kTestArgv);
}

#if GOOGLE_COMMANDLINEFLAGS_FULL_API
using GetCommandLineOptionTest = base::FlagTest;

TEST_F(GetCommandLineOptionTest, NameExistsAndIsDefault) {
  std::string value("will be changed");
  bool r = GetCommandLineOption("test_bool", &value);
  EXPECT_TRUE(r);
  EXPECT_EQ("false", value);

  r = GetCommandLineOption("test_int32", &value);
  EXPECT_TRUE(r);
  EXPECT_EQ("-1", value);
}

TEST_F(GetCommandLineOptionTest, NameExistsAndWasAssigned) {
  absl::SetFlag(&FLAGS_test_int32, 400);
  std::string value("will be changed");
  const bool r = GetCommandLineOption("test_int32", &value);
  EXPECT_TRUE(r);
  EXPECT_EQ("400", value);
}

TEST_F(GetCommandLineOptionTest, NameExistsAndWasSet) {
  SetCommandLineOption("test_int32", "700");
  std::string value("will be changed");
  const bool r = GetCommandLineOption("test_int32", &value);
  EXPECT_TRUE(r);
  EXPECT_EQ("700", value);
}

TEST_F(GetCommandLineOptionTest, NameExistsAndWasNotSet) {
  // This doesn't set the flag's value, but rather its default value.
  // is_default is still true, but the 'default' value returned has changed!
  SetCommandLineOptionWithMode("test_int32", "800", SET_FLAGS_DEFAULT);
  std::string value("will be changed");
  const bool r = GetCommandLineOption("test_int32", &value);
  EXPECT_TRUE(r);
  EXPECT_EQ("800", value);

  // Restore defaults, since flag saver does not do it.
  absl::SetFlag(&FLAGS_test_int32, -1);
  SetCommandLineOptionWithMode("test_int32", "-1", SET_FLAGS_DEFAULT);
}

TEST_F(GetCommandLineOptionTest, NameExistsAndWasConditionallySet) {
  SetCommandLineOptionWithMode("test_int32", "900", SET_FLAG_IF_DEFAULT);
  std::string value("will be changed");
  const bool r = GetCommandLineOption("test_int32", &value);
  EXPECT_TRUE(r);
  EXPECT_EQ("900", value);
}

TEST_F(GetCommandLineOptionTest, NameDoesNotExist) {
  std::string value("will not be changed");
  const bool r = GetCommandLineOption("test_int3210", &value);
  EXPECT_FALSE(r);
  EXPECT_EQ("will not be changed", value);
}

#if FILESYSTEM_SUPPORTED
#if !PORTABLE_BASE
using FlagsSetBeforeInitTest = base::FlagTest;

TEST_F(FlagsSetBeforeInitTest, TryFromEnv) {
  EXPECT_EQ("pre-set", absl::GetFlag(FLAGS_test_tryfromenv));
}
#endif  // !PORTABLE_BASE
#endif  // FILESYSTEM_SUPPORTED
#endif  // GOOGLE_COMMANDLINEFLAGS_FULL_API

// The following test case verifies that ParseCommandLineFlags() and
// ParseCommandLineNonHelpFlags() uses the last definition of a flag
// in case it's defined more than once.
// Define the test flag in the global namespace.
}  // namespace

ABSL_FLAG(int32_t, test_flag, -1, "used for testing commandlineflags.cc");

namespace {
// Parses and returns the --test_flag flag.
// If with_help is true, calls ParseCommandLineFlags; otherwise calls
// ParseCommandLineNonHelpFlags.
int32_t ParseTestFlag(bool with_help, int argc, const char** const_argv) {
  absl::FlagSaver fs;  // Restores the flags before returning.

  // Makes a copy of the input array s.t. it can be reused
  // (ParseCommandLineFlags() will alter the array).
  absl::FixedArray<char*, 0> argv_save(argc + 1);
  char** argv = argv_save.data();
  memcpy(argv, const_argv, sizeof(*argv) * (argc + 1));

  if (with_help) {
    base::ParseCommandLine(&argc, &argv);
  } else {
    ParseCommandLineNonHelpFlags(&argc, &argv, true);
  }

  return absl::GetFlag(FLAGS_test_flag);
}

int ParseTestFlag(bool with_help, absl::Span<const std::string> const_argv) {
  std::vector<const char*> ptr_vec;
  ptr_vec.reserve(const_argv.size() + 1);
  for (const std::string& arg : const_argv) {
    ptr_vec.push_back(arg.c_str());
  }
  ptr_vec.push_back(nullptr);

  return ParseTestFlag(with_help, const_argv.size(), &(ptr_vec[0]));
}

#if GOOGLE_COMMANDLINEFLAGS_FULL_API
using ParseCommandLineFlagsUsesLastDefinitionTest = base::FlagTest;

TEST_F(ParseCommandLineFlagsUsesLastDefinitionTest,
       WhenFlagIsDefinedTwiceOnCommandLine) {
  ASSERT_EQ(-1, absl::GetFlag(FLAGS_test_flag));

  const char* argv[] = {
      "my_test",
      "--test_flag=1",
      "--test_flag=2",
      nullptr,
  };

  EXPECT_EQ(2, ParseTestFlag(true, ABSL_ARRAYSIZE(argv) - 1, argv));
  EXPECT_EQ(2, ParseTestFlag(false, ABSL_ARRAYSIZE(argv) - 1, argv));
}

#if FILESYSTEM_SUPPORTED
TEST_F(ParseCommandLineFlagsUsesLastDefinitionTest,
       WhenFlagIsDefinedTwiceInFlagFile) {
  ASSERT_EQ(-1, absl::GetFlag(FLAGS_test_flag));

  const char* argv[] = {
      "my_test",
      GetFlagFileFlag(),
      nullptr,
  };

  EXPECT_EQ(2, ParseTestFlag(true, ABSL_ARRAYSIZE(argv) - 1, argv));
  EXPECT_EQ(2, ParseTestFlag(false, ABSL_ARRAYSIZE(argv) - 1, argv));
}

TEST_F(ParseCommandLineFlagsUsesLastDefinitionTest,
       WhenFlagIsDefinedInCommandLineAndThenFlagFile) {
  const char* argv[] = {
      "my_test",
      "--test_flag=0",
      GetFlagFileFlag(),
      nullptr,
  };

  EXPECT_EQ(2, ParseTestFlag(true, ABSL_ARRAYSIZE(argv) - 1, argv));
  EXPECT_EQ(2, ParseTestFlag(false, ABSL_ARRAYSIZE(argv) - 1, argv));
}

TEST_F(ParseCommandLineFlagsUsesLastDefinitionTest,
       WhenFlagIsDefinedInFlagFileAndThenCommandLine) {
  const char* argv[] = {
      "my_test",
      GetFlagFileFlag(),
      "--test_flag=3",
      nullptr,
  };

  EXPECT_EQ(3, ParseTestFlag(true, ABSL_ARRAYSIZE(argv) - 1, argv));
  EXPECT_EQ(3, ParseTestFlag(false, ABSL_ARRAYSIZE(argv) - 1, argv));
}

TEST_F(ParseCommandLineFlagsUsesLastDefinitionTest,
       WhenFlagIsDefinedInCommandLineAndFlagFileAndThenCommandLine) {
  const char* argv[] = {
      "my_test", "--test_flag=0", GetFlagFileFlag(), "--test_flag=3", nullptr,
  };

  EXPECT_EQ(3, ParseTestFlag(true, ABSL_ARRAYSIZE(argv) - 1, argv));
  EXPECT_EQ(3, ParseTestFlag(false, ABSL_ARRAYSIZE(argv) - 1, argv));
}
#endif  // FILESYSTEM_SUPPORTED

using ParseCommandLineFlagsAndDashArgsTest = base::FlagTest;

TEST_F(ParseCommandLineFlagsAndDashArgsTest, TwoDashArgFirst) {
  const char* argv[] = {
      "my_test",
      "--",
      "--test_flag=0",
      nullptr,
  };

  EXPECT_EQ(-1, ParseTestFlag(true, ABSL_ARRAYSIZE(argv) - 1, argv));
  EXPECT_EQ(-1, ParseTestFlag(false, ABSL_ARRAYSIZE(argv) - 1, argv));
}

TEST_F(ParseCommandLineFlagsAndDashArgsTest, TwoDashArgMiddle) {
  const char* argv[] = {
      "my_test", "--test_flag=7", "--", "--test_flag=0", nullptr,
  };

  EXPECT_EQ(7, ParseTestFlag(true, ABSL_ARRAYSIZE(argv) - 1, argv));
  EXPECT_EQ(7, ParseTestFlag(false, ABSL_ARRAYSIZE(argv) - 1, argv));
}

TEST_F(ParseCommandLineFlagsAndDashArgsTest, OneDashArg) {
  const char* argv[] = {
      "my_test",
      "-",
      "--test_flag=0",
      nullptr,
  };

  EXPECT_EQ(0, ParseTestFlag(true, ABSL_ARRAYSIZE(argv) - 1, argv));
  EXPECT_EQ(0, ParseTestFlag(false, ABSL_ARRAYSIZE(argv) - 1, argv));
}

#if GTEST_HAS_DEATH_TEST

using ParseCommandLineFlagsUnknownFlagDeathTest = base::FlagDeathTest;

TEST_F(ParseCommandLineFlagsUnknownFlagDeathTest, FlagIsCompletelyUnknown) {
  const char* argv[] = {
      "my_test",
      "--this_flag_does_not_exist",
      nullptr,
  };

  EXPECT_EXIT(ParseTestFlag(true, ABSL_ARRAYSIZE(argv) - 1, argv),
              ::testing::ExitedWithCode(1),
              "Unknown command line flag 'this_flag_does_not_exist'");
  EXPECT_EXIT(ParseTestFlag(false, ABSL_ARRAYSIZE(argv) - 1, argv),
              ::testing::ExitedWithCode(1),
              "Unknown command line flag 'this_flag_does_not_exist'");
}

#if FILESYSTEM_SUPPORTED
#if !PORTABLE_BASE
// Verifies that the non-fatal error is written to STATUS.
TEST_F(ParseCommandLineFlagsUnknownFlagDeathTest,
       UnknownFlagWrittenToStatusFile) {
  std::string filename(TmpFile("STATUS"));
  unlink(filename.c_str());

  // Location of the 'STATUS' file is set via environment variable.
  setenv("GOOGLE_STATUS_DIR", ::testing::TempDir().c_str(), 1);

  // Verify that the 'STATUS' file does not exist:
  EXPECT_NE(0, access(filename.c_str(), F_OK));

  const char* argv[] = {
      "my_test",
      "--this_flag_does_not_exist",
      nullptr,
  };
  EXPECT_EXIT(ParseTestFlag(true, ABSL_ARRAYSIZE(argv) - 1, argv),
              ::testing::ExitedWithCode(1),
              "Unknown command line flag 'this_flag_does_not_exist'");

  // Verify that the 'STATUS' file was created:
}
#endif  // !PORTABLE_BASE
#endif  // FILESYSTEM_SUPPORTED

TEST_F(ParseCommandLineFlagsUnknownFlagDeathTest, BoolFlagIsCompletelyUnknown) {
  const char* argv[] = {
      "my_test",
      "--nothis_flag_does_not_exist",
      nullptr,
  };

  EXPECT_EXIT(ParseTestFlag(true, ABSL_ARRAYSIZE(argv) - 1, argv),
              ::testing::ExitedWithCode(1),
              "Unknown command line flag 'nothis_flag_does_not_exist'");
  EXPECT_EXIT(ParseTestFlag(false, ABSL_ARRAYSIZE(argv) - 1, argv),
              ::testing::ExitedWithCode(1),
              "Unknown command line flag 'nothis_flag_does_not_exist'");
}

TEST_F(ParseCommandLineFlagsUnknownFlagDeathTest, FlagIsNotABool) {
  const char* argv[] = {
      "my_test",
      "--notest_string",
      nullptr,
  };

  EXPECT_EXIT(ParseTestFlag(true, ABSL_ARRAYSIZE(argv) - 1, argv),
              ::testing::ExitedWithCode(1),
              "Negative form is not valid for the flag 'test_string'");
  EXPECT_EXIT(ParseTestFlag(false, ABSL_ARRAYSIZE(argv) - 1, argv),
              ::testing::ExitedWithCode(1),
              "Negative form is not valid for the flag 'test_string'");
}

#endif

using ParseCommandLineFlagsWrongFieldsTest = base::FlagTest;

using ParseCommandLineTest = base::FlagTest;

TEST_F(ParseCommandLineTest, TwoDashArgFirst) {
  const char* test_argv[] = {
      "my_test", "arg1",           "--test_flag=11", "arg2",
      "--",      "--test_flag=12", nullptr,
  };
  int argc = ABSL_ARRAYSIZE(test_argv) - 1;

  absl::FixedArray<char*, 0> argv_save(argc + 1);
  char** mutable_argv = argv_save.data();
  memcpy(mutable_argv, test_argv, sizeof(*mutable_argv) * (argc + 1));

  base::ParseCommandLine(&argc, &mutable_argv);
  EXPECT_EQ(4, argc);
  EXPECT_STREQ(mutable_argv[0], "my_test");
  EXPECT_STREQ(mutable_argv[1], "arg1");
  EXPECT_STREQ(mutable_argv[2], "arg2");
  EXPECT_STREQ(mutable_argv[3], "--test_flag=12");
  EXPECT_EQ(mutable_argv[4], nullptr);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag), 11);
}
#endif  // GOOGLE_COMMANDLINEFLAGS_FULL_API

// Make a flag for a user-defined type.
}  // namespace
namespace commandlineflags_unittest {
struct UserType {
  UserType() {}
  explicit UserType(const std::string& v) : value(v) {}
  std::string value;

  bool operator==(const UserType& x) const { return value == x.value; }
  bool operator!=(const UserType& x) const { return value != x.value; }
};

bool AbslParseFlag(absl::string_view text, UserType* dst, std::string* error) {
  if (text == "bad_user_type") {
    return false;
  }
  dst->value = std::string(text);
  return true;
}

bool AbslParseFlag(absl::string_view text, std::vector<UserType>* dst,
                   std::string* error) {
  std::vector<UserType> new_dst;
  std::vector<std::string> values = absl::StrSplit(text, ',');
  for (const auto& v : values) {
    new_dst.emplace_back();
    if (!absl::ParseFlag(v, &new_dst.back(), error)) return false;
  }
  *dst = std::move(new_dst);
  return true;
}

// This AbslParseFlag overload handles a type which is (at least plausibly)
// owned by the user: the UserType member means that the associated (user-owned)
// namespace is searched by ADL.
bool AbslParseFlag(absl::string_view text,
                   std::vector<std::pair<std::string, UserType>>* dst,
                   std::string* error) {
  // An empty flag value results in an empty container.
  if (text.empty()) {
    dst->clear();
    return true;
  }
  std::vector<std::pair<std::string, UserType>> new_dst;
  // Split first by commas. (Escaping is not supported.)
  std::vector<std::string> values = absl::StrSplit(text, ',');
  for (const auto& v : values) {
    // Loose error handling: assigning from StrSplit to a pair always succeeds.
    std::pair<std::string, std::string> vv = absl::StrSplit(v, '=');
    new_dst.emplace_back(vv.first, UserType());
    // Now use AbslParseFlag for the value of the user-defined type.
    if (!absl::ParseFlag(vv.second, &new_dst.back().second, error)) {
      return false;
    }
  }
  *dst = std::move(new_dst);
  return true;
}

std::string AbslUnparseFlag(const UserType& v) { return v.value; }

std::string AbslUnparseFlag(const std::vector<UserType>& v) {
  return absl::StrJoin(v, ",", [](std::string* result, const UserType& t) {
    absl::StrAppend(result, t.value);
  });
}

// Inverse of the AbslParseFlag overload above.
std::string AbslUnparseFlag(
    const std::vector<std::pair<std::string, UserType>>& v) {
  return absl::StrJoin(
      v, ",",
      [](std::string* result, const std::pair<std::string, UserType>& t) {
        absl::StrAppend(result, absl::StrJoin({t.first, t.second.value}, "="));
      });
}
}  // namespace commandlineflags_unittest

namespace {
using commandlineflags_unittest::UserType;

// Another user-defined flag type, this time an enum.
enum EnumFlag { kEnum1 = 1, kEnum2 = 2 };
std::string AbslUnparseFlag(EnumFlag v) {
  return (v == kEnum1) ? "One" : "Two";
}
bool AbslParseFlag(absl::string_view text, EnumFlag* dst, std::string* error) {
  if (text == "One") {
    *dst = kEnum1;
    return true;
  }
  if (text == "Two") {
    *dst = kEnum2;
    return true;
  }
  return false;
}

// Tests the handling of different ways of getting/setting a flag of
// type T defined via ABSL_FLAG.  val1 must be the default value for
// the flag.  val2 must be a value different from val1.  val1str must be the
// string representation of val1.  val2str must be the string
// representation of val2.
template <typename T>
void GetSet(const char* name, absl::Flag<T>* flag, T val1,
            absl::string_view val1str, T val2, const std::string& val2str) {
  ASSERT_NE(val1, val2) << name;

  // Check default value => val1
  EXPECT_EQ(val1, absl::GetFlag(*flag)) << name;

  // Change the default => val2
  SetCommandLineOptionWithMode(name, val2str, SET_FLAGS_DEFAULT);
  EXPECT_EQ(val2, absl::GetFlag(*flag)) << name;

  // Change if default (success) => val1
  SetCommandLineOptionWithMode(name, val1str, SET_FLAG_IF_DEFAULT);
  EXPECT_EQ(val1, absl::GetFlag(*flag)) << name;

  // Change if default (failure) => remains val1
  SetCommandLineOptionWithMode(name, val2str, SET_FLAG_IF_DEFAULT);
  EXPECT_EQ(val1, absl::GetFlag(*flag)) << name;

  // Parse from command line => val2
  {
    char arg0[100], arg1[100];
    snprintf(arg0, sizeof(arg0), "%s", "my_test");
    snprintf(arg1, sizeof(arg1), "--%s=%s", name, val2str.c_str());
    char* argv[] = {arg0, arg1, nullptr};
    int argc = ABSL_ARRAYSIZE(argv) - 1;
    char** argvp = argv;
    ParseCommandLineNonHelpFlags(&argc, &argvp, true);
  }
  EXPECT_EQ(val2, absl::GetFlag(*flag)) << name;

  // Use SetFlag => val1
  absl::SetFlag(flag, val1);
  EXPECT_EQ(val1, absl::GetFlag(*flag)) << name;

  // Set via generic API => val2
  SetCommandLineOption(name, val2str);
  EXPECT_EQ(val2, absl::GetFlag(*flag)) << name;

  absl::SetFlag(flag, val1);

  // Check GetByName.
  T result;
  EXPECT_TRUE(base::GetByName(name, &result)) << name;
  EXPECT_EQ(val1, result) << name;

  // Check flag's name.
  EXPECT_EQ(std::string(name), absl::GetFlagReflectionHandle(*flag).Name());
}

// Always define flags in the global namespace.
// This avoids unused variable warnings on some platforms (e.g. nacl).
}  // namespace

ABSL_FLAG(bool, test_encap_bool, false, "help");
ABSL_FLAG(int32_t, test_encap_int32, 1, "help");
ABSL_FLAG(int64_t, test_encap_int64, 2, "help");
ABSL_FLAG(uint64_t, test_encap_uint64, 3, "help");
ABSL_FLAG(double, test_encap_double, 4.0, "help");
ABSL_FLAG(std::string, test_encap_string, "a", "help");
ABSL_FLAG(std::vector<std::string>, test_encap_stringvec,
          std::vector<std::string>({"a", "b ", " ", "", "c"}), "help");
ABSL_FLAG(UserType, test_encap_user, UserType("x"), "help");
ABSL_FLAG(std::vector<UserType>, test_encap_uservec, {UserType("y")}, "help");
using PairVec = std::vector<std::pair<std::string, UserType>>;
ABSL_FLAG(PairVec, test_encap_pairvec, PairVec(), "help");

namespace {

using EncapsulatedFlagTest = base::FlagTest;

#if GOOGLE_COMMANDLINEFLAGS_FULL_API
TEST_F(EncapsulatedFlagTest, GetAndSet) {
  GetSet("test_encap_bool", &FLAGS_test_encap_bool, false, "false", true,
         "true");
  GetSet("test_encap_int32", &FLAGS_test_encap_int32, 1, "1", 2, "2");
  GetSet("test_encap_int64", &FLAGS_test_encap_int64, int64_t{2}, "2",
         int64_t{3}, "3");
  GetSet("test_encap_uint64", &FLAGS_test_encap_uint64, uint64_t{3}, "3",
         uint64_t{4}, "4");
  GetSet("test_encap_double", &FLAGS_test_encap_double, 4.0, "4", 5.0, "5");
  GetSet("test_encap_string", &FLAGS_test_encap_string, std::string("a"), "a",
         std::string("b"), "b");
  GetSet("test_encap_stringvec", &FLAGS_test_encap_stringvec,
         std::vector<std::string>{"a", "b ", " ", "", "c"}, "a,b , ,,c",
         std::vector<std::string>{"x", "y", "z"}, "x,y,z");
  GetSet("test_encap_user", &FLAGS_test_encap_user, UserType("x"), "x",
         UserType("y"), "y");
  GetSet("test_encap_uservec", &FLAGS_test_encap_uservec,
         std::vector<UserType>{UserType("y")}, "y",
         std::vector<UserType>{UserType("x"), UserType("y")}, "x,y");
  GetSet("test_encap_pairvec", &FLAGS_test_encap_pairvec, {}, "",
         std::vector<std::pair<std::string, UserType>>{{"x", UserType("a")},
                                                       {"y", UserType("b")}},
         "x=a,y=b");
}
#endif  // GOOGLE_COMMANDLINEFLAGS_FULL_API

}  // namespace

ABSL_FLAG(std::string, test_string_conversion, "foo", "help");
ABSL_FLAG(int64_t, test_int_conversion, 1, "help");
ABSL_FLAG(EnumFlag, test_user_enum, kEnum1, "");

namespace {

TEST_F(EncapsulatedFlagTest, Enum) {
  EXPECT_EQ(absl::GetFlag(FLAGS_test_user_enum), kEnum1);
  absl::SetFlag(&FLAGS_test_user_enum, kEnum2);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_user_enum), kEnum2);
}

// Test that a value of a different type can be passed to SetFlag<T>
// as long as the passed value is implicitly convertible to T.
TEST_F(EncapsulatedFlagTest, SetFlagConversion) {
  // const char* => string
  absl::SetFlag(&FLAGS_test_string_conversion, "hello");
  EXPECT_EQ("hello", absl::GetFlag(FLAGS_test_string_conversion));

  // int => int64_t
  absl::SetFlag(&FLAGS_test_int_conversion, 100);
  EXPECT_EQ(100, absl::GetFlag(FLAGS_test_int_conversion));
}

}  // namespace

ABSL_FLAG(int32_t, test_direct_int, 100, "help");

namespace {

using RetiredFlagValueTest = base::FlagTest;

TEST_F(RetiredFlagValueTest, Values) {
  std::string value;

  EXPECT_FALSE(GetCommandLineOption("legacy_bool", &value));
  EXPECT_EQ("", SetCommandLineOption("legacy_bool", "false"));

  EXPECT_FALSE(GetCommandLineOption("legacy_encap", &value));
  EXPECT_EQ("", SetCommandLineOption("legacy_encap", "100"));
}

TEST_F(RetiredFlagValueTest, StringArgWithFlagLikeValue) {
  const char* argv[] = {
      "my_test", "--legacy_encap=100", "--legacy_string", "--looks-like-a-flag",
      nullptr,
  };

  EXPECT_EQ(-1, ParseTestFlag(true, ABSL_ARRAYSIZE(argv) - 1, argv));
  EXPECT_EQ(-1, ParseTestFlag(false, ABSL_ARRAYSIZE(argv) - 1, argv));
}

// Convert a space separated arg list in *str into an argv array.
// Mutates the contents of *str.
static std::vector<char*> SplitArgs(std::string* str) {
  std::vector<char*> result;
  char* src = &(*str)[0];
  result.push_back(src);
  for (int i = 0; i < str->size(); i++) {
    if (src[i] == ' ') {
      src[i] = '\0';
      result.push_back(src + i + 1);  // Beginning of next arg
    }
  }
  result.push_back(nullptr);  // Ensure extra null at end
  return result;
}

// Convert argc/argv into a space separated string.
static std::string JoinArgs(int argc, char** argv) {
  std::string result;
  for (int i = 0; i < argc; i++) {
    if (i > 0) {
      result.push_back(' ');
    }
    result.append(argv[i]);
  }
  return result;
}

#if GOOGLE_COMMANDLINEFLAGS_FULL_API
using ParseCommandLineFlagsTest = base::FlagTest;

TEST_F(ParseCommandLineFlagsTest, Reordering) {
  struct {
    const char* input;      // Space separated list of arguments
    const char* stripped;   // Resulting arguments with remove_flags = true
    const char* reordered;  // Resulting arguments with remove_flags = false
    int first_nonopt;       // Expected result with remove_flags = false
  } cases[] = {
      {"prog", "prog", "prog", 1},
      {"prog a b c", "prog a b c", "prog a b c", 1},
      {"prog -r=3 a b c", "prog a b c", "prog -r=3 a b c", 2},
      {"prog -r=3 --test_bool a b", "prog a b", "prog -r=3 --test_bool a b", 3},
      {"prog a -r=3 b --test_bool", "prog a b", "prog -r=3 --test_bool a b", 3},
      {"prog a -r 3 b --test_bool", "prog a b", "prog -r 3 --test_bool a b", 4},
      {"prog --", "prog", "prog --", 2},
      {"prog -- a b -r=3 c", "prog a b -r=3 c", "prog -- a b -r=3 c", 2},
      {"prog a b c -- d e", "prog a b c d e", "prog -- a b c d e", 2},
      {"prog a b c --", "prog a b c", "prog -- a b c", 2},
  };
  for (const auto& test_case : cases) {
    const char* in = test_case.input;
    fprintf(stderr, "Testing [%s]\n", in);
    for (int remove_flags = 0; remove_flags <= 1; remove_flags++) {
      std::string scratch = in;
      std::vector<char*> args = SplitArgs(&scratch);
      char** argv = &args[0];
      int argc = args.size() - 1;  // Do not count terminating null
      int r = ParseCommandLineFlags(&argc, &argv, remove_flags);
      std::string out = JoinArgs(argc, argv);
      if (remove_flags) {
        EXPECT_EQ(test_case.stripped, out) << in;
        EXPECT_EQ(1, r) << in;
      } else {
        EXPECT_EQ(test_case.reordered, out) << in;
        EXPECT_EQ(test_case.first_nonopt, r) << in;
      }
      EXPECT_EQ(nullptr, argv[argc]) << in;
    }
  }
}

#if !PORTABLE_BASE
using OnlyCheckArgsDeathTest = base::FlagDeathTest;

TEST_F(OnlyCheckArgsDeathTest, FlagIsKnown) {
  const std::vector<std::string> argv = {"my_test", "--only_check_args",
                                         "--test_flag=2"};

  EXPECT_EXIT(ParseTestFlag(true, argv), ::testing::ExitedWithCode(0), "");
  EXPECT_EQ(ParseTestFlag(false, argv), 2);
}

// TODO: Fix only_check_args behavior with ParseCommandLineFlags
TEST_F(OnlyCheckArgsDeathTest, FlagIsUnknown) {
  const std::vector<std::string> argv = {"my_test", "--only_check_args",
                                         "--this_flag_does_not_exist"};

  EXPECT_EXIT(ParseTestFlag(true, argv), ::testing::ExitedWithCode(1),
              "Unknown command line flag");
  EXPECT_EXIT(ParseTestFlag(false, argv), ::testing::ExitedWithCode(1),
              "Unknown command line flag");
}

// TODO: Fix only_check_args behavior with ParseCommandLineFlags
TEST_F(OnlyCheckArgsDeathTest, FlagValueIsInvalid) {
  const std::vector<std::string> argv = {"my_test", "--only_check_args",
                                         "--test_flag=foo"};

  EXPECT_EXIT(ParseTestFlag(true, argv), ::testing::ExitedWithCode(1),
              "Illegal value");
  EXPECT_EXIT(ParseTestFlag(false, argv), ::testing::ExitedWithCode(1),
              "Illegal value");
}
#endif  // !PORTABLE_BASE
#endif  // GOOGLE_COMMANDLINEFLAGS_FULL_API

#if BENCHMARK_SUPPORTED
static void BM_FlagParsing(benchmark::State& state) {
  const int num_args = state.range(0);
  char** argv = new char*[num_args + 2];
  int argc = 0;
  char* program = strdup("my_test");
  char* arg = strdup("--test_flag=0");
  argv[argc++] = program;
  for (int i = 0; i < num_args; i++) {
    argv[argc++] = arg;
  }
  argv[argc] = nullptr;
  for (auto _ : state) {
    int tmp_argc = argc;
    ParseCommandLineNonHelpFlags(&tmp_argc, &argv, false);
  }
  free(program);
  free(arg);
  delete[] argv;
}
BENCHMARK(BM_FlagParsing)->Range(1, 1024);

static void BM_FlagSaver(benchmark::State& state) {
  int i = 0;
  for (auto _ : state) {
    absl::FlagSaver saver;
    absl::SetFlag(&FLAGS_test_int32, i++);
  }
}
BENCHMARK(BM_FlagSaver);

static void BM_GetFlagNum(benchmark::State& state) {
  uint64_t r = 0;
  for (auto _ : state) {
    r += absl::GetFlag(FLAGS_test_encap_uint64);
  }
  VLOG(1) << r;
}
BENCHMARK(BM_GetFlagNum);

static void BM_GetFlagNumDirect(benchmark::State& state) {
  uint64_t r = 0;
  for (auto _ : state) {
    r += absl::GetFlag(FLAGS_test_uint64);
  }
  VLOG(1) << r;
}
BENCHMARK(BM_GetFlagNumDirect);

static void BM_GetFlagDouble(benchmark::State& state) {
  double r = 0;
  for (auto _ : state) {
    r += absl::GetFlag(FLAGS_test_encap_double);
  }
  VLOG(1) << r;
}
BENCHMARK(BM_GetFlagDouble);

static void BM_GetFlagDoubleDirect(benchmark::State& state) {
  double r = 0;
  for (auto _ : state) {
    r += absl::GetFlag(FLAGS_test_double);
  }
  VLOG(1) << r;
}
BENCHMARK(BM_GetFlagDoubleDirect);

}  // namespace
ABSL_FLAG(std::string, test_bm_encap_string,
          "the quick brown fox jumps over the lazy dog", "help");
ABSL_FLAG(std::string, test_bm_direct_string,
          "the quick brown fox jumps over the lazy dog", "help");
namespace {

static void BM_GetFlagStr(benchmark::State& state) {
  uint64_t r = 0;
  for (auto _ : state) {
    r += absl::GetFlag(FLAGS_test_bm_encap_string).size();
  }
  VLOG(1) << r;
}
BENCHMARK(BM_GetFlagStr);

static void BM_GetFlagStrDirect(benchmark::State& state) {
  uint64_t r = 0;
  for (auto _ : state) {
    r += absl::GetFlag(FLAGS_test_bm_direct_string).size();
  }
  VLOG(1) << r;
}
BENCHMARK(BM_GetFlagStrDirect);

static void BM_SetFlag(benchmark::State& state) {
  std::string value(10000, 'x');
  for (auto _ : state) {
    absl::SetFlag(&FLAGS_test_bm_encap_string, value);
  }
  VLOG(1) << absl::GetFlag(FLAGS_test_bm_encap_string).size();
}
BENCHMARK(BM_SetFlag);

#endif  // BENCHMARK_SUPPORTED

}  // unnamed namespace

int main(int argc, char** argv) {
#if GTEST_HAS_DEATH_TEST
  // Store the original argv so that death tests can use them.
  base::FlagDeathTest::SaveArgv(argc, argv);
#endif  // GTEST_HAS_DEATH_TEST
  // We need to call SetArgv before parsing flags, so our "test" argv will
  // win out over this executable's real argv.  That makes running this
  // test with a real --help flag kinda annoying, unfortunately.
  SetArgv(ABSL_ARRAYSIZE(kTestArgv), kTestArgv);

  // The first arg is the usage message, also important for testing.
  std::string usage_message =
      (std::string(GetArgv0()) +
       ": <useless flag> [...]\nDoes something useless.\n");

#if GOOGLE_COMMANDLINEFLAGS_FULL_API
#if !PORTABLE_BASE
  // We test setting tryfromenv manually, and making sure
  // InitGoogle still evaluates it.
  absl::SetFlag(&FLAGS_tryfromenv, {"test_tryfromenv"});
  setenv("FLAGS_test_tryfromenv", "pre-set", 1);
#endif
#endif  // GOOGLE_COMMANDLINEFLAGS_FULL_API

  absl::SetFlag(&FLAGS_logtostderr, true);

#ifndef GTEST_GOOGLE3_MODE_
  // Make sure googletest args are taken out of argc/argv
  ::testing::InitGoogleTest(&argc, argv);
#endif

// Could also test absl::SetProgramUsageMessage() + ParseCommandLineFlags()
#if !GTEST_HAS_DEATH_TEST
  absl::SetProgramUsageMessage(usage_message);
#if GOOGLE_COMMANDLINEFLAGS_FULL_API
  ParseCommandLineFlags(&argc, &argv, /*remove_flags=*/true);
#endif  // GOOGLE_COMMANDLINEFLAGS_FULL_API
#elif defined(GTEST_HAS_ABSL)
  InitGoogle(/*usage=*/nullptr, &argc, &argv, /*remove_flags=*/true);
#else   // !GTEST_HAS_DEATH_TEST && !defined(GTEST_HAS_ABSL)
  InitGoogle(usage_message.c_str(), &argc, &argv, /*remove_flags=*/true);
#endif  // GTEST_HAS_DEATH_TEST

#if BENCHMARK_SUPPORTED
  if (!benchmark::GetBenchmarkFilter().empty()) {
    benchmark::RunSpecifiedBenchmarks();
    exit(0);
  }
#endif

  const int exit_status = RUN_ALL_TESTS();
  ShutDownCommandLineFlags();
  return exit_status;
}
