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

#include <fstream>
#include <sstream>
#include <string>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "gloop/tools/filewrapper_testdata.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

std::string GetFileContents(const std::string& path) {
  std::ifstream f(path);
  std::stringstream buffer;
  buffer << f.rdbuf();
  return buffer.str();
}

// The variables below do not exist anywhere in the source.
// Linker magically synthesizes them if they are referenced and there is
// a section named "filewrapper_toc" in one or more objects being linked.
extern "C" FileToc* __start_filewrapper_toc;
extern "C" FileToc* __stop_filewrapper_toc;

TEST(FilewrapperTest, CompareData) {
  const char filename[] = "filewrapper_testdata.txt";

  const FileToc* file_toc = filewrapper_testdata_create();
  EXPECT_EQ(absl::string_view(filename), file_toc[0].name);
  EXPECT_EQ(nullptr, file_toc[1].name);

  std::string true_contents;
  true_contents = GetFileContents("gloop/tools/filewrapper_testdata.txt");
  ASSERT_EQ(true_contents.size(), file_toc[0].size);
  EXPECT_EQ(true_contents,
            absl::string_view(file_toc[0].data, file_toc[0].size));
}

TEST(FilewrapperTest, CheckSourceUTF8) {
  std::string file_contents;
  file_contents = GetFileContents("gloop/tools/filewrapper_testdata.cc");
  EXPECT_EQ(std::string::npos, file_contents.find('\0'));

  file_contents = GetFileContents("gloop/tools/filewrapper_testdata.h");
  EXPECT_EQ(std::string::npos, file_contents.find('\0'));
}

TEST(FilewrapperTest, EnumerateFileToc) {
  int count = 0;
  bool found_testdata = false;
  for (FileToc** pp = &__start_filewrapper_toc; pp != &__stop_filewrapper_toc;
       ++pp) {
    for (FileToc* p = (*pp); p->name != nullptr; ++p) {
      absl::string_view name(p->name);

      VLOG(1) << count << " " << p->name;

      // We expect to find at least the filewrapper_testdata.txt.
      if (name == "filewrapper_testdata.txt") found_testdata = true;
    }
    count += 1;
  }

  EXPECT_TRUE(found_testdata);

  EXPECT_GE(count, 1);
}
