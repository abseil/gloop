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

#include "gloop/util/os/core/syscalls.h"

#include <fcntl.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/gloop_test.h"
#include "gloop/util/status/errno_mapping.h"

namespace {

static constexpr bool IsWasm() {
// Several syscalls link, but don't work conformantly.
#ifdef __wasm__
  return true;
#else
  return false;
#endif
}

TEST(SyscallsTest, FdatasyncFunctionExistsAndSucceeds) {
  std::string test_file_path = testing::TempDir() + "test_fdatasync_file_path";
  int fd = util_os_core::open(test_file_path.c_str(), O_RDWR | O_CREAT, 0666);
  ASSERT_NE(fd, -1);
  EXPECT_EQ(util_os_core::fdatasync(fd), 0);
  EXPECT_EQ(util_os_core::close(fd), 0);
}

TEST(SyscallsTest, FsyncFunctionExistsAndSucceeds) {
  std::string test_file_path = testing::TempDir() + "test_fsync_file_path";
  int fd = util_os_core::open(test_file_path.c_str(), O_RDWR | O_CREAT, 0666);
  ASSERT_NE(fd, -1);
  EXPECT_EQ(util_os_core::fsync(fd), 0);
  EXPECT_EQ(util_os_core::close(fd), 0);
}

TEST(SyscallsTest, InlineMkstempWorks) {
  std::string test_file_template =
      testing::TempDir() + "test_file_templateXXXXXX";
  int fd = util_os_core::mkstemp(&test_file_template);
  EXPECT_NE(fd, -1);
  EXPECT_EQ(util_os_core::close(fd), 0);
}

// Find filesystem size in bytes (accurate up to 1KB) by calling `df`.
static absl::StatusOr<int64_t> FsSizeFromDf(absl::string_view path) {
  // `df -k $path` (the -k option is portable, unlike e.g. -B or --block-size)
  // outputs something like the following:
  // Filesystem     1K-blocks   Used Available Use% Mounted on
  // /dev/foo         483946  143327    315634  32% /foobar
  // We want the second field in the second line.
  std::string command =
      absl::StrCat("df -k '", path, "' | tail -n 1 | awk '{ print $2 }'");
  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    return util::ErrnoToCanonicalStatus(errno, "Failed to run `df -k`");
  }
  char buffer[128];
  if (fgets(buffer, sizeof(buffer), pipe) == nullptr) {
    pclose(pipe);
    return util::ErrnoToCanonicalStatus(errno, "Failed to read `df -k` output");
  }
  pclose(pipe);
  int64_t fs_size = 0;
  if (!absl::SimpleAtoi(buffer, &fs_size)) {
    return absl::InternalError(absl::StrCat(
        "Failed to parse filesystem size in `df -k` output: ", buffer));
  }
  return fs_size * 1024;
}

#if UTIL_OS_CORE_HAVE_STATVFS
TEST(SyscallsTest, StatvfsAndFsblocksizeWorks) {
  std::string test_path = testing::TempDir();
  struct statvfs st;
  EXPECT_EQ(util_os_core::statvfs(test_path.c_str(), &st), 0);
  EXPECT_GT(st.f_blocks, 0);
  EXPECT_GT(util_os_core::fsblocksize(st), 0);
  auto df_size = FsSizeFromDf(test_path);
  ASSERT_TRUE(df_size.ok());
  // FsSizeFromDf() is accurate up to 1KB.
  EXPECT_NEAR(st.f_blocks * util_os_core::fsblocksize(st), *df_size, 1024);
}
#endif  // UTIL_OS_CORE_HAVE_STATVFS

TEST(SyscallsTest, StatFields) {
  std::string test_file_path = testing::TempDir() + "stat_test";
  int fd = util_os_core::open(test_file_path.c_str(), O_RDWR | O_CREAT, 0666);
  ASSERT_NE(fd, -1);
  ASSERT_EQ(util_os_core::close(fd), 0);

  struct stat sb;
  ASSERT_EQ(util_os_core::stat(test_file_path.c_str(), &sb), 0);
  EXPECT_GT(util_os_core::stat_atime(&sb).tv_sec, 0);
  EXPECT_GT(util_os_core::stat_ctime(&sb).tv_sec, 0);
  EXPECT_GT(util_os_core::stat_mtime(&sb).tv_sec, 0);

  const struct stat& const_sb = sb;
  EXPECT_GT(util_os_core::stat_atime(&const_sb).tv_sec, 0);
  EXPECT_GT(util_os_core::stat_ctime(&const_sb).tv_sec, 0);
  EXPECT_GT(util_os_core::stat_mtime(&const_sb).tv_sec, 0);
}

// Test fixture for the `openat()` family of syscalls.
//
// These tests are not intended to be exhaustive tests of POSIX APIs. Rather,
// their objective is to ensure that each argument is passed on to the
// underlying API correctly. Example: checking that `flags` and `mode` arguments
// aren't swapped for `openat()`. For these purposes, one positive and one
// negative case is sufficient for most APIs.
//
// Opportunities for improvement: Verify retry on `errno == EINTR`; verify
// `PotentiallyBlockingRegion`.
//
// The fixture creates a subdirectory for the tests to operate in:
//
//   + testing::TempDir()
//    `+ syscalls-XXXXXX/
//     |`- reg-file          # A regular file.
//     |`- broken-symlink    # A broken symlink.
//      `- subdir/           # An empty subdirectory.
//
// Directory file descriptors are opened for 'syscalls-XXXXXX/' and 'subdir/'.
class AtcallTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // syscalls-XXXXXX.
    dir_.name = ".";  // Name relative to itself.
    dir_.path = testing::TempDir() + "/syscalls-XXXXXX";
    ASSERT_TRUE(::mkdtemp(dir_.path.data()) != nullptr);

    dir_.fd = ::open(dir_.path.c_str(), O_RDONLY);
    ASSERT_NE(dir_.fd, -1);

    // file.
    reg_file_.name = "reg-file";
    reg_file_.path = dir_.path + "/" + reg_file_.name;
    int filefd = ::open(reg_file_.path.c_str(), O_WRONLY | O_CREAT, 0666);
    ASSERT_NE(filefd, -1);
    ASSERT_EQ(close(filefd), 0);

    // symlink.
    broken_symlink_.name = "broken-symlink";
    broken_symlink_.path = dir_.path + "/" + broken_symlink_.name;
    ASSERT_EQ(::symlink("bad-name", broken_symlink_.path.c_str()), 0);

    // subdir.
    subdir_.name = "subdir";
    subdir_.path = dir_.path + "/" + subdir_.name;
    ASSERT_EQ(::mkdir(subdir_.path.c_str(), 0777), 0);
    subdir_.fd = ::open(subdir_.path.c_str(), O_RDONLY);
    ASSERT_NE(subdir_.fd, -1);
  }

  struct File {
    std::string path;  // Absolute.
    std::string name;  // Relative to `dir_`.
    int fd = -1;       // For `dir_` and `subdir_` only.

    ~File() {
      if (fd != -1) (void)::close(fd);
    }
  };
  File dir_, reg_file_, broken_symlink_, subdir_;
};

TEST_F(AtcallTest, Fchmodat_FailsForNonexistentPath) {
  int res = util_os_core::fchmodat(dir_.fd, "bad-name", 0644, 0);
  int err = errno;
  EXPECT_EQ(res, -1);
  EXPECT_EQ(err, ENOENT);
}

TEST_F(AtcallTest, Fchmodat_ChangesFileModes) {
  struct ::stat st;
  ASSERT_EQ(stat(reg_file_.path.c_str(), &st), 0);
  ASSERT_NE(st.st_mode & 0777, 0400);

  int res = util_os_core::fchmodat(dir_.fd, reg_file_.name.c_str(), 0400, 0);
  EXPECT_EQ(res, 0);

  ASSERT_EQ(stat(reg_file_.path.c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0400);
}

TEST_F(AtcallTest, Fstatat_FailsForNonexistentPath) {
  struct ::stat st;
  int res = util_os_core::fstatat(dir_.fd, "bad-name", &st, 0);
  int err = errno;
  EXPECT_EQ(res, -1);
  EXPECT_EQ(err, ENOENT);
}

TEST_F(AtcallTest, Fstatat_ReturnsFileModeOfSymlink_WhenNotFollowingSymlinks) {
  struct ::stat st;
  ASSERT_EQ(util_os_core::fstatat(dir_.fd, broken_symlink_.name.c_str(), &st,
                                  AT_SYMLINK_NOFOLLOW),
            0);
  ASSERT_TRUE(S_ISLNK(st.st_mode));
}

TEST_F(AtcallTest, Utimensat_FailsForNonexistentPath) {
  struct ::timespec times[2];
  times[0].tv_sec = 0;  // st_atim
  times[0].tv_nsec = UTIME_OMIT;
  times[1].tv_sec = 0;  // st_mtim
  times[1].tv_nsec = UTIME_NOW;
  if (IsWasm()) GTEST_SKIP() << "utimensat() throws on WASM";
  int res = util_os_core::utimensat(dir_.fd, "bad-name", times, 0);
  int err = errno;
  EXPECT_EQ(res, -1);
  EXPECT_EQ(err, ENOENT);
}

TEST_F(AtcallTest, Utimensat_ChangesModificationTime) {
  constexpr int64_t kTimeSec = 1704832061;
  constexpr int32_t kTimeNsec = 42;

  struct ::stat st_before;
  ASSERT_EQ(::lstat(broken_symlink_.path.c_str(), &st_before), 0);
  ASSERT_FALSE(util_os_core::stat_mtime(&st_before).tv_sec == kTimeSec &&
               util_os_core::stat_mtime(&st_before).tv_nsec == kTimeNsec);

  struct ::timespec times[2];
  times[0].tv_sec = 0;  // st_atim
  times[0].tv_nsec = UTIME_OMIT;
  times[1].tv_sec = kTimeSec;  // st_mtim
  times[1].tv_nsec = kTimeNsec;

  if (IsWasm()) GTEST_SKIP() << "utimensat() throws on WASM";

  EXPECT_EQ(util_os_core::utimensat(dir_.fd, broken_symlink_.name.c_str(),
                                    times, AT_SYMLINK_NOFOLLOW),
            0);

  struct ::stat st_after;
  ASSERT_EQ(::lstat(broken_symlink_.path.c_str(), &st_after), 0);
  ASSERT_EQ(util_os_core::stat_mtime(&st_after).tv_sec, kTimeSec);
  ASSERT_EQ(util_os_core::stat_mtime(&st_after).tv_nsec, kTimeNsec);
}

TEST_F(AtcallTest, Linkat_FailsForBrokenSymlink_WhenFollowingSymlinks) {
  int res = util_os_core::linkat(dir_.fd, broken_symlink_.name.c_str(),  //
                                 subdir_.fd, "hardlink", AT_SYMLINK_FOLLOW);
  int err = errno;
  EXPECT_EQ(res, -1);
  if (IsWasm()) GTEST_SKIP() << "hard links not supported on WASM";
  EXPECT_EQ(err, ENOENT);
}

TEST_F(AtcallTest, Linkat_Links) {
  if (IsWasm()) GTEST_SKIP() << "hard links not supported on WASM";
  EXPECT_EQ(util_os_core::linkat(dir_.fd, reg_file_.name.c_str(),  //
                                 subdir_.fd, "hardlink", 0),
            0);

  struct ::stat st_old, st_new;
  ASSERT_EQ(::lstat(reg_file_.path.c_str(), &st_old), 0);
  ASSERT_EQ(::lstat((subdir_.path + "/hardlink").c_str(), &st_new), 0);
  EXPECT_EQ(st_old.st_dev, st_new.st_dev);
  EXPECT_EQ(st_old.st_ino, st_new.st_ino);
}

TEST_F(AtcallTest, Mkdirat_FailsToCreateSubdirectoryOfRegularFile) {
  int res = util_os_core::mkdirat(dir_.fd,
                                  (reg_file_.name + "/bad-path").c_str(), 0777);
  int err = errno;
  EXPECT_EQ(res, -1);
  EXPECT_EQ(err, ENOTDIR);
}

TEST_F(AtcallTest, Mkdirat_MakesDirectoryWithSpecifiedMode) {
  EXPECT_EQ(util_os_core::mkdirat(dir_.fd, "subdir2", 0300), 0);

  struct ::stat st;
  ASSERT_EQ(::stat((dir_.path + "/subdir2").c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0300);
}

TEST_F(AtcallTest, Openat_FailsForPathsThatAlreadyExist_WhenCreatingExclusive) {
  int res = util_os_core::openat(dir_.fd, reg_file_.name.c_str(),
                                 O_CREAT | O_WRONLY | O_EXCL, 0644);
  int err = errno;
  EXPECT_EQ(res, -1);
  EXPECT_EQ(err, EEXIST);
}

TEST_F(AtcallTest, Openat_CreatesFilesWithSpecifiedMode) {
  int fd = util_os_core::openat(dir_.fd, "file2",  //
                                O_CREAT | O_WRONLY | O_EXCL, 0400);
  EXPECT_GT(fd, -1);
  ASSERT_EQ(::close(fd), 0);

  struct ::stat st;
  ASSERT_EQ(::stat((dir_.path + "/file2").c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0400);
}

TEST_F(AtcallTest, Fdopendir_FailsForDescriptorsThatAreNotDirectories) {
  int fd = ::open(reg_file_.path.c_str(), O_RDONLY);
  ASSERT_GT(fd, -1);

  DIR* d = util_os_core::fdopendir(fd);  // Takes ownership on success.
  int err = errno;

  EXPECT_EQ(d, nullptr);
  EXPECT_EQ(err, ENOTDIR);

  if (d != nullptr) {
    ASSERT_EQ(::closedir(d), 0);
  } else {
    ASSERT_EQ(::close(fd), 0);
  }
}

TEST_F(AtcallTest, Fdopendir_OpensHandlesFromOpenat) {
  int fd = ::openat(dir_.fd, subdir_.name.c_str(), O_RDONLY);
  ASSERT_GT(fd, -1);

  DIR* d = util_os_core::fdopendir(fd);  // Takes ownership on success.
  EXPECT_NE(d, nullptr);
  if (d != nullptr) EXPECT_NE(::readdir(d), nullptr);

  if (d != nullptr) {
    ASSERT_EQ(::closedir(d), 0);
  } else {
    ASSERT_EQ(::close(fd), 0);
  }
}

TEST_F(AtcallTest, Readlinkat_FailsIfPathIsADirectory) {
  char buf[100];
  ssize_t len =
      util_os_core::readlinkat(dir_.fd, subdir_.name.c_str(), buf, sizeof(buf));
  int err = errno;
  ASSERT_EQ(len, -1);
  ASSERT_EQ(err, EINVAL);
}

TEST_F(AtcallTest, Readlinkat_ReadsLinks) {
  char buf[100];  // Sufficient for our link.
  ssize_t len = util_os_core::readlinkat(dir_.fd, broken_symlink_.name.c_str(),
                                         buf, sizeof(buf));
  ASSERT_LT(0, len);
  ASSERT_LT(len, sizeof(buf));
  ASSERT_EQ(absl::string_view(buf, len),
            "bad-name" /* as arranged by `SetUp()` */);
}

TEST_F(AtcallTest, Renameat_WillNotMakeADirectoryASubdirectoryOfItself) {
  int res = util_os_core::renameat(AT_FDCWD, dir_.path.c_str(),  //
                                   subdir_.fd, "parent");
  int err = errno;
  EXPECT_EQ(res, -1);
  EXPECT_EQ(err, EINVAL);
}

TEST_F(AtcallTest, Renameat_RenamesFiles) {
  EXPECT_EQ(util_os_core::renameat(dir_.fd, reg_file_.name.c_str(),  //
                                   subdir_.fd, "renamed-file"),
            0);
  struct ::stat st;
  ASSERT_EQ(::stat((subdir_.path + "/renamed-file").c_str(), &st), 0);
}

TEST_F(AtcallTest, Symlinkat_FailsWhenPathIncludesDanglingSymlink) {
  if (IsWasm()) GTEST_SKIP() << "symlinkat() not supported on WASM";
  int res = util_os_core::symlinkat(
      "doomed-symlink-contents",  //
      dir_.fd, (broken_symlink_.name + "/impossible-path").c_str());
  int err = errno;
  EXPECT_EQ(res, -1);
  EXPECT_EQ(err, ENOENT);
}

TEST_F(AtcallTest, Symlinkat_CreatesSymlinks) {
  struct ::stat st_original, st_via_symlink;
  ASSERT_EQ(::stat(reg_file_.path.c_str(), &st_original), 0);

  if (IsWasm()) GTEST_SKIP() << "symlinkat() not supported on WASM";
  EXPECT_EQ(util_os_core::symlinkat(("../" + reg_file_.name).c_str(),
                                    subdir_.fd, "symlink"),
            0);
  ASSERT_EQ(::stat((subdir_.path + "/symlink").c_str(), &st_via_symlink), 0);
  EXPECT_EQ(st_original.st_dev, st_via_symlink.st_dev);
  EXPECT_EQ(st_original.st_ino, st_via_symlink.st_ino);
}

TEST_F(AtcallTest, Unlinkat_FailsToRemoveFile_WhenRemovingDirectory) {
  int res =
      util_os_core::unlinkat(dir_.fd, reg_file_.name.c_str(), AT_REMOVEDIR);
  int err = errno;
  EXPECT_EQ(res, -1);
  EXPECT_EQ(err, ENOTDIR);
}

TEST_F(AtcallTest, Unlinkat_RemovesFiles_WhenRemovingFiles) {
  EXPECT_EQ(util_os_core::unlinkat(dir_.fd, reg_file_.name.c_str(), 0), 0);

  struct ::stat st;
  int res = ::lstat(reg_file_.path.c_str(), &st);
  int err = errno;
  ASSERT_EQ(res, -1);
  ASSERT_EQ(err, ENOENT);
}

}  // namespace
