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

#ifndef THIRD_PARTY_GLOOP_THREAD_SHUTDOWN_GATE_H_
#define THIRD_PARTY_GLOOP_THREAD_SHUTDOWN_GATE_H_

#include <atomic>
#include <cstdint>
#include <limits>

#include "absl/log/check.h"
#include "absl/synchronization/notification.h"

namespace thread {

// ShutdownGate
//
// Gate mechanism that allows a single waiter thread to wait for the number of
// active participant threads to reach 0 before closing the gate.
class ShutdownGate {
 public:
  ShutdownGate() = default;

  ~ShutdownGate() {
    if (!IsClosed()) {
      CloseAndWait();
    }
  }

  ShutdownGate(const ShutdownGate&) = delete;
  ShutdownGate(ShutdownGate&&) = delete;
  ShutdownGate& operator=(const ShutdownGate&) = delete;
  ShutdownGate& operator=(ShutdownGate&&) = delete;

  void Enter() {
    uint32_t v = word_.fetch_add(1, std::memory_order::relaxed);
    DCHECK_NE(v, std::numeric_limits<uint32_t>::max()) << "uint32_t overflow";
    DCHECK((v & kClosedBit) == 0)
        << "gloop: thread cannot enter a closed shutdown gate";
  }

  [[nodiscard]]
  bool TryEnter() {
    uint32_t v = word_.load(std::memory_order::relaxed);
    do {
      if ((v & kClosedBit) != 0) {
        return false;
      }
      DCHECK_NE(v, std::numeric_limits<uint32_t>::max())
          << "gloop: uint32_t overflow";
    } while (!word_.compare_exchange_strong(
        v, v + 1, std::memory_order::relaxed, std::memory_order::relaxed));
    return true;
  }

  void Leave() {
    uint32_t v = word_.fetch_sub(1, std::memory_order::acq_rel);
    DCHECK_NE((v & kClosedMask), uint32_t{0}) << "gloop: uint32_t overflow";
    if ((v & kClosedBit) != 0 && (v & kClosedMask) == 1) {
      closed_.Notify();
    }
  }

  void CloseAndWait() {
    uint32_t v = word_.fetch_or(kClosedBit, std::memory_order::acq_rel);
    DCHECK((v & kClosedBit) == 0) << "gloop: multiple threads cannot drain and "
                                     "close the same shutdown gate";
    // Tolerate bad code in release which didn't trigger the DCHECK above in
    // tests by masking the closed bit.
    if ((v & kClosedMask) != 0) {
      // Wait for existing threads to exit.
      closed_.WaitForNotification();
    }
  }

  [[nodiscard]]
  bool IsClosed() const {
    return (word_.load(std::memory_order::acquire) & kClosedBit) != 0;
  }

 private:
  static constexpr uint32_t kClosedBit = uint32_t{1}
                                         << (sizeof(uint32_t) * 8 - 1);
  static constexpr uint32_t kClosedMask = ~kClosedBit;

  absl::Notification closed_;
  std::atomic<uint32_t> word_ = 0;
};

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_SHUTDOWN_GATE_H_
