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

#include "gloop/util/symbolize/elf_reader.h"

#include <elf.h>
#include <endian.h>
#include <stdlib.h>
#include <sys/mman.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/log_severity.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "gloop/base/init_google.h"
#include "gloop/base/log_file_flags.h"
#include "gloop/base/proc_maps.h"
#include "gloop/base/strerror.h"
#include "gloop/util/symbolize/symbolize-inl.h"
#include "gloop/util/symbolize/symbolize.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using absl::ScopedMockLog;
using testing::_;
using testing::ContainerEq;
using testing::HasSubstr;
using testing::UnorderedElementsAre;
using util::ElfReader;
using util::SymbolMap;

ABSL_FLAG(std::string, input, "",
          "Dump symbols in the input file, if it's not empty.");
ABSL_DECLARE_FLAG(bool, elfreader_process_dynsyms);

namespace {
// Flags to select a particular flavor of ELF binary.
enum ElfBinaryFlavor {
  ELF32 = 1 << 0,
  ELF64 = 1 << 1,
  RDYNAMIC = 1 << 2,
  STRIPPED = 1 << 3,
  HAS_DEBUG_INFO = 1 << 4,
  MANY_SECTIONS = 1 << 5,
  PPC = 1 << 6,
  SPLIT_TEXT_SECTIONS = 1 << 7,
};

// Checks if the file name is likely an executable of the running program.
// Not very rigorous but good enough here.
bool FileNameIsLikelySelfExec(const char* file_name) {
  return strstr(file_name, "elf_reader_unittest") != nullptr;
}

// Checks if the file name is likely a dynamic shared object (DSO).
// Not very rigorous but good enough here.
bool FileNameIsLikelyDSO(const char* file_name) {
  int file_name_length = strlen(file_name);
  // Check if the file name ends with .so.
  return (file_name_length >= 3 &&
          strcmp(file_name + file_name_length - 3, ".so") == 0);
}

TEST(ElfReader, IsNativeElfFile) {
  // Test a text file and a file that doesn't exist. These should not
  // be valid ELF files.
  EXPECT_FALSE(ElfReader("/proc/cpuinfo").IsNativeElfFile());
  EXPECT_FALSE(ElfReader("/some_garbage_file").IsNativeElfFile());

  // This unittest has to be a native ELF file, or it couldn't be
  // running.
  EXPECT_TRUE(ElfReader("/proc/self/exe").IsNativeElfFile());

  // Iterate over all of the code maps loaded into this process. They
  // should all be native as well.
  ProcMapsIterator it(0);
  if (it.Valid()) {
    uint64_t begin, end, pos;
    char *file_name, *flags;
    while (it.Next(&begin, &end, &flags, &pos, nullptr, &file_name)) {
      // Not our problem, since we handle these gracefully, but might
      // as well check that the iterator still returns these as
      // valid.
      EXPECT_NE(flags[0], '\0');
      EXPECT_NE(flags[1], '\0');
      EXPECT_NE(file_name, nullptr);

      // Collect only executable maps, and exclude maps like "[heap]",
      // which don't correspond to files.  Sometimes files like
      // /var/db/nscd/passwd are mapped with an executable flag for
      // some reason (this happened on goobuntu dapper for x86_64),
      // hence we only collect maps with file names likely to be an
      // executable or dynamic shared object.
      if ((flags[2] == 'x' && file_name[0] != '\0' && file_name[0] != '[') &&
          (FileNameIsLikelySelfExec(file_name) ||
           FileNameIsLikelyDSO(file_name))) {
        ElfReader reader(file_name);
        EXPECT_TRUE(reader.IsNativeElfFile());
      }
    }
  }
}

TEST(ElfReader, NonexistentFile) {
  ScopedMockLog log;
  std::string nonexistent_file = "this_file_does_not_exist";
  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, _,
                       HasSubstr("Could not open " + nonexistent_file)));
  log.StartCapturingLogs();
  ElfReader reader(nonexistent_file);  // Generates warning after open fails.
}

TEST(ElfReader, IgnoreVdso) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log(_, _, _)).Times(0);  // No log messages expected.
  log.StartCapturingLogs();
  ElfReader reader("[vdso]");
}

TEST(ElfReader, IgnoreVsyscall) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log(_, _, _)).Times(0);  // No log messages expected.
  log.StartCapturingLogs();
  ElfReader reader("[vsyscall]");
}

std::string GenerateMalformedElfForBuildId(uint32_t n_descsz) {
  Elf64_Ehdr ehdr;
  memset(&ehdr, 0, sizeof(ehdr));
  memcpy(ehdr.e_ident,
         "\x7f"
         "ELF\x02\x01\x01",
         6);
  ehdr.e_type = ET_DYN;
  ehdr.e_machine = EM_X86_64;
  ehdr.e_version = EV_CURRENT;
  ehdr.e_ehsize = sizeof(Elf64_Ehdr);
  ehdr.e_shentsize = sizeof(Elf64_Shdr);
  ehdr.e_shnum = 3;
  ehdr.e_shstrndx = 1;

  std::string shstrtab("\0.shstrtab\0.note\0", 17);
  size_t name_shstrtab = 1;
  size_t name_note = 11;

  std::string note_body;
  Elf64_Nhdr nhdr;
  nhdr.n_namesz = 4;
  nhdr.n_descsz = n_descsz;
  nhdr.n_type = NT_GNU_BUILD_ID;
  note_body.append(reinterpret_cast<const char*>(&nhdr), sizeof(nhdr));
  note_body.append("GNU\0", 4);
  note_body.append(16, '\xaa');

  size_t shstrtab_offset = sizeof(Elf64_Ehdr);
  size_t note_offset = shstrtab_offset + shstrtab.size();
  while (note_offset % 4 != 0) {
    note_offset++;
  }

  size_t shdrs_offset = note_offset + note_body.size();
  while (shdrs_offset % 8 != 0) {
    shdrs_offset++;
  }

  ehdr.e_shoff = shdrs_offset;

  std::string buffer;
  buffer.append(reinterpret_cast<const char*>(&ehdr), sizeof(ehdr));
  buffer.append(shstrtab);
  buffer.append(note_offset - (shstrtab_offset + shstrtab.size()), '\0');
  buffer.append(note_body);
  buffer.append(shdrs_offset - (note_offset + note_body.size()), '\0');

  Elf64_Shdr shdr0;
  memset(&shdr0, 0, sizeof(shdr0));
  buffer.append(reinterpret_cast<const char*>(&shdr0), sizeof(shdr0));

  Elf64_Shdr shdr1;
  memset(&shdr1, 0, sizeof(shdr1));
  shdr1.sh_name = name_shstrtab;
  shdr1.sh_type = SHT_STRTAB;
  shdr1.sh_offset = shstrtab_offset;
  shdr1.sh_size = shstrtab.size();
  shdr1.sh_addralign = 1;
  buffer.append(reinterpret_cast<const char*>(&shdr1), sizeof(shdr1));

  Elf64_Shdr shdr2;
  memset(&shdr2, 0, sizeof(shdr2));
  shdr2.sh_name = name_note;
  shdr2.sh_type = SHT_NOTE;
  shdr2.sh_offset = note_offset;
  shdr2.sh_size = note_body.size();
  shdr2.sh_addralign = 4;
  buffer.append(reinterpret_cast<const char*>(&shdr2), sizeof(shdr2));

  return buffer;
}

std::string GenerateMalformedElfForZlibHugeAlloc() {
  Elf64_Ehdr ehdr;
  memset(&ehdr, 0, sizeof(ehdr));
  memcpy(ehdr.e_ident,
         "\x7f"
         "ELF\x02\x01\x01",
         6);
  ehdr.e_type = ET_DYN;
  ehdr.e_machine = EM_X86_64;
  ehdr.e_version = EV_CURRENT;
  ehdr.e_ehsize = sizeof(Elf64_Ehdr);
  ehdr.e_shentsize = sizeof(Elf64_Shdr);
  ehdr.e_shnum = 2;
  ehdr.e_shstrndx = 1;

  std::string shstr_body;
  Elf64_Chdr chdr;
  chdr.ch_type = ELFCOMPRESS_ZLIB;
  chdr.ch_reserved = 0;
  chdr.ch_size = 0x7fffffffffffffffULL;
  chdr.ch_addralign = 1;

  shstr_body.append(reinterpret_cast<const char*>(&chdr), sizeof(chdr));
  shstr_body.append("\x78\x9c\x03\x00\x00\x00\x00\x01", 8);

  size_t shstr_offset = sizeof(Elf64_Ehdr);
  size_t shdrs_offset = shstr_offset + shstr_body.size();
  while (shdrs_offset % 8 != 0) {
    shdrs_offset++;
  }

  ehdr.e_shoff = shdrs_offset;

  std::string buffer;
  buffer.append(reinterpret_cast<const char*>(&ehdr), sizeof(ehdr));
  buffer.append(shstr_body);
  buffer.append(shdrs_offset - (shstr_offset + shstr_body.size()), '\0');

  Elf64_Shdr shdr0;
  memset(&shdr0, 0, sizeof(shdr0));
  buffer.append(reinterpret_cast<const char*>(&shdr0), sizeof(shdr0));

  Elf64_Shdr shdr1;
  memset(&shdr1, 0, sizeof(shdr1));
  shdr1.sh_name = 1;
  shdr1.sh_type = SHT_STRTAB;
  shdr1.sh_flags = SHF_COMPRESSED;
  shdr1.sh_offset = shstr_offset;
  shdr1.sh_size = shstr_body.size();
  shdr1.sh_addralign = 1;
  buffer.append(reinterpret_cast<const char*>(&shdr1), sizeof(shdr1));

  return buffer;
}

std::string GenerateMalformedElfForNullDeref() {
  Elf64_Ehdr ehdr;
  memset(&ehdr, 0, sizeof(ehdr));
  memcpy(ehdr.e_ident,
         "\x7f"
         "ELF\x02\x01\x01",
         6);
  ehdr.e_type = ET_DYN;
  ehdr.e_machine = EM_X86_64;
  ehdr.e_version = EV_CURRENT;
  ehdr.e_ehsize = sizeof(Elf64_Ehdr);
  ehdr.e_shentsize = sizeof(Elf64_Shdr);
  ehdr.e_shnum = 2;
  ehdr.e_shstrndx = 1;

  std::string shstr("\0.shstrtab", 10);

  size_t shstr_offset = sizeof(Elf64_Ehdr);
  size_t shdrs_offset = shstr_offset + shstr.size();
  while (shdrs_offset % 8 != 0) {
    shdrs_offset++;
  }

  ehdr.e_shoff = shdrs_offset;

  std::string buffer;
  buffer.append(reinterpret_cast<const char*>(&ehdr), sizeof(ehdr));
  buffer.append(shstr);
  buffer.append(shdrs_offset - (shstr_offset + shstr.size()), '\0');

  Elf64_Shdr shdr0;
  memset(&shdr0, 0, sizeof(shdr0));
  buffer.append(reinterpret_cast<const char*>(&shdr0), sizeof(shdr0));

  Elf64_Shdr shdr1;
  memset(&shdr1, 0, sizeof(shdr1));
  shdr1.sh_name = 1;
  shdr1.sh_type = SHT_STRTAB;
  shdr1.sh_offset = shstr_offset;
  shdr1.sh_size = shstr.size();
  shdr1.sh_addralign = 1;
  buffer.append(reinterpret_cast<const char*>(&shdr1), sizeof(shdr1));

  return buffer;
}

bool WriteToFile(const std::string& filepath, const std::string& buffer) {
  std::ofstream ofs(filepath, std::ios::binary);
  if (!ofs.is_open()) return false;
  ofs.write(buffer.data(), buffer.size());
  ofs.close();
  return ofs.good();
}

TEST(ElfReader, MalformedElfBuildIdOverflow) {
  std::string buffer = GenerateMalformedElfForBuildId(0x80000020);
  std::string filepath = ::testing::TempDir() + "/malformed_buildid.elf";
  ASSERT_TRUE(WriteToFile(filepath, buffer));

  ElfReader reader(filepath);
  std::string build_id = reader.GetBuildId();
  EXPECT_EQ(build_id, "");
}

TEST(ElfReader, MalformedElfBuildIdOobRead) {
  std::string buffer = GenerateMalformedElfForBuildId(0x10000000);
  std::string filepath = ::testing::TempDir() + "/malformed_buildid_oob.elf";
  ASSERT_TRUE(WriteToFile(filepath, buffer));

  ElfReader reader(filepath);
  std::string build_id = reader.GetBuildId();
  EXPECT_EQ(build_id, "");
}

TEST(ElfReader, MalformedElfZlibHugeAlloc) {
  std::string buffer = GenerateMalformedElfForZlibHugeAlloc();
  std::string filepath = ::testing::TempDir() + "/malformed_zlib.elf";
  ASSERT_TRUE(WriteToFile(filepath, buffer));

  bool r = ElfReader::IsNonDebugStrippedELFBinary(filepath);
  EXPECT_FALSE(r);
}

TEST(ElfReader, MalformedElfNullDeref) {
  std::string buffer = GenerateMalformedElfForNullDeref();
  std::string filepath = ::testing::TempDir() + "/malformed_nullderef.elf";
  ASSERT_TRUE(WriteToFile(filepath, buffer));

  bool r = ElfReader::IsNonDebugStrippedELFBinary(filepath);
  EXPECT_FALSE(r);
}

}  // namespace

int main(int argc, char** argv) {
  absl::SetFlag(&FLAGS_logtostderr, true);
  InitGoogle(argv[0], &argc, &argv, true);

  // Dumps symbols and exits if FLAGS_input is not empty.
  if (!absl::GetFlag(FLAGS_input).empty()) {
    ElfReader reader(absl::GetFlag(FLAGS_input));
    std::unique_ptr<SymbolMap> symbol_map(util::SymbolMap::GetEmpty());
    reader.AddSymbols(symbol_map.get(), 0, 0, 0);

    std::unique_ptr<SymbolMap::Iterator> iter(symbol_map->GetIterator());
    while (!iter->done()) {
      absl::PrintF("0x%08x 0x%08x %s\n", iter->start(), iter->size(),
                   iter->name());
      iter->Next();
    }
    return 0;
  }
  return RUN_ALL_TESTS();
}
