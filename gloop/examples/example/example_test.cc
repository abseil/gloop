// Apache 2.0 Notice: 2026

#include "example/example.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gloop/util/task/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace gloop {
namespace examples {
namespace {

using util::CanonicalStatusIs;

TEST(ExampleTest, GetExampleMessage) {
  EXPECT_EQ(GetExampleMessage(),
            "Hello from Gloop Example! Example code: 0, Example "
            "status.canonical_code: 1, util::StatusToString(status): OK, "
            "net_base::IPAddress::Loopback6(): ::1, MathUtil::kPi: 3.141593, "
            "atoi32(): 64");
}

TEST(ExampleTest, StatusMatchers) {
  EXPECT_THAT(absl::OkStatus(), CanonicalStatusIs(absl::StatusCode::kOk));
  ABSL_ASSERT_OK_AND_ASSIGN(int value, absl::StatusOr<int>(42));
  EXPECT_EQ(value, 42);
}

}  // namespace
}  // namespace examples
}  // namespace gloop
