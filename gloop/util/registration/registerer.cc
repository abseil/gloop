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

#include "gloop/util/registration/registerer.h"

#include <algorithm>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

namespace util_registration {
namespace internal {

const Registry* Registry::registries_list_ = nullptr;
ABSL_CONST_INIT absl::Mutex Registry::registries_mutex_(absl::kConstInit);

Registry::Registry() {
  absl::MutexLock lock(registries_mutex_);
  registries_next_ = registries_list_;
  registries_list_ = this;
}

bool Registry::Insert(absl::string_view name, void* object,
                      absl::string_view filename) {
  std::string canonical_filename = std::string(filename);
  auto [iter, success] = objects_.try_emplace(name, object, canonical_filename);
  if (!success) {
    // Object was registered already. Make sure it was in the same file.
    CHECK_EQ(iter->second.filename, canonical_filename)
        << ": Object " << name
        << " is defined in different files. Please fix the name conflict.";
    return false;
  }
  return true;
}

void* Registry::Erase(absl::string_view name) {
  void* object = nullptr;
  auto iter = objects_.find(name);
  if (iter != objects_.end()) {
    object = iter->second.object;
    objects_.erase(iter);
  }
  return object;
}

const Registry::ObjectAndMetadata& Registry::Lookup(
    absl::string_view name) const {
  auto item = objects_.find(name);
  CHECK(item != objects_.end())
      << ": Object \"" << name << "\" hasn't been "
      << "defined; maybe you forgot to link the library containing this "
         "class, "
      << "or BUILD rule of the library is missing \"alwayslink = 1\"?"
         "";
  return item->second;
}

bool Registry::Contains(absl::string_view name) const {
  return objects_.find(name) != objects_.end();
}

std::vector<std::string> Registry::GetNames() const {
  std::vector<std::string> names;
  for (const auto& kv : objects_) {
    names.push_back(kv.first);
  }
  std::sort(names.begin(), names.end());
  return names;
}

/* static */ std::vector<const Registry*> Registry::GetAllRegistries() {
  absl::MutexLock lock(registries_mutex_);
  std::vector<const Registry*> return_value;
  for (const Registry* current = registries_list_; current;
       current = current->registries_next_) {
    return_value.push_back(current);
  }
  return return_value;
}

AliasRegistry::AliasRegistry() = default;
AliasRegistry::~AliasRegistry() = default;

void AliasRegistry::Insert(absl::string_view alias, absl::string_view name,
                           absl::string_view filename) {
  std::string canonical_filename = std::string(filename);
  auto [iter, success] = aliases_.try_emplace(alias, name, canonical_filename);
  if (!success) {
    // Alias was registered already. Make sure it was in the same file and
    // with the same name.
    CHECK_EQ(iter->second.second, canonical_filename)
        << ": Alias " << alias << " is defined in two different files. "
        << "Please fix the alias conflict.";
    CHECK_EQ(iter->second.first, name)
        << ": Alias " << alias << " was defined for two different names. "
        << "Please fix the alias conflict.";
  }
}

bool AliasRegistry::Contains(absl::string_view name) const {
  return aliases_.find(name) != aliases_.end();
}

const std::string& AliasRegistry::Lookup(absl::string_view alias) const {
  auto item = aliases_.find(alias);
  CHECK(item != aliases_.end())
      << ": Alias " << alias
      << " hasn't been defined; maybe you forgot to link the library containing"
      << " this class, or BUILD rule of the library is missing"
      << " \"alwayslink = 1\"?";
  // "item" looks like this: &[key, [name, filename]]
  return item->second.first;
}

void AliasRegistry::GetAliases(std::vector<std::string>* aliases) const {
  CHECK(aliases);
  aliases->clear();
  for (auto& kv : aliases_) {
    aliases->push_back(kv.first);
  }
  std::sort(aliases->begin(), aliases->end());
}

}  // namespace internal
}  // namespace util_registration
