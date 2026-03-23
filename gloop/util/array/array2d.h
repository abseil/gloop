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

// We define an Array2D which we access through () operator.
//
// Example:
//   Array2D<T> my_array(height, width);
//   my_array(7, 2) = 12;  // sets row 7, col 2 to 12
//   my_array(1, 1) = my_array(5, 4) + my_array(2, 3);  // easy to read
//
// Array2D has an overhead of O(height) for allocating the array.
// It uses an extra height*sizeof(T*) bytes of memory.
//
// Array2D supports memory ownership modes from util/array/array_common.h .
// See that file for information.

#ifndef THIRD_PARTY_GLOOP_UTIL_ARRAY_ARRAY2D_H_
#define THIRD_PARTY_GLOOP_UTIL_ARRAY_ARRAY2D_H_

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include <algorithm>
#include <cstddef>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "gloop/util/array/array_common.h"  // IWYU pragma: export

template <typename T>
class Array2D {
 public:
  typedef std::ptrdiff_t PD;

  // Constructs a 2D array of height and width 0.
  Array2D();

  // Constructs a 2D array of height h, width w.
  Array2D(PD h, PD w);

  // Constructs a 2D array of height h, width w, filled with a default value.
  Array2D(PD h, PD w, T default_value);

  // COPY_FROM_INSTANCE: Constructs a copy of a 2D array.
  // T must be a Plain Old Data Type (int, float, a pointer, etc.).
  //
  // SHARE_WITH_INSTANCE: Constructs an auxilliary reference to a 2D array
  // Not thread-safe.
  Array2D(InstantiationMode inst_mode, Array2D<T>* leader);

  // SHARE_WITH_FOREIGN_INSTANCE: Mirrors the given buffer as if it were a 2D
  // array.  WARNING: It is your responsibility to make sure that the buffer
  // lives longer than the instance of Array2D.
  //
  // TAKEOVER_FROM_FOREIGN_INSTANCE: Represents the given buffer
  // as if it were a 2D array. Takes responsibility of deleting it.
  // WARNING: You must not deallocate the passed buffer *EVER*.
  // Once you initialize an Array2D, it takes over the ownership
  // of the buffer. Once the Array2D is deallocated, the passed buffer
  // will be destroyed too.
  Array2D(InstantiationMode inst_mode, PD h, PD w, T* buffer);

  Array2D(const Array2D&);
  Array2D& operator=(const Array2D&);
  Array2D(Array2D&& src) noexcept;
  Array2D& operator=(Array2D&& src) noexcept;

  // Fills the array with a value.
  void Fill(T value) { std::fill_n(data_, num_elements_, value); }

  // If we are in SHARE_WITH_FOREIGN_INSTANCE mode,
  // the caller can choose to re-map the 2D array to some other buffer.
  // No other Array2Ds must depend on us for this to be possible.
  void RemapToNewBuffer(T* buffer);

  // Reallocates this Array2D to have height h and width w. The
  // existing contents of the array are discarded. Behaves nicely
  // whether the existing contents are owned by this array or the
  // array was instantiated with SHARE_WITH_FOREIGN_INSTANCE or
  // TAKEOVER_FROM_FOREIGN_INSTANCE.
  void Realloc(PD h, PD w);

  // Element accessors.
  T& operator()(PD y, PD x);
  const T& operator()(PD y, PD x) const;

  // Checks if an index is included in the bounds of the array.
  bool Contains(PD y, PD x) const;

  // Checks if an index +/- (dy,dx) is included in the bounds of the array.
  bool ContainsWithMargin(PD y, PD x, int dy, int dx) const;

  // Accessors for obvious 2D array information.
  PD width() const { return width_; }
  PD height() const { return height_; }
  PD num_elements() const { return num_elements_; }

  // Low-level accessor for stuff like memcmp, handle with care.
  T* data() const { return data_; }

  ~Array2D();

 private:
  // Resets to an "empty" state. The array has size of 0 x 0 and no
  // allocated data. The array is backed by an incompatible
  // leader. The only trickiness is that row_lut_ is nullptr which would
  // normally be invalid but with a height of 0 it can't be
  // dereferenced so it's safe.
  //
  // This is a valid state and, because it has no allocated data, it
  // can be easily overwritten with a different state.
  //
  // If this instance is not new then you must call Release() before
  // Reset() to deallocate any data.
  void Reset();

  // Completely initializes, but does not fill with any value, an instance that
  // owns the data.
  void InitOwned(PD h, PD w);

  // Called from various constructor-places.
  // Allocates the data buffer and precalculates the
  // row_lut_ lookup table.
  void AllocateDataAndLUT(PD h, PD w);

  // Allocates and calculates just the LUT.
  void AllocateLUT(PD h, PD w);

  // (Re)calculates the LUT.
  void RecalculateLUT(PD h, PD w);

  // Deletes all memory allocated by this Array2D. After Release(), it's
  // safe to either re-allocate the array or delete this instance.
  void Release();

  void AddMeAsFollower();
  void RemoveMeAsFollower();

  PD height_;
  PD width_;

  PD num_elements_;

  T* data_;                  // array elements
  T** row_lut_;              // row_lut_[i] points into data_
  bool owns_data_;           // own our data_?
  int number_of_followers_;  // incremented by sharing
  bool compatible_leader_;   // true if leader object is an Array2D
  Array2D* leader_;
};

// ----------------- Implementation ------------------

template <typename T>
Array2D<T>::Array2D() {
  Reset();
}

template <typename T>
Array2D<T>::Array2D(const PD h, const PD w) {
  InitOwned(h, w);
}

template <typename T>
Array2D<T>::Array2D(const PD h, const PD w, T default_value) {
  InitOwned(h, w);
  Fill(default_value);
}

template <typename T>
Array2D<T>::Array2D(InstantiationMode inst_mode, Array2D<T>* leader) {
  Reset();
  compatible_leader_ = true;
  leader_ = leader;

  switch (inst_mode) {
    case COPY_FROM_INSTANCE:
      height_ = leader->height_;
      width_ = leader->width_;

      num_elements_ = leader->num_elements_;

      AllocateDataAndLUT(height_, width_);

      owns_data_ = true;

      memcpy(data_, leader->data_, num_elements_ * sizeof(T));
      return;

    case SHARE_WITH_INSTANCE:
      height_ = leader->height_;
      width_ = leader->width_;

      num_elements_ = leader->num_elements_;

      owns_data_ = false;

      data_ = leader->data_;
      row_lut_ = leader->row_lut_;

      AddMeAsFollower();
      return;

    default:
      // Yes, yes, only in debug mode, otherwise it's too
      // much code bloat for an instantiation
      DLOG(FATAL) << "Unknown instantiation mode";
      return;
  }
}

template <typename T>
Array2D<T>::Array2D(InstantiationMode inst_mode, const PD h, const PD w,
                    T* buffer) {
  Reset();
  height_ = h;
  width_ = w;
  num_elements_ = h * w;
  data_ = buffer;

  DCHECK((inst_mode == SHARE_WITH_FOREIGN_INSTANCE) ||
         (inst_mode == TAKEOVER_FROM_FOREIGN_INSTANCE))
      << " Unsupported instantiation mode";

  CHECK_GE(h, 0);
  CHECK_GE(w, 0);

  if (inst_mode == TAKEOVER_FROM_FOREIGN_INSTANCE) owns_data_ = true;

  AllocateLUT(h, w);
}

template <typename T>
Array2D<T>::Array2D(const Array2D& src) {
  Reset();
  *this = src;
}

template <typename T>
Array2D<T>& Array2D<T>::operator=(const Array2D& src) {
  Release();
  InitOwned(src.height_, src.width_);
  memcpy(data_, src.data_, num_elements_ * sizeof(T));
  return *this;
}

template <typename T>
Array2D<T>::Array2D(Array2D&& src) noexcept {
  Reset();
  *this = std::move(src);
}

template <typename T>
Array2D<T>& Array2D<T>::operator=(Array2D&& src) noexcept {
  Release();
  height_ = src.height_;
  width_ = src.width_;
  num_elements_ = src.num_elements_;
  data_ = src.data_;
  row_lut_ = src.row_lut_;
  owns_data_ = src.owns_data_;
  number_of_followers_ = src.number_of_followers_;
  compatible_leader_ = src.compatible_leader_;
  leader_ = src.leader_;
  src.Reset();
  return *this;
}

template <typename T>
void Array2D<T>::InitOwned(const PD h, const PD w) {
  assert(h >= 0);
  assert(w >= 0);

  Reset();
  height_ = h;
  width_ = w;
  num_elements_ = h * w;
  owns_data_ = true;
  number_of_followers_ = 0;
  AllocateDataAndLUT(h, w);
}

template <typename T>
void Array2D<T>::Reset() {
  height_ = 0;
  width_ = 0;
  num_elements_ = 0;
  data_ = nullptr;
  row_lut_ = nullptr;
  owns_data_ = false;
  number_of_followers_ = 0;
  compatible_leader_ = false;
  leader_ = nullptr;
}

template <typename T>
void Array2D<T>::RemapToNewBuffer(T* new_buffer) {
  DCHECK(!owns_data_) << " You can't orphan this array's data!";
  DCHECK(!compatible_leader_) << " You cannot remap a buffer that belongs "
                                 "to an Array2D";
  // Don't allow arrays having dependents to suddenly change their
  // addresses as this is a source of leaks and hard-to-trace bugs.
  DCHECK_EQ(number_of_followers_, 0) << "You have arrays dependent on "
                                        "this Array2D!";

  data_ = new_buffer;

  RecalculateLUT(height_, width_);
}

template <typename T>
void Array2D<T>::Realloc(const PD h, const PD w) {
  assert(h >= 0);
  assert(w >= 0);

  Release();
  Reset();
  height_ = h;
  width_ = w;
  num_elements_ = h * w;
  owns_data_ = true;

  AllocateDataAndLUT(h, w);
}

template <typename T>
void Array2D<T>::Release() {
  DCHECK_EQ(number_of_followers_, 0)
      << "Arrays dependent on me are not deallocated\n";

  if (owns_data_) {
    delete[] data_;
    delete[] row_lut_;
  } else {
    if (compatible_leader_) {
      // and our leader is an Array2D
      RemoveMeAsFollower();
    } else {
      // and our leader is someone for whom we
      // had to allocate a row lookup table
      delete[] row_lut_;
    }
  }
}

template <class T>
Array2D<T>::~Array2D() {
  Release();
}

template <class T>
const T& Array2D<T>::operator()(const PD y, const PD x) const {
  DCHECK_GE(x, 0);
  DCHECK_GE(y, 0);
  DCHECK_LT(y, height_);
  DCHECK_LT(x, width_);

  return row_lut_[y][x];
}

template <class T>
T& Array2D<T>::operator()(const PD y, const PD x) {
  DCHECK_GE(x, 0);
  DCHECK_GE(y, 0);
  DCHECK_LT(y, height_);
  DCHECK_LT(x, width_);

  return row_lut_[y][x];
}

template <class T>
bool Array2D<T>::Contains(const PD y, const PD x) const {
  if (y < 0 || x < 0) return false;
  if (y >= height_ || x >= width_) return false;
  return true;
}

template <class T>
bool Array2D<T>::ContainsWithMargin(const PD y, const PD x, int dy,
                                    int dx) const {
  if (y < dy || x < dx) return false;
  if (y + dy >= height_ || x + dx >= width_) return false;
  return true;
}

template <class T>
void Array2D<T>::AllocateDataAndLUT(const PD h, const PD w) {
  data_ = new T[num_elements_];
  AllocateLUT(h, w);
}

template <class T>
void Array2D<T>::AllocateLUT(const PD h, const PD w) {
  row_lut_ = new T*[h];

  RecalculateLUT(h, w);
}

template <class T>
void Array2D<T>::RecalculateLUT(const PD h, const PD w) {
  for (int i = 0; i < h; ++i) row_lut_[i] = &data_[i * w];
}

template <class T>
void Array2D<T>::AddMeAsFollower() {
  DCHECK(!owns_data_);
  DCHECK(compatible_leader_);

  ++(leader_->number_of_followers_);
}

template <class T>
void Array2D<T>::RemoveMeAsFollower() {
  DCHECK(!owns_data_);
  DCHECK(compatible_leader_);

  --(leader_->number_of_followers_);
}

#endif  // THIRD_PARTY_GLOOP_UTIL_ARRAY_ARRAY2D_H_
