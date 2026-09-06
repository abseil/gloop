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

// Accessors for <asm/stat.h>.
// We define these in a separate file because you
// can't include both <sys/stat.h> and <asm/stat.h>
// in the same translation unit.

#include "gloop/base/linux_syscall_support_unittest_helper.h"

// Unlike x86 and ARM, asm/stat.h of PowerPC uses types defined elsewhere.
#ifdef __powerpc__
#include <sys/types.h>
#endif

#include <asm/stat.h>
#include <stddef.h>

size_t asm_stat_size() { return sizeof(struct stat); }
#if !defined(__x86_64__) && !defined(__aarch64__) && \
    !(defined(__riscv) && __riscv_xlen == 64)
size_t asm_stat64_size() { return sizeof(struct stat64); }
#endif
