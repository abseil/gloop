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

// Private implementation details of RCU.
#ifndef THIRD_PARTY_GLOOP_CONCURRENT_RCU_RCU_IMPL_INC_
#define THIRD_PARTY_GLOOP_CONCURRENT_RCU_RCU_IMPL_INC_

inline constexpr Domain* AssertDomainNotNull(Domain* d) {
  return ABSL_ASSERT(d != nullptr), d;
}

template <typename T>
inline constexpr Value<T>::Value(Domain *d)
    : value_(nullptr)
#ifndef NDEBUG
      ,
      domain_check_(AssertDomainNotNull(d))
#endif
{
}

template <typename T>
inline constexpr Value<T>::Value(T *value, Domain *d)
    : value_(value)
#ifndef NDEBUG
      ,
      domain_check_(AssertDomainNotNull(d))
#endif
{
}

inline Token Domain::ReaderLock() {
  namespace pc = ::base::subtle::percpu;
  // This is essentially just
  // pc::AtomicFetchAdd(*phase_handle, 1);
  // except we use a CAS loop to ensure that the read of phase handle
  // is "atomic" (with respect to percpu ops) with the increment.
  // See comments in Synchronize() for why.

  // This load is really a Consume (but we don't have it). It's necessary
  // in the unlikely case that a different thread set up the Handles and
  // wrote the current-phase handle here, but we don't yet have visibility
  // to the handles themselves.  On any sane arch we get away with it,
  // but TSAN complains.
  // TODO: fix this on power.
  int64_t handle = phase_handle_.load(std::memory_order_acquire);
  if (ABSL_PREDICT_FALSE(handle == 0)) {
    return ReaderLockInit();
  }
  if (ABSL_PREDICT_FALSE(!pc::IsFastNoInit())) {
    return ReaderLockSlow(handle);
  }

  while (true) {
    int cpu = pc::GetCurrentCpuUnsafe();
    ABSL_ASSUME(cpu >= pc::kCpuIdInitialized);
    pc::Handle h = pc::HandleFromInt(handle);
    std::atomic<int64_t> *const loc = GetPointerAtomic(h, cpu);
    const int64_t old = loc->load(std::memory_order_relaxed);
    if (ABSL_PREDICT_TRUE(
            cpu == pc::CompareAndSwapCheckUnsafe(cpu, loc, old, old + 1,
                                                 &phase_handle_, handle))) {
      return Token(h);
    }
    handle = phase_handle_.load(std::memory_order_acquire);
  }
}

inline void Token::DoUnlock() {
  if (ABSL_PREDICT_FALSE(h.rep == nullptr)) {
    return;
  }
#ifdef THREAD_SANITIZER
  while (true) {
    namespace pc = ::base::subtle::percpu;
    int cpu = pc::GetCurrentCpu();
    std::atomic<int64_t> *p = GetPointerAtomic(h, cpu);
    int64_t old = p->load(std::memory_order_relaxed);
    if (cpu == pc::CompareAndSwap(cpu, p, old, old - 1)) {
      return;
    }
  }
#else
  ::base::subtle::percpu::AtomicFetchAdd(h, -1);
#endif
}

inline void Domain::ReaderUnlock(Token t) {
  // Turns out the actual implementation doesn't touch the domain,
  // only the token.  Annotalysis cares somewhat, however (which is
  // why we have this function.)
  t.DoUnlock();
}


inline ReaderLockHolder::ReaderLockHolder(Domain *d) : t_(d->ReaderLock()) {}
inline ReaderLockHolder::ReaderLockHolder(Token t) : t_(t) {}
inline ReaderLockHolder::~ReaderLockHolder() { t_.DoUnlock(); }

template <typename T>
inline Value<T>::~Value() {
  ABSL_RAW_CHECK(value_.load(std::memory_order_relaxed) == nullptr,
                 "deleted a Value which still contained data");
}

template <typename T>
inline const T *Value<T>::Get(Domain *d) const {
  CheckSingleDomain(d);
  // TODO: this is the canonical use of C++11 consume; consume is too
  // broken to recommend using here.
  return value_.load(std::memory_order_acquire);
}

template <typename T>
inline bool Value<T>::IsNull(Domain *d) const {
  CheckSingleDomain(d);
  // NOTE: We could probably be OK with a relaxed load here, since it seems
  // unlikely that someone would use the nullness of a pointer in an RCU view as
  // a signalling mechanism. That said, the performance is the same on x86 so it
  // seems better to avoid a potentially subtle surprise.
  return value_.load(std::memory_order_acquire) == nullptr;
}

template <typename T>
inline T *Value<T>::Replace(T *new_value, Domain *d) {
  CheckSingleDomain(d);
  T *old = ReplaceUnsynchronized(new_value);
  d->Synchronize();
  return old;
}

template <typename T>
inline T *Value<T>::ReplaceUnsynchronized(T *new_value) {
  // Release barrier here pairs with acquire (or consume) in Get (and with...)
  // Acquire barrier here pairs with the release in previous Replace calls.
  return value_.exchange(new_value, std::memory_order_acq_rel);
}

template <typename T>
inline bool Value<T>::TryReplaceUnsynchronized(const T *old_value,
                                               T *new_value) {
  T *old = const_cast<T *>(old_value);
  // Barriers as in ReplaceUnsynchronized.
  return value_.compare_exchange_strong(
      old, new_value, std::memory_order_acq_rel, std::memory_order_relaxed);
}

template <typename T>
inline void Value<T>::CheckSingleDomain(Domain *d) const {
#ifndef NDEBUG
  Domain *owner = domain_check_;
  if (owner == nullptr) {
    // racing access to a linker-init domain before ctr runs,
    // don't bother checking
    return;
  }
  if (owner != d) {
    ABSL_RAW_LOG(FATAL, "Value %p owned by %p used by %p", this, owner, d);
  }
#endif
}

inline void Domain::Call(std::function<void()> f) {
  auto x = new std::function<void()>(f);
  void *p = reinterpret_cast<void *>(x);
  CallRaw(+[](void *data) {
      std::function<void()> *f = reinterpret_cast<std::function<void()>*>(data);
      (*f)();
      delete f;
    }, p);
}

template <class T>
void Domain::Free(T *obj) {
  // Casting to const void* allows for T to be const-qualified.
  void *p = const_cast<void *>(static_cast<const void *>(obj));
  CallRaw(+[](void *data) { delete static_cast<T *>(data); }, p);
}

template <class T>
void Domain::Free(std::unique_ptr<T> ptr) {
  Free(ptr.release());
}


template <class T>
void Domain::Free(std::unique_ptr<T[]> ptr) {
  FreeArray(ptr.release());
}

template <class T>
void Domain::FreeArray(T *obj) {
  // Casting to const void* allows for T to be const-qualified.
  void *p = const_cast<void *>(static_cast<const void *>(obj));
  CallRaw(+[](void *data) { delete [] static_cast<T *>(data); }, p);
}

inline void Domain::EnableCleanup() {
  // double checked locking. Note we do _not_ read any data from the cleanup
  // thread in any user of this, so no barriers are necessary.
  if (internal::CleanupEnabled()) return;
  DoEnableCleanup();
}

// For internal APIs, returns a useless RCU token that cannot be unlocked.
// Do not use under any circumstances.  You have been warned.
inline Token DummyToken() {
  // This is a disguised nullptr; if you try to
  // Unlock it you will segfault and I will laugh.
  // Exists for internal APIs that need a Token for
  // type safety but will never actually touch it.
  return Token(::base::subtle::percpu::HandleFromInt(0));
}

#endif  // THIRD_PARTY_GLOOP_CONCURRENT_RCU_RCU_IMPL_INC_
