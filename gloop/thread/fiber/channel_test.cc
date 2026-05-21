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

// TODO: Fair choice test

#include "gloop/thread/fiber/channel.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <ostream>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/time/clock.h"
#include "absl/time/simulated_clock.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "gloop/base/context.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/base/walltime.h"
#include "gloop/thread/fiber/fiber-internal.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/fifolifo-schedulers.h"
#include "gloop/thread/fiber/init-domain.h"
#include "gloop/thread/fiber/probabilistic_test_util.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/thread/fiber/selectables.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ElementsAre;
using testing::Pointee;

namespace thread {

using thread::probabilistic_test::RunTestMultipleTimes;

static const int kMillisecondsPerTick = 500;

// Some test fail in very rare instances due to timing deviations. In these
// cases, we run the test multiple times to avoid flakes
static const int kRunsPerTest = 20;
static const int kRunsToPass = 16;

static void Delay(int n) {
  ::absl::SleepFor(::absl::Milliseconds(n * kMillisecondsPerTick));
}

static int ToTicks(WallTime start, WallTime finish) {
  return round((finish - start) * 1000.0 / kMillisecondsPerTick);
}

static int Get(Reader<int>* r) {
  int v;
  if (!r->Read(&v)) v = -1;
  return v;
}

static void DelayedRead(Reader<int>* r, int delay, int* e) {
  Delay(delay);
  *e = Get(r);
}

static void DelayedWrite(Writer<int>* w, int v, int delay) {
  Delay(delay);
  w->Write(v);
}

static void DelayedClose(Writer<int>* w, int delay) {
  Delay(delay);
  w->Close();
}

static int MeasurePut(Writer<int>* w, int v) {
  absl::Time start = absl::Now();
  w->Write(v);
  return ToTicks(base::ToWallTime(start), base::ToWallTime(absl::Now()));
}

static int MeasureClose(Writer<int>* w) {
  absl::Time start = absl::Now();
  w->Close();
  return ToTicks(base::ToWallTime(start), base::ToWallTime(absl::Now()));
}

static int MeasureGet(Reader<int>* r, int expected_value) {
  absl::Time start = absl::Now();
  EXPECT_EQ(expected_value, Get(r));
  return ToTicks(base::ToWallTime(start), base::ToWallTime(absl::Now()));
}

template <typename T>
static std::vector<T> ReadAll(thread::Reader<T>* const reader) {
  std::vector<T> result;
  for (T value; reader->Read(&value);) {
    result.emplace_back(std::move(value));
  }

  return result;
}

// A struct that contains an integer value and counts for the number of times it
// has been copied on the way to its current state.
struct MovableInt {
  int i = 0;
  size_t copy_count = 0;

  // Compare based on value only.
  bool operator==(const MovableInt& other) const { return i == other.i; }

  MovableInt() = default;
  explicit MovableInt(int x) : i(x) {}
  MovableInt(const MovableInt& o) { *this = o; }
  MovableInt(MovableInt&& o) { *this = std::move(o); }

  MovableInt& operator=(const MovableInt& other) {
    i = other.i;
    copy_count = other.copy_count + 1;
    return *this;
  }

  MovableInt& operator=(MovableInt&& other) {
    swap(*this, other);
    return *this;
  }

  friend void swap(MovableInt& a, MovableInt& b) {
    using std::swap;
    swap(a.i, b.i);
    swap(a.copy_count, b.copy_count);
  }
};

std::ostream& operator<<(std::ostream& o, const MovableInt& x) {
  return o << x.i;
}

class ChannelTest : public ::testing::Test {};

TEST_F(ChannelTest, ConstructDestroy) {
  Channel<int> c(0);
  EXPECT_TRUE(c.reader() != nullptr);  // Just to use c
  EXPECT_EQ(0, c.length());
}

TEST_F(ChannelTest, BufferedElementsEqualsSizetMax) {
  // No issue creating the max size channel.
  Channel<uint8_t> chan(std::numeric_limits<size_t>::max());
}

TEST_F(ChannelTest, PutGet) {
  Channel<int> c(1);
  c.writer()->Write(100);
  EXPECT_EQ(1, c.length());

  EXPECT_EQ(100, Get(c.reader()));
  EXPECT_EQ(0, c.length());
}

TEST_F(ChannelTest, PutGet_WithDestructor) {
  // A channel for a type with allocations in its constructor and destructor.
  Channel<std::shared_ptr<int>> c(1);

  // Write then read back. We shouldn't leak.
  const std::shared_ptr<int> expected(new int);
  c.writer()->Write(expected);

  std::shared_ptr<int> result;
  ASSERT_TRUE(c.reader()->Read(&result));
  EXPECT_EQ(expected.get(), result.get());
}

TEST_F(ChannelTest, SharedPointerRefCounts) {
  const std::shared_ptr<int> x(new int(17));

  // Set up a channel of shared pointers, and write one in. Now there should be
  // two references.
  Channel<std::shared_ptr<int>> c(1);
  c.writer()->Write(x);

  ASSERT_EQ(2, x.use_count());

  // Read it back out. The channel should let go of its reference.
  std::shared_ptr<int> y;
  ASSERT_TRUE(c.reader()->Read(&y));
  ASSERT_EQ(x, y);

  EXPECT_EQ(2, x.use_count());
}

TEST_F(ChannelTest, BufferElementsAreClearedBehind) {
  // A type containing a shared pointer, but without a move-assignment operator.
  struct CopyOnlySharedPtr {
    std::shared_ptr<int> p = std::make_shared<int>();

    CopyOnlySharedPtr() = default;
    CopyOnlySharedPtr(const CopyOnlySharedPtr& other) = default;
    CopyOnlySharedPtr& operator=(const CopyOnlySharedPtr& other) = default;
  };

  // Sanity check: ensure that assigning from an rvalue ref performs a copy.
  {
    CopyOnlySharedPtr a, b;

    b = std::move(a);
    ASSERT_EQ(b.p, a.p);
    ASSERT_EQ(2, a.p.use_count());
  }

  // Set up a channel of these objects, and write one in. Now there should be
  // two references.
  CopyOnlySharedPtr x;
  Channel<CopyOnlySharedPtr> c(1);
  c.writer()->Write(x);

  ASSERT_EQ(2, x.p.use_count());

  // Read it back out. The channel should let go of its reference.
  CopyOnlySharedPtr y;
  ASSERT_TRUE(c.reader()->Read(&y));
  ASSERT_EQ(x.p, y.p);

  EXPECT_EQ(2, x.p.use_count());
}

TEST_F(ChannelTest, CopyCount_BufferSpaceAvailable) {
  Channel<MovableInt> channel(2);

  // Write one lvalue by copy and one rvalue.
  MovableInt from(0);
  channel.writer()->Write(from);
  channel.writer()->Write(MovableInt(1));

  // The lvalue should have been copied into the buffer and moved out.
  MovableInt to;
  ASSERT_TRUE(channel.reader()->Read(&to));
  ASSERT_EQ(0, to.i);
  EXPECT_EQ(1, to.copy_count);

  // The rvalue should have been moved into the buffer and moved out.
  ASSERT_TRUE(channel.reader()->Read(&to));
  ASSERT_EQ(1, to.i);
  EXPECT_EQ(0, to.copy_count);
}

TEST_F(ChannelTest, CopyCount_ReaderBlockingOnBufferedChannel) {
  struct Helper {
    static void DoWrite(thread::Writer<MovableInt>* writer) {
      // Write one lvalue by copy and one rvalue. Wait a moment before each
      // write to give the reader a chance to block. We need not strictly
      // guarantee the reader will block; it's good enough if this test covers
      // this case probabilistically, since we'll notice a problem if it's flaky
      // when run repeatedly.
      MovableInt x(0);
      ::absl::SleepFor(::absl::Milliseconds(2));
      writer->Write(x);

      ::absl::SleepFor(::absl::Milliseconds(2));
      writer->Write(MovableInt(1));
    }
  };

  Channel<MovableInt> channel(2);
  thread::Fiber do_write(absl::bind_front(&Helper::DoWrite, channel.writer()));

  // The lvalue should have been copied once (into the buffer or directly from
  // writer to reader) and moved at most once (out of the buffer, in the case
  // that the writer beat the reader).
  MovableInt to;
  EXPECT_TRUE(channel.reader()->Read(&to));
  EXPECT_EQ(0, to.i);
  EXPECT_EQ(1, to.copy_count);

  // The rvalue should have never been copied, and moved up to twice (into and
  // out of the buffer, or directly from writer to reader).
  EXPECT_TRUE(channel.reader()->Read(&to));
  EXPECT_EQ(1, to.i);
  EXPECT_EQ(0, to.copy_count);

  do_write.Join();
}

TEST_F(ChannelTest, CopyCount_WriterBlockingOnBufferedChannel) {
  struct Helper {
    static void DoWrite(thread::Writer<MovableInt>* writer) {
      // Write one lvalue by copy and one rvalue.
      MovableInt x(0);
      writer->Write(x);
      writer->Write(MovableInt(1));
    }
  };

  // Set up a channel and fill its buffer.
  Channel<MovableInt> channel(2);
  channel.writer()->Write(MovableInt());
  channel.writer()->Write(MovableInt());

  // Launch the writer.
  thread::Fiber do_write(absl::bind_front(&Helper::DoWrite, channel.writer()));

  // Remove the initial items from the buffer, unblocking the writer. Wait a
  // moment before each read to give the writer a chance to block. We need not
  // strictly guarantee the writer will block; it's good enough if this test
  // covers this case probabilistically, since we'll notice a problem if it's
  // flaky when run repeatedly.
  MovableInt dummy;

  ::absl::SleepFor(::absl::Milliseconds(2));
  EXPECT_TRUE(channel.reader()->Read(&dummy));

  ::absl::SleepFor(::absl::Milliseconds(2));
  EXPECT_TRUE(channel.reader()->Read(&dummy));

  // The lvalue should have been copied once (into the buffer or directly from
  // writer to reader) and moved at most once (out of the buffer, in the case
  // that the writer beat the reader).
  MovableInt to;
  EXPECT_TRUE(channel.reader()->Read(&to));
  EXPECT_EQ(0, to.i);
  EXPECT_EQ(1, to.copy_count);

  // The rvalue should have never been copied, and moved up to twice (into and
  // out of the buffer, or directly from writer to reader).
  EXPECT_TRUE(channel.reader()->Read(&to));
  EXPECT_EQ(1, to.i);
  EXPECT_EQ(0, to.copy_count);

  do_write.Join();
}

TEST_F(ChannelTest, CopyCount_Unbuffered) {
  struct Helper {
    static void DoWrite(thread::Writer<MovableInt>* writer) {
      // Write one lvalue by copy and one rvalue.
      MovableInt x(0);
      writer->Write(x);
      writer->Write(MovableInt(1));
    }
  };

  Channel<MovableInt> channel(0);
  thread::Fiber do_write(absl::bind_front(&Helper::DoWrite, channel.writer()));

  // The lvalue should have been copied into a temporary and moved into the
  // output parameter.
  MovableInt to;
  EXPECT_TRUE(channel.reader()->Read(&to));
  EXPECT_EQ(0, to.i);
  EXPECT_EQ(1, to.copy_count);

  // The rvalue should have been moved into the output parameter.
  EXPECT_TRUE(channel.reader()->Read(&to));
  EXPECT_EQ(1, to.i);
  EXPECT_EQ(0, to.copy_count);

  do_write.Join();
}

TEST_F(ChannelTest, Close) {
  Channel<int> c(1);
  c.writer()->Write(100);
  c.writer()->Close();
  EXPECT_EQ(1, c.length());
  EXPECT_EQ(100, Get(c.reader()));
  EXPECT_EQ(-1, Get(c.reader()));
}

TEST_F(ChannelTest, PutToWaitingReader) {
  auto runner = []() {
    Channel<int> c(0);
    int e;
    Fiber r(absl::bind_front(DelayedRead, c.reader(), 0, &e));
    bool passed = PROB_EXPECT_EQ(0, MeasurePut(c.writer(), 100));
    r.Join();
    passed &= PROB_EXPECT_EQ(100, e);
    return passed;
  };
  ASSERT_TRUE(RunTestMultipleTimes(kRunsPerTest, kRunsToPass, runner));
}

TEST_F(ChannelTest, PutBlocksWhenFull) {
  auto runner = []() {
    Channel<int> c(2);
    int e;
    Fiber r(absl::bind_front(DelayedRead, c.reader(), 1, &e));
    bool passed = PROB_EXPECT_EQ(0, MeasurePut(c.writer(), 100));
    passed &= PROB_EXPECT_EQ(0, MeasurePut(c.writer(), 200));
    passed &=
        PROB_EXPECT_EQ(1, MeasurePut(c.writer(), 300));  // Must wait for reader
    r.Join();
    passed &= PROB_EXPECT_EQ(100, e);
    passed &= PROB_EXPECT_EQ(200, Get(c.reader()));
    passed &= PROB_EXPECT_EQ(300, Get(c.reader()));
    return passed;
  };
  ASSERT_TRUE(RunTestMultipleTimes(kRunsPerTest, kRunsToPass, runner));
}

TEST_F(ChannelTest, GetBlocksWhenEmpty) {
  auto runner = []() {
    Channel<int> c(2);
    Fiber w(absl::bind_front(DelayedWrite, c.writer(), 7, 1));
    bool passed = PROB_EXPECT_EQ(1, MeasureGet(c.reader(), 7));
    w.Join();
    return passed;
  };
  ASSERT_TRUE(RunTestMultipleTimes(kRunsPerTest, kRunsToPass, runner));
}

TEST_F(ChannelTest, GetUnblocksWhenClosed) {
  auto runner = []() {
    Channel<int> c(2);
    Fiber w(absl::bind_front(DelayedClose, c.writer(), 1));
    bool passed = PROB_EXPECT_EQ(1, MeasureGet(c.reader(), -1));
    w.Join();
    return passed;
  };
  ASSERT_TRUE(RunTestMultipleTimes(kRunsPerTest, kRunsToPass, runner));
}

TEST_F(ChannelTest, PutWhenClosed) {
  Channel<int> c(1);
  c.writer()->Close();
  EXPECT_DEATH_IF_SUPPORTED(c.writer()->Write(0),
                            "Calling Write\\(\\) on closed channel");
  EXPECT_EQ(0, c.length());
}

TEST_F(ChannelTest, UnbufferedPutGet) {
  auto runner = []() {
    Channel<int> c(0);
    int e;
    Fiber r(absl::bind_front(DelayedRead, c.reader(), 1, &e));
    bool passed =
        PROB_EXPECT_EQ(1, MeasurePut(c.writer(), 100));  // Must wait for reader
    r.Join();
    passed &= PROB_EXPECT_EQ(100, e);
    return passed;
  };
  ASSERT_TRUE(RunTestMultipleTimes(kRunsPerTest, kRunsToPass, runner));
}

TEST_F(ChannelTest, UnbufferedClose) {
  Channel<int> c(0);
  int e;
  Fiber r(absl::bind_front(DelayedRead, c.reader(), 1, &e));
  EXPECT_EQ(0, MeasureClose(c.writer()));  // Must not wait for reader
  r.Join();
  EXPECT_EQ(-1, e);
}

TEST_F(ChannelTest, SelectorReaderInitiallyReady1) {
  Channel<int> c(1);
  c.writer()->Close();
  int v = -100;
  bool ok;
  EXPECT_EQ(0, Select({c.reader()->OnRead(&v, &ok)}));
  EXPECT_FALSE(ok);
  EXPECT_EQ(-100, v);
}

TEST_F(ChannelTest, SelectorReaderInitiallyReady2) {
  Channel<int> c(1);
  c.writer()->Write(100);
  int v = -100;
  bool ok;
  EXPECT_EQ(0, Select({c.reader()->OnRead(&v, &ok)}));
  EXPECT_TRUE(ok);
  EXPECT_EQ(100, v);
}

TEST_F(ChannelTest, SelectorReaderBecomesReady) {
  Channel<int> c(1);
  int v = -1;
  bool ok = false;

  // Reader is not ready
  EXPECT_EQ(-1, TrySelect({c.reader()->OnRead(&v, &ok)}));

  // Write some data.  Should make reader ready.
  c.writer()->Write(100);
  EXPECT_EQ(0, TrySelect({c.reader()->OnRead(&v, &ok)}));
  EXPECT_TRUE(ok);
  EXPECT_EQ(100, v);

  // Reader is not ready
  EXPECT_EQ(-1, TrySelect({c.reader()->OnRead(&v, &ok)}));

  // After writer is closed, reader should become ready.
  c.writer()->Close();
  EXPECT_EQ(0, TrySelect({c.reader()->OnRead(&v, &ok)}));
  EXPECT_FALSE(ok);
}

TEST_F(ChannelTest, SelectorWriterInitiallyNotReady) {
  Channel<int> c(0);
  EXPECT_EQ(-1, TrySelect({c.writer()->OnWrite(100)}));
}

TEST_F(ChannelTest, SelectorWriterInitiallyReady1) {
  Channel<int> c(0);
  int e;
  Fiber r(absl::bind_front(DelayedRead, c.reader(), 0, &e));
  Delay(1);
  EXPECT_EQ(0, TrySelect({c.writer()->OnWrite(100)}));
  r.Join();
  EXPECT_EQ(100, e);
}

TEST_F(ChannelTest, SelectorWriterInitiallyReady2) {
  Channel<int> c(1);
  EXPECT_EQ(0, TrySelect({c.writer()->OnWrite(100)}));
}

TEST_F(ChannelTest, SelectorWriterBecomesReady) {
  Channel<int> c(0);
  EXPECT_EQ(-1, TrySelect({c.writer()->OnWrite(100)}));
  int e;
  Fiber r(absl::bind_front(DelayedRead, c.reader(), 0, &e));
  Delay(1);
  EXPECT_EQ(0, TrySelect({c.writer()->OnWrite(100)}));
  r.Join();
  EXPECT_EQ(100, e);
}

TEST_F(ChannelTest, SelectorWriterReadyWhenHasRoom) {
  Channel<int> c(2);
  EXPECT_EQ(0, TrySelect({c.writer()->OnWrite(100)}));
  EXPECT_EQ(0, TrySelect({c.writer()->OnWrite(200)}));
  EXPECT_EQ(-1, TrySelect({c.writer()->OnWrite(300)}));  // No room
  EXPECT_EQ(100, Get(c.reader()));                       // Free one slot
  EXPECT_EQ(0, TrySelect({c.writer()->OnWrite(400)}));
  EXPECT_EQ(200, Get(c.reader()));
  EXPECT_EQ(400, Get(c.reader()));
}

TEST_F(ChannelTest, SelectUntilExpires) {
  auto runner = []() {
    Channel<int> c(0);
    int val;
    bool status;

    // Already expired (epoch)
    bool passed =
        PROB_EXPECT_EQ(-1, TrySelect({c.reader()->OnRead(&val, &status)}));

    // Already expired, but still positive
    passed &=
        PROB_EXPECT_EQ(-1, SelectUntil(absl::Now() + absl::Nanoseconds(-100),
                                       {c.reader()->OnRead(&val, &status)}));

    // Expires in a tick
    absl::Time deadline =
        absl::Now() + absl::Milliseconds(kMillisecondsPerTick);
    absl::Time start = absl::Now();
    passed &= PROB_EXPECT_EQ(
        -1, SelectUntil(deadline, {c.reader()->OnRead(&val, &status)}));
    passed &= PROB_EXPECT_EQ(
        1, ToTicks(base::ToWallTime(start), base::ToWallTime(absl::Now())));

    // Returns before expiry
    deadline = absl::Now() + absl::Milliseconds(2 * kMillisecondsPerTick);
    Fiber w(absl::bind_front(DelayedWrite, c.writer(), 1, 1));
    start = absl::Now();
    passed &= PROB_EXPECT_EQ(
        0, SelectUntil(deadline, {c.reader()->OnRead(&val, &status)}));
    passed &= PROB_EXPECT_GT(
        2, ToTicks(base::ToWallTime(start), base::ToWallTime(absl::Now())));
    w.Join();
    return passed;
  };
  ASSERT_TRUE(RunTestMultipleTimes(kRunsPerTest, kRunsToPass, runner));
}

TEST_F(ChannelTest, MoveOnlyType) {
  Channel<std::unique_ptr<int>> channel(10);
  const auto writer = channel.writer();

  // Write in various ways.
  writer->Write(std::make_unique<int>(17));
  Select({writer->OnWrite(std::make_unique<int>(19))});
  writer->WriteUnlessCancelled(std::make_unique<int>(23));
  writer->WriteUnlessCancelledOrExpired(std::make_unique<int>(29));

  // Check the contents.
  std::unique_ptr<int> p;

  ASSERT_TRUE(channel.reader()->Read(&p));
  ASSERT_THAT(p, Pointee(17));

  ASSERT_TRUE(channel.reader()->Read(&p));
  ASSERT_THAT(p, Pointee(19));

  ASSERT_TRUE(channel.reader()->Read(&p));
  ASSERT_THAT(p, Pointee(23));

  ASSERT_TRUE(channel.reader()->Read(&p));
  ASSERT_THAT(p, Pointee(29));

  // Leave an element in the channel. It shouldn't leak.
  writer->Write(std::make_unique<int>());
}

TEST_F(ChannelTest, MovableCopyableType) {
  // MvCpType is std::is_{copy,move}_{constructible,assignable}, but trying to
  // copy construct/assign it causes a compiler error.  This test validates
  // primarily that Channel compiles with such types.  Secondarily, validation
  // modeled on MoveOnlyType's checks is performed.
  typedef std::vector<std::unique_ptr<int>> MvCpType;
  Channel<MvCpType> channel(10);
  const auto writer = channel.writer();

  // Instances of MvCpType are a bit unwieldy to create, so do that up front.
  // Note that they will be moved out of, and must not be used after that.
  MvCpType instance1;
  instance1.emplace_back(new int(1));
  MvCpType instance2;
  instance2.emplace_back(new int(2));
  MvCpType instance3;
  instance3.emplace_back(new int(3));
  MvCpType instance4;
  instance4.emplace_back(new int(4));
  MvCpType instance5;
  instance5.emplace_back(new int(4));

  // Write in various ways.
  writer->Write(std::move(instance1));
  Select({writer->OnWrite(std::move(instance2))});
  writer->WriteUnlessCancelled(std::move(instance3));
  writer->WriteUnlessCancelledOrExpired(std::move(instance4));

  // Check the contents.
  MvCpType p;

  ASSERT_TRUE(channel.reader()->Read(&p));
  ASSERT_THAT(p, ElementsAre(Pointee(1)));

  ASSERT_TRUE(channel.reader()->Read(&p));
  ASSERT_THAT(p, ElementsAre(Pointee(2)));

  ASSERT_TRUE(channel.reader()->Read(&p));
  ASSERT_THAT(p, ElementsAre(Pointee(3)));

  ASSERT_TRUE(channel.reader()->Read(&p));
  ASSERT_THAT(p, ElementsAre(Pointee(4)));

  // Leave an element in the channel. It shouldn't leak.
  writer->Write(std::move(instance5));
}

// Channel<T> does not require <T> to be default constructible.
TEST_F(ChannelTest, NonDefaultConstructibleType) {
  class NonDefaultConstructibleType {
   public:
    explicit NonDefaultConstructibleType(int val) : val_(val) {}
    int val() { return val_; }

   private:
    int val_;
  };

  Channel<NonDefaultConstructibleType> chan(1);

  chan.writer()->Write(NonDefaultConstructibleType(42));
  NonDefaultConstructibleType out(0);
  ASSERT_TRUE(chan.reader()->Read(&out));
  EXPECT_EQ(42, out.val());
}

// Ensures that a type with alignment requirements always has appropriate
// storage as it transitions through a Channel.
TEST_F(ChannelTest, RequiredAlignment) {
  struct alignas(128) AlignedType {
    explicit AlignedType(int x) : val(x) {
      intptr_t int_this = reinterpret_cast<intptr_t>(this);
      // The compiler seems to optimize reinterpret_cast<intptr_t>(this) % 128
      // to 0 since the data type is supposed to be aligned.
      benchmark::DoNotOptimize(int_this);
      CHECK_EQ(0, int_this % 128);
    }

    int val;
  };
  Channel<AlignedType> chan(2);
  AlignedType in(1), out(0);

  // We write two values to ensure that objects beyond the first element are
  // also aligned correctly.
  chan.writer()->Write(in);
  chan.writer()->Write(in);
  ASSERT_TRUE(chan.reader()->Read(&out));
  EXPECT_EQ(1, out.val);
  ASSERT_TRUE(chan.reader()->Read(&out));
  EXPECT_EQ(1, out.val);
}

TEST(WriteUnlessCancelledTest, WriteSuccessful) {
  Channel<int> chan(1);
  ASSERT_TRUE(chan.writer()->WriteUnlessCancelled(17));

  int val = 0;
  ASSERT_TRUE(chan.reader()->Read(&val));
  EXPECT_EQ(17, val);
}

TEST(WriteUnlessCancelledTest, WriteCancelled) {
  struct Helper {
    static void Write(Writer<int>* writer, bool* result) {
      *result = writer->WriteUnlessCancelled(1);
    }
  };

  Channel<int> chan(0);  // A channel whose write will block.

  bool result = true;
  Fiber writer(absl::bind_front(&Helper::Write, chan.writer(), &result));
  // Cancel the fiber. The call should return false.
  writer.Cancel();
  writer.Join();
  EXPECT_FALSE(result);
}

TEST(WriteUnlessCancelledTest, ThreadCancelled) {
  Channel<int> chan(10);  // A channel which will not block.
  PermanentEvent event;

  EXPECT_TRUE(chan.writer()->WriteUnlessCancelled(12));

  Fiber child_proc([&event, &chan] {
    Select({event.OnEvent()});
    EXPECT_FALSE(chan.writer()->WriteUnlessCancelled(13));
  });

  child_proc.Cancel();
  event.Notify();
  child_proc.Join();
}

TEST(WriteUnlessCancelledOrExpiredTest, WriteSuccessful) {
  Channel<int> chan(1);
  ASSERT_TRUE(chan.writer()->WriteUnlessCancelledOrExpired(17));

  int val = 0;
  ASSERT_TRUE(chan.reader()->Read(&val));
  EXPECT_EQ(17, val);
}

TEST(WriteUnlessCancelledOrExpiredTest, Cancelled) {
  struct Helper {
    static void Write(Writer<int>* writer, bool* result) {
      *result = writer->WriteUnlessCancelledOrExpired(1);
    }
  };

  Channel<int> chan(0);  // A channel whose write will block.

  bool result = true;
  Fiber writer(absl::bind_front(&Helper::Write, chan.writer(), &result));
  // Cancel the fiber. The call should return false.
  writer.Cancel();
  writer.Join();
  EXPECT_FALSE(result);
}

TEST(WriteUnlessCancelledOrExpiredTest, ThreadCancelled) {
  Channel<int> chan(10);  // A channel which will not block.
  PermanentEvent event;

  EXPECT_TRUE(chan.writer()->WriteUnlessCancelled(14));

  Fiber child_proc([&event, &chan] {
    Select({event.OnEvent()});
    EXPECT_FALSE(chan.writer()->WriteUnlessCancelledOrExpired(15));
  });

  child_proc.Cancel();
  event.Notify();
  child_proc.Join();
}

TEST(WriteUnlessCancelledOrExpiredTest, DeadlineExceeded) {
  Channel<int> chan(0);

  // A write that would otherwise block, with a deadline in the past.
  {
    const base::WithDeadline wd(absl::Now() - absl::Minutes(1));
    EXPECT_FALSE(chan.writer()->WriteUnlessCancelledOrExpired(17));
  }
}

TEST(WriteUnlessCancelledOrExpiredTest, DeadlineExceededWithSimulatedClock) {
  Channel<int> chan(0);
  absl::SimulatedClock clock;

  // A write is blocking, with a long deadline in the future. Advancing the
  // clock will unblock the clock.
  {
    const base::WithDeadline wd(clock.TimeNow() + absl::Minutes(100));
    thread::Fiber do_write([&]() {
      EXPECT_FALSE(chan.writer()->WriteUnlessCancelledOrExpired(17, &clock));
    });
    clock.AdvanceTime(absl::Minutes(100));
    do_write.Join();
  }
}

// WriterCloser is movable, but not copyable.
static_assert(std::is_move_constructible_v<WriterCloser<int>>);
static_assert(std::is_move_assignable_v<WriterCloser<int>>);
static_assert(!std::is_copy_constructible_v<WriterCloser<int>>);
static_assert(!std::is_copy_assignable_v<WriterCloser<int>>);

TEST(WriterCloserTest, DefaultConstructor) {
  WriterCloser<int> wc;
  EXPECT_EQ(nullptr, wc.get());
}

TEST(WriterCloserTest, PointerConstructor) {
  Channel<int> chan(2);

  {
    // Construct a writer-closer with a pointer to a writer.
    const WriterCloser<int> wc(chan.writer());

    // We should be able to write through it, using both get() and operator->.
    wc.get()->Write(17);
    wc->Write(19);
  }

  // Once it is destroyed the channel should be closed.
  EXPECT_THAT(ReadAll(chan.reader()), ElementsAre(17, 19));
}

TEST(WriterCloserTest, MoveConstructor) {
  Channel<int> chan(2);

  // Write into a writer-closer passed into a function by rvalue.
  [](WriterCloser<int> wc) {
    wc.get()->Write(17);
    wc->Write(19);
  }(WriterCloser<int>(chan.writer()));

  // Once it is destroyed the channel should be closed.
  EXPECT_THAT(ReadAll(chan.reader()), ElementsAre(17, 19));
}

TEST(WriterCloserTest, MoveAssign_PreviouslyEmpty) {
  Channel<int> chan(1);
  WriterCloser<int> first_wc(chan.writer());

  // Move-assign an object containing the channel's writer into an empty object.
  // We should be able to write through the moved-to object, and the channel
  // should be closed when the moved-to object goes out of scope. The moved-from
  // object should be empty.
  {
    WriterCloser<int> second_wc;
    second_wc = std::move(first_wc);
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_EQ(first_wc.get(), nullptr);
    second_wc->Write(17);
  }

  EXPECT_THAT(ReadAll(chan.reader()), ElementsAre(17));
}

TEST(WriterCloserTest, MoveAssign_PreviouslyNotEmpty) {
  Channel<int> chan_0(1);
  Channel<int> chan_1(1);

  // Move-assign an object containing a new channel's writer into a non-empty
  // object. We should be able to write through the moved-to object, and the new
  // channel should be closed when it goes out of scope. The moved-from object
  // should be empty and the old channel should be immediately closed.
  {
    WriterCloser<int> first_wc(chan_0.writer());
    first_wc->Write(17);

    WriterCloser<int> second_wc(chan_1.writer());
    first_wc = std::move(second_wc);
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_EQ(second_wc.get(), nullptr);
    EXPECT_THAT(ReadAll(chan_0.reader()), ElementsAre(17));

    first_wc->Write(19);
  }

  EXPECT_THAT(ReadAll(chan_1.reader()), ElementsAre(19));
}

TEST(WriterCloserTest, MoveAssign_ToSelf) {
  Channel<int> chan(1);

  // Move-assign a non-empty object from itself. We should be able to write
  // through the object, and the channel should still be closed when it goes out
  // of scope.
  {
    WriterCloser<int> wc(chan.writer());
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
#endif  // defined(__clang__)
    wc = std::move(wc);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif  // defined(__clang__)
    wc->Write(17);
  }

  EXPECT_THAT(ReadAll(chan.reader()), ElementsAre(17));
}

TEST(WriterCloserTest, AssignNull) {
  Channel<int> chan(1);

  WriterCloser<int> wc(chan.writer());
  wc->Write(17);
  wc = nullptr;

  EXPECT_EQ(wc.get(), nullptr);
  EXPECT_THAT(ReadAll(chan.reader()), ElementsAre(17));
}

TEST(WriterCloserTest, Reset_PreviouslyEmpty) {
  Channel<int> chan(1);

  // Reset an empty object to contain the channel's writer. We should be able to
  // write through it, and the channel should be closed when it goes out of
  // scope.
  {
    WriterCloser<int> wc;
    wc.reset(chan.writer());
    wc->Write(17);
  }

  EXPECT_THAT(ReadAll(chan.reader()), ElementsAre(17));
}

TEST(WriterCloserTest, Reset_PreviouslyNotEmpty) {
  Channel<int> chan_0(1);
  Channel<int> chan_1(1);

  // Reset a non-empty object to contain a new channel's writer. We should be
  // able to write through it, and the new channel should be closed when it goes
  // out of scope. The old channel should be immediately closed.
  {
    WriterCloser<int> wc(chan_0.writer());
    wc->Write(17);

    wc.reset(chan_1.writer());
    EXPECT_THAT(ReadAll(chan_0.reader()), ElementsAre(17));

    wc->Write(19);
  }

  EXPECT_THAT(ReadAll(chan_1.reader()), ElementsAre(19));
}

TEST(WriterCloserTest, ResetToNull) {
  Channel<int> chan(1);

  WriterCloser<int> wc(chan.writer());
  wc->Write(17);
  wc.reset();

  EXPECT_THAT(ReadAll(chan.reader()), ElementsAre(17));
}

TEST(WriterCloserTest, Release) {
  Channel<int> chan(1);

  // Create a closer but then release it.
  Writer<int>* writer = nullptr;
  {
    WriterCloser<int> closer(chan.writer());
    writer = closer.release();
  }

  ASSERT_EQ(chan.writer(), writer);

  // The writer should still work.
  writer->Write(17);

  int val;
  ASSERT_TRUE(chan.reader()->Read(&val));
  EXPECT_EQ(17, val);

  // Closing the writer shouldn't cause a crash.
  writer->Close();
}

TEST(WriterCloserTest, MakeWriterCloser) {
  Channel<int> chan(1);
  {
    const auto closer = MakeWriterCloser(chan.writer());

    // The writer works.
    chan.writer()->Write(17);
    int val;
    ASSERT_TRUE(chan.reader()->Read(&val));
    EXPECT_EQ(17, val);
  }

  // The writer is closed.
  int val;
  EXPECT_FALSE(chan.reader()->Read(&val));
}

TEST(WriterCloserTest, DeleteWithRemoteClose) {
  struct Helper {
    static void RemoteClose(Writer<int>* writer) { writer->Close(); }
  };

  // Verifies that a remote Close() of "chan" and deletion synchronize.
  Channel<int>* chan = new Channel<int>(1);
  Fiber child(absl::bind_front(Helper::RemoteClose, chan->writer()));
  int unused;
  ASSERT_FALSE(chan->reader()->Read(&unused));
  delete chan;

  child.Join();
}

static void MakeBenchmarkItem(int* item) { *item = 0; }

static void MakeBenchmarkItem(std::vector<uint64_t>* item) {
  item->resize(2048);  // 16 KB of elements.
}

// Run a benchmark where a fiber repeatedly writes an item into a channel
// buffer, then reads it back out. This is intended to measure the performance
// of buffer manipulation code without any scheduling or lock contention
// overhead, making the benchmark more repeatable
template <typename T, bool move_items>
static void RunOneFiberBenchmark(benchmark::State& state) {
  Channel<T> channel(1);
  T item;
  for (auto _ : state) {
    MakeBenchmarkItem(&item);

    if (move_items) {
      channel.writer()->Write(std::move(item));
    } else {
      channel.writer()->Write(item);
    }

    channel.reader()->Read(&item);
  }

  // Report throughput in channel writes per core-second.
  state.SetItemsProcessed(state.iterations());
}

// Run a benchmark that involves a writer fiber sending items across a channel
// with a given buffer size to a reader fiber.
template <typename T>
static void RunTwoFiberBenchmark(benchmark::State* state, size_t buffer_size) {
  struct Helper {
    static void ReadAll(Reader<T>* r) {
      for (T v; r->Read(&v);) {
      }
    }

    static void WriteAll(benchmark::State* state, Writer<T>* w) {
      T item;
      for (auto _ : *state) {
        MakeBenchmarkItem(&item);
        w->Write(std::move(item));
      }
      w->Close();
    }
  };

  Channel<T> c(buffer_size);
  Fiber reader(absl::bind_front(&Helper::ReadAll, c.reader()));
  Fiber writer(absl::bind_front(&Helper::WriteAll, state, c.writer()));

  writer.Join();
  reader.Join();

  // Report throughput in channel writes per core-second.
  state->SetItemsProcessed(state->iterations());
}

template <typename T>
static void RunTwoFiberBenchmarkInScheduler(
    benchmark::State* state, size_t buffer_size,
    std::function<base::scheduling::Scheduler*()> scheduler_generator) {
  Channel<T> c(buffer_size);

  auto scheduler = scheduler_generator();
  CHECK(scheduler != nullptr);

  NewTree(TreeOptions().set_scheduler(scheduler), [&]() {
    Fiber reader([&]() {
      auto r = c.reader();
      for (T v; r->Read(&v);) {
      }
    });
    Fiber writer([&]() {
      T item;
      auto w = c.writer();
      for (auto _ : *state) {
        MakeBenchmarkItem(&item);
        w->Write(std::move(item));
      }
      w->Close();
    });

    writer.Join();
    reader.Join();
  })->Join();
  scheduler->Orphan();

  // Report throughput in channel writes per core-second.
  state->SetItemsProcessed(state->iterations());
}

static void BM_OneFiber_Ints(benchmark::State& state) {
  RunOneFiberBenchmark<int, false>(state);
}

static void BM_OneFiber_Vectors_Move(benchmark::State& state) {
  RunOneFiberBenchmark<std::vector<uint64_t>, true>(state);
}

static void BM_OneFiber_Vectors_Copy(benchmark::State& state) {
  RunOneFiberBenchmark<std::vector<uint64_t>, false>(state);
}

BENCHMARK(BM_OneFiber_Ints);
BENCHMARK(BM_OneFiber_Vectors_Move);
BENCHMARK(BM_OneFiber_Vectors_Copy);

static void BM_TwoFibers_Ints(benchmark::State& state) {
  RunTwoFiberBenchmark<int>(&state, state.range(0));
}

static void BM_TwoFibersFIFO1_Ints(benchmark::State& state) {
  RunTwoFiberBenchmarkInScheduler<int>(&state, state.range(0), []() {
    return NewChildFIFOScheduler(DefaultDomain()->root_scheduler(), 1);
  });
}

static void BM_TwoFibersFIFO2_Ints(benchmark::State& state) {
  RunTwoFiberBenchmarkInScheduler<int>(&state, state.range(0), []() {
    return NewChildFIFOScheduler(DefaultDomain()->root_scheduler(), 2);
  });
}

static void BM_TwoFibersLIFO1_Ints(benchmark::State& state) {
  RunTwoFiberBenchmarkInScheduler<int>(&state, state.range(0), []() {
    return NewChildLIFOScheduler(DefaultDomain()->root_scheduler(), 1);
  });
}

static void BM_TwoFibersLIFO2_Ints(benchmark::State& state) {
  RunTwoFiberBenchmarkInScheduler<int>(&state, state.range(0), []() {
    return NewChildLIFOScheduler(DefaultDomain()->root_scheduler(), 2);
  });
}

static void BM_TwoFibers_Vectors(benchmark::State& state) {
  RunTwoFiberBenchmark<std::vector<uint64_t>>(&state, state.range(0));
}

BENCHMARK(BM_TwoFibers_Ints)
    ->Arg(0)
    ->Arg(1)
    ->Arg(8)
    ->Arg(64)
    ->Arg(512)
    ->Arg(1024);

BENCHMARK(BM_TwoFibersFIFO1_Ints)->Arg(0)->Arg(1)->Arg(8)->Arg(64);
BENCHMARK(BM_TwoFibersFIFO2_Ints)->Arg(0)->Arg(1)->Arg(8)->Arg(64);
BENCHMARK(BM_TwoFibersLIFO1_Ints)->Arg(0)->Arg(1)->Arg(8)->Arg(64);
BENCHMARK(BM_TwoFibersLIFO2_Ints)->Arg(0)->Arg(1)->Arg(8)->Arg(64);

BENCHMARK(BM_TwoFibers_Vectors)
    ->Arg(0)
    ->Arg(1)
    ->Arg(8)
    ->Arg(64)
    ->Arg(512)
    ->Arg(1024);

}  // namespace thread
