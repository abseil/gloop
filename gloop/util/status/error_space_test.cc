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

#include "gloop/util/status/error_space.h"

#include <string>

#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "gloop/util/status/status.h"
#include "gtest/gtest.h"

namespace util {

class ErrorSpaceTestPeer {
 public:
  static void Register(absl::string_view name, const ErrorSpace* space) {
    ErrorSpace::Register(name, space);
  }

  static void Register(const ErrorSpace* (*factory)()) {
    ErrorSpace::Register(factory);
  }
};

}  // namespace util

namespace {

namespace s1 {
enum Enum1 { kOk, kNotOk };

struct Space1 : util::ErrorSpaceImpl<Space1> {
  static absl::string_view space_name() { return "Space1"; }
  static std::string code_to_string(int code) { return absl::StrCat(code); }
  static absl::StatusCode canonical_code(int code) {
    return absl::StatusCode::kUnknown;
  }
};

const util::ErrorSpace* GetErrorSpace(util::ErrorSpaceAdlTag<Enum1>) {
  return Space1::Get();
}
}  // namespace s1

namespace s2 {
enum Enum2 { kOk, kNotOk };
}  // namespace s2

namespace s3 {
enum Enum3 { kOk, kNotOk };
struct Space3 : util::ErrorSpaceImpl<Space3> {
  static absl::string_view space_name() { return "Space3"; }
  static std::string code_to_string(int code) { return absl::StrCat(code); }
  static absl::StatusCode canonical_code(int) {
    return absl::StatusCode::kFailedPrecondition;
  }
};
const util::ErrorSpace* GetErrorSpace(util::ErrorSpaceAdlTag<Enum3>) {
  return Space3::Get();
}
}  // namespace s3

TEST(ErrorSpace, CanonicalCodeTakesStatus) {
  absl::Status status = util::MakeStatus(s1::kNotOk, "msg");
  EXPECT_EQ(status.code(), absl::StatusCode::kUnknown);
}

TEST(ErrorSpace, CanonicalCodeTakesInt) {
  absl::Status status = util::MakeStatus(s3::kNotOk, "msg");
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
}

TEST(GetSpace, Adl) {
  EXPECT_EQ(s1::Space1::Get(), util::GetErrorSpaceForEnum(s1::kNotOk));
  EXPECT_TRUE(util::EnumHasErrorSpace<s1::Enum1>::value);
  EXPECT_FALSE(util::EnumHasErrorSpace<s2::Enum2>::value);
}

static void BM_Code_IsValid(benchmark::State& state) {
  int code = 1;
  for (auto _ : state) {
    bool is_valid = util::error::Code_IsValid(code);
    benchmark::DoNotOptimize(is_valid);
    code = !code;
  }
}
BENCHMARK(BM_Code_IsValid);

}  // namespace
