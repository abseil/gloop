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

// Template class for doing different kinds of NSS lookups and caching them
// Note: This library calls the underlying NSS routines, so gives Prod
// UIDs/GIDs in Prod and Corp UIDs/GIDs when run in Corp.

#ifndef THIRD_PARTY_GLOOP_BASE_NSSCACHE_H_
#define THIRD_PARTY_GLOOP_BASE_NSSCACHE_H_

#include <grp.h>
#include <pwd.h>

#include <cstdint>
#include <memory>
#include <string>

#include "absl/flags/flag.h"

#ifndef SWIG
extern absl::Flag<int32_t> FLAGS_nsscache_timeout;
#endif

bool LookupNameByUID(uid_t uid, std::string* username);

bool LookupGroupNameByGID(gid_t gid, std::string* groupname);

// Return the POSIX passwd struct for the given uid, or null if not found.
std::shared_ptr<passwd> LookupUserByUID(uid_t uid);

// Return the POSIX group struct for the given gid, or null if not found.
std::shared_ptr<group> LookupGroupByGID(gid_t gid);

bool LookupUIDByName(const std::string& username, uid_t* uid);

bool LookupGIDByGroupName(const std::string& groupname, gid_t* gid);

// Return the POSIX passwd struct for the given username, or null if not found.
std::shared_ptr<passwd> LookupUserByName(const std::string& username);

// Return the POSIX group struct for the given groupname, or null if not found.
std::shared_ptr<group> LookupGroupByGroupName(const std::string& groupname);

#endif  // THIRD_PARTY_GLOOP_BASE_NSSCACHE_H_
