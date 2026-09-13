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

#include "gloop/base/cancellation_coloring.h"

#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gtest/gtest.h"

namespace base::internal {
namespace {

// The initial color for the main thread, and for newly-created threads, should
// be kUnknown.
TEST(CancellationColoring, InitialColor) {
  // Main thread
  EXPECT_EQ(CancellationColor::kUnknown, GetActiveCancellationColor());

  // Newly-created thread
  {
    ClosureThread t{
        thread::Options().set_joinable(true),
        "",
        [] {
          EXPECT_EQ(CancellationColor::kUnknown, GetActiveCancellationColor());
        },
    };

    t.Start();
    t.Join();
  }
}

// WithCancellationColor should change the result of GetActiveCancellationColor,
// resetting it again once it's  destroyed.
TEST(CancellationColoring, SetAndRestoreColor) {
  ASSERT_EQ(CancellationColor::kUnknown, GetActiveCancellationColor());

  {
    const WithCancellationColor wcc(CancellationColor::kCoroutine);
#ifdef NDEBUG
    EXPECT_EQ(CancellationColor::kUnknown, GetActiveCancellationColor());
#else   // NDEBUG
    EXPECT_EQ(CancellationColor::kCoroutine, GetActiveCancellationColor());
#endif  // NDEBUG
  }

  EXPECT_EQ(CancellationColor::kUnknown, GetActiveCancellationColor());
}

// It should be fine to create multiple nesting WithCancellationColor objects.
// They should pop colors like a stack on their way out.
TEST(CancellationColoring, NestingColors) {
  ASSERT_EQ(CancellationColor::kUnknown, GetActiveCancellationColor());

  {
    const WithCancellationColor wcc1(CancellationColor::kCoroutine);
#ifdef NDEBUG
    EXPECT_EQ(CancellationColor::kUnknown, GetActiveCancellationColor());
#else   // NDEBUG
    EXPECT_EQ(CancellationColor::kCoroutine, GetActiveCancellationColor());
#endif  // NDEBUG

    {
      const WithCancellationColor wcc2(CancellationColor::kFibers);
#ifdef NDEBUG
      EXPECT_EQ(CancellationColor::kUnknown, GetActiveCancellationColor());
#else   // NDEBUG
      EXPECT_EQ(CancellationColor::kFibers, GetActiveCancellationColor());
#endif  // NDEBUG
    }

#ifdef NDEBUG
    EXPECT_EQ(CancellationColor::kUnknown, GetActiveCancellationColor());
#else   // NDEBUG
    EXPECT_EQ(CancellationColor::kCoroutine, GetActiveCancellationColor());
#endif  // NDEBUG
  }

  EXPECT_EQ(CancellationColor::kUnknown, GetActiveCancellationColor());
}

}  // namespace
}  // namespace base::internal
