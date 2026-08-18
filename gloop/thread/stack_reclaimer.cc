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

#include "gloop/thread/stack_reclaimer.h"

#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <string>
#include <utility>

#include "absl/base/call_once.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/sysinfo.h"
#include "gloop/base/walltime.h"
#include "gloop/thread/timedcall.h"

ABSL_FLAG(bool, thread_reclaim_stacks, true,
          "When supported, enable reclaim of unused thread stack memory.");

namespace thread {
namespace internal {

// Must be implemented by supported architectures
// NOTE: Future GTREs will export USER_REDZONE_SIZE via ptrace.h.  Until then we
// provide red_zone_size().
struct StackProperties {
  static intptr_t current_sp();   // Current stack pointer.
  static size_t red_zone_size();  // Zero if no red-zone exists.
  static bool grows_up();         // True iff the stack grows up.
};

// Start of architecture specific support.
#if defined(__GNUC__) && defined(__linux__)
#if defined(__x86_64__)
#define STACK_RECLAIM_SUPPORTED 1
// static
inline intptr_t StackProperties::current_sp() {
  intptr_t current_stack_ptr;
  __asm__ __volatile__("mov %%rsp, %0\n" : "=a"(current_stack_ptr) : :);
  return current_stack_ptr;
}

// static
inline size_t StackProperties::red_zone_size() { return 128; }
// static
inline bool StackProperties::grows_up() { return false; }
#elif defined(ARCH_PPC)
#define STACK_RECLAIM_SUPPORTED 1
// static
inline intptr_t StackProperties::current_sp() {
  intptr_t current_stack_ptr = 0;
  // PowerPC does not have an explicit stack register.  r1 is used by
  // convention.  r3 is the standard return register, but we leave this at the
  // compilers discretion in our constraint (likely to be inlined anyway).
  //
  // Note: PPC assembly uses "dst, src" not "src, dst".
  __asm__ __volatile__("mr %0, %%r1\n" : "=r"(current_stack_ptr) : :);
  return current_stack_ptr;
}

// static
inline size_t StackProperties::red_zone_size() {
  // ELFv2 little-endian ABI defines a 512-byte red-zone.
  return 512;
}
// static
inline bool StackProperties::grows_up() { return false; }
#endif
#endif  // End of possibly supported platforms for GNUC/Linux.

#ifndef STACK_RECLAIM_SUPPORTED  // Unsupported platform or environment.
#define STACK_RECLAIM_SUPPORTED 0
intptr_t StackProperties::current_sp() { LOG(FATAL) << "Unsupported"; }
size_t StackProperties::red_zone_size() { LOG(FATAL) << "Unsupported"; }
bool StackProperties::grows_up() { LOG(FATAL) << "Unsupported"; }
#endif

// The Implementation below is generic (beyond POSIX dependency) provided
// architecture stubs above have been implemented.
static inline intptr_t RoundAwayFromActiveStack(intptr_t addr, int grows_up) {
  // When the stack is growing up, we must always start from the next page, and
  // vice versa (terminating at the current page) when growing down.
  // NOTE: Page sizes are always powers of 2.
  static const size_t kPageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  static const size_t kPageMask = ~(kPageSize - 1);
  const size_t kRoundingCorrection = grows_up ? kPageSize : 0;

  return (addr + kRoundingCorrection) & kPageMask;
}

// Ticker that increments ~twice a second.  Used to detect when it is time to
// attempt another stack reclamation.  Used to amortize reclaim overhead.
//
// Starts incrementing with the first StackReclaimer constructed.
static std::atomic<int> ticker;
static TimedCall* ticker_timed_call;
static absl::once_flag init_ticker_once;

static WallTime PickNextTickerIncrement() {
  return base::ToWallTime(absl::Now() + absl::Milliseconds(500));
}

static void IncrementTicker() {
  // Read by callers of ReduceMemoryUsage().  Subsequent calls to
  // ReduceMemoryUsage() within a single generation are no-ops.
  ticker.fetch_add(1, std::memory_order_relaxed);

  ticker_timed_call->Set(PickNextTickerIncrement(), IncrementTicker);
}

static void InitTicker() {
  ticker_timed_call = new TimedCall();
  // We must construct and assign "ticker_timed_call" before we Set() it for the
  // first time.  Otherwise, we could race if pre-empted after construction, but
  // before the pointer had been set.
  ticker_timed_call->Set(PickNextTickerIncrement(), IncrementTicker);
}

static bool StackPagesMaybeLocked() { return false; }

// Retrieving the stack information is much more expensive than reclaiming from
// it.  StackReclaimer objects are intended to be reused by threading
// implementations.
StackReclaimer::StackReclaimer() : stack_addr_(0), last_ticker_read_(0) {
  if (!STACK_RECLAIM_SUPPORTED || !absl::GetFlag(FLAGS_thread_reclaim_stacks) ||
      StackPagesMaybeLocked()) {
    return;
  }

  // As 'main()' was not constructed by libc, it does not have complete stack
  // information for it.  It tries to parse /proc/maps instead.  This will never
  // result in an error, however this is particularly expensive to initialize
  // and reclaim may be incomplete/too aggressive.  (As an example, the guard
  // region is commonly not correctly calculated.)
  DCHECK_NE(getpid(), GetTID());

  pthread_attr_t attr;
  void* addr;
  if (pthread_getattr_np(pthread_self(), &attr) != 0) return;

  if (pthread_attr_getstack(&attr, &addr, &stack_size_) == 0) {
    stack_addr_ = reinterpret_cast<intptr_t>(addr);
  }

  pthread_attr_destroy(&attr);

  if (stack_addr_ == 0) return;

  // Workaround for http://b/23446180.  If we're trying to limit stack memory
  // use, we probably don't want THP on any part of it.
  if (madvise(addr, stack_size_, MADV_NOHUGEPAGE)) {
    PLOG_FIRST_N(WARNING, 1) << "madvise(MADV_NOHUGEPAGE) on thread stack";
  }

  // Start the reclaim generation counter.
  absl::call_once(init_ticker_once, InitTicker);
}

// The following description of the stack-layout may be useful in understanding
// the implementation below.
//
// The stack occupies:
//   [stack_addr_, stack_addr_ + stack_size_).
//
// The provided diagram assumes that the stack is growing down; mirror it for
// the stack growing up case.
//
// Note also that glibc reserves some space for thread-local state at the start
// of the stack.
//
// +-------------------+  <-- stack_addr_ + stack_size_
// |###################|
// |###################|
// |###################|  <-- consumed stack + thread local state
// |###################|
// |###################|
// |###################|
// +-------------------+
// |Return address     |  <-- Current frame
// |(previous base_ptr)|  (When compiled with frame pointers)
// |Local variables    |
// +-------------------+  <-- Stack pointer (current_stack_ptr)
// |...................|
// |...................|
// +-------------------+  <-- red zone boundary (scratch space)
// |                   |  <-- Unused stack space; containing potentially
// |                   |      previously faulted pages (reclaim targets).
// |                   |
// |                   |
// +-------------------+  <-- stack_addr_

// Given a stack position, and an amount of additional stack to preserve,
// returns a page-aligned [*reclaim_start, *reclaim_end) area from which
// previously faulted pages can be released.
//
// Notes:
// - "limit" is the stack boundary that the stack pointer ends up at when
//    completely full.
// - "preserve" must be the total space beyond "current_sp" to preserve.
//    Including the red-zone or any other architectural adjustments.
// - "grows_up" is parameterized for testing purposes.
void StackReclaimer::CalculateAreaToReclaim(intptr_t current_sp,
                                            size_t preserve, intptr_t limit,
                                            bool grows_up,
                                            intptr_t* reclaim_start,
                                            intptr_t* reclaim_end) {  // static
  intptr_t keep = current_sp + (grows_up ? preserve : -preserve);

  // Backing granularity is physical pages.  We must round to ensure that we do
  // not release a partially consumed page.
  keep = RoundAwayFromActiveStack(keep, grows_up);

  if (!grows_up) std::swap(keep, limit);  // We always return in-memory-order.
  *reclaim_start = keep;
  *reclaim_end = limit;
}

// Implements actual reclaim.  All stack beyond the current position adjusted
// for both the specified working size and any hardware specific offsets (e.g.
// red-zone) will be released.  If no stack is eligible for release, this
// function does nothing.
void StackReclaimer::ReclaimWithWorkingSize(size_t working_size) {
  // Not set if initialization failed, or if we previously encountered an error.
  if (!stack_addr_) return;

  // We sample the current stack depth to determine the relative point to
  // reclaim from and to ensure that we only reclaim from unused space.
  // ***************************************************************************
  // NOTE: We are careful not to perturb the stack below this line.
  // (Inline stack allocations are reserved during prolog.)
  // ***************************************************************************
  intptr_t current_stack_ptr = StackProperties::current_sp();

  // We never support remotely calling free against the stack that this was
  // initialized for (e.g. another thread, SA_ONSTACK, etc).
  //
  // [ This will also trigger on stack overflow; however, this is extremely
  // unlikely to be interesting as ReleaseStackBeyond() is universally expected
  // to be called after the stack has already unwound. ]
  if (current_stack_ptr < stack_addr_ ||
      static_cast<uintptr_t>(current_stack_ptr) >= stack_addr_ + stack_size_) {
    LOG(FATAL) << "Stack pointer out of bounds.  Calling from remote thread or "
                  "signal handler?";
  }

  // We never attempt to reclaim stack within a page of the current
  // stack-pointer.  This space is guaranteed for both the execution of
  // madvise(2) below and for any compiler optimization which may be using the
  // space implicitly.  (This is in addition to any space reserved by the
  // red-zone.)
  static const size_t kPageSize = sysconf(_SC_PAGESIZE);
  if (working_size < kPageSize) working_size = kPageSize;
  working_size += StackProperties::red_zone_size();

  intptr_t reclaim_start, reclaim_end;
  CalculateAreaToReclaim(
      current_stack_ptr, working_size,
      StackProperties::grows_up() ? stack_addr_ + stack_size_ : stack_addr_,
      StackProperties::grows_up(), &reclaim_start, &reclaim_end);
  if (reclaim_start == reclaim_end) return;  // No unused stack to reclaim.

  // The region [reclaim_start, reclaim_end) is contained by "Unused stack
  // space" in our diagram above.
  int rc;
  do {
    rc = madvise(reinterpret_cast<void*>(reclaim_start),
                 reclaim_end - reclaim_start, MADV_DONTNEED);
  } while (rc != 0 && errno == EAGAIN);

  if (rc != 0) {
    DLOG(FATAL) << "Reclaim failed with error " << -rc << ", reclaim target"
                << "= [" << std::hex << reclaim_start << ", " << std::hex
                << reclaim_end << ").  current_stack_ptr = " << std::hex
                << current_stack_ptr << " errno=" << std::dec << errno;
    // This can only be that there was some problem with the addresses we've
    // passed (e.g. we dd not correctly calculate them, or that pages are
    // not mapped where we think they are).
    //
    // In production, disable future attempts at reclaim for this thread.
    stack_addr_ = 0;
  }
}

void StackReclaimer::ReduceMemoryUsage() {
  ReduceMemoryUsage(0);  // No minimum.
}

void StackReclaimer::ReduceMemoryUsage(size_t min_working_size) {
  // In the future the reclaim target may be dynamically calculated.  We expect
  // to try lowering it to 32kb in the short term, but are being conservative
  // for initial roll-out.
  static const size_t kReclaimTarget = 48 * 1024;

  // This function may be called extremely frequently (e.g. every closure
  // processed by an Executor).  We limit reclaim to only occur once per
  // generation.  This is maintained externally by IncrementGlobalGeneration().
  int current_ticker = ticker.load(std::memory_order_relaxed);
  if (last_ticker_read_ == current_ticker) return;
  last_ticker_read_ = current_ticker;

  // This covers the case that "FLAGS_thread_reclaim_stacks" was disabled at
  // run-time, after threads have been constructed.  When possible, we will also
  // skip the initialization of StackReclaimer objects.
  if (!absl::GetFlag(FLAGS_thread_reclaim_stacks)) {
    return;
  }

  ReclaimWithWorkingSize(std::max(min_working_size, kReclaimTarget));
}

// static
bool StackReclaimer::Available() { return STACK_RECLAIM_SUPPORTED; }

}  // namespace internal
}  // namespace thread
