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

// Provides wrapper functions for POSIX syscalls, which will automatically retry
// the actual syscalls in cases of EINTR (interrupts), when appropriate.
//
// Note that some syscalls are not safe to retry, and the wrappers provided here
// for those just call the real syscall without any retry logic. For more info,
// see https://lwn.net/Articles/576478/ and <link>.
//
// In general, any syscall could block (including those syscalls which are not
// automatically retried). However, the wrapper functions are fiber aware and
// will not block a fiber tree from doing work while waiting for the kernel to
// respond.
//
// For further documentation, please see the man pages for the original
// syscalls, or please see POSIX documentation such as
// https://pubs.opengroup.org/onlinepubs/9699919799/functions/contents.html
//
// WARNING: You should not wrap these syscalls with TEMP_FAILURE_RETRY(), as
// syscalls which are safe to retry are already being automatically retried
// here. Also, TEMP_FAILURE_RETRY() is a platform-specific macro and is not
// available everywhere.

#ifndef THIRD_PARTY_GLOOP_UTIL_OS_CORE_SYSCALLS_H_
#define THIRD_PARTY_GLOOP_UTIL_OS_CORE_SYSCALLS_H_

#include <dirent.h>
#include <poll.h>
#include <sys/mount.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include <cstddef>
#include <cstdint>
#if !defined(__APPLE__)
#include <sys/statfs.h>
#endif
#include <fcntl.h>
#include <sys/statvfs.h>
#include <sys/types.h>

#include <string>

#include "absl/strings/string_view.h"

// The statvfs syscall doesn't exist for older Android APIs or emscripten.
#if !(defined(__ANDROID_API__) && __ANDROID_API__ < 19) && \
    !defined(__EMSCRIPTEN__)
#define UTIL_OS_CORE_HAVE_STATVFS 1
#else
#undef UTIL_OS_CORE_HAVE_STATVFS
#endif

// Check if the funtimes syscall is actually available on this platform.
#if defined(_DEFAULT_SOURCE) || defined(_BSD_SOURCE) || defined(__APPLE__)
#define UTIL_OS_CORE_HAVE_FUTIMES 1
#else
#undef UTIL_OS_CORE_HAVE_FUTIMES
#endif

// The accept4 syscall was added to the Android APIs in version 21.
#if defined(_GNU_SOURCE) && (!defined(__ANDROID_API__) || __ANDROID_API__ >= 21)
#define UTIL_OS_CORE_HAVE_ACCEPT4
#else
#undef UTIL_OS_CORE_HAVE_ACCEPT4
#endif

// The ppoll syscall does not exist in the Android APIs as of version 21.
#if defined(_GNU_SOURCE) && !defined(__ANDROID_API__)
#define UTIL_OS_CORE_HAVE_PPOLL
#else
#undef UTIL_OS_CORE_HAVE_PPOLL
#endif

// The posix_fallocate syscall was added to the Android APIs in version 21.
// glibc hides posix_fallocate behind a feature test macro
#if (defined(__ANDROID_API__) && __ANDROID_API__ >= 21) || \
    (defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L)
#define UTIL_OS_CORE_HAVE_POSIX_FALLOCATE
#else
#undef UTIL_OS_CORE_HAVE_POSIX_FALLOCATE
#endif

#ifndef __Fuchsia__
#define UTIL_OS_CORE_HAVE_XATTR
#else
#undef UTIL_OS_CORE_HAVE_XATTR
#endif

namespace util_os_core {

int chmod(const char* path, mode_t mode);

int fchmod(int fd, mode_t mode);

int fchmodat(int dirfd, const char* pathname, mode_t mode, int flags);

int close(int fd);

int closedir(DIR* dirp);

int connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen);

int dup(int oldfd);

int fdatasync(int fd);

int flock(int fd, int operation);

int fstat(int fd, struct ::stat* buf);

int fstatat(int fd, const char* path, struct ::stat* buf, int flags);

int lstat(const char* path, struct ::stat* buf);

int stat(const char* pathname, struct ::stat* statbuf);

// Returns a reference to the stat's timespec atime/mtime/ctime field.
// These fields are not consistently named between operating systems.
struct timespec& stat_atime(struct ::stat* statbuf);
const struct timespec& stat_atime(const struct ::stat* statbuf);
struct timespec& stat_ctime(struct ::stat* statbuf);
const struct timespec& stat_ctime(const struct ::stat* statbuf);
struct timespec& stat_mtime(struct ::stat* statbuf);
const struct timespec& stat_mtime(const struct ::stat* statbuf);

int fsync(int fd);

int ftruncate(int fd, off_t length);

#if UTIL_OS_CORE_HAVE_FUTIMES
int futimes(int fd, const struct ::timeval tv[2]);
#endif  // UTIL_OS_CORE_HAVE_FUTIMES

int utimes(const char* filename, const struct ::timeval times[2]);

int utimensat(int dirfd, const char* pathname, const struct ::timespec times[2],
              int flags);

#ifdef UTIL_OS_CORE_HAVE_XATTR
ssize_t getxattr(const char* path, const char* name, void* value, size_t size);

int setxattr(const char* path, const char* name, absl::string_view value,
             int flags);
#endif

int link(const char* oldpath, const char* newpath);

int linkat(int olddirfd, const char* oldpath,  //
           int newdirfd, const char* newpath, int flags);

off_t lseek(int fd, off_t offset, int whence);

char* getcwd(char* buf, std::size_t size);

int mkdir(const char* pathname, mode_t mode);

int mkdirat(int dirfd, const char* pathname, mode_t mode);

char* mkdtemp(char* directory_template);
inline char* mkdtemp(std::string* directory_template) {
  return util_os_core::mkdtemp(&(*directory_template)[0]);
}

int mkstemp(char* filename_template);
inline int mkstemp(std::string* filename_template) {
  return util_os_core::mkstemp(&(*filename_template)[0]);
}

int msync(void* addr, size_t length, int flags);

int munmap(void* addr, size_t len);

int open(const char* pathname, int flags);

int open(const char* pathname, int flags, mode_t mode);

int openat(int dirfd, const char* pathname, int flags, mode_t mode = 0);

::DIR* opendir(const char* name);

::DIR* fdopendir(int fd);

int poll(struct pollfd* fds, nfds_t nfds, int timeout);

int select(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds,
           struct ::timeval* timeout);

// Note: if offset is negative, returns -1 and sets errno = EINVAL, even if the
// platform-native pread() does not.
ssize_t pread(int fd, void* buf, size_t count, off_t offset);

ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset);

struct ::dirent* readdir(::DIR* dirp);

ssize_t readlink(const char* pathname, char* buf, size_t bufsiz);

int readlinkat(int dirfd, const char* pathname, char* buf, size_t bufsiz);

int rename(const char* oldpath, const char* newpath);

int renameat(int olddirfd, const char* oldpath,  //
             int newdirfd, const char* newpath);

ssize_t read(int fd, void* buf, size_t count);

ssize_t readv(int fd, const struct ::iovec* iovec, int count);

int rmdir(const char* path);

int statfs(const char* path, struct ::statfs* buf);

#if UTIL_OS_CORE_HAVE_STATVFS

// Note that the semantics of struct statvfs - in particular, the meaning of
// f_bsize and f_frsize - is operating system dependent; see fsblocksize().
int statvfs(const char* path, struct ::statvfs* buf);

// Obtain the filesystem block size (i.e. the units of f_blocks, f_bfree, and
// f_bavail) in a portable way.
uint64_t fsblocksize(const struct ::statvfs& buf);

#endif  // UTIL_OS_CORE_HAVE_STATVFS

int symlink(const char* target, const char* linkpath);

int symlinkat(const char* oldpath, int newdirfd, const char* newpath);

int truncate(const char* path, off_t length);

int unlink(const char* pathname);

int unlinkat(int dirfd, const char* pathname, int flags);

pid_t waitpid(::pid_t pid, int* status, int options);

ssize_t write(int fd, absl::string_view buf);

ssize_t writev(int fd, const struct ::iovec* iovec, int count);

#ifdef UTIL_OS_CORE_HAVE_ACCEPT4
int accept4(int sockfd, struct sockaddr* addr, socklen_t* addrlen, int flags);
#endif  // UTIL_OS_CORE_HAVE_ACCEPT4

#ifdef UTIL_OS_CORE_HAVE_PPOLL
int ppoll(struct pollfd* fds, nfds_t nfds, const struct timespec* timeout_p,
          const sigset_t* sigmask);
#endif  // UTIL_OS_CORE_HAVE_PPOLL

int mkfifo(const char* pathname, mode_t mode);

#ifdef UTIL_OS_CORE_HAVE_POSIX_FALLOCATE
int posix_fallocate(int fd, off_t offset, off_t len);
#endif  // UTIL_OS_CORE_HAVE_POSIX_FALLOCATE

ssize_t send(int sockfd, const void* buf, size_t len, int flags);

ssize_t sendto(int sockfd, const void* buf, size_t len, int flags,
               const struct sockaddr* dest_addr, socklen_t addrlen);

ssize_t sendmsg(int sockfd, const struct msghdr* msg, int flags);

int socket(int domain, int type, int protocol);

int accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen);

ssize_t recv(int s, void* buf, int64_t len, int recv_flags);

ssize_t recvmsg(int s, struct msghdr* msg, int recv_flags);

int bind(int acceptfd, const sockaddr* address, size_t len);

int listen(int fd, int n);

int shutdown(int fd, int how);

}  // namespace util_os_core

#endif  // THIRD_PARTY_GLOOP_UTIL_OS_CORE_SYSCALLS_H_
