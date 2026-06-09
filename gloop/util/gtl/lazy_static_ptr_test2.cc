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

#include <sched.h>

#include <array>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "gloop/thread/thread.h"
#include "gloop/util/gtl/lazy_static_ptr.h"
#include "gloop/util/gtl/lazy_static_ptr_test.h"
#include "gtest/gtest.h"

ABSL_FLAG(int32_t, numthreads, 10, "Number of threads");
ABSL_FLAG(int32_t, iters, 1000, "Number of iterations per thread");

namespace gtl {

// Purely for comment: the following external declarations won't work, see
// bottom of file
// extern string g_file_one_string;
// extern string g_file_three_string;

namespace {

ABSL_CONST_INIT static absl::Mutex lock(absl::kConstInit);
static int ctor_count = 0;
static void RecordCtor() {
  absl::MutexLock l(lock);
  ctor_count += 1;
  LOG(INFO) << "Blob c-tor " << ctor_count;
}

// Make sure forward-declaring usage works:
class Blob;

extern LazyStaticPtr<Blob> b0;

class Blob {
 public:
  Blob() : a1(""), a2(0), a3(1.5) { RecordCtor(); }
  explicit Blob(const char* x1) : a1(x1), a2(0), a3(2.5) { RecordCtor(); }
  Blob(const char* x1, int x2) : a1(x1), a2(x2), a3(3.5) { RecordCtor(); }
  Blob(const char* x1, int x2, double x3) : a1(x1), a2(x2), a3(x3) {
    RecordCtor();
  }
  ~Blob() { LOG(FATAL) << "Not to be called"; }

 public:
  std::string a1;
  int a2;
  double a3;
};

LazyStaticPtr<Blob> b0;

static LazyStaticPtr<Blob, const char*> b1 = {"b1"};
constexpr static auto b2 = gtl::MakeLazyPtr<const Blob>("b2", 2);

// this and the other test are not supposed to be run in parallel
TEST(LazyStaticPtr, Simple) {
  const int ctors = ctor_count;
  // initialization:
  EXPECT_EQ(ctor_count, ctors + 0);
  EXPECT_EQ(b0->a1, "");
  EXPECT_EQ(ctor_count, ctors + 1);
  EXPECT_EQ(b0->a2, 0);
  EXPECT_EQ(ctor_count, ctors + 1);
  EXPECT_EQ(b1->a1, "b1");
  EXPECT_EQ(b1->a2, 0);
  EXPECT_EQ(ctor_count, ctors + 2);
  EXPECT_EQ(b2->a1, "b2");
  EXPECT_EQ(b2->a2, 2);
  EXPECT_EQ(ctor_count, ctors + 3);

  // modification and * access:
  b0->a1 = "b0";
  (*b0).a2 = -1;
  EXPECT_EQ(b0->a1, "b0");
  EXPECT_EQ(b0->a2, -1);
  (*b1).a1 = "b1.1";
  b1->a2 = 1;
  EXPECT_EQ(b1->a1, "b1.1");
  EXPECT_EQ(b1->a2, 1);

  static_assert((std::is_same_v<const Blob&, decltype(*b2)>),
                "b2_is_not_const");
  static_assert((std::is_same_v<Blob&, decltype(*b1)>), "b1_is_not_Blob");

  // Check the element_type typedef.
  static_assert((std::is_same_v<LazyStaticPtr<Blob>::element_type, Blob>),
                "element_type_wrong");
  static_assert(
      (std::is_same_v<LazyStaticPtr<const Blob>::element_type, const Blob>),
      "const_element_type_wrong");

  // These won't compile: *b2 is const as we've just checked:
  // (*b2).a1 = "b2.1";
  // b2->a2 = 20;
  EXPECT_EQ((*b2).a1, "b2");
  EXPECT_EQ((*b2).a2, 2);

  EXPECT_EQ(ctor_count, ctors + 3);
}

// A more realistic example, equivalent to a static map.
typedef std::map<int, int> Map;
typedef Map::value_type MapValue;
constexpr MapValue map_array[] = {{1, 2}, {2, 4}};
static LazyStaticPtr<Map, const MapValue*, const MapValue*> lazy_map = {
    std::begin(map_array), std::end(map_array)};
constexpr static LazyStaticPtr<Map, const MapValue*, const MapValue*>
    const_lazy_map = {map_array, map_array + 2};

TEST(LazyStaticPtr, Map) {
  EXPECT_EQ(4, (*lazy_map)[2]);
  EXPECT_EQ(4, const_lazy_map->at(2));
}

// default constructor for std::array.
static LazyStaticPtr<std::array<std::string, 3>> lazy_array;

TEST(LazyStaticPtr, Array) {
  EXPECT_TRUE((*lazy_array)[0].empty());
  (*lazy_array)[0] = "greebo";
  EXPECT_EQ("greebo", (*lazy_array)[0]);
}

// non-default constructor for std::array.
constexpr static LazyStaticPtr<std::array<absl::string_view, 3>,
                               std::array<absl::string_view, 3>>
    lazy_array_plusplus = {std::array<absl::string_view, 3>{"a", "b", "c"}};

TEST(LazyStaticPtr, ArrayWithInit) {
  EXPECT_EQ("a", (*lazy_array_plusplus)[0]);
  EXPECT_EQ("b", (*lazy_array_plusplus)[1]);
  EXPECT_EQ("c", (*lazy_array_plusplus)[2]);
}

// REQUIRES: pt is some smart pointer not holding null.
template <typename PT>
void DumpFunction(const PT& pt) {
  LOG(INFO) << "pt @" << &*pt << ": " << *pt;
}

TEST(LazyStaticPtr, DumpFunction) {
  std::shared_ptr<std::string> lp(new std::string("linked"));
  DumpFunction(lp);
  std::shared_ptr<std::string> shp(new std::string("shared"));
  DumpFunction(shp);
  std::unique_ptr<std::string> scp = std::make_unique<std::string>("scoped");
  DumpFunction(scp);
  static LazyStaticPtr<std::string, const char*> lsp = {"lazy"};
  DumpFunction(lsp);
}

TEST(LazyStaticPtr, DereferenceConst) {
  static const LazyStaticPtr<Blob, const char*> b = {"b"};
  EXPECT_EQ("b", b.get()->a1);
  EXPECT_EQ("b", (*b).a1);
  EXPECT_EQ("b", b->a1);
}

// This won't compile if you try to use it:
// static LazyStaticPtr<Blob[30]> ba;

static LazyStaticPtr<Blob, const char*, int, double> pb1 = {"pb1", 1, 1.5};
// This won't compile either; too many initializers.
// LazyStaticPtr<Blob, const char*, int, double> NC = { "pb1", 1, 1.5, 0 };

static LazyStaticPtr<const Blob, const char*> pb2 = {"pb2"};

class TesterThread : public Thread {
 public:
  void Run() override {
    for (int i = 0; i < absl::GetFlag(FLAGS_iters); i++) {
      sched_yield();
      EXPECT_EQ(pb1->a1, "pb1");
      sched_yield();
      EXPECT_EQ((*pb1).a2, 1);
      EXPECT_GT(pb1->a3, 1.0);
      sched_yield();
      EXPECT_EQ(pb2->a1, "pb2");
      EXPECT_EQ((*pb2).a2, 0);
      sched_yield();
      EXPECT_EQ(pb2->a3, 2.5);
      sched_yield();
    }
  }
};

TEST(LazyStaticPtr, Threads) {
  const int ctors = ctor_count;
  std::vector<std::shared_ptr<TesterThread>> threads;
  for (int i = 0; i < absl::GetFlag(FLAGS_numthreads); ++i) {
    TesterThread* t = new TesterThread;
    t->SetJoinable(true);
    threads.push_back(absl::WrapUnique(t));
  }
  for (int i = 0; i < absl::GetFlag(FLAGS_numthreads); ++i) threads[i]->Start();
  for (int i = 0; i < absl::GetFlag(FLAGS_numthreads); ++i) threads[i]->Join();
  EXPECT_EQ(ctor_count, ctors + 2);
}

// WARNING: This is NOT an intended use case.  It's only here to verify that
// LazyStaticPtr can successfully be used in code that runs very early -
// specifically, before main().  g_file_one_name / g_file_three_name are OK with
// our style guidelines, but the following 4 lines are decidedly NOT OK with
// our style guidelines because they assign the result of a function call to a
// global.

static int g_file_oneLength = g_file_one_name->size();
static int g_file_threeLength = g_file_three_name->size();
static std::string g_file_one_copy = *g_file_one_name;      // NOLINT
static std::string g_file_three_copy = *g_file_three_name;  // NOLINT

// These two lines illustrate the problem we're trying to work around.  They
// reference complex globals in other files and whether they work or not is
// entirely determined by link order.
// static string g_string_one_copy = g_file_one_string;
// static string g_string_three_copy = g_file_three_string;

TEST(LazyStaticPtr, LinkOrderIrrelevancy) {
  EXPECT_EQ(g_file_oneLength, 8);
  EXPECT_EQ(g_file_threeLength, 10);
  EXPECT_STREQ(g_file_one_copy.c_str(), "File One") << g_file_one_copy;
  EXPECT_STREQ(g_file_three_copy.c_str(), "File Three") << g_file_three_copy;

  // Again, purely for documentation's sake, these two lines would not work if
  // you uncommented them and the declarations they depend upon.
  // EXPECT_STREQ(g_string_one_copy.c_str(), "File One");
  // EXPECT_STREQ(g_string_three_copy.c_str(), "File Three");
}

class ComplexType {
 public:
  constexpr explicit ComplexType(int seconds)
      : duration_{absl::Seconds(seconds)} {}
  constexpr absl::Duration duration() const { return duration_; }

 private:
  absl::Duration duration_;
};

constexpr ComplexType ConstexprFunction(int seconds) {
  return ComplexType(seconds + seconds);
}

TEST(LazyStaticPtr, ClassSize) {
  constexpr auto size_of_two_ptrs = 2 * sizeof(void*);
  EXPECT_LE(sizeof(LazyStaticPtr<int>), size_of_two_ptrs);
  EXPECT_LE(sizeof(LazyStaticPtr<std::string>), size_of_two_ptrs);
  EXPECT_LE(sizeof(LazyStaticPtr<char, char>), size_of_two_ptrs);
}

TEST(LazyStaticPtr, MakeLazyPtr) {
  struct PodType {
    int x;
    double y;
  };

  class VeryComplexType {
   public:
    constexpr VeryComplexType(const ComplexType& c, const PodType& p,
                              absl::string_view s, bool b)
        : complex_type_{c}, pod_type_{p}, string_view_{s}, flag_{b} {}

    ComplexType complex_type() const { return complex_type_; }
    PodType pod_type() const { return pod_type_; }
    constexpr absl::string_view string_view() const { return string_view_; }
    bool flag() const { return flag_; }

   private:
    const ComplexType complex_type_;
    const PodType pod_type_;
    const absl::string_view string_view_;
    const bool flag_;
  };

  constexpr static auto kVeryComplexLazyPtr = gtl::MakeLazyPtr<VeryComplexType>(
      ConstexprFunction(21), PodType{239, 30.0}, absl::string_view("c++"),
      true);

  EXPECT_EQ(absl::Seconds(42), kVeryComplexLazyPtr->complex_type().duration());
  EXPECT_EQ(239, kVeryComplexLazyPtr->pod_type().x);
  EXPECT_EQ(30.0, kVeryComplexLazyPtr->pod_type().y);
  EXPECT_EQ("c++", kVeryComplexLazyPtr->string_view());
  EXPECT_EQ(true, kVeryComplexLazyPtr->flag());
}

static gtl::LazyStaticPtr<bool> bm_lazy;

static void BM_LazyStaticPtrGet(benchmark::State& state) {
  bool r = false;
  for (auto s : state) {
    r = *(bm_lazy);
  }
  CHECK(!r);
}
BENCHMARK(BM_LazyStaticPtrGet);

}  // namespace
}  // namespace gtl
