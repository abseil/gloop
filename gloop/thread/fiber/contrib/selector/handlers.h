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

// Handlers below implement SelectorHandler and are meant to be used
// as a parameter to Selector::Selector() as in selector_test.cc usage example.
//
// Usage example: selector_test.cc

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_CONTRIB_SELECTOR_HANDLERS_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_CONTRIB_SELECTOR_HANDLERS_H_

#include <algorithm>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "gloop/thread/fiber/channel.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/select.h"

namespace thread {

// Cancel Handler wraps thread::OnCancel().
// It calls a function passed to constructor on fiber cancellation.
// Usage example: selector_test.cc
class CancelHandler {
 public:
  explicit CancelHandler(absl::AnyInvocable<void()> handler)
      : handler_(std::move(handler)) {}

  thread::Case GetCase() { return thread::OnCancel(); }

  bool Execute() {
    handler_();
    return true;
  }

 private:
  absl::AnyInvocable<void()> handler_;
};

// Wraps thread::Reader<T>::OnRead().
// Calls one of two functions passed to constructor respectively when channel
// is ready to be read from and when channel is closed.
// In the latter case, this handler won't be selected again by later calls to
// Selector::Execute().
// Usage example: selector_test.cc
template <typename T>
class ReadHandler {
 public:
  ReadHandler(thread::Reader<T>* reader, absl::AnyInvocable<void(T)> on_read,
              absl::AnyInvocable<void()> on_close)
      : reader_(reader),
        on_read_(std::move(on_read)),
        on_close_(std::move(on_close)) {}

  thread::Case GetCase() { return reader_->OnRead(&t_, &ok_); }

  bool Execute() {
    if (ok_) {
      on_read_(std::move(t_));
      return true;
    } else {
      on_close_();
      return false;
    }
  }

 private:
  thread::Reader<T>* const reader_;
  absl::AnyInvocable<void(T)> on_read_;
  absl::AnyInvocable<void()> on_close_;
  T t_;
  bool ok_ = true;
};

// Class template argument deduction guideline for ReadHandler.
template <class T, class... U>
ReadHandler(thread::Reader<T>*, U...) -> ReadHandler<T>;

// Wraps thread::Writer<T>::OnWrite().
// Calls the function passed to the constructor when a write happened.
// The constructor takes in a pointer to a value; the WriteHandler will attempt
// to write that value in every call to Execute().
// The value pointed to by a WriteHandler may change between calls; however it
// needs to stay alive for the lifetime of this Handler and the Select that
// wraps it.
// Usage example: selector_test.cc
template <typename T>
class WriteHandler {
 public:
  // The value that we're trying to write to the channel needs to stay alive for
  // the lifetime of this Selector.  To make those semantics clear (and to
  // prevent passing in a temporary value) we take the input by const pointer.
  WriteHandler(thread::Writer<T>* writer, const T* value,
               absl::AnyInvocable<void()> handler)
      : writer_(writer), value_(value), handler_(std::move(handler)) {}

  thread::Case GetCase() {
    // OnWrite takes its input by const reference, but internally immediately
    // turns it back into a pointer.
    return writer_->OnWrite(*value_);
  }

  bool Execute() {
    handler_();
    return true;
  }

 private:
  thread::Writer<T>* const writer_;
  const T* const value_;
  absl::AnyInvocable<void()> handler_;
};

// Class template argument deduction guideline for WriteHandler.
template <class T, class... U>
WriteHandler(thread::Writer<T>*, U...) -> WriteHandler<T>;

// Wraps Fiber::OnJoinable.
// Calls the function passed into the constructor when the given Fiber and all
// of its descendent children are completed.
// After it runs once, the handler won't be selected again by later calls to
// Selector::Execute().
// Does not count as a Join() against the referenced fiber.
// Usage example: selector_test.cc
//
// CAUTION:
// As with Fiber::OnJoinable, there is no guarantee made that the fiber f was
// not immediately deleted prior to Execute() returning.  Unless callers are
// explicitly synchronized against the existence of f's storage, they may NOT
// reference it again.
class JoinHandler {
 public:
  JoinHandler(thread::Fiber* fiber, absl::AnyInvocable<void()> handler)
      : fiber_(fiber), handler_(std::move(handler)) {}

  thread::Case GetCase() { return fiber_->OnJoinable(); }

  bool Execute() {
    handler_();
    return false;  // Don't run again
  }

 private:
  Fiber* const fiber_;
  absl::AnyInvocable<void()> handler_;
};

// Wraps any thread::Case into a selector. The handler should return true if
// this case should be selectable again and false otherwise.
class CaseHandler {
 public:
  CaseHandler(thread::Case thread_case, absl::AnyInvocable<bool()> handler)
      : case_(thread_case), handler_(std::move(handler)) {}

  thread::Case GetCase() { return case_; }

  bool Execute() { return handler_(); }

 private:
  thread::Case case_;
  absl::AnyInvocable<bool()> handler_;
};

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_CONTRIB_SELECTOR_HANDLERS_H_
