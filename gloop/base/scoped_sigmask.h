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

#ifndef THIRD_PARTY_GLOOP_BASE_SCOPED_SIGMASK_H_
#define THIRD_PARTY_GLOOP_BASE_SCOPED_SIGMASK_H_

#ifdef __linux__
#include <csignal>  // IWYU pragma: keep
#endif

#include "absl/base/internal/raw_logging.h"

namespace base {

class ScopedSigmask {
 public:
  // Masks all signal handlers. (SIG_SETMASK, All)
  ScopedSigmask() noexcept;

  // No copy, move or assign
  ScopedSigmask(const ScopedSigmask&) = delete;
  ScopedSigmask& operator=(const ScopedSigmask&) = delete;

  // Restores the masked signal handlers to its former state.
  ~ScopedSigmask() noexcept;

#ifdef __linux__
  // Linux specific support
  explicit ScopedSigmask(sigset_t* set) noexcept;
  ScopedSigmask(int how, sigset_t* set) noexcept;
  ScopedSigmask(int how, sigset_t set) noexcept;
#endif

 private:
#ifdef __linux__
  static void Setmask(int how, sigset_t* set, sigset_t* old);

  sigset_t old_set_;
#endif  // __linux__
};

#ifdef __linux__

inline ScopedSigmask::ScopedSigmask() noexcept {
  sigset_t set;
  sigfillset(&set);
  Setmask(SIG_SETMASK, &set, &old_set_);
}

inline ScopedSigmask::~ScopedSigmask() noexcept {
  Setmask(SIG_SETMASK, &old_set_, nullptr);
}

inline ScopedSigmask::ScopedSigmask(sigset_t* set) noexcept {
  Setmask(SIG_SETMASK, set, &old_set_);
}

inline ScopedSigmask::ScopedSigmask(int how, sigset_t* set) noexcept {
  Setmask(how, set, &old_set_);
}

inline ScopedSigmask::ScopedSigmask(int how, sigset_t set) noexcept {
  Setmask(how, &set, &old_set_);
}

inline void ScopedSigmask::Setmask(int how, sigset_t* set, sigset_t* old) {
  const int result = pthread_sigmask(how, set, old);
  if (result) ABSL_RAW_LOG(FATAL, "pthread_sigmask returned %d", result);
}

#else  // __linux__

inline ScopedSigmask::ScopedSigmask() noexcept {}
inline ScopedSigmask::~ScopedSigmask() noexcept {}

#endif  // __linux__

}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_SCOPED_SIGMASK_H_
