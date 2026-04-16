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

#ifndef THIRD_PARTY_GLOOP_BASE_VARSETTER_H_
#define THIRD_PARTY_GLOOP_BASE_VARSETTER_H_

#include <atomic>
#include <utility>

#include "absl/flags/flag.h"

// Use a `VarSetter<T>` object to temporarily set an object of type `T` to a
// particular value.  When the `VarSetter` object is destroyed, the underlying
// object will revert to its former value.
//
// Sample code:
//
//   int i = 1;
//   {
//     CHECK_EQ(i, 1);
//     VarSetter<int> i_setter(&i, 2);
//     CHECK_EQ(i, 2);
//     i = 3;
//     CHECK_EQ(i, 3);
//   }
//   CHECK_EQ(i, 1);
template <typename T>
class VarSetter {
 public:
  VarSetter(T* object, T value)
      : object_(object), old_value_(std::move(*object)) {
    *object_ = std::move(value);
  }
  VarSetter(const VarSetter&) = delete;
  VarSetter& operator=(const VarSetter&) = delete;
  ~VarSetter() { *object_ = std::move(old_value_); }

 private:
  T* object_;
  T old_value_;
};

// Support `std::atomic`. Note that this makes a copy of the old value.
template <typename T>
class VarSetter<std::atomic<T>> {
 public:
  VarSetter(std::atomic<T>* object, T value)
      : object_(object), old_value_(*object) {
    *object = std::move(value);
  }
  VarSetter(const VarSetter&) = delete;
  VarSetter& operator=(const VarSetter&) = delete;
  ~VarSetter() { *object_ = std::move(old_value_); }

 private:
  std::atomic<T>* object_;
  T old_value_;
};

// Support `absl::Flag` with `absl::GetFlag()`/`absl::SetFlag()`.
// For best effect, avoid specifying the template parameter, i.e.:
//
//   VarSetter f_setter(&FLAGS_my_int_flag, 2);
template <typename T>
class VarSetter<absl::Flag<T>> {
 public:
  VarSetter(absl::Flag<T>* flag, T value)
      : flag_(flag), old_value_(absl::GetFlag(*flag)) {
    absl::SetFlag(flag_, std::move(value));
  }
  // Mimic the behavior of SetFlag() by offering a template overload taking a
  // different type than T, allowing calls with types that are convertible but
  // not implicitly convertible to T.
  template <typename V>
  VarSetter(absl::Flag<T>* flag, V value)
      : flag_(flag), old_value_(absl::GetFlag(*flag)) {
    absl::SetFlag(flag_, std::move(value));
  }
  VarSetter(const VarSetter&) = delete;
  VarSetter& operator=(const VarSetter&) = delete;
  ~VarSetter() { absl::SetFlag(flag_, std::move(old_value_)); }

 private:
  absl::Flag<T>* flag_;
  T old_value_;
};

// Allow using CTAD with VarSetter, so that you can write things like this:
//   VarSetter foo(&my_var, 42);
// Rather than needing to explicitly specify VarSetter<int> for that.
template <typename T, typename V>
explicit VarSetter(T*, V) -> VarSetter<T>;

#endif  // THIRD_PARTY_GLOOP_BASE_VARSETTER_H_
