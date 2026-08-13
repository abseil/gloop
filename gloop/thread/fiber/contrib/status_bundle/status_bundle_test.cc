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

#include "gloop/thread/fiber/contrib/status_bundle/status_bundle.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "benchmark/benchmark.h"
#include "gloop/base/context.h"
#include "gloop/base/tracecontext.h"
#include "gloop/base/tracer.h"
#include "gloop/thread/fiber/channel.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/select.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using absl_testing::IsOk;
using testing::AnyOf;

namespace thread {

////////////////////////////////////////////////////////////////////////
// Helpers
////////////////////////////////////////////////////////////////////////

using Operation = std::function<absl::Status()>;

static void WaitForCancellation() { thread::Select({thread::OnCancel()}); }

static void CreateAndJoinBundle(absl::Status* status,
                                absl::Span<const Operation> ops) {
  StatusBundle b;
  for (const auto& op : ops) {
    b.Add(op);
  }

  *status = b.Join();
}

////////////////////////////////////////////////////////////////////////
// Functionality
////////////////////////////////////////////////////////////////////////

// It should be fine to create and join a bundle without adding any operation.
// The result should be OK.
TEST(FunctionalityTest, EmptyBundle) {
  StatusBundle b;

  EXPECT_THAT(b.Join(), IsOk());
  EXPECT_THAT(b.Join(), IsOk());
}

// Providing options when creating the bundle shouldn't change the fact that
// it's okay to create and join it without adding any operations.
TEST(FunctionalityTest, EmptyBundleWithOptions) {
  const std::string parent_name(thread::Fiber::Current()->options().name());
  const std::string child_name = "taco_truck";
  ASSERT_NE(parent_name, child_name);

  StatusBundle b(thread::FiberOptions().SetInternedName(child_name));
  EXPECT_THAT(b.Join(), IsOk());
  EXPECT_EQ(parent_name, thread::Fiber::Current()->options().name());
}

// If there is one op and it succeeds, Join should return OK.
TEST(FunctionalityTest, SingleOp_Success) {
  StatusBundle b;
  b.Add([] { return absl::OkStatus(); });

  EXPECT_THAT(b.Join(), IsOk());
  EXPECT_THAT(b.Join(), IsOk());
}

// As above, but with fiber options.
TEST(FunctionalityTest, SingleOpWithOptionsSuccess) {
  const std::string parent_name(thread::Fiber::Current()->options().name());
  const std::string child_name = "burrito_truck";
  ASSERT_NE(parent_name, child_name);

  StatusBundle b(thread::FiberOptions().SetInternedName(child_name));
  b.Add([&] {
    EXPECT_EQ(child_name, thread::Fiber::Current()->options().name());
    return absl::OkStatus();
  });
  EXPECT_THAT(b.Join(), IsOk());
  EXPECT_EQ(parent_name, thread::Fiber::Current()->options().name());
}

// If there is one operation and it fails, its error should be plumbed back from
// Join.
TEST(FunctionalityTest, SingleOp_Error) {
  const absl::Status err_0(absl::StatusCode::kNotFound, "taco");

  StatusBundle b;
  b.Add([err_0] { return err_0; });

  EXPECT_EQ(err_0, b.Join());
  EXPECT_EQ(err_0, b.Join());
}

// Cancellation of the fiber calling Join should be plumbed through to the
// operation(s) it's waiting on.
TEST(FunctionalityTest, SingleOp_ParasiticCancellation) {
  const std::vector<Operation> ops = {
      [] {
        WaitForCancellation();
        return absl::OkStatus();
      },
  };

  // Start a bundle on another fiber. It shouldn't yet finish.
  absl::Status s;
  thread::Fiber f([&s, ops] { CreateAndJoinBundle(&s, ops); });

  EXPECT_EQ(-1, thread::SelectUntil(absl::Now() + absl::Milliseconds(2),
                                    {f.OnJoinable()}));

  // Cancel the fiber. Now it should finish.
  f.Cancel();
  f.Join();
}

// StatusBundle::Cancel should cancel the outstanding operation(s).
TEST(FunctionalityTest, SingleOp_ObservesExplicitCancellation) {
  StatusBundle bundle;

  // Add an op that waits for cancellation.
  absl::Notification cancelled;
  bundle.Add([&cancelled] {
    WaitForCancellation();
    cancelled.Notify();
    return absl::OkStatus();
  });

  // The cancellation should not yet have happened.
  EXPECT_FALSE(cancelled.WaitForNotificationWithTimeout(absl::Milliseconds(2)));
  EXPECT_EQ(
      thread::SelectUntil(absl::Now() + absl::Seconds(1), {bundle.OnCancel()}),
      -1);

  // Cancel the bundle. Now the cancellation should happen.
  bundle.Cancel();
  cancelled.WaitForNotification();
  bundle.Join().IgnoreError();
  EXPECT_TRUE(bundle.Cancelled());
  EXPECT_EQ(thread::Select({bundle.OnCancel()}), 0);

  // The calling fiber should be unaffected.
  EXPECT_FALSE(thread::Cancelled());
}

// If all operations succeed, Join should return OK.
TEST(FunctionalityTest, FewOps_Success) {
  StatusBundle b;
  b.Add([] { return absl::OkStatus(); });
  b.Add([] { return absl::OkStatus(); });
  b.Add([] { return absl::OkStatus(); });

  EXPECT_THAT(b.Join(), IsOk());
  EXPECT_THAT(b.Join(), IsOk());
}

// If multiple operations concurrently return distinct errors, one of those
// errors should be plumbed back by Join. It doesn't matter which, but the
// answer should be consistent.
TEST(FunctionalityTest, FewOps_UnorderedErrors) {
  const absl::Status err_0(absl::StatusCode::kNotFound, "taco");
  const absl::Status err_1(absl::StatusCode::kNotFound, "burrito");
  const absl::Status err_2(absl::StatusCode::kNotFound, "enchilada");

  StatusBundle b;
  b.Add([] { return absl::OkStatus(); });
  b.Add([err_0] {
    absl::SleepFor(absl::Microseconds(100));
    return err_0;
  });
  b.Add([] { return absl::OkStatus(); });
  b.Add([err_1] {
    absl::SleepFor(absl::Microseconds(100));
    return err_1;
  });
  b.Add([] { return absl::OkStatus(); });
  b.Add([err_2] {
    absl::SleepFor(absl::Microseconds(100));
    return err_2;
  });
  b.Add([] { return absl::OkStatus(); });

  {
    const absl::Status status = b.Join();
    EXPECT_THAT(status, AnyOf(err_0, err_1, err_2));
    EXPECT_EQ(status, b.Join());
  }
}

// If only one of multiple operations returns an error, that error should be
// returned by Join, even with other operations concurrently succeeding.
TEST(FunctionalityTest, FewOps_OneError_OthersDontWait) {
  const absl::Status err_0(absl::StatusCode::kNotFound, "taco");

  StatusBundle b;
  b.Add([] { return absl::OkStatus(); });
  b.Add([err_0] {
    absl::SleepFor(absl::Microseconds(100));
    return err_0;
  });
  b.Add([] { return absl::OkStatus(); });

  EXPECT_EQ(err_0, b.Join());
  EXPECT_EQ(err_0, b.Join());
}

// If one operation returns an error, the other outstanding operations should be
// cancelled. Their follow-on errors should be ignored, since they are caused by
// the cancellation.
TEST(FunctionalityTest, FewOps_OneError_OthersWaitForCancellation) {
  const absl::Status err_0(absl::StatusCode::kNotFound, "taco");

  StatusBundle b;
  b.Add([] {
    WaitForCancellation();
    return absl::OkStatus();
  });
  b.Add([] {
    WaitForCancellation();
    return absl::CancelledError();
  });
  b.Add([err_0] {
    absl::SleepFor(absl::Microseconds(100));
    return err_0;
  });
  b.Add([] {
    WaitForCancellation();
    return absl::CancelledError();
  });
  b.Add([] {
    WaitForCancellation();
    return absl::OkStatus();
  });

  EXPECT_EQ(err_0, b.Join());
  EXPECT_EQ(err_0, b.Join());
}

// Cancellation of the fiber calling Join should be plumbed through to the
// operations it's waiting on.
TEST(FunctionalityTest, FewOps_ObserveFiberCancellation) {
  const std::vector<Operation> ops = {
      [] {
        WaitForCancellation();
        return absl::OkStatus();
      },
      [] {
        WaitForCancellation();
        return absl::OkStatus();
      },
      [] {
        WaitForCancellation();
        return absl::OkStatus();
      },
  };

  // Start a bundle on another fiber. It shouldn't yet finish.
  absl::Status s;
  thread::Fiber f([&s, ops] { CreateAndJoinBundle(&s, ops); });

  EXPECT_EQ(-1, thread::SelectUntil(absl::Now() + absl::Milliseconds(2),
                                    {f.OnJoinable()}));

  // Cancel the fiber. Now it should finish.
  f.Cancel();
  f.Join();
}

// StatusBundle::Cancel should cancel the outstanding operations.
TEST(FunctionalityTest, FewOps_ObserveExplicitCancellation) {
  StatusBundle bundle;

  // Add several ops that wait for cancellation.
  absl::Notification cancelled;
  for (size_t i = 0; i < 3; ++i) {
    if (i == 1) {
      bundle.Add([&cancelled] {
        WaitForCancellation();
        cancelled.Notify();
        return absl::OkStatus();
      });
    } else {
      bundle.Add([] {
        WaitForCancellation();
        return absl::OkStatus();
      });
    }
  }

  // The cancellation should not yet have happened.
  EXPECT_FALSE(cancelled.WaitForNotificationWithTimeout(absl::Milliseconds(2)));

  // Cancel the bundle. Now the cancellation should happen.
  bundle.Cancel();
  EXPECT_TRUE(bundle.Cancelled());
  EXPECT_EQ(thread::Select({bundle.OnCancel()}), 0);
  cancelled.WaitForNotification();
  bundle.Join().IgnoreError();

  // The calling fiber should be unaffected.
  EXPECT_FALSE(thread::Cancelled());
}

// If an operation returns an error, it should cause cancellation of both
// currently outstanding and future operations.
TEST(FunctionalityTest, FewOps_PreviousErrorCancellation_NewOpsObserve) {
  const absl::Status err(absl::StatusCode::kNotFound, "taco");

  // Start a bundle that with an op that returns an error, waiting for the error
  // to be observed.
  absl::Notification cancelled;

  StatusBundle b;
  b.Add([] { return absl::OkStatus(); });
  b.Add([err] { return err; });
  b.Add([&cancelled] {
    WaitForCancellation();
    cancelled.Notify();
    return absl::OkStatus();
  });

  cancelled.WaitForNotification();

  // Add new operations. They should see the cancellation.
  b.Add([] {
    WaitForCancellation();
    return absl::OkStatus();
  });
  b.Add([] {
    WaitForCancellation();
    return absl::OkStatus();
  });
  b.Add([] {
    WaitForCancellation();
    return absl::OkStatus();
  });

  EXPECT_TRUE(b.Cancelled());
  EXPECT_EQ(thread::Select({b.OnCancel()}), 0);
  ASSERT_EQ(err, b.Join());
}

// StatusBundle::Cancel should cause cancellation of both currently outstanding
// and future operations.
TEST(FunctionalityTest, FewOps_PreviousExplicitCancellation_NewOpsObserve) {
  const absl::Status err(absl::StatusCode::kNotFound, "taco");

  // Start a bundle with a few ops, including one that waits for cancellation.
  StatusBundle b;
  absl::Notification cancelled;
  b.Add([] { return absl::OkStatus(); });
  b.Add([err, &cancelled] {
    WaitForCancellation();
    cancelled.Notify();
    return err;
  });
  b.Add([] { return absl::OkStatus(); });

  // Explicitly cancel the bundle, and wait for this to be noticed.
  b.Cancel();
  EXPECT_TRUE(b.Cancelled());
  EXPECT_EQ(thread::Select({b.OnCancel()}), 0);
  cancelled.WaitForNotification();

  // Add new operations. They should see the cancellation.
  b.Add([] {
    WaitForCancellation();
    return absl::OkStatus();
  });
  b.Add([] {
    WaitForCancellation();
    return absl::OkStatus();
  });
  b.Add([] {
    WaitForCancellation();
    return absl::OkStatus();
  });

  EXPECT_TRUE(b.Cancelled());
  EXPECT_EQ(thread::Select({b.OnCancel()}), 0);
  ASSERT_EQ(err, b.Join());
}

// The bundle should be able to handle a large number of operations, all
// concurrently returning OK. The result should be OK.
TEST(FunctionalityTest, ManyOps_Success) {
  StatusBundle b;
  for (size_t i = 0; i < 1e3; ++i) {
    b.Add([] { return absl::OkStatus(); });
  }

  EXPECT_THAT(b.Join(), IsOk());
  EXPECT_THAT(b.Join(), IsOk());
}

// The bundle should be able to handle a large number of operations,
// concurrently returning OK and a set of errors. The result should be one of
// the errors.
TEST(FunctionalityTest, ManyOps_UnorderedErrors) {
  const std::vector<absl::Status> errs = {
      absl::Status(absl::StatusCode::kNotFound, "taco"),
      absl::Status(absl::StatusCode::kNotFound, "burrito"),
      absl::Status(absl::StatusCode::kNotFound, "queso"),
  };

  StatusBundle b;
  for (size_t i = 0; i < 1e3; ++i) {
    b.Add([] { return absl::OkStatus(); });
    b.Add([i, &errs] { return errs[i % errs.size()]; });
  }

  {
    const absl::Status status = b.Join();
    EXPECT_THAT(status, AnyOf(errs[0], errs[1], errs[2]));
    EXPECT_EQ(status, b.Join());
  }
}

// The bundle should be able to handle a large number of operations, with
// exactly one returning an error (before most of the others are created). The
// result should be that error.
TEST(FunctionalityTest, ManyOps_OneError_Early_OthersDontWait) {
  const absl::Status err(absl::StatusCode::kNotFound, "taco");

  StatusBundle b;
  b.Add([err] {
    absl::SleepFor(absl::Milliseconds(2));
    return err;
  });
  for (size_t i = 0; i < 1e3; ++i) {
    b.Add([] { return absl::OkStatus(); });
  }

  EXPECT_EQ(err, b.Join());
  EXPECT_EQ(err, b.Join());
}

// The bundle should be able to handle a large number of operations, with
// exactly one returning an error (in the middle of the pack). The result should
// be that error.
TEST(FunctionalityTest, ManyOps_OneError_Middle_OthersDontWait) {
  const absl::Status err(absl::StatusCode::kNotFound, "taco");

  StatusBundle b;
  for (size_t i = 0; i < 1e3; ++i) {
    if (i == 500) {
      b.Add([err] {
        absl::SleepFor(absl::Milliseconds(2));
        return err;
      });
      continue;
    }

    b.Add([] { return absl::OkStatus(); });
  }

  EXPECT_EQ(err, b.Join());
  EXPECT_EQ(err, b.Join());
}

// The bundle should be able to handle a large number of operations, with
// exactly one returning an error (near the end). The result should be that
// error.
TEST(FunctionalityTest, ManyOps_OneError_Late_OthersDontWait) {
  const absl::Status err(absl::StatusCode::kNotFound, "taco");

  StatusBundle b;
  for (size_t i = 0; i < 1e3; ++i) {
    if (i == 950) {
      b.Add([err] {
        absl::SleepFor(absl::Milliseconds(2));
        return err;
      });
      continue;
    }

    b.Add([] { return absl::OkStatus(); });
  }

  EXPECT_EQ(err, b.Join());
  EXPECT_EQ(err, b.Join());
}

// The bundle should be able to handle a large number of operations, with
// exactly one returning an error (before most of the others are created). The
// other operations should be cancelled, and the result should be that error.
TEST(FunctionalityTest, ManyOps_OneError_Early_OthersWaitForCancellation) {
  const absl::Status err(absl::StatusCode::kNotFound, "taco");

  StatusBundle b;
  b.Add([err] {
    absl::SleepFor(absl::Milliseconds(2));
    return err;
  });
  for (size_t i = 0; i < 1e3; ++i) {
    b.Add([] {
      WaitForCancellation();
      return absl::OkStatus();
    });
  }

  EXPECT_EQ(err, b.Join());
  EXPECT_EQ(err, b.Join());
}

// The bundle should be able to handle a large number of operations, with
// exactly one returning an error (in the middle of the pack). The other
// operations should be cancelled, and the result should be that error.
TEST(FunctionalityTest, ManyOps_OneError_Middle_OthersWaitForCancellation) {
  const absl::Status err(absl::StatusCode::kNotFound, "taco");

  StatusBundle b;
  for (size_t i = 0; i < 1e3; ++i) {
    if (i == 500) {
      b.Add([err] {
        absl::SleepFor(absl::Milliseconds(2));
        return err;
      });
    }

    b.Add([] {
      WaitForCancellation();
      return absl::OkStatus();
    });
  }

  EXPECT_EQ(err, b.Join());
  EXPECT_EQ(err, b.Join());
}

// The bundle should be able to handle a large number of operations, with
// exactly one returning an error (near the end). The other operations should be
// cancelled, and the result should be that error.
TEST(FunctionalityTest, ManyOps_OneError_Late_OthersWaitForCancellation) {
  const absl::Status err(absl::StatusCode::kNotFound, "taco");

  StatusBundle b;
  for (size_t i = 0; i < 1e3; ++i) {
    if (i == 900) {
      b.Add([err] {
        absl::SleepFor(absl::Milliseconds(2));
        return err;
      });
    }

    b.Add([] {
      WaitForCancellation();
      return absl::OkStatus();
    });
  }

  EXPECT_EQ(err, b.Join());
  EXPECT_EQ(err, b.Join());
}

// Cancellation of the fiber calling Join should be plumbed through to the
// operations it's waiting on, even when there are many.
TEST(FunctionalityTest, ManyOps_ObserveFiberCancellation) {
  const Operation op = [] {
    WaitForCancellation();
    return absl::OkStatus();
  };

  const std::vector<Operation> ops(1e3, op);

  // Start a bundle on another fiber. It shouldn't yet finish.
  absl::Status s;
  thread::Fiber f([&s, ops] { CreateAndJoinBundle(&s, ops); });

  EXPECT_EQ(-1, thread::SelectUntil(absl::Now() + absl::Milliseconds(2),
                                    {f.OnJoinable()}));

  // Cancel the fiber. Now it should finish.
  f.Cancel();
  f.Join();
}

// StatusBundle::Cancel should cancel the outstanding operations, even when
// there are many.
TEST(FunctionalityTest, ManyOps_ObserveExplicitCancellation) {
  StatusBundle bundle;

  // Add many ops that wait for cancellation.
  absl::Notification cancelled;
  for (size_t i = 0; i < 1000; ++i) {
    if (i == 500) {
      bundle.Add([&cancelled] {
        WaitForCancellation();
        cancelled.Notify();
        return absl::OkStatus();
      });
    } else {
      bundle.Add([] {
        WaitForCancellation();
        return absl::OkStatus();
      });
    }
  }

  // The cancellation should not yet have happened.
  EXPECT_FALSE(cancelled.WaitForNotificationWithTimeout(absl::Milliseconds(2)));

  // Cancel the bundle. Now the cancellation should happen.
  bundle.Cancel();
  cancelled.WaitForNotification();
  bundle.Join().IgnoreError();

  // The calling fiber should be unaffected.
  EXPECT_FALSE(thread::Cancelled());
}

// If an operation returns an error (near the beginning of a set of many), it
// should cause cancellation of both currently outstanding and future
// operations.
TEST(FunctionalityTest, ManyOps_PreviousErrorCancellation_Early_NewOpsObserve) {
  const absl::Status err(absl::StatusCode::kNotFound, "taco");

  // Start a bundle that with an op that returns an error, waiting for the error
  // to be observed.
  absl::Notification cancelled;

  StatusBundle b;
  b.Add([] { return absl::OkStatus(); });
  b.Add([err] { return err; });
  b.Add([&cancelled] {
    WaitForCancellation();
    cancelled.Notify();
    return absl::OkStatus();
  });

  cancelled.WaitForNotification();

  // Add new operations. They should all see the cancellation.
  for (size_t i = 0; i < 1e3; ++i) {
    b.Add([] {
      WaitForCancellation();
      return absl::OkStatus();
    });
  }

  ASSERT_TRUE(b.Cancelled());
  ASSERT_EQ(thread::Select({b.OnCancel()}), 0);
  ASSERT_EQ(err, b.Join());
}

// If an operation returns an error (near the middle of a set of many), it
// should cause cancellation of both currently outstanding and future
// operations.
TEST(FunctionalityTest,
     ManyOps_PreviousErrorCancellation_Middle_NewOpsObserve) {
  const absl::Status err(absl::StatusCode::kNotFound, "taco");

  // Start a bundle that with an intermediate number of ops.
  StatusBundle b;
  for (size_t i = 0; i < 500; ++i) {
    b.Add([] { return absl::OkStatus(); });
  }

  // Add an op that returns an error and wait for it to be observed by the
  // bundle.
  absl::Notification cancelled;
  b.Add([err] { return err; });
  b.Add([&cancelled] {
    WaitForCancellation();
    cancelled.Notify();
    return absl::OkStatus();
  });

  cancelled.WaitForNotification();

  // Add new operations. They should all see the cancellation.
  for (size_t i = 0; i < 500; ++i) {
    b.Add([] {
      WaitForCancellation();
      return absl::OkStatus();
    });
  }

  ASSERT_TRUE(b.Cancelled());
  ASSERT_EQ(thread::Select({b.OnCancel()}), 0);
  ASSERT_EQ(err, b.Join());
}

// If an operation returns an error (near the end of a set of many), it should
// cause cancellation of both currently outstanding and future operations.
TEST(FunctionalityTest, ManyOps_PreviousErrorCancellation_Late_NewOpsObserve) {
  const absl::Status err(absl::StatusCode::kNotFound, "taco");

  // Start a bundle that with a large number of ops.
  StatusBundle b;
  for (size_t i = 0; i < 1e3; ++i) {
    b.Add([] { return absl::OkStatus(); });
  }

  // Add an op that returns an error and wait for it to be observed by the
  // bundle.
  absl::Notification cancelled;
  b.Add([err] { return err; });
  b.Add([&cancelled] {
    WaitForCancellation();
    cancelled.Notify();
    return absl::OkStatus();
  });

  cancelled.WaitForNotification();

  // Add new operations. They should see the cancellation.
  b.Add([] {
    WaitForCancellation();
    return absl::OkStatus();
  });
  b.Add([] {
    WaitForCancellation();
    return absl::OkStatus();
  });

  ASSERT_TRUE(b.Cancelled());
  ASSERT_EQ(thread::Select({b.OnCancel()}), 0);
  ASSERT_EQ(err, b.Join());
}

// The deadline for an operation should come from the one active when calling
// StatusBundle::Add, not when creating the bundle.
TEST(FunctionalityTest, ContextDeadlines) {
  // Create a status bundle with a context deadline active. This shouldn't
  // affect anything about the fibers run as part of the bundle.
  std::unique_ptr<StatusBundle> b;
  {
    const base::WithDeadline wd(absl::Now() + absl::Seconds(123));
    b = std::make_unique<StatusBundle>();
  }

  // Ops added without a context deadline active should receive no deadline.
  b->Add([] {
    EXPECT_EQ(absl::InfiniteFuture(), base::CurrentContext().deadline());
    return absl::OkStatus();
  });

  // Ops added with a context deadline active should receive that one.
  {
    const absl::Time deadline = absl::Now() + absl::Seconds(456);
    const base::WithDeadline wd(deadline);

    b->Add([deadline] {
      EXPECT_EQ(deadline, base::CurrentContext().deadline());
      return absl::OkStatus();
    });
  }

  ASSERT_THAT(b->Join(), IsOk());
}

// StatusBundle::Cancelled should reflect whether the current/future operations
// in the bundle have been cancelled.
TEST(FunctionalityTest, CancelledAccessor) {
  bool cancelled = false;
  thread::Channel<bool> comms(0);

  auto fiber = thread::Fiber([&cancelled, &comms]() {
    StatusBundle b;
    bool unused;
    comms.reader()->Read(&unused);
    cancelled = b.Cancelled();
    CHECK_EQ(thread::Select({b.OnCancel()}), 0);
    (void)b.Join();
  });

  fiber.Cancel();
  comms.writer()->Write(true);
  fiber.Join();

  EXPECT_TRUE(cancelled);
}

// It should be possible to use ResultCallback, in addition to a callable
// object.
TEST(FunctionalityTest, CallbackInterface) {
  struct Helper {
    static absl::Status ReturnOk() { return absl::OkStatus(); }
  };

  StatusBundle b;
  b.Add(&Helper::ReturnOk);

  EXPECT_THAT(b.Join(), IsOk());
  EXPECT_THAT(b.Join(), IsOk());
}

// StatusBundle::OnJoinable should say whether the bundle is currently joinable
// or not.
TEST(FunctionalityTest, OnJoinable) {
  StatusBundle b;

  absl::Notification should_return;
  b.Add([&] {
    should_return.WaitForNotification();
    return absl::OkStatus();
  });

  EXPECT_EQ(-1, thread::SelectUntil(absl::Now() + absl::Milliseconds(2),
                                    {b.OnJoinable()}));

  should_return.Notify();
  EXPECT_EQ(thread::Select({b.OnJoinable()}), 0);

  ASSERT_THAT(b.Join(), IsOk());
}

// Functors that are move-only and can only be called once should be supported.
TEST(FunctionalityTest, SupportsMoveOnlyFunctor) {
  struct MoveOnlyFunctor {
    MoveOnlyFunctor() = default;

    // Not copyable.
    MoveOnlyFunctor(const MoveOnlyFunctor&) = delete;
    MoveOnlyFunctor& operator=(const MoveOnlyFunctor&) = delete;

    // Moveable.
    MoveOnlyFunctor(MoveOnlyFunctor&&) = default;
    MoveOnlyFunctor& operator=(MoveOnlyFunctor&&) = default;

    // This operator() is only callable on an rvalue, e.g. std::move(functor)()
    // compiles, but functor() does not.
    absl::Status operator()() && { return absl::OkStatus(); }
  };
  StatusBundle b;
  b.Add(MoveOnlyFunctor());
  EXPECT_THAT(b.Join(), IsOk());
}

////////////////////////////////////////////////////////////////////////
// Benchmarks
////////////////////////////////////////////////////////////////////////

static void BM_Common(benchmark::State& state,
                      absl::Span<const Operation> ops) {
  for (auto s : state) {
    StatusBundle b;
    for (const auto& op : ops) {
      b.Add(op);
    }

    b.Join().IgnoreError();
  }

  state.SetItemsProcessed(state.iterations() * ops.size());
}

static void BM_FastOps_Success(benchmark::State& state) {
  const Operation op = [] { return absl::OkStatus(); };
  const std::vector<Operation> ops(state.range(0), op);

  BM_Common(state, ops);
}

BENCHMARK(BM_FastOps_Success)->Range(1, 1 << 14);

static void BM_FastOps_EarlyError(benchmark::State& state) {
  const Operation success_op = [] { return absl::OkStatus(); };
  const Operation error_op = [] { return absl::UnknownError(""); };

  int num_ops = state.range(0);
  std::vector<Operation> ops(num_ops, success_op);
  ops[std::min(16, num_ops - 1)] = error_op;

  BM_Common(state, ops);
}

BENCHMARK(BM_FastOps_EarlyError)->Range(1, 1 << 14);

static void BM_FastOps_LateError(benchmark::State& state) {
  const Operation success_op = [] { return absl::OkStatus(); };
  const Operation error_op = [] { return absl::UnknownError(""); };

  int num_ops = state.range(0);
  std::vector<Operation> ops(num_ops, success_op);
  ops[num_ops - 1] = error_op;

  BM_Common(state, ops);
}

BENCHMARK(BM_FastOps_LateError)->Range(1, 1 << 14);

static void BM_ShortDelayInOps_Success(benchmark::State& state) {
  const Operation op = [] {
    absl::SleepFor(absl::Milliseconds(1));
    return absl::OkStatus();
  };

  const std::vector<Operation> ops(state.range(0), op);
  BM_Common(state, ops);
}

BENCHMARK(BM_ShortDelayInOps_Success)->Range(1, 1 << 14);

static void BM_CreateAndJoinEmptyBundle(benchmark::State& state) {
  for (auto _ : state) {
    StatusBundle bundle;
    EXPECT_THAT(bundle.Join(), IsOk());
  }
}
BENCHMARK(BM_CreateAndJoinEmptyBundle);

}  // namespace thread
