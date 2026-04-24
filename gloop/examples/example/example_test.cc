#include "example/example.h"

#include "gtest/gtest.h"

namespace gloop {
namespace examples {
namespace {

TEST(ExampleTest, GetExampleMessage) {
  EXPECT_EQ(GetExampleMessage(),
            "Hello from Gloop Example! Example code: 0, Example "
            "status.canonical_code: 1, util::StatusToString(status): OK, "
            "net_base::IPAddress::Loopback6(): ::1, MathUtil::kPi: 3.141593");
}

}  // namespace
}  // namespace examples
}  // namespace gloop
