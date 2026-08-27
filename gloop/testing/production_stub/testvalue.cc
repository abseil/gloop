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

#include "gloop/testing/production_stub/testvalue.h"

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/log/vlog_is_on.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

namespace testing {
namespace testvalue {

ABSL_CONST_INIT static absl::Mutex map_lock(absl::kConstInit);

struct MapEntry {
  const size_t type_id;
  bool live ABSL_GUARDED_BY(map_lock);
  int active ABSL_GUARDED_BY(map_lock);
  const std::function<void(void*)> run_callback;

  MapEntry(size_t t, std::function<void(void*)> run_cb)
      : type_id(t), live(true), active(0), run_callback(std::move(run_cb)) {}

  // This type is neither copyable nor movable.
  MapEntry(const MapEntry&) = delete;
  MapEntry& operator=(const MapEntry&) = delete;

  ~MapEntry() = default;

  void MaybeDeleteThis() ABSL_EXCLUSIVE_LOCKS_REQUIRED(map_lock) {
    bool do_delete = !live && active == 0;
    if (do_delete) {
      map_lock.unlock();
      delete this;
      map_lock.lock();
    }
  }
};

typedef absl::flat_hash_map<std::string, MapEntry*> Map;

static Map* adjuster_map ABSL_GUARDED_BY(map_lock) = nullptr;

ABSL_CONST_INIT std::atomic<bool> internal_enable{false};

void Enable() { internal_enable.store(true, std::memory_order_relaxed); }

void InternalAdjust(absl::string_view label, size_t type_id, void* dst) {
  DCHECK(IsEnabled());
  MapEntry* entry = nullptr;
  {
    absl::MutexLock l(map_lock);
    if (adjuster_map != nullptr) {
      Map::const_iterator iter = adjuster_map->find(label);
      if (iter != adjuster_map->end() && iter->second != nullptr) {
        entry = iter->second;
        DCHECK(entry->live);
        ++entry->active;
      }
    }
  }

  if (entry != nullptr) {
    if (VLOG_IS_ON(1)) {
      VLOG(1) << "adjusting value for " << label;
    } else {
      LOG_FIRST_N(INFO, 1) << "adjusting value for the first time (use VLOG to "
                              "log further adjustments)";
    }
    CHECK_EQ(entry->type_id, type_id) << "type mismatch for label " << label;
    entry->run_callback(dst);

    absl::MutexLock l(map_lock);
    --entry->active;
    entry->MaybeDeleteThis();
  }
}

static bool AllCallsDone(MapEntry* entry)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(map_lock) {
  return entry->active == 0;
}

// Deletes the given MapEntry object. `entry` must no longer be referenced
// in the adjustor_map. Note: This call will temporarily release `map_lock`
// while deleting the entry.
static void InternalDeleteEntry(MapEntry* entry)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(map_lock) {
  if (entry == nullptr) return;
  map_lock.Await(absl::Condition(AllCallsDone, entry));
  DCHECK(entry->live);
  entry->live = false;
  entry->MaybeDeleteThis();
}

// Removes the entry for the given label from the adjustor_map, and returns the
// entry if one was found. It is the caller's responsibility to delete the
// entry.
static MapEntry* InternalClear(absl::string_view label)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(map_lock) {
  Map::iterator iter = adjuster_map->find(label);
  if (iter != adjuster_map->end() && iter->second != nullptr) {
    MapEntry* entry = iter->second;
    iter->second = nullptr;
    return entry;
  }
  return nullptr;
}

void InternalSetCallback(absl::string_view label, size_t type_id,
                         std::function<void(void*)> run_callback) {
  VLOG(1) << "setting adjuster for " << label;
  CHECK(IsEnabled()) << "Did not call testing::testvalue::Enable";
  MapEntry* old_entry = nullptr;
  absl::MutexLock l(map_lock);
  if (adjuster_map == nullptr) {
    adjuster_map = new Map;
  }
  for (;;) {
    MapEntry** slot = &(*adjuster_map)[label];
    if (*slot != nullptr) {
      // Hold on to the old entry, so that we can replace it in the map before
      // we release the lock to delete it.
      old_entry = InternalClear(label);
      continue;
    }
    *slot = new MapEntry(type_id, std::move(run_callback));
    break;
  }
  InternalDeleteEntry(old_entry);
}

void Clear(absl::string_view label) {
  VLOG(1) << "clearing adjuster for " << label;
  absl::MutexLock l(map_lock);
  if (adjuster_map != nullptr) {
    MapEntry* old_entry = InternalClear(label);
    InternalDeleteEntry(old_entry);
  }
}

void Reset() {
  absl::MutexLock l(map_lock);
  if (adjuster_map != nullptr) {
    for (;;) {
      // Collect live map entries. Note that some of the adjuster_map entries
      // may have been cleared by Clear().
      std::vector<std::string> labels;
      for (const auto& [label, map_entry] : *adjuster_map) {
        if (map_entry != nullptr) labels.push_back(label);
      }
      if (!labels.empty()) {
        for (absl::string_view label : labels) {
          MapEntry* old_entry = InternalClear(label);
          InternalDeleteEntry(old_entry);
        }
        continue;
      }
      adjuster_map->clear();
      break;
    }
  }
}

}  // namespace testvalue
}  // namespace testing
