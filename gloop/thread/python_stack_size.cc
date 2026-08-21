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

#include "gloop/thread/python_stack_size.h"

#include <cstdlib>

#include "absl/base/attributes.h"
#include "absl/log/log.h"

namespace thread {
namespace python {

// Returns true if this code is linked against a hermetic python launcher
// which overrides this function.
extern "C" ABSL_ATTRIBUTE_WEAK bool PythonLauncherLinkage() { return false; }

// Returns requested_stack_size OR a possibly larger value if this is
// a non-test Python binary.  The library name is used in a log message.
size_t MaybeAdjustStackSize(const size_t requested_stack_size,
                            const char* requesting_library_name) {
  if (!PythonLauncherLinkage()) {
    return requested_stack_size;  // Not a Python binary.
  }

  // This value was chosen because 64k-128k defaults caused crashes on some
  // reasonably common Python callbacks.  b/22490382
  static const size_t kPythonMinProdStack = 240 * 1024;
  // Keep a smaller stack size for test code to help shake out problems.
  static const size_t kPythonMinTestStack = 180 * 1024;
  // A required test environment variable per <link>.
  // We explicitly chose NOT to key off TEST_SRCDIR as testing.pybase.basetest
  // sets that and gets used by non-test code.
  static bool is_a_test = (getenv("TEST_TMPDIR") != nullptr);

  // A value of 0 is allowed, it requests a huge default via thread.cc.
  if (requested_stack_size && requested_stack_size < kPythonMinProdStack) {
    if (is_a_test) {
      // Keep a smaller stack size for test code to help shake out problems.
      // This also allows C++ code being tested by a Python test to run with
      // the same setting a pure C++ application will have in production.
      LOG_EVERY_POW_2(INFO)
          << requesting_library_name << " thread stack size of "
          << requested_stack_size << " might be too small for Python callbacks."
          << " Crash? Read <link>.";
      if (requested_stack_size < kPythonMinTestStack) {
        LOG_FIRST_N(INFO, 1)
            << requesting_library_name << " thread stack size increased to "
            << kPythonMinTestStack << " for test Python use.";
        return kPythonMinTestStack;
      }
      return requested_stack_size;
    } else {
      // Raise the stack size in non-test Python applications.  This saves a
      // lot of extra boilerplate code for the common Python use case.
      LOG_FIRST_N(INFO, 1) << requesting_library_name
                           << " thread stack size increased to "
                           << kPythonMinProdStack
                           << " for non-test Python use.";
      return kPythonMinProdStack;
    }
  }  // end if stack size appears small.

  return requested_stack_size;
}

}  // end namespace python
}  // end namespace thread
