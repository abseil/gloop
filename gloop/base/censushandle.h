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

// These classes are part of the Census project.

#ifndef THIRD_PARTY_GLOOP_BASE_CENSUSHANDLE_H_
#define THIRD_PARTY_GLOOP_BASE_CENSUSHANDLE_H_

#include <atomic>
#include <cstdint>
#include <memory>

#include "absl/base/attributes.h"

class CensusHandle;

namespace stats_census {
class CensusHandleManager;
bool IsDefaultHandle(const CensusHandle&);
}  // namespace stats_census

namespace base::subtle {
class WithLifetimeBoundContext;
}  // namespace base::subtle

// This class defines an opaque handle, CensusHandle, that is created inside
// Census library. This handle is embedded inside TraceContext and gets copied
// as part of the NewCallback mechanism. It is only interpreted by Census
// library. See stats/census/public/census-interface.h for details.
// CensusHandle is thread-compatible.
class CensusHandle {
  // This class carefully synchronizes with the SIGPROF signal handler. Any
  // operation which mutates a `CensusHandle` by reference or pointer might be
  // modifying the thread-local CensusHandle. Any subsequent `Unref()` can race
  // with the signal handler. As such, any call to `Unref()` should first use a
  // `std::atomic_signal_fence` to ensure that the signal handler sees all
  // earlier mutations before the `Unref()`.
 public:
  constexpr CensusHandle() = default;

  CensusHandle(const CensusHandle& other)
      : entry_([&] {
          // cache `other.get_entry()` to avoid redundant loads due
          // to compilers not caching atomic loads.
          auto* entry = other.get_entry();
          if (entry != nullptr) entry->Ref();
          return entry;
        }()) {}

  CensusHandle& operator=(const CensusHandle& other) {
    EntryBase* old_entry = get_entry();
    EntryBase* other_entry = other.get_entry();
    if (old_entry == other_entry) return *this;
    set_entry(other_entry);

    // As CensusHandle is thread-compatible and not thread-safe, we don't need
    // to handle users concurrently doing reads and writes of a CensusHandle
    // from different threads; users will need to do their own synchronization
    // in that case. We need to make sure that when the sigprof handler
    // interrupts this function, and reads the old value of rep_, then the code
    // below to unref old cannot have started. atomic_signal_fence (as opposed
    // to an atomic_thread_fence) expresses this constraint directly and allows
    // the compiler to enforce it as efficiently as it can.
    std::atomic_signal_fence(std::memory_order_seq_cst);

    if (other_entry != nullptr) other_entry->Ref();
    if (old_entry != nullptr) old_entry->Unref();
    return *this;
  }

#ifndef SWIG
  CensusHandle(CensusHandle&& other) noexcept : entry_(other.get_entry()) {
    other.set_entry(nullptr);
  }

  CensusHandle& operator=(CensusHandle&& other) noexcept {
    if (this == &other) return *this;
    EntryBase* old_entry = get_entry();
    EntryBase* other_entry = other.get_entry();
    set_entry(other_entry);
    other.set_entry(nullptr);

    // As CensusHandle is thread-compatible and not thread-safe, we don't need
    // to handle users concurrently doing reads and writes of a CensusHandle
    // from different threads; users will need to do their own synchronization
    // in that case. We need to make sure that when the sigprof handler
    // interrupts this function, and reads the old value of entry_, then the
    // code below to unref old cannot have started. atomic_signal_fence (as
    // opposed to an atomic_thread_fence) expresses this constraint directly and
    // allows the compiler to enforce it as efficiently as it can.
    std::atomic_signal_fence(std::memory_order_seq_cst);

    if (old_entry != nullptr) old_entry->Unref();
    return *this;
  }
#endif

  ~CensusHandle() {
    if (EntryBase* e = get_entry(); e != nullptr) {
      // As CensusHandle is thread-compatible and not thread-safe, we don't need
      // to handle users concurrently doing reads and writes of a CensusHandle
      // from different threads; users will need to do their own synchronization
      // in that case. We need to make sure that when the sigprof handler
      // interrupts this function, and reads the old value of entry_, then the
      // code below to unref old cannot have started. atomic_signal_fence (as
      // opposed to an atomic_thread_fence) expresses this constraint directly
      // and allows the compiler to enforce it as efficiently as it can.
      std::atomic_signal_fence(std::memory_order_seq_cst);
      e->Unref();
    }
  }

  void Swap(CensusHandle* other) {
    EntryBase* tmp = get_entry();
    set_entry(other->get_entry());
    other->set_entry(tmp);
  }

  // A swap implementation for CensusHandle that is more efficient than
  // std::swap.
  friend void swap(CensusHandle& h1, CensusHandle& h2) noexcept {
    h1.Swap(&h2);
  }

 private:
  friend class base::subtle::WithLifetimeBoundContext;
  friend class stats_census::CensusHandleManager;
  friend bool ::stats_census::IsDefaultHandle(const CensusHandle&);
  friend class TestEntry;  // Defined/used in censushandle_test.cc.

  // EntryBase is a base class used in the new Census implementation. It
  // implements similar reference counting logic to
  // util/refcount/reference_counted.cc, but uses 8 bytes instead of 16.
  //
  // EntryBase should only be used by Census; other code should use CensusHandle
  // or call functions in the Census library.
  class EntryBase {
   public:
    EntryBase() : rc_(1) {}
    virtual ~EntryBase() {}

   protected:
    // Increments the reference count.
    void Ref() const { rc_.fetch_add(1, std::memory_order_relaxed); }

    // Decrements the reference count and returns true if it is the last
    // reference.
    ABSL_MUST_USE_RESULT bool UnrefNoDelete() const {
      // If we read rc_ and it is 1, this is the only reference.  We can avoid
      // doing the decrement in that case, and just delete.
      // Note: load is much faster than fetch_sub, so doing the read makes the
      // last Unref much faster, but the others slightly slower. This is a net
      // cycle win when there are only a few Unref calls per EntryBase object.
      if (rc_.load(std::memory_order_acquire) == 1) {
        return true;
      }
      intptr_t rc_old = rc_.fetch_sub(1, std::memory_order_acq_rel);
      return rc_old == 1;
    }

    // Decrements the reference count and deletes `this` if it is the last
    // reference.
    void Unref() const {
      if (UnrefNoDelete()) delete this;
    }

   private:
    // The reference count.
    mutable std::atomic<intptr_t> rc_;
    friend class ::CensusHandle;
    friend class TestEntry;
  };

#ifndef SWIG
  explicit CensusHandle(std::unique_ptr<EntryBase> e) : entry_(e.release()) {}
#endif

  // Returns the current value of `entry` using a relaxed atomic load.
  EntryBase* get_entry() const {
    return entry_.load(std::memory_order_relaxed);
  }

  // Sets the current value of `entry` using a relaxed atomic store.
  void set_entry(EntryBase* e) { entry_.store(e, std::memory_order_relaxed); }

  // Atomic to be read from signal handlers (SIGPROF).
  std::atomic<EntryBase*> entry_ = {nullptr};
  static_assert(std::atomic<EntryBase*>::is_always_lock_free);
};

#endif  // THIRD_PARTY_GLOOP_BASE_CENSUSHANDLE_H_
