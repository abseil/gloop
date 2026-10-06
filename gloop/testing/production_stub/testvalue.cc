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

#include "gloop/testing/production_stub/testvalue.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"
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

// A label paired with its precomputed hash, so that when
// LabelFilter::MayContain returns true (or in Clear) we do not hash the label a
// second time for adjuster_map.
struct HashedLabel {
  explicit HashedLabel(absl::string_view l) : label(l), hash(absl::HashOf(l)) {}
  // Implicit for heterogeneous equality lookup in Map (via StringEq).
  operator absl::string_view() const { return label; }

  absl::string_view label;
  size_t hash;
};

struct LabelHash {
  using is_transparent = void;
  size_t operator()(absl::string_view label) const {
    return absl::HashOf(label);
  }
  size_t operator()(HashedLabel label) const { return label.hash; }
};

typedef absl::flat_hash_map<std::string, MapEntry*, LabelHash> Map;

static Map* adjuster_map ABSL_GUARDED_BY(map_lock) = nullptr;

// A saturating counting filter over the hashes of the labels that currently
// have an adjuster in adjuster_map: each slot counts the labels that hash to
// it, saturating at UINT8_MAX (after which it is never decremented). This lets
// InternalAdjust() skip labels without an adjuster (the overwhelmingly common
// case) with a single load, without contending on map_lock, while fitting in
// two dedicated cache lines.
class alignas(ABSL_CACHELINE_SIZE) LabelFilter {
 public:
  constexpr LabelFilter() = default;

  // Returns false if `label` definitely has no adjuster; true if it may have
  // one.
  bool MayContain(HashedLabel label) const {
    // A relaxed load suffices: all updates to a slot are serialized under
    // `map_lock`, and keep the slot > 0 while `label` is registered (any
    // SetCallback(label) either writes > 0 or synchronizes via `map_lock` with
    // the earlier Add() that did). Thus, if SetCallback(label) happens-before
    // Adjust(label) and Adjust(label) happens-before Clear(label), write-read
    // and read-write coherence on the slot guarantee this load sees a value
    // > 0; the caller then locks `map_lock` to synchronize access to the map.
    // Using `relaxed` also avoids a spurious TSAN happens-before edge from
    // Clear() (when Remove() stores 0) to subsequent Adjust() calls that miss
    // in the filter.
    return Slot(label).load(std::memory_order_relaxed) != 0;
  }

  void Add(HashedLabel label) ABSL_EXCLUSIVE_LOCKS_REQUIRED(map_lock) {
    std::atomic<uint8_t>& slot = Slot(label);
    if (uint8_t count = slot.load(std::memory_order_relaxed);
        count < std::numeric_limits<uint8_t>::max()) {
      slot.store(count + 1, std::memory_order_relaxed);
    }
  }

  void Remove(HashedLabel label) ABSL_EXCLUSIVE_LOCKS_REQUIRED(map_lock) {
    std::atomic<uint8_t>& slot = Slot(label);
    if (uint8_t count = slot.load(std::memory_order_relaxed);
        count < std::numeric_limits<uint8_t>::max()) {
      DCHECK_GT(count, 0);
      slot.store(count - 1, std::memory_order_relaxed);
    }
  }

 private:
  static constexpr size_t kNumSlots = 128;

  const std::atomic<uint8_t>& Slot(HashedLabel label) const {
    return slots_[label.hash % kNumSlots];
  }
  std::atomic<uint8_t>& Slot(HashedLabel label) {
    return slots_[label.hash % kNumSlots];
  }

  std::atomic<uint8_t> slots_[kNumSlots] = {};
};

ABSL_CONST_INIT static LabelFilter label_filter;

ABSL_CONST_INIT std::atomic<bool> internal_enable{false};

void Enable() { internal_enable.store(true, std::memory_order_relaxed); }

void InternalAdjust(absl::string_view label, size_t type_id, void* dst) {
  DCHECK(IsEnabled());
  const HashedLabel hashed_label(label);
  if (!label_filter.MayContain(hashed_label)) return;

  MapEntry* entry = nullptr;
  {
    absl::MutexLock l(map_lock);
    if (adjuster_map != nullptr) {
      Map::const_iterator iter = adjuster_map->find(hashed_label);
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
  const HashedLabel hashed_label(label);
  Map::iterator iter = adjuster_map->find(hashed_label);
  if (iter != adjuster_map->end() && iter->second != nullptr) {
    MapEntry* entry = iter->second;
    iter->second = nullptr;
    label_filter.Remove(hashed_label);
    return entry;
  }
  return nullptr;
}

void InternalSetCallback(absl::string_view label, size_t type_id,
                         std::function<void(void*)> run_callback) {
  VLOG(1) << "setting adjuster for " << label;
  CHECK(IsEnabled()) << "Did not call testing::testvalue::Enable";
  absl::MutexLock l(map_lock);
  if (adjuster_map == nullptr) {
    adjuster_map = new Map;
  }
  // Replace any old entry in a single step, without updating the filter, so
  // that concurrent Adjust() calls observe either the old or the new adjuster.
  MapEntry** slot = &(*adjuster_map)[label];
  MapEntry* old_entry =
      std::exchange(*slot, new MapEntry(type_id, std::move(run_callback)));
  if (old_entry == nullptr) {
    label_filter.Add(HashedLabel(label));
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
