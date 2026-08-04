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
// Flavors of flags
// ----------------
// (a) Encapsulated flags created by ABSL_FLAG.  These can be of any
// type T that supports parsing, unparsing, etc.  Each such flag is
// represented by a programmer visible object of type Flag<T>.
//
// (b) Direct access flags created by DEFINE_<type> for a few types (bool,
// int32_t, int64_t, uint64_t, double, string).  Each such flag is represented
// by a global variable of type <type>.
//
// (c) Retired flags: these just reserve a flag name but are not actually
// made available for programs to inspect/modify.
//
// Important types
// ---------------
// Every flag (regardless of flavor) is represented by a
// CommandLineFlag object. CommandLineFlag holds all information about
// a single flag such as its name and type.
//
// CommandLineFlag holds all flag state.
//
// CommandLineFlag::op is a pointer to a function that provides all
// type-specific operations like parsing, unparsing, allocation, etc.
//
// FlagRegistry is a collection of CommandLineFlag objects.  There's
// just a single global registry, where all defined flags live.
//
// FlagRegisterer is the helper class used by the DEFINE_* macros to
// allow work to be done at global initialization time.
//
// CommandLineFlagParser is the class that reads from the commandline
// and instantiates flag values based on that.  It operates on the
// contents of the global registry.

#include "gloop/base/commandlineflags.h"

#include <assert.h>
#include <stdarg.h>  // For va_list and related operations
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
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
#include "absl/cleanup/cleanup.h"
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
#include "gloop/base/strerror.h"

#if GOOGLE_HAVE_FNMATCH
#include <fnmatch.h>
#endif

#ifndef PATH_SEPARATOR
#define PATH_SEPARATOR '/'
#endif

namespace {
namespace absl_flags = absl::flags_internal;
}  // namespace

namespace base {
namespace {

// Return true iff flag value was changed via direct-access.
// `flag` is the pointer to the flag object beging tested
// `a` is the pointer to a flag value being compared
// `b` is the pointer to a flag value being compared
// This function compares values of `a` and 'b' according to the flag's value
// type.
bool ChangedDirectly(absl::CommandLineFlag* flag, const void* a,
                     const void* b) {
#define CHANGED_FOR_TYPE(T)                                                  \
  if (flag->IsOfType<T>()) {                                                 \
    return *reinterpret_cast<const T*>(a) != *reinterpret_cast<const T*>(b); \
  }

  CHANGED_FOR_TYPE(bool);
  CHANGED_FOR_TYPE(int32_t);
  CHANGED_FOR_TYPE(int64_t);
  CHANGED_FOR_TYPE(uint64_t);
  CHANGED_FOR_TYPE(double);
  CHANGED_FOR_TYPE(std::string);
#undef CHANGED_FOR_TYPE

  return false;
}

using FlagValidator = bool (*)();
class CommandLineV1Flag;

class V1FlagState : public absl_flags::FlagStateInterface {
 public:
  V1FlagState(CommandLineV1Flag* flag, int64_t counter, FlagValidator validator,
              bool modified, bool on_command_line, void* current,
              absl_flags::FlagOpFn op)
      : flag_(flag),
        counter_(counter),
        validator_(validator),
        modified_(modified),
        on_command_line_(on_command_line),
        current_(current),
        op_(op) {}

  ~V1FlagState() override { absl::flags_internal::Delete(op_, current_); }

 private:
  friend class CommandLineV1Flag;

  // Restores the flag to the saved state.
  void Restore() && override;

  // Flag and saved state.
  CommandLineV1Flag* flag_;
  int64_t counter_;
  FlagValidator validator_;
  bool modified_;
  bool on_command_line_;
  void* current_;
  absl_flags::FlagOpFn op_;
};

class CommandLineV1Flag final : public absl::CommandLineFlag {
 public:
  constexpr CommandLineV1Flag(const char* name, const char* help,
                              const char* filename,
                              const absl_flags::FlagOpFn op,
                              void* defvalue_storage, void* current_storage)
      : name_(name),
        filename_(filename),
        op_(op),
        help_(help),
        def_(defvalue_storage),
        cur_(current_storage) {}

  // Non-polymorphic access methods
  void* GetAddr() const { return cur_; }
  void* GetDefault() const { return def_; }
  void SetModified(bool is_modified) {
    absl::MutexLock l(*DataGuard());
    modified_ = is_modified;
  }
  bool HasValidatorFn() const {
    absl::MutexLock l(*DataGuard());
    return validator_ != nullptr;
  }
  bool SetValidator(FlagValidator fn) {
    absl::MutexLock l(*DataGuard());
    return SetValidatorLocked(fn);
  }
  bool SetValidatorLocked(FlagValidator fn)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(data_guard_) {
    // ok to register the same function over and over again
    if (fn == validator_) return true;

    // Can't set validator to a different function, unless reset first.
    if (fn != nullptr && validator_ != nullptr) {
      ABSL_INTERNAL_LOG(
          WARNING, absl::StrCat("Ignoring SetValidator() for flag '", Name(),
                                "': validate-fn already registered"));

      return false;
    }

    validator_ = fn;
    counter_++;
    return true;
  }

  // Polymorphic access methods
  absl::string_view Name() const override { return name_; }
  std::string Filename() const override {
    return absl_flags::GetUsageConfig().normalize_filename(filename_);
  }
  absl::string_view Typename() const {
    if (IsOfType<bool>()) return "bool";
    if (IsOfType<int32_t>()) return "int32";
    if (IsOfType<int64_t>()) return "int64";
    if (IsOfType<uint64_t>()) return "uint64";
    if (IsOfType<double>()) return "double";
    if (IsOfType<std::string>()) return "string";

    return "";
  }
  std::string Help() const override { return help_; }
  bool IsAbseilFlag() const override { return false; }
  absl_flags::FlagFastTypeId TypeId() const override {
    return absl_flags::FastTypeId(op_);
  }
  bool IsModified() const override {
    absl::MutexLock l(*DataGuard());
    return modified_;
  }
  bool IsSpecifiedOnCommandLine() const override {
    absl::MutexLock l(*DataGuard());
    return on_command_line_;
  }
  std::string DefaultValue() const override {
    absl::MutexLock l(*DataGuard());
    return absl::flags_internal::Unparse(op_, def_);
  }
  std::string CurrentValue() const override {
    absl::MutexLock l(*DataGuard());
    return absl::flags_internal::Unparse(op_, cur_);
  }
  bool ValidateDefaultValue() const override {
    absl::MutexLock lock(*DataGuard());
    if (modified_) return true;
    return InvokeValidator(def_);
  }
  bool ValidateInputValue(absl::string_view value) const override {
    absl::MutexLock l(*DataGuard());

    void* obj = absl::flags_internal::Clone(op_, def_);
    std::string ignored_error;
    const bool result = absl_flags::Parse(op_, value, obj, &ignored_error) &&
                        InvokeValidator(obj);
    absl::flags_internal::Delete(op_, obj);
    return result;
  }
  std::unique_ptr<absl_flags::FlagStateInterface> SaveState() override {
    absl::MutexLock l(*DataGuard());
    return std::make_unique<V1FlagState>(
        this, counter_, validator_, modified_, on_command_line_,
        absl::flags_internal::Clone(op_, cur_), op_);
  }
  // Restores the flag state to the supplied state object.
  bool RestoreState(const V1FlagState& flag_state) {
    {
      absl::MutexLock l(*DataGuard());
      modified_ = flag_state.modified_;
      on_command_line_ = flag_state.on_command_line_;

      if (counter_ == flag_state.counter_ &&
          !ChangedDirectly(this, flag_state.current_, cur_))
        return false;

      absl::flags_internal::Copy(op_, flag_state.current_, cur_);

      counter_++;

      SetValidatorLocked(flag_state.validator_);
    }

    // Revalidate the flag because the validator might store state based
    // on the flag's value, which just changed due to the restore.
    // Failing validation is ignored because it's assumed that the flag
    // was valid previously and there's little that can be done about it
    // here, anyway.
    if (!ValidateInputValue(CurrentValue())) {
      LOG(WARNING) << "Saved value " << CurrentValue()
                   << " did not pass validation for flag " << Name();
    }
    return true;
  }
  bool ParseFrom(absl::string_view value, absl_flags::FlagSettingMode set_mode,
                 absl_flags::ValueSource source, std::string& err) override {
    absl::MutexLock l(*DataGuard());

    // Direct-access flags can be modified without going through the
    // flag API. Detect such changes and update the flag->modified_ bit.
    if (!modified_ && ChangedDirectly(this, cur_, def_)) {
      modified_ = true;
    }

    switch (set_mode) {
      case absl_flags::FlagSettingMode::SET_FLAGS_VALUE: {
        // set or modify the flag's value
        if (!TryParseLocked(cur_, value, err)) return false;
        modified_ = true;

        if (source == absl_flags::kCommandLine) {
          on_command_line_ = true;
        }
        break;
      }
      case absl_flags::FlagSettingMode::SET_FLAG_IF_DEFAULT: {
        // set the flag's value, but only if it hasn't been set by someone else
        if (!modified_) {
          if (!TryParseLocked(cur_, value, err)) return false;
          modified_ = true;
        } else {
          // TODO: review and fix this semantic. Currently we do not
          // fail in this case if flag is modified. This is misleading since the
          // flag's value is not updated even though we return true.
          //   *err = absl::StrCat(Name(), " is already set to ",
          //                       CurrentValue(), "\n");
          //   return false;
          return true;
        }
        break;
      }
      case absl_flags::FlagSettingMode::SET_FLAGS_DEFAULT: {
        // modify the flag's default-value
        if (!TryParseLocked(def_, value, err)) return false;

        if (!modified_) {
          // Need to set both defvalue *and* current, in this case
          absl::flags_internal::Copy(op_, def_, cur_);
        }
        break;
      }
    }

    return true;
  }

  void CheckDefaultValueParsingRoundtrip() const override {}

 private:
  absl::Mutex* DataGuard() const ABSL_LOCK_RETURNED(data_guard_) {
    if (ABSL_PREDICT_FALSE(!inited_.load(std::memory_order_acquire))) {
      ABSL_CONST_INIT static absl::Mutex init_lock(absl::kConstInit);

      absl::MutexLock lock(init_lock);
      if (!data_guard_.has_value()) {  // Must initialize Mutex for this flag.
        const_cast<CommandLineV1Flag*>(this)->data_guard_.emplace();
      }

      inited_.store(true, std::memory_order_release);
    }

    return const_cast<absl::Mutex*>(&*data_guard_);
  }

  void Read(void* dst) const override {
    absl::ReaderMutexLock l(*DataGuard());
    absl::flags_internal::CopyConstruct(op_, cur_, dst);
  }

  bool InvokeValidator(const void* value) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(data_guard_) {
    if (!validator_) {
      return true;
    }

#define ABSL_FLAGS_HANDLE_TYPE(T, ArgType)                                     \
  if (IsOfType<T>()) {                                                         \
    const T* v = reinterpret_cast<const T*>(value);                            \
    return reinterpret_cast<bool (*)(const char*, ArgType)>(validator_)(name_, \
                                                                        *v);   \
  }

    ABSL_FLAGS_HANDLE_TYPE(bool, bool);
    ABSL_FLAGS_HANDLE_TYPE(int32_t, int32_t);
    ABSL_FLAGS_HANDLE_TYPE(int64_t, int64_t);
    ABSL_FLAGS_HANDLE_TYPE(uint64_t, uint64_t);
    ABSL_FLAGS_HANDLE_TYPE(double, double);
    ABSL_FLAGS_HANDLE_TYPE(std::string, const std::string&);
#undef ABSL_FLAGS_HANDLE_TYPE

    ABSL_INTERNAL_LOG(
        FATAL,
        absl::StrCat("Flag '", Name(),
                     "' of encapsulated type should not have a validator"));

    return false;
  }

  // Attempts to parse supplied `value` string using parsing routine in the
  // `flag` argument. If parsing is successful, it will try to validate that the
  // parsed value is valid for the specified 'flag'. Finally this function
  // stores the parsed value in 'dst' assuming it is a pointer to the flag's
  // value type. In case if any error is encountered in either step, the error
  // message is stored in 'err'
  bool TryParseLocked(void* dst, absl::string_view value, std::string& err)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(data_guard_) {
    void* tentative_value = absl::flags_internal::Clone(op_, def_);
    std::string parse_err;
    if (!absl_flags::Parse(op_, value, tentative_value, &parse_err)) {
      auto type_name = Typename();
      absl::string_view err_sep = parse_err.empty() ? "" : "; ";
      absl::string_view typename_sep = type_name.empty() ? "" : " ";
      err = absl::StrCat("Illegal value '", value, "' specified for",
                         typename_sep, type_name, " flag '", Name(), "'",
                         err_sep, parse_err);
      absl::flags_internal::Delete(op_, tentative_value);
      return false;
    }

    if (!InvokeValidator(tentative_value)) {
      err = absl::StrCat("Failed validation of new value '",
                         absl::flags_internal::Unparse(op_, tentative_value),
                         "' for flag '", Name(), "'");
      absl::flags_internal::Delete(op_, tentative_value);
      return false;
    }

    counter_++;
    absl::flags_internal::Copy(op_, tentative_value, dst);
    absl::flags_internal::Delete(op_, tentative_value);
    return true;
  }

  // Immutable state (after initialization).
  // Flags name passed to DEFINE_<type> as second arg.
  const char* const name_;
  // The file name where DEFINE_<type> resides.
  const char* const filename_;
  // Type-specific handler.
  const absl_flags::FlagOpFn op_;
  // Help message literal.
  const char* help_;
  // Lazily initialized mutex for this flag's data.
  std::optional<absl::Mutex> data_guard_;

  // True is data_guard_ has been lazily initialized.
  mutable std::atomic<bool> inited_{false};

  // Mutable state (guarded by data_guard).
  bool modified_ = false;              // Has flag value been modified?
  bool on_command_line_ = false;       // Specified on command line.
  void* def_;                          // Pointer to default value.
  void* cur_;                          // Pointer to current value.
  int64_t counter_ = 0;                // Mutation counter.
  FlagValidator validator_ = nullptr;  // Validator function, or nullptr.
};

void V1FlagState::Restore() && {
  if (flag_->RestoreState(*this)) {
    LOG(INFO) << "Restore saved value of " << flag_->Name()
              << " to: " << flag_->CurrentValue();
  }
}

// A map from flag address to absl::CommandLineFlag*. Used when
// registering validators.
class FlagAddressToFlagMap {
 public:
  void Register(CommandLineV1Flag* flag) {
    absl::MutexLock lock(guard_);

    auto& vec = buckets_[BucketForFlag(flag->GetAddr())];
    if (vec.size() == vec.capacity()) {
      // Bypass default 2x growth factor with 1.25 so we have fuller vectors.
      // This saves 4% memory compared to default growth.
      vec.reserve(vec.size() * 1.25 + 0.5);
    }
    vec.push_back(flag);
  }

  CommandLineV1Flag* FindFlagByAddress(const void* flag_addr) {
    absl::MutexLock lock(guard_);

    const auto& flag_vector = buckets_[BucketForFlag(flag_addr)];
    for (CommandLineV1Flag* entry : flag_vector) {
      if (entry->GetAddr() == flag_addr) {
        return entry;
      }
    }
    return nullptr;
  }

 private:
  // Instead of std::map, we use a custom hash table where each bucket stores
  // flags in a vector. This reduces memory usage 40% of the memory that would
  // have been used by std::map.
  //
  // kNumBuckets was picked as a large enough prime. As of writing this code, a
  // typical large binary has ~8k (old-style) flags, and this would gives
  // buckets with roughly 50 elements each.
  //
  // Note that reads to this hash table are rare: exactly as many as we have
  // flags with validators. As of writing, a typical binary only registers 52
  // validated flags.
  static constexpr size_t kNumBuckets = 163;
  std::vector<CommandLineV1Flag*> buckets_[kNumBuckets];
  absl::Mutex guard_;

  static int BucketForFlag(const void* ptr) {
    // Modulo a prime is good enough here. On a real program, bucket size stddev
    // after registering 8k flags is ~5 (mean size at 51).
    return reinterpret_cast<uintptr_t>(ptr) % kNumBuckets;
  }
};
constexpr size_t FlagAddressToFlagMap::kNumBuckets;

FlagAddressToFlagMap* GlobalFlagAddressToFlagMap() {
  static FlagAddressToFlagMap* global_flag_addr_map = new FlagAddressToFlagMap;
  return global_flag_addr_map;
}

CommandLineV1Flag* FindCommandLineV1Flag(const void* flag_addr) {
  return GlobalFlagAddressToFlagMap()->FindFlagByAddress(flag_addr);
}

}  // namespace

bool IsAbseilFlag(const absl::CommandLineFlag& f) {
  return absl_flags::PrivateHandleAccessor::IsAbseilFlag(f);
}

bool FlagHasValidatorFn(const absl::CommandLineFlag& f) {
  if (base::IsAbseilFlag(f)) return false;
  const auto* v1_flag = dynamic_cast<const CommandLineV1Flag*>(&f);
  return v1_flag && v1_flag->HasValidatorFn();
}

bool IsValidFlagValue(absl::string_view name, absl::string_view value) {
  absl::CommandLineFlag* flag = absl::FindCommandLineFlag(name);

  return flag != nullptr &&
         (flag->IsRetired() ||
          absl_flags::PrivateHandleAccessor::ValidateInputValue(*flag, value));
}

}  // namespace base

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
    if (!absl::StartsWith(flag_name, "no")) {
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
      if (!absl_flags::PrivateHandleAccessor::IsAbseilFlag(*flag)) {
        auto* v1_flag = static_cast<base::CommandLineV1Flag*>(flag);
        type_name = v1_flag->Typename();
      }
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
static bool ReadFileIntoString(const char* filename, std::string* out_or_err) {
  const int kBufSize = 8092;
  char buffer[kBufSize];
  FILE* fp = fopen(filename, "r");
  if (!fp) {
    *out_or_err = base::StrError(errno);
    return false;
  }
  auto fcleanup = absl::MakeCleanup([fp] { fclose(fp); });
  out_or_err->clear();
  size_t n;
  while ((n = fread(buffer, 1, kBufSize, fp)) > 0) {
    if (ferror(fp)) {
      *out_or_err = base::StrError(errno);
      return false;
    }
    out_or_err->append(buffer, n);
  }
  if (ferror(fp)) {
    *out_or_err = base::StrError(errno);
    return false;
  }
  return true;
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
    std::string content_or_err;
    if (!ReadFileIntoString(file, &content_or_err)) {
      error_flags_[filename] = absl::StrFormat(
          "%sFailed to open flagfile %s: %s\n", kError, file, content_or_err);
      continue;
    }
    absl::StrAppend(&msg,
                    ProcessOptionsFromString(content_or_err, set_mode, file));
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

// --------------------------------------------------------------------
// GetFromEnv()
// AddFlagValidator()
//    These are helper functions for routines like BoolFromEnv() and
//    RegisterFlagValidator, defined below.  They're defined here so
//    they can live in the unnamed namespace (which makes friendship
//    declarations for these classes possible).
// --------------------------------------------------------------------
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

bool AddFlagValidator(const void* flag_addr,
                      base::FlagValidator validate_fn_proto,
                      absl::SourceLocation loc) {
  // Encapsulated flags do not support validators, and there is no
  // exported RegisterFlagValidator() that can be passed the address
  // of an encapsulated flag.  So this path is only reached for
  // direct-access flags.
  //
  // For such a flag, flag_addr is the address of the variable that
  // holds the current value of the flag.  Find the associated
  // CommandLineFlag object using the map from address to
  // absl::CommandLineFlag* maintained inside the registry.
  base::CommandLineV1Flag* flag = base::FindCommandLineV1Flag(flag_addr);
  if (!flag) {
    LOG(WARNING) << loc.file_name() << ":" << loc.line()
                 << " Ignoring RegisterValidateFunction() for flag address "
                 << flag_addr << ": no flag found at that address";
    return false;
  }

  return flag->SetValidator(validate_fn_proto);
}

// Special type used when invalid type is passed to FlagRegisterer.
struct UnknownType {};
bool AbslParseFlag(absl::string_view text, UnknownType*, std::string*) {
  return false;
}
std::string AbslUnparseFlag(UnknownType v) { return ""; }

}  // namespace

// Now define the functions that are exported via the .h file

// --------------------------------------------------------------------
// FlagRegisterer
//    This class exists merely to have a global constructor (the
//    kind that runs before main(), that goes and initializes each
//    flag that's been declared.  Note that it's very important we
//    don't have a destructor that deletes flag_, because that would
//    cause us to delete current_storage/defvalue_storage as well,
//    which can cause a crash if anything tries to access the flag
//    values in a global destructor.
// --------------------------------------------------------------------

FlagRegisterer::FlagRegisterer(const char* name, const char* type,
                               const char* help, const char* filename,
                               void* current_storage, void* defvalue_storage) {
  if (help == nullptr) help = "";
  // Callers expects the type-name to not include any namespace
  // components, so we get rid of those, if any.
  if (strchr(type, ':')) type = strrchr(type, ':') + 1;

  // Find the op function for this type.
  absl_flags::FlagOpFn op = nullptr;
#define HANDLE_BUILTIN_TYPE(t, name)    \
  if (!op && strcmp(type, name) == 0) { \
    op = &absl_flags::FlagOps<t>;       \
  }

  HANDLE_BUILTIN_TYPE(bool, "bool");
  HANDLE_BUILTIN_TYPE(int32_t, "int32");
  HANDLE_BUILTIN_TYPE(int64_t, "int64");
  HANDLE_BUILTIN_TYPE(uint64_t, "uint64");
  HANDLE_BUILTIN_TYPE(double, "double");
#undef HANDLE_BUILTIN_TYPE

  if (!op && strcmp(type, "string") == 0) {
    op = &absl_flags::FlagOps<std::string>;
  }

  if (!op) {
    LOG(DFATAL) << "Unknown flag type '" << type << "'";
    op = absl_flags::FlagOps<UnknownType>;
  }

  auto* flag = new base::CommandLineV1Flag(name, help, filename, op,
                                           defvalue_storage, current_storage);

  // TODO: move into CommandLineV1Flag in next CL
  if (op != &absl_flags::FlagOps<std::string>) {
    // Ensure that both Sizeof() and s are used even if
    // ANNOTATE_BENIGN_RACE_SIZED is a no-op to avoid compiler
    // warnings about unused function/variable.
    const size_t s = absl::flags_internal::Sizeof(op);
    if (s > 0) {
      ABSL_ANNOTATE_BENIGN_RACE_SIZED(flag->GetAddr(), s, "FLAGS value");
    }
  }

  absl_flags::RegisterCommandLineFlag(*flag, nullptr);
  base::GlobalFlagAddressToFlagMap()->Register(flag);
}

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

// --------------------------------------------------------------------
// Old style type erased flag information storage and interfaces to access it.

struct FilenameFlagnameLess {
  bool operator()(const CommandLineFlagInfo& a,
                  const CommandLineFlagInfo& b) const {
    int cmp = absl::string_view(a.filename).compare(b.filename);
    if (cmp != 0) return cmp < 0;
    return a.name < b.name;
  }
};

static void FillCommandLineFlagInfo(absl::CommandLineFlag& flag,
                                    CommandLineFlagInfo* result) {
  assert(!flag.IsRetired());

  result->name = std::string(flag.Name());
  result->description = flag.Help();
  result->filename = flag.Filename();

  if (absl_flags::PrivateHandleAccessor::IsAbseilFlag(flag)) {
    result->has_validator_fn = false;
  } else {
    auto* v1_flag = static_cast<base::CommandLineV1Flag*>(&flag);

    if (!v1_flag->IsModified() &&
        ChangedDirectly(v1_flag, v1_flag->GetAddr(), v1_flag->GetDefault())) {
      v1_flag->SetModified(true);
    }

    result->type = std::string(v1_flag->Typename());
    result->has_validator_fn = v1_flag->HasValidatorFn();
  }

  result->current_value = flag.CurrentValue();
  result->default_value = flag.DefaultValue();
  result->is_default = !absl_flags::PrivateHandleAccessor::IsModified(flag);
  result->flag_ptr =
      absl_flags::PrivateHandleAccessor::IsAbseilFlag(flag)
          ? nullptr
          : static_cast<base::CommandLineV1Flag*>(&flag)->GetAddr();
}

bool GetCommandLineFlagInfo(absl::string_view name,
                            CommandLineFlagInfo* output_info) {
  if (name.empty()) return false;

  absl::CommandLineFlag* flag = absl::FindCommandLineFlag(name);
  if (flag == nullptr || flag->IsRetired()) {
    return false;
  }

  assert(output_info);
  FillCommandLineFlagInfo(*flag, output_info);
  return true;
}

CommandLineFlagInfo GetCommandLineFlagInfoOrDie(absl::string_view name) {
  CommandLineFlagInfo info;
  if (!GetCommandLineFlagInfo(name, &info)) {
#if GOOGLE_COMMANDLINEFLAGS_FULL_API
    LOG(FATAL) << "Flag '" << name << "' does not exist.";
#else
    LOG(FATAL) << "GOOGLE_COMMANDLINEFLAGS_FULL_API=0 in this build; "
                  "GetCommandLineFlagInfoOrDie() should not be called.";
#endif
  }
  return info;
}

void GetAllFlags(std::vector<CommandLineFlagInfo>* output_vector) {
  absl_flags::ForEachFlag([&](absl::CommandLineFlag& flag) {
    if (flag.IsRetired()) return;

    CommandLineFlagInfo fi;
    FillCommandLineFlagInfo(flag, &fi);
    output_vector->push_back(fi);
  });

  // Now sort the flags, first by filename they occur in, then alphabetically
  std::sort(output_vector->begin(), output_vector->end(),
            FilenameFlagnameLess());
}

// --------------------------------------------------------------------
// CommandlineFlagsIntoString()
// ReadFlagsFromString()
//    These are mostly-deprecated routines that stick the
//    commandline flags into a string and read them back
//    out again.  I can see a use for CommandlineFlagsIntoString,
//    for creating a flagfile
//    -- some, I think, are a poor-man's attempt at absl::FlagSaver --
//    and are included only until we can delete them from callers.
//    Note they don't save --flagfile flags (though they do save
//    the result of having called the flagfile, of course).
// --------------------------------------------------------------------

static std::string TheseCommandlineFlagsIntoString(
    absl::Span<const CommandLineFlagInfo> flags) {
  std::string retval;
  for (const CommandLineFlagInfo& flag : flags) {
    absl::StrAppend(&retval, "--", flag.name, "=", flag.current_value, "\n");
  }
  return retval;
}

std::string CommandlineFlagsIntoString() {
  std::vector<CommandLineFlagInfo> sorted_flags;
  GetAllFlags(&sorted_flags);
  return TheseCommandlineFlagsIntoString(sorted_flags);
}

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
// RegisterFlagValidator()
//    RegisterFlagValidator() is the function that clients use to
//    'decorate' a flag with a validation function.  Once this is
//    done, every time the flag is set (including when the flag
//    is parsed from argv), the validator-function is called.
//       These functions return true if the validator was added
//    successfully, or false if not: the flag already has a validator,
//    (only one allowed per flag), the 1st arg isn't a flag, etc.
//       This function is not thread-safe.
// --------------------------------------------------------------------

bool RegisterFlagValidator(const bool* flag,
                           bool (*validate_fn)(const char*, bool),
                           absl::SourceLocation loc) {
  return AddFlagValidator(
      flag, reinterpret_cast<base::FlagValidator>(validate_fn), loc);
}
bool RegisterFlagValidator(const int32_t* flag,
                           bool (*validate_fn)(const char*, int32_t),
                           absl::SourceLocation loc) {
  return AddFlagValidator(
      flag, reinterpret_cast<base::FlagValidator>(validate_fn), loc);
}
bool RegisterFlagValidator(const int64_t* flag,
                           bool (*validate_fn)(const char*, int64_t),
                           absl::SourceLocation loc) {
  return AddFlagValidator(
      flag, reinterpret_cast<base::FlagValidator>(validate_fn), loc);
}
bool RegisterFlagValidator(const uint64_t* flag,
                           bool (*validate_fn)(const char*, uint64_t),
                           absl::SourceLocation loc) {
  return AddFlagValidator(
      flag, reinterpret_cast<base::FlagValidator>(validate_fn), loc);
}
bool RegisterFlagValidator(const double* flag,
                           bool (*validate_fn)(const char*, double),
                           absl::SourceLocation loc) {
  return AddFlagValidator(
      flag, reinterpret_cast<base::FlagValidator>(validate_fn), loc);
}
bool RegisterFlagValidator(const std::string* flag,
                           bool (*validate_fn)(const char*, const std::string&),
                           absl::SourceLocation loc) {
  return AddFlagValidator(
      flag, reinterpret_cast<base::FlagValidator>(validate_fn), loc);
}

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
