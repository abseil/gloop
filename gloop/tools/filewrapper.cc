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

// NOTE NOTE: This utility does not depend on //gloop/base, so that it can be
// used to build //gloop/base itself. Because we don't create any threads
// ourselves, this also means we need not be concerned with multithreading
// issues (like globals with non-trivial destructors).

// Utility to embed arbitrary files into C++ binaries.
//
// This utility encapsulates the given files as binary blobs in a .o file or
// as char arrays in a .cc file.
//
// Usage: %s [options] name files...
//
// By default, the following files are generated:
//     <name>.h       The header file describing the data.
//     <name>.cc      Table of contents initialization.
//     <name>_data.o  The raw data and associated symbols.
//
// Besides the names of the output files, the <name> argument also determines
// the names of initialization routines and data structures, as described
// below.
//
// The header and table of contents files are vanilla C.
//
// The header file defines the structure that stores the table of contents:
//     struct FileToc {
//       char* name;
//       char* data;
//       size_t size;
//       unsigned char md5digest[16];
//     };
//
// (This structure is also defined in
// https://github.com/abseil/gloop/tree/main/gloop/base/file_toc.h, and these
// definitions must be kept consistent!)
//
// The <name>.cc defines the functions:
//     const struct FileToc* <name>_create();
//     size_t <name>_size();
//
// The create function initializes and returns a static array containing the
// table of contents, while the size function returns the size of that array.

#include <fts.h>
#include <getopt.h>
#include <openssl/md5.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iosfwd>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#define LITE_MD5_CTX MD5_CTX
#define MD5_DIGEST_SIZE MD5_DIGEST_LENGTH

auto& MD5_init = ::MD5_Init;
auto& MD5_update = ::MD5_Update;

#ifndef PATH_MAX
#define PATH_MAX (512)
#endif

namespace {

std::string runfiles;  // NOLINT(runtime/string)
std::string progname;  // NOLINT(runtime/string)

// Command-line flags.
bool FLAGS_allow_dir = false;
bool FLAGS_create_header = true;
bool FLAGS_create_impl = true;
bool FLAGS_data_in_cc = false;
bool FLAGS_eliminate_duplication = true;
bool FLAGS_flatten = false;
bool FLAGS_help = false;
bool FLAGS_c_linkage = false;
bool FLAGS_redact_filename = false;

std::string FLAGS_include_path = "";    // NOLINT(runtime/string)
std::string FLAGS_ld = "ld";            // NOLINT(runtime/string)
std::string FLAGS_ld_opts = "";         // NOLINT(runtime/string)
std::string FLAGS_namespace = "";       // NOLINT(runtime/string)
std::string FLAGS_objcopy = "objcopy";  // NOLINT(runtime/string)
std::string FLAGS_objcopy_opts =        // NOLINT(runtime/string)
    "-I binary -B i386 -O elf32-i386";
bool FLAGS_objcopy_non_executable_stack = true;
std::string FLAGS_out_cc = "";  // NOLINT(runtime/string)
std::string FLAGS_out_h = "";   // NOLINT(runtime/string)
std::string FLAGS_out_o = "";   // NOLINT(runtime/string)
std::string FLAGS_toc_section_name =
    "filewrapper_toc";  // NOLINT(runtime/string)
bool FLAGS_sort_toc = false;
std::vector<std::string> FLAGS_strip;
std::string FLAGS_align = "16";  // NOLINT(runtime/string)

bool* const kBoolFlags[] = {
    /* 0 */ &FLAGS_allow_dir,
    /* 1 */ &FLAGS_create_header,
    /* 2 */ &FLAGS_create_impl,
    /* 3 */ &FLAGS_data_in_cc,
    /* 4 */ &FLAGS_eliminate_duplication,
    /* 5 */ &FLAGS_flatten,
    /* 6 */ &FLAGS_help,
    /* 7 */ &FLAGS_objcopy_non_executable_stack,
    /* 8 */ &FLAGS_sort_toc,
    /* 9 */ &FLAGS_c_linkage,
    /* A */ &FLAGS_redact_filename,
};

std::string* const kStrFlags[] = {
    /* 0 */ &FLAGS_include_path,
    /* 1 */ &FLAGS_ld,
    /* 2 */ &FLAGS_ld_opts,
    /* 3 */ &FLAGS_namespace,
    /* 4 */ &FLAGS_objcopy,
    /* 5 */ &FLAGS_objcopy_opts,
    /* 6 */ &FLAGS_out_cc,
    /* 7 */ &FLAGS_out_h,
    /* 8 */ &FLAGS_out_o,
    /* 9 */ &FLAGS_align,
    /* A */ &FLAGS_toc_section_name,
};

std::vector<std::string>* const kStrVecFlags[] = {
    /* 0 */ &FLAGS_strip,
};

// Return values for getopt_long_only().
#define BOOL_FLAG(n) (('b' << 8) + (n))
#define STRING_FLAG(n) (('s' << 8) + (n))
#define STRVEC_FLAG(n) (('v' << 8) + (n))

struct option options[] = {
    {"align", required_argument, nullptr, STRING_FLAG(9)},

    {"allow_dir", optional_argument, nullptr, BOOL_FLAG(0)},
    {"noallow_dir", no_argument, nullptr, BOOL_FLAG(0)},

    {"create_header", optional_argument, nullptr, BOOL_FLAG(1)},
    {"nocreate_header", no_argument, nullptr, BOOL_FLAG(1)},

    {"create_impl", optional_argument, nullptr, BOOL_FLAG(2)},
    {"nocreate_impl", no_argument, nullptr, BOOL_FLAG(2)},

    {"data_in_cc", optional_argument, nullptr, BOOL_FLAG(3)},
    {"nodata_in_cc", no_argument, nullptr, BOOL_FLAG(3)},

    {"eliminate_duplication", optional_argument, nullptr, BOOL_FLAG(4)},
    {"noeliminate_duplication", no_argument, nullptr, BOOL_FLAG(4)},

    {"flatten", optional_argument, nullptr, BOOL_FLAG(5)},
    {"noflatten", no_argument, nullptr, BOOL_FLAG(5)},

    {"redact_filename", optional_argument, nullptr, BOOL_FLAG(10)},
    {"noredact_filename", no_argument, nullptr, BOOL_FLAG(10)},

    {"help", optional_argument, nullptr, BOOL_FLAG(6)},
    {"nohelp", no_argument, nullptr, BOOL_FLAG(6)},
    {"helpfull", optional_argument, nullptr, BOOL_FLAG(6)},
    {"nohelpfull", no_argument, nullptr, BOOL_FLAG(6)},
    {"helpshort", optional_argument, nullptr, BOOL_FLAG(6)},
    {"nohelpshort", no_argument, nullptr, BOOL_FLAG(6)},

    {"include_path", required_argument, nullptr, STRING_FLAG(0)},

    {"ld", required_argument, nullptr, STRING_FLAG(1)},
    {"ldopts", required_argument, nullptr, STRING_FLAG(2)},

    {"namespace", required_argument, nullptr, STRING_FLAG(3)},

    {"objcopy", required_argument, nullptr, STRING_FLAG(4)},
    {"objcopy_opts", required_argument, nullptr, STRING_FLAG(5)},
    {"objcopy_non_executable_stack", optional_argument, nullptr, BOOL_FLAG(7)},
    {"noobjcopy_non_executable_stack", no_argument, nullptr, BOOL_FLAG(7)},

    {"out_cc", required_argument, nullptr, STRING_FLAG(6)},
    {"out_h", required_argument, nullptr, STRING_FLAG(7)},
    {"out_o", required_argument, nullptr, STRING_FLAG(8)},

    {"sort_toc", optional_argument, nullptr, BOOL_FLAG(8)},
    {"nosort_toc", no_argument, nullptr, BOOL_FLAG(8)},

    {"c_linkage", optional_argument, nullptr, BOOL_FLAG(9)},
    {"noc_linkage", no_argument, nullptr, BOOL_FLAG(9)},

    {"strip", required_argument, nullptr, STRVEC_FLAG(0)},
    {"toc_section_name", optional_argument, nullptr, STRING_FLAG(10)},

    {nullptr, 0, nullptr, 0},
};

// Help text consistent with previous python implementation.
int Usage() {
  std::cout << "Usage: " << progname << " [options] name files...\n";
  std::cout << "       " << progname << " --nocreate_impl [options] name\n";
  std::cout << R"(
  --align: align embedded data to this value.
    (default: '16')
  --[no]allow_dir: Recursively expand directories to their contents
    (default: 'false')
  --[no]create_header: Whether to create the .h file
    (default: 'true')
  --[no]create_impl: Whether to create the .o and .cc files
    (default: 'true')
  --[no]data_in_cc: Whether to have the data as char arrays in the .cc file.
    There will be no _data.o file as output. This makes it portable but the .cc
    file is ~2.5x the size of the input files.
    (default: 'false')
  --eliminate_duplication: Whether to share a single copy of the data
    between TOC entries that have the same size and md5digest.
    (default: 'true')
  --[no]flatten: Strip all directories from file names
    (default: 'false')
  --include_path: Path to use when writing #include
    (default: '')
  --ld: Path to ld utility
    (default: 'ld')
  --ldopts: ld options
    (default: '')
  --namespace: C++ namespace to wrap symbols in.
    (default: '')
  --objcopy: Path to objcopy utility
    (default: 'objcopy')
  --[no]objcopy_non_executable_stack: whether to add a .note.GNU-stack section
    via objcopy flags
    (default: 'true')
  --objcopy_opts: objcopy options to set the .o to be "normal" for this platform
    (default: '-I binary -B i386 -O elf32-i386')
  --out_cc: Filename for the .cc file
    (default: '')
  --out_h: Filename for the .h file
    (default: '')
  --out_o: Filename for the .o file
    (default: '')
  --[no]sort_toc: Whether to sort the TOC entries by name.
    (default: 'false')
  --strip: Leading prefix to strip off of file names;
    repeat this option to specify a list of values
    (default: "['']")
  --toc_section_name: Put generated TOC into the given ELF section.
    (default: "filewrapper_toc", set to empty to disable)
  --[no]c_linkage: whether to declare functions with c linkage
    (default: 'false')
  )";
  return 1;
}

// Similar to BSD errx().
void FatalError(const char* fmt, ...) {
  fprintf(stderr, "%s: ", progname.c_str());
  std::va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
  exit(1);
}

int FlagError(const char* flag) {
  std::cerr << "Flags parsing error: ";
  struct option* op = options;
  while (op->name != nullptr && strcmp(flag, op->name) != 0) ++op;
  if (op->name == nullptr) {
    std::cerr << "Unknown command line flag '" << flag << "'\n";
  } else {
    std::cerr << "Missing value for flag --" << flag << "\n";
  }
  std::cerr << "Pass --help to see help on flags.\n";
  return 1;
}

bool BoolFlag(const char* flag, const char* arg, bool* var) {
  const bool no = (strncmp(flag, "no", 2) == 0);
  if (arg == nullptr) {
    *var = !no;
    return true;
  }
  assert(!no);
  for (const char* t : {"true", "t", "1"}) {
    if (strcasecmp(arg, t) == 0) {
      *var = true;
      return true;
    }
  }
  for (const char* f : {"false", "f", "0"}) {
    if (strcasecmp(arg, f) == 0) {
      *var = false;
      return true;
    }
  }
  std::cerr << "Flags parsing error: ";
  std::cerr << "flag --" << flag << ": ";
  std::cerr << "('Non-boolean argument to boolean flag', '" << arg << "')\n";
  return false;
}

bool StrFlag(const char* flag, const char* arg, std::string* var) {
  assert(arg != nullptr);
  *var = arg;
  return true;
}

bool StrVecFlag(const char* flag, const char* arg,
                std::vector<std::string>* var) {
  assert(arg != nullptr);
  const char* ap = arg;
  for (const char* p = ap; *p != '\0'; ++p) {
    if (*p == ',') {
      var->push_back(std::string(ap, p - ap));
      ap = p + 1;
    }
  }
  var->push_back(ap);
  return true;
}

std::string Basename(const std::string& path) {
  const std::string::size_type pos = path.find_last_of('/');
  if (pos == std::string::npos) return path;
  return path.substr(pos + 1);
}

std::streamoff Size(const std::string& path) {
  struct stat buf;
  if (stat(path.c_str(), &buf) == -1) return 0;
  return buf.st_size;
}

bool IsDir(const std::string& path) {
  struct stat buf;
  if (stat(path.c_str(), &buf) == -1) return false;
  return S_ISDIR(buf.st_mode);
}

// Determine the "runfiles" directory from argv[0].
std::string RunFiles(int argc, char** argv) {
  if (argc <= 0) return std::string();

  assert(strlen(argv[0]) < PATH_MAX);
  char path[PATH_MAX];
  strcpy(path, argv[0]);  // NOLINT(runtime/printf)
  while (readlink(path, path, sizeof path) > 0) {
    std::string link = path;
    std::string::size_type pos = link.find("/<path>");
    if (pos == std::string::npos) continue;
    if (std::string r = link.substr(0, pos) + "/<path>"; IsDir(r)) {
      return r;
    }
  }

  std::string runfiles = argv[0];
  const std::string suffix = ".runfiles/";
  std::string::size_type pos = runfiles.rfind(suffix);
  if (pos != std::string::npos) {
    return runfiles.substr(0, pos + suffix.size());
  }
  pos = runfiles.find_last_of('.');
  if (pos != std::string::npos) {
    if (runfiles.find_first_of('/', pos) == std::string::npos) {
      runfiles.erase(pos);
    }
  }
  runfiles += suffix;
  return runfiles;
}

std::string ProgramInvocationShortName(int argc, char** argv) {
  if (argc <= 0) return "filewrapper";
  return Basename(argv[0]);
}

int ParseCommandLineFlags(int* argc, char*** argv) {
  const int ac = *argc;
  char** const av = *argv;
  opterr = 0;  // no error messages from getopt_long_only()
  for (;;) {
    int opt_index = 0;
    const char* const arg = av[optind ? optind : 0];
    const int value = getopt_long_only(ac, av, "", options, &opt_index);
    if (value == -1) break;  // no more options
    if (value == '?') return FlagError(arg + 2);
    const char* const flag = options[opt_index].name;
    int index = (value & 0xff);
    switch (value >> 8) {
      case 'b':
        assert(index < sizeof kBoolFlags / sizeof kBoolFlags[0]);
        if (kBoolFlags[index] == &FLAGS_help) return Usage();
        if (!BoolFlag(flag, optarg, kBoolFlags[index])) return 1;
        break;
      case 's':
        assert(index < sizeof kStrFlags / sizeof kStrFlags[0]);
        if (!StrFlag(flag, optarg, kStrFlags[index])) return 1;
        break;
      case 'v':
        assert(index < sizeof kStrVecFlags / sizeof kStrVecFlags[0]);
        if (!StrVecFlag(flag, optarg, kStrVecFlags[index])) return 1;
        break;
      default:
        assert(false);
        break;
    }
  }
  *argc -= optind;
  *argv += optind;
  return 0;
}

// Expands any directories in the input list to the files they contain.
std::vector<std::string> ExpandDirs(const std::vector<std::string>& infiles) {
  std::vector<std::string> allfiles;
  for (auto filename : infiles) {
    char* const paths[] = {&filename[0], nullptr};
    if (FTS* const tree = fts_open(paths, FTS_LOGICAL, nullptr)) {
      while (FTSENT* node = fts_read(tree)) {
        if (node->fts_info == FTS_F) {
          allfiles.push_back(node->fts_path);
        } else if (node->fts_info == FTS_D && !FLAGS_allow_dir) {
          FatalError("refusing to process dir '%s'", node->fts_path);
        }
      }
      fts_close(tree);
    }
  }
  return allfiles;
}

// A short, escaped representation of a character.  We choose octal
// escapes as they always end after three characters.
std::string Escape(unsigned char c) {
  static const char kDigits[] = "01234567";
  std::string::value_type buf[sizeof "\\377"];
  std::string::value_type* ep = buf + sizeof buf;
  std::string::value_type* p = ep;
  switch (c) {
    case '"':
    case '?':
    case '\\':
      *--p = c;
      break;
    case '\a':
      *--p = 'a';
      break;
    case '\b':
      *--p = 'b';
      break;
    case '\f':
      *--p = 'f';
      break;
    case '\n':
      *--p = 'n';
      break;
    case '\r':
      *--p = 'r';
      break;
    case '\v':
      *--p = 'v';
      break;
    case '\t':
      *--p = 't';
      break;
    default:
      *--p = kDigits[c & 7];
      if ((c >>= 3) != 0) {
        *--p = kDigits[c & 7];
        if ((c >>= 3) != 0) {
          *--p = kDigits[c & 3];
        }
      }
      break;
  }
  *--p = '\\';
  return std::string(p, ep - p);
}

std::string TrimFront(const std::string& s, char c) {
  std::string::size_type pos = 0;
  while (pos < s.size() && s[pos] == c) ++pos;
  return s.substr(pos);
}

std::string TrimBack(const std::string& s, char c) {
  std::string::size_type pos = s.size();
  while (pos > 0 && s[pos - 1] == c) --pos;
  return s.substr(0, pos);
}

// Strip out non-alphanumeric characters, replacing them with underscore.
std::string ToCIdentifier(const std::string& s) {
  std::string symbol = s;
  for (auto& c : symbol)
    if (!isalnum(c)) c = '_';
  return symbol;
}

std::vector<std::string> Split(const std::string& str, const std::string& sep) {
  std::vector<std::string> result;
  std::string::size_type pos = 0;
  for (;;) {
    std::string::size_type end = str.find_first_of(sep, pos);
    std::string::size_type len = (end == std::string::npos ? end : end - pos);
    result.push_back(str.substr(pos, len));
    if (end == std::string::npos) break;
    pos = end + sep.size();
  }
  return result;
}

// Generates (possibly nested) namespace wrapping.
std::pair<std::string, std::string> GetNamespaces() {
  std::string intro;
  std::string outro;
  if (!FLAGS_namespace.empty()) {
    for (const auto& ns : Split(FLAGS_namespace, "::")) {
      intro = intro + "namespace " + ns + " {\n";
      outro = "}  // namespace " + ns + "\n" + outro;
    }
    intro = intro + "\n";
    outro = "\n" + outro;
  }
  return std::make_pair(intro, outro);
}

// Information about an encapsulated file.
struct Initializer {
  Initializer(std::string f, std::string s, std::streamoff sz,
              LITE_MD5_CTX* digest)
      : filename(FLAGS_redact_filename ? "" : std::move(f)),
        sym(std::move(s)),
        size(sz) {
    static_assert(MD5_DIGEST_SIZE == 16,
                  "MD5 digest size must be 16 bytes (not hex)");
    MD5_Final(md5digest, digest);
  }
  std::string filename;
  std::string sym;
  std::streamoff size;
  unsigned char md5digest[16];
};

std::string_view Md5DigestAsSV(const Initializer& initializer) {
  return std::string_view(reinterpret_cast<const char*>(initializer.md5digest),
                          sizeof(initializer.md5digest));
}

// Checks if there is a previous symbol with the same size/md5digest.
// Returns a pointer to the symbol of a previously found equivalent
// initializer, or nullptr if none is found.
const std::string* PreviousEquivalentSymbol(
    const Initializer& new_initializer,
    const std::unordered_map<std::string_view, const Initializer*>&
        md5_to_initializer) {
  if (FLAGS_eliminate_duplication) {
    if (auto it = md5_to_initializer.find(Md5DigestAsSV(new_initializer));
        it != md5_to_initializer.end() &&
        new_initializer.size == it->second->size) {
      return &it->second->sym;
    }
  }
  return nullptr;  // no match
}

// Converts each file to a .o and link them together into a single .o.
// Returns (file, symbol, size, md5digest) info in "initializers", and
// declarations for the symbol in "externs", one element in each vector
// for each input file.
//
// For each input file to be embedded, we run objcopy to create a .o object
// file.  The object file contains the contents of the input file, along
// with three symbols that are automatically defined by objcopy:
//
//   _binary_objfile_start
//   _binary_objfile_end
//   _binary_objfile_size
//
// The _start symbol and the measured size of the file are used to build
// the table of contents.
void EncapsulateFiles(const std::vector<std::string>& infiles,
                      const std::string& obj_name,
                      std::vector<Initializer>& initializers,
                      std::vector<std::string>& externs) {
  const std::string set_symbol_size = runfiles + "<path>";

  // Create a temporary directory for the intermediate .o files.  Note
  // that objcopy chooses the symbol names based on the output filename,
  // including any path components.  (See the computation of "symbol"
  // below.)  We want the symbol names to be guaranteed unique within an
  // executable, but we also want the build to be deterministic across
  // runs, and insensitive to the absolute locations of source or output
  // files.  So we name the tmpdir based on the (presumed relative)
  // output filename.
  std::string tmpdir = obj_name + ".tmpdir." + progname;
  if (mkdir(tmpdir.c_str(), 0777) != 0 && errno != EEXIST) {
    FatalError("Could not create temporary directory '%s'", tmpdir.c_str());
  }
  tmpdir += '/';

  std::string objcopy = FLAGS_objcopy;
  if (!FLAGS_objcopy_opts.empty()) {
    objcopy += " " + FLAGS_objcopy_opts;
  }

  // By default, Linux binaries have executable stacks, which is unwise for
  // security reasons.  To have a non-executable stack, every object file
  // linked into the binary must explicitly indicate that it does not require
  // an executable stack.  This is indicated by the presence of a section
  // named ".note.GNU-stack".  Thus, unless someone indicates otherwise on the
  // command line, we create such a section by passing the appropriate flag to
  // objcopy.  See b/2372300.
  if (FLAGS_objcopy_non_executable_stack) {
    objcopy += " --add-section .note.GNU-stack=/dev/null";
  }

  std::vector<std::string> filecopies;
  std::vector<std::string> objfiles;
  bool empty_files = false;

  // We will keep pointers into initializers in the md5_to_initializer map.
  // Prevent reallocation.
  initializers.reserve(infiles.size());

  // This map will point into `initializers`.
  std::unordered_map<std::string_view, const Initializer*> md5_to_initializer;

  std::size_t seq = 0;
  for (const auto& filename : infiles) {
    const std::streamoff size = Size(filename);
    const std::string seq_str = std::to_string(seq++);

    const std::string filecopy = tmpdir + "s" + seq_str;
    const std::string obj = tmpdir + "f" + seq_str;
    const std::string symbol =
        ToCIdentifier("_binary_" + tmpdir + "s" + seq_str + "_start");

    LITE_MD5_CTX digest;
    MD5_init(&digest);

    if (size == 0) {
      // NOTE: objcopy >2.13 disallows empty files.
      empty_files = true;
    } else {
      // Copy the input file to the temp directory and append a NUL.
      std::ifstream f_in(filename, std::ios_base::in | std::ios_base::binary);
      std::ofstream f_out(filecopy, std::ios_base::out | std::ios_base::trunc |
                                        std::ios_base::binary);
      if (!f_out.is_open()) {
        FatalError("Could not open '%s' for writing", filecopy.c_str());
      }
      const std::size_t kBufSize = 4096;
      std::unique_ptr<unsigned char[]> buf(new unsigned char[kBufSize]);
      auto rbuf = reinterpret_cast<char*>(buf.get());
      for (;;) {
        f_in.read(rbuf, kBufSize);
        const std::streamsize cc = f_in.gcount();
        if (cc == 0) break;
        f_out.write(rbuf, cc);
        MD5_update(&digest, buf.get(), cc);
      }
      f_out.write("", 1);
      f_out.close();
      f_in.close();
      if (f_out.fail() || !f_in.eof()) {
        FatalError("Failed to copy '%s' to '%s'", filename.c_str(),
                   filecopy.c_str());
      }
      filecopies.push_back(filecopy);

      std::string cmd = objcopy + " " + filecopy + " " + obj;
      if (system(cmd.c_str()) != 0) {
        FatalError("objcopy failed");
      }
      cmd = set_symbol_size + " " + obj;
      cmd += " " + symbol + " " + std::to_string(size);
      if (system(cmd.c_str()) != 0) {
        FatalError("set_symbol_size failed");
      }
      objfiles.push_back(std::move(obj));
    }

    Initializer initializer(filename, "&" + symbol, size, &digest);

    // Drop this symbol in favor of any previous equivalent one.
    const std::string* prev =
        PreviousEquivalentSymbol(initializer, md5_to_initializer);
    if (prev != nullptr) {
      if (size != 0) {
        unlink(objfiles.back().c_str());
        objfiles.pop_back();
      }
      initializer.sym = *prev;
    } else {
      externs.push_back("extern const char " + symbol + ";");
    }

    initializers.push_back(std::move(initializer));
    const auto& last_initializer = initializers.back();
    md5_to_initializer[Md5DigestAsSV(last_initializer)] = &last_initializer;
  }

  if (objfiles.empty() && empty_files) {
    FatalError("requires at least one non empty file");
  }

  // Parse FLAGS_ld_opts into individual args.
  std::vector<std::string> ld_opts;
  if (!FLAGS_ld_opts.empty()) {
    std::size_t idx = 0;
    while (true) {
      std::size_t next_idx = FLAGS_ld_opts.find(' ', idx);
      if (next_idx == std::string::npos) {
        // No more spaces found.
        ld_opts.push_back(FLAGS_ld_opts.substr(idx, std::string::npos));
        break;
      } else if (next_idx > idx) {
        ld_opts.push_back(FLAGS_ld_opts.substr(idx, next_idx - idx));
      }
      idx = next_idx + 1;
    }
  }

  // Build a relocatable linker script which aligns the input sections.
  std::string script_name = tmpdir + "linker_script.xr";
  std::ofstream script_out(script_name.c_str(), std::ios_base::out |
                                                    std::ios_base::trunc |
                                                    std::ios_base::binary);
  if (!script_out.is_open()) {
    FatalError("Could not open '%s' for writing (%s)", script_name.c_str(),
               strerror(errno));
  }
  script_out << "SECTIONS { .lrodata    : SUBALIGN(" << FLAGS_align
             << ") { *(.lrodata); }\n"
             << "           .embeddings : SUBALIGN(" << FLAGS_align
             << ") { *(.embeddings); }\n"
             << "           .rodata     : SUBALIGN(" << FLAGS_align
             << ") { *(.rodata); }\n"
             << "           .data       : SUBALIGN(" << FLAGS_align
             << ") { *(.data); } }\n";
  script_out.close();
  if (script_out.fail()) {
    FatalError("Couldn't write %s (%s)", script_name.c_str(), strerror(errno));
  }

  // Build the list of args to pass to ld, which will link all of the wrapped
  // objects together.
  int num_args = objfiles.size() + ld_opts.size() + 7;
  const char** args_array = new const char*[num_args];

  int arg_idx = 0;
  args_array[arg_idx++] = FLAGS_ld.c_str();
  for (const auto& opt : ld_opts) {
    args_array[arg_idx++] = opt.c_str();
  }
  args_array[arg_idx++] = "-r";
  args_array[arg_idx++] = "-o";
  args_array[arg_idx++] = obj_name.c_str();
  args_array[arg_idx++] = "-T";
  args_array[arg_idx++] = script_name.c_str();
  for (const auto& objfile : objfiles) {
    args_array[arg_idx++] = objfile.c_str();
  }
  args_array[arg_idx++] = nullptr;

  if (arg_idx != num_args) {
    FatalError("problem counting args");
  }

  int pid = fork();
  if (pid == -1) {
    FatalError("fork() failed");
  }

  if (pid == 0) {
    execvp(FLAGS_ld.c_str(), const_cast<char**>(args_array));
    FatalError("ld via execvp failed (child)");
  } else {
    int status;
    int waitpid_result = waitpid(pid, &status, 0);
    if (waitpid_result == -1) {
      FatalError("waitpid() failed");
    }
    if (WIFSIGNALED(status)) {
      int signo = WTERMSIG(status);
      fprintf(stderr, "%s: child killed by signal %d (%s)\n", progname.c_str(),
              signo, strsignal(signo));
      exit(128 | signo);
    }
    if (!WIFEXITED(status)) {
      FatalError("unexpected wait status %d", status);
    }
    if (WEXITSTATUS(status) != 0) {
      fprintf(stderr, "%s: execvp or ld was unsuccessful\n", progname.c_str());
      exit(WEXITSTATUS(status));
    }
  }

  // Remove the temporary directory.
  unlink(script_name.c_str());
  for (const auto& obj : objfiles) unlink(obj.c_str());
  for (const auto& filecopy : filecopies) unlink(filecopy.c_str());
  if (rmdir(tmpdir.c_str()) != 0) {
    FatalError("Could not remove temporary directory '%s'", tmpdir.c_str());
  }
}

// "what-was-done" comment written at the top of each file.
std::string Comment(const std::string& base) {
  std::string comment;
  comment = "//  Automatically generated by tools/filewrapper\n";
  comment += "//    " + base + "\n";
  if (FLAGS_flatten) comment += "//    --flatten\n";
  for (const auto& strip : FLAGS_strip) {
    comment += "//    --strip " + strip + "\n";
  }
  if (FLAGS_data_in_cc) comment += "//    --data_in_cc\n";
  return comment;
}

// Generates a header guard for the TOC factory.
std::string GetHeaderGuard(const std::string& base) {
  std::string guard;
  if (!FLAGS_namespace.empty()) {
    for (const auto& ns : Split(FLAGS_namespace, "::")) {
      guard += ns;
      guard += '_';
    }
  }
  guard += base;
  return guard;
}

// Writes the table of contents .h file.
void WriteHeader(const std::string& filename, const std::string& comment,
                 const std::pair<std::string, std::string>& namespaces,
                 const std::string& base) {
  std::ofstream hdr(filename, std::ios_base::out | std::ios_base::trunc);
  if (!hdr.is_open()) {
    FatalError("Unable to open header file '%s' for writing", filename.c_str());
  }

  hdr << comment << "//  Output: " << filename << "\n\n";

  std::ifstream toc(runfiles + "_main/gloop/base/file_toc.h");
  if (toc.is_open()) {
    std::string line;
    while (std::getline(toc, line)) hdr << line << "\n";
    toc.close();
  } else {
    hdr << "#include \"gloop/base/file_toc.h\"\n";
  }
  hdr << "\n";

  const std::string guard = "__STRUCT_FILE_TOC_" + GetHeaderGuard(base) + "_";
  hdr << "#ifndef " << guard << "\n";
  hdr << "#define " << guard << "\n";
  hdr << "\n" << namespaces.first;
  const std::string extern_decl =
      FLAGS_c_linkage ? "BASE_FILE_TOC_EXTERN " : "";
  hdr << extern_decl << "const struct FileToc* " << base << "_create();\n";
  hdr << extern_decl << "size_t " << base << "_size();\n";
  hdr << namespaces.second << "\n";
  hdr << "#endif  // " << guard << "\n";
  hdr.close();
  if (hdr.fail()) {
    FatalError("Error during header creation");
  }
}

// Embeds each file into the .cc file as string literal.
std::vector<Initializer> EmbedFiles(const std::vector<std::string>& infiles,
                                    const std::string& cc_name,
                                    std::ofstream& f_cc) {
  // For each input file we create an array named dataX (where X is a sequence
  // number starting at 0) in an anonymous namespace.
  //
  // Although the worst-case expansion of the data is 316% (for a sequence of
  // entirely high-bit chars), the size expansion for random data is a more
  // modest 162%, and text (or mostly text) will be almost unexpanded.
  std::vector<Initializer> initializers;
  std::size_t seq = 0;

  // This map will point into `initializers`.
  std::unordered_map<std::string_view, const Initializer*> md5_to_initializer;

  // We store pointers into `initializers` in `md5_to_initializer` map.
  // Prevent reallocation.
  initializers.reserve(infiles.size());

  // Copied from base/port.h and renamed.
  f_cc << "#if defined(COMPILER_MSVC)\n";
  f_cc << "#define ALIGN_ATTRIBUTE(X) __declspec(align(X))\n";
  f_cc << "#elif defined(__GNUC__) || defined(COMPILER_ICC)\n";
  f_cc << "#define ALIGN_ATTRIBUTE(X) __attribute__((aligned(X)))\n";
  f_cc << "#endif\n";

  f_cc << "namespace {\n";

  for (const auto& filename : infiles) {
    // Embed the input file into the .cc file.
    std::streamoff size = 0;
    std::ifstream f_in(filename, std::ios_base::in | std::ios_base::binary);
    if (!f_in.is_open()) {
      FatalError("Unable to open input file '%s'", filename.c_str());
    }

    const std::streampos offset = f_cc.tellp();

    const std::string seq_str = std::to_string(seq++);
    const std::string symbol =
        ToCIdentifier("filewrapper_" + seq_str + "_" + filename);

    f_cc << "ALIGN_ATTRIBUTE(" << FLAGS_align << ") "
         << "const char " << symbol << "[] =\n";
    std::string pending_line = "\"";
    bool esc_digit = false;

    LITE_MD5_CTX digest;
    MD5_init(&digest);

    const std::size_t kBufSize = 4096;
    std::unique_ptr<unsigned char[]> buf(new unsigned char[kBufSize]);
    auto rbuf = reinterpret_cast<char*>(buf.get());
    for (;;) {
      f_in.read(rbuf, kBufSize);
      const std::streamsize cc = f_in.gcount();
      if (cc == 0) break;
      for (std::streamsize i = 0; i < cc; ++i) {
        unsigned char c = rbuf[i];
        std::string rep(1, c);  // default to self
        if (!isprint(c) || c == '"' || c == '?' || c == '\\' ||
            (isdigit(c) && esc_digit)) {
          rep = Escape(c);  // "\0" through "\377"
          esc_digit = (rep.size() < 4 && isdigit(rep.back()));
        }
        if (pending_line.size() + rep.size() > 79) {
          f_cc << pending_line << "\"\n";
          pending_line = "\"";
        }
        pending_line += rep;
      }
      MD5_update(&digest, buf.get(), cc);
      size += cc;
    }
    f_cc << pending_line << "\";\n";

    f_in.close();
    if (!f_in.eof()) {
      FatalError("Unable to read input file '%s'", filename.c_str());
    }

    Initializer initializer(filename, symbol, size, &digest);

    // Drop this symbol in favor of any previous equivalent one.
    const std::string* prev =
        PreviousEquivalentSymbol(initializer, md5_to_initializer);
    if (prev != nullptr) {
      f_cc.seekp(offset);
      initializer.sym = *prev;
    }

    initializers.push_back(std::move(initializer));

    const Initializer& last_initializer = initializers.back();
    md5_to_initializer[Md5DigestAsSV(last_initializer)] = &last_initializer;
  }

  f_cc << "}  // namespace\n";
  return initializers;
}

// Writes the .cc file.
void WriteCpp(const std::string& cc_filename, const std::string& comment,
              const std::pair<std::string, std::string>& namespaces,
              const std::string& base, const std::vector<std::string>& externs,
              std::vector<Initializer>& initializers,
              const std::vector<std::string>& infiles) {
  std::ofstream toc(cc_filename, std::ios_base::out | std::ios_base::trunc);
  if (!toc.is_open()) {
    FatalError("Unable to open cc file '%s' for writing", cc_filename.c_str());
  }

  toc << comment << "//  Output: " << cc_filename << "\n\n";
  toc << "#include \"";
  if (!FLAGS_include_path.empty() && (base.empty() || base[0] != '/')) {
    toc << FLAGS_include_path;
    if (FLAGS_include_path.back() != '/') toc << '/';
  }
  toc << base << ".h\"\n\n";

  if (FLAGS_data_in_cc) {
    initializers = EmbedFiles(infiles, cc_filename, toc);
  } else {
    for (const auto& ext : externs) toc << ext << "\n";
  }
  toc << "\n";

  if (FLAGS_sort_toc) {
    std::sort(initializers.begin(), initializers.end(),
              [](const Initializer& a, const Initializer& b) {
                return a.filename < b.filename;
              });
  }

  // Ensure that the prefix ends, but does not start, with a slash.
  // This allows both absolute repo paths like "//some/path" and relative ones
  // like "subdir/" to work.
  std::vector<std::string> prefixes;
  prefixes.reserve(FLAGS_strip.size());
  for (const auto& prefix : FLAGS_strip) {
    prefixes.push_back(TrimFront(TrimBack(prefix, '/'), '/') + "/");
  }

  toc << "static const struct FileToc toc[";
  toc << initializers.size() + 1;  // one more for sentinel
  toc << "] = {\n";
  for (const auto& initializer : initializers) {
    std::string filename = initializer.filename;
    if (FLAGS_flatten) {
      filename = Basename(filename);
    } else {
      for (const auto& prefix : prefixes) {
        if (prefix.size() <= filename.size()) {
          if (filename.compare(0, prefix.size(), prefix) == 0) {
            filename = filename.substr(prefix.size());
          }
        }
      }
    }
    toc << "  { ";
    toc << "\"" << filename << "\", ";
    toc << (initializer.size ? initializer.sym : "\"\"") << ", ";
    toc << initializer.size << ", {";
    const std::ios_base::fmtflags ff = toc.flags(std::ios_base::hex);
    const char fill = toc.fill('0');
    for (std::size_t i = 0; i < sizeof initializer.md5digest; ++i) {
      if (i != 0) toc << ",";
      toc << " 0x" << std::setw(2)
          << static_cast<int>(initializer.md5digest[i]);
    }
    toc.fill(fill);
    toc.flags(ff);
    toc << " } },\n";
  }
  toc << "  { (const char*) 0, (const char*) 0, 0, {} }\n";
  toc << "};\n\n";

  if (!FLAGS_toc_section_name.empty()) {
    // The named section is only supported on ELF platforms, with GCC or Clang
    // and if not explicitly disabled via the DISABLE_FILEWRAPPER_TOC_SECTION.
    // The latter is need for some Waymo embedded architectures.
    toc << "#if defined(__ELF__) && defined(__GNUC__) && "
        << " !defined(DISABLE_FILEWRAPPER_TOC_SECTION)\n"
        << "  #define ATTRIBUTE_SECTION(name) "
        << "    __attribute__((section(#name), used))\n"
        << "#endif\n"
        << "#ifndef ATTRIBUTE_SECTION\n"
        << "  #define ATTRIBUTE_SECTION(name) /**/\n"
        << "#endif\n\n";
  }
  toc << "static const struct FileToc* toc_ptr ";
  if (!FLAGS_toc_section_name.empty()) {
    toc << "ATTRIBUTE_SECTION(" << FLAGS_toc_section_name << ") ";
  }
  toc << "= toc;\n\n";

  toc << namespaces.first;
  toc << "const struct FileToc* " << base << "_create() {\n";
  toc << "  return toc_ptr;\n";
  toc << "}\n";
  toc << "\n";
  toc << "size_t " << base << "_size() {\n";
  toc << "  return " << initializers.size() << ";\n";
  toc << "}\n";
  toc << namespaces.second;

  const std::streampos end_pos = toc.tellp();
  toc.close();
  if (toc.fail()) {
    FatalError("Error during cc creation");
  }

  if (FLAGS_data_in_cc && FLAGS_eliminate_duplication) {
    // If we did a backwards seek in EmbedFiles() we may have written
    // data past end_pos, so we truncate() now just in case we did.
    if (truncate(cc_filename.c_str(), end_pos) != 0) {
      FatalError("Unable to truncate cc file '%s'", cc_filename.c_str());
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  runfiles = RunFiles(argc, argv);
  progname = ProgramInvocationShortName(argc, argv);
  if (int rc = ParseCommandLineFlags(&argc, &argv)) return rc;

  // The base name for the files, functions and data structures.
  if (argc < 1) return Usage();
  const std::string base = (argc--, *argv++);

  // The files to encapsulate.
  std::vector<std::string> infiles;
  while (argc > 0) infiles.push_back((argc--, *argv++));
  if (infiles.empty() && FLAGS_create_impl) return Usage();

  // Compute the final destinations for the files.
  std::string hdr_name = base + ".h";
  if (!FLAGS_out_h.empty()) hdr_name = FLAGS_out_h;
  std::string obj_name = base + "_data.o";
  if (!FLAGS_out_o.empty()) obj_name = FLAGS_out_o;
  std::string src_name = base + ".cc";
  if (!FLAGS_out_cc.empty()) src_name = FLAGS_out_cc;

  // Validate arguments
  if (!FLAGS_namespace.empty() && FLAGS_c_linkage) {
    FatalError(
        "Defining a namespace and requesting c_linkage are incompatible.");
  }

  std::vector<Initializer> initializers;  // info for encapsulated files
  std::vector<std::string> externs;       // extern declarations

  if (FLAGS_create_impl) {
    infiles = ExpandDirs(infiles);
    if (!FLAGS_data_in_cc) {
      EncapsulateFiles(infiles, obj_name, initializers, externs);
    }
  }

  const std::string comment = Comment(base);
  const std::pair<std::string, std::string> namespaces = GetNamespaces();

  if (FLAGS_create_header) {
    WriteHeader(hdr_name, comment, namespaces, base);
  }
  if (FLAGS_create_impl) {
    WriteCpp(src_name, comment, namespaces, base, externs, initializers,
             infiles);
  }

  return 0;
}
