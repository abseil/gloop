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

// This header provides template/preprocessor code that allows creating static
// maps in a distributed (and thread-safe) fashion. Attempts to set different
// values for the same key are detected, and CHECK'ed for.
//
// The ability to build maps at compile time is useful as an add-on for
// class registration, when different actions need to be taken based on
// something other than strings. For example when different registered class
// needs to be used depending on the value of an enum.

// The example usage is as follows. Note that the first template parameter must
// be the type name of the new static map type itself, i.e. this uses the
// Curiously Recurring Template Pattern:
//
//   struct IntToStringMap : util_registration::StaticFlatHashMap<
//       IntToStringMap, int, std::string> {};
// ...
//   static const auto kUnused1 =
//       IntToStringMap::InsertValue(10, "A simple string");
//   static const auto kUnused2 =
//       IntToStringMap::InsertValue(20, "Another simple string");
// ...
//
//   const string* value = IntToStringMap::GetValue(10);
//   CHECK(value);
//
//   std::vector<int> keys;
//   IntToStringMap::GetKeys(&keys);
//
// Equivalently, one could use:
//
//   std::vector<int> keys = IntToStringMap::Keys();
//
// It is also possible to define a default value for the cases when a default
// makes sense. When the default is defined, GetValue(X) would transparently
// return it for all values of X for which a value was not explicitly set.
//
// The default is set using:
//
//   static const auto kUnused =
//       IntToStringMap::SetDefaultValue("default string");
//
// Also static sets are available:
//
//   static SetName : util_registration::StaticFlatHashSet<SetName, KeyType> {};
//   static const auto kUnused = SetName::InsertKey(KeyValue);
//
//   bool SetName::ContainsKey(KeyValue);
//   std::vector<KeyType> keys;
//   void SetName::GetKeys(&keys);  // Or SetName::Keys() as documented above.
//
// If ordering is required in the return value of Keys(), ordered static sets
// and maps can be defined as follows:
//
//   struct MapName :
//       util_registration::StaticMap<MapName, KeyType, ValueType> {};
//   struct SetName :
//       util_registration::StaticSet<SetName, KeyType, ValueType> {};
//
// There are deprecated macro-based interfaces. These are interchangeable with
// the above methods for defining and populating maps, e.g.:
//
//    DEFINE_STATIC_MAP(IntToStringMap, int, string);
//    SET_STATIC_MAP_VALUE(IntToStringMap, 10, "A simple string");
//    SET_STATIC_MAP_DEFAULT_VALUE(IntToStringMap, "default string");
//
//    DEFINE_STATIC_SET(SetName, KeyType);
//    SET_STATIC_SET_KEY(SetName, KeyValue);
//
#ifndef THIRD_PARTY_GLOOP_UTIL_REGISTRATION_STATIC_MAP_H_
#define THIRD_PARTY_GLOOP_UTIL_REGISTRATION_STATIC_MAP_H_

#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/absl_check.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/source_location.h"

namespace util_registration {

template <typename MapName, typename KeyType, typename ValueType,
          typename MapType =
              std::map<KeyType, std::pair<const char*, ValueType>, std::less<>>>
class StaticMapBase {
 public:
  // This type is neither copyable nor movable.
  StaticMapBase(const StaticMapBase&) = delete;
  StaticMapBase& operator=(const StaticMapBase&) = delete;

 public:
  template <typename Arg = const KeyType&>
  static const ValueType* GetValue(Arg&& key) {
    const MapType& internal_map = GetSingleton()->internal_map_;
    auto value_iter = internal_map.find(std::forward<Arg>(key));

    if (value_iter == internal_map.end()) {
      return GetSingleton()->default_value_.get();
    } else {
      return &(value_iter->second.second);
    }
  }

  static void GetKeys(std::vector<KeyType>* keys) {
    CHECK(keys);
    keys->clear();

    const MapType& internal_map = GetSingleton()->internal_map_;
    keys->reserve(internal_map.size());
    for (const auto& [key, _] : internal_map) {
      keys->push_back(key);
    }
  }

  static std::vector<KeyType> Keys() {
    std::vector<KeyType> keys;
    GetKeys(&keys);
    return keys;
  }

  // Do not call this outside of file scope.
  static bool InsertValue(
      KeyType key, ValueType value,
      const absl::SourceLocation& location = absl::SourceLocation::current()) {
    MapName* static_map = GetSingleton();
    absl::MutexLock l(&static_map->map_lock_);

    auto [it, inserted] = static_map->internal_map_.emplace(
        std::piecewise_construct, std::forward_as_tuple(std::move(key)),
        std::forward_as_tuple(location.file_name(), std::move(value)));
    ABSL_CHECK(inserted) << "Attempting to redefine value for key " << it->first
                         << ", that has been defined at " << it->second.first
                         << ", at " << location.file_name();
    return false;
  }

  static bool SetDefaultValue(
      ValueType value,
      const absl::SourceLocation& location = absl::SourceLocation::current()) {
    MapName* static_map = GetSingleton();
    absl::MutexLock l(static_map->map_lock_);

    ABSL_CHECK(static_map->default_value_.get() == nullptr)
        << "Attempting to redefine static map default value at "
        << location.file_name() << ", that has been defined at "
        << static_map->default_value_location_;
    static_map->default_value_ = std::make_unique<ValueType>(std::move(value));
    static_map->default_value_location_ = location.file_name();
    return false;
  }

 protected:
  StaticMapBase() = default;

 private:
  static MapName* GetSingleton() {
    static absl::NoDestructor<MapName> instance;
    return &*instance;
  }

  absl::Mutex map_lock_;
  MapType internal_map_;
  std::unique_ptr<ValueType> default_value_;
  const char* default_value_location_;
};

template <typename MapName, typename KeyType, typename ValueType>
using StaticMap = StaticMapBase<MapName, KeyType, ValueType>;

template <typename MapName, typename KeyType, typename ValueType>
using StaticFlatHashMap = StaticMapBase<
    MapName, KeyType, ValueType,
    absl::flat_hash_map<KeyType, std::pair<const char*, ValueType>>>;

template <typename SetName, typename KeyType,
          typename SetType = std::map<KeyType, const char*, std::less<>>>
class StaticSetBase {
 public:
  // This type is neither copyable nor movable.
  StaticSetBase(const StaticSetBase&) = delete;
  StaticSetBase& operator=(const StaticSetBase&) = delete;

 public:
  template <typename KeyArg = KeyType>
  static bool ContainsKey(const KeyArg& key) {
    const SetType& internal_set = GetSingleton()->internal_set_;
    auto key_iter = internal_set.find(key);
    return key_iter != internal_set.end();
  }

  static void GetKeys(std::vector<KeyType>* keys) {
    CHECK(keys);
    keys->clear();

    const SetType& internal_set = GetSingleton()->internal_set_;
    keys->reserve(internal_set.size());
    for (const auto& [key, _] : internal_set) {
      keys->push_back(key);
    }
  }

  static std::vector<KeyType> Keys() {
    std::vector<KeyType> keys;
    GetKeys(&keys);
    return keys;
  }

  // Do not call this outside of file scope.
  static bool InsertKey(KeyType key, const absl::SourceLocation& location =
                                         absl::SourceLocation::current()) {
    SetName* static_set = GetSingleton();
    absl::MutexLock l(static_set->set_lock_);

    auto [it, inserted] = static_set->internal_set_.insert(
        {std::move(key), location.file_name()});
    CHECK(inserted) << "Attempting to reinsert key " << it->first
                    << ", that has been defined at " << it->second << ", at "
                    << location.file_name();
    return false;
  }

 protected:
  StaticSetBase() = default;

 private:
  static SetName* GetSingleton() {
    static absl::NoDestructor<SetName> instance;
    return &*instance;
  }

  absl::Mutex set_lock_;
  SetType internal_set_;
};

template <typename SetName, typename KeyType>
using StaticSet = StaticSetBase<SetName, KeyType>;

template <typename SetName, typename KeyType>
using StaticFlatHashSet =
    StaticSetBase<SetName, KeyType, absl::flat_hash_map<KeyType, const char*>>;

}  // namespace util_registration

// Defines a static map.
#define DEFINE_STATIC_MAP(MapName, KeyType, ValueType) \
  class MapName                                        \
      : public util_registration::StaticMap<MapName, KeyType, ValueType> {}

// Defines a static map with a flat_hash_map as the underlying storage type.
#define DEFINE_STATIC_FLAT_HASH_MAP(MapName, KeyType, ValueType)      \
  class MapName                                                       \
      : public util_registration::StaticFlatHashMap<MapName, KeyType, \
                                                    ValueType> {}

// Sets value for a given key in map map_name (happens at the time of
// static initialization in the constructor of InsertValue used in
// the implementation).
#define SET_STATIC_MAP_VALUE(map_name, key, value)                      \
  ABSL_ATTRIBUTE_UNUSED static const auto STATIC_MAP_TEMP_OBJECT_NAME = \
      map_name::InsertValue(key, value);

// Sets default value for an undefined key in map map_name (happens at
// the time of static initialization in the constructor of
// DefaultInsertValue used in the implementation).
#define SET_STATIC_MAP_DEFAULT_VALUE(map_name, value)                   \
  ABSL_ATTRIBUTE_UNUSED static const auto STATIC_MAP_TEMP_OBJECT_NAME = \
      map_name::SetDefaultValue(value);

// Helper macros used to concatenate three strings.  STATIC_MAP_PASTE
// has to be defined in terms of a secondary macro __STATIC_MAP_PASTE
// as a workaround for a peculiarity of the preprocessor spec where a
// macro is not expanded if it's passed as a parameter to a
// parameterized macro which then uses a # or ## operator on it.
#define __STATIC_MAP_PASTE(a, b, c) a##b##_##c
#define STATIC_MAP_PASTE(a, b, c) __STATIC_MAP_PASTE(a, b, c)

// STATIC_MAP_STRINGIFY is a helper macro to effectively apply #
// operator to an arbitrary value (and also takes a macro as a param).
// It's implemented using a helper for reasons identical to
// STATIC_MAP_PASTE above.
#define STATIC_MAP_STRINGIFY_HELPER(x) #x
#define STATIC_MAP_STRINGIFY(x) STATIC_MAP_STRINGIFY_HELPER(x)

// Unique object name used for temp instances of InsertValue,
// SetDefaultValue. Including a counter is important to allow use
// of this macro nested inside multiple levels of macros.
#define STATIC_MAP_TEMP_OBJECT_NAME \
  STATIC_MAP_PASTE(obj_, __LINE__, __COUNTER__)

// Unique identifier generator macro. Including a counter is important
// to allow use of this macro nested inside multiple levels of macros.
#define STATIC_MAP_FILE_LINE \
  __FILE__                   \
  ":" STATIC_MAP_STRINGIFY(__LINE__) ":" STATIC_MAP_STRINGIFY(__COUNTER__)

// Defines a static set.
#define DEFINE_STATIC_SET(SetName, KeyType) \
  class SetName : public util_registration::StaticSet<SetName, KeyType> {}

// Defines a static set with a flat_hash_map as the underlying storage type.
#define DEFINE_STATIC_FLAT_HASH_SET(SetName, KeyType) \
  class SetName                                       \
      : public util_registration::StaticFlatHashSet<SetName, KeyType> {}

// Adds a key to a specific static set set_name. This is done at the
// time of static initialization in the constructor of KeyInserted.
#define SET_STATIC_SET_KEY(set_name, key)                               \
  ABSL_ATTRIBUTE_UNUSED static const auto STATIC_SET_TEMP_OBJECT_NAME = \
      set_name::InsertKey(key);

// Unique object name used for temp instances of
// InsertKey. Including a counter is important to allow use of this
// macro nested inside multiple levels of macros.
#define STATIC_SET_TEMP_OBJECT_NAME \
  STATIC_MAP_PASTE(obj_, __LINE__, __COUNTER__)

#endif  // THIRD_PARTY_GLOOP_UTIL_REGISTRATION_STATIC_MAP_H_
