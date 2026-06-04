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

#include "gloop/base/log_file_object.h"

#if GLOOP_INTERNAL_PROD_LOGGING

#include <fcntl.h>
#include <features.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/internal/strerror.h"
#include "absl/base/log_severity.h"
#include "absl/base/thread_annotations.h"
#include "absl/flags/flag.h"
#include "absl/flags/internal/program_name.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/internal/logging_directories.h"
#include "gloop/base/log_file_flags.h"
#include "gloop/base/port.h"
#include "gloop/base/user_name.h"

namespace base_logging {
namespace logging_internal {

LogFileObject::LogFileObject(absl::LogSeverity severity,
                             const char* base_filename, absl::TimeZone tz)
    : base_filename_selected_(base_filename != nullptr),
      base_filename_((base_filename != nullptr) ? base_filename : ""),
      symlink_basename_initialized_(false),
      filename_extension_(),
      permissions_(0),
      file_(nullptr),
      severity_(severity),
      bytes_since_flush_(0),
      file_length_(0),
      tz_(tz),
      next_flush_time_(absl::InfinitePast()),
      log_creation_time_(absl::InfinitePast()),
      disk_full_(false) {
  assert(severity == absl::NormalizeLogSeverity(severity));
}

LogFileObject::~LogFileObject() { CloseLogfile(); }

void LogFileObject::Write(bool force_flush, time_t timestamp,
                          const char* message, size_t message_len) {
  const absl::Time timestamp_as_time = absl::FromTimeT(timestamp);

  // We don't log if the base_name_ is "" (which means "don't write")
  if (base_filename_selected_ && base_filename_.empty()) {
    return;
  }

  // Attempt to roll over no more than once per second.  If we try rolling
  // over more often, the open() will return EEXIST because a logfile of
  // the same name was (presumably) created in the same second.
  if ((file_length_ >> 20) >= std::max(1, absl::GetFlag(FLAGS_max_log_size)) &&
      timestamp_as_time - log_creation_time_ > absl::Seconds(1)) {
    // Note that because we close the logfile before attempting to open a
    // new logfile, it's possible we'll begin dropping log messages at this
    // point. Because we've waited at least a second since the last attempt,
    // we know that failures are due to bad or full disks, not the simple
    // fact that a rollover occurred less than one second in the past in
    // the same directory.
    CloseLogfile();
    file_length_ = bytes_since_flush_ = 0;
  }

  // If there's no destination file, make one before outputting
  if (file_ == nullptr) {
    // The logfile's filename will have the date/time & pid in it.
    // POSIX says pid_t shall be a signed integer type [sys/types.h].
    const std::string time_pid_string = absl::StrCat(
        absl::FormatTime("%Y%m%d-%H%M%S.", timestamp_as_time, tz_), getpid());

    if (base_filename_selected_) {
      if (!CreateLogfile(time_pid_string)) {
        return;
      }
    } else {
      // If no base filename for logs of this severity has been set, use a
      // default base filename of
      // "<program name>.<hostname>.<user name>.log.<severity level>.".  So
      // logfiles will have names like
      // qserver.m32.root.log.INFO.19990817-150000.4354, where
      // 19990817 is a date (1999 August 17), 150000 is a time (15:00:00),
      // and 4354 is the pid of the logging process.  The date & time reflect
      // when the file was created for output.
      //
      // Where does the file get put?  Successively try the directories
      // returned by LoggingDirectories()
      struct utsname buf;
      if (0 != uname(&buf)) {
        *buf.nodename = '\0';  // ensure null termination on failure
      }

      std::string uidname = MyUserName();
      // We should not call CHECK() here because this function can be
      // called in the middle of logging. Simply use a name like invalid-user.
      if (uidname.empty()) uidname = "invalid-user";

      const std::string stripped_filename = absl::StrCat(
          absl::flags_internal::ShortProgramInvocationName(), ".", buf.nodename,
          ".", uidname, ".log.", absl::LogSeverityName(severity_), ".");
      // We're going to (potentially) try to put logs in several different dirs
      const std::vector<std::string> log_dirs = LoggingDirectories();

      // Go through the list of dirs, and try to create the log file in each
      // until we succeed or run out of options
      bool success = false;
      for (const std::string& dir : log_dirs) {
        // No point in trying to create a logfile if the directory doesn't even
        // exist.
        if (access(dir.c_str(), F_OK) != 0) {
          continue;
        }
        bool needs_slash = dir.empty() || dir.back() != '/';
        base_filename_ =
            absl::StrCat(dir, needs_slash ? "/" : "", stripped_filename);
        if (CreateLogfile(time_pid_string)) {
          success = true;
          break;
        }
      }
      // If we never succeeded, we have to give up
      if (!success) {
        absl::FPrintF(stderr, "Could not open any log file.\n");
        // This prevents attempting to open a new logfile more than once per
        // second even when the open is failing, to avoid spamming STDERR and
        // making an excessive number of system calls when recent failures
        // have occurred.
        log_creation_time_ = timestamp_as_time;
        return;
      }
    }

    // Write a header message into the log file
    const std::string file_header_string =
        TestOnlyFileHeaderString(timestamp_as_time);
    fwrite(file_header_string.data(), 1, file_header_string.size(), file_);
    file_length_ += file_header_string.size();
    bytes_since_flush_ += file_header_string.size();
    log_creation_time_ = timestamp_as_time;
  }

  // Write to LOG file:
  if (disk_full_ && absl::Now() < next_flush_time_) return;
  // fwrite() doesn't return an error when the disk is full, for messages that
  // are less than 4096 bytes. When the disk is full, it returns the message
  // length for messages that are less than 4096 bytes. fwrite() returns 4096
  // for message lengths that are greater than 4096, thereby indicating an
  // error.
  errno = 0;
  fwrite(message, 1, message_len, file_);
  if (absl::GetFlag(FLAGS_stop_logging_if_full_disk) && errno == ENOSPC) {
    // Disk full; stop writing to disk until the next flush time.
    disk_full_ = true;
    return;
  } else {
    disk_full_ = false;
    file_length_ += message_len;
    bytes_since_flush_ += message_len;
  }

  // See important msgs *now*.  Also, flush logs at least every 10^6 chars,
  // or if there are buffered log messages more than FLAGS_logbufsecs old.
  // As noted in the flag doc string, this does *not* imply a flush
  // every FLAGS_logbufsecs seconds.
  if (force_flush || (bytes_since_flush_ >= 1000000) ||
      (absl::Now() >= next_flush_time_)) {
#ifdef __linux__
    uint32_t flushed_bytes = bytes_since_flush_;
#endif
    FlushUnlocked();
#ifdef __linux__
    static const int64_t kPageSize = getpagesize();

    // On Linux, calling fadvise(DONTNEED) will flush modified in-core data
    // to block device.  This may cause many seeks, so we only call it if a
    // flush crosses 256 KB boundary.  The fadvise(DONTNEED) call itself
    // doesn't wait for all writeback to be completed.
    const uint32_t kFadviseInterval = 256 * 1024;
    if (file_length_ / kFadviseInterval >
        (file_length_ - flushed_bytes) / kFadviseInterval) {
      // Don't evict the most recent page.
      uint32_t len = file_length_ & ~(kPageSize - 1);
      posix_fadvise(fileno(file_), 0, len, POSIX_FADV_DONTNEED);
    }
#endif
  }
}

void LogFileObject::SetBasename(const char* basename) {
  if (basename == nullptr) {
    base_filename_selected_ = false;
    base_filename_.clear();
  } else {
    base_filename_selected_ = true;
    if (base_filename_ == basename) {
      return;
    }
    base_filename_ = basename;
  }

  // Close old log file since we are changing names
  CloseLogfile();
}

void LogFileObject::SetExtension(const char* ext) {
  if (filename_extension_ != ext) {
    // Close old log file since we are changing names
    CloseLogfile();
    filename_extension_ = ext;
  }
}

void LogFileObject::SetPermissions(int permissions) {
  if (permissions_ != permissions) {
    // Close old log file since we are changing permissions
    // This also deletes the file otherwise it fails to create a file with
    // the same name.
    CloseLogfile(/*delete_file=*/true);
    permissions_ = permissions;
  }
}

void LogFileObject::SetSymlinkBasename(const char* symlink_basename) {
  symlink_basename_ = symlink_basename;
  symlink_basename_initialized_ = true;
}

void LogFileObject::Flush() { FlushUnlocked(); }

std::string LogFileObject::TestOnlyFileHeaderString(
    absl::Time timestamp) const {
  static const std::string* hostname = [] {
    struct utsname buf;
    return new std::string(uname(&buf) ? "(unknown)" : buf.nodename);
  }();
  return absl::StrCat(
      absl::FormatTime("Log file created at: %Y/%m/%d %H:%M:%S", timestamp,
                       tz_),
      "\nRunning on machine: ", *hostname,
      "\nLog line format: [IWEF]mmdd hh:mm:ss.uuuuuu threadid file:line] "
      "msg\n");
}

void LogFileObject::FlushUnlocked() {
  if (file_ != nullptr) {
    fflush(file_);
    bytes_since_flush_ = 0;
  }
  // Figure out when we are due for another flush.
  next_flush_time_ =
      absl::Now() + absl::Seconds(absl::GetFlag(FLAGS_logbufsecs));
}

void LogFileObject::FlushUnsafe() ABSL_NO_THREAD_SAFETY_ANALYSIS {
  if (file_ != nullptr) {
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 19)
    fflush_unlocked(file_);
#else
    fflush(file_);
#endif
  }
}

bool LogFileObject::CreateLogfile(const std::string& time_pid_string) {
  // The log file is rotating, so discard any previous path.
  log_path_.clear();

  std::string string_filename =
      base_filename_ + filename_extension_ + time_pid_string;
  const char* filename = string_filename.c_str();
  int fd = open(filename, O_WRONLY | O_CREAT | O_EXCL,
                permissions_ == 0 ? 0664 : permissions_);

  if (fd == -1) {
    absl::FPrintF(stderr, "Could not open the log file '%s': %s\n", filename,
                  absl::base_internal::StrError(errno));
    return false;
  }

  // Mark the file close-on-exec. We don't really care if this fails
  fcntl(fd, F_SETFD, FD_CLOEXEC);

  // If the application specified permissions, try to chmod the file.
  if (permissions_ != 0) {
    chmod(filename, permissions_);
  }

  file_ = fdopen(fd, "a");  // Make a FILE*.
  if (file_ == nullptr) {
    absl::FPrintF(stderr, "Could not fdopen the log file '%s': %s\n", filename,
                  absl::base_internal::StrError(errno));
    close(fd);
    unlink(filename);  // Erase the half-baked evidence: an unusable log file
    return false;
  }

  // We only initialize the symlink basename when creating a log file because
  // ShortProgramInvocationName is initialized as part of command line flag
  // parsing.  If a SetLog* method is called which affects logs prior to
  // parsing, it would (without lazy initialization) result in symlinks being
  // labeled as unknown.
  if (!symlink_basename_initialized_) {
    symlink_basename_ = absl::flags_internal::ShortProgramInvocationName();
    symlink_basename_initialized_ = true;
  }

  // We try to create a symlink called <program_name>.<severity>,
  // which is easier to use.  (Every time we create a new logfile,
  // we destroy the old symlink and create a new one, so it always
  // points to the latest logfile.)  If it fails, we're sad but it's
  // no error.
  if (!symlink_basename_.empty()) {
    // take directory from filename
    const char* slash = strrchr(filename, PATH_SEPARATOR);
    const std::string linkname =
        absl::StrCat(symlink_basename_, ".", absl::LogSeverityName(severity_));
    std::string linkpath;
    if (slash) linkpath = std::string(filename, slash - filename + 1);
    linkpath += linkname;
    // Errors deliberately ignored:
    unlink(linkpath.c_str());

    // Make the symlink be relative (in the same dir) so that if the
    // entire log directory gets relocated the link is still valid.
    const char* linkdest = slash ? (slash + 1) : filename;
    // Errors deliberately ignored:
    if (symlink(linkdest, linkpath.c_str()) == 0) {
      symlink_path_ = std::move(linkpath);
    } else {
      symlink_path_ = "";
    }

    // Make an additional link to the log file in a place specified by
    // FLAGS_log_link, if indicated
    std::string log_link = absl::GetFlag(FLAGS_log_link);
    if (!log_link.empty()) {
      linkpath = absl::StrCat(log_link, "/", linkname);
      // Errors deliberately ignored for these two calls:
      unlink(linkpath.c_str());
      (void)symlink(filename, linkpath.c_str());
    }
  }

  log_path_ = string_filename;
  return true;  // Everything worked
}

void LogFileObject::CloseLogfile(bool delete_file) {
  if (file_ == nullptr) return;
  fclose(file_);
  file_ = nullptr;
  if (delete_file) {
    ::unlink(log_path_.c_str());
  }
  log_path_.clear();
  log_creation_time_ = absl::InfinitePast();
}

}  // namespace logging_internal
}  // namespace base_logging

#endif  // GLOOP_INTERNAL_PROD_LOGGING
