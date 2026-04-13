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

// Unit test for thread guards.
//
// Surprisingly, I do this without death tests in the guard area.
// It is awkward to trap SIGSEGV in a safe fashion so instead I
// make system calls in the guard area and look for EFAULT returns.
//
// This relies on the stack growing down so watch out if you use an
// HPPA or something.  See stack_incr in GuardTestThread::DoStackTest.

#include <unistd.h>

#include <cstddef>

#include "absl/base/macros.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/flags.h"
#include "absl/log/log.h"
#include "gloop/base/address_is_readable.h"
#include "gloop/base/init_google.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"

// Thread which tests the size of its stack.
class GuardTestThread : public Thread {
 public:
  // After Start and Join, *ok is set to true if stack and guard sizes
  // were verified to be OK.
  GuardTestThread(int stack_size, int guard_size, bool* ok)
      : Thread(thread::Options()
                   .set_stack_size(stack_size)
                   .set_guard_size(guard_size),
               "guardtest"),
        requested_stack_size_(stack_size),
        requested_guard_size_(guard_size),
        ok_(ok) {
    CHECK_LT(0, guard_size) << "guard_size of 0 is not supported by test.";
    this->SetJoinable(true);
  }

  virtual void Run() { *ok_ = DoStackTest(); }

 private:
  // Returns true if sizes are OK.
  bool DoStackTest() {
    const char* stack_addr =
        static_cast<const char*>(__builtin_frame_address(0));

    // "Stuff" on the stack may have already consumed up to approximately
    // PTHREAD_STACK_MIN bytes of space.
    const int expected_stack_size = requested_stack_size_ - PTHREAD_STACK_MIN;
    const int expected_guard_size = requested_guard_size_;

    // Change this to 1 if on a machine where stack grows up.
    const int stack_incr = -1;

    /* There may an arbitrary amount of extra stack, but in these tests
       we assume that there will always be at least a bit of a guard.  */
    int stack_size = 0;
    while (base::AddressIsReadable(stack_addr + stack_size))
      stack_size += stack_incr;

    int guard_size = stack_incr;
    while (!base::AddressIsReadable(stack_addr + stack_size + guard_size) &&
           (guard_size * stack_incr) < requested_guard_size_)
      guard_size += stack_incr;

    /* normalize back to positive.  */
    stack_size *= stack_incr;
    guard_size *= stack_incr;

    LOG(INFO) << "detected available stack size " << stack_size
              << ", guard size >= " << guard_size;
    LOG(INFO) << "expected stack size >= " << expected_stack_size << ".  "
              << (stack_size >= expected_stack_size ? "OK" : "BAD");
    LOG(INFO) << "expected guard size >= " << expected_guard_size << ".  "
              << (guard_size >= expected_guard_size ? "OK" : "BAD");

    if (stack_size < expected_stack_size || guard_size < expected_guard_size)
      return false;
    return true;
  }

  int requested_stack_size_;
  int requested_guard_size_;
  bool* ok_;
};

bool TestOneSize(int stack_size, int guard_size) {
  LOG(INFO) << "Testing thread with stack_size = " << stack_size
            << ", guard_size = " << guard_size;

  bool ok;
  GuardTestThread thread(stack_size, guard_size, &ok);
  thread.Start();
  thread.Join();

  LOG(INFO) << (ok ? "PASSED" : "FAILED");
  return ok;
}

int main(int argc, char** argv) {
  InitGoogle(argv[0], &argc, &argv, true);

  int pagesize = getpagesize();
  bool any_failures = false;

  int sizes[] = {16, 256};
  for (size_t i = 0; i < ABSL_ARRAYSIZE(sizes); i++) {
    int n = sizes[i];

    if (!TestOneSize(n * pagesize, 1)) any_failures = true;

    if (!TestOneSize(n * pagesize, pagesize)) any_failures = true;

    // Would fail with NPTL if Thread didn't compensate.
    if (!TestOneSize(n * pagesize, (n / 2) * pagesize)) any_failures = true;

#if 0  // TODO: test linuxthreads, or enable after linuxthreads gone.
    // LinuxThreads refuses to set the guard attr to be larger than
    // the stack size, and NPTL fails to start the thread if the stack
    // is larger than the guard size, so the next two threads fail a
    // CHECK if Thread fails to compensate.
    if (!TestOneSize(n * pagesize, n * pagesize))
      any_failures = true;

    if (!TestOneSize(n * pagesize, (n + 16) * pagesize))
      any_failures = true;
#endif
  }

  return (any_failures ? 1 : 0);
}
