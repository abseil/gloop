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

// Combining linkstatic and linkshared unfortunately allows multiple copies of
// base to be instantiated.  This test forces such a configuration, ensuring
// that ThreadIdentities are not shared between the two modules.

#include <dlfcn.h>
#include <stdio.h>

#include "absl/base/internal/thread_identity.h"
#include "absl/log/check.h"
#include "absl/synchronization/internal/create_thread_identity.h"

// We use a define so that this file can be built both as the test and as a
// minimal shared library which forwards a call into its copy of base.
#ifndef ABSL_THREADIDENTITYTEST_SHARED_MAIN
// When built as libthreadidentitytest_shared_lib.so

extern "C" void* ExportableCurrentThreadIdentityIfPresent() {
  void* result = absl::base_internal::CurrentThreadIdentityIfPresent();
  return result;
}

extern "C" void* ExportableGetOrCreateCurrentThreadIdentity() {
  void* result =
      absl::synchronization_internal::GetOrCreateCurrentThreadIdentity();
  return result;
}

#else
// When built as thread-identity_test_shared

#include "gtest/gtest.h"

static const char* so_relative_path =
    "/_main/gloop/base/libthreadidentitytest_shared_lib.so";

class ThreadIdentitySharedLibraryTest : public ::testing::Test {
 public:
  virtual ~ThreadIdentitySharedLibraryTest() {}

  static void SetUpTestCase() {
    // Blaze bundled our shared object into runfiles, go find it.
    char so_path[512];
    snprintf(so_path, sizeof(so_path), "%s/%s", ::testing::SrcDir().c_str(),
             so_relative_path);
    void* handle = dlopen(so_path, RTLD_LOCAL | RTLD_NOW);
    CHECK(handle != nullptr) << "Unable to open test shared library.";
    exported_get_identity_function_ = reinterpret_cast<void* (*)()>(
        dlsym(handle, "ExportableCurrentThreadIdentityIfPresent"));
    CHECK(exported_get_identity_function_ != nullptr)
        << "Unable to find test library ThreadIdentity export.";
    exported_create_identity_function_ = reinterpret_cast<void* (*)()>(
        dlsym(handle, "ExportableGetOrCreateCurrentThreadIdentity"));
    CHECK(exported_create_identity_function_ != nullptr)
        << "Unable to find test library ThreadIdentity export.";
  }

 protected:
  // Forwards to CurrentThreadIdentity within our shared library.
  absl::base_internal::ThreadIdentity* ThreadIdentityOrNullFromSo() {
    return reinterpret_cast<absl::base_internal::ThreadIdentity*>(
        exported_get_identity_function_());
  }
  absl::base_internal::ThreadIdentity* GetOrCreateThreadIdentityFromSo() {
    return reinterpret_cast<absl::base_internal::ThreadIdentity*>(
        exported_create_identity_function_());
  }

 private:
  static void* (*exported_get_identity_function_)();
  static void* (*exported_create_identity_function_)();
};

void* (*ThreadIdentitySharedLibraryTest::exported_get_identity_function_)();
void* (*ThreadIdentitySharedLibraryTest::exported_create_identity_function_)();

TEST_F(ThreadIdentitySharedLibraryTest, IdentitiesAreUnique) {
  ASSERT_TRUE(
      absl::synchronization_internal::GetOrCreateCurrentThreadIdentity() !=
      GetOrCreateThreadIdentityFromSo());
  ASSERT_TRUE(absl::base_internal::CurrentThreadIdentityIfPresent() !=
              ThreadIdentityOrNullFromSo());
  ASSERT_TRUE(
      absl::synchronization_internal::GetOrCreateCurrentThreadIdentity() !=
      ThreadIdentityOrNullFromSo());
  ASSERT_TRUE(absl::base_internal::CurrentThreadIdentityIfPresent() !=
              GetOrCreateThreadIdentityFromSo());
}
#endif
