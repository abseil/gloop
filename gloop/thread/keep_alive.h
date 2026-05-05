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

#ifndef THIRD_PARTY_GLOOP_THREAD_KEEP_ALIVE_H_
#define THIRD_PARTY_GLOOP_THREAD_KEEP_ALIVE_H_

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "gloop/thread/executor.h"

namespace thread {

template <typename T>
class ABSL_ATTRIBUTE_TRIVIAL_ABI ABSL_NULLABILITY_COMPATIBLE KeepAlivePtr {
 public:
  static_assert(!std::is_reference_v<T>, "T must not be a reference");
  static_assert(!std::is_const_v<T>, "T must not be const qualified");
  static_assert(!std::is_volatile_v<T>, "T must not be volatile qualified");
  static_assert(std::is_base_of_v<Executor, T>,
                "T must be derived from thread::Executor");

  using element_type = T;

  explicit KeepAlivePtr(T* executor)
      : executor_(std::shared_ptr<T>(std::shared_ptr<T>(), executor)) {
    if (executor_ != nullptr) {
      executor_->ShutdownRef();
    }
  }

  explicit KeepAlivePtr(T& executor ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : KeepAlivePtr(std::addressof(executor)) {}

  KeepAlivePtr(const KeepAlivePtr& other) : executor_(other.executor_) {
    if (executor_ != nullptr) {
      executor_->ShutdownRef();
    }
  }

  constexpr KeepAlivePtr(KeepAlivePtr&& other) noexcept
      : executor_(std::exchange(other.executor_, nullptr)) {}

  template <typename U,
            typename = std::enable_if_t<std::is_convertible_v<U*, T*> &&
                                        !std::is_same_v<U, T>>>
  KeepAlivePtr(const KeepAlivePtr<U>& other) : executor_(other.executor_) {
    if (executor_ != nullptr) {
      executor_->ShutdownRef();
    }
  }

  template <typename U,
            typename = std::enable_if_t<std::is_convertible_v<U*, T*> &&
                                        !std::is_same_v<U, T>>>
  constexpr KeepAlivePtr(KeepAlivePtr<U>&& other) noexcept
      : executor_(std::exchange(other.executor_, nullptr)) {}

  ~KeepAlivePtr() {
    if (executor_ != nullptr) {
      executor_->ShutdownUnref();
    }
  }

  KeepAlivePtr& operator=(const KeepAlivePtr& other) {
    if (this != &other) {
      if (other.executor_ != nullptr) {
        other.executor_->ShutdownRef();
      }
      if (executor_ != nullptr) {
        executor_->ShutdownUnref();
      }
      executor_ = other.executor_;
    }
    return *this;
  }

  KeepAlivePtr& operator=(KeepAlivePtr&& other) noexcept {
    if (this != &other) {
      if (executor_ != nullptr) {
        executor_->ShutdownUnref();
      }
      executor_ = std::exchange(other.executor_, nullptr);
    }
    return *this;
  }

  template <typename U,
            typename = std::enable_if_t<std::is_convertible_v<U*, T*> &&
                                        !std::is_same_v<U, T>>>
  KeepAlivePtr& operator=(const KeepAlivePtr<U>& other) {
    if (other.executor_ != nullptr) {
      other.executor_->ShutdownRef();
    }
    if (executor_ != nullptr) {
      executor_->ShutdownUnref();
    }
    executor_ = other.executor_;
    return *this;
  }

  template <typename U,
            typename = std::enable_if_t<std::is_convertible_v<U*, T*> &&
                                        !std::is_same_v<U, T>>>
  KeepAlivePtr& operator=(KeepAlivePtr<U>&& other) noexcept {
    if (executor_ != nullptr) {
      executor_->ShutdownUnref();
    }
    executor_ = std::exchange(other.executor_, nullptr);
    return *this;
  }

  [[nodiscard]]
  constexpr T* absl_nullable get() const noexcept {
    return executor_.get();
  }

  [[nodiscard]]
  constexpr T& operator*() const noexcept {
    ABSL_ASSERT(*this);
    return *get();
  }

  [[nodiscard]]
  constexpr T* absl_nonnull operator->() const noexcept {
    ABSL_ASSERT(*this);
    return get();
  }

  constexpr operator T* absl_nullable() const noexcept { return get(); }

  constexpr explicit operator bool() const noexcept { return get() != nullptr; }

  void reset(std::nullptr_t = nullptr) {
    if (executor_ != nullptr) {
      executor_->ShutdownUnref();
    }
    executor_.reset();
  }

  void reset(T* executor) {
    if (executor != executor_) {
      if (executor != nullptr) {
        executor->ShutdownRef();
      }
      if (executor_ != nullptr) {
        executor_->ShutdownUnref();
      }
      executor_ = std::shared_ptr<T>(std::shared_ptr<T>(), executor);
    }
  }

  template <typename U,
            typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  void reset(const std::shared_ptr<U>& executor) {
    if (executor != nullptr) {
      executor->ShutdownRef();
    }
    if (executor_ != nullptr) {
      executor_->ShutdownUnref();
    }
    executor_ = executor;
  }

  template <typename U,
            typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  void reset(std::shared_ptr<U>&& executor) {
    if (executor != nullptr) {
      executor->ShutdownRef();
    }
    if (executor_ != nullptr) {
      executor_->ShutdownUnref();
    }
    executor_ = std::move(executor);
  }

 private:
  template <typename U>
  friend class KeepAlivePtr;

  std::shared_ptr<T> absl_nullable executor_;
};

template <typename T, typename U>
[[nodiscard]]
constexpr bool operator==(const KeepAlivePtr<T> absl_nullable& lhs,
                          const KeepAlivePtr<U> absl_nullable& rhs) noexcept {
  return lhs.get() == rhs.get();
}

template <typename T>
[[nodiscard]]
constexpr bool operator==(const KeepAlivePtr<T> absl_nullable& lhs,
                          std::nullptr_t) noexcept {
  return lhs.get() == nullptr;
}

template <typename T>
[[nodiscard]]
constexpr bool operator==(std::nullptr_t,
                          const KeepAlivePtr<T> absl_nullable& rhs) noexcept {
  return nullptr == rhs.get();
}

template <typename T, typename U>
[[nodiscard]]
constexpr bool operator!=(const KeepAlivePtr<T> absl_nullable& lhs,
                          const KeepAlivePtr<U> absl_nullable& rhs) noexcept {
  return lhs.get() != rhs.get();
}

template <typename T>
[[nodiscard]]
constexpr bool operator!=(const KeepAlivePtr<T> absl_nullable& lhs,
                          std::nullptr_t) noexcept {
  return lhs.get() != nullptr;
}

template <typename T>
[[nodiscard]]
constexpr bool operator!=(std::nullptr_t,
                          const KeepAlivePtr<T> absl_nullable& rhs) noexcept {
  return nullptr != rhs.get();
}

template <typename T>
KeepAlivePtr(T*) -> KeepAlivePtr<T>;

template <typename T>
KeepAlivePtr(T&) -> KeepAlivePtr<T>;

}  // namespace thread

namespace std {

template <typename T>
struct pointer_traits<::thread::KeepAlivePtr<T>> {
  using pointer = ::thread::KeepAlivePtr<T>;
  using element_type = typename pointer::element_type;
  using difference_type = ptrdiff_t;

  template <typename U>
  using rebind = ::thread::KeepAlivePtr<U>;

  [[nodiscard]]
  static constexpr element_type* absl_nullable to_address(
      const pointer& p) noexcept {
    return p.get();
  }
};

}  // namespace std

#endif  // THIRD_PARTY_GLOOP_THREAD_KEEP_ALIVE_H_
