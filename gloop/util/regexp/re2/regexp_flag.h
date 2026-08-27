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

#ifndef THIRD_PARTY_GLOOP_UTIL_REGEXP_RE2_REGEXP_FLAG_H_
#define THIRD_PARTY_GLOOP_UTIL_REGEXP_RE2_REGEXP_FLAG_H_

#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "re2/re2.h"

// RegexpFlag
// ----------
//
// RegexpFlag allows you to define a flag whose value is a regular expression
// without compiling a temporary RE2 object for each match call. The default
// RE2 options are used; for custom RE2 options, see RegexpFlagWithOptions<>.
//
// Like a smart pointer, a RegexpFlag provides operator*(), operator->() and
// get() for access to the underlying RE2 object and, of course, must outlive
// any pointers/references to the underlying RE2 object.
//
// The default value must be set using OrDie() so that an invalid regular
// expression cannot go unnoticed.
//
// Example usage:
//
// ABSL_FLAG(re2::RegexpFlag, rowkey_regexp, re2::RegexpFlag::OrDie(""),
//           "If not empty, only rowkeys fully matching the regexp will "
//           "be processed.");
//
// bool ShouldProcess(const string& rowkey) {
//   auto regexp = absl::GetFlag(FLAGS_rowkey_regexp);
//   if (regexp->pattern().empty()) {
//     return true;
//   }
//   return RE2::FullMatch(rowkey, *regexp);
// }
//
// At the risk of restating the obvious, under no circumstances whatsoever
// should you write something like this:
//
//   const auto& regexp = *absl::GetFlag(FLAGS_rowkey_regexp);
// or
//   const auto* regexp = absl::GetFlag(FLAGS_rowkey_regexp).get();
//
// That would immediately leave you with a dangling reference!
//
// A RegexpFlag defined in one file can be declared in other files and its
// value can be changed. You might do this during process initialisation or
// in test code.
//
// Example usage:
//
// ABSL_DECLARE_FLAG(re2::RegexpFlag, rowkey_regexp);
//
// ...
//   absl::SetFlag(&FLAGS_rowkey_regexp, re2::RegexpFlag::OrDie("foo|bar"));
// ...
//
// RegexpListFlag
// --------------
//
// RegexpListFlag allows you to define a flag whose value is a comma-separated
// list of regular expressions.
// As a limitation, those regular expressions cannot contain literal (or
// escaped) commas: use the hex escape sequence \x2C to match a comma.
//
// Just like RegexpFlag:
//  - RegexpListFlag allows specifying options as a template parameter.
//  - The default value must be set using RegexpListFlag::OrDie() so that an
//    invalid regular expression cannot go unnoticed (empty default regexps can
//    use `{}` for redability).
//
// The semantics are aligned with the semantics of `std::vector<std::string>`:
//   - an empty flag corresponds to an empty list of regexps, not a list of a
//     single empty regexp;
//   - empty regexps are allowed ("a,,b" yields three regexps).
//
// RegexpFlagWithOptions<>
// -----------------------
//
// RegexpFlagWithOptions<> takes a single template parameter: a class whose
// operator()() returns the custom RE2 options that you desire.
//
// Example usage:
//
// struct CaseInsensitiveOptions {
//   RE2::Options operator()() const {
//     RE2::Options options;
//     options.set_case_sensitive(false);
//     return options;
//   }
// };
//
// ABSL_FLAG(re2::RegexpFlagWithOptions<CaseInsensitiveOptions>,
//           rowkey_regexp,
//           re2::RegexpFlagWithOptions<CaseInsensitiveOptions>::OrDie(""),
//           "If not empty, only rowkeys fully matching the regexp will "
//           "be processed. Note that matching is case-insensitive.");
//
// Unsurprisingly, RegexpFlag is a specialisation of RegexpFlagWithOptions<>,
// so the guidance offered for the former applies to all uses of the latter.
namespace re2 {

template <typename Options>
class RegexpFlagWithOptions {
 public:
  const RE2& operator*() const { return *get(); }
  const RE2* operator->() const { return get(); }
  const RE2* get() const { return re_.get(); }

  static RegexpFlagWithOptions OrDie(absl::string_view pattern) {
    RegexpFlagWithOptions flag;
    std::string error;
    CHECK(AbslParseFlag(pattern, &flag, &error)) << error;
    return flag;
  }

 private:
  friend bool AbslParseFlag(absl::string_view pattern,
                            RegexpFlagWithOptions* flag, std::string* error) {
    auto re = std::make_shared<const RE2>(pattern, Options()());
    if (!re->ok()) {
      *error = re->error();
      return false;
    }
    flag->re_ = std::move(re);
    return true;
  }

  friend std::string AbslUnparseFlag(const RegexpFlagWithOptions& flag) {
    // This code runs during static initialization so we have to be careful and
    // not LOG or CHECK if we were initialized with the default constructor. So
    // instead we'll unparse as an empty string.
    if (flag.re_ == nullptr) return "";
    return flag->pattern();
  }

  std::shared_ptr<const RE2> re_;
};

template <typename Options>
class RegexpListFlagWithOptions {
 public:
  using value_type = RegexpFlagWithOptions<Options>;

  static RegexpListFlagWithOptions OrDie(
      std::initializer_list<absl::string_view> patterns) {
    RegexpListFlagWithOptions flag;
    flag.regexps_.reserve(patterns.size());
    for (auto pattern : patterns) {
      // Ensure the invariant that the patterns do not contain commas. Another
      // option would be to escape them ourselves (but then the Parse/Unparse
      // roundtrip would no longer be a syntactic no-op).
      CHECK(!absl::StrContains(pattern, ','))
          << "RegexpListFlag does not support commas in the patterns, escape "
             "the commas using \x2c: "
          << pattern;
      flag.regexps_.push_back(value_type::OrDie(pattern));
    }
    return flag;
  }

  auto begin() const { return regexps_.begin(); }
  auto end() const { return regexps_.end(); }

 private:
  friend bool AbslParseFlag(absl::string_view text,
                            RegexpListFlagWithOptions* flag,
                            std::string* error) {
    std::vector<value_type> regexps;
    // Note: an empty string corresponds to an empty list of regexps, not a list
    // of a single empty regexp.
    if (!text.empty()) {
      for (const auto part : absl::StrSplit(text, ',', absl::AllowEmpty())) {
        if (!AbslParseFlag(part, &regexps.emplace_back(), error)) {
          return false;
        }
      }
    }
    flag->regexps_ = std::move(regexps);
    return true;
  }

  friend std::string AbslUnparseFlag(const RegexpListFlagWithOptions& flag) {
    return absl::StrJoin(flag.regexps_, ",",
                         [](std::string* out, const auto& re) {
                           absl::StrAppend(out, re->pattern());
                         });
  }

 private:
  // Invariant: All those regexps are non-null and their patterns do not contain
  // comma literals.
  std::vector<value_type> regexps_;
};

namespace regexp_flag_internal {

struct DefaultOptions {
  RE2::Options operator()() const {
    RE2::Options options;
    return options;
  }
};

}  // namespace regexp_flag_internal

using RegexpFlag = RegexpFlagWithOptions<regexp_flag_internal::DefaultOptions>;
using RegexpListFlag =
    RegexpListFlagWithOptions<regexp_flag_internal::DefaultOptions>;

}  // namespace re2

#endif  // THIRD_PARTY_GLOOP_UTIL_REGEXP_RE2_REGEXP_FLAG_H_
