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

// Typically, these routines will all be os, and possibly processor,
// specific.  Every routine should thus be protected by ifdefs so
// that programs won't compile if these routines are run on a
// processor/OS that haven't been supported yet.
#include "gloop/base/user_name.h"

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include <string>

#if defined(__linux__) || defined(__APPLE__)
#include "gloop/base/nsscache.h"
#endif

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

#ifndef _WIN32

// ----------------------------------------------------------------------
// Internal helpers for UserName() and MyUserName()
//
//    These functions are used by UserName() and MyUserName() to cache
//    the current user's passwd entry. Calling geteuid() is a syscall,
//    and getpwuid_r() is quite slow (see below), so we cache
//    the result if we know we are looking for the current user.
//
//    For other users, they will be looked up via the NSSCache.
// ----------------------------------------------------------------------

// Implementation for a cached copy of the current user's UID, which is equal to
// 'effective_current_uid'. (Note that 'my_current_uid' is passed explicitly, so
// that the cache will be refreshed if the effective UID changes.)
//
// Caching is only implemented for platforms with a meaningful getpwuid, which
// excludes Windows.
ABSL_CONST_INIT static absl::Mutex effective_user_name_lock(absl::kConstInit);

struct UserNameCacheEntry {
  uid_t uid;
  std::string name;
};
static UserNameCacheEntry* effective_user_name
    ABSL_GUARDED_BY(effective_user_name_lock) = nullptr;

static std::string UserName_UnCached(const uid_t uid) {
#if defined(__linux__) || (defined(__APPLE__))
  std::string username;
  if (LookupNameByUID(uid, &username)) {
    return username;
  } else {
    return std::string();
  }
#else
  // Maybe there are ways to do the same on other OS's, but let's worry
  // about that when we really care.
  ABSL_RAW_LOG(WARNING, "UserName(), OS not supported");
  return "";
#endif
}

static std::string MyUserName_Cached(const uid_t current_effective_uid) {
  absl::MutexLock l(effective_user_name_lock);
  if (effective_user_name == nullptr ||
      effective_user_name->uid != current_effective_uid) {
    // Cache miss
    delete effective_user_name;
    effective_user_name = new UserNameCacheEntry{
        current_effective_uid, UserName_UnCached(current_effective_uid)};
  }
  return effective_user_name->name;
}

// ----------------------------------------------------------------------
// UserName()
//
//    Returns the user name of user specified by 'uid'. We should not
//    log errors from this function, because it can be called in the
//    context of a "logging" function, which might hold on to a
//    mutex. We do not want to have to try and hold on to the same
//    mutex and get into a deadlock.
// ----------------------------------------------------------------------
std::string UserName(const uid_t uid) {
  // Avoid calling getpwuid if root because getpwuid loads in
  // shared libraries, which chew up a bunch of our address space.
  if (uid == 0) {
    return "root";
  }
  if (uid == geteuid()) {
    // Attempt to return a cached value.
    return MyUserName_Cached(uid);
  }
  return UserName_UnCached(uid);
}

// ----------------------------------------------------------------------
// MyUserName()
//   Like UserName(geteuid()), but often much faster.
// ----------------------------------------------------------------------

// We keep a one-element cache that maps from uid to user name.
// (We keep the uid as a key for the cache so that the correct
// response is returned even if the process switches user ids.)

std::string MyUserName() {
  const uid_t uid = geteuid();
  return MyUserName_Cached(uid);
}

#else                                 // _WIN32

std::string MyUserName() {
#pragma comment(lib, "advapi32.lib")  // Has GetUserName() implementation.
  char buf[256];
  DWORD size_buf = sizeof(buf);
  if (GetUserNameA(buf, &size_buf)) {
    return std::string(&buf[0]);
  }
  ABSL_RAW_LOG(WARNING, "UserName(), name too long");
  return std::string();
}

#endif  // _WIN32
