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

// Unit test cases for SafeInt containers.

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/hash/hash_testing.h"
#include "gloop/base/uword.h"
#include "gloop/util/intops/strong_array.h"
#include "gloop/util/intops/strong_fixedarray.h"
#include "gloop/util/intops/strong_int.h"
#include "gloop/util/intops/strong_vector.h"
#include "gtest/gtest.h"

ABSL_FLAG(int32_t, bm_bitmap_size, 10000, "Size of bitmap for benchmarks");

DEFINE_STRONG_INT_TYPE(StrongInt8, int8_t);
DEFINE_STRONG_INT_TYPE(StrongUInt8, uint8_t);
DEFINE_STRONG_INT_TYPE(StrongInt16, int16_t);
DEFINE_STRONG_INT_TYPE(StrongUInt16, uint16_t);
DEFINE_STRONG_INT_TYPE(StrongInt32, int32_t);
DEFINE_STRONG_INT_TYPE(StrongInt64, int64_t);
DEFINE_STRONG_INT_TYPE(StrongUInt32, uint32_t);
DEFINE_STRONG_INT_TYPE(StrongUInt64, uint64_t);
DEFINE_STRONG_INT_TYPE(StrongUWordt, uword_t);
DEFINE_STRONG_INT_TYPE(StrongLong, long);  // NOLINT

namespace util_intops {

// All tests below will be executed on all supported StrongInt<> types.
typedef ::testing::Types<StrongInt8, StrongUInt8, StrongInt16, StrongUInt16,
                         StrongInt32, StrongInt64, StrongUInt64, StrongUWordt,
                         StrongLong>
    SupportedStrongIntTypes;

// The types small enough to cause index overflows.
typedef ::testing::Types<StrongInt8, StrongUInt8, StrongInt16, StrongUInt16>
    EasyToOverflowStrongIntTypes;

// Array ----------------------------------------------------------------------
//

template <typename _T>
class StrongArrayTest : public ::testing::Test {
 protected:
  typedef _T T;
  typedef StrongArray<T, std::string> ArrayType;
  typedef StrongArray<T, const std::string> ConstArrayType;

  StrongArrayTest() {}
};

TYPED_TEST_SUITE(StrongArrayTest, SupportedStrongIntTypes);

TYPED_TEST(StrongArrayTest, TestSubscriptOperator) {
  std::string base_array[2];
  typename TestFixture::ArrayType iti_array(base_array);
  // operator []
  typename TestFixture::T i(0);
  std::string& ref_0 = iti_array[i];
  ref_0 = "cool";
  typename TestFixture::ArrayType::value_type& ref_1 = iti_array[++i];
  ref_1 = "thing";
  const std::string& const_ref_0 = iti_array[--i];
  const typename TestFixture::ArrayType::value_type& const_ref_1 =
      iti_array[++i];
  EXPECT_EQ("cool", const_ref_0);
  EXPECT_EQ("thing", const_ref_1);
  iti_array[typename TestFixture::T(0)] = "warm";
  EXPECT_EQ("warm", const_ref_0);

  // Indexing into a const array:
  const typename TestFixture::ArrayType const_iti_array(base_array);
  const std::string& const_ref_1b = const_iti_array[i];
  EXPECT_EQ("thing", const_ref_1b);
}

TYPED_TEST(StrongArrayTest, TestPointerMath) {
  std::string foo[5];
  std::string* bar = foo + 2;
  typename TestFixture::ArrayType iti_foo(foo);
  EXPECT_EQ(foo, iti_foo.data());

  typename TestFixture::ArrayType iti_bar =
      iti_foo + typename TestFixture::T(2);
  EXPECT_EQ(bar, iti_bar.data());

  typename TestFixture::ArrayType iti_foo2 =
      iti_bar - typename TestFixture::T(2);
  EXPECT_EQ(foo, iti_foo2.data());

  const typename TestFixture::ArrayType const_iti_bar =
      iti_foo + typename TestFixture::T(2);
  EXPECT_EQ(bar, const_iti_bar.data());

  const typename TestFixture::ArrayType const_iti_foo2 =
      const_iti_bar - typename TestFixture::T(2);
  EXPECT_EQ(foo, const_iti_foo2.data());
}

TYPED_TEST(StrongArrayTest, TestConst) {
  const std::string foo[5];
  const typename TestFixture::ConstArrayType iti_foo(foo);
  EXPECT_EQ(foo, iti_foo.data());
}

TYPED_TEST(StrongArrayTest, TestConstSubscriptOperator) {
  const std::string base_array[2];
  typename TestFixture::ConstArrayType iti_array(base_array);
  // operator []
  typename TestFixture::T i(0);
  typename TestFixture::ConstArrayType::value_type& ref_0 = iti_array[i];
  ASSERT_EQ(&ref_0, base_array + 0);
  const std::string& ref_1 = iti_array[++i];
  ASSERT_EQ(&ref_1, base_array + 1);
}

TYPED_TEST(StrongArrayTest, TestConstPointerMath) {
  const std::string foo[5];
  const std::string* bar = foo + 2;
  typename TestFixture::ConstArrayType iti_foo(foo);
  EXPECT_EQ(foo, iti_foo.data());

  typename TestFixture::T delta(2);
  typename TestFixture::ConstArrayType iti_bar = iti_foo + delta;
  EXPECT_EQ(bar, iti_bar.data());

  typename TestFixture::ConstArrayType iti_foo2 = iti_bar - delta;
  EXPECT_EQ(foo, iti_foo.data());

  const typename TestFixture::ConstArrayType const_iti_bar =
      iti_foo + typename TestFixture::T(2);
  EXPECT_EQ(bar, const_iti_bar.data());

  const typename TestFixture::ConstArrayType const_iti_foo2 =
      const_iti_bar - typename TestFixture::T(2);
  EXPECT_EQ(foo, const_iti_foo2.data());
}

TYPED_TEST(StrongArrayTest, TestMutableToConstAssignment) {
  std::string base_array[2];
  typename TestFixture::ArrayType mutable_array(base_array);
  mutable_array[typename TestFixture::T(0)] = "hello";
  typename TestFixture::ConstArrayType const_array(base_array);
  EXPECT_EQ("hello", const_array[typename TestFixture::T(0)]);
}

TYPED_TEST(StrongArrayTest, DefaultConstructor) {
  // Vector creates a StrongArray using the default constructor.
  std::vector<typename TestFixture::ArrayType> v(1);
  // Jumps through the array to get the right type, literal NULL doesn't work.
  EXPECT_EQ(v[0].data(), typename TestFixture::ArrayType(nullptr).data())
      << "The default constructor populates StrongArray with a NULL pointer";
}

// FixedArray -----------------------------------------------------------------
//
// As our wrapper only calls the corresponding FixedArray methods, we refrain
// from doing an extensive test for the fixed array functionality.  Instead, we
// essentially test our modifications to operator [] and perform basic sanity
// testing to all other pass-through methods.

template <typename _T>
class StrongFixedArrayTest : public ::testing::Test {
 protected:
  typedef _T T;
  typedef StrongFixedArray<T, std::string> ArrayType;

  StrongFixedArrayTest() : array_(nullptr) {}

  void NewArrayOfSize(int n) { array_ = new ArrayType(n); }
  void NewArrayOfSize(T n) { array_ = new ArrayType(n); }
  void TearDown() override { delete array_; }

  ArrayType* array_;
};

TYPED_TEST_SUITE(StrongFixedArrayTest, SupportedStrongIntTypes);

TYPED_TEST(StrongFixedArrayTest, TestSubscriptOperator) {
  this->NewArrayOfSize(2);
  // operator []
  typename TestFixture::T i(0);
  typename TestFixture::ArrayType::reference ref_0 = (*this->array_)[i];
  ref_0 = "cool";
  typename TestFixture::ArrayType::reference ref_1 = (*this->array_)[++i];
  ref_1 = "thing";
  typename TestFixture::ArrayType::const_reference const_ref_0 =
      (*this->array_)[--i];
  typename TestFixture::ArrayType::const_reference const_ref_1 =
      (*this->array_)[++i];
  EXPECT_EQ("cool", const_ref_0);
  EXPECT_EQ("thing", const_ref_1);
}

TYPED_TEST(StrongFixedArrayTest, TestSize) {
  this->NewArrayOfSize(2);
  EXPECT_EQ(2, this->array_->size());
}

TYPED_TEST(StrongFixedArrayTest, TestTypedSize) {
  this->NewArrayOfSize(typename TestFixture::T(2));
  EXPECT_EQ(2, this->array_->size());
}

TYPED_TEST(StrongFixedArrayTest, TestData) {
  typedef typename TestFixture::ArrayType::pointer ptr;
  typedef typename TestFixture::ArrayType::const_pointer const_ptr;
  typedef typename TestFixture::ArrayType::iterator iter;
  typedef typename TestFixture::ArrayType::const_iterator const_iter;

  this->NewArrayOfSize(10);

  ptr data = this->array_->data();
  iter begin = this->array_->begin();
  EXPECT_EQ(data, begin);

  const_ptr const_data = this->array_->data();
  const_iter const_begin = this->array_->begin();
  EXPECT_EQ(const_data, const_begin);
}

TYPED_TEST(StrongFixedArrayTest, TestIterators) {
  typename TestFixture::ArrayType::iterator it;
  typename TestFixture::ArrayType::const_iterator const_it;

  this->NewArrayOfSize(3);

  typename TestFixture::T i(0);
  (*this->array_)[i] = "a";
  (*this->array_)[++i] = "b";
  (*this->array_)[++i] = "c";
  it = this->array_->begin();
  const_it = this->array_->begin();
  EXPECT_EQ("a", *it);
  EXPECT_EQ("a", *const_it);
  it = this->array_->end();
  const_it = this->array_->end();
  EXPECT_EQ("c", *(it - 1));
  EXPECT_EQ("c", *(const_it - 1));
}

TYPED_TEST(StrongFixedArrayTest, TestIteration) {
  this->NewArrayOfSize(3);
  typename TestFixture::T i(0);
  (*this->array_)[i] = "a";
  (*this->array_)[++i] = "b";
  (*this->array_)[++i] = "c";
  std::string result;
  for (auto i : this->array_->index_range()) {
    result += (*this->array_)[i];
  }
  EXPECT_EQ("abc", result);
}
// STL Vector -----------------------------------------------------------------
//
// As our wrapper only calls the corresponding STL vector methods, we refrain
// from doing an extensive test for the vector functionality.  Instead, we
// essentially test our modifications to the at() methods and operator [] and
// perform basic sanity testing to all other pass-through methods.

template <typename _T>
class StrongVectorTest : public ::testing::Test {
 protected:
  typedef _T T;
  typedef StrongVector<T, std::string> VectorType;
  typedef typename VectorType::ParentType ParentType;

  // Vector<string> indexed by value of type T.
  VectorType vec_;
  VectorType vec_tmp_;
  std::string cool_ = "cool";
  std::string thing_ = "thing";
};

TYPED_TEST_SUITE(StrongVectorTest, SupportedStrongIntTypes);

TYPED_TEST(StrongVectorTest, TestConstructor) {
  typename TestFixture::VectorType vec(2);
  EXPECT_EQ(2, vec.size());
}

TYPED_TEST(StrongVectorTest, TestTypedConstructor) {
  typename TestFixture::VectorType vec(typename TestFixture::T(2));
  EXPECT_EQ(2, vec.size());
}

TYPED_TEST(StrongVectorTest, TestCopyConstructor) {
  this->vec_.push_back(this->cool_);
  this->vec_.push_back(this->thing_);
  typename TestFixture::VectorType vec_tmp(this->vec_);
  EXPECT_TRUE(this->vec_ == vec_tmp);
}

TYPED_TEST(StrongVectorTest, TestAccessors) {
  this->vec_.push_back(this->cool_);
  this->vec_.push_back(this->thing_);
  EXPECT_EQ(2, this->vec_.size());
  typename TestFixture::ParentType* mutable_vec = this->vec_.mutable_get();
  const typename TestFixture::ParentType& vec = this->vec_.get();
  EXPECT_EQ(2, mutable_vec->size());
  EXPECT_EQ(2, vec.size());
  EXPECT_EQ(this->cool_, (*mutable_vec)[0]);
  EXPECT_EQ(this->cool_, vec[0]);
  EXPECT_EQ(this->thing_, (*mutable_vec)[1]);
  EXPECT_EQ(this->thing_, vec[1]);
}

TYPED_TEST(StrongVectorTest, TestSubscriptOperatorAndAt) {
  typename TestFixture::T i(0);
  // operator [] and at
  this->vec_.push_back(this->cool_);
  this->vec_.push_back(this->thing_);
  typename TestFixture::VectorType::reference ref_0 = this->vec_[i];
  typename TestFixture::VectorType::const_reference const_ref_0 = this->vec_[i];
  EXPECT_EQ(this->cool_, ref_0);
  EXPECT_EQ(this->cool_, const_ref_0);
  ++i;
  typename TestFixture::VectorType::reference ref_1 = this->vec_[i];
  typename TestFixture::VectorType::const_reference const_ref_1 = this->vec_[i];
  EXPECT_EQ(this->thing_, ref_1);
  EXPECT_EQ(this->thing_, const_ref_1);
}

TYPED_TEST(StrongVectorTest, TestPushPopBack) {
  this->vec_.push_back(this->cool_);
  EXPECT_EQ(this->cool_, this->vec_.front());
  this->vec_.pop_back();
  EXPECT_EQ(0, this->vec_.size());
}

TYPED_TEST(StrongVectorTest, TestMovePushPopBack) {
  std::string cool_two(this->cool_);
  this->vec_.push_back(std::move(cool_two));
  EXPECT_EQ(this->cool_, this->vec_.front());
  this->vec_.pop_back();
  EXPECT_EQ(0, this->vec_.size());
}

TYPED_TEST(StrongVectorTest, TestAssignment) {
  this->vec_tmp_.push_back(this->cool_);
  this->vec_ = this->vec_tmp_;
  EXPECT_EQ(1, this->vec_.size());
  EXPECT_EQ(this->cool_, this->vec_.front());
}

TYPED_TEST(StrongVectorTest, TestClearAndEmpty) {
  this->vec_.push_back(this->cool_);
  EXPECT_EQ(1, this->vec_.size());
  this->vec_.clear();
  EXPECT_EQ(0, this->vec_.size());
  EXPECT_EQ(true, this->vec_.empty());
}

TYPED_TEST(StrongVectorTest, TestAssign) {
  // assign(n, val)
  this->vec_.assign(5, this->cool_);
  EXPECT_EQ(5, this->vec_.size());

  // assign(first, last)
  typename TestFixture::VectorType::iterator it;
  it = this->vec_.begin() + 1;
  this->vec_.assign(it, this->vec_.end() - 1);  // remove extremes
  EXPECT_EQ(3, this->vec_.size());
}

TYPED_TEST(StrongVectorTest, TestIterators) {
  typename TestFixture::VectorType::iterator it;
  typename TestFixture::VectorType::const_iterator const_it;
  typename TestFixture::VectorType::reverse_iterator rev_it;
  typename TestFixture::VectorType::const_reverse_iterator const_rev_it;

  this->vec_.push_back("a");
  this->vec_.push_back("b");
  this->vec_.push_back("c");
  it = this->vec_.begin();
  const_it = this->vec_.begin();
  EXPECT_EQ("a", *it);
  EXPECT_EQ("a", *const_it);
  it = this->vec_.end();
  const_it = this->vec_.end();
  EXPECT_EQ("c", *(it - 1));
  EXPECT_EQ("c", *(const_it - 1));
  rev_it = this->vec_.rbegin();
  const_rev_it = this->vec_.rbegin();
  EXPECT_EQ("c", *rev_it);
  EXPECT_EQ("c", *const_rev_it);
  rev_it = this->vec_.rend();
  const_rev_it = this->vec_.rend();
  EXPECT_EQ("a", *(rev_it - 1));
  EXPECT_EQ("a", *(const_rev_it - 1));
}

TYPED_TEST(StrongVectorTest, TestSizes) {
  this->vec_.push_back(this->cool_);
  EXPECT_EQ(1, this->vec_.size());
  this->vec_.push_back(this->thing_);
  EXPECT_EQ(2, this->vec_.size());
  // Let's ensure that max_size() equals the underlying vector's max_size().
  // We extract the underlying vector using the accessor method.
  EXPECT_EQ(this->vec_.get().max_size(), this->vec_.max_size());
}

TYPED_TEST(StrongVectorTest, TestResize) {
  // resize down
  this->vec_.push_back(this->cool_);
  this->vec_.push_back(this->thing_);
  EXPECT_EQ(2, this->vec_.size());
  this->vec_.resize(1);
  EXPECT_EQ(1, this->vec_.size());
  EXPECT_EQ(this->cool_, this->vec_.front());
  // resize up
  this->vec_.resize(5, "!!");
  EXPECT_EQ(5, this->vec_.size());
  EXPECT_EQ(this->cool_, this->vec_.front());  // ensure it kept cool
  for (typename TestFixture::T i(1); i.value() < 5; ++i) {
    EXPECT_EQ("!!", this->vec_[i]);
  }
}

TYPED_TEST(StrongVectorTest, TestTypedResize) {
  // resize down
  this->vec_.push_back(this->cool_);
  this->vec_.push_back(this->thing_);
  EXPECT_EQ(2, this->vec_.size());
  this->vec_.resize(typename TestFixture::T(1));
  EXPECT_EQ(1, this->vec_.size());
  EXPECT_EQ(this->cool_, this->vec_.front());
  // resize up
  this->vec_.resize(typename TestFixture::T(5), "!!");
  EXPECT_EQ(5, this->vec_.size());
  EXPECT_EQ(this->cool_, this->vec_.front());  // ensure it kept cool
  for (typename TestFixture::T i(1); i.value() < 5; ++i) {
    EXPECT_EQ("!!", this->vec_[i]);
  }
}

TYPED_TEST(StrongVectorTest, TestCapacityAndReserve) {
  this->vec_.reserve(5);
  EXPECT_EQ(5, this->vec_.capacity());
}

TYPED_TEST(StrongVectorTest, TestTypedCapacityAndReserve) {
  this->vec_.reserve(typename TestFixture::T(5));
  EXPECT_EQ(5, this->vec_.capacity());
}

TYPED_TEST(StrongVectorTest, TestFrontBackData) {
  typedef typename TestFixture::VectorType::reference ref;
  typedef typename TestFixture::VectorType::const_reference const_ref;
  typedef typename TestFixture::VectorType::pointer ptr;
  typedef typename TestFixture::VectorType::const_pointer const_ptr;

  this->vec_.push_back(this->cool_);
  this->vec_.push_back(this->thing_);
  this->vec_.push_back("!!");

  ref front = this->vec_.front();
  const_ref const_front = this->vec_.front();
  EXPECT_EQ(this->cool_, front);
  EXPECT_EQ(this->cool_, const_front);
  ref back = this->vec_.back();
  const_ref const_back = this->vec_.back();
  EXPECT_EQ("!!", back);
  EXPECT_EQ("!!", const_back);
  ptr data = this->vec_.data();
  const_ptr const_data = this->vec_.data();
  EXPECT_EQ(this->vec_.get().data(), data);
  EXPECT_EQ(this->vec_.get().data(), const_data);
}

TYPED_TEST(StrongVectorTest, TestInsertAndErase) {
  typename TestFixture::T i;
  // insert
  this->vec_.insert(this->vec_.cbegin(), this->cool_);
  EXPECT_EQ(1, this->vec_.size());
  EXPECT_EQ(this->cool_, this->vec_.front());
  this->vec_.insert(this->vec_.cend(), 2, "!!");
  EXPECT_EQ(3, this->vec_.size());
  for (i = typename TestFixture::T(1); i.value() < 3; ++i) {
    EXPECT_EQ("!!", this->vec_[i]);
  }
  std::vector<std::string> copy(this->vec_.end() - 1, this->vec_.end());
  this->vec_.insert(this->vec_.cbegin(), copy.begin(), copy.end());
  EXPECT_EQ(4, this->vec_.size());
  for (i = typename TestFixture::T(0); i.value() < 4; ++i) {
    if (i.value() == 0 || i.value() > 1) {
      EXPECT_EQ("!!", this->vec_[i]);
    } else {
      EXPECT_EQ(this->cool_, this->vec_[i]);
    }
  }
  // erase
  this->vec_.erase(this->vec_.begin() + 1);
  EXPECT_EQ(3, this->vec_.size());
  for (i = typename TestFixture::T(0); i.value() < 3; ++i) {
    EXPECT_EQ("!!", this->vec_[i]);
  }
  this->vec_.erase(this->vec_.begin(), this->vec_.end());
  EXPECT_EQ(0, this->vec_.size());
}

TYPED_TEST(StrongVectorTest, TestMoveInsert) {
  std::string cool_two(this->cool_);
  this->vec_.insert(this->vec_.cbegin(), std::move(cool_two));
  EXPECT_EQ(1, this->vec_.size());
  EXPECT_EQ(this->cool_, this->vec_.front());
}

TYPED_TEST(StrongVectorTest, TestEmplace) {
  auto iter_0 = this->vec_.emplace(this->vec_.begin(), this->cool_.c_str());
  EXPECT_EQ(this->vec_.begin(), iter_0);
  EXPECT_EQ(1, this->vec_.size());
  EXPECT_EQ(this->cool_, this->vec_.front());
  auto iter_1 = this->vec_.emplace(this->vec_.begin(), this->thing_.c_str());
  EXPECT_EQ(this->vec_.begin(), iter_1);
  EXPECT_EQ(2, this->vec_.size());
  EXPECT_EQ(this->thing_, this->vec_.front());
  EXPECT_EQ(this->cool_, this->vec_.back());
}

TYPED_TEST(StrongVectorTest, TestEmplaceBack) {
  auto* ptr = &this->vec_.emplace_back(this->cool_.c_str());
  EXPECT_EQ(1, this->vec_.size());
  EXPECT_EQ(&this->vec_.back(), ptr);
  EXPECT_EQ(this->cool_, this->vec_.front());
  ptr = &this->vec_.emplace_back(this->thing_.c_str());
  EXPECT_EQ(2, this->vec_.size());
  EXPECT_EQ(&this->vec_.back(), ptr);
  EXPECT_EQ(this->cool_, this->vec_.front());
  EXPECT_EQ(this->thing_, this->vec_.back());
}

TYPED_TEST(StrongVectorTest, TestSwap) {
  // member swap
  this->vec_tmp_.push_back("vec_tmp");
  this->vec_.push_back("vec");
  this->vec_.swap(this->vec_tmp_);
  EXPECT_EQ("vec", this->vec_tmp_.front());
  EXPECT_EQ("vec_tmp", this->vec_.front());
  // non-member swap
  swap(this->vec_, this->vec_tmp_);
  EXPECT_EQ("vec", this->vec_.front());
  EXPECT_EQ("vec_tmp", this->vec_tmp_.front());
}

TYPED_TEST(StrongVectorTest, TestStartEndIndexIteration) {
  for (int limit = 0; limit < 5; ++limit) {
    this->vec_.clear();
    EXPECT_EQ(0, this->vec_.size());
    this->vec_tmp_.clear();
    EXPECT_EQ(0, this->vec_tmp_.size());
    for (int i = 0; i < limit; ++i) {
      std::string s(i, 'x');
      this->vec_.push_back(s);
      this->vec_tmp_.push_back(s);
    }
    // Verifies preconditions.
    ASSERT_EQ(limit, this->vec_.size());
    ASSERT_EQ(limit, this->vec_tmp_.size());
    int iteration_counter = 0;
    for (typename TestFixture::T i = this->vec_.start_index();
         i < this->vec_.end_index(); ++i) {
      EXPECT_EQ(this->vec_[i], this->vec_tmp_[i]);
      iteration_counter++;
    }
    EXPECT_EQ(limit, iteration_counter);
  }
}

TYPED_TEST(StrongVectorTest, TestIndexRangeIteration) {
  for (int limit = 0; limit < 5; ++limit) {
    this->vec_.clear();
    EXPECT_EQ(0, this->vec_.size());
    this->vec_tmp_.clear();
    EXPECT_EQ(0, this->vec_tmp_.size());
    for (int i = 0; i < limit; ++i) {
      std::string s(i, 'x');
      this->vec_.push_back(s);
      this->vec_tmp_.push_back(s);
    }
    // Verifies preconditions.
    ASSERT_EQ(limit, this->vec_.size());
    ASSERT_EQ(limit, this->vec_tmp_.size());
    int iteration_counter = 0;
    for (const typename TestFixture::T i : this->vec_.index_range()) {
      EXPECT_EQ(this->vec_[i], this->vec_tmp_[i]);
      iteration_counter++;
    }
    EXPECT_EQ(limit, iteration_counter);
  }
}

TYPED_TEST(StrongVectorTest, TestInitializerListOperations) {
  for (int limit = 0; limit < 5; ++limit) {
    typename TestFixture::VectorType vec({"ab", "cd"});
    this->vec_ = {"ab", "cd"};
    this->vec_tmp_.assign({"ab", "cd"});
    for (auto i : {&this->vec_, &this->vec_tmp_, &vec}) {
      ASSERT_EQ(2, i->size());
      EXPECT_EQ("ab", (*i)[typename TestFixture::T{0}]);
      EXPECT_EQ("cd", (*i)[typename TestFixture::T{1}]);
    }
  }
}

TYPED_TEST(StrongVectorTest, AbslHash) {
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      {typename TestFixture::VectorType{}, typename TestFixture::VectorType{""},
       typename TestFixture::VectorType{"a"},
       typename TestFixture::VectorType{"b"},
       typename TestFixture::VectorType{"a", ""},
       typename TestFixture::VectorType{"a", "b"},
       typename TestFixture::VectorType{"", "a"},
       typename TestFixture::VectorType{"b", "a"}}));
}

template <typename _T>
class OverflowStrongVectorTest : public ::testing::Test {
 protected:
  typedef _T T;
  typedef StrongVector<T, std::string> VectorType;
  enum { kLimit = std::numeric_limits<typename T::ValueType>::max() };
};

TYPED_TEST_SUITE(OverflowStrongVectorTest, EasyToOverflowStrongIntTypes);

TYPED_TEST(OverflowStrongVectorTest, Constructor) {
  typename TestFixture::VectorType v(TestFixture::kLimit);
  EXPECT_EQ(v.size(), TestFixture::kLimit) << "Works fine at the limit";
  EXPECT_DEBUG_DEATH(typename TestFixture::VectorType(TestFixture::kLimit + 1),
                     "Overflow")
      << "Dies outside the limit";
}

TYPED_TEST(OverflowStrongVectorTest, Resize) {
  typename TestFixture::VectorType v;
  v.resize(TestFixture::kLimit);
  EXPECT_EQ(v.size(), TestFixture::kLimit) << "Works fine at the limit";
  EXPECT_DEBUG_DEATH(v.resize(TestFixture::kLimit + 1), "Overflow")
      << "Dies outside the limit";
}

TYPED_TEST(OverflowStrongVectorTest, Reserve) {
  typename TestFixture::VectorType v;
  v.reserve(TestFixture::kLimit);
  EXPECT_DEBUG_DEATH(v.reserve(TestFixture::kLimit + 1), "Overflow")
      << "Dies outside the limit";
}

TYPED_TEST(OverflowStrongVectorTest, Insert) {
  typename TestFixture::VectorType v;
  v.insert(v.cend(), TestFixture::kLimit, "ok");
  EXPECT_EQ(v.size(), TestFixture::kLimit) << "Works fine at the limit";
  EXPECT_DEBUG_DEATH(v.insert(v.cend(), 1, "fatal"), "Overflow")
      << "Dies outside the limit";
}

template <typename _T>
class StrongVectorUniquePtrTest : public ::testing::Test {
 protected:
  typedef _T T;
  typedef StrongVector<T, std::unique_ptr<std::string>> VectorType;
  enum { kLimit = std::numeric_limits<typename T::ValueType>::max() };
};

TYPED_TEST_SUITE(StrongVectorUniquePtrTest, SupportedStrongIntTypes);

TYPED_TEST(StrongVectorUniquePtrTest, PushAndEmplace) {
  typename TestFixture::VectorType v;
  std::unique_ptr<std::string> foo;
  v.push_back(std::move(foo));
  v.emplace_back(new std::string);
}

TYPED_TEST(StrongVectorUniquePtrTest, MoveConstructorAndAssignment) {
  typename TestFixture::VectorType v1;
  std::unique_ptr<std::string> foo;
  v1.push_back(std::move(foo));
  v1.emplace_back(new std::string("move"));

  using IntType = typename TestFixture::T;
  typename TestFixture::VectorType v2(std::move(v1));
  ASSERT_EQ(2, v2.size());
  ASSERT_EQ(nullptr, v2.at(IntType(0)).get());
  ASSERT_EQ("move", *v2.at(IntType(1)));

  typename TestFixture::VectorType v3;
  v3 = std::move(v2);
  ASSERT_EQ(2, v3.size());
  ASSERT_EQ(nullptr, v3.at(IntType(0)).get());
  ASSERT_EQ("move", *v3.at(IntType(1)));

  StrongVector<IntType, typename TestFixture::VectorType> v4;
  v4.push_back(std::move(v3));
  ASSERT_EQ(2, v4.back().size());
  ASSERT_EQ(nullptr, v4.back().at(IntType(0)).get());
  ASSERT_EQ("move", *v4.back().at(IntType(1)));
}

TYPED_TEST(StrongVectorUniquePtrTest, SizedConstructor) {
  typename TestFixture::VectorType v1(4);
}

TYPED_TEST(StrongVectorUniquePtrTest, CanResize) {
  typename TestFixture::VectorType v1;
  v1.resize(4);
}

}  // namespace util_intops
