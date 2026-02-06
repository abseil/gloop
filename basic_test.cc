#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace {
namespace gloop {

// This is just some placeholder code to make sure that we can build and test in
// OSS with dependencies.
TEST(BasicTest, Add) {
  constexpr absl::string_view kFoo = "foo";
  EXPECT_EQ(kFoo, "foo");
}

}  // namespace gloop
}  // namespace
