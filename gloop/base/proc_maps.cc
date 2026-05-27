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

#include "gloop/base/proc_maps.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/types.h>

#include <cstdint>

#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef __linux__
#include <sys/sysmacros.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>    // for iterating over dll's in ProcMapsIter
#include <mach-o/loader.h>  // for iterating over dll's in ProcMapsIter
#endif

#ifdef _WIN32
#include <stringapiset.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/thread_annotations.h"
#include "absl/flags/flag.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "gloop/base/scheduling/scheduling_mode.h"
#include "gloop/base/spinlock.h"

// Re-run fn until it doesn't cause EINTR.
#define NO_INTR(fn) \
  do {              \
  } while ((fn) < 0 && errno == EINTR)

// Avoid deadlocks in HeapChecker (b/138719973).
namespace {

ABSL_CONST_INIT static absl::base_internal::SpinLock prefix_lock(
    absl::base_internal::SCHEDULE_KERNEL_ONLY);

ABSL_CONST_INIT static std::string* procfs_prefix ABSL_GUARDED_BY(prefix_lock) =
    nullptr;

}  // namespace

ABSL_FLAG(std::string, procfs_prefix, "",
          "string to prepend to /proc filenames opened "
          "via OpenProcFile")
    .OnUpdate([] {
      const std::string prefix = absl::GetFlag(FLAGS_procfs_prefix);
      SpinLockHolder l(prefix_lock);
      // If the flag has previously been set, we need to clear it.
      if (prefix.empty()) {
        if (procfs_prefix != nullptr) {
          procfs_prefix->clear();
        }
        // Lazily-allocate the prefix, as it won't be used 99% of the time.
      } else {
        if (procfs_prefix == nullptr) {
          procfs_prefix = new std::string(prefix);
        } else {
          procfs_prefix->assign(prefix);
        }
      }
    });

#ifdef __linux__
namespace proc_maps_internal {

void ConstructFilename(const char* spec, pid_t pid, char* buf, int buf_size) {
  ABSL_RAW_DCHECK(buf_size >= 0, "invalid buffer size");
  // We are duplicating the code here for performance.  The second
  // call requires constructing a new string object and then
  // destructing it again, which is a waste if the string ends up
  // being the same as the passed in cstring anyway.  Additionally,
  // don't prepend the prefix in the case when these functions are
  // being used to read kernel-generated files from places other than
  // /proc
  if (!pid) {
    pid = getpid();
  }

  size_t prefix_len = 0;
  if (absl::StartsWith(spec, "/proc")) {
    SpinLockHolder l(prefix_lock);
    if (procfs_prefix != nullptr) {
      prefix_len = std::min<size_t>(buf_size, procfs_prefix->size());
      memcpy(buf, procfs_prefix->c_str(), prefix_len);
    }
  }
  buf += prefix_len;
  buf_size -= prefix_len;

  ABSL_RAW_CHECK(snprintf(buf, buf_size, spec, pid, pid) < buf_size,
                 "Output truncated.");
}

bool HasProcfsPrefix() {
  SpinLockHolder l(prefix_lock);
  return procfs_prefix != nullptr && !procfs_prefix->empty();
}

}  // namespace proc_maps_internal
#endif

// A templatized helper function instantiated for Mach (OS X) only.
// It can handle finding info for both 32 bits and 64 bits.
// Returns true if it successfully handled the hdr, false else.
#if defined(__APPLE__)
template <uint32_t kMagic, uint32_t kLCSegment, typename MachHeader,
          typename SegmentCommand>
static bool NextExtMachHelper(const mach_header* hdr, int current_image,
                              int current_load_cmd, uint64_t* start,
                              uint64_t* end, char** flags, uint64_t* offset,
                              int64_t* inode, char** filename, dev_t* dev) {
  static char kDefaultPerms[5] = "r-xp";
  if (hdr->magic != kMagic) return false;
  const char* lc = (const char*)hdr + sizeof(MachHeader);
  // TODO: make this not-quadratic (increment and hold state)
  for (int j = 0; j < current_load_cmd; j++)  // advance to *our* load_cmd
    lc += ((const load_command*)lc)->cmdsize;
  if (((const load_command*)lc)->cmd == kLCSegment) {
    const intptr_t dlloff = _dyld_get_image_vmaddr_slide(current_image);
    const SegmentCommand* sc = (const SegmentCommand*)lc;
    if (start) *start = sc->vmaddr + dlloff;
    if (end) *end = sc->vmaddr + sc->vmsize + dlloff;
    if (flags) *flags = kDefaultPerms;  // can we do better?
    if (offset) *offset = sc->fileoff;
    if (inode) *inode = 0;
    if (filename)
      *filename = const_cast<char*>(_dyld_get_image_name(current_image));
    if (dev) *dev = 0;
    return true;
  }

  return false;
}
#endif

ProcMapsIterator::ProcMapsIterator(pid_t pid) { Init(pid, nullptr); }

ProcMapsIterator::ProcMapsIterator(pid_t pid, Buffer* buffer) {
  Init(pid, buffer);
}

void ProcMapsIterator::Init(pid_t pid, Buffer* buffer) {
  pid_ = pid;
  if (!buffer) {
    // If the user didn't pass in any buffer storage, allocate it
    // now. This is the normal case; the signal handler passes in a
    // static buffer.
    buffer = dynamic_buffer_ = new Buffer;
  } else {
    dynamic_buffer_ = nullptr;
  }

  ibuf_ = buffer->buf;

  stext_ = etext_ = nextline_ = ibuf_;
  ebuf_ = ibuf_ + Buffer::kBufSize - 1;
  nextline_ = ibuf_;

#if defined(__linux__)
  // /maps exists in two places: /proc/pid/ and /proc/pid/task/tid
  // (for each thread in the process.)  The only difference between
  // these is the "global" view (/proc/pid/maps) attempts to label
  // each VMA which is the stack of a thread.  This is nice to have,
  // but not critical, and scales quadratically.  Use the main thread's
  // "local" view to ensure adequate performance.
  // (Note that ConstructFilename gives the <pid> argument twice to snprintf,
  // so it's fine that we have two %ds and only one source.)
  proc_maps_internal::ConstructFilename("/proc/%d/task/%d/maps", pid, ibuf_,
                                        Buffer::kBufSize);

  // No error logging since this can be called from the crash dump
  // handler at awkward moments. Users should call Valid() before
  // using.
  NO_INTR(fd_ = open(ibuf_, O_RDONLY));
#elif defined(__APPLE__)
  current_load_cmd_ = -1;
  current_image_ = _dyld_image_count();  // count down from the top
#elif defined(_WIN32)
  snapshot_ = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                       GetCurrentProcessId());
  memset(&module_, 0, sizeof(module_));
#else
  fd_ = -1;  // so Valid() is always false
#endif
}

ProcMapsIterator::~ProcMapsIterator() {
#if defined _WIN32
  if (snapshot_ != INVALID_HANDLE_VALUE) CloseHandle(snapshot_);
#elif defined(__APPLE__)
  // no cleanup necessary!
#elif defined __linux__
  // As it turns out, Linux guarantees that close() does in fact close a file
  // descriptor even when the return value is EINTR. According to the notes in
  // the manpage for close(2), this is widespread yet not fully portable, which
  // is unfortunate. POSIX explicitly leaves this behavior as unspecified.
  if (fd_ >= 0) close(fd_);
#else
  if (fd_ >= 0) NO_INTR(close(fd_));
#endif
  delete dynamic_buffer_;
}

bool ProcMapsIterator::Valid() const {
#if defined _WIN32
  return snapshot_ != INVALID_HANDLE_VALUE;
#elif defined(__APPLE__)
  return 1;
#else
  return fd_ != -1;
#endif
}

bool ProcMapsIterator::Next(uint64_t* start, uint64_t* end, char** flags,
                            uint64_t* offset, int64_t* inode, char** filename) {
  return NextExt(start, end, flags, offset, inode, filename, nullptr);
}

// based on code in google.cc originally written by Mike Burrows
// This has too many arguments.  It should really be building
// a map object and returning it.  The problem is that this is called
// when the memory allocator state is undefined, hence the arguments.
bool ProcMapsIterator::NextExt(uint64_t* start, uint64_t* end, char** flags,
                               uint64_t* offset, int64_t* inode,
                               char** filename, dev_t* dev) {
#if defined __linux__
  do {
    // Advance to the start of the next line
    stext_ = nextline_;

    // See if we have a complete line in the buffer already
    nextline_ = static_cast<char*>(memchr(stext_, '\n', etext_ - stext_));
    if (!nextline_) {
      // Shift/fill the buffer so we do have a line
      int count = etext_ - stext_;

      // Move the current text to the start of the buffer
      memmove(ibuf_, stext_, count);
      stext_ = ibuf_;
      etext_ = ibuf_ + count;

      int nread = 0;  // fill up buffer with text
      while (etext_ < ebuf_) {
        NO_INTR(nread = read(fd_, etext_, ebuf_ - etext_));
        if (nread > 0)
          etext_ += nread;
        else
          break;
      }

      // Zero out remaining characters in buffer at EOF to avoid returning
      // garbage from subsequent calls.
      if (etext_ != ebuf_ && nread == 0) {
        memset(etext_, 0, ebuf_ - etext_);
      }
      *etext_ = '\n';  // sentinel; safe because ibuf extends 1 char beyond ebuf
      nextline_ = static_cast<char*>(memchr(stext_, '\n', etext_ + 1 - stext_));
    }
    *nextline_ = 0;                               // turn newline into nul
    nextline_ += ((nextline_ < etext_) ? 1 : 0);  // skip nul if not end of text
    // stext_ now points at a nul-terminated line
    unsigned long long tmpstart, tmpend, tmpoffset;           // NOLINT
    long long tmpinode, local_inode;                          // NOLINT
    unsigned long long local_start, local_end, local_offset;  // NOLINT
    int major, minor;
    unsigned filename_offset = 0;
    // for now, assume all linuxes have the same format
    int para_num =
        sscanf(stext_, "%llx-%llx %4s %llx %x:%x %lld %n",
               start ? &local_start : &tmpstart, end ? &local_end : &tmpend,
               flags_, offset ? &local_offset : &tmpoffset, &major, &minor,
               inode ? &local_inode : &tmpinode, &filename_offset);

    if (para_num != 7) continue;

    if (start) *start = local_start;
    if (end) *end = local_end;
    if (offset) *offset = local_offset;
    if (inode) *inode = local_inode;
    // Depending on the Linux kernel being used, there may or may not be a space
    // after the inode if there is no filename.  sscanf will in such situations
    // nondeterministically either fill in filename_offset or not (the results
    // differ on multiple calls in the same run even with identical arguments).
    // We don't want to wander off somewhere beyond the end of the string.
    size_t stext_length = strlen(stext_);
    if (filename_offset == 0 || filename_offset > stext_length)
      filename_offset = stext_length;

    // We found an entry
    if (flags) *flags = flags_;
    if (filename) *filename = stext_ + filename_offset;
    if (dev) *dev = makedev(major, minor);

    return true;
  } while (etext_ > ibuf_);
#elif defined(__APPLE__)
  // We return a separate entry for each segment in the DLL. (TODO:
  // can we do better?)  A DLL ("image") has load-commands, some of which
  // talk about segment boundaries.
  // cf image_for_address from
  // http://svn.digium.com/view/asterisk/team/oej/minivoicemail/dlfcn.c?revision=53912
  for (; current_image_ >= 0; current_image_--) {
    const mach_header* hdr = _dyld_get_image_header(current_image_);
    if (!hdr) continue;
    if (current_load_cmd_ < 0)         // set up for this image
      current_load_cmd_ = hdr->ncmds;  // again, go from the top down

    // We start with the next load command (we've already looked at this one).
    for (current_load_cmd_--; current_load_cmd_ >= 0; current_load_cmd_--) {
#ifdef MH_MAGIC_64
      if (NextExtMachHelper<MH_MAGIC_64, LC_SEGMENT_64, struct mach_header_64,
                            struct segment_command_64>(
              hdr, current_image_, current_load_cmd_, start, end, flags, offset,
              inode, filename, dev)) {
        return true;
      }
#endif
      if (NextExtMachHelper<MH_MAGIC, LC_SEGMENT, struct mach_header,
                            struct segment_command>(
              hdr, current_image_, current_load_cmd_, start, end, flags, offset,
              inode, filename, dev)) {
        return true;
      }
    }
    // If we get here, no more load_cmd's in this image talk about
    // segments.  Go on to the next image.
  }
#elif defined _WIN32
  static char kDefaultPerms[5] = "r-xp";
  BOOL ok;
  if (module_.dwSize == 0) {  // only possible before first call
    module_.dwSize = sizeof(module_);
    ok = Module32FirstW(snapshot_, &module_);
  } else {
    ok = Module32NextW(snapshot_, &module_);
  }
  if (ok) {
    uint64_t base_addr = reinterpret_cast<DWORD_PTR>(module_.modBaseAddr);
    if (start) *start = base_addr;
    if (end) *end = base_addr + module_.modBaseSize;
    if (flags) *flags = kDefaultPerms;
    if (offset) *offset = 0;
    if (inode) *inode = 0;
    if (filename &&
        WideCharToMultiByte(CP_UTF8, /*dwFlags=*/0, module_.szExePath,
                            /*cchWideChar=*/-1, module_filename_utf8_,
                            sizeof(module_filename_utf8_),
                            /*lpDefaultChar=*/nullptr,
                            /*lpUsedDefaultChar=*/nullptr)) {
      *filename = module_filename_utf8_;
    }
    if (dev) *dev = 0;
    return true;
  }
#endif

  // We didn't find anything
  return false;
}

int ProcMapsIterator::FormatLine(char* buffer, int bufsize, uint64_t start,
                                 uint64_t end, const char* flags,
                                 uint64_t offset, int64_t inode,
                                 const char* filename, dev_t dev) {
  // We assume 'flags' looks like 'rwxp' or 'rwx'.
  char r = (flags && flags[0] == 'r') ? 'r' : '-';
  char w = (flags && flags[0] && flags[1] == 'w') ? 'w' : '-';
  char x = (flags && flags[0] && flags[1] && flags[2] == 'x') ? 'x' : '-';
  // On Linux, the last flag is either 'p' or 's', but never '-'.
  char p =
      (flags && flags[0] && flags[1] && flags[2] && flags[3]) ? flags[3] : 'p';

  int dev_major;
  int dev_minor;
#ifdef __linux__
  dev_major = major(dev);
  dev_minor = minor(dev);
#else
  dev_major = dev / 256;
  dev_minor = dev % 256;
#endif

  const int rc = absl::SNPrintF(
      buffer, bufsize, "%08x-%08x %c%c%c%c %08x %02x:%02x %-11d %s\n", start,
      end, r, w, x, p, offset, dev_major, dev_minor, inode, filename);
  return (rc < 0 || rc >= bufsize) ? 0 : rc;
}
