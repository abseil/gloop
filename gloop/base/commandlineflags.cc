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

// This file contains the commandlineflags implementation.

#include "gloop/base/commandlineflags.h"

#include <assert.h>
#include <stdarg.h>  // For va_list and related operations
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/dynamic_annotations.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/no_destructor.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/commandlineflag.h"
#include "absl/flags/config.h"
#include "absl/flags/flag.h"
#include "absl/flags/internal/parse.h"
#include "absl/flags/internal/path_util.h"
#include "absl/flags/internal/private_handle_accessor.h"
#include "absl/flags/internal/program_name.h"
#include "absl/flags/internal/registry.h"
#include "absl/flags/internal/usage.h"
#include "absl/flags/marshalling.h"
#include "absl/flags/parse.h"
#include "absl/flags/reflection.h"
#include "absl/flags/usage.h"
#include "absl/flags/usage_config.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/source_location.h"
#include "absl/types/span.h"
#include "gloop/base/config.h"
#include "gloop/base/logging_extensions.h"

#if GOOGLE_HAVE_FNMATCH
#include <fnmatch.h>
#endif

#ifndef PATH_SEPARATOR
#define PATH_SEPARATOR '/'
#endif

namespace {
namespace absl_flags = absl::flags_internal;
}  // namespace

namespace {

// There are also 'reporting' flags, in commandlineflags_reporting.cc.

static const char kError[] = "ERROR: ";

// If true, ignore flags on the command line that have not been defined.
// Otherwise, an undefined flag results in an error.
static bool ignore_undefined_flags = false;

// Whether error messages should be written to the task status file.
enum class ErrorLevel { kSevere, kMinor };

// Same but without truncation.
void ReportError(ErrorLevel error_level, const std::string& error) {
  fwrite(error.data(), 1, error.size(), stderr);
  fflush(stderr);  // should be unnecessary, but cygwin's rxvt buffers stderr
}

// Report error. Truncates to 255 chars.
void ReportErrorF(ErrorLevel error_level, const char* format, ...) {
  char error_message[255];
  va_list ap;
  va_start(ap, format);
  vsnprintf(error_message, sizeof(error_message), format, ap);
  va_end(ap);
  ReportError(error_level, error_message);
}

static std::string CleanFileName(absl::string_view fname) {
#ifdef _WIN32
  std::string normalized(fname);
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  fname = normalized;
#endif

  // Skip any leading slashes
  auto pos = fname.find_first_not_of(PATH_SEPARATOR);
  if (pos == absl::string_view::npos) return "";

  fname.remove_prefix(pos);
  return std::string(fname);
}

bool ContainsHelpshortFlags(absl::string_view filename) {
  // We only want flags in binary's main. We expect the main
  // routine to reside in <program>.cc or <program>-main.cc or
  // <program>_main.cc, where the <program> is the name of the binary.
  auto suffix = absl_flags::Basename(filename);
  if (!absl::ConsumePrefix(&suffix, absl_flags::ShortProgramInvocationName())) {
    return false;
  }
  return absl::StartsWith(suffix, ".") || absl::StartsWith(suffix, "-main.") ||
         absl::StartsWith(suffix, "_main.");
}

class ContainsHelppackageFlags {
  using StringSet = std::unordered_set<std::string>;

 public:
  bool operator()(absl::string_view filename) const {
    static const StringSet* const kCandidatePackageDirs =
        GetCandidatePackageDirs();

    absl::string_view dir_name = absl_flags::Package(filename);
    for (const auto& d : *kCandidatePackageDirs) {
      if (absl::StartsWith(dir_name, d)) return true;
    }

    return false;
  }

 private:
  static const StringSet* GetCandidatePackageDirs() {
    auto candidate_dirs = std::make_unique<StringSet>();

    absl_flags::ForEachFlag([&](const absl::CommandLineFlag& flag) {
      if (flag.IsRetired()) return;

      std::string filename = flag.Filename();

      if (!ContainsHelpshortFlags(filename)) return;

      candidate_dirs->emplace(absl_flags::Package(filename));
    });

    return candidate_dirs.release();
  }
};

absl::NoDestructor<absl::FlagsUsageConfig> g_user_config;
}  // namespace

namespace base {
void SetAbslFlagsUsageConfig(absl::FlagsUsageConfig config) {
  *g_user_config = std::move(config);
}
}  // namespace base

namespace {
// Install Abseil Flags' library usage callbacks. This needs to be done before
// any operation that may call one of the callbacks.
void InstallFlagsUsageConfig() {
  absl::FlagsUsageConfig config = *g_user_config;
  if (config.normalize_filename == nullptr) {
    config.normalize_filename = &CleanFileName;
  }
  if (config.contains_helpshort_flags == nullptr) {
    config.contains_helpshort_flags = ContainsHelpshortFlags;
  }
  if (config.contains_help_flags == nullptr) {
    config.contains_help_flags = ContainsHelppackageFlags{};
  }
  if (config.contains_helppackage_flags == nullptr) {
    config.contains_helppackage_flags = ContainsHelppackageFlags{};
  }
  absl::SetFlagsUsageConfig(config);
}

static absl::CommandLineFlag* SplitArgument(const char* arg, std::string* key,
                                            const char** v,
                                            std::string* error_message) {
  // Find the flag object for this option
  const char* value = strchr(arg, '=');
  if (value == nullptr) {
    key->assign(arg);
    *v = nullptr;
  } else {
    // Strip out the "=value" portion from arg
    key->assign(arg, value - arg);
    *v = ++value;  // advance past the '='
  }
  absl::string_view flag_name = *key;

  absl::CommandLineFlag* flag = absl::FindCommandLineFlag(flag_name);

  if (flag == nullptr) {
    // If we can't find the flag-name, then we should return an error.
    // The one exception is if 1) the flag-name is 'nox', 2) there
    // exists a flag named 'x', and 3) 'x' is a boolean flag.
    // In that case, we want to return flag 'x'.
    if (!(flag_name[0] == 'n' && flag_name[1] == 'o')) {
      // flag-name is not 'nox', so we're not in the exception case.
      *error_message =
          absl::StrFormat("%sUnknown command line flag '%s'\n", kError, *key);
      return nullptr;
    }
    flag = absl::FindCommandLineFlag(flag_name.substr(2));
    if (flag == nullptr) {
      // No flag named 'x' exists, so we're not in the exception case.
      *error_message =
          absl::StrFormat("%sUnknown command line flag '%s'\n", kError, *key);
      return nullptr;
    }
    if (!flag->IsOfType<bool>()) {
      absl::string_view type_name;
      absl::string_view typename_sep = type_name.empty() ? "" : " ";

      // 'x' exists but is not boolean, so we're not in the exception case.
      *error_message =
          absl::StrCat("ERROR: boolean value (", *key, ") specified for",
                       typename_sep, type_name, " command line flag\n");
      return nullptr;
    }
    // We're in the exception case!
    // Make up a fake value to replace the "no" we stripped out
    key->assign(std::string(flag_name.substr(2)));  // the name without the "no"
    *v = "0";
  }

  // Assign a value if this is a boolean flag
  if (*v == nullptr && flag->IsOfType<bool>()) {
    *v = "1";  // the --nox case was already handled, so this is the --x case
  }

  return flag;
}

// Source of a specified flag value.
using ValueSource = absl_flags::ValueSource;

// --------------------------------------------------------------------
// CommandLineFlagParser
//    Parsing is done in two stages.  In the first, we go through
//    argv.  For every flag-like arg we can make sense of, we parse
//    it and set the appropriate FLAGS_* variable.  For every flag-
//    like arg we can't make sense of, we store it in a vector,
//    along with an explanation of the trouble.  In stage 2, we
//    handle the 'reporting' flags like --help and --mpm_version.
//    (This is via a call to HandleCommandLineHelpFlags(), in
//    commandlineflags_reporting.cc.)
//    An optional stage 3 prints out the error messages.
//       This is a bit of a simplification.  For instance, --flagfile
//    is handled as soon as it's seen in stage 1, not in stage 2.
// --------------------------------------------------------------------

class CommandLineFlagParser {
 public:
  // The argument is the flag-registry to register the parsed flags in
  explicit CommandLineFlagParser(ValueSource source) : source_(source) {}
  ~CommandLineFlagParser() = default;

  // Stage 2: print reporting info and exit, if requested.
  // In commandlineflags_reporting.cc:HandleCommandLineHelpFlags().

  // Stage 3: report any errors and return true if any were found.
  bool ReportErrors();

  // Set a particular command line option.  "newval" is a std::string
  // describing the new value that the option has been set to.  If
  // option_name does not specify a valid option name, or value is not
  // a valid value for option_name, newval is empty.  Does recursive
  // processing for --flagfile and --fromenv.  Returns the new value
  // if everything went ok, or empty-string if not.  (Actually, the
  // return-string could hold many flag/value pairs due to --flagfile.)
  // If "errors" is not null, then it will be set to an appropriate error
  // message that explains why setting the flag value failed.
  std::string ProcessSingleOption(absl::CommandLineFlag* flag,
                                  absl::string_view value,
                                  FlagSettingMode set_mode,
                                  std::string* errors);

  // Set a whole batch of command line options as specified by
  // contentdata, which is in flagfile format (and probably has been
  // read from a flagfile).  string_source is only cosmetic, and is
  // used to provide intelligible errors when the flag doesn't exist
  // (it should be set to "string" or a filename in the case of
  // flagfiles).
  //
  // Returns the new value if everything went ok, or empty-string if
  // not.  (Actually, the return-string could hold many flag/value
  // pairs due to --flagfile.)
  std::string ProcessOptionsFromString(const std::string& contentdata,
                                       FlagSettingMode set_mode,
                                       const char* string_source);

  // These are the 'recursive' flags, defined at the top of this file.
  // Whenever we see these flags on the commandline, we must take action.
  // These are called by ProcessSingleOptionLocked and, similarly, return
  // new values if everything went ok, or the empty-string if not.
  std::string ProcessFlagfile(absl::Span<const std::string> filename_list,
                              FlagSettingMode set_mode);
  // diff fromenv/tryfromenv
  std::string ProcessFromenv(absl::Span<const std::string> flaglist,
                             FlagSettingMode set_mode, bool errors_are_fatal);

 private:
  const ValueSource source_;
  std::map<std::string, std::string>
      error_flags_;  // map from name to error message
  // This could be a std::set<std::string>, but we reuse the map to minimize the
  // .o size

  // --[flag] name was not registered
  std::map<std::string, std::string> undefined_names_;

  int flagfile_depth_ = 0;
};

// Snarf an entire file into a C++ std::string.  This is just so that we
// can do all the I/O in one place and not worry about it everywhere.
// Plus, it's convenient to have the whole file contents at hand.
// Adds a newline at the end of the file.
#define PFATAL(s) \
  do {            \
    perror(s);    \
    exit(1);      \
  } while (0)

static std::string ReadFileIntoString(const char* filename) {
  const int kBufSize = 8092;
  char buffer[kBufSize];
  std::string s;
  FILE* fp = fopen(filename, "r");
  if (!fp) PFATAL(filename);
  size_t n;
  while ((n = fread(buffer, 1, kBufSize, fp)) > 0) {
    if (ferror(fp)) PFATAL(filename);
    s.append(buffer, n);
  }
  if (ferror(fp)) PFATAL(filename);
  fclose(fp);
  return s;
}

std::string CommandLineFlagParser::ProcessFlagfile(
    absl::Span<const std::string> filename_list, FlagSettingMode set_mode) {
  if (filename_list.empty()) return "";

  if (flagfile_depth_ > 100) {
    error_flags_["flagfile"] = "ERROR: Unbounded --flagfile recursion\n";
    return "";
  }
  ++flagfile_depth_;

  std::string msg;
  for (const std::string& filename : filename_list) {
    const char* file = filename.c_str();
    msg += ProcessOptionsFromString(ReadFileIntoString(file), set_mode, file);
  }

  --flagfile_depth_;
  return msg;
}

std::string CommandLineFlagParser::ProcessFromenv(
    absl::Span<const std::string> flaglist, FlagSettingMode set_mode,
    bool errors_are_fatal) {
  if (flaglist.empty()) return "";

  std::string msg;

  for (const std::string& flag_str : flaglist) {
    absl::CommandLineFlag* flag = absl::FindCommandLineFlag(flag_str);
    if (flag == nullptr) {
      error_flags_[flag_str] = absl::StrFormat(
          "%sUnknown command line flag '%s' "
          "(via --fromenv or --tryfromenv)\n",
          kError, flag_str);
      undefined_names_[flag_str] = "";
      continue;
    }

    const std::string envname = std::string("FLAGS_") + flag_str;
    const char* envval = getenv(envname.c_str());
    if (!envval) {
      if (errors_are_fatal) {
        error_flags_[flag_str] =
            (std::string(kError) + envname + " not found in environment\n");
      }
      continue;
    }

    // Avoid infinite recursion.
    if ((strcmp(envval, "fromenv") == 0) ||
        (strcmp(envval, "tryfromenv") == 0)) {
      error_flags_[flag_str] = absl::StrFormat(
          "%sinfinite recursion on environment flag '%s'\n", kError, envval);
      continue;
    }

    // We do not attempt to set retired flag. It will fail, but we do not care.
    if (flag->IsRetired()) continue;

    msg += ProcessSingleOption(flag, envval, set_mode, nullptr /* errors */);
  }
  return msg;
}

std::string CommandLineFlagParser::ProcessSingleOption(
    absl::CommandLineFlag* flag, absl::string_view value,
    FlagSettingMode set_mode, std::string* errors) {
  std::string msg;
  if (!absl_flags::PrivateHandleAccessor::ParseFrom(
          *flag, value, static_cast<absl_flags::FlagSettingMode>(set_mode),
          static_cast<absl_flags::ValueSource>(source_), msg)) {
    if (flag->IsRetired()) {
      // Setting retired flags fail. We do not care, but we want to report a
      // access warning.
      return "";
    }

    error_flags_[std::string(flag->Name())] = "ERROR: " + msg;
    if (errors != nullptr) {
      *errors = msg;
    }
    return "";
  } else {
    msg = absl::StrCat(flag->Name(), " set to ", flag->CurrentValue(), "\n");
  }

  // The recursive flags, --flagfile and --fromenv and --tryfromenv,
  // must be dealt with as soon as they're seen.  They will emit
  // messages of their own.
  if (flag->Name() == "flagfile") {
    msg += ProcessFlagfile(absl::GetFlag(FLAGS_flagfile), set_mode);

  } else if (flag->Name() == "fromenv") {
    // last arg indicates envval-not-found is fatal (unlike in --tryfromenv)
    msg += ProcessFromenv(absl::GetFlag(FLAGS_fromenv), set_mode, true);

  } else if (flag->Name() == "tryfromenv") {
    msg += ProcessFromenv(absl::GetFlag(FLAGS_tryfromenv), set_mode, false);
  }

  return msg;
}

bool CommandLineFlagParser::ReportErrors() {
  // error_flags_ indicates errors we saw while parsing.
  // But we ignore undefined-names if ok'ed by --undefok
  for (const std::string& flagname : absl::GetFlag(FLAGS_undefok)) {
    // We also deal with --no<flag>, in case the flagname was boolean
    const std::string no_version = std::string("no") + flagname;
    if (undefined_names_.find(flagname) != undefined_names_.end()) {
      error_flags_[flagname] = "";  // clear the error message
    }
    if (undefined_names_.find(no_version) != undefined_names_.end()) {
      error_flags_[no_version] = "";
    }
  }
  if (ignore_undefined_flags) {
    // Ignore all undefined flags.
    for (std::map<std::string, std::string>::const_iterator it =
             undefined_names_.begin();
         it != undefined_names_.end(); ++it)
      error_flags_[it->first] = "";  // clear the error message
  }

  bool found_error = false;
  std::string error_message;
  for (std::map<std::string, std::string>::const_iterator it =
           error_flags_.begin();
       it != error_flags_.end(); ++it) {
    if (!it->second.empty()) {
      error_message.append(it->second.data(), it->second.size());
      found_error = true;
    }
  }
  if (found_error) {
#if !GOOGLE_COMMANDLINEFLAGS_FULL_API
    error_message += "NOTE: command line flags are disabled in this build\n";
#endif
    ReportError(ErrorLevel::kSevere, error_message);
  }
  return found_error;
}

// Returns true if 'str' matches the pattern (equivalent to
// fnmatch(pattern, str, FNM_PATHNAME) == 0).
// TODO: Implement for Windows (see cr/121078190 for an example).
static bool MatchPath(const char* pattern, const char* str) {
#if GOOGLE_HAVE_FNMATCH
  const int fnmatch_return = fnmatch(pattern, str, FNM_PATHNAME);
  LOG(INFO) << "fnmatch(" << pattern << ", " << str
            << ", FNM_PATHNAME) returned " << fnmatch_return;
  return fnmatch_return == 0;
#else
  LOG(INFO) << "MatchPath called when not supported";
  return false;
#endif
}

std::string CommandLineFlagParser::ProcessOptionsFromString(
    const std::string& contentdata, FlagSettingMode set_mode,
    const char* string_source) {
  std::string retval;
  const char* flagfile_contents = contentdata.c_str();
  bool flags_are_relevant = true;  // set to false when filenames don't match
  bool in_filename_section = false;

  // We read this file a line at a time.
  for (absl::string_view whole_line : absl::StrSplit(flagfile_contents, '\n')) {
    // Skip leading spaces.
    const std::string line(absl::StripLeadingAsciiWhitespace(whole_line));

    // Each line can be one of four things:
    // 1) A comment line -- we skip it
    // 2) An empty line -- we skip it
    // 3) A list of filenames -- starts a new filenames+flags section
    // 4) A --flag=value line -- apply if previous filenames match
    if (line.empty() || line[0] == '#') {
      // comment or empty line; just ignore

    } else if (line[0] == '-') {    // flag
      in_filename_section = false;  // instead, it was a flag-line
      if (!flags_are_relevant)      // skip this flag; applies to someone else
        continue;

      const char* name_and_val = line.c_str() + 1;  // skip the leading -
      if (*name_and_val == '-') name_and_val++;     // skip second - too
      std::string key;
      const char* value;
      std::string error_message;
      absl::CommandLineFlag* flag =
          SplitArgument(name_and_val, &key, &value, &error_message);

      if (flag == nullptr) {
        // 2014-11-21: It appears that for unknown historical reasons,
        // errors parsing flagfile lines don't cause a failure in this
        // implementation (errors are generated in Java and Python).
        // It appears difficult to change this behavior safely, as it's
        // been in place for 8+ years and is impossible to identify who
        // depends upon this during program startup.
        //
        // As a minor mitigation, we are logging when we encounter an
        // unknown flag, so at least there is some evidence of what
        // happened.
        ReportErrorF(ErrorLevel::kMinor,
                     "ERROR: flag '%s' specified in %s "
                     "does not exist\n",
                     key.c_str(), string_source);
      } else if (value == nullptr) {
        // "WARNING: flagname '" + key + "' missing a value\n"

      } else {
        retval +=
            ProcessSingleOption(flag, value, set_mode, nullptr /* errors */);
        if (flag->IsRetired()) {
          // Setting retired flags fail. We do not care, but we want to report a
          // access warning.
          continue;
        }
      }

    } else {                       // a filename!
      if (!in_filename_section) {  // start over: assume filenames don't match
        in_filename_section = true;
        flags_are_relevant = false;
      }

      // Split the line up at spaces into glob-patterns
      const char* space = line.c_str();  // just has to be non-nullptr
      for (const char* word = line.c_str(); *space; word = space + 1) {
        if (flags_are_relevant)  // we can stop as soon as we match
          break;
        space = strchr(word, ' ');
        if (space == nullptr) space = word + strlen(word);
        const std::string glob(word, space - word);
        // We try matching both against the full argv0 and basename(argv0)
        if (glob == ProgramInvocationName()  // small optimization
            || glob == ProgramInvocationShortName() ||
            MatchPath(glob.c_str(), ProgramInvocationName()) ||
            MatchPath(glob.c_str(), ProgramInvocationShortName())) {
          flags_are_relevant = true;
        }
      }
    }
  }
  return retval;
}

#if GOOGLE_COMMANDLINEFLAGS_FULL_API
template <typename T>
static T GetFromEnv(const char* varname, const char* type, T dflt) {
  T result = dflt;
  const char* const valstr = getenv(varname);
  if (valstr) {
    std::string err;
    if (!absl::ParseFlag(valstr, &result, &err)) {
      const char* sep = err.empty() ? "" : "; ";
      ReportErrorF(
          ErrorLevel::kSevere,
          "ERROR: error parsing env variable '%s' with value '%s'%s%s\n",
          varname, valstr, sep, err.c_str());
      std::exit(1);
    }
  }
  return result;
}
#endif

}  // namespace

// Now define the functions that are exported via the .h file

// --------------------------------------------------------------------
// SetArgv()
// GetArgvs()
// GetArgv()
// GetArgv0()
// ProgramInvocationName()
// ProgramInvocationShortName()
//    Functions to set and get argv.  Typically the setter is called
//    by ParseCommandLineFlags.  Also can get the ProgramUsage std::string,
//    set by SetUsageMessage.
// --------------------------------------------------------------------

// These values are not protected by a Mutex because they are normally
// set only once during program startup.
static const char* argv0 = "UNKNOWN";  // just the program name
static const char* cmdline = "";       // the entire command-line
static uint32_t argv_sum = 0;

// Returns a mutable reference to the argv std::string.
static std::vector<std::string>& InternalGetArgvs() {
  static std::vector<std::string>* argvs = new std::vector<std::string>();
  return *argvs;
}

namespace base {
void SetArgv(int argc, const char** argv) {
  static bool called_set_argv = false;
  if (called_set_argv)  // we already have an argv for you
    return;

  called_set_argv = true;
  ResetArgv(argc, argv);
}

void ResetArgv(int argc, const char** argv) {
  static bool called_reset_argv = false;
  assert(argc > 0);  // every program has at least a progname

  absl_flags::SetProgramInvocationName(argv[0]);

  if (called_reset_argv) {
    free(const_cast<char*>(argv0));
  }
  argv0 = strdup(argv[0]);
  assert(argv0);

  InternalGetArgvs().clear();
  std::string cmdline_string;  // easier than doing strcats
  for (int i = 0; i < argc; i++) {
    if (i != 0) {
      cmdline_string += ' ';
    }
    cmdline_string += argv[i];
    InternalGetArgvs().push_back(argv[i]);
  }

  if (called_reset_argv) {
    free(const_cast<char*>(cmdline));
  }
  cmdline = strdup(cmdline_string.c_str());
  assert(cmdline);
  called_reset_argv = true;

  // Compute a simple sum of all the chars in argv
  argv_sum = 0;
  for (const char* c = cmdline; *c; c++) argv_sum += *c;
}

const std::vector<std::string>& GetArgvs() { return InternalGetArgvs(); }
std::string GetArgv() { return cmdline; }
const char* GetArgv0() { return argv0; }
uint32_t GetArgvSum() { return argv_sum; }
const char* ProgramInvocationName() {  // like the GNU libc fn
  return GetArgv0();
}
const char* ProgramInvocationShortName() {  // like the GNU libc fn
  const char* slash = strrchr(argv0, '/');
#ifdef _WIN32
  if (!slash) slash = strrchr(argv0, '\\');
#endif
  return slash ? slash + 1 : argv0;
}
}  // namespace base

// --------------------------------------------------------------------
// GetCommandLineOption()
// SetCommandLineOption()
// SetCommandLineOptionWithMode()
//    The programmatic way to get/set a flag's value, using a absl::string_view
//    for its name rather than the variable itself (that is,
//    SetCommandLineOption("foo", x) rather than FLAGS_foo = x).
//    There's also a bit more flexibility here due to the various
//    set-modes, but typically these are used when you only have
//    that flag's name as a std::string, perhaps at runtime.
//    All of these work on the default, global registry.
// --------------------------------------------------------------------

bool GetCommandLineOption(absl::string_view name, std::string* value) {
  if (name.empty()) return false;
  assert(value);

  absl::CommandLineFlag* flag = absl::FindCommandLineFlag(name);
  if (flag == nullptr || flag->IsRetired()) {
    return false;
  }

  *value = flag->CurrentValue();
  return true;
}

std::string SetCommandLineOptionWithMode(absl::string_view name,
                                         absl::string_view value,
                                         FlagSettingMode set_mode) {
  absl::CommandLineFlag* flag = absl::FindCommandLineFlag(name);

  if (!flag || flag->IsRetired()) return "";

  std::string error;
  if (!absl_flags::PrivateHandleAccessor::ParseFrom(
          *flag, value, static_cast<absl_flags::FlagSettingMode>(set_mode),
          absl_flags::kProgrammaticChange, error)) {
    // Errors here are all of the form: the provided name was a recognized
    // flag, but the value was invalid (bad type, or validation failed).
    absl_flags::ReportUsageError(error, false);
    return "";
  }

  return absl::StrCat(flag->Name(), " set to ", flag->CurrentValue(), "\n");
}

std::string SetCommandLineOption(absl::string_view name,
                                 absl::string_view value) {
  return SetCommandLineOptionWithMode(name, value, SET_FLAGS_VALUE);
}

namespace base {

bool WasPresentOnCommandLine(absl::string_view name) {
  return absl_flags::WasPresentOnCommandLine(name);
}

}  // namespace base

bool ReadFlagsFromString(const std::string& flagfilecontents,
                         const char* /*prog_name*/,  // TODO: nix this
                         bool errors_are_fatal) {
  CommandLineFlagParser parser(absl_flags::kProgrammaticChange);
  {
    absl::FlagSaver saved_states;

    parser.ProcessOptionsFromString(flagfilecontents, SET_FLAGS_VALUE,
                                    "user-provided string");
    // Should we handle --help and such when reading flags from a string?  Sure.
    const absl_flags::HelpMode help_mode =
        absl_flags::HandleUsageFlags(std::cout, absl::ProgramUsageMessage());

    absl_flags::MaybeExit(help_mode);

    if (parser.ReportErrors()) {
      // Error.  Restore all global flags to their previous values.
      if (errors_are_fatal) {
        std::exit(1);
      }
      return false;
    }
  }
  parser.ProcessOptionsFromString(flagfilecontents, SET_FLAGS_VALUE,
                                  "user-provided string");
  return true;
}

// --------------------------------------------------------------------
// BoolFromEnv()
// Int32FromEnv()
// Int64FromEnv()
// Uint64FromEnv()
// DoubleFromEnv()
// StringFromEnv()
//    Reads the value from the environment and returns it.
//    Example usage:
//       DEFINE_bool(myflag, BoolFromEnv("MYFLAG_DEFAULT", false), "whatever");
// --------------------------------------------------------------------
#if GOOGLE_COMMANDLINEFLAGS_FULL_API
bool BoolFromEnv(const char* v, bool defval) {
  return GetFromEnv(v, "bool", defval);
}
int32_t Int32FromEnv(const char* v, int32_t defval) {
  return GetFromEnv(v, "int32_t", defval);
}
int64_t Int64FromEnv(const char* v, int64_t defval) {
  return GetFromEnv(v, "int64_t", defval);
}
uint64_t Uint64FromEnv(const char* v, uint64_t defval) {
  return GetFromEnv(v, "uint64_t", defval);
}
double DoubleFromEnv(const char* v, double defval) {
  return GetFromEnv(v, "double", defval);
}
const char* StringFromEnv(const char* varname, const char* defval) {
  const char* const val = getenv(varname);
  return val ? val : defval;
}
#endif

// --------------------------------------------------------------------
// ParseCommandLineFlags()
// ParseCommandLineNonHelpFlags()
// HandleCommandLineHelpFlags()
//    This is the main function called from main(), to actually
//    parse the commandline.  It modifies argc and argv as described
//    at the top of commandlineflags.h.  You can also divide this
//    function into two parts, if you want to do work between
//    the parsing of the flags and the printing of any help output.
//    The return value is an index of first positional argument in an output
//    argument vector.
// --------------------------------------------------------------------

namespace base {

void ParseCommandLine(int* argc, char*** argv) {
  InstallFlagsUsageConfig();

  char** argv_val = *argv;

  // Save original argument vector, to be available during program lifetime.
  SetArgv(*argc, const_cast<const char**>(argv_val));

  const std::vector<char*> positional_args =
      absl::ParseCommandLine(*argc, argv_val);

  // The positional_args contains our desired output. Copy the values into argv.
  // The C-standard requires argv[argc] to be nullptr, but some code passes
  // an "artificial" argv that isn't null-terminated. To accommodate both
  // cases we keep the end of the argument vector in place and move the
  // beginning (instead of keeping the beginning at 0, moving the end and
  // setting the argv[argc] to nullptr). This way we do not need to set
  // argv[argc] to nullptr. If it was there in original argument vector it
  // will continue to exist in our output.
  *argv = argv_val = argv_val + *argc - positional_args.size();
  std::copy(positional_args.begin(), positional_args.end(), argv_val);
  *argc = positional_args.size();
}

void ReportCommandLineHelp(absl::string_view filter,
                           absl::string_view usage_message) {
  if (usage_message.empty()) usage_message = absl::ProgramUsageMessage();
  if (filter.empty()) {
    absl_flags::SetFlagsHelpMode(absl_flags::HelpMode::kPackage);
  } else {
    absl_flags::SetFlagsHelpMode(absl_flags::HelpMode::kMatch);
    absl_flags::SetFlagsHelpMatchSubstr(filter);
  }

  absl_flags::HandleUsageFlags(std::cout, usage_message);
  absl_flags::SetFlagsHelpMode(absl_flags::HelpMode::kNone);
}

void ReportCommandLineShortHelp(absl::string_view usage_message) {
  if (usage_message.empty()) usage_message = absl::ProgramUsageMessage();
  absl_flags::SetFlagsHelpMode(absl_flags::HelpMode::kShort);
  absl_flags::HandleUsageFlags(std::cout, usage_message);
  absl_flags::SetFlagsHelpMode(absl_flags::HelpMode::kNone);
}

void ReportCommandLineFullHelp(absl::string_view usage_message) {
  if (usage_message.empty()) usage_message = absl::ProgramUsageMessage();
  absl_flags::SetFlagsHelpMode(absl_flags::HelpMode::kFull);
  absl_flags::HandleUsageFlags(std::cout, usage_message);
  absl_flags::SetFlagsHelpMode(absl_flags::HelpMode::kNone);
}

void ReportCommandLineHelpMatch(absl::string_view filter,
                                absl::string_view usage_message) {
  if (usage_message.empty()) usage_message = absl::ProgramUsageMessage();
  absl_flags::SetFlagsHelpMode(absl_flags::HelpMode::kMatch);
  absl_flags::SetFlagsHelpMatchSubstr(filter);
  absl_flags::HandleUsageFlags(std::cout, usage_message);
  absl_flags::SetFlagsHelpMode(absl_flags::HelpMode::kNone);
}

}  // namespace base

static uint32_t ParseCommandLineFlagsInternal(int* argc, char*** argv,
                                              bool remove_flags,
                                              bool do_report) {
  using absl_flags::OnUndefinedFlag;
  using absl_flags::UsageFlagsAction;

  InstallFlagsUsageConfig();

  char** argv_val = *argv;

  // Save original argument vector, to be available during program lifetime.
  SetArgv(*argc, const_cast<const char**>(argv_val));

  const std::vector<char*> positional_args = absl_flags::ParseCommandLineImpl(
      *argc, argv_val,
      do_report ? UsageFlagsAction::kHandleUsage
                : UsageFlagsAction::kIgnoreUsage,
      ignore_undefined_flags ? OnUndefinedFlag::kIgnoreUndefined
                             : OnUndefinedFlag::kAbortIfUndefined);

  if (remove_flags) {
    // If Abseil Flags were intended to be removed, the positional_args contain
    // our desired output. Copy the values into argv as is and return 1 as the
    // index of the first positional argument.
    // The C-standard requires argv[argc] to be nullptr, but some code passes
    // an "artificial" argv that isn't null-terminated. To accommodate both
    // cases we keep the end of the argument vector in place and move the
    // beginning (instead of keeping the beginning at 0, moving the end and
    // setting the argv[argc] to nullptr). This way we do not need to set
    // argv[argc] to nullptr. If it was there in the original argument vector it
    // will continue to exist in our output.
    *argv = argv_val = argv_val + *argc - positional_args.size();
    std::copy(positional_args.begin(), positional_args.end(), argv_val);
    *argc = positional_args.size();
    return 1;
  }

  absl::flat_hash_set<void*> position_args_set(positional_args.begin(),
                                               positional_args.end());

  // If we were asked to keep arguments corresponding to Abseil Flags, we should
  // iterate through the original arguments list and keep those that are not
  // present in positional arguments list.
  uint32_t out_pos = 1;
  for (int in_pos = 0; in_pos < *argc; ++in_pos) {
    if (position_args_set.count(argv_val[in_pos]) == 0) {
      argv_val[out_pos++] = argv_val[in_pos];
    }
  }

  // Now we can add all the positional arguments back into original list.
  // The first index we add positional argument to is our result value.
  // We are skipping first element in positional_args, since it is program name.
  std::copy(positional_args.begin() + 1, positional_args.end(),
            argv_val + out_pos);
  *argc = out_pos + positional_args.size() - 1;

  return out_pos;
}

uint32_t ParseCommandLineFlags(int* argc, char*** argv, bool remove_flags) {
  return ParseCommandLineFlagsInternal(argc, argv, remove_flags, true);
}

uint32_t ParseCommandLineNonHelpFlags(int* argc, char*** argv,
                                      bool remove_flags) {
  return ParseCommandLineFlagsInternal(argc, argv, remove_flags, false);
}

void IgnoreUndefinedCommandLineFlags() { ignore_undefined_flags = true; }
