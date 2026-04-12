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

#include "gloop/util/registration/registerer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/casts.h"
#include "absl/functional/bind_front.h"
#include "absl/hash/hash.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "gloop/gloop_test.h"
#include "gloop/thread/executor.h"

namespace {

using testing::ElementsAreArray;
using testing::IsSupersetOf;
using testing::UnorderedElementsAreArray;
using util_registration::internal::Registry;

// Ensure that Registerer has specefied names registered,
template <class Registerer>
void CheckNames(std::vector<std::string> expected_names) {
  std::sort(expected_names.begin(), expected_names.end());

  std::vector<std::string> names;
  names.push_back("FOO");  // Add some garbage that should be removed
  Registerer::GetNames(&names);

  EXPECT_THAT(names, UnorderedElementsAreArray(expected_names));

  // Also check the alternate method.
  names = Registerer::RegisteredNames();
  EXPECT_THAT(names, ElementsAreArray(expected_names));
}

std::vector<std::string> UnusedRegisteredNames() {
  std::vector<std::string> unused_names;
  for (const Registry* registry : Registry::GetAllRegistries()) {
    const std::vector<std::string> names = registry->GetNames();
    for (const auto& name : names) {
      const auto& object = registry->Lookup(name);
      if (!object.used) {
        unused_names.push_back(name);
      }
    }
  }
  return unused_names;
}

template <class Registerer>
void CheckUnusedNames(const std::vector<std::string>& expected_names) {
  std::vector<std::string> names = UnusedRegisteredNames();
  EXPECT_THAT(names, IsSupersetOf(expected_names));
}

// Ensure that AliasRegisterer has exactly two aliases registered,
// alias1 and alias2.
template <class AliasRegisterer>
void CheckAliases(std::string alias1, std::string alias2, std::string alias3) {
  // Get alias1 and alias2 in alphabetical order.
  std::vector<std::string> input_aliases;
  input_aliases.push_back(alias1);
  input_aliases.push_back(alias2);
  input_aliases.push_back(alias3);
  std::sort(input_aliases.begin(), input_aliases.end());

  std::vector<std::string> aliases;
  aliases.push_back("FOO");  // Add some garbage that should be removed
  AliasRegisterer::GetAliases(&aliases);

  ASSERT_EQ(3, aliases.size());
  EXPECT_EQ(input_aliases[0], aliases[0]);
  EXPECT_EQ(input_aliases[1], aliases[1]);
  EXPECT_EQ(input_aliases[2], aliases[2]);
}

///////////////////////////////////
// Aliases
///////////////////////////////////

const std::string kAliasesBase = "AliasesBase";
const std::string kAliasesTest1 = "AliasesTest1";
const std::string kAliasesTest2 = "AliasesTest2";
const std::string kAliasesBaseAlias = "AliasesBaseAlias";
const std::string kAliasesTest1Alias = "AliasesTest1Alias";
const std::string kAliasesTest2Alias1 = "AliasesTest2Alias1";
const std::string kAliasesTest2Alias2 = "AliasesTest2Alias2";

class AliasesBase {
 public:
  AliasesBase() {}
  virtual ~AliasesBase() {}
  virtual const std::string& name() const { return kAliasesBase; }
};

DEFINE_REGISTERER(AliasesBase);
DEFINE_ALIAS_REGISTERER(AliasesBase);
#define REGISTER_WITH_ALIAS(name, alias) \
  REGISTER_ENTITY(name, AliasesBase);    \
  REGISTER_ALIAS(name, alias, AliasesBase)

class AliasesTest1 : public AliasesBase {
 public:
  AliasesTest1() : AliasesBase() {}
  const std::string& name() const override { return kAliasesTest1; }
};
REGISTER_WITH_ALIAS(AliasesTest1, kAliasesTest1Alias);

class AliasesTest2 : public AliasesBase {
 public:
  AliasesTest2() : AliasesBase() {}
  const std::string& name() const override { return kAliasesTest2; }
};
REGISTER_WITH_ALIAS(AliasesTest2, kAliasesTest2Alias1);
REGISTER_ALIAS(AliasesTest2, kAliasesTest2Alias2, AliasesBase);
// Ensure that duplicate alias registrations are ignored.
REGISTER_ALIAS(AliasesTest2, kAliasesTest2Alias2, AliasesBase);

TEST(Aliases, IsValidAlias) {
  // Check that the classes were registered appropriately
  ASSERT_TRUE(AliasesBaseAliasRegisterer::IsValidAlias(kAliasesTest1Alias));
  ASSERT_TRUE(AliasesBaseAliasRegisterer::IsValidAlias(kAliasesTest2Alias1));
  ASSERT_TRUE(AliasesBaseAliasRegisterer::IsValidAlias(kAliasesTest2Alias2));
  ASSERT_FALSE(AliasesBaseAliasRegisterer::IsValidAlias(kAliasesBaseAlias));
}

TEST(Aliases, GetNameByAlias) {
  // Check that we can get names using aliases, and then create instances using
  // the Creator interface
  ASSERT_TRUE(AliasesBaseAliasRegisterer::IsValidAlias(kAliasesTest1Alias));
  std::string name1 =
      AliasesBaseAliasRegisterer::GetNameByAliasOrDie(kAliasesTest1Alias);
  ASSERT_EQ(kAliasesTest1, name1);
  AliasesBaseCreator* creator1 = AliasesBaseRegisterer::GetByNameOrDie(name1);
  ASSERT_TRUE(creator1);
  AliasesBase* test1 = (*creator1)();
  ASSERT_TRUE(AliasesBaseAliasRegisterer::IsValidAlias(kAliasesTest2Alias1));
  std::string name2 =
      AliasesBaseAliasRegisterer::GetNameByAliasOrDie(kAliasesTest2Alias1);
  ASSERT_EQ(kAliasesTest2, name2);
  ASSERT_TRUE(AliasesBaseAliasRegisterer::IsValidAlias(kAliasesTest2Alias2));
  name2 = AliasesBaseAliasRegisterer::GetNameByAliasOrDie(kAliasesTest2Alias2);
  ASSERT_EQ(kAliasesTest2, name2);
  AliasesBaseCreator* creator2 = AliasesBaseRegisterer::GetByNameOrDie(name2);
  ASSERT_TRUE(creator2);
  AliasesBase* test2 = (*creator2)();

  // Check that the classes are what we think they are, and clean up
  ASSERT_EQ(kAliasesTest1, test1->name());
  ASSERT_EQ(kAliasesTest2, test2->name());
  delete test1;
  delete test2;
}

TEST(Aliases, GetAliases) {
  // Check the list of Aliases.
  CheckAliases<AliasesBaseAliasRegisterer>(
      kAliasesTest1Alias, kAliasesTest2Alias1, kAliasesTest2Alias2);
}

///////////////////////////////////
// No arguments
///////////////////////////////////

const char kNoArgumentsBase[] = "NoArgumentsBase";
const char kNoArgumentsTest1[] = "NoArgumentsTest1";
const char kNoArgumentsTest2[] = "NoArgumentsTest2";
const char kUnusedNoArgumentsTest[] = "UnusedNoArgumentsTest";

class NoArgumentsBase {
 public:
  NoArgumentsBase() {}
  virtual ~NoArgumentsBase() {}
  virtual std::string name() const { return kNoArgumentsBase; }
};

DEFINE_REGISTERER(NoArgumentsBase);
#define REGISTER_NO_ARGS(name) REGISTER_ENTITY(name, NoArgumentsBase);
#define REGISTER_FACTORY_NO_ARGS(name, FactoryName) \
  REGISTER_FACTORY_ENTITY(name, NoArgumentsBase, FactoryName);

class NoArgumentsTest1 : public NoArgumentsBase {
 public:
  NoArgumentsTest1() : NoArgumentsBase() {}
  std::string name() const override { return kNoArgumentsTest1; }
};
REGISTER_NO_ARGS(NoArgumentsTest1);

class NoArgumentsTest2 : public NoArgumentsBase {
 public:
  static NoArgumentsBase* Create() { return new NoArgumentsTest2(); }
  std::string name() const override { return kNoArgumentsTest2; }

 private:
  NoArgumentsTest2() : NoArgumentsBase() {}
};
REGISTER_FACTORY_NO_ARGS(NoArgumentsTest2, NoArgumentsTest2::Create);

class UnusedNoArgumentsTest : public NoArgumentsBase {
 public:
  UnusedNoArgumentsTest() : NoArgumentsBase() {}
  std::string name() const override { return kUnusedNoArgumentsTest; }
};
REGISTER_NO_ARGS(UnusedNoArgumentsTest);

TEST(NoArguments, IsValid) {
  // Check that the classes were registered appropriately
  ASSERT_TRUE(NoArgumentsBaseRegisterer::IsValidName(kNoArgumentsTest1));
  ASSERT_TRUE(NoArgumentsBaseRegisterer::IsValidName(kNoArgumentsTest2));
  ASSERT_FALSE(NoArgumentsBaseRegisterer::IsValidName(kNoArgumentsBase));
}

TEST(NoArguments, CreateByName) {
  // Check that we can create instances of the relevant classes
  NoArgumentsBase* test1 =
      NoArgumentsBaseRegisterer::CreateByNameOrDie(kNoArgumentsTest1);
  ASSERT_TRUE(test1);
  NoArgumentsBase* test2 =
      NoArgumentsBaseRegisterer::CreateByNameOrDie(kNoArgumentsTest2);
  ASSERT_TRUE(test2);

  // Check that the classes are what we think they are, and clean up
  ASSERT_EQ(kNoArgumentsTest1, test1->name());
  ASSERT_EQ(kNoArgumentsTest2, test2->name());

  delete test1;
  delete test2;
}

TEST(NoArguments, GetByName) {
  // Check that we can create instances using the Creator interface
  NoArgumentsBaseCreator* creator1 =
      NoArgumentsBaseRegisterer::GetByNameOrDie(kNoArgumentsTest1);
  ASSERT_TRUE(creator1);
  NoArgumentsBase* test1 = (*creator1)();
  NoArgumentsBaseCreator* creator2 =
      NoArgumentsBaseRegisterer::GetByNameOrDie(kNoArgumentsTest2);
  ASSERT_TRUE(creator2);
  NoArgumentsBase* test2 = (*creator2)();

  // Check that the classes are what we think they are, and clean up
  ASSERT_EQ(kNoArgumentsTest1, test1->name());
  ASSERT_EQ(kNoArgumentsTest2, test2->name());
  delete test1;
  delete test2;
}

TEST(NoArguments, GetNames) {
  // Check the list of names.
  CheckNames<NoArgumentsBaseRegisterer>(
      {kNoArgumentsTest1, kNoArgumentsTest2, kUnusedNoArgumentsTest});
  CheckUnusedNames<NoArgumentsBaseRegisterer>({kUnusedNoArgumentsTest});
}

TEST(NoArguments, GetFilenameByName) {
  // Check that we get the filename.
  std::string filename =
      NoArgumentsBaseRegisterer::GetFilenameByNameOrDie(kNoArgumentsTest1);
  ASSERT_EQ(__FILE__, filename);
}

///////////////////////////////////
// One argument
///////////////////////////////////

const char kOneArgumentBase[] = "OneArgumentBase";
const char kOneArgumentTest1[] = "OneArgumentTest1";
const char kOneArgumentTest2[] = "OneArgumentTest2";

class OneArgumentBase {
 public:
  OneArgumentBase(int32_t t1) : t1_(t1) {}
  virtual ~OneArgumentBase() {}
  virtual std::string name() const { return kOneArgumentBase; }

  int32_t t1_;
};

DEFINE_REGISTERER(OneArgumentBase, int32_t);
#define REGISTER_ONE_ARG(name) REGISTER_ENTITY(name, OneArgumentBase);
#define REGISTER_FACTORY_ONE_ARG(name, FactoryName) \
  REGISTER_FACTORY_ENTITY(name, OneArgumentBase, FactoryName);

class OneArgumentTest1 : public OneArgumentBase {
 public:
  OneArgumentTest1(int32_t t1) : OneArgumentBase(t1) {}
  std::string name() const override { return kOneArgumentTest1; }
};
REGISTER_ONE_ARG(OneArgumentTest1);

class OneArgumentTest2 : public OneArgumentBase {
 public:
  static OneArgumentBase* Create(int32_t t1) {
    return new OneArgumentTest2(t1);
  }
  std::string name() const override { return kOneArgumentTest2; }

 private:
  OneArgumentTest2(int32_t t1) : OneArgumentBase(t1) {}
};
REGISTER_FACTORY_ONE_ARG(OneArgumentTest2, OneArgumentTest2::Create);

TEST(OneArgument, IsValid) {
  // Check that the classes were registered appropriately
  ASSERT_TRUE(OneArgumentBaseRegisterer::IsValidName(kOneArgumentTest1));
  ASSERT_TRUE(OneArgumentBaseRegisterer::IsValidName(kOneArgumentTest2));
  ASSERT_FALSE(OneArgumentBaseRegisterer::IsValidName(kOneArgumentBase));
}

TEST(OneArgument, CreateByName) {
  // Check that we can create instances of the relevant classes
  OneArgumentBase* test1 =
      OneArgumentBaseRegisterer::CreateByNameOrDie(kOneArgumentTest1, 1);
  ASSERT_TRUE(test1);
  OneArgumentBase* test2 =
      OneArgumentBaseRegisterer::CreateByNameOrDie(kOneArgumentTest2, 2);
  ASSERT_TRUE(test2);

  // Check that the classes are what we think they are, and clean up
  ASSERT_EQ(kOneArgumentTest1, test1->name());
  ASSERT_EQ(1, test1->t1_);
  ASSERT_EQ(kOneArgumentTest2, test2->name());
  ASSERT_EQ(2, test2->t1_);
  delete test1;
  delete test2;
}

TEST(OneArgument, GetByName) {
  // Check that we can create instances using the Creator interface
  OneArgumentBaseCreator* creator1 =
      OneArgumentBaseRegisterer::GetByNameOrDie(kOneArgumentTest1);
  ASSERT_TRUE(creator1);
  OneArgumentBase* test1 = (*creator1)(1);
  OneArgumentBaseCreator* creator2 =
      OneArgumentBaseRegisterer::GetByNameOrDie(kOneArgumentTest2);
  ASSERT_TRUE(creator2);
  OneArgumentBase* test2 = (*creator2)(2);

  // Check that the classes are what we think they are, and clean up
  ASSERT_EQ(kOneArgumentTest1, test1->name());
  ASSERT_EQ(1, test1->t1_);
  ASSERT_EQ(kOneArgumentTest2, test2->name());
  ASSERT_EQ(2, test2->t1_);
  delete test1;
  delete test2;
}

TEST(OneArgument, GetNames) {
  // Check the list of names.
  CheckNames<OneArgumentBaseRegisterer>({kOneArgumentTest1, kOneArgumentTest2});
  CheckUnusedNames<OneArgumentBaseRegisterer>({});
}

TEST(OneArgument, GetFilenameByName) {
  // Check that we get the filename.
  std::string filename =
      OneArgumentBaseRegisterer::GetFilenameByNameOrDie(kOneArgumentTest1);
  ASSERT_EQ(__FILE__, filename);
}

///////////////////////////////////
// Two arguments
///////////////////////////////////

const std::string kTwoArgumentsBase = "TwoArgumentsBase";
const std::string kTwoArgumentsTest1 = "TwoArgumentsTest1";
const std::string kTwoArgumentsTest2 = "TwoArgumentsTest2";

class TwoArgumentsBase {
 public:
  TwoArgumentsBase(int32_t t1, const std::string& t2) : t1_(t1), t2_(t2) {}
  virtual ~TwoArgumentsBase() {}
  virtual const std::string& name() const { return kTwoArgumentsBase; }

  int32_t t1_;
  std::string t2_;
};

DEFINE_REGISTERER(TwoArgumentsBase, int32_t, const std::string&);
#define REGISTER_TWO_ARGS(name) REGISTER_ENTITY(name, TwoArgumentsBase);
#define REGISTER_FACTORY_TWO_ARGS(name, FactoryName) \
  REGISTER_FACTORY_ENTITY(name, TwoArgumentsBase, FactoryName);

class TwoArgumentsTest1 : public TwoArgumentsBase {
 public:
  TwoArgumentsTest1(int32_t t1, const std::string& t2)
      : TwoArgumentsBase(t1, t2) {}
  const std::string& name() const override { return kTwoArgumentsTest1; }
};
REGISTER_TWO_ARGS(TwoArgumentsTest1);

class TwoArgumentsTest2 : public TwoArgumentsBase {
 public:
  static TwoArgumentsBase* Create(int32_t t1, const std::string& t2) {
    return new TwoArgumentsTest2(t1, t2);
  }
  const std::string& name() const override { return kTwoArgumentsTest2; }

 private:
  TwoArgumentsTest2(int32_t t1, const std::string& t2)
      : TwoArgumentsBase(t1, t2) {}
};
REGISTER_FACTORY_TWO_ARGS(TwoArgumentsTest2, TwoArgumentsTest2::Create);

TEST(TwoArguments, IsValid) {
  // Check that the classes were registered appropriately
  ASSERT_TRUE(TwoArgumentsBaseRegisterer::IsValidName(kTwoArgumentsTest1));
  ASSERT_TRUE(TwoArgumentsBaseRegisterer::IsValidName(kTwoArgumentsTest2));
  ASSERT_FALSE(TwoArgumentsBaseRegisterer::IsValidName(kTwoArgumentsBase));
}

TEST(TwoArguments, CreateByName) {
  // Check that we can create instances of the relevant classes
  TwoArgumentsBase* test1 =
      TwoArgumentsBaseRegisterer::CreateByNameOrDie(kTwoArgumentsTest1, 1, "1");
  ASSERT_TRUE(test1);
  TwoArgumentsBase* test2 =
      TwoArgumentsBaseRegisterer::CreateByNameOrDie(kTwoArgumentsTest2, 2, "2");
  ASSERT_TRUE(test2);

  // Check that the classes are what we think they are, and clean up
  ASSERT_EQ(kTwoArgumentsTest1, test1->name());
  ASSERT_EQ(1, test1->t1_);
  ASSERT_EQ("1", test1->t2_);
  ASSERT_EQ(kTwoArgumentsTest2, test2->name());
  ASSERT_EQ(2, test2->t1_);
  ASSERT_EQ("2", test2->t2_);
  delete test1;
  delete test2;
}

TEST(TwoArguments, GetByName) {
  // Check that we can create instances using the Creator interface
  TwoArgumentsBaseCreator* creator1 =
      TwoArgumentsBaseRegisterer::GetByNameOrDie(kTwoArgumentsTest1);
  ASSERT_TRUE(creator1);
  TwoArgumentsBase* test1 = (*creator1)(1, "1");
  TwoArgumentsBaseCreator* creator2 =
      TwoArgumentsBaseRegisterer::GetByNameOrDie(kTwoArgumentsTest2);
  ASSERT_TRUE(creator2);
  TwoArgumentsBase* test2 = (*creator2)(2, "2");

  // Check that the classes are what we think they are, and clean up
  ASSERT_EQ(kTwoArgumentsTest1, test1->name());
  ASSERT_EQ(1, test1->t1_);
  ASSERT_EQ("1", test1->t2_);
  ASSERT_EQ(kTwoArgumentsTest2, test2->name());
  ASSERT_EQ(2, test2->t1_);
  ASSERT_EQ("2", test2->t2_);
  delete test1;
  delete test2;
}

TEST(TwoArguments, GetNames) {
  // Check the list of names.
  CheckNames<TwoArgumentsBaseRegisterer>(
      {kTwoArgumentsTest1, kTwoArgumentsTest2});
}

TEST(TwoArguments, GetFilenameByName) {
  // Check that we get the filename.
  std::string filename =
      TwoArgumentsBaseRegisterer::GetFilenameByNameOrDie(kTwoArgumentsTest1);
  ASSERT_EQ(__FILE__, filename);
}

///////////////////////////////////
// Three arguments
///////////////////////////////////

const std::string kThreeArgumentsBase = "ThreeArgumentsBase";
const std::string kThreeArgumentsTest1 = "ThreeArgumentsTest1";
const std::string kThreeArgumentsTest2 = "ThreeArgumentsTest2";

class ThreeArgumentsBase {
 public:
  ThreeArgumentsBase(int32_t t1, const std::string& t2, void* t3)
      : t1_(t1), t2_(t2), t3_(t3) {}
  virtual ~ThreeArgumentsBase() {}
  virtual const std::string& name() const { return kThreeArgumentsBase; }

  int32_t t1_;
  std::string t2_;
  void* t3_;
};

DEFINE_REGISTERER(ThreeArgumentsBase, int32_t, const std::string&, void*);
#define REGISTER_THREE_ARGS(name) REGISTER_ENTITY(name, ThreeArgumentsBase);
#define REGISTER_FACTORY_THREE_ARGS(name, FactoryName) \
  REGISTER_FACTORY_ENTITY(name, ThreeArgumentsBase, FactoryName);

class ThreeArgumentsTest1 : public ThreeArgumentsBase {
 public:
  ThreeArgumentsTest1(int32_t t1, const std::string& t2, void* t3)
      : ThreeArgumentsBase(t1, t2, t3) {}
  const std::string& name() const override { return kThreeArgumentsTest1; }
};
REGISTER_THREE_ARGS(ThreeArgumentsTest1);

class ThreeArgumentsTest2 : public ThreeArgumentsBase {
 public:
  static ThreeArgumentsBase* Create(int32_t t1, const std::string& t2,
                                    void* t3) {
    return new ThreeArgumentsTest2(t1, t2, t3);
  }
  const std::string& name() const override { return kThreeArgumentsTest2; }

 private:
  ThreeArgumentsTest2(int32_t t1, const std::string& t2, void* t3)
      : ThreeArgumentsBase(t1, t2, t3) {}
};
REGISTER_FACTORY_THREE_ARGS(ThreeArgumentsTest2, ThreeArgumentsTest2::Create);

TEST(ThreeArguments, IsValid) {
  // Check that the classes were registered appropriately
  ASSERT_TRUE(ThreeArgumentsBaseRegisterer::IsValidName(kThreeArgumentsTest1));
  ASSERT_TRUE(ThreeArgumentsBaseRegisterer::IsValidName(kThreeArgumentsTest2));
  ASSERT_FALSE(ThreeArgumentsBaseRegisterer::IsValidName(kThreeArgumentsBase));
}

TEST(ThreeArguments, CreateByName) {
  // Check that we can create instances of the relevant classes
  ThreeArgumentsBase* test1 = ThreeArgumentsBaseRegisterer::CreateByNameOrDie(
      kThreeArgumentsTest1, 1, "1", this);
  ASSERT_TRUE(test1);
  ThreeArgumentsBase* test2 = ThreeArgumentsBaseRegisterer::CreateByNameOrDie(
      kThreeArgumentsTest2, 2, "2", nullptr);
  ASSERT_TRUE(test2);

  // Check that the classes are what we think they are, and clean up
  ASSERT_EQ(kThreeArgumentsTest1, test1->name());
  ASSERT_EQ(1, test1->t1_);
  ASSERT_EQ("1", test1->t2_);
  ASSERT_EQ(reinterpret_cast<void*>(this), test1->t3_);
  ASSERT_EQ(kThreeArgumentsTest2, test2->name());
  ASSERT_EQ(2, test2->t1_);
  ASSERT_EQ("2", test2->t2_);
  ASSERT_EQ(nullptr, test2->t3_);
  delete test1;
  delete test2;
}

TEST(ThreeArguments, GetByName) {
  // Check that we can create instances using the Creator interface
  ThreeArgumentsBaseCreator* creator1 =
      ThreeArgumentsBaseRegisterer::GetByNameOrDie(kThreeArgumentsTest1);
  ASSERT_TRUE(creator1);
  ThreeArgumentsBase* test1 = (*creator1)(1, "1", this);
  ThreeArgumentsBaseCreator* creator2 =
      ThreeArgumentsBaseRegisterer::GetByNameOrDie(kThreeArgumentsTest2);
  ASSERT_TRUE(creator2);
  ThreeArgumentsBase* test2 = (*creator2)(2, "2", nullptr);

  // Check that the classes are what we think they are, and clean up
  ASSERT_EQ(kThreeArgumentsTest1, test1->name());
  ASSERT_EQ(1, test1->t1_);
  ASSERT_EQ("1", test1->t2_);
  ASSERT_EQ(reinterpret_cast<void*>(this), test1->t3_);
  ASSERT_EQ(kThreeArgumentsTest2, test2->name());
  ASSERT_EQ(2, test2->t1_);
  ASSERT_EQ("2", test2->t2_);
  ASSERT_EQ(nullptr, test2->t3_);
  delete test1;
  delete test2;
}

TEST(ThreeArguments, GetNames) {
  // Check the list of names.
  CheckNames<ThreeArgumentsBaseRegisterer>(
      {kThreeArgumentsTest1, kThreeArgumentsTest2});
}

TEST(ThreeArguments, GetFilenameByName) {
  // Check that we get the filename.
  std::string filename = ThreeArgumentsBaseRegisterer::GetFilenameByNameOrDie(
      kThreeArgumentsTest1);
  ASSERT_EQ(__FILE__, filename);
}

///////////////////////////////////
// Four arguments
///////////////////////////////////

const std::string kFourArgumentsBase = "FourArgumentsBase";
const std::string kFourArgumentsTest1 = "FourArgumentsTest1";
const std::string kFourArgumentsTest2 = "FourArgumentsTest2";

class FourArgumentsBase {
 public:
  FourArgumentsBase(int32_t t1, const std::string& t2, void* t3, void* t4)
      : t1_(t1), t2_(t2), t3_(t3), t4_(t4) {}
  virtual ~FourArgumentsBase() {}
  virtual const std::string& name() const { return kFourArgumentsBase; }

  int32_t t1_;
  std::string t2_;
  void* t3_;
  void* t4_;
};

DEFINE_REGISTERER(FourArgumentsBase, int32_t, const std::string&, void*, void*);
#define REGISTER_FOUR_ARGS(name) REGISTER_ENTITY(name, FourArgumentsBase);
#define REGISTER_FACTORY_FOUR_ARGS(name, FactoryName) \
  REGISTER_FACTORY_ENTITY(name, FourArgumentsBase, FactoryName);

class FourArgumentsTest1 : public FourArgumentsBase {
 public:
  FourArgumentsTest1(int32_t t1, const std::string& t2, void* t3, void* t4)
      : FourArgumentsBase(t1, t2, t3, t4) {}
  const std::string& name() const override { return kFourArgumentsTest1; }
};
REGISTER_FOUR_ARGS(FourArgumentsTest1);

class FourArgumentsTest2 : public FourArgumentsBase {
 public:
  static FourArgumentsBase* Create(int32_t t1, const std::string& t2, void* t3,
                                   void* t4) {
    return new FourArgumentsTest2(t1, t2, t3, t4);
  }
  const std::string& name() const override { return kFourArgumentsTest2; }

 private:
  FourArgumentsTest2(int32_t t1, const std::string& t2, void* t3, void* t4)
      : FourArgumentsBase(t1, t2, t3, t4) {}
};
REGISTER_FACTORY_FOUR_ARGS(FourArgumentsTest2, FourArgumentsTest2::Create);

TEST(FourArguments, IsValid) {
  // Check that the classes were registered appropriately
  ASSERT_TRUE(FourArgumentsBaseRegisterer::IsValidName(kFourArgumentsTest1));
  ASSERT_TRUE(FourArgumentsBaseRegisterer::IsValidName(kFourArgumentsTest2));
  ASSERT_FALSE(FourArgumentsBaseRegisterer::IsValidName(kFourArgumentsBase));
}

TEST(FourArguments, CreateByName) {
  // Check that we can create instances of the relevant classes
  FourArgumentsBase* test1 = FourArgumentsBaseRegisterer::CreateByNameOrDie(
      kFourArgumentsTest1, 1, "1", this, nullptr);
  ASSERT_TRUE(test1);
  FourArgumentsBase* test2 = FourArgumentsBaseRegisterer::CreateByNameOrDie(
      kFourArgumentsTest2, 2, "2", nullptr, this);
  ASSERT_TRUE(test2);

  // Check that the classes are what we think they are, and clean up
  ASSERT_EQ(kFourArgumentsTest1, test1->name());
  ASSERT_EQ(1, test1->t1_);
  ASSERT_EQ("1", test1->t2_);
  ASSERT_EQ(reinterpret_cast<void*>(this), test1->t3_);
  ASSERT_EQ(nullptr, test1->t4_);
  ASSERT_EQ(kFourArgumentsTest2, test2->name());
  ASSERT_EQ(2, test2->t1_);
  ASSERT_EQ("2", test2->t2_);
  ASSERT_EQ(nullptr, test2->t3_);
  ASSERT_EQ(reinterpret_cast<void*>(this), test2->t4_);
  delete test1;
  delete test2;
}

TEST(FourArguments, GetByName) {
  // Check that we can create instances using the Creator interface
  FourArgumentsBaseCreator* creator1 =
      FourArgumentsBaseRegisterer::GetByNameOrDie(kFourArgumentsTest1);
  ASSERT_TRUE(creator1);
  FourArgumentsBase* test1 = (*creator1)(1, "1", this, nullptr);
  FourArgumentsBaseCreator* creator2 =
      FourArgumentsBaseRegisterer::GetByNameOrDie(kFourArgumentsTest2);
  ASSERT_TRUE(creator2);
  FourArgumentsBase* test2 = (*creator2)(2, "2", nullptr, this);

  // Check that the classes are what we think they are, and clean up
  ASSERT_EQ(kFourArgumentsTest1, test1->name());
  ASSERT_EQ(1, test1->t1_);
  ASSERT_EQ("1", test1->t2_);
  ASSERT_EQ(reinterpret_cast<void*>(this), test1->t3_);
  ASSERT_EQ(nullptr, test1->t4_);
  ASSERT_EQ(kFourArgumentsTest2, test2->name());
  ASSERT_EQ(2, test2->t1_);
  ASSERT_EQ("2", test2->t2_);
  ASSERT_EQ(nullptr, test2->t3_);
  ASSERT_EQ(reinterpret_cast<void*>(this), test2->t4_);
  delete test1;
  delete test2;
}

TEST(FourArguments, GetNames) {
  // Check the list of names.
  CheckNames<FourArgumentsBaseRegisterer>(
      {kFourArgumentsTest1, kFourArgumentsTest2});
}

TEST(FourArguments, GetFilenameByName) {
  // Check that we get the filename.
  std::string filename =
      FourArgumentsBaseRegisterer::GetFilenameByNameOrDie(kFourArgumentsTest1);
  ASSERT_EQ(__FILE__, filename);
}

///////////////////////////////////
// Singleton
///////////////////////////////////

const std::string kSingletonBase = "SingletonBase";
const std::string kSingletonTest1 = "SingletonTest1";
const std::string kSingletonTest2 = "SingletonTest2";

class SingletonBase {
 public:
  virtual const std::string& name() const { return kSingletonBase; }

 protected:
  SingletonBase() {}
  virtual ~SingletonBase() {}
};

DEFINE_REGISTERER(SingletonBase);
#define REGISTER_SINGLETON(name) REGISTER_SINGLETON_ENTITY(name, SingletonBase);

class SingletonTest1 : public SingletonBase {
 public:
  SingletonTest1() {}
  ~SingletonTest1() override {}

  const std::string& name() const override { return kSingletonTest1; }
};
REGISTER_SINGLETON(SingletonTest1);

class SingletonTest2 : public SingletonBase {
 public:
  SingletonTest2() {}
  ~SingletonTest2() override {}

  const std::string& name() const override { return kSingletonTest2; }
};
REGISTER_SINGLETON(SingletonTest2);

TEST(Singleton, IsValid) {
  // Check that the classes were registered appropriately
  ASSERT_TRUE(SingletonBaseRegisterer::IsValidName(kSingletonTest1));
  ASSERT_TRUE(SingletonBaseRegisterer::IsValidName(kSingletonTest2));
  ASSERT_FALSE(SingletonBaseRegisterer::IsValidName(kSingletonBase));
}

TEST(Singleton, CreateByName) {
  // Check that we can create instances of the relevant classes
  SingletonBase* test1 =
      SingletonBaseRegisterer::CreateByNameOrDie(kSingletonTest1);
  ASSERT_TRUE(test1);
  SingletonBase* another_test1 =
      SingletonBaseRegisterer::CreateByNameOrDie(kSingletonTest1);
  ASSERT_TRUE(another_test1);

  SingletonBase* test2 =
      SingletonBaseRegisterer::CreateByNameOrDie(kSingletonTest2);
  ASSERT_TRUE(test2);
  SingletonBase* another_test2 =
      SingletonBaseRegisterer::CreateByNameOrDie(kSingletonTest2);
  ASSERT_TRUE(another_test2);

  // Check that the classes are what we think they are, and clean up
  ASSERT_EQ(kSingletonTest1, test1->name());
  ASSERT_EQ(kSingletonTest2, test2->name());
  ASSERT_EQ(test1, another_test1);
  ASSERT_EQ(test2, another_test2);
}

TEST(Singleton, GetByName) {
  // Check that we can create instances using the Creator interface
  SingletonBaseCreator* creator1 =
      SingletonBaseRegisterer::GetByNameOrDie(kSingletonTest1);
  ASSERT_TRUE(creator1);
  SingletonBase* test1 = (*creator1)();
  SingletonBase* another_test1 = (*creator1)();

  SingletonBaseCreator* creator2 =
      SingletonBaseRegisterer::GetByNameOrDie(kSingletonTest2);
  ASSERT_TRUE(creator2);
  SingletonBase* test2 = (*creator2)();
  SingletonBase* another_test2 = (*creator2)();

  // Check that the classes are what we think they are, and clean up
  ASSERT_EQ(kSingletonTest1, test1->name());
  ASSERT_EQ(kSingletonTest2, test2->name());
  ASSERT_EQ(test1, another_test1);
  ASSERT_EQ(test2, another_test2);
}

TEST(Singleton, GetNames) {
  // Check the list of names.
  CheckNames<SingletonBaseRegisterer>({kSingletonTest1, kSingletonTest2});
}

TEST(Singleton, GetFilenameByName) {
  // Check that we get the filename.
  std::string filename =
      SingletonBaseRegisterer::GetFilenameByNameOrDie(kSingletonTest1);
  ASSERT_EQ(__FILE__, filename);
}

///////////////////////////////////
// Typedef registerers
///////////////////////////////////

// DEFINE_REGISTERER is usually used with abstract classes that exist only for
// that purpose, but it also works with typedefs. The real type can be a pointer
// to anything, even a primitive data type.

const char kPi[] = "Pi";
const char kE[] = "E";
const double kPiValue = 3.14;
const double kEValue = 2.71;

typedef const double SpecialConstant;
DEFINE_REGISTERER(SpecialConstant);
#define REGISTER_SPECIAL_CONSTANT(name, FactoryName) \
  REGISTER_FACTORY_ENTITY(name, SpecialConstant, FactoryName)

SpecialConstant* PiFactory() { return &kPiValue; }
REGISTER_SPECIAL_CONSTANT(Pi, PiFactory);

SpecialConstant* EFactory() { return &kEValue; }
REGISTER_SPECIAL_CONSTANT(E, EFactory);

TEST(Typedef, IsValid) {
  // Check that the classes were registered appropriately.
  ASSERT_TRUE(SpecialConstantRegisterer::IsValidName(kPi));
  ASSERT_TRUE(SpecialConstantRegisterer::IsValidName(kE));
  ASSERT_FALSE(SpecialConstantRegisterer::IsValidName(""));
}

TEST(Typedef, CreateByName) {
  ASSERT_EQ(&kPiValue, SpecialConstantRegisterer::CreateByNameOrDie(kPi));
  ASSERT_EQ(kPiValue, *SpecialConstantRegisterer::CreateByNameOrDie(kPi));
  ASSERT_EQ(&kEValue, SpecialConstantRegisterer::CreateByNameOrDie(kE));
  ASSERT_EQ(kEValue, *SpecialConstantRegisterer::CreateByNameOrDie(kE));
}

// =============================================================================
// To illustrate a use-case for dynamic registration objects, we
// provide the following simple example:
//
// We will be instantiating objects from registered classes that have
// an embedded dependency on this interface.
//
// We create two implementations of the interface, one is a
// heavyweight implementation, while the other is a lightweight mock
// which is useful for testing. The client instantiates the registered
// objects, but should require no knowledge of the specific dependency
// implementation that has been injected into the object.
class DependencyInterface {
 public:
  virtual ~DependencyInterface() {}
  virtual std::string const& Type() const = 0;
};

// Heavyweight implementation.
class LargeDependency : public DependencyInterface {
 public:
  std::string const& Type() const override { return type_; }

  static const std::string type_;
};
const std::string LargeDependency::type_ = "Large";

// Lightweight implementation.
class MockDependency : public DependencyInterface {
 public:
  std::string const& Type() const override { return type_; }

  static const std::string type_;
};
const std::string MockDependency::type_ = "Mock";

class DynamicRegistererTest : public testing::Test {
 protected:
  LargeDependency largedep_;
  MockDependency mockdep_;
};

///////////////////////////////////
// No arguments
///////////////////////////////////

class DepNoArgumentsBase {
 public:
  // NOTE: Even though the constructor takes the dependency as a
  // parameter, we still call this the "NoArgument" example since the
  // client code that instantiates the objects uses no arguments
  // (other than the classname), i.e., the dependency object doesn't
  // count since that is injected via the registration code.
  DepNoArgumentsBase(DependencyInterface* dep) : dep_(dep) {}
  // Returns a pointer to the injected dependency (for examination).
  DependencyInterface const* Dependency() { return dep_; }

 private:
  DependencyInterface* dep_;
};
DEFINE_REGISTERER(DepNoArgumentsBase);
#define NEW_NO_ARGS_REGISTRATION(name, cb) \
  NEW_ENTITY_REGISTRATION_CB(name, DepNoArgumentsBase, cb)

class DepNoArgumentsTest : public DepNoArgumentsBase {
 public:
  DepNoArgumentsTest(DependencyInterface* dep) : DepNoArgumentsBase(dep) {}

  // Factory method used to register with a concrete dependency.
  static DepNoArgumentsBase* Create(DependencyInterface* dep) {
    return new DepNoArgumentsTest(dep);
  }
};

static void DepNoArgumentsClientTest(absl::string_view classname,
                                     absl::string_view expected_dep_type) {
  // Client code instantiating the object needs no knowledge of the
  // specific dependency injected.
  std::unique_ptr<DepNoArgumentsBase> obj(
      DepNoArgumentsBaseRegisterer::CreateByNameOrDie(classname));
  ASSERT_TRUE(obj.get() != nullptr);
  // Check that the object has the expected dependency injected.
  EXPECT_EQ(expected_dep_type, obj->Dependency()->Type());

  // Now try the same thing, but via the factory object.
  DepNoArgumentsBaseCreator* creator =
      DepNoArgumentsBaseRegisterer::GetByNameOrDie(classname);
  ASSERT_TRUE(creator != nullptr);
  obj.reset((*creator)());
  ASSERT_TRUE(obj.get() != nullptr);
  // Check that the object has the expected dependency injected.
  EXPECT_EQ(expected_dep_type, obj->Dependency()->Type());
}

TEST_F(DynamicRegistererTest, DepNoArguments) {
  std::string const kClassName = "DepNoArgumentsTest";
  EXPECT_FALSE(DepNoArgumentsBaseRegisterer::IsValidName(kClassName));
  {
    // As long as the following object is in scope, we can instantiate
    // "DepNoArgumentsTest" objects. Additionally, the objects created
    // will have a heavyweight dependency injected.
    std::unique_ptr<DepNoArgumentsBaseRegisterer> registerer(
        NEW_NO_ARGS_REGISTRATION(
            kClassName,
            absl::bind_front(
                &DepNoArgumentsTest::Create,
                absl::implicit_cast<DependencyInterface*>(&largedep_))));

    DepNoArgumentsClientTest(kClassName, LargeDependency::type_);

    // Register with the same name, to check for correct creator
    // deletion behavior.
    {
      // Only one mapping can exist per name. The mapping is deleted
      // by the most recent registerer object to go out of scope.
      std::unique_ptr<DepNoArgumentsBaseRegisterer> registerer2(
          NEW_NO_ARGS_REGISTRATION(
              kClassName,
              absl::bind_front(
                  &DepNoArgumentsTest::Create,
                  absl::implicit_cast<DependencyInterface*>(&mockdep_))));
      // The new registration doesn't overwrite the previous one.
      DepNoArgumentsClientTest(kClassName, LargeDependency::type_);
    }
    // The mapping should be gone, even though "registerer" is still
    // in scope.
    EXPECT_FALSE(DepNoArgumentsBaseRegisterer::IsValidName(kClassName));
  }
  EXPECT_FALSE(DepNoArgumentsBaseRegisterer::IsValidName(kClassName));
  {
    // As long as the following object is in scope, we can instantiate
    // "DepNoArgumentsTest" objects. Additionally, the objects created
    // will have a lightweight mock dependency injected.
    std::unique_ptr<DepNoArgumentsBaseRegisterer> registerer(
        NEW_NO_ARGS_REGISTRATION(
            kClassName,
            absl::bind_front(
                &DepNoArgumentsTest::Create,
                absl::implicit_cast<DependencyInterface*>(&mockdep_))));

    DepNoArgumentsClientTest(kClassName, MockDependency::type_);
  }
  EXPECT_FALSE(DepNoArgumentsBaseRegisterer::IsValidName(kClassName));
}

///////////////////////////////////
// One argument
///////////////////////////////////

class DepOneArgumentBase {
 public:
  DepOneArgumentBase(DependencyInterface* dep, int32_t arg)
      : dep_(dep), arg_(arg) {}
  // Returns a pointer to the injected dependency (for examination).
  DependencyInterface const* Dependency() { return dep_; }
  int32_t Arg() const { return arg_; }

 private:
  DependencyInterface* dep_;
  int32_t arg_;
};
DEFINE_REGISTERER(DepOneArgumentBase, int32_t);
#define NEW_ONE_ARG_REGISTRATION(name, cb) \
  NEW_ENTITY_REGISTRATION_CB(name, DepOneArgumentBase, cb)

class DepOneArgumentTest : public DepOneArgumentBase {
 public:
  DepOneArgumentTest(DependencyInterface* dep, int32_t arg)
      : DepOneArgumentBase(dep, arg) {}

  // Factory method used to register with a concrete dependency.
  static DepOneArgumentBase* Create(DependencyInterface* dep, int32_t arg) {
    return new DepOneArgumentTest(dep, arg);
  }
};

static void DepOneArgumentClientTest(absl::string_view classname,
                                     absl::string_view expected_dep_type) {
  int32_t arg = 1;
  // Client code instantiating the object needs no knowledge of the
  // specific dependency injected.
  std::unique_ptr<DepOneArgumentBase> obj(
      DepOneArgumentBaseRegisterer::CreateByNameOrDie(classname, arg));
  ASSERT_TRUE(obj.get() != nullptr);
  // Check that the object has the expected dependency injected.
  EXPECT_EQ(expected_dep_type, obj->Dependency()->Type());
  // Check that the object was constructed with the correct args.
  EXPECT_EQ(arg, obj->Arg());

  // Now try the same thing, but via the factory object.
  DepOneArgumentBaseCreator* creator =
      DepOneArgumentBaseRegisterer::GetByNameOrDie(classname);
  ASSERT_TRUE(creator != nullptr);
  obj.reset((*creator)(arg));
  ASSERT_TRUE(obj.get() != nullptr);
  // Check that the object has the expected dependency injected.
  EXPECT_EQ(expected_dep_type, obj->Dependency()->Type());
  // Check that the object was constructed with the correct args.
  EXPECT_EQ(arg, obj->Arg());
}

TEST_F(DynamicRegistererTest, DepOneArgument) {
  std::string const kClassName = "DepOneArgumentTest";
  EXPECT_FALSE(DepOneArgumentBaseRegisterer::IsValidName(kClassName));
  {
    // As long as the following object is in scope, we can instantiate
    // "DepOneArgumentTest" objects. Additionally, the objects created
    // will have a heavyweight dependency injected.
    std::unique_ptr<DepOneArgumentBaseRegisterer> registerer(
        NEW_ONE_ARG_REGISTRATION(
            kClassName,
            absl::bind_front(
                &DepOneArgumentTest::Create,
                absl::implicit_cast<DependencyInterface*>(&largedep_))));

    DepOneArgumentClientTest(kClassName, LargeDependency::type_);
  }
  EXPECT_FALSE(DepOneArgumentBaseRegisterer::IsValidName(kClassName));
  {
    // As long as the following object is in scope, we can instantiate
    // "DepOneArgumentTest" objects. Additionally, the objects created
    // will have a lightweight mock dependency injected.
    std::unique_ptr<DepOneArgumentBaseRegisterer> registerer(
        NEW_ONE_ARG_REGISTRATION(
            kClassName,
            absl::bind_front(
                &DepOneArgumentTest::Create,
                absl::implicit_cast<DependencyInterface*>(&mockdep_))));

    DepOneArgumentClientTest(kClassName, MockDependency::type_);
  }
  EXPECT_FALSE(DepOneArgumentBaseRegisterer::IsValidName(kClassName));
}

///////////////////////////////////
// Two arguments
///////////////////////////////////

class DepTwoArgumentsBase {
 public:
  DepTwoArgumentsBase(DependencyInterface* dep, int32_t arg1,
                      std::string const& arg2)
      : dep_(dep), arg1_(arg1), arg2_(arg2) {}
  // Returns a pointer to the injected dependency (for examination).
  DependencyInterface const* Dependency() { return dep_; }
  int32_t Arg1() const { return arg1_; }
  std::string const& Arg2() const { return arg2_; }

 private:
  DependencyInterface* dep_;
  int32_t arg1_;
  std::string arg2_;
};
DEFINE_REGISTERER(DepTwoArgumentsBase, int32_t, std::string const&);
#define NEW_TWO_ARGS_REGISTRATION(name, cb) \
  NEW_ENTITY_REGISTRATION_CB(name, DepTwoArgumentsBase, cb)

class DepTwoArgumentsTest : public DepTwoArgumentsBase {
 public:
  DepTwoArgumentsTest(DependencyInterface* dep, int32_t arg1,
                      std::string const& arg2)
      : DepTwoArgumentsBase(dep, arg1, arg2) {}

  // Factory method used to register with a concrete dependency.
  static DepTwoArgumentsBase* Create(DependencyInterface* dep, int32_t arg1,
                                     std::string const& arg2) {
    return new DepTwoArgumentsTest(dep, arg1, arg2);
  }
};

static void DepTwoArgumentsClientTest(absl::string_view classname,
                                      absl::string_view expected_dep_type) {
  int32_t arg1 = 1;
  std::string arg2 = "1";
  // Client code instantiating the object needs no knowledge of the
  // specific dependency injected.
  std::unique_ptr<DepTwoArgumentsBase> obj(
      DepTwoArgumentsBaseRegisterer::CreateByNameOrDie(classname, arg1, arg2));
  ASSERT_TRUE(obj.get() != nullptr);
  // Check that the object has the expected dependency injected.
  EXPECT_EQ(expected_dep_type, obj->Dependency()->Type());
  // Check that the object was constructed with the correct args.
  EXPECT_EQ(arg1, obj->Arg1());
  EXPECT_EQ(arg2, obj->Arg2());

  // Now try the same thing, but via the factory object.
  DepTwoArgumentsBaseCreator* creator =
      DepTwoArgumentsBaseRegisterer::GetByNameOrDie(classname);
  ASSERT_TRUE(creator != nullptr);
  obj.reset((*creator)(arg1, arg2));
  ASSERT_TRUE(obj.get() != nullptr);
  // Check that the object has the expected dependency injected.
  EXPECT_EQ(expected_dep_type, obj->Dependency()->Type());
  // Check that the object was constructed with the correct args.
  EXPECT_EQ(arg1, obj->Arg1());
  EXPECT_EQ(arg2, obj->Arg2());
}

TEST_F(DynamicRegistererTest, DepTwoArguments) {
  std::string const kClassName = "DepTwoArgumentsTest";
  EXPECT_FALSE(DepTwoArgumentsBaseRegisterer::IsValidName(kClassName));
  {
    // As long as the following object is in scope, we can instantiate
    // "DepTwoArgumentsTest" objects. Additionally, the objects created
    // will have a heavyweight dependency injected.
    std::unique_ptr<DepTwoArgumentsBaseRegisterer> registerer(
        NEW_TWO_ARGS_REGISTRATION(
            kClassName,
            absl::bind_front(
                &DepTwoArgumentsTest::Create,
                absl::implicit_cast<DependencyInterface*>(&largedep_))));

    DepTwoArgumentsClientTest(kClassName, LargeDependency::type_);
  }
  EXPECT_FALSE(DepTwoArgumentsBaseRegisterer::IsValidName(kClassName));
  {
    // As long as the following object is in scope, we can instantiate
    // "DepTwoArgumentsTest" objects. Additionally, the objects created
    // will have a lightweight mock dependency injected.
    std::unique_ptr<DepTwoArgumentsBaseRegisterer> registerer(
        NEW_TWO_ARGS_REGISTRATION(
            kClassName,
            absl::bind_front(
                &DepTwoArgumentsTest::Create,
                absl::implicit_cast<DependencyInterface*>(&mockdep_))));

    DepTwoArgumentsClientTest(kClassName, MockDependency::type_);
  }
  EXPECT_FALSE(DepTwoArgumentsBaseRegisterer::IsValidName(kClassName));
}

///////////////////////////////////
// Three arguments
///////////////////////////////////

class DepThreeArgumentsBase {
 public:
  DepThreeArgumentsBase(DependencyInterface* dep, int32_t arg1,
                        std::string const& arg2, void* arg3)
      : dep_(dep), arg1_(arg1), arg2_(arg2), arg3_(arg3) {}
  // Returns a pointer to the injected dependency (for examination).
  DependencyInterface const* Dependency() { return dep_; }
  int32_t Arg1() const { return arg1_; }
  std::string const& Arg2() const { return arg2_; }
  void* Arg3() const { return arg3_; }

 private:
  DependencyInterface* dep_;
  int32_t arg1_;
  std::string arg2_;
  void* arg3_;
};
DEFINE_REGISTERER(DepThreeArgumentsBase, int32_t, std::string const&, void*);
#define NEW_THREE_ARGS_REGISTRATION(name, cb) \
  NEW_ENTITY_REGISTRATION_CB(name, DepThreeArgumentsBase, cb)

class DepThreeArgumentsTest : public DepThreeArgumentsBase {
 public:
  DepThreeArgumentsTest(DependencyInterface* dep, int32_t arg1,
                        std::string const& arg2, void* arg3)
      : DepThreeArgumentsBase(dep, arg1, arg2, arg3) {}

  // Factory method used to register with a concrete dependency.
  static DepThreeArgumentsBase* Create(DependencyInterface* dep, int32_t arg1,
                                       std::string const& arg2, void* arg3) {
    return new DepThreeArgumentsTest(dep, arg1, arg2, arg3);
  }
};

static void DepThreeArgumentsClientTest(absl::string_view classname,
                                        absl::string_view expected_dep_type) {
  int32_t arg1 = 1;
  std::string arg2 = "1";
  void* arg3 = nullptr;
  // Client code instantiating the object needs no knowledge of the
  // specific dependency injected.
  std::unique_ptr<DepThreeArgumentsBase> obj(
      DepThreeArgumentsBaseRegisterer::CreateByNameOrDie(classname, arg1, arg2,
                                                         arg3));
  ASSERT_TRUE(obj.get() != nullptr);
  // Check that the object has the expected dependency injected.
  EXPECT_EQ(expected_dep_type, obj->Dependency()->Type());
  // Check that the object was constructed with the correct args.
  EXPECT_EQ(arg1, obj->Arg1());
  EXPECT_EQ(arg2, obj->Arg2());
  EXPECT_EQ(arg3, obj->Arg3());

  // Now try the same thing, but via the factory object.
  DepThreeArgumentsBaseCreator* creator =
      DepThreeArgumentsBaseRegisterer::GetByNameOrDie(classname);
  ASSERT_TRUE(creator != nullptr);
  obj.reset((*creator)(arg1, arg2, arg3));
  ASSERT_TRUE(obj.get() != nullptr);
  // Check that the object has the expected dependency injected.
  EXPECT_EQ(expected_dep_type, obj->Dependency()->Type());
  // Check that the object was constructed with the correct args.
  EXPECT_EQ(arg1, obj->Arg1());
  EXPECT_EQ(arg2, obj->Arg2());
  EXPECT_EQ(arg3, obj->Arg3());
}

TEST_F(DynamicRegistererTest, DepThreeArguments) {
  std::string const kClassName = "DepThreeArgumentsTest";
  EXPECT_FALSE(DepThreeArgumentsBaseRegisterer::IsValidName(kClassName));
  {
    // As long as the following object is in scope, we can instantiate
    // "DepThreeArgumentsTest" objects. Additionally, the objects created
    // will have a heavyweight dependency injected.
    std::unique_ptr<DepThreeArgumentsBaseRegisterer> registerer(
        NEW_THREE_ARGS_REGISTRATION(
            kClassName,
            absl::bind_front(
                &DepThreeArgumentsTest::Create,
                absl::implicit_cast<DependencyInterface*>(&largedep_))));

    DepThreeArgumentsClientTest(kClassName, LargeDependency::type_);
  }
  EXPECT_FALSE(DepThreeArgumentsBaseRegisterer::IsValidName(kClassName));
  {
    // As long as the following object is in scope, we can instantiate
    // "DepThreeArgumentsTest" objects. Additionally, the objects created
    // will have a lightweight mock dependency injected.
    std::unique_ptr<DepThreeArgumentsBaseRegisterer> registerer(
        NEW_THREE_ARGS_REGISTRATION(
            kClassName,
            absl::bind_front(
                &DepThreeArgumentsTest::Create,
                absl::implicit_cast<DependencyInterface*>(&mockdep_))));

    DepThreeArgumentsClientTest(kClassName, MockDependency::type_);
  }
  EXPECT_FALSE(DepThreeArgumentsBaseRegisterer::IsValidName(kClassName));
}

///////////////////////////////////
// Four arguments
///////////////////////////////////

class DepFourArgumentsBase {
 public:
  DepFourArgumentsBase(DependencyInterface* dep, int32_t arg1,
                       std::string const& arg2, void* arg3, void* arg4)
      : dep_(dep), arg1_(arg1), arg2_(arg2), arg3_(arg3), arg4_(arg4) {}
  // Returns a pointer to the injected dependency (for examination).
  DependencyInterface const* Dependency() { return dep_; }
  int32_t Arg1() const { return arg1_; }
  std::string const& Arg2() const { return arg2_; }
  void* Arg3() const { return arg3_; }
  void* Arg4() const { return arg4_; }

 private:
  DependencyInterface* dep_;
  int32_t arg1_;
  std::string arg2_;
  void* arg3_;
  void* arg4_;
};
DEFINE_REGISTERER(DepFourArgumentsBase, int32_t, std::string const&, void*,
                  void*);
#define NEW_FOUR_ARGS_REGISTRATION(name, cb) \
  NEW_ENTITY_REGISTRATION_CB(name, DepFourArgumentsBase, cb)

class DepFourArgumentsTest : public DepFourArgumentsBase {
 public:
  DepFourArgumentsTest(DependencyInterface* dep, int32_t arg1,
                       std::string const& arg2, void* arg3, void* arg4)
      : DepFourArgumentsBase(dep, arg1, arg2, arg3, arg4) {}

  // Factory method used to register with a concrete dependency.
  static DepFourArgumentsBase* Create(DependencyInterface* dep, int32_t arg1,
                                      std::string const& arg2, void* arg3,
                                      void* arg4) {
    return new DepFourArgumentsTest(dep, arg1, arg2, arg3, arg4);
  }
};

static void DepFourArgumentsClientTest(absl::string_view classname,
                                       absl::string_view expected_dep_type) {
  int32_t arg1 = 1;
  std::string arg2 = "1";
  void* arg3 = nullptr;
  void* arg4 = &arg3;
  // Client code instantiating the object needs no knowledge of the
  // specific dependency injected.
  std::unique_ptr<DepFourArgumentsBase> obj(
      DepFourArgumentsBaseRegisterer::CreateByNameOrDie(classname, arg1, arg2,
                                                        arg3, arg4));
  ASSERT_TRUE(obj.get() != nullptr);
  // Check that the object has the expected dependency injected.
  EXPECT_EQ(expected_dep_type, obj->Dependency()->Type());
  // Check that the object was constructed with the correct args.
  EXPECT_EQ(arg1, obj->Arg1());
  EXPECT_EQ(arg2, obj->Arg2());
  EXPECT_EQ(arg3, obj->Arg3());
  EXPECT_EQ(arg4, obj->Arg4());

  // Now try the same thing, but via the factory object.
  DepFourArgumentsBaseCreator* creator =
      DepFourArgumentsBaseRegisterer::GetByNameOrDie(classname);
  ASSERT_TRUE(creator != nullptr);
  obj.reset((*creator)(arg1, arg2, arg3, arg4));
  ASSERT_TRUE(obj.get() != nullptr);
  // Check that the object has the expected dependency injected.
  EXPECT_EQ(expected_dep_type, obj->Dependency()->Type());
  // Check that the object was constructed with the correct args.
  EXPECT_EQ(arg1, obj->Arg1());
  EXPECT_EQ(arg2, obj->Arg2());
  EXPECT_EQ(arg3, obj->Arg3());
  EXPECT_EQ(arg4, obj->Arg4());
}

TEST_F(DynamicRegistererTest, DepFourArguments) {
  std::string const kClassName = "DepFourArgumentsTest";
  EXPECT_FALSE(DepFourArgumentsBaseRegisterer::IsValidName(kClassName));
  {
    // As long as the following object is in scope, we can instantiate
    // "DepFourArgumentsTest" objects. Additionally, the objects created
    // will have a heavyweight dependency injected.
    std::unique_ptr<DepFourArgumentsBaseRegisterer> registerer(
        NEW_FOUR_ARGS_REGISTRATION(
            kClassName,
            absl::bind_front(
                &DepFourArgumentsTest::Create,
                absl::implicit_cast<DependencyInterface*>(&largedep_))));

    DepFourArgumentsClientTest(kClassName, LargeDependency::type_);
  }
  EXPECT_FALSE(DepFourArgumentsBaseRegisterer::IsValidName(kClassName));
  {
    // As long as the following object is in scope, we can instantiate
    // "DepFourArgumentsTest" objects. Additionally, the objects created
    // will have a lightweight mock dependency injected.
    std::unique_ptr<DepFourArgumentsBaseRegisterer> registerer(
        NEW_FOUR_ARGS_REGISTRATION(
            kClassName,
            absl::bind_front(
                &DepFourArgumentsTest::Create,
                absl::implicit_cast<DependencyInterface*>(&mockdep_))));

    DepFourArgumentsClientTest(kClassName, MockDependency::type_);
  }
  EXPECT_FALSE(DepFourArgumentsBaseRegisterer::IsValidName(kClassName));
}

///////////////////////////////////
// StatusOr Registration
///////////////////////////////////

namespace Zero_Arg {
struct RegisterStatusOr0 {};

DEFINE_FACTORY_REGISTERER(RegisterStatusOr0,
                          absl::StatusOr<RegisterStatusOr0*>);
#define REGISTER_STATUSOR_ZERO_ARG(name, factory) \
  REGISTER_FACTORY_ENTITY(name, RegisterStatusOr0, factory)

struct Success : public RegisterStatusOr0 {
  static absl::StatusOr<RegisterStatusOr0*> Create() { return new Success(); }
};
REGISTER_STATUSOR_ZERO_ARG(Success, Success::Create);

struct Error : public RegisterStatusOr0 {
  static absl::StatusOr<RegisterStatusOr0*> Create() {
    return absl::CancelledError();
  }
};
REGISTER_STATUSOR_ZERO_ARG(Error, Error::Create);

TEST_F(DynamicRegistererTest, RegisterStatusOr_0_Arg) {
  absl::StatusOr<RegisterStatusOr0*> success =
      RegisterStatusOr0Registerer::CreateByNameOrDie("Success");
  EXPECT_NE(nullptr, success.value());
  delete *success;

  absl::StatusOr<RegisterStatusOr0*> error =
      RegisterStatusOr0Registerer::CreateByNameOrDie("Error");
  EXPECT_EQ(absl::CancelledError(), error.status());
}
}  // namespace Zero_Arg

namespace One_Arg {
struct RegisterStatusOr1 {};

DEFINE_FACTORY_REGISTERER(RegisterStatusOr1, absl::StatusOr<RegisterStatusOr1*>,
                          int);
#define REGISTER_STATUSOR_ONE_ARG(name, factory) \
  REGISTER_FACTORY_ENTITY(name, RegisterStatusOr1, factory)

struct Success : public RegisterStatusOr1 {
  static absl::StatusOr<RegisterStatusOr1*> Create(int arg) {
    return new Success();
  }
};
REGISTER_STATUSOR_ONE_ARG(Success, Success::Create);

struct Error : public RegisterStatusOr1 {
  static absl::StatusOr<RegisterStatusOr1*> Create(int arg) {
    return absl::CancelledError();
  }
};
REGISTER_STATUSOR_ONE_ARG(Error, Error::Create);

TEST_F(DynamicRegistererTest, RegisterStatusOr_1_Arg) {
  absl::StatusOr<RegisterStatusOr1*> success =
      RegisterStatusOr1Registerer::CreateByNameOrDie("Success", 0);
  EXPECT_NE(nullptr, success.value());
  delete *success;

  absl::StatusOr<RegisterStatusOr1*> error =
      RegisterStatusOr1Registerer::CreateByNameOrDie("Error", 0);
  EXPECT_EQ(absl::CancelledError(), error.status());
}
}  // namespace One_Arg

TEST(Registerer, ThreadSafety) {
  struct Type {};

  auto& reg = util_registration::internal::GetRegistry<Type>();
  absl::Notification notify;
  size_t sink = 0;
  thread::Executor::DefaultExecutor()->Schedule([&] {
    for (int i = 0; i < 1000; ++i) {
      // Iterate over the registry and read all its contents in a loop to
      // trigger TSan failures in case of unsynchronized access.
      for (const auto& name : reg.GetNames()) {
        const auto& obj = reg.Lookup(name);
        sink += absl::Hash<void*>{}(obj.object) +
                absl::Hash<std::string>{}(obj.filename);
      }
    }
    notify.Notify();
  });

  int dummy = 0;
  for (int i = 0; i < 1000; ++i) {
    reg.Insert(absl::StrCat(i), &dummy, __FILE__);
  }

  notify.WaitForNotification();
  EXPECT_THAT(sink, testing::AnyOf(testing::Eq(0), testing::Ne(0)));
}

// TODO: Add a basic test for 2, 3, and 4 args.

///////////////////////////////////
// Move-only construction
///////////////////////////////////

struct MoveOnlyConstructorBase {
  explicit MoveOnlyConstructorBase(std::unique_ptr<int32_t> arg)
      : t1(std::move(arg)) {}

  std::unique_ptr<int32_t> t1;
};

DEFINE_REGISTERER(MoveOnlyConstructorBase, std::unique_ptr<int32_t>);
#define REGISTER_MOVE_ONLY_CONSTRUCTOR(name) \
  REGISTER_ENTITY(name, MoveOnlyConstructorBase);
#define REGISTER_FACTORY_MOVE_ONLY_CONSTRUCTOR(name, FactoryName) \
  REGISTER_FACTORY_ENTITY(name, MoveOnlyConstructorBase, FactoryName);

struct MoveOnlyConstructor : public MoveOnlyConstructorBase {
  explicit MoveOnlyConstructor(std::unique_ptr<int32_t> arg)
      : MoveOnlyConstructorBase(std::move(arg)) {}
};
REGISTER_MOVE_ONLY_CONSTRUCTOR(MoveOnlyConstructor);

TEST(MoveOnlyConstructor, CreateByName) {
  // Check that we can create an instance of the relevant class.
  auto move_only_arg = std::make_unique<int32_t>(1);
  MoveOnlyConstructorBase* test1 =
      MoveOnlyConstructorBaseRegisterer::CreateByNameOrDie(
          "MoveOnlyConstructor", std::move(move_only_arg));
  ASSERT_NE(test1, nullptr);
  ASSERT_NE(test1->t1, nullptr);
  EXPECT_EQ(*test1->t1, 1);
  delete test1;
}

///////////////////////////////////
// Benchmarks
///////////////////////////////////

void BM_GetByName(benchmark::State& state) {
  std::string key(kNoArgumentsTest1);
  for (auto _ : state) {
    NoArgumentsBaseRegisterer::GetByNameOrDie(kNoArgumentsTest1);
  }
}

BENCHMARK(BM_GetByName);

}  // namespace
