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

#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "devtools/build/runtime/get_runfiles_dir.h"
#include "file/base/file.h"
#include "file/base/filesystem.h"
#include "file/base/options.h"
#include "file/base/options.pb.h"
#include "file/util/temp_file.h"
#include "gloop/util/process/subprocess.h"

// Creates a temporary file with the given size in MB.
std::string CreateTempFile(int size_mb) {
  absl::StatusOr<std::string> temp_file_path_status =
      file::MakeTempFilename("", "filewrapper_benchmark_input");
  CHECK_OK(temp_file_path_status.status());
  std::string temp_file_path = temp_file_path_status.value();

  File* temp_file;
  const absl::Status temp_file_status =
      file::Open(temp_file_path, "w", &temp_file, file::Defaults());
  CHECK_OK(temp_file_status);

  std::vector<char> data_chunk(1024 * 1024, 'a');  // 1MB of 'a' characters
  absl::Cord data_cord(absl::string_view(data_chunk.data(), data_chunk.size()));
  for (int i = 0; i < size_mb; ++i) {
    CHECK_OK(temp_file->Write(data_cord, nullptr, file::Defaults()));
  }
  CHECK_OK(temp_file->Close(file::Defaults()));
  return temp_file_path;
}

static void BM_Filewrapper(benchmark::State& state) {
  const std::string binary_path = devtools_build::GetDataDependencyFilepath(
      "https://github.com/abseil/gloop/tree/main/gloop/tools/filewrapper_impl");
  const int size_mb = state.range(0);
  const std::string input_file = CreateTempFile(size_mb);

  absl::StatusOr<std::string> out_h_status =
      file::MakeTempFilename("", "output_h");
  CHECK_OK(out_h_status.status());
  const std::string out_h_file = out_h_status.value();

  absl::StatusOr<std::string> out_cc_status =
      file::MakeTempFilename("", "output_cc");
  CHECK_OK(out_cc_status.status());
  const std::string out_cc_file = out_cc_status.value();

  for (auto s : state) {
    SubProcess process;
    process.SetProgram(binary_path,
                       {binary_path, "--include_path", "tools", "--out_h",
                        out_h_file, "--out_cc", out_cc_file, "--data_in_cc",
                        "filewrapper_benchmark_data", input_file});
    process.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
    process.SetChannelAction(CHAN_STDERR, ACTION_PIPE);

    CHECK(process.Start());
    std::string stdout_str, stderr_str;
    process.Communicate(&stdout_str, &stderr_str);
    int exit_code = process.Wait();
    CHECK_EQ(exit_code, 0) << stderr_str;

    CHECK_OK(file::Delete(out_h_file, file::Defaults()));
    CHECK_OK(file::Delete(out_cc_file, file::Defaults()));
  }

  CHECK_OK(file::Delete(input_file, file::Defaults()));
}

BENCHMARK(BM_Filewrapper)->Range(1, 20);  // 1MB to 20MB
