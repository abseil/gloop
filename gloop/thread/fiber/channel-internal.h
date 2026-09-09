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

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_CHANNEL_INTERNAL_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_CHANNEL_INTERNAL_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <type_traits>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/chunked_queue.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "gloop/thread/fiber/select-internal.h"
#include "gloop/thread/fiber/select.h"

namespace thread {
namespace internal {

// Type-independent channel implementation.
struct ChannelWaiterState {
  // NOTE: Caller is responsible for synchronizing all access to methods below
  // via ChannelState::mu_

  // Attempt to find an eligible "reader" to be paired with "writer".  Or,
  // a "writer" to be paired with the passed "reader", respectively.
  //
  // Returns true, and updates *reader (or *writer), if an eligible waiter
  // exists. The reader and writer both returned with selector mutexes held and
  // are guaranteed to be pickable.
  // Returns false with no side-effects otherwise.
  // REQUIRES: reader != nullptr, writer != nullptr
  bool GetMatchingReader(CaseState* writer, CaseState** reader)
      ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(true, (*reader)->sel->mu,
                                      writer->sel->mu);
  bool GetMatchingWriter(CaseState* reader, CaseState** writer)
      ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(true, reader->sel->mu,
                                      (*writer)->sel->mu);

  // Attempt to find an eligible queued writer.  There is no matching reader in
  // this case, it is used for when space becomes available in the queue due to
  // a read completing, allowing a writer to complete without partner.
  //
  // Returns true, and updates *writer, if a suitable waiter exists.  *writer is
  // returned with selector mutex held and guaranteed pickable.
  // Returns false with no side-effects otherwise.
  bool GetWaitingWriter(CaseState** writer)
      ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(true, (*writer)->sel->mu);

  // Unlock (and mark selected) the passed reader/writer respectively.
  // REQUIRES: selector mutex is held, picked == kNonePicked
  void UnlockAndReleaseReader(CaseState* reader)
      ABSL_UNLOCK_FUNCTION(reader->sel->mu);
  void UnlockAndReleaseWriter(CaseState* writer)
      ABSL_UNLOCK_FUNCTION(writer->sel->mu);

  // Releases all waiting readers.  Unselected readers are picked and marked to
  // return that this channel was closed.
  void CloseAndReleaseReaders();

  internal::CaseState* waiting_readers_ = nullptr;
  internal::CaseState* waiting_writers_ = nullptr;
};

// Type-dependent channel implementation.
template <typename T>
class ChannelState final : public ChannelWaiterState {
 public:
  explicit ChannelState(size_t capacity)
      : capacity_(capacity), closed_(false), rd_(this), wr_(this) {
    DCHECK(Invariants());
  }

  // This type is neither copyable nor movable.
  ChannelState(const ChannelState&) = delete;
  ChannelState& operator=(const ChannelState&) = delete;

  ~ChannelState();

  void Close() {
    absl::MutexLock l(mu_);
    DCHECK(Invariants());
    CHECK(!closed_) << "Calling Close() on closed channel";
    CHECK(waiting_writers_ == nullptr)
        << "Calling Close() on channel with blocked writers";
    closed_ = true;
    this->CloseAndReleaseReaders();
    DCHECK(Invariants());
  }

  bool Get(T* dst) {
    bool result;
    Select({OnRead(dst, &result)});
    return result;
  }

  inline size_t Length() const {
    absl::MutexLock l(mu_);
    return queue_.size();
  }

  inline Case OnRead(T* dst, bool* ok) {
    Case c = {
        &rd_,
        reinterpret_cast<intptr_t>(dst),
        reinterpret_cast<intptr_t>(ok),
    };
    return c;
  }

  inline Case OnWrite(const T& item) {
    // Guard against user error at compile-time.
    static_assert(
        std::is_copy_constructible<T>::value,
        "Channel<T>::OnWrite called with const T& for a type T that is not "
        "copy constructible.");
    return {&wr_, reinterpret_cast<intptr_t>(&item),
            reinterpret_cast<intptr_t>(&CopyOut)};
  }

  // Style approval: <link>
  inline Case OnWrite(T&& item) {
    return {&wr_, reinterpret_cast<intptr_t>(&item),
            reinterpret_cast<intptr_t>(&MoveOut)};
  }

 private:
  const size_t capacity_;  // User-supplied channel buffer size

  mutable absl::Mutex mu_;
  // chunked_queue doesn't work with over aligned types, so we use std::deque
  // for those.
  std::conditional_t<(alignof(T) > alignof(std::max_align_t)), std::deque<T>,
                     absl::chunked_queue<T>>
      queue_ ABSL_GUARDED_BY(mu_);
  bool closed_ ABSL_GUARDED_BY(mu_);

  struct Rd final : public Selectable {
    ChannelState* state;
    explicit Rd(ChannelState* s) : state(s) {}
    bool Handle(CaseState* reader, bool enqueue) override;
    void Unregister(CaseState* c) override {
      absl::MutexLock l(state->mu_);
      internal::RemoveFromList(&state->waiting_readers_, c);
    }
  };
  Rd rd_;

  struct Wr final : public Selectable {
    ChannelState* state;
    explicit Wr(ChannelState* s) : state(s) {}
    bool Handle(CaseState* writer, bool enqueue) override;
    void Unregister(CaseState* c) override {
      absl::MutexLock l(state->mu_);
      internal::RemoveFromList(&state->waiting_writers_, c);
    }
  };
  Wr wr_;

  static T CopyOut(T* item) { return *static_cast<const T*>(item); }

  static T MoveOut(T* item) { return std::move(*item); }

  static T CopyOrMoveOut(T* item, intptr_t arg2) {
    return reinterpret_cast<T (*)(T*)>(arg2)(item);
  }

  T* Front() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) { return &queue_.front(); }

  bool Invariants() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
};

template <typename T>
ChannelState<T>::~ChannelState() {
  absl::MutexLock l(
      mu_);  // Must synchronize with remote operations (e.g. Close()).
  DCHECK(Invariants());
}

template <typename T>
bool ChannelState<T>::Rd::Handle(CaseState* reader, bool enqueue) {
  ChannelState* ch = state;
  absl::MutexLock l(ch->mu_);
  DCHECK(ch->Invariants());

  T* dst_item = reinterpret_cast<T*>(reader->params->arg1);
  bool* dst_ok = reinterpret_cast<bool*>(reader->params->arg2);

  // Is there a buffered item to read?
  if (!ch->queue_.empty()) {
    DVLOG(2) << "Get from buffer";
    reader->sel->mu.lock();
    if (reader->sel->picked == Selector::kNonePicked) {
      // Move out of the buffer. Explicitly destruct behind for types that don't
      // have a move-assignment operator and where it may be harmful to leave
      // around a copy. (For example, a shared_ptr-like object with only a copy-
      // assignment operator.)
      *dst_item = std::move(ch->queue_.front());
      ch->queue_.pop_front();
      *dst_ok = true;
      ch->UnlockAndReleaseReader(reader);

      // Potentially admit a waiting writer.
      CaseState* unblocked_writer;
      if (ch->GetWaitingWriter(&unblocked_writer)) {
        T* item = reinterpret_cast<T*>(unblocked_writer->params->arg1);
        ch->queue_.push_back(
            CopyOrMoveOut(item, unblocked_writer->params->arg2));
        ch->UnlockAndReleaseWriter(unblocked_writer);
      }
    } else {
      // While we weren't technically able to proceed, there's no point in
      // Select() processing further cases, so we'll still return true below.
      reader->sel->mu.unlock();
    }
    DCHECK(ch->Invariants());
    return true;
  }

  // Try to transfer directly from waiting writer to reader
  CaseState* writer;
  if (ch->GetMatchingWriter(reader, &writer)) {
    T* item = reinterpret_cast<T*>(writer->params->arg1);
    *dst_item = CopyOrMoveOut(item, writer->params->arg2);
    *dst_ok = true;
    ch->UnlockAndReleaseReader(reader);
    ch->UnlockAndReleaseWriter(writer);
    DCHECK(ch->Invariants());
  }

  reader->sel->mu.lock();
  // We must guarantee that this case is eligible to proceed before any
  // side-effects can occur.
  if (reader->sel->picked != Selector::kNonePicked) {
    reader->sel->mu.unlock();
    // Already handled item
    DVLOG(2) << "Read cancelled since another selector case done";
    DCHECK(ch->Invariants());
    return true;
  }

  if (ch->closed_) {
    DVLOG(2) << "Read failing because channel closed";
    *dst_ok = false;
    ch->UnlockAndReleaseReader(reader);
    return true;
  }

  if (enqueue) {
    // Register with waiting readers
    DVLOG(2) << "Read waiting";
    internal::PushBack(&ch->waiting_readers_, reader);
  }

  reader->sel->mu.unlock();
  DCHECK(ch->Invariants());
  return false;
}

template <typename T>
bool ChannelState<T>::Wr::Handle(CaseState* writer, bool enqueue) {
  ChannelState* ch = state;
  absl::MutexLock l(ch->mu_);
  DCHECK(ch->Invariants());
  CHECK(!ch->closed_) << "Calling Write() on closed channel";

  // First try to transfer directly from writer to a waiting reader
  CaseState* reader;
  if (ch->GetMatchingReader(writer, &reader)) {
    T* writer_item = reinterpret_cast<T*>(writer->params->arg1);
    T* reader_item = reinterpret_cast<T*>(reader->params->arg1);
    *reader_item = CopyOrMoveOut(writer_item, writer->params->arg2);
    *reinterpret_cast<bool*>(reader->params->arg2) = true;
    ch->UnlockAndReleaseReader(reader);
    ch->UnlockAndReleaseWriter(writer);
    DCHECK(ch->Invariants());
    return true;
  }

  writer->sel->mu.lock();
  // We must guarantee that this case is eligible to proceed before any
  // side-effects can occur.
  if (writer->sel->picked != Selector::kNonePicked) {
    writer->sel->mu.unlock();
    // Already handled item
    DVLOG(2) << "Write cancelled since another selector case done";
    DCHECK(ch->Invariants());
    return true;
  }

  // Is there room to buffer item?
  if (ch->queue_.size() < ch->capacity_) {
    DVLOG(2) << "Add to buffer";
    ch->queue_.push_back(CopyOrMoveOut(
        reinterpret_cast<T*>(writer->params->arg1), writer->params->arg2));
    ch->UnlockAndReleaseWriter(writer);
    DCHECK(ch->Invariants());
    return true;
  }

  if (enqueue) {
    // Register with waiting writers
    DVLOG(2) << "Write waiting";
    internal::PushBack(&ch->waiting_writers_, writer);
  }

  writer->sel->mu.unlock();
  DCHECK(ch->Invariants());
  return false;
}

template <typename T>
bool ChannelState<T>::Invariants() const {
  // Use CHECK since if the caller wants no prod failures, they will
  // call DCHECK(Invariants()) and not get in here in prod mode.
  CHECK_LE(queue_.size(), capacity_);
  return true;
}

}  // namespace internal
}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_CHANNEL_INTERNAL_H_
