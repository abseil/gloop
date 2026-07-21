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

#ifndef THIRD_PARTY_GLOOP_BASE_PER_THREAD_H_
#define THIRD_PARTY_GLOOP_BASE_PER_THREAD_H_

// This interface provides functionality similar to pthread_getspecific() and
// pthread_setspecific().  It is typically faster than those calls when
// using linuxthreads (old versions of glibc).  The implementation may slow
// down when large numbers of threads use in from many levels on their stacks.

// Declaration:
//      ABSL_CONST_INIT static PerThread::Key static_key{PerThread::kInvalid};
//
// Initialization:
//      PerThread::Allocate(&key, desctructor);
//
// Use:
//      void **per_thread_data = PerThread::Data(&key);
//      ... = *per_thread_data;
//      *per_thread_data  = ...;

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "absl/base/internal/raw_logging.h"

#ifdef _WIN32
namespace base {
namespace per_thread_internal {
// Internal API called at the end of execution of each terminating thread.
void WinThreadExit();
}  // namespace per_thread_internal
}  // namespace base
#endif

#if defined(__ANDROID__) || defined(__APPLE__)
// per_thread.cc depends on the order of TLS destruction where it needs the
// d'tor functions of pthread_key_create to be called before the storage managed
// by thread_local is released. This ordering is currently unspecified. On Linux
// it happens to work, but not our mobile platforms (which otherwise support TLS
// just fine).
// Hence we have this `BASE_INTERNAL_PER_THREAD_TLS` macro specifically for
// controlling whether or not this ordering is correct instead of just using
// other platforms TLS detection macros.
// See the comments associated with `per_thread_key` in per_thread.cc to
// understand why this dependency on pthread_key_create exists.
// Android details: b/78022094 and <link>
// Apple details: b/184757523 and <link>
#define BASE_INTERNAL_PER_THREAD_TLS 0
#define BASE_INTERNAL_PER_THREAD_TLS_KEYWORD \
  _Pragma(                                   \
      "GCC error \"Unguarded use of "        \
      "BASE_INTERNAL_PER_THREAD_TLS_KEYWORD\"")
#elif defined(ABSL_HAVE_TLS)
#define BASE_INTERNAL_PER_THREAD_TLS 1
#define BASE_INTERNAL_PER_THREAD_TLS_KEYWORD __thread
#elif defined(_MSC_VER)
#define BASE_INTERNAL_PER_THREAD_TLS 1
#define BASE_INTERNAL_PER_THREAD_TLS_KEYWORD __declspec(thread)
#elif defined(ABSL_HAVE_THREAD_LOCAL)
#define BASE_INTERNAL_PER_THREAD_TLS 1
#define BASE_INTERNAL_PER_THREAD_TLS_KEYWORD thread_local
#else
#define BASE_INTERNAL_PER_THREAD_TLS 0
#define BASE_INTERNAL_PER_THREAD_TLS_KEYWORD \
  _Pragma(                                   \
      "GCC error \"Unguarded use of "        \
      "BASE_INTERNAL_PER_THREAD_TLS_KEYWORD\"")
#endif

class PerThread {
 public:
  using Key =
      std::atomic<int32_t>;  // The type is a key used to index each location
  enum { kInvalid = 0 };     // A zero value is guaranteed invalid.

  // Data() returns a pointer to the per-thread data at index "*data_key"
  // in the current thread's per-thread data array.
  // The value "*data_key" must have previously been allocated by a call to
  // Allocate().
  static void** Data(const Key& data_key);

  // GetData() returns the value of the per-thread data at index "*data_key"
  // in the current thread's per-thread data array.
  // The value "data_key" must have previously been allocated by a call to
  // Allocate().
  // It is equivalent to reading *Data(data_key), except that Data() is
  // permitted to call malloc(), while GetData() is guaranteed not to.
  static void* GetData(const Key& data_key);

  // Allocate() tests *key to see whether it is PerThread::kInvalid.
  // If it is not, Allocate() does nothing.
  // If it is, Allocate(), sets *key a new data key suitable for passing to
  // Data().    If Allocate() is called concurrently by multiple threads
  // on the same location, at most one new key will be allocated.
  //
  // If both "destructor" and *Data(key) are non-zero when a thread terminates,
  // *Data(*key) is saved to a temporary and then set to zero followed by a
  // call to (*destructor)(*Data(key)) using the saved copy of *Data(key).
  //
  // If some destructor makes *Data(key) non-zero again, the destructor
  // will be run anew, up to a maximum of kMaxDestructorIterate times.
  // No errors are returned; this call always either succeeds or crashes.
  // This routine will not call malloc().
  typedef void (*Destructor)(void* data);
  static void Allocate(PerThread::Key* key, Destructor destructor);
  enum { kMaxDestructorIterate = 4 };

  // There is no mechanism to deallocate data keys.  Clients should allocate
  // them sparingly.

  // ************ Public interface ends here ************

 private:
  PerThread();  // private constructor; there are no instances
  static void AllocateInternal(PerThread::Key* key, Destructor destructor);

  // Internal declarations here only to allow the inline of Data()

  // This array is thread-local iff the platform supports thread-local storage.
#if BASE_INTERNAL_PER_THREAD_TLS
  static BASE_INTERNAL_PER_THREAD_TLS_KEYWORD void* per_thread_data[];
#else
  static void* per_thread_data[];
#endif

  // The following are constants that define the cache used if thread-local
  // storage is unavailable.

  // Log2(bytes in a "page frame")
  // We assume that no two stacks share a page frame; a page frame need not be
  // identical to a hardware page frame provided this condition is met.
  enum { kPageFrameShift = 12 };

  // The cache is a hash table with 1<<kLog2CacheLines lines
  // each containing 1<<kLog2CacheAssoc entries
  enum {
    kLog2CacheLines = 10,
    kLog2CacheAssoc = 2,
    kTotalCacheEntries = 1 << (kLog2CacheLines + kLog2CacheAssoc)
  };

  // Each entry in the cache contains a stack pointer page frame number
  // and a pointer to per thread data.
  // This structure must be a power-of-two bytes (An intptr_t is
  // large enough to hold a pointer.)
  struct CacheEntry {
    std::atomic<intptr_t> sp;  // stack pointer page frame
    std::atomic<void**> data;  // pointer to per-thread data
  };
  static CacheEntry per_thread_cache[];

  // Internal declarations in the header file only because C++ insists.
  static void** DataSlowPathNonTLS(bool allocate, int examined, intptr_t sp,
                                   CacheEntry* e);
  static void DataSlowPathTLS();
  static void KeyDest(void** data);
  struct ModuleInit {
    ModuleInit();
    static void Init();
  };
  static ModuleInit module_init;
  // routine to add an integer to a pointer, without index scaling
  static void* PtrAdd(void* p, size_t i) { return static_cast<char*>(p) + i; }
#ifdef _WIN32
  friend void base::per_thread_internal::WinThreadExit();  // For KeyDest.
#endif
};

inline void* PerThread::GetData(const PerThread::Key& data_key) {
  auto local_data_key = data_key.load(std::memory_order_relaxed);
  if (BASE_INTERNAL_PER_THREAD_TLS) {
    return *static_cast<void**>(
        PtrAdd(PerThread::per_thread_data, local_data_key));
  } else {
    int x;  // use address of x as a proxy for the stack pointer
    intptr_t sp = reinterpret_cast<intptr_t>(&x) >> PerThread::kPageFrameShift;
    // Safe to cast to int32_t because we use only the bits[15:6] on x86-64.
    int32_t line = static_cast<int32_t>(sp + (sp >> 5));
    line &= static_cast<int32_t>((((1 << PerThread::kLog2CacheLines) - 1)
                                  << PerThread::kLog2CacheAssoc) *
                                 sizeof(PerThread::per_thread_cache[0]));
    PerThread::CacheEntry* e =
        static_cast<CacheEntry*>(PtrAdd(PerThread::per_thread_cache, line));
    // here we look at only the first few elements in the bucket
    // to reduce the size of the inlined code.  The rest are handled by
    // the slow path.
    if (e[0].sp.load(std::memory_order_relaxed) == sp) {
      return *static_cast<void**>(
          PtrAdd(e[0].data.load(std::memory_order_relaxed), local_data_key));
    }
    if (1 < (1 << PerThread::kLog2CacheAssoc) &&
        e[1].sp.load(std::memory_order_relaxed) == sp) {
      return *static_cast<void**>(
          PtrAdd(e[1].data.load(std::memory_order_relaxed), local_data_key));
    }
    void** data = PerThread::DataSlowPathNonTLS(false, 2, sp, e);
    // false means "no malloc", 2 is number of elements examined
    return (data == nullptr)
               ? nullptr
               : *static_cast<void**>(PtrAdd(data, local_data_key));
  }
}

inline void** PerThread::Data(const PerThread::Key& data_key) {
  auto local_data_key = data_key.load(std::memory_order_relaxed);
  ABSL_RAW_DCHECK(local_data_key != kInvalid,
                  "Allocate() must be called before Data()");
  if (BASE_INTERNAL_PER_THREAD_TLS) {
    if (PerThread::per_thread_data[0] == nullptr) {
      PerThread::DataSlowPathTLS();
    }
    return static_cast<void**>(
        PtrAdd(PerThread::per_thread_data, local_data_key));
  } else {
    int x;  // use address of x as a proxy for the stack pointer
    intptr_t sp = reinterpret_cast<intptr_t>(&x) >> PerThread::kPageFrameShift;
    // Safe to cast to int32_t because we use only the bits[15:6] on x86-64.
    int32_t line = static_cast<int32_t>(sp + (sp >> 5));
    line &= static_cast<int32_t>((((1 << PerThread::kLog2CacheLines) - 1)
                                  << PerThread::kLog2CacheAssoc) *
                                 sizeof(PerThread::per_thread_cache[0]));
    PerThread::CacheEntry* e =
        static_cast<CacheEntry*>(PtrAdd(PerThread::per_thread_cache, line));
    // here we look at only the first few elements in the bucket
    // to reduce the size of the inlined code.  The rest are handled by
    // the slow path.
    if (e[0].sp.load(std::memory_order_relaxed) == sp) {
      return static_cast<void**>(
          PtrAdd(e[0].data.load(std::memory_order_relaxed), local_data_key));
    }
    if (1 < (1 << PerThread::kLog2CacheAssoc) &&
        e[1].sp.load(std::memory_order_relaxed) == sp) {
      return static_cast<void**>(
          PtrAdd(e[1].data.load(std::memory_order_relaxed), local_data_key));
    }
    return static_cast<void**>(
        PtrAdd(PerThread::DataSlowPathNonTLS(true, 2, sp, e), local_data_key));
    // true means "malloc allowed", 2 is number of elements examined
  }
}

inline void PerThread::Allocate(PerThread::Key* key, Destructor destructor) {
  if (key->load(std::memory_order_relaxed) == PerThread::kInvalid) {
    PerThread::AllocateInternal(key, destructor);
  }
}

#endif  // THIRD_PARTY_GLOOP_BASE_PER_THREAD_H_
