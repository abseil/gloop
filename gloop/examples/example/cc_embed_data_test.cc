#include "absl/strings/string_view.h"
#include "example/embedded_test_data.h"
#include "gtest/gtest.h"

namespace gloop::examples {
namespace {

TEST(CcEmbedDataTest, EmbeddedDataMatches) {
  const FileToc* toc = embedded_test_data_create();
  ASSERT_NE(toc, nullptr);

  // The TOC is null-terminated.
  ASSERT_NE(toc[0].name, nullptr);
  EXPECT_EQ(absl::string_view(toc[0].name), "test_data.txt");
  EXPECT_EQ(absl::string_view(toc[0].data, toc[0].size),
            "Hello, this is a test data file for cc_embed_data.\n");

  // Check that the next entry is null.
  EXPECT_EQ(toc[1].name, nullptr);
  EXPECT_EQ(toc[1].data, nullptr);
  EXPECT_EQ(toc[1].size, 0);
}

}  // namespace
}  // namespace gloop::examples
