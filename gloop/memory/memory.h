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

#ifndef THIRD_PARTY_GLOOP_MEMORY_MEMORY_H_
#define THIRD_PARTY_GLOOP_MEMORY_MEMORY_H_

#include <memory>
#include <type_traits>

#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/base/throw_delegate.h"

namespace gloop {

// OwnerEqual()
//
// Returns whether the two `std::weak_ptr` have the same owner.
template <typename T, typename U>
[[nodiscard]] bool OwnerEqual(const std::weak_ptr<T>& lhs,
                              const std::weak_ptr<U>& rhs) {
#if defined(__cpp_lib_smart_ptr_owner_equality) && \
    __cpp_lib_smart_ptr_owner_equality >= 202306L
  return lhs.owner_equal(rhs);
#else
  return !lhs.owner_before(rhs) && !rhs.owner_before(lhs);
#endif
}

// OwnerEqual()
//
// Returns whether the two `std::shared_ptr` have the same owner.
template <typename T, typename U>
[[nodiscard]] bool OwnerEqual(const std::shared_ptr<T>& lhs,
                              const std::shared_ptr<U>& rhs) {
#if defined(__cpp_lib_smart_ptr_owner_equality) && \
    __cpp_lib_smart_ptr_owner_equality >= 202306L
  return lhs.owner_equal(rhs);
#else
  return !lhs.owner_before(rhs) && !rhs.owner_before(lhs);
#endif
}

// IsUnowned()
//
// Returns whether the `std::weak_ptr` is unowned.
template <typename T>
[[nodiscard]] bool IsUnowned(const std::weak_ptr<T>& ptr) {
  return gloop::OwnerEqual(ptr, std::weak_ptr<T>());
}

// IsUnowned()
//
// Returns whether the `std::shared_ptr` is unowned.
template <typename T>
[[nodiscard]] bool IsUnowned(const std::shared_ptr<T>& ptr) {
  return gloop::OwnerEqual(ptr, std::shared_ptr<T>());
}

namespace internal_memory {

template <typename T>
struct IsSharedPtr : std::false_type {};

template <typename T>
struct IsSharedPtr<std::shared_ptr<T>> : std::true_type {};

}  // namespace internal_memory

// SharedFromThat<T>()
//
// Gets a `std::shared_ptr<T>`, which may or may not be owned, whose managed
// pointer is `that`. `T` must be derived from `EnableSharedFromThis<T>` or
// `std::enable_shared_from_this<T>`.
template <typename T>
[[nodiscard]]
std::shared_ptr<T>
    absl_nullability_complex SharedFromThat(T* absl_nullability_complex that);

// MaybeShared<T>
//
// Similar to `std::enable_shared_from_this<T>` except that it will not throw if
// `shared_from_this()` is called without `T` having been created via
// `std::make_shared` or having been passed to the constructor of
// `std::shared_ptr`. Instead in that situation it will return
// `std::shared_ptr<T>` such that the managed pointer is `this` but it is
// unowned.
//
// As a rule of thumb, just use `std::enable_shared_from_this` instead. This
// should *only* be used when retrofitting `std::shared_ptr` into a class
// hierarchy which was previously passed around as references or raw pointers.
template <typename T>
struct MaybeShared : public std::enable_shared_from_this<T> {
  [[nodiscard]]
  std::shared_ptr<T> absl_nonnull shared_from_this() {
    // We cannot call `shared_from_this()` as it throws if `T` was not created
    // via `std::make_shared` or never passed to `std::shared_ptr()`.
    auto weak = this->weak_from_this();
    if (gloop::IsUnowned(weak)) {
      // Launder.
      return std::shared_ptr<T>(std::shared_ptr<T>(), static_cast<T*>(this));
    }
    auto shared = weak.lock();
    if (ABSL_PREDICT_FALSE(shared == nullptr)) {
      absl::ThrowStdBadWeakPtr();
    }
    return shared;
  }

  [[nodiscard]]
  std::shared_ptr<const T> absl_nonnull shared_from_this() const {
    // We cannot call `shared_from_this()` as it throws if `T` was not created
    // via `std::make_shared` or never passed to `std::shared_ptr()`.
    auto weak = this->weak_from_this();
    if (gloop::IsUnowned(weak)) {
      // Launder.
      return std::shared_ptr<const T>(std::shared_ptr<const T>(),
                                      static_cast<const T*>(this));
    }
    auto shared = weak.lock();
    if (ABSL_PREDICT_FALSE(shared == nullptr)) {
      absl::ThrowStdBadWeakPtr();
    }
    return shared;
  }

 private:
  using std::enable_shared_from_this<T>::weak_from_this;
};

template <typename T>
[[nodiscard]]
std::shared_ptr<T>
    absl_nullability_complex SharedFromThat(T* absl_nullability_complex that) {
  return std::static_pointer_cast<T>(that->shared_from_this());
}

}  // namespace gloop

#endif  // THIRD_PARTY_GLOOP_MEMORY_MEMORY_H_
