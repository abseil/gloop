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

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "gloop/gloop_test.h"
#include "google/protobuf/duration.pb.h"
#include "google/protobuf/timestamp.pb.h"

namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

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
  ABSL_ASSERT_OK(sor_proto);
  const auto& proto = sor_proto.value();
  EXPECT_EQ(proto.seconds(), expected_sec);
  EXPECT_EQ(proto.nanos(), expected_nsec);

  P out_proto;
  const auto status = util_time::EncodeGoogleApiProto(v, &out_proto);
  ABSL_ASSERT_OK(status);
  EXPECT_EQ(out_proto.seconds(), expected_sec);
  EXPECT_EQ(out_proto.nanos(), expected_nsec);
  EXPECT_EQ(proto.seconds(), out_proto.seconds());
  EXPECT_EQ(proto.nanos(), out_proto.nanos());

  // Complete the round-trip by decoding the proto back to a absl::Duration.
  const auto sor_duration = util_time::DecodeGoogleApiProto(proto);
  ABSL_ASSERT_OK(sor_duration);
  const auto& duration = sor_duration.value();
  EXPECT_EQ(duration, v);
}

TEST(ProtoUtilGoogleApi, RoundTripDuration) {
  // Shorthand to make the test cases readable.
  const auto& s = [](int64_t n) { return absl::Seconds(n); };
  const auto& ns = [](int64_t n) { return absl::Nanoseconds(n); };
  const struct {
    absl::Duration d;
    struct {
      int64_t sec;
      int32_t nsec;
    } expected;
  } kTestCases[] = {
      {s(0), {0, 0}},
      {s(123) + ns(456), {123, 456}},
      {ns(-5), {0, -5}},
      {s(-10) - ns(5), {-10, -5}},
      {s(-315576000000), {-315576000000, 0}},
      {s(315576000000), {315576000000, 0}},
      {util_time::MakeGoogleApiDurationMin(), {-315576000000, -999999999}},
      {util_time::MakeGoogleApiDurationMax(), {315576000000, 999999999}},
  };

  for (const auto& tc : kTestCases) {
    RoundTripGoogleApi<absl::Duration, google::protobuf::Duration>(
        tc.d, tc.expected.sec, tc.expected.nsec);
  }
}

TEST(ProtoUtilGoogleApi, DurationTruncTowardZero) {
  // Shorthand to make the test cases readable.
  const absl::Duration tick = absl::Nanoseconds(1) / 4;
  const auto& s = [](int64_t n) { return absl::Seconds(n); };
  const struct {
    absl::Duration d;
    struct {
      int64_t sec;
      int32_t nsec;
    } expected;
  } kTestCases[] = {
      {tick, {0, 0}},
      {-tick, {0, 0}},
      {s(2) + tick, {2, 0}},
      {s(2) - tick, {1, 999999999}},
      {s(1) + tick, {1, 0}},
      {s(1) - tick, {0, 999999999}},
      {s(-1) + tick, {0, -999999999}},
      {s(-1) - tick, {-1, 0}},
      {s(-2) + tick, {-1, -999999999}},
      {s(-2) - tick, {-2, 0}},
      {util_time::MakeGoogleApiDurationMin() - tick,
       {-315576000000, -999999999}},
      {util_time::MakeGoogleApiDurationMin() + tick,
       {-315576000000, -999999998}},
      {util_time::MakeGoogleApiDurationMax() - tick, {315576000000, 999999998}},
      {util_time::MakeGoogleApiDurationMax() + tick, {315576000000, 999999999}},
  };

  for (const auto& tc : kTestCases) {
    const auto sor = util_time::EncodeGoogleApiProto(tc.d);
    ABSL_ASSERT_OK(sor);
    const auto& proto = sor.value();
    EXPECT_EQ(proto.seconds(), tc.expected.sec) << "d=" << tc.d;
    EXPECT_EQ(proto.nanos(), tc.expected.nsec) << "d=" << tc.d;
  }
}

TEST(ProtoUtilGoogleApi, EncodeDurationError) {
  const absl::Duration kTestCases[] = {
      util_time::MakeGoogleApiDurationMin() - absl::Nanoseconds(1),  //
      util_time::MakeGoogleApiDurationMax() + absl::Nanoseconds(1),  //
      -absl::InfiniteDuration(),                                     //
      absl::InfiniteDuration()};                                     //
  for (const auto& d : kTestCases) {
    const auto sor = util_time::EncodeGoogleApiProto(d);
    EXPECT_FALSE(sor.ok()) << "d=" << d;

    google::protobuf::Duration proto;
    const auto status = util_time::EncodeGoogleApiProto(d, &proto);
    EXPECT_FALSE(status.ok()) << "d=" << d;
  }
}

TEST(ProtoUtilGoogleApi, DecodeDurationError) {
  const google::protobuf::Duration kTestCases[] = {
      MakeGoogleApiDuration(1, -1),                                   //
      MakeGoogleApiDuration(-1, 1),                                   //
      MakeGoogleApiDuration(0, 999999999 + 1),                        //
      MakeGoogleApiDuration(0, -999999999 - 1),                       //
      MakeGoogleApiDuration(-315576000000 - 1, 0),                    //
      MakeGoogleApiDuration(315576000000 + 1, 0),                     //
      MakeGoogleApiDuration(std::numeric_limits<int64_t>::min(), 0),  //
      MakeGoogleApiDuration(std::numeric_limits<int64_t>::max(), 0),  //
      MakeGoogleApiDuration(0, std::numeric_limits<int32_t>::min()),  //
      MakeGoogleApiDuration(0, std::numeric_limits<int32_t>::max()),  //
      MakeGoogleApiDuration(std::numeric_limits<int64_t>::min(),
                            std::numeric_limits<int32_t>::min()),  //
      MakeGoogleApiDuration(std::numeric_limits<int64_t>::max(),
                            std::numeric_limits<int32_t>::max()),  //
  };
  for (const auto& d : kTestCases) {
    const auto sor = util_time::DecodeGoogleApiProto(d);
    EXPECT_FALSE(sor.ok()) << "d=" << d.DebugString();
  }
}

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

TEST(ProtoUtilGoogleApi, RoundTripTime) {
  // Shorthand to make the test cases readable.
  const absl::Time epoch = absl::UnixEpoch();  // The protobuf epoch.
  const auto& s = [](int64_t n) { return absl::Seconds(n); };
  const auto& ns = [](int64_t n) { return absl::Nanoseconds(n); };
  const struct {
    absl::Time t;
    struct {
      int64_t sec;
      int32_t nsec;
    } expected;
  } kTestCases[] = {
      {epoch, {0, 0}},
      {epoch - ns(1), {-1, 999999999}},
      {epoch + ns(1), {0, 1}},
      {epoch + s(123) + ns(456), {123, 456}},
      {epoch - ns(5), {-1, 999999995}},
      {epoch - s(10) - ns(5), {-11, 999999995}},
      {util_time::MakeGoogleApiTimeMin(), {-62135596800, 0}},
      {util_time::MakeGoogleApiTimeMax(), {253402300799, 999999999}},
  };

  for (const auto& tc : kTestCases) {
    RoundTripGoogleApi<absl::Time, google::protobuf::Timestamp>(
        tc.t, tc.expected.sec, tc.expected.nsec);
  }
}

TEST(ProtoUtilGoogleApi, TimeTruncTowardInfPast) {
  const absl::Duration tick = absl::Nanoseconds(1) / 4;
  const absl::Time before_epoch = absl::FromUnixSeconds(-1234567890);
  const absl::Time epoch = absl::UnixEpoch();
  const absl::Time after_epoch = absl::FromUnixSeconds(1234567890);
  const struct {
    absl::Time t;
    struct {
      int64_t sec;
      int32_t nsec;
    } expected;
  } kTestCases[] = {
      {before_epoch + tick, {-1234567890, 0}},
      {before_epoch - tick, {-1234567890 - 1, 999999999}},
      {epoch + tick, {0, 0}},
      {epoch - tick, {-1, 999999999}},
      {after_epoch + tick, {1234567890, 0}},
      {after_epoch - tick, {1234567890 - 1, 999999999}},
      {util_time::MakeGoogleApiTimeMin() + tick, {-62135596800, 0}},
      {util_time::MakeGoogleApiTimeMax() - tick, {253402300799, 999999998}},
      {util_time::MakeGoogleApiTimeMax() + tick, {253402300799, 999999999}},
  };

  for (const auto& tc : kTestCases) {
    const auto sor = util_time::EncodeGoogleApiProto(tc.t);
    ABSL_ASSERT_OK(sor);
    const auto& proto = sor.value();
    EXPECT_EQ(proto.seconds(), tc.expected.sec) << "t=" << tc.t;
    EXPECT_EQ(proto.nanos(), tc.expected.nsec) << "t=" << tc.t;
  }
}

TEST(ProtoUtilGoogleApi, EncodeTimeError) {
  const absl::Time kTestCases[] = {
      util_time::MakeGoogleApiTimeMin() - absl::Nanoseconds(1),  //
      util_time::MakeGoogleApiTimeMax() + absl::Nanoseconds(1),  //
      absl::InfinitePast(),                                      //
      absl::InfiniteFuture(),                                    //
  };

  for (const auto& t : kTestCases) {
    const auto sor = util_time::EncodeGoogleApiProto(t);
    EXPECT_FALSE(sor.ok()) << "t=" << t;

    google::protobuf::Timestamp proto;
    const auto status = util_time::EncodeGoogleApiProto(t, &proto);
    EXPECT_FALSE(status.ok()) << "t=" << t;
  }
}

TEST(ProtoUtilGoogleApi, DecodeTimeError) {
  const google::protobuf::Timestamp kTestCases[] = {
      MakeGoogleApiTimestamp(1, -1),             //
      MakeGoogleApiTimestamp(1, 999999999 + 1),  //
      MakeGoogleApiTimestamp(
          absl::ToUnixSeconds(util_time::MakeGoogleApiTimeMin() -
                              absl::Seconds(1)),
          0),  //
      MakeGoogleApiTimestamp(
          absl::ToUnixSeconds(util_time::MakeGoogleApiTimeMax() +
                              absl::Seconds(1)),
          0),                                                          //
      MakeGoogleApiTimestamp(std::numeric_limits<int64_t>::min(), 0),  //
      MakeGoogleApiTimestamp(std::numeric_limits<int64_t>::max(), 0),  //
      MakeGoogleApiTimestamp(0, std::numeric_limits<int32_t>::min()),  //
      MakeGoogleApiTimestamp(0, std::numeric_limits<int32_t>::max()),  //
      MakeGoogleApiTimestamp(std::numeric_limits<int64_t>::min(),
                             std::numeric_limits<int32_t>::min()),  //
      MakeGoogleApiTimestamp(std::numeric_limits<int64_t>::max(),
                             std::numeric_limits<int32_t>::max()),  //
  };

  for (const auto& d : kTestCases) {
    const auto sor = util_time::DecodeGoogleApiProto(d);
    EXPECT_FALSE(sor.ok()) << "d=" << d.DebugString();
  }
}

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

namespace deprecated {

template <typename T>
absl::Duration MakeDuration(int64_t s, T n) {
  return absl::Seconds(s) + absl::Nanoseconds(n);
}

template <typename T>
absl::Time MakeUnixTime(int64_t s, T n) {
  return absl::FromUnixSeconds(s) + absl::Nanoseconds(n);
}

// Helper function that tests the ToProto() and FromProto() functions. The
// templated first argument may be either a absl::Duration or a absl::Time.
template <typename T>
void RoundTripDeprecated(T v, int64_t expected_sec, int32_t expected_nsec) {
  const auto proto = util_time::ToProto(v);
  EXPECT_EQ(expected_sec, proto.seconds()) << "Given: " << v;
  EXPECT_EQ(expected_nsec, proto.nanos()) << "Given: " << v;
  absl::Duration subsec;  // for checking if v is nanosecond aligned
  absl::IDivDuration(v - T(), absl::Seconds(1), &subsec);
  if (absl::Nanoseconds(subsec / absl::Nanoseconds(1)) == subsec) {
    const T v2 = util_time::FromProto(proto);
    EXPECT_EQ(v, v2);
  }
}

TEST(ProtoUtilDeprecated, Duration) {
  const struct {
    absl::Duration d;
    int64_t expected_sec;
    int32_t expected_nsec;
  } kTestCases[] = {
      {MakeDuration(0, 0), 0, 0},
      {MakeDuration(0, 0.5), 0, 0},
      {MakeDuration(123, 456), 123, 456},
      {MakeDuration(123, 456.5), 123, 456},
      {MakeDuration(std::numeric_limits<int64_t>::max(), 0),
       std::numeric_limits<int64_t>::max(), 0},
      {MakeDuration(std::numeric_limits<int64_t>::max(), 0.5),
       std::numeric_limits<int64_t>::max(), 0},
      {MakeDuration(std::numeric_limits<int64_t>::max(), 999999999),
       std::numeric_limits<int64_t>::max(), 999999999},
      {MakeDuration(std::numeric_limits<int64_t>::max(), 999999999.5),
       std::numeric_limits<int64_t>::max(), 999999999},
      {MakeDuration(0, -5), 0, -5},
      {MakeDuration(0, -5.5), 0, -5},
      {MakeDuration(-10, -5), -10, -5},
      {MakeDuration(-10, -5.5), -10, -5},
      {MakeDuration(std::numeric_limits<int64_t>::min(), 1.5),
       std::numeric_limits<int64_t>::min() + 1, -999999998},
      {MakeDuration(std::numeric_limits<int64_t>::min(), 1),
       std::numeric_limits<int64_t>::min() + 1, -999999999},
      {MakeDuration(std::numeric_limits<int64_t>::min(), 0.5),
       std::numeric_limits<int64_t>::min() + 1, -999999999},
      {MakeDuration(std::numeric_limits<int64_t>::min(), 0),
       std::numeric_limits<int64_t>::min(), 0},
  };

  for (const auto& tc : kTestCases) {
    RoundTripDeprecated(tc.d, tc.expected_sec, tc.expected_nsec);
  }
}

TEST(ProtoUtilDeprecated, Timestamp) {
  const struct {
    absl::Time t;
    int64_t expected_sec;
    int32_t expected_nsec;
  } kTestCases[] = {
      {MakeUnixTime(0, 0), 0, 0},
      {MakeUnixTime(0, 0.5), 0, 0},
      {MakeUnixTime(123, 456), 123, 456},
      {MakeUnixTime(123, 456.5), 123, 456},
      {MakeUnixTime(std::numeric_limits<int64_t>::max(), 0),
       std::numeric_limits<int64_t>::max(), 0},
      {MakeUnixTime(std::numeric_limits<int64_t>::max(), 0.5),
       std::numeric_limits<int64_t>::max(), 0},
      {MakeUnixTime(std::numeric_limits<int64_t>::max(), 999999999),
       std::numeric_limits<int64_t>::max(), 999999999},
      {MakeUnixTime(std::numeric_limits<int64_t>::max(), 999999999.5),
       std::numeric_limits<int64_t>::max(), 999999999},
      {MakeUnixTime(0, -5), -1, 999999995},
      {MakeUnixTime(0, -5.5), -1, 999999994},
      {MakeUnixTime(-10, -5), -11, 999999995},
      {MakeUnixTime(-10, -5.5), -11, 999999994},
      {MakeUnixTime(std::numeric_limits<int64_t>::min(), 1.5),
       std::numeric_limits<int64_t>::min(), 1},
      {MakeUnixTime(std::numeric_limits<int64_t>::min(), 1),
       std::numeric_limits<int64_t>::min(), 1},
      {MakeUnixTime(std::numeric_limits<int64_t>::min(), 0.5),
       std::numeric_limits<int64_t>::min(), 0},
      {MakeUnixTime(std::numeric_limits<int64_t>::min(), 0),
       std::numeric_limits<int64_t>::min(), 0},
  };

  for (const auto& tc : kTestCases) {
    RoundTripDeprecated(tc.t, tc.expected_sec, tc.expected_nsec);
  }
}

TEST(ProtoUtilDeprecated, Infinity) {
  const auto& dur_pos_inf = util_time::ToProto(absl::InfiniteDuration());
  EXPECT_EQ(std::numeric_limits<int64_t>::max(), dur_pos_inf.seconds());
  EXPECT_EQ(999999999, dur_pos_inf.nanos());

  const auto& dur_neg_inf = util_time::ToProto(-absl::InfiniteDuration());
  EXPECT_EQ(std::numeric_limits<int64_t>::min(), dur_neg_inf.seconds());
  EXPECT_EQ(-999999999, dur_neg_inf.nanos());

  const auto& time_future = util_time::ToProto(absl::InfiniteFuture());
  EXPECT_EQ(std::numeric_limits<int64_t>::max(), time_future.seconds());
  EXPECT_EQ(999999999, time_future.nanos());

  const auto& time_past = util_time::ToProto(absl::InfinitePast());
  EXPECT_EQ(std::numeric_limits<int64_t>::min(), time_past.seconds());
  EXPECT_EQ(0, time_past.nanos());
}

}  // namespace deprecated

}  // namespace
