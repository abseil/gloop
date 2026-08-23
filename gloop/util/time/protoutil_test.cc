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

#include "gloop/util/time/protoutil.h"

#include <cstdint>
#include <limits>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "google/protobuf/duration.pb.h"
#include "google/protobuf/timestamp.pb.h"
#include "gtest/gtest.h"

namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::Not;

google::protobuf::Duration MakeGoogleApiDuration(int64_t s, int32_t ns) {
  google::protobuf::Duration proto;
  proto.set_seconds(s);
  proto.set_nanos(ns);
  return proto;
}

google::protobuf::Timestamp MakeGoogleApiTimestamp(int64_t s, int32_t ns) {
  google::protobuf::Timestamp proto;
  proto.set_seconds(s);
  proto.set_nanos(ns);
  return proto;
}

// Helper function that tests the EncodeGoogleApiProto() and
// DecodeGoogleApiProto() functions. Both variants of the EncodeGoogleApiProto
// are tested to ensure they return the proto result. The templated first
// argument may be either a absl::Duration or a absl::Time. The template P
// represents a google::protobuf::Duration or google::protobuf::Timestamp.
template <typename T, typename P>
void RoundTripGoogleApi(T v, int64_t expected_sec, int32_t expected_nsec) {
  const auto sor_proto = util_time::EncodeGoogleApiProto(v);
  ASSERT_THAT(sor_proto, IsOk());
  const auto& proto = sor_proto.value();
  EXPECT_EQ(proto.seconds(), expected_sec);
  EXPECT_EQ(proto.nanos(), expected_nsec);

  P out_proto;
  const auto status = util_time::EncodeGoogleApiProto(v, &out_proto);
  EXPECT_THAT(status, IsOk());
  EXPECT_EQ(out_proto.seconds(), expected_sec);
  EXPECT_EQ(out_proto.nanos(), expected_nsec);
  EXPECT_EQ(proto.seconds(), out_proto.seconds());
  EXPECT_EQ(proto.nanos(), out_proto.nanos());

  // Complete the round-trip by decoding the proto back to a absl::Duration.
  const auto sor_duration = util_time::DecodeGoogleApiProto(proto);
  ASSERT_THAT(sor_duration, IsOk());
  const auto& duration = sor_duration.value();
  EXPECT_EQ(duration, v);
}

struct GoogleApiDurationTestCase {
  const char* name;
  absl::Duration d;
  int64_t expected_sec;
  int32_t expected_nsec;
};

using ProtoUtilGoogleApiRoundTripDurationTest =
    testing::TestWithParam<GoogleApiDurationTestCase>;

TEST_P(ProtoUtilGoogleApiRoundTripDurationTest, RoundTrip) {
  const auto& tc = GetParam();
  RoundTripGoogleApi<absl::Duration, google::protobuf::Duration>(
      tc.d, tc.expected_sec, tc.expected_nsec);
}

INSTANTIATE_TEST_SUITE_P(
    ProtoUtilGoogleApi, ProtoUtilGoogleApiRoundTripDurationTest,
    testing::ValuesIn(std::vector<GoogleApiDurationTestCase>{
        {"Zero", absl::Seconds(0), 0, 0},
        {"Positive", absl::Seconds(123) + absl::Nanoseconds(456), 123, 456},
        {"NegNsec", absl::Nanoseconds(-5), 0, -5},
        {"NegSec_NegNsec", absl::Seconds(-10) - absl::Nanoseconds(5), -10, -5},
        {"LargeNegative", absl::Seconds(-315576000000), -315576000000, 0},
        {"LargePositive", absl::Seconds(315576000000), 315576000000, 0},
        {"Min", util_time::MakeGoogleApiDurationMin(), -315576000000,
         -999999999},
        {"Max", util_time::MakeGoogleApiDurationMax(), 315576000000, 999999999},
    }),
    [](const testing::TestParamInfo<GoogleApiDurationTestCase>& info) {
      return info.param.name;
    });

struct DurationTruncTestCase {
  const char* name;
  absl::Duration d;
  int64_t expected_sec;
  int32_t expected_nsec;
};

using ProtoUtilGoogleApiDurationTruncTest =
    testing::TestWithParam<DurationTruncTestCase>;

TEST_P(ProtoUtilGoogleApiDurationTruncTest, Truncate) {
  const auto& tc = GetParam();
  const auto sor = util_time::EncodeGoogleApiProto(tc.d);
  ASSERT_THAT(sor, IsOk());
  const auto& proto = sor.value();
  EXPECT_EQ(proto.seconds(), tc.expected_sec) << "d=" << tc.d;
  EXPECT_EQ(proto.nanos(), tc.expected_nsec) << "d=" << tc.d;
}

INSTANTIATE_TEST_SUITE_P(
    ProtoUtilGoogleApi, ProtoUtilGoogleApiDurationTruncTest,
    testing::ValuesIn(std::vector<DurationTruncTestCase>{
        {"Tick", absl::Nanoseconds(1) / 4, 0, 0},
        {"NegTick", -absl::Nanoseconds(1) / 4, 0, 0},
        {"TwoSecPlusTick", absl::Seconds(2) + absl::Nanoseconds(1) / 4, 2, 0},
        {"TwoSecMinusTick", absl::Seconds(2) - absl::Nanoseconds(1) / 4, 1,
         999999999},
        {"OneSecPlusTick", absl::Seconds(1) + absl::Nanoseconds(1) / 4, 1, 0},
        {"OneSecMinusTick", absl::Seconds(1) - absl::Nanoseconds(1) / 4, 0,
         999999999},
        {"NegOneSecPlusTick", absl::Seconds(-1) + absl::Nanoseconds(1) / 4, 0,
         -999999999},
        {"NegOneSecMinusTick", absl::Seconds(-1) - absl::Nanoseconds(1) / 4, -1,
         0},
        {"NegTwoSecPlusTick", absl::Seconds(-2) + absl::Nanoseconds(1) / 4, -1,
         -999999999},
        {"NegTwoSecMinusTick", absl::Seconds(-2) - absl::Nanoseconds(1) / 4, -2,
         0},
        {"MinMinusTick",
         util_time::MakeGoogleApiDurationMin() - absl::Nanoseconds(1) / 4,
         -315576000000, -999999999},
        {"MinPlusTick",
         util_time::MakeGoogleApiDurationMin() + absl::Nanoseconds(1) / 4,
         -315576000000, -999999998},
        {"MaxMinusTick",
         util_time::MakeGoogleApiDurationMax() - absl::Nanoseconds(1) / 4,
         315576000000, 999999998},
        {"MaxPlusTick",
         util_time::MakeGoogleApiDurationMax() + absl::Nanoseconds(1) / 4,
         315576000000, 999999999},
    }),
    [](const testing::TestParamInfo<DurationTruncTestCase>& info) {
      return info.param.name;
    });

struct EncodeDurationErrorTestCase {
  const char* name;
  absl::Duration d;
};

using ProtoUtilGoogleApiEncodeDurationErrorTest =
    testing::TestWithParam<EncodeDurationErrorTestCase>;

TEST_P(ProtoUtilGoogleApiEncodeDurationErrorTest, Error) {
  const auto& tc = GetParam();
  EXPECT_THAT(util_time::EncodeGoogleApiProto(tc.d), Not(IsOk()))
      << "d=" << tc.d;

  google::protobuf::Duration proto;
  EXPECT_THAT(util_time::EncodeGoogleApiProto(tc.d, &proto), Not(IsOk()))
      << "d=" << tc.d;
}

INSTANTIATE_TEST_SUITE_P(
    ProtoUtilGoogleApi, ProtoUtilGoogleApiEncodeDurationErrorTest,
    testing::ValuesIn(std::vector<EncodeDurationErrorTestCase>{
        {"BelowMin",
         util_time::MakeGoogleApiDurationMin() - absl::Nanoseconds(1)},
        {"AboveMax",
         util_time::MakeGoogleApiDurationMax() + absl::Nanoseconds(1)},
        {"NegInfinite", -absl::InfiniteDuration()},
        {"PosInfinite", absl::InfiniteDuration()},
    }),
    [](const testing::TestParamInfo<EncodeDurationErrorTestCase>& info) {
      return info.param.name;
    });

struct DecodeDurationErrorTestCase {
  const char* name;
  google::protobuf::Duration proto;
};

using ProtoUtilGoogleApiDecodeDurationErrorTest =
    testing::TestWithParam<DecodeDurationErrorTestCase>;

TEST_P(ProtoUtilGoogleApiDecodeDurationErrorTest, Error) {
  const auto& tc = GetParam();
  EXPECT_THAT(util_time::DecodeGoogleApiProto(tc.proto), Not(IsOk()))
      << "proto=" << tc.proto.DebugString();
}

INSTANTIATE_TEST_SUITE_P(
    ProtoUtilGoogleApi, ProtoUtilGoogleApiDecodeDurationErrorTest,
    testing::ValuesIn(std::vector<DecodeDurationErrorTestCase>{
        {"PosSecNegNsec", MakeGoogleApiDuration(1, -1)},
        {"NegSecPosNsec", MakeGoogleApiDuration(-1, 1)},
        {"NsecTooLarge", MakeGoogleApiDuration(0, 999999999 + 1)},
        {"NsecTooSmall", MakeGoogleApiDuration(0, -999999999 - 1)},
        {"SecTooSmall", MakeGoogleApiDuration(-315576000000 - 1, 0)},
        {"SecTooLarge", MakeGoogleApiDuration(315576000000 + 1, 0)},
        {"Int64Min",
         MakeGoogleApiDuration(std::numeric_limits<int64_t>::min(), 0)},
        {"Int64Max",
         MakeGoogleApiDuration(std::numeric_limits<int64_t>::max(), 0)},
        {"Int32MinNsec",
         MakeGoogleApiDuration(0, std::numeric_limits<int32_t>::min())},
        {"Int32MaxNsec",
         MakeGoogleApiDuration(0, std::numeric_limits<int32_t>::max())},
        {"MinSecMinNsec",
         MakeGoogleApiDuration(std::numeric_limits<int64_t>::min(),
                               std::numeric_limits<int32_t>::min())},
        {"MaxSecMaxNsec",
         MakeGoogleApiDuration(std::numeric_limits<int64_t>::max(),
                               std::numeric_limits<int32_t>::max())},
    }),
    [](const testing::TestParamInfo<DecodeDurationErrorTestCase>& info) {
      return info.param.name;
    });

TEST(ProtoUtilGoogleApi, DurationProtoMax) {
  const auto proto_max = util_time::MakeGoogleApiDurationProtoMax();
  const absl::Duration duration_max = util_time::MakeGoogleApiDurationMax();
  EXPECT_THAT(util_time::DecodeGoogleApiProto(proto_max),
              IsOkAndHolds(duration_max));
}

TEST(ProtoUtilGoogleApi, DurationMaxIsMax) {
  const absl::Duration duration_max = util_time::MakeGoogleApiDurationMax();
  EXPECT_THAT(
      util_time::EncodeGoogleApiProto(duration_max + absl::Nanoseconds(1)),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ProtoUtilGoogleApi, DurationProtoMin) {
  const auto proto_min = util_time::MakeGoogleApiDurationProtoMin();
  const absl::Duration duration_min = util_time::MakeGoogleApiDurationMin();
  EXPECT_THAT(util_time::DecodeGoogleApiProto(proto_min),
              IsOkAndHolds(duration_min));
}

TEST(ProtoUtilGoogleApi, DurationMinIsMin) {
  const absl::Duration duration_min = util_time::MakeGoogleApiDurationMin();
  EXPECT_THAT(
      util_time::EncodeGoogleApiProto(duration_min - absl::Nanoseconds(1)),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

struct GoogleApiTimeTestCase {
  const char* name;
  absl::Time t;
  int64_t expected_sec;
  int32_t expected_nsec;
};

using ProtoUtilGoogleApiRoundTripTimeTest =
    testing::TestWithParam<GoogleApiTimeTestCase>;

TEST_P(ProtoUtilGoogleApiRoundTripTimeTest, RoundTrip) {
  const auto& tc = GetParam();
  RoundTripGoogleApi<absl::Time, google::protobuf::Timestamp>(
      tc.t, tc.expected_sec, tc.expected_nsec);
}

INSTANTIATE_TEST_SUITE_P(
    ProtoUtilGoogleApi, ProtoUtilGoogleApiRoundTripTimeTest,
    testing::ValuesIn(std::vector<GoogleApiTimeTestCase>{
        {"Epoch", absl::UnixEpoch(), 0, 0},
        {"EpochMinusOneNsec", absl::UnixEpoch() - absl::Nanoseconds(1), -1,
         999999999},
        {"EpochPlusOneNsec", absl::UnixEpoch() + absl::Nanoseconds(1), 0, 1},
        {"Positive",
         absl::UnixEpoch() + absl::Seconds(123) + absl::Nanoseconds(456), 123,
         456},
        {"EpochMinusFiveNsec", absl::UnixEpoch() - absl::Nanoseconds(5), -1,
         999999995},
        {"Negative",
         absl::UnixEpoch() - absl::Seconds(10) - absl::Nanoseconds(5), -11,
         999999995},
        {"Min", util_time::MakeGoogleApiTimeMin(), -62135596800, 0},
        {"Max", util_time::MakeGoogleApiTimeMax(), 253402300799, 999999999},
    }),
    [](const testing::TestParamInfo<GoogleApiTimeTestCase>& info) {
      return info.param.name;
    });

struct TimeTruncTestCase {
  const char* name;
  absl::Time t;
  int64_t expected_sec;
  int32_t expected_nsec;
};

using ProtoUtilGoogleApiTimeTruncTest =
    testing::TestWithParam<TimeTruncTestCase>;

TEST_P(ProtoUtilGoogleApiTimeTruncTest, Truncate) {
  const auto& tc = GetParam();
  const auto sor = util_time::EncodeGoogleApiProto(tc.t);
  ASSERT_THAT(sor, IsOk());
  const auto& proto = sor.value();
  EXPECT_EQ(proto.seconds(), tc.expected_sec) << "t=" << tc.t;
  EXPECT_EQ(proto.nanos(), tc.expected_nsec) << "t=" << tc.t;
}

INSTANTIATE_TEST_SUITE_P(
    ProtoUtilGoogleApi, ProtoUtilGoogleApiTimeTruncTest,
    testing::ValuesIn(std::vector<TimeTruncTestCase>{
        {"BeforeEpochPlusTick",
         absl::FromUnixSeconds(-1234567890) + absl::Nanoseconds(1) / 4,
         -1234567890, 0},
        {"BeforeEpochMinusTick",
         absl::FromUnixSeconds(-1234567890) - absl::Nanoseconds(1) / 4,
         -1234567891, 999999999},
        {"EpochPlusTick", absl::UnixEpoch() + absl::Nanoseconds(1) / 4, 0, 0},
        {"EpochMinusTick", absl::UnixEpoch() - absl::Nanoseconds(1) / 4, -1,
         999999999},
        {"AfterEpochPlusTick",
         absl::FromUnixSeconds(1234567890) + absl::Nanoseconds(1) / 4,
         1234567890, 0},
        {"AfterEpochMinusTick",
         absl::FromUnixSeconds(1234567890) - absl::Nanoseconds(1) / 4,
         1234567889, 999999999},
        {"MinPlusTick",
         util_time::MakeGoogleApiTimeMin() + absl::Nanoseconds(1) / 4,
         -62135596800, 0},
        {"MaxMinusTick",
         util_time::MakeGoogleApiTimeMax() - absl::Nanoseconds(1) / 4,
         253402300799, 999999998},
        {"MaxPlusTick",
         util_time::MakeGoogleApiTimeMax() + absl::Nanoseconds(1) / 4,
         253402300799, 999999999},
    }),
    [](const testing::TestParamInfo<TimeTruncTestCase>& info) {
      return info.param.name;
    });

struct EncodeTimeErrorTestCase {
  const char* name;
  absl::Time t;
};

using ProtoUtilGoogleApiEncodeTimeErrorTest =
    testing::TestWithParam<EncodeTimeErrorTestCase>;

TEST_P(ProtoUtilGoogleApiEncodeTimeErrorTest, Error) {
  const auto& tc = GetParam();
  EXPECT_THAT(util_time::EncodeGoogleApiProto(tc.t), Not(IsOk()))
      << "t=" << tc.t;

  google::protobuf::Timestamp proto;
  EXPECT_THAT(util_time::EncodeGoogleApiProto(tc.t, &proto), Not(IsOk()))
      << "t=" << tc.t;
}

INSTANTIATE_TEST_SUITE_P(
    ProtoUtilGoogleApi, ProtoUtilGoogleApiEncodeTimeErrorTest,
    testing::ValuesIn(std::vector<EncodeTimeErrorTestCase>{
        {"BelowMin", util_time::MakeGoogleApiTimeMin() - absl::Nanoseconds(1)},
        {"AboveMax", util_time::MakeGoogleApiTimeMax() + absl::Nanoseconds(1)},
        {"InfinitePast", absl::InfinitePast()},
        {"InfiniteFuture", absl::InfiniteFuture()},
    }),
    [](const testing::TestParamInfo<EncodeTimeErrorTestCase>& info) {
      return info.param.name;
    });

struct DecodeTimeErrorTestCase {
  const char* name;
  google::protobuf::Timestamp proto;
};

using ProtoUtilGoogleApiDecodeTimeErrorTest =
    testing::TestWithParam<DecodeTimeErrorTestCase>;

TEST_P(ProtoUtilGoogleApiDecodeTimeErrorTest, Error) {
  const auto& tc = GetParam();
  EXPECT_THAT(util_time::DecodeGoogleApiProto(tc.proto), Not(IsOk()))
      << "proto=" << tc.proto.DebugString();
}

INSTANTIATE_TEST_SUITE_P(
    ProtoUtilGoogleApi, ProtoUtilGoogleApiDecodeTimeErrorTest,
    testing::ValuesIn(std::vector<DecodeTimeErrorTestCase>{
        {"PosSecNegNsec", MakeGoogleApiTimestamp(1, -1)},
        {"NsecTooLarge", MakeGoogleApiTimestamp(1, 999999999 + 1)},
        {"BelowMinSec",
         MakeGoogleApiTimestamp(
             absl::ToUnixSeconds(util_time::MakeGoogleApiTimeMin() -
                                 absl::Seconds(1)),
             0)},
        {"AboveMaxSec",
         MakeGoogleApiTimestamp(
             absl::ToUnixSeconds(util_time::MakeGoogleApiTimeMax() +
                                 absl::Seconds(1)),
             0)},
        {"Int64Min",
         MakeGoogleApiTimestamp(std::numeric_limits<int64_t>::min(), 0)},
        {"Int64Max",
         MakeGoogleApiTimestamp(std::numeric_limits<int64_t>::max(), 0)},
        {"Int32MinNsec",
         MakeGoogleApiTimestamp(0, std::numeric_limits<int32_t>::min())},
        {"Int32MaxNsec",
         MakeGoogleApiTimestamp(0, std::numeric_limits<int32_t>::max())},
        {"MinSecMinNsec",
         MakeGoogleApiTimestamp(std::numeric_limits<int64_t>::min(),
                                std::numeric_limits<int32_t>::min())},
        {"MaxSecMaxNsec",
         MakeGoogleApiTimestamp(std::numeric_limits<int64_t>::max(),
                                std::numeric_limits<int32_t>::max())},
    }),
    [](const testing::TestParamInfo<DecodeTimeErrorTestCase>& info) {
      return info.param.name;
    });

TEST(ProtoUtilGoogleApi, TimestampProtoMax) {
  const auto proto_max = util_time::MakeGoogleApiTimestampProtoMax();
  const absl::Time time_max = util_time::MakeGoogleApiTimeMax();
  EXPECT_THAT(util_time::DecodeGoogleApiProto(proto_max),
              IsOkAndHolds(time_max));
}

TEST(ProtoUtilGoogleApi, TimeMaxIsOK) {
  const absl::Time time_max = util_time::MakeGoogleApiTimeMax();
  const absl::TimeZone utc = absl::UTCTimeZone();
  const absl::Time other_time_max =
      absl::FromCivil(absl::CivilSecond(9999, 12, 31, 23, 59, 59), utc) +
      absl::Nanoseconds(999999999);
  EXPECT_EQ(time_max, other_time_max);
}

TEST(ProtoUtilGoogleApi, TimeMaxIsMax) {
  const absl::Time time_max = util_time::MakeGoogleApiTimeMax();
  EXPECT_THAT(util_time::EncodeGoogleApiProto(time_max + absl::Nanoseconds(1)),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ProtoUtilGoogleApi, TimestampProtoMin) {
  const auto proto_min = util_time::MakeGoogleApiTimestampProtoMin();
  const absl::Time time_min = util_time::MakeGoogleApiTimeMin();
  EXPECT_THAT(util_time::DecodeGoogleApiProto(proto_min),
              IsOkAndHolds(time_min));
}

TEST(ProtoUtilGoogleApi, TimeMinIsOK) {
  const absl::Time time_min = util_time::MakeGoogleApiTimeMin();
  const absl::TimeZone utc = absl::UTCTimeZone();
  const absl::Time other_time_min =
      absl::FromCivil(absl::CivilSecond(1, 1, 1, 0, 0, 0), utc);
  EXPECT_EQ(time_min, other_time_min);
}

TEST(ProtoUtilGoogleApi, TimeMinIsMin) {
  const absl::Time time_min = util_time::MakeGoogleApiTimeMin();
  EXPECT_THAT(util_time::EncodeGoogleApiProto(time_min - absl::Nanoseconds(1)),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
