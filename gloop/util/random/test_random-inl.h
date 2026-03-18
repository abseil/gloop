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

//
// This file contains some helper classes used to test
// the RNGs and algorithms.  DO NOT USE in regular code.
// The aim is to make checking algorithms against known
// sequences easier.
#ifndef UTIL_RANDOM_TEST_RANDOM_INL_H__
#define UTIL_RANDOM_TEST_RANDOM_INL_H__

#include <cstdint>
#include <initializer_list>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "gloop/util/random/random_base.h"

namespace random_test {

// Sequential base class provides common default implementations
// of virtual methods from RandomBase.  Other users need to
// provide Rand64() and Clone().
class Sequence : public RandomBase {
 public:
  uint8_t Rand8() override {
    return static_cast<uint8_t>(Rand64() & 0x000000ff);
  }

  uint16_t Rand16() override {
    return static_cast<uint16_t>(Rand64() & 0x0000ffff);
  }

  uint32_t Rand32() override {
    return static_cast<uint32_t>(Rand64() & 0xffffffff);
  }
};

// FixedValueSequence: Always return the same value.
class FixedValueSequence : public Sequence {
 public:
  // Initialize with the value to return.
  explicit FixedValueSequence(uint64_t v) : v_(v) {}

  // This type is neither copyable nor movable.
  FixedValueSequence(const FixedValueSequence&) = delete;
  FixedValueSequence& operator=(const FixedValueSequence&) = delete;

  FixedValueSequence* Clone() const override {
    return new FixedValueSequence(v_);
  }

  uint64_t Rand64() override { return v_; }

 private:
  uint64_t v_;
};

// IncrementingSequence: return sequentially increasing values.
class IncrementingSequence : public Sequence {
 public:
  // Initialize with the initial value (default 1)
  explicit IncrementingSequence(uint32_t v) : v_(v) {}

  // This type is neither copyable nor movable.
  IncrementingSequence(const IncrementingSequence&) = delete;
  IncrementingSequence& operator=(const IncrementingSequence&) = delete;

  IncrementingSequence* Clone() const override {
    return new IncrementingSequence(v_);
  }

  uint64_t Rand64() override { return v_++; }

 private:
  uint64_t v_;
};

// DecrementingSequence: return sequentially decreasing values.
class DecrementingSequence : public Sequence {
 public:
  // Initialize with the initial value. (default kuint64max)
  explicit DecrementingSequence(uint64_t v) : v_(v) {}

  // This type is neither copyable nor movable.
  DecrementingSequence(const DecrementingSequence&) = delete;
  DecrementingSequence& operator=(const DecrementingSequence&) = delete;

  DecrementingSequence* Clone() const override {
    return new DecrementingSequence(v_);
  }

  uint64_t Rand64() override { return v_--; }

 private:
  uint64_t v_;
};

// VectorSequence: Return the next value in a vector, in a loop.
// Returns 0 when the vector is empty.
//
// iterator / array ex:
//   uint64 array[] = { 0, 1, 2, 3 };
//   VectorSequence s(array, array+ABSL_ARRAYSIZE(array));
//
// vector ex:
//   vector<uint64> seq = { 0, 1, 2, 3 };
//   VectorSequence s(seq);
//
// initializer list ex:
//   VectorSequence s({ 0, 1, 2, 3 });
class VectorSequence : public Sequence {
 public:
  // Initially empty
  VectorSequence() : pos_(0) {}

  // Initialize with iterators (begin, end)
  template <typename T>
  VectorSequence(T begin, T end) : pos_(0), sequence_(begin, end) {}

  template <typename C>
  explicit VectorSequence(C collection)
      : VectorSequence(collection.begin(), collection.end()) {}

  explicit VectorSequence(std::initializer_list<int64_t> init)
      : VectorSequence(init.begin(), init.end()) {}

  // This type is neither copyable nor movable.
  VectorSequence(const VectorSequence&) = delete;
  VectorSequence& operator=(const VectorSequence&) = delete;

  // Append a value to the end of the sequence.
  void Append(uint64_t v) { sequence_.push_back(v); }

  VectorSequence* Clone() const override {
    VectorSequence* twin = new VectorSequence(sequence_);
    twin->pos_ = pos_;
    return twin;
  }

  uint64_t Rand64() override {
    if (sequence_.empty()) {
      return 0;
    } else if (pos_ == sequence_.size()) {
      pos_ = 0;
    }
    return sequence_.at(pos_++);
  }

  // Reset the sequence to the beginning.
  void Reset() { pos_ = 0; }

 private:
  uint64_t pos_;
  std::vector<int64_t> sequence_;
};

// Random number generator with only RandDouble() implemented.
// The returned values come from the sequence provided in the constructor.
// By analogy with VectorSequence, values are provided in a loop, and,
// if sequence is empty, 0.0 is returned.
// The specified values must be in the range [0.0, 1.0).
//
// WARNING: RandDouble() generates a double in the range [1.0, 2.0) (it can
// generate any double in that range), then subtracts 1.0 from it; therefore
// not any double in the range [0.0, 1.0) can be generated.  For example,
// the range of strictly positive values that RandDouble() can generate is
// [2^-52, 1 - 2^-52].  This class, however, accepts all values in [0.0, 1.0).
class RandDoubleSequence : public RandomBase {
 public:
  explicit RandDoubleSequence(std::vector<double> sequence)
      : sequence_(sequence), it_(sequence_.begin()) {
    CheckValues();
  }

  RandDoubleSequence* Clone() const override {
    RandDoubleSequence* twin = new RandDoubleSequence(sequence_);
    twin->it_ = it_;
    return twin;
  }

  uint8_t Rand8() override { LOG(FATAL); }
  uint16_t Rand16() override { LOG(FATAL); }
  uint32_t Rand32() override { LOG(FATAL); }
  uint64_t Rand64() override { LOG(FATAL); }

  double RandDouble() override {
    if (sequence_.empty()) {
      return 0.0;
    }
    if (it_ == sequence_.end()) {
      it_ = sequence_.begin();
    }
    return *it_++;
  }

 private:
  void CheckValues() {
    for (double v : sequence_) {
      CHECK(0.0 <= v && v < 1.0) << v;
    }
  }

  const std::vector<double> sequence_;
  std::vector<double>::const_iterator it_;
};

}  // namespace random_test

#endif  // UTIL_RANDOM_TEST_RANDOM_INL_H__
