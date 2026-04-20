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

#ifndef THIRD_PARTY_GLOOP_BASE_PROC_MAPS_H_
#define THIRD_PARTY_GLOOP_BASE_PROC_MAPS_H_

#include <limits.h>
#include <stddef.h>
#include <sys/types.h>

#include <cstdint>
#include <string>

#ifdef _WIN32
// clang-format off
#include <windows.h>  // NOLINT: Must come before TlHelp32.h
#include <TlHelp32.h>
// clang-format on
#endif

#include "absl/base/macros.h"  // IWYU pragma: keep
#include "absl/flags/declare.h"
#include "gloop/base/port.h"  // IWYU pragma: keep

// String to prepend to /proc filenames opened via OpenProcFile
ABSL_DECLARE_FLAG(std::string, procfs_prefix);

#ifdef __linux__
namespace proc_maps_internal {

void ConstructFilename(const char* spec, pid_t pid, char* buf, int buf_size);

bool HasProcfsPrefix();

}  // namespace proc_maps_internal
#endif

// A ProcMapsIterator abstracts access to /proc/maps for a given
// process. Needs to be stack-allocatable and avoid using stdio/malloc
// so it can be used in the google stack dumper, heap-profiler, etc.
//
// On Windows and Mac OS X, this iterator iterates *only* over DLLs
// mapped into this process space.  For Linux and Solaris,
// it iterates over *all* mapped memory regions, including anonymous
// mmaps.  For other O/Ss, it is unlikely to work at all, and Valid()
// will always return false.
namespace gloop {
class DlIteratePhdrIterator;
}

class ProcMapsIterator {
 public:
  struct Buffer {
    static constexpr size_t kBufSize = PATH_MAX + 1024;
    char buf[kBufSize];
  };

  // Create a new iterator for the specified pid.  pid can be 0 for "self".
  explicit ProcMapsIterator(pid_t pid, bool use_dl_iterate_phdr = false);

  // Create an iterator with specified storage (for use in signal
  // handler). "buffer" should point to a ProcMapsIterator::Buffer
  // buffer can be null in which case a buffer will be allocated.
  ProcMapsIterator(pid_t pid, Buffer* buffer, bool use_dl_iterate_phdr = false);

  // Returns true if the iterator successfully initialized;
  bool Valid() const;

  // Returns a pointer to the most recently parsed line. Only valid
  // after Next() returns true, and until the iterator is destroyed or
  // Next() is called again.  This may give strange results on non-Linux
  // systems.  Prefer FormatLine() if that may be a concern.
  const char* CurrentLine() const { return stext_; }

  // Writes the "canonical" form of the /proc/xxx/maps info for a single
  // line to the passed-in buffer. Returns the number of bytes written,
  // or 0 if it was not able to write the complete line.  (To guarantee
  // success, buffer should have size at least Buffer::kBufSize.)
  // Takes as arguments values set via a call to Next().  The
  // "canonical" form of the line (taken from linux's /proc/xxx/maps):
  //    <start_addr(hex)>-<end_addr(hex)> <perms(rwxp)> <offset(hex)>   +
  //    <major_dev(hex)>:<minor_dev(hex)> <inode> <filename> Note: the
  // eg
  //    08048000-0804c000 r-xp 00000000 03:01 3793678    /bin/cat
  // If you don't have the dev_t (dev), feel free to pass in 0.
  // (Next() doesn't return a dev_t, though NextExt does.)
  //
  // Note: if filename and flags were obtained via a call to Next(),
  // then the output of this function is only valid if Next() returned
  // true, and only until the iterator is destroyed or Next() is
  // called again.  (Since filename, at least, points into CurrentLine.)
  static int FormatLine(char* buffer, int bufsize, uint64_t start, uint64_t end,
                        const char* flags, uint64_t offset, int64_t inode,
                        const char* filename, dev_t dev);

  // Find the next entry in /proc/maps; return true if found or false
  // if at the end of the file.
  //
  // Any of the result pointers can be null if you're not interested
  // in those values.
  //
  // If "flags" and "filename" are passed, they end up pointing to
  // storage within the ProcMapsIterator that is valid only until the
  // iterator is destroyed or Next() is called again. The caller may
  // modify the contents of these strings (up as far as the first NUL,
  // and only until the subsequent call to Next()) if desired.

  // The offsets are all uint64_t in order to handle the case of a
  // 32-bit process running on a 64-bit kernel
  //
  // IMPORTANT NOTE: see top-of-class notes for details about what
  // mapped regions Next() iterates over, depending on O/S.
  // TODO: make flags and filename const.
  bool Next(uint64_t* start, uint64_t* end, char** flags, uint64_t* offset,
            int64_t* inode, char** filename);

  bool NextExt(uint64_t* start, uint64_t* end, char** flags, uint64_t* offset,
               int64_t* inode, char** filename, dev_t* dev);

  ~ProcMapsIterator();

 private:
  void Init(pid_t pid, Buffer* buffer, bool use_dl_iterate_phdr);

  char* ibuf_;      // input buffer
  char* stext_;     // start of text
  char* etext_;     // end of text
  char* nextline_;  // start of next line
  char* ebuf_;      // end of buffer (1 char for a nul)
#if defined _WIN32
  HANDLE snapshot_;        // filehandle on dll info
  MODULEENTRY32W module_;  // info about current dll (and dll iterator)
  // MODULEENTRY32W's `szExePath` contains up to MAX_PATH (260) code units of
  // UTF-16, including a U+0000 terminator.
  //
  // Each UTF-16 code unit is encoded in 1 to 3 bytes of UTF-8 (UTF-16 surrogate
  // pairs map 2 UTF-16 code units to 4 bytes of UTF-8, so 2 bytes of UTF-8 for
  // each surrogate code unit), so multiply by 3 for the worst case.
  char module_filename_utf8_[ABSL_ARRAYSIZE(module_.szExePath) * 3];
#elif defined(__APPLE__)
  int current_image_;     // dll's are called "images" in macos parlance
  int current_load_cmd_;  // the segment of this dll we're examining
#else
  int fd_;  // filehandle on /proc/*/maps
  gloop::DlIteratePhdrIterator* dl_iter_;
#endif
  pid_t pid_;
  char flags_[10];
  Buffer* dynamic_buffer_;  // dynamically-allocated Buffer
};

#endif  // THIRD_PARTY_GLOOP_BASE_PROC_MAPS_H_
