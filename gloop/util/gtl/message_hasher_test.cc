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

#include "gloop/util/gtl/message_hasher.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/hash/hash.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gloop/util/gtl/message_hasher_unittest.pb.h"
#include "gloop/util/gtl/stl_util.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "google/protobuf/message.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"

namespace {

using google::protobuf::io::ArrayOutputStream;
using google::protobuf::io::CodedOutputStream;

// This is the fastest possible way to serialize a proto deterministically.  It
// can be used as a baseline to compare the performance of hashing after
// serialization.
struct not_a_hash {
  size_t operator()(const google::protobuf::MessageLite& pb) const {
    static absl::NoDestructor<std::string> s;
    gtl::STLStringResizeUninitialized(s.get(), pb.ByteSizeLong());
    ArrayOutputStream array_stream(s->data(), s->size());
    {
      CodedOutputStream stream(&array_stream);
      stream.SetSerializationDeterministic(true);
      pb.SerializeWithCachedSizes(&stream);
    }
    return 0;
  }
};

struct old_pb_hash {
  size_t operator()(const google::protobuf::MessageLite& pb) const {
    absl::Hash<std::string> hasher;
    return hasher(gtl::internal_message_hasher::SerializeDeterministically(pb));
  }
};

struct old_pb_equals {
  bool operator()(const google::protobuf::MessageLite& x,
                  const google::protobuf::MessageLite& y) const {
    return gtl::internal_message_hasher::SerializeDeterministically(x) ==
           gtl::internal_message_hasher::SerializeDeterministically(y);
  }
};

struct MessageDifferencerEquals {
  bool operator()(const google::protobuf::Message& x,
                  const google::protobuf::Message& y) const {
    if (x.GetDescriptor() != y.GetDescriptor()) {
      return false;
    }
    return google::protobuf::util::MessageDifferencer::Equals(x, y);
  }
};

struct PbHashPbEquals {
  using Hasher = gtl::pb_hash;
  using Eq = gtl::pb_equals;
};

struct OldPbHashPbEquals {
  using Hasher = old_pb_hash;
  using Eq = old_pb_equals;
};

struct PbHashMessageDifferencer {
  using Hasher = gtl::pb_hash;
  using Eq = MessageDifferencerEquals;
};

void SetSomeFields(gtl::TestMessage* tm) {
  tm->set_some_number(3);
  tm->set_name("Bob Hope");
  tm->set_weight(153.3);
  tm->add_flags(true);
  tm->add_flags(false);
}

template <typename T>
class MessageHashTest : public ::testing::Test {};

using MessageHashTestTypes = ::testing::Types<PbHashPbEquals, OldPbHashPbEquals,
                                              PbHashMessageDifferencer>;

TYPED_TEST_SUITE(MessageHashTest, MessageHashTestTypes);

TYPED_TEST(MessageHashTest, HashForEmpty) {
  gtl::TestMessage msg;
  typename TypeParam::Hasher hasher;
  typename TypeParam::Eq eq;
  EXPECT_EQ(hasher(msg), hasher(msg));
  EXPECT_TRUE(eq(msg, msg));
}

TYPED_TEST(MessageHashTest, SeparateObjects) {
  gtl::TestMessage msg1, msg2;
  SetSomeFields(&msg1);
  SetSomeFields(&msg2);
  typename TypeParam::Hasher hasher;
  typename TypeParam::Eq eq;
  EXPECT_EQ(hasher(msg1), hasher(msg2));
  EXPECT_TRUE(eq(msg1, msg2));
}

TYPED_TEST(MessageHashTest, NotEqualSimple) {
  gtl::TestMessage msg1, msg2;
  SetSomeFields(&msg1);
  SetSomeFields(&msg2);
  typename TypeParam::Hasher hasher;
  typename TypeParam::Eq eq;

  // Different atomic value
  msg2.set_name("other name");
  EXPECT_FALSE(eq(msg1, msg2));
  EXPECT_NE(hasher(msg1), hasher(msg2));

  msg2 = msg1;
  ASSERT_EQ(hasher(msg1), hasher(msg2));
  ASSERT_TRUE(eq(msg1, msg2));

  // Added elements to repeated field
  msg2.add_flags(false);
  EXPECT_FALSE(eq(msg1, msg2));
  EXPECT_NE(hasher(msg1), hasher(msg2));

  msg2 = msg1;
  ASSERT_EQ(hasher(msg1), hasher(msg2));
  ASSERT_TRUE(eq(msg1, msg2));

  // Same elements, different order
  msg2.clear_flags();
  msg2.add_flags(false);
  msg2.add_flags(true);
  EXPECT_FALSE(eq(msg1, msg2));
  EXPECT_NE(hasher(msg1), hasher(msg2));

  msg2 = msg1;
  ASSERT_EQ(hasher(msg1), hasher(msg2));
  ASSERT_TRUE(eq(msg1, msg2));

  // Cleared field.
  msg2.clear_name();
  EXPECT_FALSE(eq(msg1, msg2));
  EXPECT_NE(hasher(msg1), hasher(msg2));
}

TYPED_TEST(MessageHashTest, CopiesOfEachOther) {
  gtl::TestMessage msg1, msg2;
  SetSomeFields(&msg1);
  msg2 = msg1;
  typename TypeParam::Hasher hasher;
  typename TypeParam::Eq eq;
  EXPECT_EQ(hasher(msg1), hasher(msg2));
  EXPECT_TRUE(eq(msg1, msg2));
}

TYPED_TEST(MessageHashTest, CordFieldsWorkCorrectly) {
  std::string s(1024, 'a');
  const absl::Cord constructed(s + s);
  absl::Cord concatenated;
  concatenated.Append(s);
  concatenated.Append(s);
  ASSERT_FALSE(concatenated.TryFlat());
  ASSERT_TRUE(constructed.TryFlat());
  ASSERT_EQ(constructed, concatenated);
  gtl::WithCordField m1, m2;
  m1.set_cord(constructed);
  m2.set_cord(concatenated);
  ASSERT_TRUE(m1.cord().TryFlat());
  ASSERT_FALSE(m2.cord().TryFlat());
  typename TypeParam::Hasher hasher;
  typename TypeParam::Eq eq;
  auto h1 = hasher(m1);
  auto h2 = hasher(m2);
  EXPECT_EQ(h1, h2);
  EXPECT_TRUE(eq(m1, m2));
}

TYPED_TEST(MessageHashTest, ComplexMessage) {
  gtl::ComplexTestMessage msg1, msg2;
  SetSomeFields(msg1.add_message_field());
  (*msg1.mutable_map_field())["something"] = true;
  (*msg1.mutable_map_field())["something else"] = false;
  SetSomeFields(msg2.add_message_field());
  (*msg2.mutable_map_field())["something else"] = false;
  (*msg2.mutable_map_field())["something"] = true;
  typename TypeParam::Hasher hasher;
  EXPECT_EQ(hasher(msg1), hasher(msg2));
  typename TypeParam::Eq eq;
  EXPECT_TRUE(eq(msg1, msg2));
}

TYPED_TEST(MessageHashTest, NotEqualComplex) {
  gtl::ComplexTestMessage msg1, msg2;
  SetSomeFields(msg1.add_message_field());
  (*msg1.mutable_map_field())["something"] = true;
  (*msg1.mutable_map_field())["something else"] = false;
  msg2 = msg1;
  typename TypeParam::Hasher hasher;
  typename TypeParam::Eq eq;
  ASSERT_EQ(hasher(msg1), hasher(msg2));
  ASSERT_TRUE(eq(msg1, msg2));

  // Diff in map
  (*msg2.mutable_map_field())["something"] = false;
  EXPECT_FALSE(eq(msg1, msg2));
  EXPECT_NE(hasher(msg1), hasher(msg2));

  msg2 = msg1;
  ASSERT_EQ(hasher(msg1), hasher(msg2));
  ASSERT_TRUE(eq(msg1, msg2));

  // Swap in map
  (*msg2.mutable_map_field())["something"] = false;
  (*msg2.mutable_map_field())["something else"] = true;
  EXPECT_FALSE(eq(msg1, msg2));
  EXPECT_NE(hasher(msg1), hasher(msg2));
}

TYPED_TEST(MessageHashTest, SeparateWithExtensions) {
  auto set_extension = [](gtl::ComplexTestMessage* tm) {
    auto* ext = tm->MutableExtension(gtl::ExtensionMessage::extension_message);
    ext->set_extended_bool(true);
    auto* msg = ext->mutable_inner_message();
    (*msg->mutable_map_field())["inner thing"] = true;
    (*msg->mutable_map_field())["inner other thing"] = false;
  };
  gtl::ComplexTestMessage msg1, msg2;
  SetSomeFields(msg1.add_message_field());
  set_extension(&msg1);
  SetSomeFields(msg2.add_message_field());
  set_extension(&msg2);
  typename TypeParam::Hasher hasher;
  EXPECT_EQ(hasher(msg1), hasher(msg2));
  typename TypeParam::Eq eq;
  EXPECT_TRUE(eq(msg1, msg2));
}

TYPED_TEST(MessageHashTest, NotEqualWithExtensions) {
  auto set_extension = [](gtl::ComplexTestMessage* tm) {
    auto* ext = tm->MutableExtension(gtl::ExtensionMessage::extension_message);
    ext->set_extended_bool(true);
    auto* msg = ext->mutable_inner_message();
    (*msg->mutable_map_field())["inner thing"] = true;
    (*msg->mutable_map_field())["inner other thing"] = false;
  };
  gtl::ComplexTestMessage msg1, msg2;
  SetSomeFields(msg1.add_message_field());
  set_extension(&msg1);
  msg2 = msg1;
  typename TypeParam::Hasher hasher;
  typename TypeParam::Eq eq;
  ASSERT_EQ(hasher(msg1), hasher(msg2));
  ASSERT_TRUE(eq(msg1, msg2));

  // Change within the extension
  {
    auto* ext = msg2.MutableExtension(gtl::ExtensionMessage::extension_message);
    ext->set_extended_bool(false);
  }
  EXPECT_FALSE(eq(msg1, msg2));
  EXPECT_NE(hasher(msg1), hasher(msg2));

  msg2 = msg1;
  ASSERT_EQ(hasher(msg1), hasher(msg2));
  ASSERT_TRUE(eq(msg1, msg2));

  {
    // Cleared one side of extension
    msg2.ClearExtension(gtl::ExtensionMessage::extension_message);
    EXPECT_FALSE(eq(msg1, msg2));
    EXPECT_NE(hasher(msg1), hasher(msg2));
    // Change of the extension type, but otherwise identical content
    auto* ext =
        msg2.MutableExtension(gtl::OtherExtensionMessage::extension_message);
    ext->set_extended_bool(true);
    auto* msg = ext->mutable_inner_message();
    (*msg->mutable_map_field())["inner thing"] = true;
    (*msg->mutable_map_field())["inner other thing"] = false;
  }
  EXPECT_FALSE(eq(msg1, msg2));
  EXPECT_NE(hasher(msg1), hasher(msg2));
}

TYPED_TEST(MessageHashTest, UnknownExtensions) {
  gtl::ComplexTestMessageWithMoreFields original_msg;
  (*original_msg.mutable_map_field())["something"] = true;
  original_msg.set_prior_field(false);
  auto* ext =
      original_msg.MutableExtension(gtl::UnknownExtension::unknown_extension);
  ext->set_extended_bool(false);
  gtl::ComplexTestMessage msg1, msg2;
  ASSERT_TRUE(msg1.ParseFromString(original_msg.SerializeAsString()));
  ext->set_extended_bool(true);
  ASSERT_TRUE(msg2.ParseFromString(original_msg.SerializeAsString()));

  typename TypeParam::Hasher hasher;
  EXPECT_NE(hasher(msg1), hasher(msg2));
  typename TypeParam::Eq eq;
  EXPECT_FALSE(eq(msg1, msg2));
  msg1.DiscardUnknownFields();
  msg2.DiscardUnknownFields();
  EXPECT_EQ(hasher(msg1), hasher(msg2));
  EXPECT_TRUE(eq(msg1, msg2));
}

TYPED_TEST(MessageHashTest, UnknownFieldsSimple) {
  gtl::TestMessageWithAdditionalBool original_msg;
  original_msg.set_some_number(1);
  original_msg.set_other_bool(false);
  gtl::TestMessage msg1, msg2;
  ASSERT_TRUE(msg1.ParseFromString(original_msg.SerializeAsString()));
  original_msg.set_other_bool(true);
  ASSERT_TRUE(msg2.ParseFromString(original_msg.SerializeAsString()));
  typename TypeParam::Hasher hasher;
  EXPECT_NE(hasher(msg1), hasher(msg2));
  typename TypeParam::Eq eq;
  EXPECT_FALSE(eq(msg1, msg2));
  msg1.DiscardUnknownFields();
  msg2.DiscardUnknownFields();
  EXPECT_EQ(hasher(msg1), hasher(msg2));
  EXPECT_TRUE(eq(msg1, msg2));
}

TYPED_TEST(MessageHashTest, UnknownFieldsComplex) {
  // In this test, two unknown fields are added to the ComplexTestMessage, one
  // before the existing fields (in position 1), one after (in position 7).
  gtl::ComplexTestMessageWithMoreFields original_msg;
  (*original_msg.mutable_map_field())["something"] = true;
  original_msg.set_prior_field(false);
  gtl::ComplexTestMessage msg1, msg2;
  ASSERT_TRUE(msg1.ParseFromString(original_msg.SerializeAsString()));
  original_msg.set_prior_field(true);
  ASSERT_TRUE(msg2.ParseFromString(original_msg.SerializeAsString()));

  typename TypeParam::Hasher hasher;
  EXPECT_NE(hasher(msg1), hasher(msg2));
  typename TypeParam::Eq eq;
  EXPECT_FALSE(eq(msg1, msg2));
  msg1.DiscardUnknownFields();
  msg2.DiscardUnknownFields();
  EXPECT_EQ(hasher(msg1), hasher(msg2));
  EXPECT_TRUE(eq(msg1, msg2));

  original_msg.set_later_field(false);
  ASSERT_TRUE(msg1.ParseFromString(original_msg.SerializeAsString()));
  original_msg.set_later_field(true);
  ASSERT_TRUE(msg2.ParseFromString(original_msg.SerializeAsString()));
  EXPECT_NE(hasher(msg1), hasher(msg2));
  EXPECT_FALSE(eq(msg1, msg2));
  msg1.DiscardUnknownFields();
  msg2.DiscardUnknownFields();
  EXPECT_EQ(hasher(msg1), hasher(msg2));
  EXPECT_TRUE(eq(msg1, msg2));

  auto* inner = original_msg.add_message_field();
  inner->set_some_number(1);
  inner->set_other_bool(false);
  ASSERT_TRUE(msg1.ParseFromString(original_msg.SerializeAsString()));
  inner->set_other_bool(true);
  ASSERT_TRUE(msg2.ParseFromString(original_msg.SerializeAsString()));
}

// If any of the Mix() calls in the code are accidentally omitted, at least one
// of these tests will fail.
TYPED_TEST(MessageHashTest, DifferentAliasedStringsDifferentHashes) {
  typename TypeParam::Hasher hasher;
  gtl::TestMessage m;
  absl::flat_hash_map<size_t, gtl::TestMessage> ms;
  const size_t kNameSize = 1024;
  m.set_name(std::string(kNameSize, 'a'));
  ms[gtl::pb_hash{}(m)] = m;
  for (size_t i = 0; i < kNameSize; ++i) {
    ++(*m.mutable_name())[i];
    auto [it, p] = ms.emplace(hasher(m), m);
    EXPECT_TRUE(p) << "Message {\n"
                   << it->second.DebugString()
                   << "\n}\n\n has the same hash as message {\n"
                   << absl::StrCat(m) << "\n}";
    --(*m.mutable_name())[i];
  }
}

TYPED_TEST(MessageHashTest, DifferentRepeatedBoolsDifferentValues) {
  typename TypeParam::Hasher hasher;
  gtl::TestMessage m;
  absl::flat_hash_map<size_t, gtl::TestMessage> ms;
  const size_t kNumBools = 2048;
  for (size_t i = 0; i < kNumBools; ++i) {
    m.add_flags(false);
  }
  for (size_t i = 0; i < kNumBools; ++i) {
    m.set_flags(i, !m.flags(i));
    auto [it, p] = ms.emplace(hasher(m), m);
    EXPECT_TRUE(p) << "Message {\n"
                   << it->second.DebugString()
                   << "\n}\n\n has the same hash as message {\n"
                   << absl::StrCat(m) << "\n}";
    m.set_flags(i, !m.flags(i));
  }
}

TEST(MessageHasherTest, FlatSetIsValid) {
  gtl::pb_flat_hash_set<gtl::TestMessage> set1;
  EXPECT_TRUE(set1.empty());
  gtl::TestMessage p1;
  set1.insert(p1);
  EXPECT_FALSE(set1.empty());
  set1.insert(p1);
  EXPECT_EQ(1, set1.size());
  SetSomeFields(&p1);
  set1.insert(p1);
  EXPECT_EQ(2, set1.size());
  for (auto i = set1.begin(); i != set1.end(); ++i) {
    EXPECT_TRUE(i == set1.find(*i));
  }
  set1.erase(*(set1.begin()));
  EXPECT_EQ(1, set1.size());
}

TEST(MessageHasherTest, NodeSetIsValid) {
  gtl::pb_node_hash_set<gtl::TestMessage> set1;
  EXPECT_TRUE(set1.empty());
  gtl::TestMessage p1;
  set1.insert(p1);
  EXPECT_FALSE(set1.empty());
  set1.insert(p1);
  EXPECT_EQ(1, set1.size());
  SetSomeFields(&p1);
  set1.insert(p1);
  EXPECT_EQ(2, set1.size());
  for (auto i = set1.begin(); i != set1.end(); ++i) {
    EXPECT_TRUE(i == set1.find(*i));
  }
  set1.erase(*(set1.begin()));
  EXPECT_EQ(1, set1.size());
}

TEST(MessageHasherTest, LinkedSetIsValid) {
  gtl::pb_linked_hash_set<gtl::TestMessage> set1;
  EXPECT_TRUE(set1.empty());
  gtl::TestMessage p1;
  set1.insert(p1);
  EXPECT_FALSE(set1.empty());
  set1.insert(p1);
  EXPECT_EQ(1, set1.size());
  SetSomeFields(&p1);
  set1.insert(p1);
  EXPECT_EQ(2, set1.size());
  for (auto i = set1.begin(); i != set1.end(); ++i) {
    EXPECT_TRUE(i == set1.find(*i));
  }
  set1.erase(*(set1.begin()));
  EXPECT_EQ(1, set1.size());
}

TEST(MessageHasherTest, MapIsValid) {
  gtl::pb_hash_map<gtl::TestMessage, int> my_map;
  ASSERT_TRUE(my_map.empty());
  gtl::TestMessage pb;
  my_map[pb] = 3;
  ASSERT_EQ(3, my_map[pb]);
  ASSERT_FALSE(my_map.empty());
  my_map[pb] = 5;
  ASSERT_EQ(5, my_map[pb]);
  gtl::TestMessage pb2;
  SetSomeFields(&pb2);
  my_map[pb2] = 7;
  ASSERT_EQ(5, my_map[pb]);
}

TEST(MessageHasherTest, FlatMapIsValid) {
  gtl::pb_flat_hash_map<gtl::TestMessage, int> my_map;
  EXPECT_TRUE(my_map.empty());
  gtl::TestMessage pb;
  my_map[pb] = 3;
  EXPECT_EQ(3, my_map[pb]);
  EXPECT_FALSE(my_map.empty());
  my_map[pb] = 5;
  EXPECT_EQ(5, my_map[pb]);
  gtl::TestMessage pb2;
  SetSomeFields(&pb2);
  my_map[pb2] = 7;
  EXPECT_EQ(5, my_map[pb]);
  EXPECT_EQ(2, my_map.size());
}

TEST(MessageHasherTest, NodeMapIsValid) {
  gtl::pb_node_hash_map<gtl::TestMessage, int> my_map;
  EXPECT_TRUE(my_map.empty());
  gtl::TestMessage pb;
  my_map[pb] = 3;
  EXPECT_EQ(3, my_map[pb]);
  EXPECT_FALSE(my_map.empty());
  my_map[pb] = 5;
  EXPECT_EQ(5, my_map[pb]);
  gtl::TestMessage pb2;
  SetSomeFields(&pb2);
  my_map[pb2] = 7;
  EXPECT_EQ(5, my_map[pb]);
  EXPECT_EQ(2, my_map.size());
}

TEST(MessageHasherTest, LinkedMapIsValid) {
  gtl::pb_linked_hash_map<gtl::TestMessage, int> my_map;
  EXPECT_TRUE(my_map.empty());
  gtl::TestMessage pb;
  my_map[pb] = 3;
  EXPECT_EQ(3, my_map[pb]);
  EXPECT_FALSE(my_map.empty());
  my_map[pb] = 5;
  EXPECT_EQ(5, my_map[pb]);
  gtl::TestMessage pb2;
  SetSomeFields(&pb2);
  my_map[pb2] = 7;
  EXPECT_EQ(5, my_map[pb]);
  EXPECT_EQ(2, my_map.size());
}

TEST(OpaquePbHashKeyTest, HashEq) {
  gtl::TestMessage a;
  a.set_some_number(3);
  a.set_name("Bob Hope");
  a.set_weight(153.3);
  a.add_flags(true);
  a.add_flags(false);

  gtl::TestMessage b = a;
  b.set_some_number(42);
  // Make `b` large to exercise the streaming has part of the hash computation.
  for (int i = 0; i < 2000; ++i) {
    b.add_flags((i & 1) == 0);
  }

  const gtl::TestMessage empty;

  const gtl::OpaquePbHashKey empty_key(empty);
  const gtl::OpaquePbHashKey a_key(a);
  const gtl::OpaquePbHashKey b_key(b);

  const gtl::OpaquePbHashKey<gtl::TestMessage>::absl_container_hash hash;
  EXPECT_EQ(hash(empty_key), hash(empty));
  EXPECT_EQ(hash(a_key), hash(a));
  EXPECT_EQ(hash(b_key), hash(b));
  EXPECT_NE(hash(a_key), hash(b));
  EXPECT_NE(hash(a_key), hash(empty));

  const gtl::OpaquePbHashKey<gtl::TestMessage>::absl_container_eq eq;
  EXPECT_TRUE(eq(empty_key, empty_key));
  EXPECT_TRUE(eq(empty_key, empty));
  EXPECT_TRUE(eq(empty, empty_key));

  EXPECT_TRUE(eq(a_key, a_key));
  EXPECT_TRUE(eq(a_key, a));
  EXPECT_TRUE(eq(a, a_key));

  EXPECT_TRUE(eq(b_key, b_key));
  EXPECT_TRUE(eq(b_key, b));
  EXPECT_TRUE(eq(b, b_key));

  EXPECT_FALSE(eq(empty_key, a_key));
  EXPECT_FALSE(eq(empty_key, b_key));
  EXPECT_FALSE(eq(a_key, b_key));
  EXPECT_FALSE(eq(empty_key, a));
  EXPECT_FALSE(eq(empty_key, b));
  EXPECT_FALSE(eq(a_key, b));
  EXPECT_FALSE(eq(a, empty_key));
  EXPECT_FALSE(eq(b, empty_key));
  EXPECT_FALSE(eq(b, a_key));
}

TEST(OpaquePbHashKeyTest, FlatHashMap) {
  absl::flat_hash_map<gtl::OpaquePbHashKey<gtl::TestMessage>, int> my_map;
  ASSERT_TRUE(my_map.empty());
  gtl::TestMessage pb;
  my_map[pb] = 3;
  ASSERT_EQ(3, my_map[pb]);
  ASSERT_FALSE(my_map.empty());
  my_map[pb] = 5;
  ASSERT_EQ(5, my_map[pb]);
  gtl::TestMessage pb2;
  SetSomeFields(&pb2);
  my_map[pb2] = 7;
  ASSERT_EQ(5, my_map[pb]);
}

TEST(OpaquePbHashKeyTest, FlatHashMap_CompoundKey) {
  using KeyType = std::pair<int, gtl::OpaquePbHashKey<gtl::TestMessage>>;
  absl::flat_hash_map<KeyType, int> my_map;
  ASSERT_TRUE(my_map.empty());
  KeyType key{1, gtl::TestMessage()};
  my_map[key] = 3;
  ASSERT_EQ(3, my_map[key]);
  ASSERT_FALSE(my_map.empty());
  my_map[key] = 5;
  ASSERT_EQ(5, my_map[key]);
  gtl::TestMessage pb2;
  SetSomeFields(&pb2);
  KeyType key2{2, pb2};
  my_map[key2] = 7;
  ASSERT_EQ(5, my_map[key]);
}

// BENCHMARKS
template <typename Hasher>
void BM_HashEmptyMessage(benchmark::State& state) {
  gtl::TestMessage m;
  for (auto _ : state) {
    ::benchmark::DoNotOptimize(m);
    ::benchmark::DoNotOptimize(Hasher{}(m));
  }
}
BENCHMARK(BM_HashEmptyMessage<gtl::pb_hash>);
BENCHMARK(BM_HashEmptyMessage<old_pb_hash>);

template <typename Hasher>
void BM_HashTinyMessage(benchmark::State& state) {
  gtl::TestMessage m;
  m.set_some_number(1);
  for (auto _ : state) {
    ::benchmark::DoNotOptimize(m);
    ::benchmark::DoNotOptimize(Hasher{}(m));
  }
}
BENCHMARK(BM_HashTinyMessage<gtl::pb_hash>);
BENCHMARK(BM_HashTinyMessage<old_pb_hash>);

template <typename Hasher>
void BM_HashMessageWithSubstantialAliasing(benchmark::State& state) {
  gtl::TestMessage m;
  m.set_some_number(1);
  m.set_weight(0.1);
  m.add_flags(true);
  m.add_flags(false);
  m.set_name(std::string(state.range(0), 'a'));
  for (auto _ : state) {
    ::benchmark::DoNotOptimize(m);
    ::benchmark::DoNotOptimize(Hasher{}(m));
  }
  state.SetBytesProcessed(state.iterations() * m.ByteSizeLong());
}
BENCHMARK(BM_HashMessageWithSubstantialAliasing<gtl::pb_hash>)
    ->Range(0, 1 << 23);
BENCHMARK(BM_HashMessageWithSubstantialAliasing<old_pb_hash>)
    ->Range(0, 1 << 23);

// The "Packed.*Ints" benchmarks take as their argument the element at which the
// repeated ints should differ, so we can evaluate the performance of equality
// implementations that may or may not return early when they encounter a
// difference.
const int64_t kNumPackedInts = 64;
template <typename Equals>
void BM_PbEqualsPackedSmallInts(benchmark::State& state) {
  gtl::SimpleTestMessagePackedInt64 m1, m2;
  for (size_t i = 0; i < kNumPackedInts; ++i) {
    m1.add_num(i);
  }
  m2 = m1;
  m2.set_num(state.range(0), m2.num(state.range(0)) + 1);
  for (auto _ : state) {
    ::benchmark::DoNotOptimize(m1);
    ::benchmark::DoNotOptimize(m2);
    ::benchmark::DoNotOptimize(Equals{}(m1, m2));
  }
  state.SetBytesProcessed(static_cast<int64_t>(
      state.iterations() * (m1.ByteSizeLong() + m2.ByteSizeLong())));
}
BENCHMARK(BM_PbEqualsPackedSmallInts<gtl::pb_equals>)
    ->RangeMultiplier(2)
    ->Range(0, kNumPackedInts - 1)
    ->Threads(1)
    ->Threads(128);
BENCHMARK(BM_PbEqualsPackedSmallInts<old_pb_equals>)
    ->RangeMultiplier(2)
    ->Range(0, kNumPackedInts - 1)
    ->Threads(1)
    ->Threads(128);
BENCHMARK(BM_PbEqualsPackedSmallInts<MessageDifferencerEquals>)
    ->RangeMultiplier(2)
    ->Range(0, kNumPackedInts - 1)
    ->Threads(1)
    ->Threads(128);

// This is the same as above, but the overall message size is larger because the
// integers are larger (by an additive factor of 1 << 30).  This makes the
// message large enough that most reasonable serialization forms will have to
// allocate memory.
template <typename Equals>
void BM_PbEqualsPackedLargeInts(benchmark::State& state) {
  gtl::SimpleTestMessagePackedInt64 m1, m2;
  for (size_t i = 0; i < kNumPackedInts; ++i) {
    m1.add_num(i + (1 << 30));
  }
  m2 = m1;
  m2.set_num(state.range(0), m2.num(state.range(0)) + 1);
  for (auto _ : state) {
    ::benchmark::DoNotOptimize(m1);
    ::benchmark::DoNotOptimize(m2);
    ::benchmark::DoNotOptimize(Equals{}(m1, m2));
  }
  state.SetBytesProcessed(static_cast<int64_t>(
      state.iterations() * (m1.ByteSizeLong() + m2.ByteSizeLong())));
}
BENCHMARK(BM_PbEqualsPackedLargeInts<gtl::pb_equals>)
    ->RangeMultiplier(2)
    ->Range(0, kNumPackedInts - 1)
    ->Threads(1)
    ->Threads(128);
BENCHMARK(BM_PbEqualsPackedLargeInts<old_pb_equals>)
    ->RangeMultiplier(2)
    ->Range(0, kNumPackedInts - 1)
    ->Threads(1)
    ->Threads(128);
BENCHMARK(BM_PbEqualsPackedLargeInts<MessageDifferencerEquals>)
    ->RangeMultiplier(2)
    ->Range(0, kNumPackedInts - 1)
    ->Threads(1)
    ->Threads(128);

std::vector<gtl::TestMessage> MakeFlatHashMapBenchmarkProtos() {
  static constexpr int kSize = 1000;
  gtl::TestMessage pb;
  std::vector<gtl::TestMessage> pbs(kSize);
  for (int i = 0; i < kSize; ++i) {
    auto& pb = pbs[i];
    pb.set_some_number(i);
    pb.set_weight(0.1);
    pb.add_flags(true);
    pb.add_flags(false);
    pb.set_name(std::string(10, 'a'));
  }
  return pbs;
}

template <typename Map>
void BM_FlatHashMap_Insert(benchmark::State& state) {
  const auto pbs = MakeFlatHashMapBenchmarkProtos();
  for (auto _ : state) {
    Map map;
    for (const auto& pb : pbs) {
      map.emplace(pb, 42);
      benchmark::DoNotOptimize(map);
    }
  }
}
BENCHMARK(BM_FlatHashMap_Insert<
          absl::flat_hash_map<gtl::OpaquePbHashKey<gtl::TestMessage>, int>>);
BENCHMARK(BM_FlatHashMap_Insert<gtl::pb_flat_hash_map<gtl::TestMessage, int>>);

template <typename Map>
void BM_FlatHashMap_Lookup(benchmark::State& state) {
  const auto pbs = MakeFlatHashMapBenchmarkProtos();
  Map map;
  for (const auto& pb : pbs) {
    map.emplace(pb, 42);
  }
  for (auto _ : state) {
    for (const auto& pb : pbs) {
      auto it = map.find(pb);
      benchmark::DoNotOptimize(it);
    }
  }
}
BENCHMARK(BM_FlatHashMap_Lookup<
          absl::flat_hash_map<gtl::OpaquePbHashKey<gtl::TestMessage>, int>>);
BENCHMARK(BM_FlatHashMap_Lookup<gtl::pb_flat_hash_map<gtl::TestMessage, int>>);

}  // namespace
