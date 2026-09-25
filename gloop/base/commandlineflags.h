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
// Direct-Access flags
// -------------------
// WARNING: Please use ABSL_FLAG above instead for new code.
//
// Macros of the form DEFINE_<type> (where <type> is any of int32_t,
// int64_t, uint64_t, bool. double, string) can also be used to define
// flags.  Such flags turn into plain global variables of type <type>
// that can be read and written directly.  Note: such flags are not
// thread-safe and therefore we recommend that new code use
// ABSL_FLAG instead.
//
// Example (alternative to the ABSL_FLAG example above):
//
//    DEFINE_int64(buffer_size, 4096, "Buffer size to use for IO");
//
// The flag can be read and written as follows:
//    int64_t bufsize = FLAGS_buffer_size;
//    ..
//    FLAGS_buffer_size =  65536;
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

//
// NOTE: all functions below MUST have an explicit 'extern' before
// them.  Our automated opensourcing tools use this as a signal to do
// appropriate munging for windows, which needs to add GFLAGS_DLL_DECL.
//
#define GFLAGS_DLL_DECL        /* rewritten to be non-empty in windows dir */
#define GFLAGS_DLL_DEFINE_FLAG /* rewritten to be non-empty in windows dir */

// --------------------------------------------------------------------
// Defining flags:

// WARNING: Please use ABSL_FLAG (<link>) instead for new code.
//
// Defining direct access flags:
//
// #define DEFINE_bool(name, default_value, help) ...
// #define DEFINE_int32(name, default_value, help) ...
// #define DEFINE_int64(name, default_value, help) ...
// #define DEFINE_uint64(name, default_value, help) ...
// #define DEFINE_double(name, default_value, help) ...
// #define DEFINE_string(name, default_value, help) ...
//
// The preceding macros expand to the definition of a direct-access flag
// of the specified type.

// --------------------------------------------------------------------
// Validation

// Register a validator function for the specified direct access flag.
// Returns true if successfully registered, false if not (because the
// first argument doesn't point to a command-line flag, or because a
// validator is already registered for this flag).
//
// The validator function is called when the flag is parsed from the
// command line, or modified via SetCommandLineOption.  The validator
// function is _not_ called when the flag is directly modified
// using the = operator.
//
// The validator function should return true if a candidate flag value
// is valid, and false otherwise. If the function returns false for
// the new setting of the flag, the flag will retain its current value.
// If it returns false for the default value, InitGoogle() will die.
//
// RegisterFlagValidator is safe to call at global construct time (as
// in the example below).
//
// Example:
//    static bool ValidatePort(const char* flagname, int32_t value) {
//      if (value > 0 && value < 65536)   // value is ok
//        return true;
//      fprintf(stderr, "Invalid value for --%s: %d\n",
//              flagname, static_cast<int>(value));
//      return false;
//    }
//    DEFINE_int32(port, 0, "What port to listen on");
//    static bool dummy = RegisterFlagValidator(&FLAGS_port, &ValidatePort);
//
// Validation functions must not call back into the flag library
// except to read other flags via GetFlag or direct variable access.
//
// REQUIRES: Must be called from the .cc file that DEFINE_...()s
// the flag that is being validated.
extern bool RegisterFlagValidator(
    const bool* flag, bool (*validate_fn)(const char*, bool),
    absl::SourceLocation loc = absl::SourceLocation::current());
extern bool RegisterFlagValidator(
    const int32_t* flag, bool (*validate_fn)(const char*, int32_t),
    absl::SourceLocation loc = absl::SourceLocation::current());
extern bool RegisterFlagValidator(
    const int64_t* flag, bool (*validate_fn)(const char*, int64_t),
    absl::SourceLocation loc = absl::SourceLocation::current());
extern bool RegisterFlagValidator(
    const uint64_t* flag, bool (*validate_fn)(const char*, uint64_t),
    absl::SourceLocation loc = absl::SourceLocation::current());
extern bool RegisterFlagValidator(
    const double* flag, bool (*validate_fn)(const char*, double),
    absl::SourceLocation loc = absl::SourceLocation::current());
extern bool RegisterFlagValidator(
    const std::string* flag,
    bool (*validate_fn)(const char*, const std::string&),
    absl::SourceLocation loc = absl::SourceLocation::current());

namespace base {

// This helper models legacy v1 flags validators semantics. It is still possible
// to set a flag's value via absl::SetFlag to a value which fails validation.
// Migrating to this type in place of validators will not affect absl::SetFlag
// restrictions, thus it will not break any existing violations of the
// validators.
// To use this helper define a validation routine like this (int flag example):
//  namespace n {
//  bool ValidateMyFlag(const int& value, std::string* err) {...}
//  }  // namespace n
// This routine should return `true` if `value` is valid. Otherwise it should
// return `false` and set `err` to contain the reason for the failure.
// Define a flag like this:
// ABSL_FLAG(base::LegacyValidatedFlag<n::ValidateMyFlag>, name, 123, "help");

template <auto Validator>
struct LegacyValidatedFlag;

template <typename T, bool (*Validator)(const T&, std::string*)>
struct LegacyValidatedFlag<Validator> {
  LegacyValidatedFlag() = default;
  LegacyValidatedFlag(T init_value) : value(init_value) {}  // NOLINT
  // This is only going to be used for string flags
  LegacyValidatedFlag(const char* init_value) : value(init_value) {}  // NOLINT

  const T& Get() const { return value; }

  friend bool AbslParseFlag(absl::string_view text, LegacyValidatedFlag* f,
                            std::string* error) {
    return absl::ParseFlag(text, &f->value, error) &&
           Validator(f->value, error);
  }

  friend std::string AbslUnparseFlag(const LegacyValidatedFlag& f) {
    return absl::UnparseFlag(f.value);
  }

 private:
  T value;
};

}  // namespace base

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

// This is often used for logging.  TODO: figure out a better way
extern std::string CommandlineFlagsIntoString();

// Usually where this is used, a FlagSaver should be used instead.
extern bool ReadFlagsFromString(const std::string& flagfilecontents,
                                const char* prog_name,
                                bool errors_are_fatal);  // uses SET_FLAGS_VALUE

// --------------------------------------------------------------------
// Implementation details:
//
// The following is not part of the commandlineflags API and clients
// must not rely on details of the code below.

// Now come the command line flag declaration/definition macros that
// will actually be used.  They're kind of hairy.  A major reason
// for this is initialization: we want people to be able to access
// variables in global constructors and have that not crash, even if
// their global constructor runs before the global constructor here.
// (Obviously, we can't guarantee the flags will have the correct
// default value in that case, but at least accessing them is safe.)
// The only way to do that is have flags point to a static buffer.
// So we make one, using a union to ensure proper alignment, and
// then use placement-new to actually set up the flag with the
// correct default value.  In the same vein, we have to worry about
// flag access in global destructors, so FlagRegisterer has to be
// careful never to destroy the flag-values it constructs.
//
// Note that when we define a flag variable FLAGS_<name>, we also
// preemptively define a junk variable, FLAGS_no<name>.  This is to
// cause a link-time error if someone tries to define 2 flags with
// names like "logging" and "nologging".  We do this because a bool
// flag FLAG can be set from the command line to true with a "-FLAG"
// argument, and to false with a "-noFLAG" argument, and so this can
// potentially avert confusion.
//
// We also put flags into their own namespace.  It is purposefully
// named in an opaque way that people should have trouble typing
// directly.  The idea is that DEFINE puts the flag in the weird
// namespace, and DECLARE imports the flag from there into the current
// namespace.  The net result is to force people to use DECLARE to get
// access to a flag, rather than saying "extern bool FLAGS_whatever;"
// or some such instead.  We want this so we can put extra
// functionality (like sanity-checking) in DECLARE if we want, and
// make sure it is picked up everywhere.
//
// We also put the type of the variable in the namespace, so that
// people can't DECLARE_int32 something that they DEFINE_bool'd
// elsewhere.

class GFLAGS_DLL_DECL FlagRegisterer {
 public:
  FlagRegisterer(const char* name, const char* type, const char* help,
                 const char* filename, void* current_storage,
                 void* defvalue_storage);
};

namespace base {

namespace internal {

// This type declares the type of the mutation callback used by watched Flags
// The callback is noexcept.
// TODO: add noexcept after C++17 support is added.
typedef void (*FlagCallback)();

using absl::flags_internal::Retire;

}  // namespace internal

bool FlagHasValidatorFn(const absl::CommandLineFlag& f);
bool IsAbseilFlag(const absl::CommandLineFlag& f);

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

// If your application #defines STRIP_FLAG_HELP to a non-zero value
// before #including this file, the help string will not appear in the
// binary, and instead the pointer to the help string will resolve to
// a special string (interpreted specially by the usage printer).
// This can reduce the size of the resulting binary somewhat, and may
// also be useful for security reasons.
// TODO: Rename this macro.

#if !GOOGLE_COMMANDLINEFLAGS_FULL_API
#if !defined(STRIP_FLAG_HELP)
#define STRIP_FLAG_HELP 1
#endif
#endif

#if defined(STRIP_FLAG_HELP) && STRIP_FLAG_HELP > 0
// Need this construct to avoid the 'defined but not used' warning.
#define COMMANDLINEFLAGS_IMPL_MAYBE_STRIPPED_HELP(txt) \
  (false ? (txt) : absl::flags_internal::kStrippedFlagHelp)
#else
#define COMMANDLINEFLAGS_IMPL_MAYBE_STRIPPED_HELP(txt) txt
#endif

#define COMMANDLINEFLAGS_IMPL_DECLARE_HELP_WRAPPER(name, txt) \
  static const char* AbslFlagsWrapHelp##name() {              \
    return COMMANDLINEFLAGS_IMPL_MAYBE_STRIPPED_HELP(txt);    \
  }

// When GOOGLE_COMMANDLINEFLAGS_FULL_API is defined, we use FlagRegisterer to
// register the name, address and (optionally) the help string for each
// flag. When it is not defined, we still declare the flag variable and
// initialize it with the default value, but don't make it settable via the
// command line, and omit its associated strings from the binary.
#if GOOGLE_COMMANDLINEFLAGS_FULL_API
#define COMMANDLINEFLAGS_IMPL_REGISTER_LEGACY_FLAG(name, type_name, shorttype, \
                                                   help)                       \
  namespace fL##shorttype {                                                    \
    COMMANDLINEFLAGS_IMPL_DECLARE_HELP_WRAPPER(name, (help))                   \
    static FlagRegisterer o_##name(#name, #type_name,                          \
                                   AbslFlagsWrapHelp##name(), __FILE__,        \
                                   &FLAGS_##name, &FLAGS_no##name);            \
  }
#define COMMANDLINEFLAGS_IMPL_REGISTER_STRING_FLAG(name, help)               \
  namespace fLS {                                                            \
  COMMANDLINEFLAGS_IMPL_DECLARE_HELP_WRAPPER(name, (help))                   \
  static FlagRegisterer o_##name(#name, "string", AbslFlagsWrapHelp##name(), \
                                 __FILE__, s_##name[0].s,                    \
                                 new (s_##name[1].s)                         \
                                     std::string(*FLAGS_no##name));          \
  }
#else  // !PORTABLE_USE_FULLCOMMANDLINEFLAGS
#define COMMANDLINEFLAGS_IMPL_REGISTER_LEGACY_FLAG(name, type, shorttype, help)
#define COMMANDLINEFLAGS_IMPL_REGISTER_STRING_FLAG(name, help)
#endif  // !GOOGLE_COMMANDLINEFLAGS_FULL_API

// Each command-line flag has two variables associated with it: one
// with the current value, and one with the default value.  However,
// we have a third variable, which is where value is assigned; it's a
// constant.  This guarantees that FLAG_##value is initialized at
// static initialization time (e.g. before program-start) rather than
// at global construction time (which is after program-start but
// before main), at least when 'value' is a compile-time constant.  We
// use a small trick for the "default value" variable, and call it
// FLAGS_no<name>.  This serves the second purpose of assuring a
// compile error if someone tries to define a flag named no<name>
// which is illegal (--foo and --nofoo both affect the "foo" flag).
#define COMMANDLINEFLAGS_IMPL_DEFINE_VARIABLE(type, type_name, shorttype,      \
                                              name, value, help)               \
  namespace fL##shorttype {                                                    \
    extern GFLAGS_DLL_DECLARE_FLAG type FLAGS_##name;                          \
    extern type FLAGS_no##name;                                                \
    static const type FLAGS_nono##name = value;                                \
    /* We always want to export defined variables, dll or no */                \
    GFLAGS_DLL_DEFINE_FLAG type FLAGS_##name = FLAGS_nono##name;               \
    [[maybe_unused]] type FLAGS_no##name = FLAGS_nono##name;                   \
  }                                                                            \
  COMMANDLINEFLAGS_IMPL_REGISTER_LEGACY_FLAG(name, type_name, shorttype, help) \
  using fL##shorttype::FLAGS_##name

// For DEFINE_bool, we want to do the extra check that the passed-in
// value is actually a bool, and not a string or something that can be
// coerced to a bool.  These declarations (no definition needed!) will
// help us do that, and never evaluate From, which is important.
// We'll use 'sizeof(IsBool(val))' to distinguish. This code requires
// that the compiler have different sizes for bool & double. Since
// this is not guaranteed by the standard, we check it with a
// static_assert.
namespace fLB {
static_assert(sizeof(double) != sizeof(bool),
              "expected_sizeof_double_neq_sizeof_bool");
template <typename From>
double GFLAGS_DLL_DECL IsBoolFlag(const From& from);
GFLAGS_DLL_DECL bool IsBoolFlag(bool from);
}  // namespace fLB

// Here are the actual DEFINE_*-macros. The respective DECLARE_*-macros
// are in a separate include, commandlineflags_declare.h, for reducing
// the physical transitive size for DECLARE use.
#define DEFINE_bool(name, val, txt)                               \
  namespace fLB {                                                 \
  static_assert(sizeof(::fLB::IsBoolFlag(val)) != sizeof(double), \
                "FLAG_##name##_value_is_not_a_bool");             \
  }                                                               \
  COMMANDLINEFLAGS_IMPL_DEFINE_VARIABLE(bool, bool, B, name, val, txt)

#define DEFINE_int32(name, val, txt) \
  COMMANDLINEFLAGS_IMPL_DEFINE_VARIABLE(int32_t, int32, I, name, val, txt)

#define DEFINE_int64(name, val, txt) \
  COMMANDLINEFLAGS_IMPL_DEFINE_VARIABLE(int64_t, int64, I64, name, val, txt)

#define DEFINE_uint64(name, val, txt) \
  COMMANDLINEFLAGS_IMPL_DEFINE_VARIABLE(uint64_t, uint64, U64, name, val, txt)

#define DEFINE_double(name, val, txt) \
  COMMANDLINEFLAGS_IMPL_DEFINE_VARIABLE(double, double, D, name, val, txt)

// Strings are trickier, because they're not a POD, so we can't
// construct them at static-initialization time (instead they get
// constructed at global-constructor time, which is much later).  To
// try to avoid crashes in that case, we use a char buffer to store
// the string, which we can static-initialize, and then placement-new
// into it later.  It's not perfect, but the best we can do.

namespace fLS {

inline std::string* dont_pass0toDEFINE_string(char* absl_nonnull stringspot,
                                              const char* absl_nonnull value) {
  return new (stringspot) std::string(value);
}
inline std::string* dont_pass0toDEFINE_string(char* absl_nonnull stringspot,
                                              absl::string_view value) {
  return new (stringspot) std::string(value);
}
inline std::string* dont_pass0toDEFINE_string(char* stringspot,
                                              int value) = delete;
}  // namespace fLS

// We need to define a var named FLAGS_no##name so people don't define
// --string and --nostring.  And we need a temporary place to put val
// so we don't have to evaluate it twice.  Two great needs that go
// great together!
// The weird 'using' + 'extern' inside the fLS namespace is to work around
// an unknown compiler bug/issue with the gcc 4.2.1 on SUSE 10.  See
//    https://github.com/gflags/gflags/issues/31
#define DEFINE_string(name, val, txt)                       \
  namespace fLS {                                           \
  static union {                                            \
    void* align;                                            \
    char s[sizeof(std::string)];                            \
  } s_##name[2];                                            \
  std::string* const FLAGS_no##name =                       \
      ::fLS::dont_pass0toDEFINE_string(s_##name[0].s, val); \
  extern GFLAGS_DLL_DEFINE_FLAG std::string& FLAGS_##name;  \
  using fLS::FLAGS_##name;                                  \
  std::string& FLAGS_##name = *FLAGS_no##name;              \
  } /* namespace fLS */                                     \
  COMMANDLINEFLAGS_IMPL_REGISTER_STRING_FLAG(name, txt)     \
  using fLS::FLAGS_##name

#endif  // SWIG

// --------------------------------------------------------------------
// Old style type erased flag information storage and interfaces to access it.

// struct CommandLineFlagInfo holds all information for a flag.
struct CommandLineFlagInfo {
  std::string name;           // the name of the flag
  std::string type;           // DO NOT use. Use flag->IsOfType<T>() instead.
  std::string description;    // the "help text" associated with the flag
  std::string current_value;  // the current value, as a string
  std::string default_value;  // the default value, as a string
  std::string filename;       // 'cleaned' version of filename holding the flag
  bool has_validator_fn;  // true if RegisterFlagValidator called on this flag

  ABSL_DEPRECATED(
      "This field has a misleading name and behavior. E.g., setting"
      " the flag to its default value directly (assigning to FLAGS_##name) will"
      " cause is_default to remain true, but setting the flag via "
      "SetCommandLineOption() will cause is_default to become false. Prefer "
      "calling WasPresentOnCommandLine to determine if the value has been "
      "provided via the command line (directly or indirectly), or comparing "
      "current_value and default_value to determine if the flag has the default"
      " value.")
  bool is_default;  // true if the flag has the default value and
                    // has not been set explicitly from the cmdline
                    // or via SetCommandLineOption.

  // nullptr for ABSL_FLAG.  A pointer to the flag's current value
  // otherwise.  E.g., for DEFINE_int32(foo, ...), flag_ptr will be
  // &FLAGS_foo.
  const void* flag_ptr;
};

//-----------------------------------------------------------------------------

// If a flag named "name" exists, store its information in *output_info
// and return true. Else return false without changing *output_info.
// Thread-safe.
bool GetCommandLineFlagInfo(absl::string_view name,
                            CommandLineFlagInfo* output_info);

// Returns the CommandLineFlagInfo of the flagname.  exit() with an
// error code if name not found. This function should not be called on platforms
// where GOOGLE_COMMANDLINEFLAGS_FULL_API=0.
// Thread-safe.
CommandLineFlagInfo GetCommandLineFlagInfoOrDie(absl::string_view name);

// Store the list of all flags in *output_vector, sorted by file.
void GetAllFlags(std::vector<CommandLineFlagInfo>* output_vector);

//-----------------------------------------------------------------------------

#endif  // THIRD_PARTY_GLOOP_BASE_COMMANDLINEFLAGS_H_
