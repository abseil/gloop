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

// A very specialized data structure.  Collects a large number of items
// (fast, in parallel) then removes them all (serial, not as fast).
//
// Not directly specific to RCU, but very well suited to the needs
// of the RCU implementation.
//
// Do not use this unless you really, really know what you're
// doing. Read the comments and consult the author.
#ifndef THIRD_PARTY_GLOOP_CONCURRENT_RCU_PILE_H_
#define THIRD_PARTY_GLOOP_CONCURRENT_RCU_PILE_H_

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <type_traits>

#include "absl/base/internal/raw_logging.h"
#include "absl/numeric/bits.h"
#include "gloop/base/percpu.h"
#include "tcmalloc/internal/sysinfo.h"

namespace base {
namespace rcu {

// Defines an array of pages on each CPU, dynamically backed as
// needed. Async-signal-safe.
class PercpuGrowingArray {
 public:
  // Requires manual initialization/destruction--call Init exactly once before
  // any other methods, and call Destroy() to free associated resources.
  constexpr PercpuGrowingArray() : slices_{::base::subtle::percpu::Handle()} {}
  void Init();
  void Destroy();
  ~PercpuGrowingArray() = default;

  // Make sure the <i>th page has been allocated on <cpu>.
  void Ensure(size_t page, int cpu);
  // Get a pointer to the <i>th page on <cpu>, which had better exist.
  void* GetPage(size_t page, int cpu);

  size_t SliceSize(int i) const { return kPageSize << i; }
#if defined(__x86_64__) || defined(ARCH_ARM) || defined(__aarch64__) || \
    defined(__riscv)
  static constexpr size_t kNumSlices = 20;
  static constexpr size_t kPageSize = 4096;
#elif defined(ARCH_PPC) || defined(__wasm__)
  static const size_t kNumSlices = 16;
  static const size_t kPageSize = 65536;
#else
#error Unsupported architecture
#endif

  // total # of page
  static constexpr size_t kPages = (1ul << kNumSlices) - 1;
  // The percpu pages are allocated in chunks of
  // SliceSize(0), SliceSize(1), SliceSize(2), ... SliceSize(kNumSlices - 1).
  // Free all the pages on <cpu> in the SliceSize(<i>) chunk.
  // (This allocation strategy is comparable to a flat, resized array, but
  // never has to realloc/copy (for signal safety.)
  void FreeSlice(int i, int cpu);

  // Which slice does a page live on?
  static constexpr size_t PageToSlice(size_t page) {
    return Log2Floor(page + 1);
  }

  PercpuGrowingArray(const PercpuGrowingArray& rhs) = delete;
  PercpuGrowingArray& operator=(const PercpuGrowingArray& rhs) = delete;

 private:
  // We essentially want a flat array of pages that grows like vector,
  // but we can't use malloc or locks (signal safety!). So we grow the
  // array logarithmically, but to avoid any need to copy, keep all
  // the previous smaller ones.  The logically flat array is stored
  // in a logarithmic number of chunks, each chunk twice the size of
  // the last, allocated as-needed.  slices_[i] holds (if anything)
  // 2^i pages of usable space.  Together they define an array of size
  // up to (2^20-1) pages.
  ::base::subtle::percpu::Handle slices_[kNumSlices];
  static constexpr size_t Log2Floor(size_t x) {
    // Communicate to the compiler that x cannot be zero and allow it to
    // optimize away the slow path of absl::bit_width.
    if (x == 0) __builtin_unreachable();
    return absl::bit_width(x) - 1;
  }

  void EnsureSliceSlow(std::atomic<int64_t>* loc, size_t slice, int cpu);
};

static_assert(std::is_trivially_destructible<PercpuGrowingArray>::value,
              "PercpuGrowingArrays must be trivially destructible to avoid "
              "MSan use-after-dtor reports.");

// The actual data structure (uses the above as backing.)
// T must be copyable and should be POD.
template <typename T>
class Pile {
 public:
  // Requires manual initialization/destruction--call Init exactly once before
  // any other methods, and call Destroy() to free associated resources.
  constexpr Pile() : arr_(), n_{}, highwater_{} {}
  void Init();
  void Destroy();

  // Add an item to the pile. Very fast. Async signal safe.
  // REQUIRES: all calls to Iterate either happen-before or happen-after Add.
  void Add(T t);
  // Have any items been Add()ed since the last Iterate?
  bool empty() const;
  // Call f(t) for each item added to the Pile.
  // REQUIRES: all calls to Add either happen-before or happen-after
  // Iterate.
  void Iterate(void (*f)(T cd));

  Pile(const Pile& rhs) = delete;
  Pile& operator=(const Pile& rhs) = delete;

 private:
  // The implementation is essentially a flat array of items on each
  // CPU (implemented via PercpuGrowingArray.) Add reserves a spot in
  // the local CPU's array and writes to it; iterate empties each
  // array. The trick comes in that we need to be async signal safe
  // (thus can't malloc or use locks), and also don't want to
  // preallocate a giant array.
  PercpuGrowingArray arr_;

  ::base::subtle::percpu::Handle n_;  // number of items Added from each CPU
  // Last slice _unused_ in previous round.
  ::base::subtle::percpu::Handle highwater_;

  // these are little functions because we can't properly define static consts
  // in a template class:

  // To simplify arithmetic later, we don't overlap items over
  // pages. (In practice item size always divides kPageSize so it doesn't
  // matter.)
  constexpr size_t items_per_page() const {
    return PercpuGrowingArray::kPageSize / sizeof(T);
  }
  // max per cpu. We should never even approach this.
  constexpr size_t max_items_per_cpu() const {
    return items_per_page() * PercpuGrowingArray::kPages;
  }
};

// Half open range (for range-for)
class Range {
 public:
  explicit Range(int n) : Range(0, n) {}
  Range(int i, int j) : i_(i), j_(j) {}

  class IntIterator : public std::iterator<std::forward_iterator_tag, int> {
   public:
    explicit IntIterator(int i) : i_(i) {}
    bool operator==(const IntIterator& rhs) const { return i_ == rhs.i_; }
    bool operator!=(const IntIterator& rhs) const { return i_ != rhs.i_; }
    IntIterator& operator++() {
      i_++;
      return *this;
    }

    int operator*() const { return i_; }

   private:
    int i_;
  };

  IntIterator begin() { return IntIterator(i_); }
  IntIterator end() { return IntIterator(j_); }

 private:
  int i_, j_;
};

inline void PercpuGrowingArray::Ensure(size_t page, int cpu) {
  size_t slice = PageToSlice(page);
  std::atomic<int64_t>* loc = GetPointerAtomic(slices_[slice], cpu);
  if (loc->load(std::memory_order_acquire) != 0) return;
  EnsureSliceSlow(loc, slice, cpu);
}

inline void* PercpuGrowingArray::GetPage(size_t page, int cpu) {
  size_t slice = PageToSlice(page);
  int64_t raw =
      GetPointerAtomic(slices_[slice], cpu)->load(std::memory_order_relaxed);
  // Subtract off all the pages in earlier slices.
  page -= (1ul << slice) - 1;
  raw += kPageSize * page;
  return reinterpret_cast<void*>(raw);
}

template <typename T>
void Pile<T>::Destroy() {
  FreeHandle(n_);
  FreeHandle(highwater_);
  arr_.Destroy();
}

template <typename T>
inline void Pile<T>::Init() {
  using ::base::subtle::percpu::AllocHandle;
  n_ = AllocHandle();
  highwater_ = AllocHandle();
  for (int cpu : Range(tcmalloc::tcmalloc_internal::NumCPUs())) {
    GetPointerAtomic(highwater_, cpu)
        ->store(PercpuGrowingArray::kNumSlices, std::memory_order_relaxed);
  }
  arr_.Init();
}

template <typename T>
inline void Pile<T>::Add(T t) {
  using ::base::subtle::percpu::CompareAndSwap;
  using ::base::subtle::percpu::GetCurrentCpu;
  int cpu;
  size_t i;
  // This should be a AtomicIncrement which is moderately faster, but
  // the API doesn't let you get both cpu + index.  It's totally
  // possible to do so but sadly we haven't committed a better return
  // type. :(
  while (true) {
    cpu = GetCurrentCpu();
    std::atomic<int64_t>* loc = GetPointerAtomic(n_, cpu);
    i = *loc;
    if (cpu == CompareAndSwap(cpu, loc, i, i + 1)) {
      break;
    }
  }
  // This is basically impossible to trigger, you'd OOM first.
  // We allow about a million pages, several hundred items per page,
  // and surely each item represents at least a few bytes of stranded memory.
  ABSL_RAW_CHECK(i < max_items_per_cpu(),
                 "used too many items in a Pile. "
                 "I am truly impressed this triggered.");
  // We now own the i-th entry in the array on <cpu>.  The trick is
  // finding that entry.
  size_t page = i / items_per_page();
  size_t j = i % items_per_page();
  arr_.Ensure(page, cpu);
  T* p = static_cast<T*>(arr_.GetPage(page, cpu));
  // Note that we require Iterate to wait until all these Adds are
  // happens-before it; so we don't need to worry about any ordering
  // issues on t.
  p[j] = t;
  std::atomic_thread_fence(std::memory_order_release);
}

template <typename T>
inline bool Pile<T>::empty() const {
  for (int cpu : Range(tcmalloc::tcmalloc_internal::NumCPUs())) {
    if (GetPointerAtomic(n_, cpu)->load(std::memory_order_relaxed) != 0) {
      return false;
    }
  }
  return true;
}

template <typename T>
inline void Pile<T>::Iterate(void (*f)(T t)) {
  const size_t kItemsPerPage = items_per_page();
  for (int cpu : Range(tcmalloc::tcmalloc_internal::NumCPUs())) {
    size_t n = GetPointerAtomic(n_, cpu)->load(std::memory_order_acquire);
    size_t page_end = n / kItemsPerPage + 1;
    size_t remaining = n;
    for (size_t page = 0; page < page_end; ++page) {
      T* p = static_cast<T*>(arr_.GetPage(page, cpu));
      size_t limit = remaining < kItemsPerPage ? remaining : kItemsPerPage;
      for (size_t j = 0; j < limit; ++j) {
        f(p[j]);
      }
      remaining -= limit;
    }
    if (remaining != 0) {
      ABSL_RAW_LOG(DFATAL, "Unexpected remainder of %zd found: %zd %zd %zd",
                   remaining, n, page_end, kItemsPerPage);
    }
    // Slices past n and highwater haven't been used for 2
    // iterations. Get rid of them.
    size_t high =
        GetPointerAtomic(highwater_, cpu)->load(std::memory_order_relaxed);
    size_t unused = arr_.PageToSlice(n) + 1;
    for (int i = std::max(unused, high); i < PercpuGrowingArray::kNumSlices;
         ++i) {
      arr_.FreeSlice(i, cpu);
    }
    GetPointerAtomic(highwater_, cpu)->store(unused, std::memory_order_relaxed);
    GetPointerAtomic(n_, cpu)->store(0, std::memory_order_relaxed);
  }
}

}  // namespace rcu
}  // namespace base

#endif  // THIRD_PARTY_GLOOP_CONCURRENT_RCU_PILE_H_
