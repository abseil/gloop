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

// Template class for doing different kinds of NSS lookups and caching them

#include "gloop/base/nsscache.h"

#include <assert.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/thread_annotations.h"
#include "absl/debugging/leak_check.h"
#include "absl/flags/flag.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

ABSL_FLAG(int32_t, nsscache_timeout, 2,
          "How long by default to keep entries in the NSS cache (secs). "
          "<0 implies no ttl");

#define BASE_NSSCACHE_LOOKUP_REENTRANT 0

#if !BASE_NSSCACHE_LOOKUP_REENTRANT
template <class Data, typename Key>
using NSSFn = int (&)(Key, Data*, char*, size_t, Data**);
#else
template <class Data, typename Key>
using NSSFn = Data* (&)(Key);
#endif  // BASE_NSSCACHE_LOOKUP_REENTRANT

// NSSInfo is thread-compatible (but note that Lookup isn't const).
template <class Data, typename UserKey, typename Key, NSSFn<Data, Key>>
class NSSInfo {
 public:
  // Create an NSSInfo with the default cache ttl.
  NSSInfo()
      : data_(nullptr),
        ttl_(absl::Seconds(absl::GetFlag(FLAGS_nsscache_timeout))) {}

  NSSInfo(const NSSInfo&) = delete;
  NSSInfo& operator=(const NSSInfo&) = delete;

  ~NSSInfo() {}

  // Lookup Takes an appropriate "key" value, and looks up the
  // specified object in the cache or via NSS. Returns true if the
  // data() member has been bound to valid data.
  ABSL_MUST_USE_RESULT
  bool Lookup(const UserKey& key) ABSL_LOCKS_EXCLUDED(cache_lock_, getXY_lock_);

  std::shared_ptr<Data> data() const;

 private:
  struct CacheRec;

  // Functions for mapping between user-supplied keys and nss keys.
  static int MapNSSKey(int key) { return key; }
  static const char* MapNSSKey(const std::string& s) { return s.c_str(); }

  // TODO: Determine whether std::unordered_map would outperform
  // std::map.
  typedef std::map<UserKey, std::shared_ptr<CacheRec>> Cache;

  std::shared_ptr<CacheRec> data_;
  absl::Duration ttl_;
  static Cache* cache_ ABSL_PT_GUARDED_BY(cache_lock_);
  static absl::Mutex cache_lock_;
  static absl::Mutex getXY_lock_;
};

// Some systems don't have this field defined, so we need to check for it.
inline std::nullptr_t GetPasswdField(const void*) { return nullptr; }
template <class T>
inline decltype(&std::declval<T*>()->pw_passwd) GetPasswdField(T* p) {
  return &p->pw_passwd;
}
template <class T>
inline decltype(&std::declval<T*>()->gr_passwd) GetPasswdField(T* p) {
  return &p->gr_passwd;
}

// Some systems don't have this field defined, so we need to check for it.
inline std::nullptr_t GetGecosField(const void*) { return nullptr; }
template <class T>
inline decltype(&std::declval<T*>()->pw_gecos) GetGecosField(T* p) {
  return &p->pw_gecos;
}

static std::optional<std::string> ToString(const char* str) {
  if (str == nullptr) {
    return std::nullopt;
  }

  return str;
}

static char* ToStringPtr(std::optional<std::string>& str) {
  return str.has_value() ? &str.value()[0] : nullptr;
}

// Helper C++-style struct that mirrors the given C-style struct, but with the
// data owned in a safe manner.
template <class>
class OwnedData;

template <>
class OwnedData<passwd> {
 public:
  OwnedData() = default;

  void Init(const passwd& src) {
    pw_name_ = ToString(src.pw_name);
    if (char* const* p = GetPasswdField(&src)) {
      pw_passwd_ = ToString(*p);
    }
    pw_uid_ = src.pw_uid;
    pw_gid_ = src.pw_gid;
    if (char* const* p = GetGecosField(&src)) {
      pw_gecos_ = ToString(*p);
    }
    pw_dir_ = ToString(src.pw_dir);
    pw_shell_ = ToString(src.pw_shell);
  }

  void CopyTo(passwd& dst) {
    dst.pw_name = ToStringPtr(pw_name_);
    if (char** p = GetPasswdField(&dst)) {
      *p = ToStringPtr(pw_passwd_);
    }
    dst.pw_uid = pw_uid_;
    dst.pw_gid = pw_gid_;
    if (char** p = GetGecosField(&dst)) {
      *p = ToStringPtr(pw_gecos_);
    }
    dst.pw_dir = ToStringPtr(pw_dir_);
    dst.pw_shell = ToStringPtr(pw_shell_);
  }

 private:
  std::optional<std::string> pw_name_;
  std::optional<std::string> pw_passwd_;
  uid_t pw_uid_;
  gid_t pw_gid_;
  std::optional<std::string> pw_gecos_;
  std::optional<std::string> pw_dir_;
  std::optional<std::string> pw_shell_;

  OwnedData(const OwnedData&) = delete;
  OwnedData& operator=(const OwnedData&) = delete;
};

template <>
class OwnedData<group> {
 public:
  OwnedData() = default;

  void Init(const group& src) {
    gr_name_ = ToString(src.gr_name);
    if (char* const* p = GetPasswdField(&src)) {
      gr_passwd_ = ToString(*p);
    }
    gr_gid_ = src.gr_gid;

    gr_mem_data_.clear();
    for (char* const* s = src.gr_mem; s != nullptr && *s != nullptr; ++s) {
      gr_mem_data_.push_back(ToString(*s));
    }

    gr_mem_.clear();
    gr_mem_.reserve(gr_mem_data_.size() + 1);
    for (std::optional<std::string>& s : gr_mem_data_) {
      gr_mem_.push_back(ToStringPtr(s));
    }
    gr_mem_.push_back(nullptr);
  }

  void CopyTo(group& dst) {
    dst.gr_name = ToStringPtr(gr_name_);
    if (char** p = GetPasswdField(&dst)) {
      *p = ToStringPtr(gr_passwd_);
    }
    dst.gr_gid = gr_gid_;
    dst.gr_mem = gr_mem_.data();
  }

 private:
  std::optional<std::string> gr_name_;
  std::optional<std::string> gr_passwd_;
  gid_t gr_gid_;
  std::vector<std::optional<std::string>> gr_mem_data_;
  std::vector<char*> gr_mem_;

  OwnedData(const OwnedData&) = delete;
  OwnedData& operator=(const OwnedData&) = delete;
};

template <class Data, typename UserKey, typename Key, NSSFn<Data, Key> LookupFn>
struct NSSInfo<Data, UserKey, Key, LookupFn>::CacheRec {
  bool valid = false;
  Data dbdata;
  std::string buffer;
  OwnedData<Data> owned_data;
  absl::Time expired;
};

template <class Data, typename UserKey, typename Key, NSSFn<Data, Key> LookupFn>
std::shared_ptr<Data> NSSInfo<Data, UserKey, Key, LookupFn>::data() const {
  if (data_ == nullptr) return nullptr;
  // Using the aliasing constructor to share the ownership with the cache entry.
  return std::shared_ptr<Data>(data_, &data_->dbdata);
}

template <class Data, typename UserKey, typename Key, NSSFn<Data, Key> LookupFn>
bool NSSInfo<Data, UserKey, Key, LookupFn>::Lookup(const UserKey& key) {
  {
    absl::MutexLock l(cache_lock_);
    if (!cache_) cache_ = new Cache;

    // Free our existing record
    data_.reset();

    // Look in the cache
    typename Cache::iterator it = cache_->find(key);
    if (it != cache_->end()) {
      // We found the entry in the cache. Is it fresh?
      const std::shared_ptr<CacheRec>& rec = it->second;
      absl::Time now = absl::Now();
      if (now > rec->expired) {
        // Erase the expired cache entry and fall through to create a new one
        cache_->erase(it);
      } else if (rec->valid) {
        data_ = rec;
        return true;  // valid cache hit
      } else {
        return false;  // invalid cache hit
      }
    }
  }

  // Not found, or stale - look it up

  auto rec = std::make_shared<CacheRec>();
  Key nsskey = MapNSSKey(key);

#if !BASE_NSSCACHE_LOOKUP_REENTRANT
  Data* res = nullptr;
  rec->buffer.resize(1024);
  do {
    int bufsize = rec->buffer.size();
    int err;
    {
      // NSS functions aren't thread-safe in statically-linked
      // applications - NSS pulls in its own dynamic copy of libc, and
      // the pthread_mutex_lock() contention path appears to blow up
      // due to the NSS library using a different libc from the rest
      // of the process.
      absl::MutexLock l(getXY_lock_);
      // See BUG 1378718 for details. this should disable checks for leaks seen
      // in nss_ldap functions (ber_memcalloc and ber_memalloc was leaking)
      {
        absl::LeakCheckDisabler disabler;
        err = LookupFn(nsskey, &rec->dbdata,
                       const_cast<char*>(rec->buffer.data()), bufsize, &res);
      }
    }
    if (res) {
      rec->valid = true;
    } else {
      if (err != ERANGE) break;

      // We need a bigger buffer: since the cost of doing these
      // lookups sometimes involves significant disk I/O, if the
      // buffer is less than 512 KB, we grow by 8X (1 KB, 8 KB, 64K, 512 KB).
      // Thereafter, we just double in size (1 MB, 2 MB, etc.).
      const int growth_factor = bufsize < (512 << 10) ? 8 : 2;
      if (bufsize >= std::numeric_limits<int>::max() / growth_factor) {
        // Keep the error and avoid integer overflow or unbounded memory growth
        break;
      }

      rec->buffer.resize(bufsize * growth_factor);
    }
  } while (!res);

#else
  Data* src = LookupFn(nsskey);
  if (src != nullptr) {
    // Copy over TLS data into our own heap structure so that we can safely
    // share it globally across threads.
    rec->owned_data.Init(*src);
    rec->owned_data.CopyTo(rec->dbdata);
    rec->valid = true;
  }

#endif  // BASE_NSSCACHE_LOOKUP_REENTRANT

  // We got it

  rec->expired = absl::Now() + ttl_;

  {
    absl::MutexLock l(cache_lock_);
    // Check for races
    if (cache_->find(key) == cache_->end()) {
      // Up to now, *rec was modified w/o lock. Hereafter, *rec is protected
      // by cache_lock_.
      // Transfer original ref count to cache pointer
      cache_->emplace(key, rec);
    }
  }

  if (rec->valid) {
    data_ = rec;
    return true;
  }

  return false;
}

template <class Data, typename UserKey, typename Key, NSSFn<Data, Key> LookupFn>
typename NSSInfo<Data, UserKey, Key, LookupFn>::Cache*
    NSSInfo<Data, UserKey, Key, LookupFn>::cache_;

template <class Data, typename UserKey, typename Key, NSSFn<Data, Key> LookupFn>
ABSL_CONST_INIT absl::Mutex NSSInfo<Data, UserKey, Key, LookupFn>::cache_lock_(
    absl::kConstInit);
template <class Data, typename UserKey, typename Key, NSSFn<Data, Key> LookupFn>
ABSL_CONST_INIT absl::Mutex NSSInfo<Data, UserKey, Key, LookupFn>::getXY_lock_(
    absl::kConstInit);

#if !BASE_NSSCACHE_LOOKUP_REENTRANT
// Instantiations for common NSS lookup operations
typedef NSSInfo<struct passwd, std::string, const char*, getpwnam_r> User;
typedef NSSInfo<struct group, std::string, const char*, getgrnam_r> Group;
typedef NSSInfo<struct passwd, uid_t, uid_t, getpwuid_r> UserByID;
typedef NSSInfo<struct group, gid_t, gid_t, getgrgid_r> GroupByID;

#else
typedef NSSInfo<struct passwd, std::string, const char*, getpwnam> User;
typedef NSSInfo<struct group, std::string, const char*, getgrnam> Group;
typedef NSSInfo<struct passwd, uid_t, uid_t, getpwuid> UserByID;
typedef NSSInfo<struct group, gid_t, gid_t, getgrgid> GroupByID;

#endif  // BASE_NSSCACHE_LOOKUP_REENTRANT

bool LookupNameByUID(uid_t uid, std::string* username) {
  UserByID user;
  if (user.Lookup(uid)) {
    std::string name = user.data()->pw_name;
    *username = name;
    return true;
  }
  return false;
}

bool LookupGroupNameByGID(gid_t gid, std::string* groupname) {
  GroupByID group;
  if (group.Lookup(gid)) {
    std::string name = group.data()->gr_name;
    *groupname = name;
    return true;
  }
  return false;
}

bool LookupUIDByName(absl::string_view username, uid_t* uid) {
  User user;
  if (user.Lookup(std::string(username))) {
    *uid = user.data()->pw_uid;
    return true;
  }
  return false;
}

bool LookupGIDByGroupName(absl::string_view groupname, gid_t* gid) {
  Group group;
  if (group.Lookup(std::string(groupname))) {
    *gid = group.data()->gr_gid;
    return true;
  }
  return false;
}

std::shared_ptr<passwd> LookupUserByUID(uid_t uid) {
  UserByID user;
  if (user.Lookup(uid)) return user.data();
  return nullptr;
}

std::shared_ptr<passwd> LookupUserByName(absl::string_view username) {
  User user;
  if (user.Lookup(std::string(username))) return user.data();
  return nullptr;
}

std::shared_ptr<group> LookupGroupByGID(gid_t gid) {
  GroupByID group;
  if (group.Lookup(gid)) return group.data();
  return nullptr;
}

std::shared_ptr<group> LookupGroupByGroupName(absl::string_view groupname) {
  Group group;
  if (group.Lookup(std::string(groupname))) return group.data();
  return nullptr;
}
