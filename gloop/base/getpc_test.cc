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

//
// This verifies that GetPC works correctly.  This test uses a minimum
// of Google infrastructure, to make it very easy to port to various
// O/Ses and CPUs and test that GetPC is working.

#include "gloop/base/getpc.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>  // for setitimer

// Needs to be volatile so compiler doesn't try to optimize it away
static volatile void* getpc_retval = nullptr;  // what GetPC returns
static volatile bool prof_handler_called = false;

static void prof_handler(int sig, siginfo_t*, void* signal_ucontext) {
  if (!prof_handler_called)
    getpc_retval = GetPC(*reinterpret_cast<ucontext_t*>(signal_ucontext));
  prof_handler_called = true;  // only store the retval once
}

static void RoutineCallingTheSignal() {
  struct sigaction sa;
  sa.sa_sigaction = prof_handler;
  sa.sa_flags = SA_RESTART | SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGPROF, &sa, nullptr) != 0) {
    perror("sigaction");
    exit(1);
  }

  struct itimerval timer;
  timer.it_interval.tv_sec = 0;
  timer.it_interval.tv_usec = 1000;
  timer.it_value = timer.it_interval;
  setitimer(ITIMER_PROF, &timer, nullptr);

  // Now we need to do some work for a while, that doesn't call any
  // other functions, so we can be guaranteed that when the SIGPROF
  // fires, we're the routine executing.
  int r = 0;
  for (int i = 0; !prof_handler_called; ++i) {
    for (int j = 0; j < i; j++) {
      r ^= i;
      r <<= 1;
      r ^= j;
      r >>= 1;
    }
  }

  // Now make sure the above loop doesn't get optimized out
  srand(r);
}

// This is an upper bound of how many bytes the instructions for
// RoutineCallingTheSignal might be.  There's probably a more
// principled way to do this, but I don't know how portable it would be.
// (The function is 372 bytes when compiled with -g on Mac OS X 10.4.
// I can imagine it would be even bigger in 64-bit architectures.)
const int kRoutineSize = 512 * sizeof(void*) / 4;  // allow 1024 for 64-bit

int main(int argc, char** argv) {
  RoutineCallingTheSignal();

  // Annoyingly, C++ disallows casting pointer-to-function to
  // pointer-to-object, so we use a C-style cast instead.
  char* expected = reinterpret_cast<char*>(&RoutineCallingTheSignal);
  volatile char* actual = static_cast<volatile char*>(getpc_retval);

  if (actual < expected || actual > expected + kRoutineSize) {
    printf("Test FAILED: actual PC: %p, expected PC: %p\n", actual, expected);
    return 1;
  } else {
    printf("PASS\n");
    return 0;
  }
}
