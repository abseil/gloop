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

#include "gloop/base/internal/munge_output.h"

#include <cerrno>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "gloop/util/status/errno_mapping.h"
#include "re2/re2.h"

namespace base_logging {
namespace logging_testing {
namespace {
// Munges the message portion of a log line, i.e. excluding the prefix, to
// homogenize minor platform differences.
std::string MungeMessage(std::string message) {
  absl::StrReplaceAll(
      {
          // Null pointers print differently on different platforms:
          {"ptr 0x0", "ptr (nil)"},                    // Darwin
          {"ptr 0000000000000000", "ptr (nil)"},       // MSVC
          {"ptr 0000000012345678", "ptr 0x12345678"},  // MSVC
          /* `strerror` strings differ some too: */
          {"Undefined error: 0", "Success"},
          {"No error information", "Success"},  // musl
          {"No error", "Success"},
          {"Interrupted function call", "Interrupted system call"},
          {"Device not configured", "No such device or address"},
      },
      &message);
  // Must only match at end of line so cannot use ReplaceAll above.
  if (absl::EndsWith(message, "ptr 0")) {
    absl::StrReplaceAll({{"ptr 0", "ptr (nil)"}}, &message);  // musl
  }
  return message;
}
}  // namespace

std::optional<std::string> MungeLine(std::string line) {
  // absl::FlagSaver does some logging we wish to ignore because it is not
  // present on platforms lacking full flags support.
  static constexpr LazyRE2 flagsaver_re = {"(flag.cc):[0-9]+\\] Restore saved"};
  if (RE2::PartialMatch(line, *flagsaver_re)) return std::nullopt;

  if (!line.empty() && line.back() == '\r') line.pop_back();

  // Matches the severity, filename, and message from a log line so that we can
  // reconstruct a munged line without other (unstable) metadata, e.g.
  // timestamp, thread ID.
  //
  // Example:
  //   I0615 10:25:34.205924   26084 init_google.cc:852] Message here
  // Becomes:
  //   IDATE TIME__ init_google.cc:LINE] Message here
  static constexpr LazyRE2 prefix_re = {
      R"re2(([EFIW])\d{4} \d+:\d+:\d+\.\d+\s+-?\d+ ([a-zA-Z_.-]+):\d+\](.*))re2"};
  absl::string_view severity, filename, message;
  if (RE2::FullMatch(line, *prefix_re, &severity, &filename, &message))
    return absl::StrCat(severity, "DATE TIME__ ", filename, ":LINE]",
                        MungeMessage(std::string(message)));
  return MungeMessage(std::move(line));
}

absl::StatusOr<std::string> ReadFile(absl::string_view filename) {
  std::unique_ptr<FILE, std::function<void(FILE*)>> fp(
      fopen(std::string(filename).c_str(), "rb"), [](FILE* fp) { fclose(fp); });
  if (!fp)
    return util::ErrnoToCanonicalStatus(
        errno, absl::StrCat("failed to open file ", filename, " for munging"));
  if (fseek(fp.get(), 0, SEEK_END) == -1)
    return util::ErrnoToCanonicalStatus(
        errno, absl::StrCat("failed to seek file ", filename, " for munging"));
  // NOLINTNEXTLINE(runtime/int)
  const long size = ftell(fp.get());  // NOLINT(google-runtime-int)
  if (size == -1)
    return util::ErrnoToCanonicalStatus(
        errno, absl::StrCat("failed to tell file ", filename, " for munging"));
  if (fseek(fp.get(), 0, SEEK_SET) == -1)
    return util::ErrnoToCanonicalStatus(
        errno, absl::StrCat("failed to seek file ", filename, " for munging"));
  std::string contents(size, 0);
  if (fread(&contents[0], 1, size, fp.get()) < size)
    return util::ErrnoToCanonicalStatus(
        errno, absl::StrCat("failed to read file ", filename, " for munging"));
  if (fclose(fp.release()) == -1)
    return util::ErrnoToCanonicalStatus(
        errno, absl::StrCat("failed to close file ", filename, " for munging"));
  return contents;
}

absl::StatusOr<std::string> MungeFile(absl::string_view file,
                                      absl::string_view output) {
  absl::StatusOr<std::string> raw_contents = ReadFile(file);
  if (!raw_contents.ok()) return raw_contents;
  std::deque<std::string> munged_lines;
  for (absl::string_view line :
       absl::StrSplit(*raw_contents, absl::ByChar('\n'))) {
    std::optional<std::string> munged_line = MungeLine(std::string(line));
    if (munged_line) munged_lines.push_back(*std::move(munged_line));
  }
  const std::string munged_contents = absl::StrJoin(munged_lines, "\n");
  if (!output.empty()) {
    std::unique_ptr<FILE, std::function<void(FILE*)>> fp(
        fopen(std::string(output).c_str(), "wb"), [](FILE* fp) { fclose(fp); });
    if (!fp)
      return util::ErrnoToCanonicalStatus(
          errno,
          absl::StrCat("failed to open file ", output, " for munged output"));
    if (fwrite(munged_contents.data(), sizeof(munged_contents.front()),
               munged_contents.size(), fp.get()) < munged_contents.size())
      return util::ErrnoToCanonicalStatus(
          errno, absl::StrCat("failed to write to file ", output,
                              " for munged output"));
    if (fclose(fp.release()) == -1)
      return util::ErrnoToCanonicalStatus(
          errno,
          absl::StrCat("failed to close file ", output, " for munged output"));
  }
  return munged_contents;
}
}  // namespace logging_testing
}  // namespace base_logging
