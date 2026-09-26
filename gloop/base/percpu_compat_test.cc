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

#include "absl/base/config.h"
#include "absl/log/log.h"
#include "gloop/base/percpu.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace base {
namespace subtle {
namespace percpu {

namespace {

using testing::Eq;
using testing::Ge;
using testing::Ne;

TEST(PercpuCompatTest, GetCurrentCpu) {
#ifdef ABSL_HAVE_SCHED_GETCPU
  ASSERT_THAT(sched_getcpu(), Ge(0));
  EXPECT_THAT(GetCurrentCpu(), Ge(0)) << "sched_getcpu() = " << sched_getcpu();
#else   // ABSL_HAVE_SCHED_GETCPU
  EXPECT_THAT(GetCurrentCpu(), Eq(-1));
#endif  // ABSL_HAVE_SCHED_GETCPU
}

TEST(PercpuCompatTest, GetCurrentVirtualFlatCpu) {
  if (base::subtle::percpu::IsFast()) {
    if (base::subtle::percpu::UsingRseqVirtualCpus()) {
      EXPECT_THAT(GetCurrentVirtualFlatCpu(), Ge(0));
    } else {
      EXPECT_THAT(GetCurrentVirtualFlatCpu(), Eq(GetCurrentCpu()));
    }
  } else {
#ifdef ABSL_HAVE_SCHED_GETCPU
    EXPECT_THAT(GetCurrentVirtualFlatCpu(), Ge(0));
#else   // ABSL_HAVE_SCHED_GETCPU
    EXPECT_THAT(GetCurrentVirtualFlatCpu(), Eq(-1));
#endif  // ABSL_HAVE_SCHED_GETCPU
  }
}

TEST(PercpuCompatTest, IsFastAndCpuId) {
#if PERCPU_USE_RSEQ
  if (IsFast()) {
    LOG(INFO) << "PERCPU_USE_RSEQ=True,Fast";
    EXPECT_TRUE(IsFastNoInit());
    EXPECT_THAT(RseqCpuId(), Ge(0));
    EXPECT_THAT(RseqVirtualFlatCpuId(), Ge(0));
  } else {
    LOG(INFO) << "PERCPU_USE_RSEQ=True,Slow";
    EXPECT_FALSE(IsFastNoInit());
    EXPECT_THAT(RseqCpuId(), Eq(kCpuIdUnsupported));
    EXPECT_THAT(RseqVirtualFlatCpuId(), Eq(kCpuIdUnsupported));
  }
#else
  LOG(INFO) << "PERCPU_USE_RSEQ=False";
  EXPECT_FALSE(IsFast());
  EXPECT_FALSE(IsFastNoInit());
  EXPECT_THAT(RseqCpuId(), Eq(kCpuIdUnsupported));
  EXPECT_THAT(RseqVirtualFlatCpuId(), Eq(kCpuIdUnsupported));
  EXPECT_THAT(RseqAbi(), Eq(nullptr));
#endif
  LOG(INFO) << "RseqCpuId()=" << RseqCpuId();
  LOG(INFO) << "RseqVirtualFlatCpuId()=" << RseqVirtualFlatCpuId();
}

TEST(PercpuCompatTest, Handle) {
  EXPECT_THAT(NullHandle().rep, Eq(nullptr));

  Handle handle = AllocHandle();
#if __linux__
  EXPECT_THAT(handle.rep, Ne(nullptr));
#else
  EXPECT_THAT(handle.rep, Eq(nullptr));
#endif
  FreeHandle(handle);
  FreeHandle(NullHandle());
}

}  // namespace
}  // namespace percpu
}  // namespace subtle
}  // namespace base
