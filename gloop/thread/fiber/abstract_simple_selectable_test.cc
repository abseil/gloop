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

#include "gloop/thread/fiber/abstract_simple_selectable.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/thread/fiber/selectables.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using thread::AbstractSimpleSelectable;

namespace {

// A Source is either empty, or holds a value of type T.  The OnValue
// can be used to wait until a value is available, then extract it, leaving
// the Source empty again.
template <typename T>
class Source : public AbstractSimpleSelectable<T, void> {
 public:
  // Constructs an empty Source.
  Source() = default;

  // This type is neither copyable nor movable.
  Source(const Source&) = delete;
  Source& operator=(const Source&) = delete;

  // Returns true if the Source currently holds a value.
  bool HasValue() const {
    absl::ReaderMutexLock lock(&this->mutex_);
    return this->is_enabled();
  }

  // Stores a value into the Source, discarding the previous value if any.
  void SetValue(const T& value) {
    absl::MutexLock lock(&this->mutex_);
    value_ = value;
    this->SetEnabled(true);
  }

  // When the Source is not empty, stores the Source's value into sink,
  // emptying the Source in the process.
  thread::Case OnValue(T* sink) { return this->OnEnabled(sink, nullptr); }

 protected:
  bool WhenSelected(T* sink, void* ignored) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(
          (AbstractSimpleSelectable<T, void>::mutex_)) {
    *sink = value_;
    return false;
  }

 private:
  T value_ ABSL_GUARDED_BY((AbstractSimpleSelectable<T, void>::mutex_));
};

// Test the "Source" example given in the doc comment for
// abstract_simple_selectable.h
TEST(AbstractSimpleSelectableTest, SourceBasicTest) {
  // Create an empty Source.  It should not have a value.
  Source<int> source;
  EXPECT_FALSE(source.HasValue());
  // Give the Source a value.  It should have a value now.
  source.SetValue(12345);
  EXPECT_TRUE(source.HasValue());
  // Extract the value from the Source.  The Source should be immediately
  // selectable, we should get the value out, and the Source should be empty
  // afterwards.
  int value = -1;
  EXPECT_EQ(0, thread::TrySelect({source.OnValue(&value)}));
  EXPECT_EQ(12345, value);
  EXPECT_FALSE(source.HasValue());
  // Try to get the value again now that the source is empty.  The Source
  // should not be selectable anymore, and we should fail to get a value.
  value = -1;
  EXPECT_EQ(-1, thread::TrySelect({source.OnValue(&value)}));
  EXPECT_EQ(-1, value);
  EXPECT_FALSE(source.HasValue());
}

// Test that a Select call waiting on AbstractSimpleSelectable's Case will
// unblock when the AbstractSimpleSelectable becomes enabled.
TEST(AbstractSimpleSelectableTest, EnablingUnblocksSelect) {
  Source<std::string> source;
  std::string value;
  int case_index = -1;
  thread::Fiber fiber([&source, &value, &case_index]() {
    case_index = thread::Select({source.OnValue(&value)});
  });
  // Give the fiber a chance to start up.
  absl::SleepFor(absl::Seconds(1));
  // The fiber should still be blocked on the Select() call.
  EXPECT_EQ(-1, thread::TrySelect({fiber.OnJoinable()}));
  EXPECT_EQ(-1, case_index);
  EXPECT_EQ("", value);
  // Now put a value in the Source and wait for the fiber to complete; the
  // Select call should unblock and we should get the value back out.
  source.SetValue("hello");
  fiber.Join();
  EXPECT_EQ(0, case_index);
  EXPECT_EQ("hello", value);
}

// Test that the implementation of the Unregister method works.
TEST(AbstractSimpleSelectableTest, Unregister) {
  Source<bool> source1;
  Source<bool> source2;
  bool value1 = false, value2 = false;
  // Select until sometime in the future, to create a need to call the
  // Unregister method.  Neither case is selectable right now, so nothing
  // should happen.
  EXPECT_EQ(-1, thread::SelectUntil(
                    absl::Now() + absl::Seconds(1),
                    {source1.OnValue(&value1), source2.OnValue(&value2)}));
  EXPECT_FALSE(value1);
  EXPECT_FALSE(value2);
  // Now enable one of the Selectables.  Since they have been unregistered by
  // now, nothing should happen.
  source1.SetValue(true);
  EXPECT_FALSE(value1);
  EXPECT_FALSE(value2);
}

// Test synchronization between SetEnabled and the destructor.
TEST(AbstractSimpleSelectableTest, DeletingWhenEnabled) {
  std::unique_ptr<Source<int>> source(new Source<int>);
  thread::Fiber fiber([&source]() {
    int value;
    thread::Select({source->OnValue(&value)});
    source.reset();
  });
  // Give the fiber a chance to start up.
  absl::SleepFor(absl::Seconds(1));
  // Put a value into the Source.  This will allow the fiber to unblock and
  // delete the Source.  There should not be any data races between SetEnabled
  // and the destructor.
  source->SetValue(42);
  fiber.Join();
}

class StringSetter
    : public AbstractSimpleSelectable<const std::string, std::string> {
 public:
  StringSetter() { SetEnabled(true); }

  // This type is neither copyable nor movable.
  StringSetter(const StringSetter&) = delete;
  StringSetter& operator=(const StringSetter&) = delete;

  // Returns a Case that is always immediately selectable, and that copies the
  // input string to the output string when selected.  Note that the input
  // string must outlive the returned Case.
  thread::Case OnSelected(const std::string& in, std::string* out) {
    return OnEnabled(&in, out);
  }

 protected:
  bool WhenSelected(const std::string* in, std::string* out) override {
    *out = *in;
    return true;
  }
};

TEST(AbstractSimpleSelectableTest, OnlyDoesActionIfSelected) {
  StringSetter setter;
  const std::string str1 = "foo";
  const std::string str2 = "bar";
  std::string out1, out2;
  thread::CaseArray cases = {setter.OnSelected(str1, &out1),
                             setter.OnSelected(str2, &out2)};
  // Repeat a few times to verify that the cases are reusable.
  for (int i = 0; i < 10; ++i) {
    // There's no guarantee as to which one gets selected, but exactly one of
    // the side effects should happen on each iteration.
    const int case_index = thread::Select(cases);
    switch (case_index) {
      case 0:
        EXPECT_EQ("foo", out1);
        EXPECT_EQ("", out2);
        break;
      case 1:
        EXPECT_EQ("", out1);
        EXPECT_EQ("bar", out2);
        break;
      default:
        ADD_FAILURE() << "Unexpected case_index: " << case_index;
        break;
    }
    out1.clear();
    out2.clear();
  }
}

class Incrementer : public AbstractSimpleSelectable<int, void> {
 public:
  Incrementer() = default;

  // This type is neither copyable nor movable.
  Incrementer(const Incrementer&) = delete;
  Incrementer& operator=(const Incrementer&) = delete;

  void Notify() {
    absl::MutexLock lock(mutex_);
    SetEnabled(true);
  }

  // Returns a Case that is selectable only if Notify has been called, and
  // which increments value by 1 each time it is selected.
  thread::Case OnNotified(int* value) { return OnEnabled(value, nullptr); }

 protected:
  bool WhenSelected(int* value, void* ignored) override {
    ++(*value);
    return true;
  }
};

// Test that an AbstractSimpleSelectable Case object can be reused.
TEST(AbstractSimpleSelectableTest, CasesAreReusable) {
  Incrementer incrementer;
  incrementer.Notify();
  int value = 0;
  thread::CaseArray cases = {incrementer.OnNotified(&value)};
  EXPECT_EQ(0, value);
  thread::Select(cases);
  EXPECT_EQ(1, value);
  thread::Select(cases);
  EXPECT_EQ(2, value);
}

// Test that two Select() calls waiting on the same AbstractSimpleSelectable
// Case can both select it (once it is enabled).
TEST(AbstractSimpleSelectableTest, SelectableByMultipleSelects) {
  Incrementer incrementer;
  // Start two fibers both blocked on the same AbstractSimpleSelectable
  // instance.
  int value = 0;
  thread::CaseArray cases = {incrementer.OnNotified(&value)};
  thread::Fiber fiber1([&cases]() { thread::Select(cases); });
  thread::Fiber fiber2([&cases]() { thread::Select(cases); });
  // Give the fibers a chance to start up.
  absl::SleepFor(absl::Seconds(1));
  // The fibers should still be blocked on the Select() calls.
  EXPECT_EQ(-1, thread::TrySelect({fiber1.OnJoinable()}));
  EXPECT_EQ(-1, thread::TrySelect({fiber2.OnJoinable()}));
  EXPECT_EQ(0, value);
  // Now enable the Incrementer.  Both Select() calls should unblock and take
  // action.
  incrementer.Notify();
  fiber1.Join();
  fiber2.Join();
  EXPECT_EQ(2, value);
}

// An object that eventually finishes with some result.
class WorkItem {
 public:
  // Becomes selectable when Finish() is called.
  thread::Case OnFinished() const;
  // Sets status() and makes OnFinished() selectable.
  void Finish(absl::Status result);
  // The result previously given to Finish().
  absl::Status status() const {
    absl::MutexLock lock(mutex_);
    return status_;
  }

 private:
  class Unready : public AbstractSimpleSelectable<void, void> {
   public:
    using AbstractSimpleSelectable::OnEnabled;
    void Finish() {
      mutex_.lock();
      SetEnabled(true);
      bool done = !is_selecting();
      mutex_.unlock();
      if (done) {
        delete this;
      }
    }

   private:
    bool WhenSelected(void*, void*) override { return true; }
    void Unregister(thread::internal::CaseState* state) override {
      mutex_.lock();
      UnregisterLocked(state);
      bool done = is_enabled() && !is_selecting();
      mutex_.unlock();
      if (done) {
        delete this;
      }
    }
  };
  mutable absl::Mutex mutex_;
  Unready* unready_ = new Unready();
  absl::Status status_ = absl::InternalError("Not ready");
};

void WorkItem::Finish(absl::Status result) {
  absl::MutexLock lock(mutex_);
  CHECK(unready_ != nullptr) << "Can only call Finish once.";
  status_ = std::move(result);
  unready_->Finish();
  unready_ = nullptr;
}

thread::Case WorkItem::OnFinished() const {
  absl::MutexLock lock(mutex_);
  if (unready_) {
    return unready_->OnEnabled(nullptr, nullptr);
  } else {
    return thread::AlwaysSelectableCase();
  }
}

TEST(AbstractSimpleSelectableTest, SetEnabledWhenIsNotSelecting) {
  // Select the same case twice to make sure that we aren't relying on being the
  // only case or always winning a race in Pick().
  WorkItem work;
  EXPECT_EQ(-1, thread::TrySelect({work.OnFinished(), work.OnFinished()}));
  EXPECT_FALSE(work.status().ok());
  work.Finish(absl::OkStatus());
  EXPECT_LE(0, thread::TrySelect({work.OnFinished(), work.OnFinished()}));
  ABSL_EXPECT_OK(work.status());
}

TEST(AbstractSimpleSelectableTest, SetEnabledWhenIsSelecting) {
  WorkItem work;
  thread::Fiber worker([&] {
    // Select the same case twice to make sure that we aren't relying on being
    // the only case or always winning a race in Pick().
    EXPECT_LE(0, thread::TrySelect({work.OnFinished(), work.OnFinished()}));
    ABSL_EXPECT_OK(work.status());
  });
  work.Finish(absl::OkStatus());
  worker.Join();
  ABSL_EXPECT_OK(work.status());
}

}  // namespace
