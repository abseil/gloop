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

// Copyright 2010 Google Inc. All Rights Reserved.

#include "gloop/base/examine_stack.h"

#include <inttypes.h>  // PRIxPTR
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef __linux__
#include <sys/mman.h>
#include <ucontext.h>
#endif

#include <array>
#include <atomic>
#include <memory>
#include <ostream>
#include <string>

// You're not allowed to use anything outside of base/ here (otherwise
// the library you use might as well be part of base!).  So, no strutil
// or other handy things -- you get to do that sort of thing "by hand".

#include "absl/base/attributes.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/debugging/internal/examine_stack.h"
#include "absl/debugging/internal/symbolize.h"
#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/flag.h"
#include "absl/functional/function_ref.h"
#include "absl/log/internal/globals.h"
#include "absl/strings/string_view.h"
#include "gloop/base/proc_maps.h"

#ifdef __linux__
#endif

namespace {
ABSL_CONST_INIT std::atomic<bool> g_symbolize_stacktrace{true};
constexpr int kDefaultDumpStackFramesLimit = 128;
}  // namespace

ABSL_FLAG(bool, dump_all_maps_on_failure, false,
          "Include all mappings (rather than just code files) from"
          "/proc/self/maps in a failure dump");

ABSL_FLAG(bool, symbolize_stacktrace, true,
          "Symbolize the stack trace in the tombstone (and any stack trace "
          "dumped with various Dump*StackTrace() functions)")
    .OnUpdate([] {
      g_symbolize_stacktrace.store(absl::GetFlag(FLAGS_symbolize_stacktrace),
                                   std::memory_order_relaxed);
      absl::log_internal::EnableSymbolizeLogStackTrace(
          absl::GetFlag(FLAGS_symbolize_stacktrace));
    });

ABSL_FLAG(bool, skip_address_map, false,
          "Skip the address map in the tombstone");

ABSL_FLAG(
    int, dump_stack_frames_limit, kDefaultDumpStackFramesLimit,
    "Maximum number of stack frames to print. "
    "Caution: increasing the default may cause timeout during stack printing. "
    "You may need to adjust --stacktrace_timeout and/or set "
    "--alarm_on_failure=no.")
    .OnUpdate([] {
      absl::log_internal::SetMaxFramesInLogStackTrace(
          absl::GetFlag(FLAGS_dump_stack_frames_limit));
    });

namespace base {

// Async signal-safe - usable in signal handlers.
void DebugWriteToStderr(const char* data, void* unused) {
  absl::raw_log_internal::AsyncSignalSafeWriteError(data, strlen(data));
}

void DebugWriteToStream(const char* data, void* os) {
  auto* cast_os = static_cast<std::ostream*>(os);
  *cast_os << data;
}

void DebugWriteToFile(const char* data, void* file) {
  fwrite(data, strlen(data), 1, reinterpret_cast<FILE*>(file));
}

void DebugWriteToString(const char* data, void* str) {
  reinterpret_cast<std::string*>(str)->append(data);
}

// Returns the stack pointer from signal context, nullptr if unknown.
// vuc is a ucontext_t *.  We use void* to avoid the use
// of ucontext_t on non-POSIX systems.
uintptr_t GetSP(void* const vuc) {
#ifdef __linux__
  if (vuc != nullptr) {
    ucontext_t* context = reinterpret_cast<ucontext_t*>(vuc);
#if defined(__aarch64__)
    return context->uc_mcontext.sp;
#elif defined(__arm__)
    return context->uc_mcontext.arm_sp;
#elif defined(__i386__)
    static_assert(7 < ABSL_ARRAYSIZE(context->uc_mcontext.gregs),
                  "gregs_array_too_small");
    return context->uc_mcontext.gregs[REG_ESP];
#elif defined(__powerpc64__)
    return context->uc_mcontext.gp_regs[1];
#elif defined(__powerpc__)
    return context->uc_mcontext.regs->gpr[1];
#elif defined(__x86_64__)
    static_assert(15 < ABSL_ARRAYSIZE(context->uc_mcontext.gregs),
                  "gregs_array_too_small");
    return context->uc_mcontext.gregs[REG_RSP];
#elif defined(__riscv)
    return context->uc_mcontext.__gregs[REG_SP];
#else
#error "Undefined Architecture."
#endif
  }
#elif defined(__akaros__)
  auto* ctx = reinterpret_cast<struct user_context*>(vuc);
  return get_user_ctx_sp(ctx);
#endif
  return 0;
}

#if defined(__linux__)
static const char* kNoPrint = nullptr;

#if defined(__i386__)
// If i is a valid register number, store its name in *name and its value
// (extraced from *vuc) in *value and return true. Else return false.
//
// Can store special value kNoPrint in *name if register should not be printed.
//
static bool GetRegister(int i, void* const vuc, uintptr_t* value,
                        const char** name) {
  // EIP is printed in the stack trace, so skip it, so we fit in three lines.
  static const std::array<const char*, 19> reg_names = {
      " gs", " fs", " es", " ds", "edi",    "esi", "ebp", "esp", "ebx", "edx",
      "ecx", "eax", "trp", "err", kNoPrint, " cs", "efl", "usp", " ss"};
  ucontext_t* uc = reinterpret_cast<ucontext_t*>(vuc);
  static_assert(ABSL_ARRAYSIZE(uc->uc_mcontext.gregs) == reg_names.size());
  if (static_cast<size_t>(i) < reg_names.size()) {
    *value = uc->uc_mcontext.gregs[i];
    *name = reg_names[i];
    return true;
  }
  return false;
}

#elif defined(__x86_64__)
static bool GetRegister(int i, void* const vuc, uintptr_t* value,
                        const char** name) {
  (void)kNoPrint;  // unused

  static const std::array<const char* const, 23> reg_names = {
      " r8", " r9", "r10", "r11", "r12", "r13", "r14", "r15",
      "rdi", "rsi", "rbp", "rbx", "rdx", "rax", "rcx", "rsp",
      "rip", "efl", "cgf", "err", "trp", "msk", "cr2"};
  ucontext_t* uc = reinterpret_cast<ucontext_t*>(vuc);
  static_assert(ABSL_ARRAYSIZE(uc->uc_mcontext.gregs) == reg_names.size());

  if (static_cast<size_t>(i) < reg_names.size()) {
    *value = uc->uc_mcontext.gregs[i];
    *name = reg_names[i];
    return true;
  }
  return false;
}

#elif defined(__powerpc__)
// This includes 32-bit and 64-bit PowerPC
static bool GetRegister(int i, void* const vuc, uintptr_t* value,
                        const char** name) {
  (void)kNoPrint;  // unused

  static const std::array<const char*, 44> reg_names = {
      " r0", " r1", " r2", " r3", " r4",  " r5", " r6",   " r7",     " r8",
      " r9", "r10", "r11", "r12", "r13",  "r14", "r15",   "r16",     "r17",
      "r18", "r19", "r20", "r21", "r22",  "r23", "r24",   "r25",     "r26",
      "r27", "r28", "r29", "r30", "r31",  "nip", "msr",   "orig_r3", "ctr",
      " lr", "xer", "ccr", " mq", "trap", "dar", "dsisr", "result"};

// struct mcontext have different field names for 32-bit and 64-bit PowerPC.
#ifdef __powerpc64__
#define PPC_GREGS gp_regs
#else
#define PPC_GREGS regs->gpr
#endif
  ucontext_t* uc = reinterpret_cast<ucontext_t*>(vuc);
  constexpr size_t kNumGregs = ABSL_ARRAYSIZE(uc->uc_mcontext.PPC_GREGS);
  static_assert(reg_names.size() == 12 + kNumGregs);
  if (static_cast<size_t>(i) < reg_names.size()) {
    *name = reg_names[i];
    switch (i) {
      case kNumGregs:
        *value = uc->uc_mcontext.regs->nip;
        break;
      case kNumGregs + 1:
        *value = uc->uc_mcontext.regs->msr;
        break;
      case kNumGregs + 2:
        *value = uc->uc_mcontext.regs->orig_gpr3;
        break;
      case kNumGregs + 3:
        *value = uc->uc_mcontext.regs->ctr;
        break;
      case kNumGregs + 4:
        *value = uc->uc_mcontext.regs->link;
        break;
      case kNumGregs + 5:
        *value = uc->uc_mcontext.regs->xer;
        break;
      case kNumGregs + 6:
        *value = uc->uc_mcontext.regs->ccr;
        break;
      case kNumGregs + 7:
        *value = uc->uc_mcontext.regs->mq;
        break;
      case kNumGregs + 8:
        *value = uc->uc_mcontext.regs->trap;
        break;
      case kNumGregs + 9:
        *value = uc->uc_mcontext.regs->dar;
        break;
      case kNumGregs + 10:
        *value = uc->uc_mcontext.regs->dsisr;
        break;
      case kNumGregs + 11:
        *value = uc->uc_mcontext.regs->result;
        break;
      default:
        *value = uc->uc_mcontext.PPC_GREGS[i];
        break;
    }
    return true;
  }
  return false;
}
#elif defined(__aarch64__)
static bool GetRegister(int i, void* const vuc, uintptr_t* value,
                        const char** name) {
  (void)kNoPrint;  // unused

  static const std::array<const char*, 34> reg_names = {
      " x0", " x1", " x2", " x3", " x4", " x5", " x6",    " x7", " x8",
      " x9", "x10", "x11", "x12", "x13", "x14", "x15",    "x16", "x17",
      "x18", "x19", "x20", "x21", "x22", "x23", "x24",    "x25", "x26",
      "x27", "x28", "x29", "x30", "sp",  "pc",  "pstate",
  };
  ucontext_t* uc = reinterpret_cast<ucontext_t*>(vuc);
  constexpr int kNumMcontextRegs = ABSL_ARRAYSIZE(uc->uc_mcontext.regs);

  // For aarch64, we dump 3 additional special registers not
  // in uc_mcontext.regs.
  static_assert(kNumMcontextRegs + 3 == reg_names.size());

  if (static_cast<size_t>(i) < reg_names.size()) {
    *name = reg_names[i];
    switch (i) {
      case kNumMcontextRegs:
        *value = uc->uc_mcontext.sp;
        break;
      case kNumMcontextRegs + 1:
        *value = uc->uc_mcontext.pc;
        break;
      case kNumMcontextRegs + 2:
        *value = uc->uc_mcontext.pstate;
        break;
      default:
        *value = uc->uc_mcontext.regs[i];
    }
    return true;
  }
  return false;
}
#elif defined(__arm__)
static bool GetRegister(int i, void* const vuc, uintptr_t* value,
                        const char** name) {
  (void)kNoPrint;  // unused.
  ucontext_t* uc = reinterpret_cast<ucontext_t*>(vuc);
  switch (i) {
    case 0:
      *name = " r0";
      *value = uc->uc_mcontext.arm_r0;
      return true;
    case 1:
      *name = " r1";
      *value = uc->uc_mcontext.arm_r1;
      return true;
    case 2:
      *name = " r2";
      *value = uc->uc_mcontext.arm_r2;
      return true;
    case 3:
      *name = " r3";
      *value = uc->uc_mcontext.arm_r3;
      return true;
    case 4:
      *name = " r4";
      *value = uc->uc_mcontext.arm_r4;
      return true;
    case 5:
      *name = " r5";
      *value = uc->uc_mcontext.arm_r5;
      return true;
    case 6:
      *name = " r6";
      *value = uc->uc_mcontext.arm_r6;
      return true;
    case 7:
      *name = " r7";
      *value = uc->uc_mcontext.arm_r7;
      return true;
    case 8:
      *name = " r8";
      *value = uc->uc_mcontext.arm_r8;
      return true;
    case 9:
      *name = " r9";
      *value = uc->uc_mcontext.arm_r9;
      return true;
    case 10:
      *name = "r10";
      *value = uc->uc_mcontext.arm_r10;
      return true;
    case 11:
      *name = " fp";
      *value = uc->uc_mcontext.arm_fp;
      return true;
    case 12:
      *name = " ip";
      *value = uc->uc_mcontext.arm_ip;
      return true;
    case 13:
      *name = " sp";
      *value = uc->uc_mcontext.arm_sp;
      return true;
    case 14:
      *name = " lr";
      *value = uc->uc_mcontext.arm_lr;
      return true;
    case 15:
      *name = " pc";
      *value = uc->uc_mcontext.arm_pc;
      return true;
    default:
      return false;
  }
}
#elif defined(__riscv)
static bool GetRegister(int i, void* const vuc, uintptr_t* value,
                        const char** name) {
  (void)kNoPrint;  // unused

  ucontext_t* uc = reinterpret_cast<ucontext_t*>(vuc);
  static const std::array<const char*, 32> reg_names = {
      "pc",
      "ra",
      "sp",
      "gp",
      "tp",
      "t0",
      "t1",
      "t2",
      "s0",
      "s1",
      "a0",
      "a1",
      "a2",
      "a3",
      "a4",
      "a5",
      "a6",
      "a7",
      "s2",
      "s3",
      "s4",
      "s5",
      "s6",
      "s7",
      "s8",
      "s9",
      "s"
      "10",
      "s"
      "11",
      "t3",
      "t4",
      "t5",
      "t6",
  };
  static_assert(ABSL_ARRAYSIZE(uc->uc_mcontext.__gregs) == reg_names.size());
  if (static_cast<size_t>(i) < reg_names.size()) {
    *value = uc->uc_mcontext.__gregs[i];
    *name = reg_names[i];
    return true;
  }
  return false;
}
#else
// Generic no-op Implementation
static bool GetRegister(int i, uintptr_t* value, const char** name) {
  (void)kNoPrint;  // unused.
  return false;
}
#endif
#endif  // __linux__

// Dump the register context as directed by writer.
// vuc is a ucontext_t *.  We use void* to avoid the use
// of ucontext_t on non-POSIX systems.
void DumpRegisterContext(void* const vuc, DebugWriter* writer,
                         void* writer_arg) {
#if defined(__linux__)
  if (vuc != nullptr) {
    writer("--- CPU registers: ---\n", writer_arg);

    // Print multiple registers on the same line for conciseness.
    char line[250] = " ";
    size_t line_len = 1;
    uintptr_t value;
    const char* name;
    for (int reg = 0; GetRegister(reg, vuc, &value, &name); reg++) {
      if (name == kNoPrint) continue;  // Deliberately skipped register

      char buf[250];
      snprintf(buf, sizeof(buf), "%s=%" PRIxPTR, name, value);

      // Wrap at 80 characters.
      const size_t buf_len = strlen(buf);
      if (line_len + 1 + buf_len >= 80) {
        strcpy(line + line_len, "\n");
        writer(line, writer_arg);

        strcpy(line, " ");
        line_len = 1;
      }

      strcpy(line + line_len, " ");
      line_len += 1;
      memcpy(line + line_len, buf, buf_len + 1);
      line_len += buf_len;
    }

    strcpy(line + line_len, "\n");
    writer(line, writer_arg);
  }
#else
  writer("Register dump on this platform in not yet supported\n", writer_arg);
#endif  // __linux__ && !__arm__
}

#if defined(__linux__)
void DumpRegisterContext(
    void* vuc, absl::FunctionRef<void(absl::string_view, uintptr_t)> fn) {
  uintptr_t value;
  const char* name;
  for (int reg = 0; GetRegister(reg, vuc, &value, &name); ++reg) {
    if (name != kNoPrint) {
      fn(name, value);
    }
  }
}
#endif  // defined(__linux__)

// Dump a list of executable mappings (mostly shared libraries) to writer.
// Currently Linux-only; grovels through /proc/self/maps.
// Async-termination-safe (acquires no locks),
// but not async-signal-safe (because it can set errno).
void DumpAddressMap(DebugWriter* writer, void* writer_arg) {
#ifdef __linux__

  std::unique_ptr<char[]> alloced_out_buffer;
  std::unique_ptr<char[]> alloced_dir_buffer;
  char* out_buffer;
  char* dir;
  ProcMapsIterator::Buffer* maps_buffer;
  static const size_t kOutBufSize = PATH_MAX + 250;

  {
    // Dynamically allocate the output buffer; ProcMapsIterator will
    // allocate its own buffer
    alloced_out_buffer.reset(new char[kOutBufSize]);
    maps_buffer = nullptr;
    out_buffer = alloced_out_buffer.get();
    alloced_dir_buffer.reset(new char[kOutBufSize]);
    dir = alloced_dir_buffer.get();
  }
  ProcMapsIterator it(0, maps_buffer);

  if (it.Valid()) {
    writer("--- Memory map: ---\n", writer_arg);

    uint64_t begin, end, pos;
    char *filename, *flags;

    const bool dump_all_maps_on_failure =
        absl::GetFlag(FLAGS_dump_all_maps_on_failure);
    while (it.Next(&begin, &end, &flags, &pos, nullptr, &filename)) {
      if (filename != nullptr && filename[0] == '\0') {
        // Get the pre-hugepage text/hugepage data mappings that we stored.
        const void* pbegin = reinterpret_cast<const void*>(begin);
        const void* pend = reinterpret_cast<const void*>(end);
        uint64_t position;
        const char* cfilename;
        if (absl::debugging_internal::GetFileMappingHint(
                &pbegin, &pend, &position, &cfilename)) {
          begin = reinterpret_cast<uint64_t>(pbegin);
          end = reinterpret_cast<uint64_t>(pend);
          pos = position;
          filename = const_cast<char*>(cfilename);
        }
      }

      // Only print executable maps unless we're asked for all of them.
      if ((dump_all_maps_on_failure) ||
          (flags[2] == 'x' && filename[0] != '\0')) {
        // Collapse common "...-(dbg|opt)/" prefix for brevity.
        char* ptr = strstr(filename, "-dbg/");
        if (nullptr == ptr) ptr = strstr(filename, "-opt/");
        if (nullptr != ptr) {
          char* end = ptr + strlen("-XXX");
          if (memcmp(dir, filename, end - filename)) {
            strncpy(dir, filename, end - filename);
            dir[end - filename] = '\0';
            snprintf(out_buffer, kOutBufSize, "  build=%s\n", dir);
            writer(out_buffer, writer_arg);
          }
          sprintf(filename, "$build%s", end);
        }

        // Print out the mapping, with the file offset if nonzero.
        int n = sprintf(out_buffer, "  %08llx-%08llx: %s",
                        static_cast<unsigned long long>(begin),  // NOLINT
                        static_cast<unsigned long long>(end),    // NOLINT
                        filename);
        if (pos == 0) {
          // Append a newline.
          out_buffer[n] = '\n';
          out_buffer[++n] = '\0';
        } else {
          // Append offset.
          sprintf(out_buffer + n, " (@%llx)\n",
                  static_cast<unsigned long long>(pos));  // NOLINT
        }
        writer(out_buffer, writer_arg);
      }
    }
  }
#endif  // __linux__
}

// The %p field width for printf() functions is two characters per byte,
// and two extra for the leading "0x".
static const int kPrintfPointerFieldWidth = 2 + 2 * sizeof(void*);

// Print a program counter and its symbol name.
static void DumpPCAndSymbol(DebugWriter* writer, void* writer_arg,
                            void* const pc) {
  char tmp[1024];
  const char* symbol = "(unknown)";
  // Symbolizes the previous address of pc because pc may be in the
  // next function.  The overrun happens when the function ends with
  // a call to a function annotated noreturn (e.g. CHECK).
  // If symbolization of pc-1 fails, also try pc on the off-chance
  // that we crashed on the first instruction of a function (that
  // actually happens very often e.g. __restore_rt).
  const uintptr_t prev_pc = reinterpret_cast<uintptr_t>(pc) - 1;
  if (absl::Symbolize(reinterpret_cast<char*>(prev_pc), tmp, sizeof(tmp)) ||
      absl::Symbolize(pc, tmp, sizeof(tmp))) {
    symbol = tmp;
  }
  char buf[1024];
  snprintf(buf, sizeof(buf), "    @ %*p  %s\n", kPrintfPointerFieldWidth, pc,
           symbol);
  writer(buf, writer_arg);
}

static void DumpPC(DebugWriter* writer, void* writer_arg, void* const pc) {
  char buf[100];
  snprintf(buf, sizeof(buf), "    @ %*p\n", kPrintfPointerFieldWidth, pc);
  writer(buf, writer_arg);
}

// Async-signal safe mmap allocator.
static void* Allocate(size_t num_bytes) {
#if __linux__
  void* p = mmap(nullptr, num_bytes, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return p == MAP_FAILED ? nullptr : p;
#else
  return nullptr;
#endif  // __linux__
}

static void Deallocate(void* p, size_t size) {
#if __linux__
  munmap(p, size);
#endif  // __linux__
}

// Dump current stack trace as directed by writer.
// Make sure this function is not inlined to avoid skipping too many top frames.
ABSL_ATTRIBUTE_NOINLINE
void DumpStackTrace(int skip_count, DebugWriter* writer, void* writer_arg) {
  absl::debugging_internal::DumpStackTrace(
      skip_count + 1, absl::GetFlag(FLAGS_dump_stack_frames_limit),
      g_symbolize_stacktrace.load(std::memory_order_relaxed), writer,
      writer_arg);
  ABSL_BLOCK_TAIL_CALL_OPTIMIZATION();
}

void DumpPCAndStackTrace(void* const pc, void* const stack[], int depth,
                         DebugWriter* writer, void* writer_arg) {
  if (pc != nullptr) {
    if (absl::GetFlag(FLAGS_symbolize_stacktrace)) {
      DumpPCAndSymbol(writer, writer_arg, pc);
    } else {
      DumpPC(writer, writer_arg, pc);
    }
  }
  for (int i = 0; i < depth; i++) {
    if (absl::GetFlag(FLAGS_symbolize_stacktrace)) {
      DumpPCAndSymbol(writer, writer_arg, stack[i]);
    } else {
      DumpPC(writer, writer_arg, stack[i]);
    }
  }
}

std::string CurrentStackTrace() {
  std::string result = "Stack trace:\n";
  DumpStackTrace(1, DebugWriteToString, &result);
  ABSL_BLOCK_TAIL_CALL_OPTIMIZATION();
  return result;
}

void SavedStackTrace::CreateCurrent(int skip_count) {
  depth_ = absl::GetStackTrace(stack_, ABSL_ARRAYSIZE(stack_), 1 + skip_count);
}

// Convenient wrapper around DumpPCAndFrameSizesAndSymbol() for signal
// handlers. "noinline" so that GetStackFrames() skips the top-most stack
// frame for this function.
ABSL_ATTRIBUTE_NOINLINE void DumpPCAndStackTraceForSignalHandler(
    void* const uc, DebugWriter* writer, void* writer_arg) {
  void* const pc = base::GetPC(uc);
  void* stack_buf[kDefaultDumpStackFramesLimit];
  int frame_sizes_buf[kDefaultDumpStackFramesLimit];

  void** stack = stack_buf;
  int* frame_sizes = frame_sizes_buf;
  int num_stack = kDefaultDumpStackFramesLimit;
  size_t allocated_bytes = 0;

  const int dump_stack_frames_limit =
      absl::GetFlag(FLAGS_dump_stack_frames_limit);
  if (dump_stack_frames_limit <= num_stack) {
    // User requested fewer frames than we already have space for.
    num_stack = dump_stack_frames_limit;
  } else {
    // Get space for frame pointers and sizes in one allocation.
    size_t needed_bytes =
        dump_stack_frames_limit * (sizeof(stack[0]) + sizeof(frame_sizes[0]));
    void* p = Allocate(needed_bytes);
    if (p != nullptr) {  // We got the space.
      num_stack = dump_stack_frames_limit;
      stack = static_cast<void**>(p);
      // Frame sizes immediately follow frame pointers.
      frame_sizes = reinterpret_cast<int*>(&stack[num_stack]);
      allocated_bytes = needed_bytes;
    }
  }

  int min_dropped_frames;
  int depth = absl::GetStackFramesWithContext(
      stack + 1,
      frame_sizes + 1,  // Reserve stack[0] for pc.
      num_stack - 1,
      1,  // Do not include this function in stack trace.
      uc, &min_dropped_frames);
  absl::debugging_internal::DumpPCAndFrameSizesAndStackTrace(
      pc, stack + 1, frame_sizes + 1, depth, min_dropped_frames,
      absl::GetFlag(FLAGS_symbolize_stacktrace), writer, writer_arg);
  auto hook = absl::debugging_internal::GetDebugStackTraceHook();
  if (hook != nullptr) {
    int start = 1;
    if (pc != nullptr) {
      stack[0] = pc;
      start = 0;
      depth++;
    }
    hook(stack + start, depth, pc, writer, writer_arg);
  }
  if (allocated_bytes != 0) Deallocate(stack, allocated_bytes);
}

}  // namespace base
