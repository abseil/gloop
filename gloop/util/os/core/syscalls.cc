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

#include "gloop/util/os/core/syscalls.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if !defined(__APPLE__)
#include <sys/statfs.h>
#endif
#ifdef UTIL_OS_CORE_HAVE_XATTR
#include <sys/xattr.h>
#endif

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>

#include "absl/strings/string_view.h"
#include "gloop/base/scheduling/domain.h"

namespace util_os_core {

int chmod(const char* path, mode_t mode) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::chmod(path, mode);
  } while (status == -1 && errno == EINTR);
  return status;
}

int fchmod(int fd, mode_t mode) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::fchmod(fd, mode);
  } while (status == -1 && errno == EINTR);
  return status;
}

int fchmodat(int dirfd, const char* pathname, mode_t mode, int flags) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::fchmodat(dirfd, pathname, mode, flags);
  } while (status == -1 && errno == EINTR);
  return status;
}

// NOTE: The close() syscall is not safe to retry.
int close(int fd) {
  base::scheduling::PotentiallyBlockingRegion region;
  return ::close(fd);
}

// NOTE: The closedir() syscall is not safe to retry.
int closedir(DIR* dirp) {
  base::scheduling::PotentiallyBlockingRegion region;
  return ::closedir(dirp);
}

int connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::connect(sockfd, addr, addrlen);
  } while (status == -1 && errno == EINTR);
  return status;
}

int dup(int oldfd) {
  base::scheduling::PotentiallyBlockingRegion region;
  int fd;
  do {
    fd = ::dup(oldfd);
  } while (fd == -1 && errno == EINTR);
  return fd;
}

int fdatasync(int fd) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
#ifdef __APPLE__
    status = ::fcntl(fd, F_FULLFSYNC);
#else
    status = ::fdatasync(fd);
#endif
  } while (status == -1 && errno == EINTR);
  return status;
}

int flock(int fd, int operation) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::flock(fd, operation);
  } while (status == -1 && errno == EINTR);
  return status;
}

int fstat(int fd, struct ::stat* buf) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::fstat(fd, buf);
  } while (status == -1 && errno == EINTR);
  return status;
}

int fstatat(int fd, const char* path, struct ::stat* buf, int flags) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::fstatat(fd, path, buf, flags);
  } while (status == -1 && errno == EINTR);
  return status;
}

int lstat(const char* path, struct ::stat* buf) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::lstat(path, buf);
  } while (status == -1 && errno == EINTR);
  return status;
}

int stat(const char* pathname, struct ::stat* statbuf) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::stat(pathname, statbuf);
  } while (status == -1 && errno == EINTR);
  return status;
}

struct timespec& stat_atime(struct ::stat* statbuf) {
#if defined(__APPLE__)
  return statbuf->st_atimespec;
#else
  return statbuf->st_atim;
#endif
}

const struct timespec& stat_atime(const struct ::stat* statbuf) {
#if defined(__APPLE__)
  return statbuf->st_atimespec;
#else
  return statbuf->st_atim;
#endif
}

struct timespec& stat_ctime(struct ::stat* statbuf) {
#if defined(__APPLE__)
  return statbuf->st_ctimespec;
#else
  return statbuf->st_ctim;
#endif
}

const struct timespec& stat_ctime(const struct ::stat* statbuf) {
#if defined(__APPLE__)
  return statbuf->st_ctimespec;
#else
  return statbuf->st_ctim;
#endif
}

struct timespec& stat_mtime(struct ::stat* statbuf) {
#if defined(__APPLE__)
  return statbuf->st_mtimespec;
#else
  return statbuf->st_mtim;
#endif
}

const struct timespec& stat_mtime(const struct ::stat* statbuf) {
#if defined(__APPLE__)
  return statbuf->st_mtimespec;
#else
  return statbuf->st_mtim;
#endif
}

int fsync(int fd) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::fsync(fd);
  } while (status == -1 && errno == EINTR);
  return status;
}

int ftruncate(int fd, off_t length) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::ftruncate(fd, length);
  } while (status == -1 && errno == EINTR);
  return status;
}

#if UTIL_OS_CORE_HAVE_FUTIMES
int futimes(int fd, const struct ::timeval tv[2]) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::futimes(fd, tv);
  } while (status == -1 && errno == EINTR);
  return status;
}
#endif  // UTIL_OS_CORE_HAVE_FUTIMES

int utimes(const char* filename, const struct ::timeval times[2]) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::utimes(filename, times);
  } while (status == -1 && errno == EINTR);
  return status;
}

int utimensat(int dirfd, const char* pathname, const struct timespec times[2],
              int flags) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::utimensat(dirfd, pathname, times, flags);
  } while (status == -1 && errno == EINTR);
  return status;
}

#ifdef UTIL_OS_CORE_HAVE_XATTR
ssize_t getxattr(const char* path, const char* name, void* value, size_t size) {
  base::scheduling::PotentiallyBlockingRegion region;
  ssize_t xattr_bytes;
  do {
#ifdef __APPLE__
    xattr_bytes = ::getxattr(path, name, value, size,
                             /*position=*/0, /*options=*/0);
#else
    xattr_bytes = ::getxattr(path, name, value, size);
#endif
  } while (xattr_bytes == -1 && errno == EINTR);
  return xattr_bytes;
}

int setxattr(const char* path, const char* name, absl::string_view value,
             int flags) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
#ifdef __APPLE__
    status = ::setxattr(path, name, value.data(), value.size(),
                        /*position=*/0, /*options=*/flags);
#else
    status = ::setxattr(path, name, value.data(), value.size(), flags);
#endif
  } while (status == -1 && errno == EINTR);
  return status;
}
#endif

int link(const char* oldpath, const char* newpath) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::link(oldpath, newpath);
  } while (status == -1 && errno == EINTR);
  return status;
}

int linkat(int olddirfd, const char* oldpath, int newdirfd, const char* newpath,
           int flags) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::linkat(olddirfd, oldpath, newdirfd, newpath, flags);
  } while (status == -1 && errno == EINTR);
  return status;
}

// NOTE: The lseek() syscall does not trigger EINTR.
off_t lseek(int fd, off_t offset, int whence) {
  base::scheduling::PotentiallyBlockingRegion region;
  return ::lseek(fd, offset, whence);
}

char* getcwd(char* buf, std::size_t size) {
  base::scheduling::PotentiallyBlockingRegion region;
  return ::getcwd(buf, size);
}

int mkdir(const char* pathname, mode_t mode) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::mkdir(pathname, mode);
  } while (status == -1 && errno == EINTR);
  return status;
}

int mkdirat(int dirfd, const char* pathname, mode_t mode) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::mkdirat(dirfd, pathname, mode);
  } while (status == -1 && errno == EINTR);
  return status;
}

char* mkdtemp(char* directory_template) {
  base::scheduling::PotentiallyBlockingRegion region;
  char* path;
  do {
    path = ::mkdtemp(directory_template);
  } while (path == nullptr && errno == EINTR);
  return path;
}

int mkstemp(char* filename_template) {
  base::scheduling::PotentiallyBlockingRegion region;
  int fd;
  do {
    fd = ::mkstemp(filename_template);
  } while (fd == -1 && errno == EINTR);
  return fd;
}

int msync(void* addr, size_t length, int flags) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::msync(addr, length, flags);
  } while (status == -1 && errno == EINTR);
  return status;
}

// NOTE: The munmap() syscall is not safe to retry.
int munmap(void* addr, size_t len) {
  base::scheduling::PotentiallyBlockingRegion region;
  return ::munmap(addr, len);
}

int open(const char* pathname, int flags) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::open(pathname, flags);
  } while (status == -1 && errno == EINTR);
  return status;
}

int open(const char* pathname, int flags, mode_t mode) {
  base::scheduling::PotentiallyBlockingRegion region;
  int fd;
  do {
    fd = ::open(pathname, flags, mode);
  } while (fd == -1 && errno == EINTR);
  return fd;
}

int openat(int dirfd, const char* pathname, int flags, mode_t mode) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::openat(dirfd, pathname, flags, mode);
  } while (status == -1 && errno == EINTR);
  return status;
}

::DIR* opendir(const char* name) {
  base::scheduling::PotentiallyBlockingRegion region;
  ::DIR* dir;
  do {
    dir = ::opendir(name);
  } while (dir == nullptr && errno == EINTR);
  return dir;
}

::DIR* fdopendir(int fd) {
  base::scheduling::PotentiallyBlockingRegion region;
  ::DIR* dir;
  do {
    dir = ::fdopendir(fd);
  } while (dir == nullptr && errno == EINTR);
  return dir;
}

int poll(struct pollfd* fds, nfds_t nfds, int timeout) {
  base::scheduling::PotentiallyBlockingRegion region;
  int ready_cnt;
  do {
    ready_cnt = ::poll(fds, nfds, timeout);
  } while (ready_cnt == -1 && errno == EINTR);
  return ready_cnt;
}

int select(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds,
           struct ::timeval* timeout) {
  base::scheduling::PotentiallyBlockingRegion region;
  int ready_cnt;
  do {
    ready_cnt = ::select(nfds, readfds, writefds, exceptfds, timeout);
  } while (ready_cnt == -1 && errno == EINTR);
  return ready_cnt;
}

ssize_t pread(int fd, void* buf, size_t count, off_t offset) {
  base::scheduling::PotentiallyBlockingRegion region;
#if defined(__APPLE__)
  // Darwin's pread() returns 0 when reading 0 bytes at a negative offset, but
  // we want to emulate the Linux behavior instead.
  if (count == 0 && offset < 0) {
    errno = EINVAL;
    return -1;
  }
#endif  // __APPLE__
  ssize_t bytes_read;
  do {
    bytes_read = ::pread(fd, buf, count, offset);
  } while (bytes_read == -1 && errno == EINTR);
  return bytes_read;
}

ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset) {
  base::scheduling::PotentiallyBlockingRegion region;
  ssize_t bytes_written;
  do {
    bytes_written = ::pwrite(fd, buf, count, offset);
  } while (bytes_written == -1 && errno == EINTR);
  return bytes_written;
}

struct ::dirent* readdir(::DIR* dirp) {
  base::scheduling::PotentiallyBlockingRegion region;
  struct ::dirent* next_dir;
  do {
    errno = 0;
    next_dir = ::readdir(dirp);
  } while (next_dir == nullptr && errno == EINTR);
  return next_dir;
}

ssize_t readlink(const char* pathname, char* buf, size_t bufsiz) {
  base::scheduling::PotentiallyBlockingRegion region;
  ssize_t bytes_read;
  do {
    bytes_read = ::readlink(pathname, buf, bufsiz);
  } while (bytes_read == -1 && errno == EINTR);
  return bytes_read;
}

int readlinkat(int dirfd, const char* pathname, char* buf, size_t bufsiz) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::readlinkat(dirfd, pathname, buf, bufsiz);
  } while (status == -1 && errno == EINTR);
  return status;
}

int rename(const char* oldpath, const char* newpath) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::rename(oldpath, newpath);
  } while (status == -1 && errno == EINTR);
  return status;
}

int renameat(int olddirfd, const char* oldpath, int newdirfd,
             const char* newpath) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::renameat(olddirfd, oldpath, newdirfd, newpath);
  } while (status == -1 && errno == EINTR);
  return status;
}

ssize_t read(int fd, void* buf, size_t count) {
  base::scheduling::PotentiallyBlockingRegion region;
  ssize_t bytes_read;
  do {
    bytes_read = ::read(fd, buf, count);
  } while (bytes_read == -1 && errno == EINTR);
  return bytes_read;
}

ssize_t readv(int fd, const struct ::iovec* iovec, int count) {
  base::scheduling::PotentiallyBlockingRegion region;
  ssize_t bytes_read;
  do {
    bytes_read = ::readv(fd, iovec, count);
  } while (bytes_read == -1 && errno == EINTR);
  return bytes_read;
}

int rmdir(const char* path) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::rmdir(path);
  } while (status == -1 && errno == EINTR);
  return status;
}

int statfs(const char* path, struct ::statfs* buf) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::statfs(path, buf);
  } while (status == -1 && errno == EINTR);
  return status;
}

#if UTIL_OS_CORE_HAVE_STATVFS

int statvfs(const char* path, struct ::statvfs* buf) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::statvfs(path, buf);
  } while (status == -1 && errno == EINTR);
  return status;
}

uint64_t fsblocksize(const struct ::statvfs& buf) {
  // On Darwin, in struct statvfs, f_blocks and f_bfree are in units of
  // f_frsize, while f_bsize is the preferred length of i/o requests. By
  // contrast, Linux historically didn't use f_frsize in struct statvfs, and has
  // f_blocks and f_bfree in units of f_bsize; f_bsize and f_frsize are synonyms
  // on most Linux filesystems.
#if defined(__linux__) || defined(__Fuchsia__)
  return buf.f_bsize;
#elif defined(__APPLE__)
  return buf.f_frsize;
#else
#error "Don't know how to query struct statvfs block size on this platform."
#endif
}

#endif  // UTIL_OS_CORE_HAVE_STATVFS

int symlink(const char* target, const char* linkpath) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::symlink(target, linkpath);
  } while (status == -1 && errno == EINTR);
  return status;
}

int symlinkat(const char* oldpath, int newdirfd, const char* newpath) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::symlinkat(oldpath, newdirfd, newpath);
  } while (status == -1 && errno == EINTR);
  return status;
}

int truncate(const char* path, off_t length) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::truncate(path, length);
  } while (status == -1 && errno == EINTR);
  return status;
}

int unlink(const char* pathname) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::unlink(pathname);
  } while (status == -1 && errno == EINTR);
  return status;
}

int unlinkat(int dirfd, const char* pathname, int flags) {
  base::scheduling::PotentiallyBlockingRegion region;
  int status;
  do {
    status = ::unlinkat(dirfd, pathname, flags);
  } while (status == -1 && errno == EINTR);
  return status;
}

pid_t waitpid(::pid_t pid, int* status, int options) {
  base::scheduling::PotentiallyBlockingRegion region;
  pid_t child_pid;
  do {
    child_pid = ::waitpid(pid, status, options);
  } while (child_pid == -1 && errno == EINTR);
  return child_pid;
}

ssize_t write(int fd, absl::string_view buf) {
  base::scheduling::PotentiallyBlockingRegion region;
  ssize_t bytes_written;
  do {
    bytes_written = ::write(fd, buf.data(), buf.size());
  } while (bytes_written == -1 && errno == EINTR);
  return bytes_written;
}

ssize_t writev(int fd, const struct ::iovec* iovec, int count) {
  base::scheduling::PotentiallyBlockingRegion region;
  ssize_t bytes_written;
  do {
    bytes_written = ::writev(fd, iovec, count);
  } while (bytes_written == -1 && errno == EINTR);
  return bytes_written;
}

#ifdef UTIL_OS_CORE_HAVE_ACCEPT4
int accept4(int sockfd, struct sockaddr* addr, socklen_t* addrlen, int flags) {
  base::scheduling::PotentiallyBlockingRegion region;
  int fd;
  do {
    fd = ::accept4(sockfd, addr, addrlen, flags);
  } while (fd == -1 && errno == EINTR);
  return fd;
}
#endif  // UTIL_OS_CORE_HAVE_ACCEPT4

#ifdef UTIL_OS_CORE_HAVE_PPOLL
int ppoll(struct pollfd* fds, nfds_t nfds, const struct timespec* timeout_p,
          const sigset_t* sigmask) {
  base::scheduling::PotentiallyBlockingRegion region;
  int ret;
  do {
    ret = ::ppoll(fds, nfds, timeout_p, sigmask);
  } while (ret == -1 && errno == EINTR);
  return ret;
}
#endif  // UTIL_OS_CORE_HAVE_PPOLL

int mkfifo(const char* pathname, mode_t mode) {
  base::scheduling::PotentiallyBlockingRegion region;
  int ret;
  do {
    ret = ::mkfifo(pathname, mode);
  } while (ret == -1 && errno == EINTR);
  return ret;
}

#ifdef UTIL_OS_CORE_HAVE_POSIX_FALLOCATE
int posix_fallocate(int fd, off_t offset, off_t len) {
  base::scheduling::PotentiallyBlockingRegion region;
  int ret;
  do {
    ret = ::posix_fallocate(fd, offset, len);
  } while (ret == EINTR);
  return ret;
}
#endif  // UTIL_OS_CORE_HAVE_POSIX_FALLOCATE

ssize_t send(int sockfd, const void* buf, size_t len, int flags) {
  base::scheduling::PotentiallyBlockingRegion region;
  ssize_t bytes_written;
  do {
    bytes_written = ::send(sockfd, buf, len, flags);
  } while (bytes_written == -1 && errno == EINTR);
  return bytes_written;
}

ssize_t sendto(int sockfd, const void* buf, size_t len, int flags,
               const struct sockaddr* dest_addr, socklen_t addrlen) {
  base::scheduling::PotentiallyBlockingRegion region;
  ssize_t bytes_written;
  do {
    bytes_written = ::sendto(sockfd, buf, len, flags, dest_addr, addrlen);
  } while (bytes_written == -1 && errno == EINTR);
  return bytes_written;
}

ssize_t sendmsg(int sockfd, const struct msghdr* msg, int flags) {
  base::scheduling::PotentiallyBlockingRegion region;
  ssize_t bytes_written;
  do {
    bytes_written = ::sendmsg(sockfd, msg, flags);
  } while (bytes_written == -1 && errno == EINTR);
  return bytes_written;
}

int socket(int domain, int type, int protocol) {
  base::scheduling::PotentiallyBlockingRegion region;
  return ::socket(domain, type, protocol);
}

int accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
  base::scheduling::PotentiallyBlockingRegion region;
  int ret;
  do {
    ret = ::accept(sockfd, addr, addrlen);
  } while (ret == -1 && errno == EINTR);
  return ret;
}

ssize_t recv(int s, void* buf, int64_t len, int recv_flags) {
  base::scheduling::PotentiallyBlockingRegion region;
  ssize_t bytes_written;
  do {
    bytes_written = ::recv(s, buf, len, recv_flags);
  } while (bytes_written == -1 && errno == EINTR);
  return bytes_written;
}

ssize_t recvmsg(int s, struct msghdr* msg, int recv_flags) {
  base::scheduling::PotentiallyBlockingRegion region;
  ssize_t bytes_written;
  do {
    bytes_written = ::recvmsg(s, msg, recv_flags);
  } while (bytes_written == -1 && errno == EINTR);
  return bytes_written;
}

int bind(int acceptfd, const sockaddr* address, size_t len) {
  base::scheduling::PotentiallyBlockingRegion region;
  return ::bind(acceptfd, address, len);
}

int listen(int fd, int n) {
  base::scheduling::PotentiallyBlockingRegion region;
  return ::listen(fd, n);
}

int shutdown(int fd, int how) {
  base::scheduling::PotentiallyBlockingRegion region;
  return ::shutdown(fd, how);
}

}  // namespace util_os_core
