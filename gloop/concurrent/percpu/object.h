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

#ifndef THIRD_PARTY_GLOOP_CONCURRENT_PERCPU_OBJECT_H_
#define THIRD_PARTY_GLOOP_CONCURRENT_PERCPU_OBJECT_H_

// This file defines PerCpu<T>, a templated class that contains a T and a lock
// per CPU. Conceptually, therefore, this models:
//
//   std::array<websitetools::feeds::synchronized<T>, NumCPUs()>
//
// You can get() a T for the current CPU, and you can iterate over all Ts.

#include <cstddef>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/base/internal/raw_logging.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "gloop/base/internal/percpu.inc"
#include "gloop/base/percpu.h"
#include "gloop/base/sysinfo.h"

#ifndef PERCPU_USE_RSEQ_GOTO
#error "PERCPU_USE_RSEQ_GOTO must be defined"
#endif

#if PERCPU_USE_RSEQ_GOTO  // RSEQ CpuSpinLock functions require asm goto
#include "gloop/concurrent/percpu/object_native.inc"
#else
#include "gloop/concurrent/percpu/object_emulated.inc"
#endif

#include "absl/container/fixed_array.h"

namespace concurrent {
namespace percpu {

// Internal detail. Ignore this section.
namespace percpu_internal {
struct Unlocker;

template <typename T>
using pointer = std::unique_ptr<T, Unlocker>;
struct RemoteUnlocker;

template <typename T>
using remote_pointer = std::unique_ptr<T, RemoteUnlocker>;
template <typename PerCpuType, typename T>
class iterator;
}  // namespace percpu_internal

template <typename T>
class PerCpu {
 public:
  template <typename... Args>
  explicit PerCpu(Args... args);
  ~PerCpu();

  PerCpu(PerCpu&&) = delete;
  PerCpu(const PerCpu&) = delete;
  PerCpu& operator=(PerCpu&&) = delete;
  PerCpu& operator=(const PerCpu&) = delete;

  [[nodiscard]] std::size_t size() const { return array_.size(); }
  [[nodiscard]] bool empty() const { return false; }

  // |pointer| represents a smart pointer to a single per-CPU T, along with an
  // implicit lock for the lifetime of the pointer. Think of a pointer as a
  // lock object and the T* itself.
  //
  // Using pointer requires a bit of care. If you are not careful, you can
  // deadlock yourself. Imagine:
  //
  //   auto p1 = percpu.get();
  //   auto p2 = percpu.get();  // Deadlock.
  //
  // Advice: don't name the result of get(). Instead, use it in the same
  // statement, like:
  //
  //   *percpu.get() += 3;
  //
  // Since 'pointer' represents both a lock and a pointer, you cannot copy it.
  // It is movable, though.
  typedef percpu_internal::pointer<T> pointer;
  typedef percpu_internal::pointer<const T> const_pointer;

  // Returns a pointer to a per-CPU T that is likely for the CPU that this
  // thread is running on. Note that there is no guarantee about which CPU's T
  // you receive; by the time this returns, the thread may already have moved to
  // another CPU. Therefore, do not rely on the particular CPU chosen for
  // correctness, only for probabilistic performance.
  [[nodiscard]] pointer get();
  [[nodiscard]] const_pointer get() const;

  // |iterator| represents a smart iterator through the set of per-CPU T's,
  // along with an implicit lock for the per-CPU T for the time the iterator is
  // positioned at it. Think of an iterator as a lock object and the T* itself,
  // along with the ability to advance to another per-CPU T. That is:
  // conceptually, calling begin() acquires a lock, calling ++iterator releases
  // one per-CPU lock and acquires another.
  //
  // iteration is *much* slower than get(), above. Only use this for
  // aggregation/work-stealing purposes, for example.
  //
  // Using iterator requires a bit of care. If you are not careful, you can
  // deadlock yourself. Imagine:
  //
  //   auto it1 = percpu.begin();
  //   ++it1;
  //   auto it2 = percpu.begin();
  //   ++it2;  // Deadlock.
  //
  // Advice: keep your loops very short and focused. Range-based for is best:
  //
  //   for (const auto& e : percpu) {
  //     total += e;
  //   }
  //
  // Since 'iterator' represents both a lock and a pointer, you cannot copy it.
  // It is movable, though.
  typedef percpu_internal::iterator<PerCpu, T> iterator;
  typedef percpu_internal::iterator<const PerCpu, const T> const_iterator;
  iterator begin();
  iterator end();
  const_iterator begin() const;
  const_iterator end() const;

  // remote_get(base::subtle::percpu::GetCurrentCpu()) is logically equivalent
  // to get(), but much slower. The results you get from these two methods are
  // generally for different CPUs than the one the current thread is running on.
  // You may use this as part of iteration over the per-CPU T's, though
  // generally the iterator interface above will be less error-prone. This will
  // be far more expensive than calling get().
  typedef percpu_internal::remote_pointer<T> remote_pointer;
  typedef percpu_internal::remote_pointer<const T> const_remote_pointer;
  remote_pointer remote_get(std::size_t cpu);
  const_remote_pointer remote_get(std::size_t cpu) const;

 private:
  T* ptr(std::size_t cpu) { return reinterpret_cast<T*>(&array_[cpu]); }
  const T* ptr(std::size_t cpu) const {
    return reinterpret_cast<const T*>(&array_[cpu]);
  }

  typedef typename std::aligned_storage<
      sizeof(T), (alignof(T) + ABSL_CACHELINE_SIZE - 1) &
                     ~(ABSL_CACHELINE_SIZE - 1)>::type aligned_type;
  static_assert(alignof(aligned_type) >= alignof(T),
                "aligned_type is insufficiently aligned.");

  mutable percpu_internal::PerCpuLock lock_;
  absl::FixedArray<aligned_type, 0> array_;
};

// Implementation follows.
namespace percpu_internal {
struct Unlocker {
  PerCpuLock* lock;
  std::size_t cpu;

  template <typename T>
  void operator()(T* ptr) const ABSL_NO_THREAD_SAFETY_ANALYSIS {
    if (ptr) {
      lock->Unlock(cpu);
    }
  }
};

struct RemoteUnlocker {
  PerCpuLock* lock;
  std::size_t cpu;

  template <typename T>
  void operator()(T* ptr) const ABSL_NO_THREAD_SAFETY_ANALYSIS {
    if (ptr) {
      lock->UnlockOn(cpu);
    }
  }
};

template <typename PerCpuType, typename T>
class iterator : public std::iterator<std::random_access_iterator_tag, T> {
 private:
  typedef typename std::iterator<std::random_access_iterator_tag, T> superclass;

 public:
  typedef typename superclass::reference reference;
  typedef typename superclass::pointer pointer;
  typedef typename superclass::difference_type difference_type;

  [[nodiscard]] reference operator*() const { return *pointer_.get(); }
  [[nodiscard]] pointer operator->() const { return pointer_.get(); }

  iterator& operator++() {
    increment();
    return *this;
  }

  iterator& operator--() {
    decrement();
    return *this;
  }

  // We don't support these because our iterators aren't safely copyable.
  // iterator operator++(int);
  // iterator operator--(int);
  // iterator operator-(difference_type n) const;

  iterator& operator+=(difference_type n) {
    advance(n);
    return *this;
  }
  iterator& operator-=(difference_type n) {
    advance(-n);
    return *this;
  }

  iterator() = default;

  iterator(iterator&& other) noexcept { *this = std::move(other); }

  iterator& operator=(iterator&& other) noexcept {
    if (this != &other) {
      parent_ = other.parent_;
      other.parent_ = nullptr;
      cpu_ = other.cpu_;
      other.cpu_ = 0;
      pointer_ = std::move(other.pointer_);
    }
    return *this;
  }

  difference_type operator-(const iterator& other) const {
    return -distance_to(other);
  }

  bool operator==(const iterator& other) const { return equal(other); }
  bool operator!=(const iterator& other) const { return !equal(other); }
  ~iterator() = default;

  iterator(const iterator&) = delete;
  iterator& operator=(const iterator&) = delete;

 private:
  friend PerCpuType;

  iterator(PerCpuType* parent, std::size_t cpu)
      : parent_(parent), cpu_(cpu), pointer_(parent_->remote_get(cpu)) {}

  void increment() { advance(1); }

  void decrement() { advance(-1); }

  void advance(std::ptrdiff_t n) {
    pointer_.reset();
    cpu_ += n;
    pointer_ = parent_->remote_get(cpu_);
  }

  std::ptrdiff_t distance_to(const iterator& other) const {
    return other.cpu_ - cpu_;
  }

  bool equal(const iterator& other) const {
    // Since each iterator represents a lock against an object, the only way two
    // iterators can be equal is if both are nullptr.
    return !pointer_ && !other.pointer_;
  }

  PerCpuType* parent_ = nullptr;
  std::size_t cpu_ = 0;
  remote_pointer<T> pointer_;
};
}  // namespace percpu_internal

template <typename T>
template <typename... Args>
PerCpu<T>::PerCpu(Args... args)
    : lock_(), array_(base::subtle::percpu::NumCPUs()) {
  for (std::size_t i = 0; i != size(); ++i) {
    new (&array_[i]) T(args...);
  }
}

template <typename T>
PerCpu<T>::~PerCpu() {
  for (std::size_t i = 0; i != size(); ++i) {
    ptr(i)->~T();
  }
}

template <typename T>
typename PerCpu<T>::pointer PerCpu<T>::get() ABSL_NO_THREAD_SAFETY_ANALYSIS {
  const std::size_t cpu = lock_.Lock();
  if (ABSL_PREDICT_FALSE(!(cpu < size()))) {
    // TODO: debugging these unexpected failures
    int num_online_cpus = NumCPUs();
    ABSL_RAW_LOG(FATAL, "Cpu index out of bounds (%zu < %d/%zu).", cpu,
                 num_online_cpus, size());
  }
  return pointer(ptr(cpu), percpu_internal::Unlocker{&lock_, cpu});
}

template <typename T>
typename PerCpu<T>::const_pointer PerCpu<T>::get() const
    ABSL_NO_THREAD_SAFETY_ANALYSIS {
  const std::size_t cpu = lock_.Lock();
  if (ABSL_PREDICT_FALSE(!(cpu < size()))) {
    // TODO: debugging these unexpected failures
    int num_online_cpus = NumCPUs();
    ABSL_RAW_LOG(FATAL, "Cpu index out of bounds (%zu < %d/%zu).", cpu,
                 num_online_cpus, size());
  }
  return const_pointer(ptr(cpu), percpu_internal::Unlocker{&lock_, cpu});
}

template <typename T>
typename PerCpu<T>::iterator PerCpu<T>::begin() {
  return iterator(this, 0);
}

template <typename T>
typename PerCpu<T>::iterator PerCpu<T>::end() {
  return iterator(this, size());
}

template <typename T>
typename PerCpu<T>::const_iterator PerCpu<T>::begin() const {
  return const_iterator(this, 0);
}

template <typename T>
typename PerCpu<T>::const_iterator PerCpu<T>::end() const {
  return const_iterator(this, size());
}

template <typename T>
typename PerCpu<T>::remote_pointer PerCpu<T>::remote_get(std::size_t cpu)
    ABSL_NO_THREAD_SAFETY_ANALYSIS {
  if (cpu >= 0 && cpu < size()) {
    lock_.LockOn(cpu);
    return remote_pointer(ptr(cpu),
                          percpu_internal::RemoteUnlocker{&lock_, cpu});
  } else {
    return nullptr;
  }
}

template <typename T>
typename PerCpu<T>::const_remote_pointer PerCpu<T>::remote_get(
    std::size_t cpu) const ABSL_NO_THREAD_SAFETY_ANALYSIS {
  if (cpu >= 0 && cpu < size()) {
    lock_.LockOn(cpu);
    return const_remote_pointer(ptr(cpu),
                                percpu_internal::RemoteUnlocker{&lock_, cpu});
  } else {
    return nullptr;
  }
}

}  // namespace percpu
}  // namespace concurrent

#endif  // THIRD_PARTY_GLOOP_CONCURRENT_PERCPU_OBJECT_H_
