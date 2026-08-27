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

// An implementation of thread-specific data that uses
// a cache to speed up accesses.
// This implementation should be replaced when thread-local storage is widely
// available.

#include "gloop/base/per_thread.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include <atomic>
#include <cstdint>
#include <cstring>

#include "absl/base/attributes.h"
#include "absl/base/internal/low_level_alloc.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/thread_annotations.h"
#include "gloop/base/scheduling/scheduling_mode.h"
#include "gloop/base/spinlock.h"
#include "gloop/util/atomic_danger/atomic_danger.h"

// We maintain a cache of stack page frame numbers to
// per-thread data arrays.
PerThread::CacheEntry PerThread::per_thread_cache[kTotalCacheEntries];

// The per-thread data consists of an array of kPerThreadSlots slots.
// All but slot 0 may be allocated by the user.
// Allow a larger number of slots if !BASE_INTERNAL_PER_THREAD_TLS because
// STATIC_THREAD_LOCAL will then use a slot per variable.
enum { kPerThreadSlots = BASE_INTERNAL_PER_THREAD_TLS ? 64 : 512 };

// If thread-local storage is available, we use it.
#if BASE_INTERNAL_PER_THREAD_TLS
BASE_INTERNAL_PER_THREAD_TLS_KEYWORD void*
    PerThread::per_thread_data[kPerThreadSlots];
#else
void* PerThread::per_thread_data[kPerThreadSlots];
#endif

namespace {

using absl::base_internal::LowLevelAlloc;

// Protects destructor and next_free_key.
ABSL_CONST_INIT SpinLock mu(absl::base_internal::SCHEDULE_KERNEL_ONLY);

// Destructor by key.
ABSL_CONST_INIT PerThread::Destructor
    destructor[kPerThreadSlots] ABSL_GUARDED_BY(mu);

// The next key to allocate.
ABSL_CONST_INIT int next_free_key ABSL_GUARDED_BY(mu) = 1;

// This is the pthread_[gs]etspecific key used.
// We use it even if we're using thread-local storage so we are notified
// when a thread dies.
// We initialize per_thread_key is a strange way.
// - We cannot initialize it purely with a static constructor or a module
//   initializer because some of our clients may call us before that happens.
//   We use a static constructor in addition to on-the-fly initialization to
//   ensure that initialization does happen before multiple threads exist.
// - We avoid pthread_once() and similar in case they are slow.  Although we
//   can do initialization on our slow path, our slow path can be invoked quite
//   often.
// - We avoid an additional boolean to indicate whether initialization has
//   taken place because that would put a memory barrier in the slow path
//   and thus be equivalent to pthread_once().
// - So we make two assumptions:  pthread_key_t is comparable, and atomically
//   assigned.  We use null_key to tell whether the initialization has
//   happened.
// We initialize per_thread_key with:
//   while (per_thread_key == null_key) { pthread_key_create(...); }
// This could allocate two keys if the null value is a legal key,
// we simply discard the additional key.
#ifndef _WIN32

static pthread_key_t per_thread_key;
static pthread_key_t null_key;

inline bool SetSlowPathTLS(void** value) {
  return (pthread_setspecific(per_thread_key, value) == 0);
}
inline void** GetSlowPathTLS() {
  return reinterpret_cast<void**>(pthread_getspecific(per_thread_key));
}

#else

std::atomic<DWORD> per_thread_key{TLS_OUT_OF_INDEXES};

inline bool SetSlowPathTLS(void** value) {
  // TlsSetValue is the WIN32 API for TLS.
  return TlsSetValue(per_thread_key.load(std::memory_order_relaxed), value) !=
         0;
}
inline void** GetSlowPathTLS() {
  // TlsGetValue is the WIN32 API for TLS.
  return reinterpret_cast<void**>(
      TlsGetValue(per_thread_key.load(std::memory_order_relaxed)));
}

#endif  // !_WIN32

}  // namespace

// This gets called by the pthread_key mechanism when a thread dies.
// We clean up the thread's per-thread state.
void PerThread::KeyDest(void** data) {
  if (data != nullptr) {
    // take a copy of the destructor function pointers
    PerThread::Destructor local_destructor[kPerThreadSlots];
    mu.lock();
    memcpy(local_destructor, destructor, sizeof(local_destructor));
    mu.unlock();
    // Calls to pthread_getspecific(per_thread_key) will now return null, but
    // in the non-tls path we want them to continue to return 'data' so we
    // can safely execute the user destructors with the existing values in
    // place.
    SetSlowPathTLS(data);
    // now run the destructors on the values
    bool again = true;  // a destructor has been run, so check for more values
                        // to destroy
    for (int iter = 0; again && iter != kMaxDestructorIterate; iter++) {
      again = false;
      for (int i = 0; i != kPerThreadSlots; i++) {
        void* w = data[i];
        if (local_destructor[i] != nullptr && w != nullptr) {
          data[i] = nullptr;
          (*local_destructor[i])(w);
          again = true;
        }
      }
    }
    SetSlowPathTLS(nullptr);
    if (!BASE_INTERNAL_PER_THREAD_TLS) {
      // remove this thread's entries from the cache
      for (int i = 0; i != kTotalCacheEntries; i++) {
        if (per_thread_cache[i].data == data) {
          // first prevent matching; then scrub data; then deallocate slot
          std::atomic_thread_fence(std::memory_order_acquire);
          per_thread_cache[i].sp.store(1, std::memory_order_relaxed);
          per_thread_cache[i].data = nullptr;
          per_thread_cache[i].sp.store(0, std::memory_order_release);
        }
      }
      LowLevelAlloc::Free(data);
    }
    // Set per_thread_data[0] to zero so that if PerThread should be used again
    // by this dying thread (perhaps in another thread-specific-data
    // destructor), it will take the slow path and perform full
    // reinitialization, thus causing it to call this destructor again.
    PerThread::per_thread_data[0] = nullptr;
  }
}

void PerThread::AllocateInternal(PerThread::Key* key,
                                 PerThread::Destructor destr) {
  mu.lock();
  if (key->load(std::memory_order_relaxed) == PerThread::kInvalid) {
    ABSL_RAW_CHECK(next_free_key < kPerThreadSlots,
                   "too many PerThread keys in use");
    destructor[next_free_key] = destr;
    // we multiply the key by the array stride so no multiplication
    // is needed on the fast path.
    key->store(next_free_key * sizeof(per_thread_cache[0].data[0]),
               std::memory_order_relaxed);
    next_free_key++;
  }
  mu.unlock();
}

// Initialize per_thread_key
void PerThread::ModuleInit::Init() {
#ifndef _WIN32
  while (per_thread_key == null_key) {
    ABSL_RAW_CHECK(
        pthread_key_create(&per_thread_key,
                           [](void* v) {
                             PerThread::KeyDest(reinterpret_cast<void**>(v));
                           }) == 0,
        "");
  }
#else
  DWORD current_key = per_thread_key.load(std::memory_order_relaxed);
  if (current_key == TLS_OUT_OF_INDEXES) {
    DWORD key = TlsAlloc();
    ABSL_RAW_CHECK(key != TLS_OUT_OF_INDEXES, "");
    if (!per_thread_key.compare_exchange_strong(current_key, key,
                                                std::memory_order_relaxed,
                                                std::memory_order_relaxed)) {
      // This thread lost the race, clean up and move on.
      TlsFree(key);
    }
  }
#endif  // !_WIN32
}

// static constructor to ensure that initialization happens before main() is
// called
PerThread::ModuleInit::ModuleInit() {
  // check that sizeof(per_thread_cache[0]) is a power of two
  static const int cache_entry_size = sizeof(PerThread::per_thread_cache[0]);
  static_assert((cache_entry_size & (cache_entry_size - 1)) == 0,
                "sizeof_perthread_cacheentry_not_power_of_two");
  PerThread::ModuleInit::Init();
}
PerThread::ModuleInit PerThread::module_init;

// This is the slow path for Data() when using thread-local storage.  All it
// does is set per_thread_data[0] so it won't be called again in this thread,
// and sets the thread-specific data so this thread's destructors will be
// called.
void PerThread::DataSlowPathTLS() {
  PerThread::per_thread_data[0] = PerThread::per_thread_data;
  PerThread::ModuleInit::Init();  // Initialize per_thread_key if necessary.
  SetSlowPathTLS(PerThread::per_thread_data);
}

// This is the slow path for Data() when not using thread-local storage.  We
// pass sp and e so that the hashing calculation is done just once; this allows
// it to be implemented in assembler without having to keep it in step with a C
// version.  "examined" is the number of elements in the bucket already
// examined on the fast path.
// If "allocate" is false, the call is not permitted to allocate memory,
// or to call pthread_setspecific (which might allocate memory).  In
// that case, if allocation would have been required, return 0.
void** PerThread::DataSlowPathNonTLS(bool allocate, int examined, intptr_t sp,
                                     CacheEntry* e) {
  // finish the search that the inlined fast-path started.
  for (int i = examined; i < (1 << PerThread::kLog2CacheAssoc); i++) {
    if (e[i].sp.load(std::memory_order_relaxed) == sp) {
      return e[i].data;
    }
  }
  PerThread::ModuleInit::Init();  // Initialize per_thread_key if necessary.
  // Search for an empty entry in the hash table.
  int empty = 0;
  while (empty != (1 << PerThread::kLog2CacheAssoc) &&
         e[empty].sp.load(std::memory_order_relaxed) != 0) {
    empty++;
  }
  // Get the array of data slots for this thread.
  void** data = GetSlowPathTLS();
  if (data == nullptr &&
      allocate) {  // 1st call for this thread; allocate new array
    data = reinterpret_cast<void**>(
        LowLevelAlloc::Alloc(sizeof(data[0]) * kPerThreadSlots));
    memset(data, 0, sizeof(data[0]) * kPerThreadSlots);
    ABSL_RAW_CHECK(SetSlowPathTLS(data), "");
  }
  // If there was an empty entry in the bucket, acquire it for our use.  We use
  // sp==1 to reserve the entry, then set the data, then we fill in the correct
  // sp value.  This is to allow the code to work even from a signal handler
  // invoked after the CAS and before the assignment of data.  If that should
  // happen, two identical entries may be made in the table.  A side-effect is
  // that page-frame 1 may not be used for a stack.
  ABSL_RAW_CHECK(sp != 1,
                 "PerThread can't tolerate using page frame 1 in a stack");
  if (data != nullptr && empty < (1 << PerThread::kLog2CacheAssoc) &&
      atomic_danger::CompareAndSwap(&e[empty].sp, 0, 1,
                                    std::memory_order_acquire) == 0) {
    e[empty].data = data;  // we succeeded
    e[empty].sp.store(sp, std::memory_order_release);
  }
  return data;  // return pointer to the first location, or 0 if there's no data
}

#ifdef _WIN32
namespace base {
namespace per_thread_internal {
void WinThreadExit() {
  if (per_thread_key.load(std::memory_order_relaxed) == TLS_OUT_OF_INDEXES) {
    return;
  }
  // WIN32 does not provide the data as an argument, so fetch it.
  void** tls_data = GetSlowPathTLS();
  if (tls_data == nullptr) {
    return;
  }
  PerThread::KeyDest(tls_data);
}
}  // namespace per_thread_internal
}  // namespace base

// Thread Termination Callbacks.
// Windows doesn't support a per-thread destructor with its
// TLS primitives.  So, we build it manually by inserting a
// function to be called on each thread's exit.
// This magic is from http://www.codeproject.com/threads/tls.asp
// and it works for VC++ 7.0 and later.

// Force a reference to _tls_used to make the linker create the TLS directory
// if it's not already there.  (e.g. if __declspec(thread) is not used).
// Force a reference to google_base_thread_exit_callback to prevent whole
// program optimization from discarding the variable.
#ifdef _WIN64

#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:google_base_thread_exit_callback")

#else  // _WIN64

#pragma comment(linker, "/INCLUDE:__tls_used")
#pragma comment(linker, "/INCLUDE:_google_base_thread_exit_callback")

#endif  // _WIN64

// Static callback function to call with each thread termination.
static void NTAPI OnThreadExit(PVOID module, DWORD reason, PVOID reserved) {
  // On XP SP0 & SP1, the DLL_PROCESS_ATTACH is never seen. It is sent on SP2+
  // and on W2K and W2K3. So don't assume it is sent.
  if (DLL_THREAD_DETACH == reason || DLL_PROCESS_DETACH == reason) {
    base::per_thread_internal::WinThreadExit();
  }
}

// .CRT$XLA to .CRT$XLZ is an array of PIMAGE_TLS_CALLBACK pointers that are
// called automatically by the OS loader code (not the CRT) when the module is
// loaded and on thread creation. They are NOT called if the module has been
// loaded by a LoadLibrary() call. It must have implicitly been loaded at
// process startup.
// By implicitly loaded, I mean that it is directly referenced by the main EXE
// or by one of its dependent DLLs. Delay-loaded DLL doesn't count as being
// implicitly loaded.
//
// See VC\crt\src\tlssup.c for reference.

// extern "C" suppresses C++ name mangling so we know the symbol name for the
// linker /INCLUDE:symbol pragma above.
extern "C" {
// The linker must not discard google_base_thread_exit_callback.  (We force a
// reference to this variable with a linker /INCLUDE:symbol pragma to ensure
// that.) If this variable is discarded, the OnThreadExit function will never be
// called.
#ifdef _WIN64

// .CRT section is merged with .rdata on x64 so it must be constant data.
#pragma const_seg(".CRT$XLB")
// When defining a const variable, it must have external linkage to be sure the
// linker doesn't discard it.
extern const PIMAGE_TLS_CALLBACK google_base_thread_exit_callback;
const PIMAGE_TLS_CALLBACK google_base_thread_exit_callback = OnThreadExit;

// Reset the default section.
#pragma const_seg()

#else  // _WIN64

#pragma data_seg(".CRT$XLB")
PIMAGE_TLS_CALLBACK google_base_thread_exit_callback = OnThreadExit;

// Reset the default section.
#pragma data_seg()

#endif  // _WIN64
}  // extern "C"

#endif  // _WIN32
