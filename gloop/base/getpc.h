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

// This is an internal header file used by profiler.cc.  It defines
// the single (inline) function GetPC.  GetPC is used in a signal
// handler to figure out the instruction that was being executed when
// the signal-handler was triggered.
//
// To get this, we use the ucontext_t argument to the signal-handler
// callback, which holds the full context of what was going on when
// the signal triggered.  How to get from a ucontext_t to a Program
// Counter is OS-dependent.

#ifndef THIRD_PARTY_GLOOP_BASE_GETPC_H_
#define THIRD_PARTY_GLOOP_BASE_GETPC_H_

#include <string.h>  // IWYU pragma: keep

#if defined OS_WINDOWS
typedef int ucontext_t;
#elif defined OS_CYGWIN
#include <cygwin/signal.h>

typedef ucontext ucontext_t;
#elif defined __APPLE__
#include <sys/ucontext.h>
#elif defined(__myriad2__)
// Implementation omitted due to lack of ucontext_t.
#else
#include <ucontext.h>  // for ucontext_t (and also mcontext_t)
#endif

#if defined __linux__ && defined __powerpc64__
#include <asm/ptrace.h>  // for PT_NIP.
#endif

#if defined OS_WINDOWS || defined OS_CYGWIN || defined __akaros__ || \
    defined __Fuchsia__
#include "absl/base/internal/raw_logging.h"
#endif

// Take the example where function Foo() calls function Bar().  For
// many architectures, Bar() is responsible for setting up and tearing
// down its own stack frame.  In that case, it's possible for the
// interrupt to happen when execution is in Bar(), but the stack frame
// is not properly set up (either before it's done being set up, or
// after it's been torn down but before Bar() returns).  In those
// cases, the stack trace cannot see the caller function anymore.
//
// GetPC can try to identify this situation, on architectures where it
// might occur, and unwind the current function call in that case to
// avoid false edges in the profile graph (that is, edges that appear
// to show a call skipping over a function).  To do this, we hard-code
// in the asm instructions we might see when setting up or tearing
// down a stack frame.
//
// This is difficult to get right: the instructions depend on the
// processor, the compiler ABI, and even the optimization level.  This
// is a best effort patch -- if we fail to detect such a situation, or
// mess up the PC, nothing happens; the returned PC is not used for
// any further processing.
struct CallUnrollInfo {
  // Offset from (e)ip register where this instruction sequence
  // should be matched. Interpreted as bytes. Offset 0 is the next
  // instruction to execute. Be extra careful with negative offsets in
  // architectures of variable instruction length (like x86) - it is
  // not that easy as taking an offset to step one instruction back!
  int pc_offset;
  // The actual instruction bytes. Feel free to make it larger if you
  // need a longer sequence.
  char ins[16];
  // How many bytes to match from ins array?
  int ins_size;
  // The offset from the stack pointer (e)sp where to look for the
  // call return address. Interpreted as bytes.
  int return_sp_offset;
};

#if defined OS_FREEBSD && defined(__i386__)
// This is probably x86-specific
inline void* GetPC(const ucontext_t& signal_ucontext) {
  return (void*)signal_ucontext.uc_mcontext.mc_eip;
}

#elif defined OS_FREEBSD && defined(__x86_64__)
// This is probably x86-specific
inline void* GetPC(const ucontext_t& signal_ucontext) {
  return (void*)signal_ucontext.uc_mcontext.mc_rip;  // UNTESTED
}

#elif defined __APPLE__ && defined(__i386__)
inline void* GetPC(const ucontext_t& signal_ucontext) {
  void* pc = nullptr;
  if (signal_ucontext.uc_mcontext) {
#if __DARWIN_UNIX03
    pc = reinterpret_cast<void*>(signal_ucontext.uc_mcontext->__ss.__eip);
#else
    pc = reinterpret_cast<void*>(signal_ucontext.uc_mcontext->ss.eip);
#endif
  }
  return pc;
}

#elif defined __APPLE__ && defined(__x86_64__)
inline void* GetPC(const ucontext_t& signal_ucontext) {
  void* pc = nullptr;
  if (signal_ucontext.uc_mcontext) {
#if __DARWIN_UNIX03
    pc = reinterpret_cast<void*>(signal_ucontext.uc_mcontext->__ss.__rip);
#else
    pc = reinterpret_cast<void*>(signal_ucontext.uc_mcontext->ss.rip);
#endif
  }
  return pc;
}

#elif defined __APPLE__ && defined(__arm__)
inline void* GetPC(const ucontext_t& signal_ucontext) {
  void* pc = nullptr;
  if (signal_ucontext.uc_mcontext) {
#if __DARWIN_UNIX03
    pc = reinterpret_cast<void*>(signal_ucontext.uc_mcontext->__ss.__pc);
#else
    pc = reinterpret_cast<void*>(signal_ucontext.uc_mcontext->ss.pc);
#endif
  }
  return pc;
}

#elif defined __APPLE__ && defined(__aarch64__)
inline void* GetPC(const ucontext_t& signal_ucontext) {
  if (signal_ucontext.uc_mcontext)
    return reinterpret_cast<void*>(
        __darwin_arm_thread_state64_get_pc(signal_ucontext.uc_mcontext->__ss));
  return nullptr;
}

#elif defined __linux__ && defined(__aarch64__)
inline void* GetPC(const ucontext_t& signal_ucontext) {
  return reinterpret_cast<void*>(signal_ucontext.uc_mcontext.pc);
}

#elif defined __linux__ && defined(__arm__)
inline void* GetPC(const ucontext_t& signal_ucontext) {
  return (void*)signal_ucontext.uc_mcontext.arm_pc;
}

#elif defined __linux__ && defined(__x86_64__)
inline void* GetPC(const ucontext_t& signal_ucontext) {
  return (void*)signal_ucontext.uc_mcontext.gregs[REG_RIP];
}

#elif defined __linux__ && defined(__ia64)
inline void* GetPC(const ucontext_t& signal_ucontext) {
  return (void*)signal_ucontext.uc_mcontext.sc_ip;
}

#elif defined(__linux__) && defined(__riscv)
inline void* GetPC(const ucontext_t& signal_ucontext) {
  return (void*)signal_ucontext.uc_mcontext.__gregs[REG_PC];
}

#elif defined __linux__ && defined(__i386)
// i386-linux-gcc is one of the cases where we know the instructions
// (usually) to test the corner cases discussed at top-of-file.

static const CallUnrollInfo callunrollinfo[] = {
#ifdef __GNUC__
    // Entry to a function:  push %ebp;  mov  %esp,%ebp
    // Top-of-stack contains the caller IP.
    {0, {0x55, 0x89, 0xe5}, 3, 0},
    // Entry to a function, second instruction:  push %ebp;  mov  %esp,%ebp
    // Top-of-stack contains the old frame, caller IP is +4.
    {-1, {0x55, 0x89, 0xe5}, 3, 4},
    // Return from a function: RET.
    // Top-of-stack contains the caller IP.
    {0, {0xc3}, 1, 0}
#endif
};

inline void* GetPC(const ucontext_t& signal_ucontext) {
  // See comment above struct CallUnrollInfo.  Only try instruction
  // flow matching if both eip and esp looks reasonable.
  const int eip = signal_ucontext.uc_mcontext.gregs[REG_EIP];
  const int esp = signal_ucontext.uc_mcontext.gregs[REG_ESP];
  if ((eip & 0xffff0000) != 0 && (~eip & 0xffff0000) != 0 &&
      (esp & 0xffff0000) != 0) {
    char* eip_char = reinterpret_cast<char*>(eip);
    for (size_t i = 0; i < sizeof(callunrollinfo) / sizeof(*callunrollinfo);
         ++i) {
      if (!memcmp(eip_char + callunrollinfo[i].pc_offset, callunrollinfo[i].ins,
                  callunrollinfo[i].ins_size)) {
        // We have a match.
        void** retaddr = (void**)(esp + callunrollinfo[i].return_sp_offset);
        return *retaddr;
      }
    }
  }
  return (void*)eip;
}

#elif defined __linux__ && defined __powerpc64__

inline void* GetPC(const ucontext_t& signal_ucontext) {
  return reinterpret_cast<void*>(signal_ucontext.uc_mcontext.gp_regs[PT_NIP]);
}

#elif defined __linux__ && defined __powerpc__

inline void* GetPC(const ucontext_t& signal_ucontext) {
  return reinterpret_cast<void*>(signal_ucontext.uc_mcontext.regs->nip);
}

#elif defined OS_WINDOWS || defined OS_CYGWIN

// If this is ever implemented, probably the way to do it is to have
// profiler.cc use a high-precision timer via timeSetEvent:
//    http://msdn2.microsoft.com/en-us/library/ms712713.aspx
// We'd use it in mode TIME_CALLBACK_FUNCTION/TIME_PERIODIC.
// The callback function would be something like prof_handler, but
// alas the arguments are different: no ucontext_t!  I don't know
// how we'd get the PC (using StackWalk64?)
//    http://msdn2.microsoft.com/en-us/library/ms680650.aspx

inline void* GetPC(const ucontext_t& signal_ucontext) {
  ABSL_RAW_LOG(ERROR, "GetPC is not yet implemented on Windows\n");
  return nullptr;
}

#elif defined __akaros__

inline void* GetPC(const ucontext_t& signal_ucontext) {
  ABSL_RAW_LOG(ERROR, "GetPC is not yet implemented on Akaros\n");
  return nullptr;
}
#elif defined(__Fuchsia__)
inline void* GetPC(const ucontext_t& signal_ucontext) {
  ABSL_RAW_LOG(ERROR, "GetPC is not yet implemented on Fuchsia\n");
  return nullptr;
}

#elif defined(__myriad2__)
// Implementation omitted due to lack of ucontext_t.

#elif defined(__wasm__)
inline void* GetPC(const ucontext_t& signal_ucontext) {
  ABSL_RAW_LOG(ERROR, "GetPC is not yet implemented on WASM\n");
  return nullptr;
}

#else
#error Need to define GetPC() for your operating system; see profiler.cc

#endif

#endif  // THIRD_PARTY_GLOOP_BASE_GETPC_H_
