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

// Introduction
// ------------
// Various files linked into a program define flags via the ABSL_FLAG macro.
//
// Example:
//
//    ABSL_FLAG(int64_t, buffer_size, 4096, "Buffer size to use for IO");
//
// ABSL_FLAG expands into a definition of an object that holds an int64_t:
//    absl::Flag<int64_t> FLAGS_buffer_size = ...;
//
// The flag can be read and written as follows:
//    int64_t bufsize = absl::GetFlag(FLAGS_buffer_size);
//    ..
//    absl::SetFlag(&FLAGS_buffer_size, 65536);
//
// main() calls into this module passing it the command-line
// argc/argv.  The contents of argc/argv are used to populate the
// appropriate flags.  E.g. passing the following on the command line
// will cause the preceding flag's value to change from the default to
// one megabyte:
//
//    --buffer_size=1048576
//
// For more details, see <link>
//
// Supported types
// ---------------
// This module contains built-in support for flags of the following types:
//    bool, int32_t, int64_t, uint64_t, double, string, std::vector<string>
//
// Scalar flag types are interpreted using C literal rules, so an integer-typed
// flag will parse 10, 012, and 0xa to be the same value. A std::vector<string>
// flag splits on commas, but treats an empty flag as an empty container (not
// a container with a single, empty string). Escaping is not supported with
// a std::vector<string> flag; if values can contain commas, use a string-type
// flag with explicit parsing, or a user-defined type.
//
// Support for a user-defined type T can be added by ensuring the following:
//
// (a) T must be copy-constructible and must support an assignment operator.
//
// (b) T must come with an associated stand-alone AbslParseFlag function.
//              bool AbslParseFlag(absl::string_view text, T* dst,
//                                 std::string* err);
//     AbslParseFlag converts a string to a value of type T, stores it
//     in *dst and returns true.  On error, returns false and optionally
//     stores an error message in *err.  AbslParseFlag must be idempotent; all
//     calls with the same text must produce the same value in *dst, regardless
//     of the prior value in *dst.
//
// (c) T must come with an associated AbslUnparseFlag function which must have
//     one of the following signatures (based on whether T is typically passed
//     by value or const-reference):
//              std::string AbslUnparseFlag(T v);
//              std::string AbslUnparseFlag(const T& v);
//      AbslUnparseFlag returns a string representation of a value of type T.
//
// (d) AbslParseFlag and AbslUnparseFlag must be in the same namespace as T
//     and must be defined by the owner of that namespace.
//
// (e) An important corollary of (d) is that user code must not define
//     AbslParseFlag or AbslUnparseFlag for any C++ builtin types or any types
//     in the std namespace.
//
// (f) The preceding operations on T (constructors, AbslParseFlag,
//     AbslUnparseFlag, etc.) must not call into the flag library. Exceptions:
//     (1) they are allowed to use GetFlag to read the values of lower-level
//     flags (e.g. flags for builtin types), (2) AbslParseFlag/AbslUnparseFlag
//     are allowed to call absl::ParseFlag and absl::AbslUnparseFlag to process
//     parts of T.
//
// (g) Any value of type T must be convertible to a string by AbslUnparseFlag,
//     and the resulting string must be parsable by AbslParseFlag.
//
// --- A note about thread-safety:
//
// We describe many functions in this routine as being thread-hostile,
// thread-compatible, or thread-safe.  Here are the meanings we use:
//
// thread-safe: it is safe for multiple threads to call this routine
//   (or, when referring to a class, methods of this class)
//   concurrently.
// thread-hostile: it is not safe for multiple threads to call this
//   routine (or methods of this class) concurrently.  In gflags,
//   most thread-hostile routines are intended to be called early in,
//   or even before, main() -- that is, before threads are spawned.
// thread-compatible: it is safe for multiple threads to read from
//   this variable (when applied to variables), or to call const
//   methods of this class (when applied to classes), as long as no
//   other thread is writing to the variable or calling non-const
//   methods of this class.

#ifndef THIRD_PARTY_GLOOP_BASE_COMMANDLINEFLAGS_H_
#define THIRD_PARTY_GLOOP_BASE_COMMANDLINEFLAGS_H_

#include <stdint.h>

#include <new>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/flags/commandlineflag.h"
#include "absl/flags/config.h"                    // IWYU pragma: keep
#include "absl/flags/flag.h"                      // IWYU pragma: keep
#include "absl/flags/internal/commandlineflag.h"  // IWYU pragma: export
#include "absl/flags/internal/usage.h"            // IWYU pragma: export
#include "absl/flags/marshalling.h"               // IWYU pragma: keep
#include "absl/flags/parse.h"                     // IWYU pragma: keep
#include "absl/flags/reflection.h"                // IWYU pragma: keep
#include "absl/flags/usage.h"                     // IWYU pragma: keep
#include "absl/flags/usage_config.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/source_location.h"
#include "gloop/base/commandlineflags_declare.h"  // IWYU pragma: keep

// Determine whether the full commandlineflags API
// should be used based on platform if it has not
// been specified on the command line.
#if !defined(GOOGLE_COMMANDLINEFLAGS_FULL_API)
#if defined(__linux__)
#define GOOGLE_COMMANDLINEFLAGS_FULL_API 1
#endif  // defined(__linux__)
#endif  // !defined(GOOGLE_COMMANDLINEFLAGS_FULL_API)

#ifndef SWIG

// --------------------------------------------------------------------
// Parsing command lines

namespace base {
// Looks for flags in argv and parses them: see <link> for accessors.
// The `argv` and `argc` values will be modified in this process, removing
// the flag (and flag value) entries.
//
// That is, if argv[1] is "--filename" and argv[2] is "foo.txt", those elements
// will be removed from argc/argv, if --filename is a valid flagname and
// "foo.txt" is a valid value for that flag.
//
// If a flag is defined more than once in the command line or flag file, the
// last definition is used.
//
// Prefer this interface to `ParseCommandLineFlags` unless you require `argc`
// and `argv` to be unmodified. `ParseCommandLineFlags` is going to be
// deprecated soon.  See top-of-file for more details on this function.
void ParseCommandLine(int* argc, char*** argv);

// ReportCommandLineHelp() reports help message similar to the effect of --help
// argument. ReportCommandLineHelp(filter) reports help message similar to the
// effect of --help=filter argument.
// Optional 'usage_message` can be used instead of absl::ProgramUsageMessage(),
// which is used by default.
void ReportCommandLineHelp(absl::string_view filter = "",
                           absl::string_view usage_message = "");
// Reports help message similar to the effect of --helpshort argument.
// Optional 'usage_message` can be used instead of absl::ProgramUsageMessage(),
// which is used by default.
void ReportCommandLineShortHelp(absl::string_view usage_message = "");
// Reports help message similar to the effect of --helpfull argument.
// Optional 'usage_message` can be used instead of absl::ProgramUsageMessage(),
// which is used by default.
void ReportCommandLineFullHelp(absl::string_view usage_message = "");
// Reports help message similar to the effect of --helpmatch=filter argument.
// Optional 'usage_message` can be used instead of absl::ProgramUsageMessage(),
// which is used by default.
void ReportCommandLineHelpMatch(absl::string_view filter,
                                absl::string_view usage_message = "");

// Sets overrides for the usage reporting configuration callbacks.
// This is the google3 version of absl::SetFlagsUsageConfig().
void SetAbslFlagsUsageConfig(absl::FlagsUsageConfig usage_config);

}  // namespace base

// Looks for flags in argv and parses them.  Rearranges argv to put
// flags first, or removes them entirely if remove_flags is true.
// If a flag is defined more than once in the command line or flag
// file, the last definition is used.  Returns the index (into argv)
// of the first non-flag argument.
// See top-of-file for more details on this function.
extern uint32_t ParseCommandLineFlags(int* argc, char*** argv,
                                      bool remove_flags);

// Calls to ParseCommandLineNonHelpFlags and then to
// HandleCommandLineHelpFlags can be used instead of a call to
// ParseCommandLineFlags during initialization, in order to allow for
// changing default values for some FLAGS (via
// e.g. SetCommandLineOptionWithMode calls) between the time of
// command line parsing and the time of dumping help information for
// the flags as a result of command line parsing.  If a flag is
// defined more than once in the command line or flag file, the last
// definition is used.  Returns the index (into argv) of the first
// non-flag argument.  (If remove_flags is true, will always return 1.)
extern uint32_t ParseCommandLineNonHelpFlags(int* argc, char*** argv,
                                             bool remove_flags);

// Disables the error normally generated when an undefined flag is found.
// Thread-hostile; meant to be called before any threads are spawned.
extern void IgnoreUndefinedCommandLineFlags();

// --------------------------------------------------------------------
// Reflection
//
// The following functions can be used to inspect/modify the
// command line flags by name.

namespace base {

// If a flag with specified "name" exists and has type T, store
// its current value in *dst and return true.  Else return false
// without touching *dst.  T must obey all of the requirements for
// types passed to ABSL_FLAG.
template <typename T>
inline bool GetByName(absl::string_view name, T* dst);

}  // namespace base

// If a flag named "name" exists, store its current value in *value
// and return true.  Else return false without changing *value.
// Thread-safe.
bool GetCommandLineOption(absl::string_view name, std::string* value);

#endif  // SWIG

// Set the value of the flag named "name" to value.  If successful,
// returns a non-empty string contains a description of the value
// that was set.  If not successful (e.g., the flag was not found or
// the value is not a valid value), returns the empty string.
// Thread-safe.
std::string SetCommandLineOption(absl::string_view name,
                                 absl::string_view value);
#ifndef SWIG

// Options that control SetCommandLineOptionWithMode.
enum FlagSettingMode {
  // update the flag's value unconditionally (can call this multiple times).
  SET_FLAGS_VALUE = absl::flags_internal::SET_FLAGS_VALUE,
  // update the flag's value, but *only if* it has not yet been updated
  // with SET_FLAGS_VALUE, SET_FLAG_IF_DEFAULT, or "FLAGS_xxx = nondef".
  SET_FLAG_IF_DEFAULT = absl::flags_internal::SET_FLAG_IF_DEFAULT,
  // set the flag's default value to this.  If the flag has not been updated
  // yet (via SET_FLAGS_VALUE, SET_FLAG_IF_DEFAULT, or "FLAGS_xxx = nondef")
  // change the flag's current value to the new default value as well.
  SET_FLAGS_DEFAULT = absl::flags_internal::SET_FLAGS_DEFAULT
};
std::string SetCommandLineOptionWithMode(absl::string_view name,
                                         absl::string_view value,
                                         FlagSettingMode set_mode);

#endif  // SWIG

namespace base {

// Return true iff a flag named "name" was specified on the command line
// (either directly, or via one of --flagfile or --fromenv or --tryfromenv).
//
// Any non-command-line modification of the flag does not affect the
// result of this function. So for example, if a flag was passed on
// the command line but then set to a different value programmatically, this
// function will still return true.
bool WasPresentOnCommandLine(absl::string_view name);

}  // namespace base

#ifndef SWIG

// --------------------------------------------------------------------
// Argv Management

namespace base {
// Makes a permanent record of the command line arguments.
// Thread-hostile; meant to be called before any threads are spawned. May only
// be called once.
extern void SetArgv(int argc, const char** argv);

// Don't call this.
// Replaces the permanent record of arguments with a new set.
// Memory allocated during SetArgv() is freed. This deliberately
// skips the check that arguments have already been recorded.
// This is thread hostile and cannot be done safely in general.
// This is needed only by certain legacy programs for compatibility.
// See http://b/25282203. If you believe you need to call this, please
// email the owners first.
// Don't call this.
extern void ResetArgv(int argc, const char** argv);

// The following functions are thread-safe as long as SetArgv() is
// only called before any threads start.
extern const std::vector<std::string>& GetArgvs();
#endif                         // SWIG
extern std::string GetArgv();  // all of argv as a string
#ifndef SWIG
extern const char* GetArgv0();               // only argv0
extern uint32_t GetArgvSum();                // simple checksum of argv
extern const char* ProgramInvocationName();  // argv0, or "UNKNOWN" if not set
extern const char* ProgramInvocationShortName();  // basename(argv0)
}  // namespace base

// TODO: Remove these using statements once all
// exploiters of them have been fixed.
using base::GetArgv;
using base::GetArgv0;
using base::GetArgvs;
using base::GetArgvSum;
using base::ProgramInvocationName;
using base::ProgramInvocationShortName;
using base::ResetArgv;
using base::SetArgv;

// --------------------------------------------------------------------
// Miscellaneous

// Useful routines for initializing flags from the environment.
// In each case, if 'varname' does not exist in the environment
// return defval.  If 'varname' does exist but is not valid
// (e.g., not a number for an int32_t flag), abort with an error.
// Otherwise, return the value.
//
// Thread-hostile; meant to be called before any threads are spawned.  These
// functions read from the process environment, which is process-global, and
// has no mechanisms for serialization, mutual exclusion, or locking.
#if GOOGLE_COMMANDLINEFLAGS_FULL_API
extern bool BoolFromEnv(const char* varname, bool defval);
extern int32_t Int32FromEnv(const char* varname, int32_t defval);
extern int64_t Int64FromEnv(const char* varname, int64_t defval);
extern uint64_t Uint64FromEnv(const char* varname, uint64_t defval);
extern double DoubleFromEnv(const char* varname, double defval);
extern const char* StringFromEnv(const char* varname, const char* defval);
#else
// commandlineflags.cc isn't compiled for NDK builds that don't use
// the base_commandlineflags_full target, so we inline these for
// compatibility instead. Without the full API these simply return the
// default value and don't actually use the environment.
inline bool BoolFromEnv(const char*, bool defval) { return defval; }
inline int32_t Int32FromEnv(const char*, int32_t defval) { return defval; }
inline int64_t Int64FromEnv(const char*, int64_t defval) { return defval; }
inline uint64_t Uint64FromEnv(const char*, uint64_t defval) { return defval; }
inline double DoubleFromEnv(const char*, double defval) { return defval; }
inline const char* StringFromEnv(const char*, const char* defval) {
  return defval;
}
#endif

// Clean up memory allocated by flags.  This is only needed to reduce
// the quantity of "potentially leaked" reports emitted by memory
// debugging tools such as valgrind.  It is not required for normal
// operation, or for the google perftools heap-checker.  It must only
// be called when the process is about to exit, and all threads that
// might access flags are quiescent.  Referencing flags after this is
// called will have unexpected consequences.  This is not safe to run
// when multiple threads might be running: the function is
// thread-hostile.
// TODO : remove this function
inline void ShutDownCommandLineFlags() {}

// --------------------------------------------------------------------
// Deprecated functions

// Usually where this is used, a FlagSaver should be used instead.
extern bool ReadFlagsFromString(const std::string& flagfilecontents,
                                const char* prog_name,
                                bool errors_are_fatal);  // uses SET_FLAGS_VALUE

namespace base {

// If a flag with specified "name" exists and has type T, store
// its current value in *dst and return true.  Else return false
// without touching *dst.  T must obey all of the requirements for
// types passed to ABSL_FLAG.
template <typename T>
inline bool GetByName(absl::string_view name, T* dst) {
  absl::CommandLineFlag* flag = absl::FindCommandLineFlag(name);
  if (!flag) return false;

  if (auto val = flag->TryGet<T>()) {
    *dst = *val;
    return true;
  }

  return false;
}

}  // namespace base

#endif  // SWIG

#endif  // THIRD_PARTY_GLOOP_BASE_COMMANDLINEFLAGS_H_
