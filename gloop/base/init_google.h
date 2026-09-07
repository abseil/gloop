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

#ifndef THIRD_PARTY_GLOOP_BASE_INIT_GOOGLE_H_
#define THIRD_PARTY_GLOOP_BASE_INIT_GOOGLE_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/base/nullability.h"
#include "absl/flags/declare.h"
#include "absl/strings/string_view.h"
#include "gloop/base/googleinit.h"

// If root, switch to this group id
ABSL_DECLARE_FLAG(std::string, gid);

// Nice priority level of the program
ABSL_DECLARE_FLAG(int32_t, nice_priority_level);

// syslog() on program start-up.
ABSL_DECLARE_FLAG(bool, syslog_on_start);

// If root, switch to this user id
ABSL_DECLARE_FLAG(std::string, uid);

// Data version of the server
ABSL_DECLARE_FLAG(std::string, data_version);

// You can use this identifier to run your initializer before parsing of the
// command-line flags.
DECLARE_MODULE_INITIALIZER(command_line_flags_parsing);
// You can use this identifier to run your initializer after parsing of the
// command-line flags.
DECLARE_MODULE_INITIALIZER(command_line_flags_parsed);

// `InitGoogle` and `InitGoogleExceptChangeRootAndUser` have multiple overloads.
// When providing a custom string constant, https://abseil.io/tips/1 and
// https://abseil.io/tips/140 suggest using `constexpr absl::string_view` for
// declaring that constant, and so it is the primary overload. The others are
// present for overload resolution regarding legacy string constant cases (e.g.,
// `constexpr char kUsage[] = "..."`), potentially null pointer cases (e.g.,
// `const char* usage = ... ? nullptr : "...";` or just `nullptr`), or
// `argv[0]`.
//
// EXPLANATION: The old implementation of `absl::string_view` used to support
// null data pointers. However, STL's `string_view` does not. There is an effort
// to migrate to the STL version, which may require runtime null pointer
// checking where static analysis is not sufficient. As a result, some of the
// additional overloads use these checks.

// Initializes misc google-related things in the binary. This includes
// initializing services such as command line flags, walltime, main-thread-id,
// logging directories, malloc extension, kernel version, and chrooting.
//
// In particular it does REQUIRE_MODULE_INITIALIZED(command_line_flags_parsing),
// parses command line flags and does RUN_MODULE_INITIALIZERS() (in that order).
// If a flag is defined more than once in the command line or flag
// file, the last definition is used.
//
// Typically called early on in main() and must be called before other
// threads start using functions from this file.
//
// 'usage' provides a short usage message passed to
//         absl::SetProgramUsageMessage().
//         Most callers provide the name of the app as 'usage' ?!
// 'argc' and 'argv' are the command line flags to parse. There is no
//         requirement for an element (*argv)[*argc] to exist or to have
//         any particular value, unlike the similar array that is passed
//         to the `main` function.
// If 'remove_flags' then parsed flags are removed from *argc/*argv.
void InitGoogle(absl::string_view usage, int* absl_nonnull argc,
                char* absl_nullable* absl_nonnull* absl_nonnull argv,
                bool remove_flags);

// Overload of `InitGoogle` where a `nullptr` `usage` value is treated as an
// empty string.
void InitGoogle(std::nullptr_t /*usage*/, int* absl_nonnull argc,
                char* absl_nullable* absl_nonnull* absl_nonnull argv,
                bool remove_flags);

// Overload of `InitGoogle` where the `usage` pointer is treated as an empty
// string if it is null. Non-null pointers are treated as null-terminated
// strings.
void InitGoogle(const char* absl_nullable usage, int* absl_nonnull argc,
                char* absl_nullable* absl_nonnull* absl_nonnull argv,
                bool remove_flags);

// Colab's adhoc import feature creates a shared library that is loaded into
// another process. To make flags etc that are defined in the shared library
// (usually) work, Colab needs to trigger InitGoogle. Thus, we export a well
// known name for it to call. This decouples the accessibility of the symbol
// from the particularities of the underlying InitGoogle functions, e.g. they
// can be marked as inline. See b/373482396 and
// <path> for more details.
// NOTE: While a name is exported, other ABI stability guarantees aren't
// provided.
// IMPORTANT: Only Colab is blessed to use this.
extern "C" void InitGoogleExportedForColab(
    const char* absl_nullable usage, int* absl_nonnull argc,
    char* absl_nullable* absl_nonnull* absl_nonnull argv, bool remove_flags);

// Normally, InitGoogle will chroot (if requested with the --chroot flag)
// and setuid to --uid and --gid (default nobody).
// This version will not, and you will be responsible for calling
// ChangeRootAndUser
// This option is provided for applications that need to read files outside
// the chroot before chrooting.
void InitGoogleExceptChangeRootAndUser(
    absl::string_view usage, int* absl_nonnull argc,
    char* absl_nullable* absl_nonnull* absl_nonnull argv, bool remove_flags);

// Overload of `InitGoogleExceptChangeRootAndUser` where a `nullptr` `usage`
// value is treated as an empty string.
inline void InitGoogleExceptChangeRootAndUser(
    std::nullptr_t /*usage*/, int* absl_nonnull argc,
    char* absl_nullable* absl_nonnull* absl_nonnull argv, bool remove_flags) {
  return InitGoogleExceptChangeRootAndUser(/*usage=*/absl::string_view(), argc,
                                           argv, remove_flags);
}

// Overload of `InitGoogleExceptChangeRootAndUser` where a the `usage` pointer
// is treated as an empty string if it is null. Non-null pointers are treated as
// null-terminated strings.
inline void InitGoogleExceptChangeRootAndUser(
    const char* absl_nullable usage, int* absl_nonnull argc,
    char* absl_nullable* absl_nonnull* absl_nonnull argv, bool remove_flags) {
  return InitGoogleExceptChangeRootAndUser(absl::NullSafeStringView(usage),
                                           argc, argv, remove_flags);
}

// Thread-hostile.
// WARNING: ChangeRootAndUser() is NOT suitable as the sole privilege-drop
// mechanism for setuid-root binaries because all controlling flags
// (--uid, --gid, --chroot) are invoker-supplied and parsed before the drop.
void ChangeRootAndUser();

// Checks (only in debug mode) if InitGoogle() has been fully executed
// and crashes if it has not been.
// Intended for checking that code that depends on complete execution
// of InitGoogle() for its proper functioning is safe to execute.
void AssertInitGoogleIsDone();

// Checks (in all modes) whether InitGoogle() has been fully executed.
// May either crash or print an error message if it has not been.
// The intent is that certain uses (based on stack trace) will cause errors
// initially, and be converted to crashes once those uses are eliminated.
// Any error message output is prefixed with "message".
void CheckInitGoogleIsDone(absl::string_view message);

namespace base {
// Only a limited set of places are allowed to query whether or not InitGoogle
// has finished executing. Typical users of this functionality are profilers and
// related libraries, not application code. (Application code should explicitly
// run desired code after calling InitGoogle).
//
// If you have a library that has a strong case for using this functionality,
// please contact //gloop/base/OWNERS.
class InitGoogleState {
 private:
  // Blocks until `InitGoogle()` is done.
  // Note that you will create a deadlock if you call it before InitGoogle from
  // the main thread.
  static void Wait();
  // Returns true iff `InitGoogle()` is done.
  static bool IsDone();
};
}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_INIT_GOOGLE_H_
