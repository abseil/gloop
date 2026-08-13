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

#include <assert.h>

#include <cstddef>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/debugging/leak_check.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "gloop/base/signal-handler.h"
#include "gloop/base/static_threadlocal.h"
#include "gloop/thread/threadlocal-internal.h"

namespace thread {
namespace local {
namespace internal {

STATIC_THREAD_LOCAL(ThreadInfo, per_thread);

#ifdef ABSL_HAVE_THREAD_LOCAL
// static
thread_local absl::Span<Instance*> Var::per_thread_instances_;
#else
absl::Span<Instance*>* Var::PerThreadInstances() {
  return &per_thread.pointer()->per_thread_instances_;
}
#endif

ABSL_CONST_INIT static absl::Mutex global_lock(absl::kConstInit);
static bool inited = false;

Instance::~Instance() {}

static void DeleteList(const std::vector<const Instance*>& del) {
  for (const Instance* instance : del) {
    instance->Unref();
  }
}

ThreadInfo::~ThreadInfo() {
  std::vector<const Instance*> del;
  {
    absl::Span<Instance*>* instances;
#ifdef ABSL_HAVE_THREAD_LOCAL
    instances = Var::PerThreadInstances();
#else
    // Do not use Var::PerThreadInstances() as per_thread is being destructed.
    instances = &per_thread_instances_;
#endif
    absl::MutexLock l(global_lock);
    if (!instances->empty()) {
      DCHECK_EQ(instances->size(), items_.size());
      DCHECK(instances->empty() || instances->data() == items_.data());
      *instances = {};
    }
    for (size_t i = 0; i < items_.size(); i++) {
      Instance* x = items_[i];
      if (x != nullptr) {
        DCHECK_EQ(x->thread_, this);
        // Remove from var
        Var::List::erase(x);
        items_[i] = nullptr;
        del.push_back(x);
      }
    }
  }
  DeleteList(del);
}

static std::vector<int>* free_ids = nullptr;  // To allow id reuse
static size_t next_id = 0;

// Thread local variable creation:
// 1. Save away type information
// 2. Initialize module if necessary
// 3. Allocate numeric id for this variable
Var::Var(Instance* prototype) : prototype_(prototype), instances_(nullptr) {
  absl::MutexLock l(global_lock);

  // Initialize the module if necessary
  if (!inited) {
    free_ids = new std::vector<int>;
    next_id = 0;
    inited = true;
  }

  // Allocate id
  if (free_ids->empty()) {
    id_ = next_id++;
  } else {
    id_ = free_ids->back();
    free_ids->pop_back();
  }
}

// Thread local variable destruction:
// 1. Collect all per-thread instances of this variable
// 2. Stash away the id so it can be reused
// 3. Delete all per-thread instances that were collected earlier
Var::~Var() {
  global_lock.lock();
  if (base::ProcessIsDying()) {
    // This could be a destructor for a Var inside a static ThreadLocal.  If
    // exit() has been called (or a fatal error has been triggered), we don't
    // delete the per-thread instances or the prototype. Otherwise, a thread
    // that's live at the point of exit() could call ThreadLocal::pointer(), and
    // access the deleted variable.
    List* instances = instances_;
    global_lock.unlock();
    if (instances != nullptr) {
      absl::IgnoreLeak(instances);
    }
    absl::IgnoreLeak(prototype_);
  } else {
    std::vector<const Instance*> del;
    if (instances_ != nullptr) {
      for (List::const_iterator iter = instances_->begin();
           iter != instances_->end(); ++iter) {
        const Instance* x = &(*iter);
        ThreadInfo* t = x->thread_;
        DCHECK_EQ(x, t->items_[id_]);
        del.push_back(x);
        t->items_[id_] = nullptr;
      }
      delete instances_;
      instances_ = nullptr;
    }
    free_ids->push_back(id_);
    global_lock.unlock();
    DeleteList(del);
    prototype_->Unref();
  }
}

// Slow path access to thread-local variable
// 1. Enlarge items/types array for this thread if necessary
// 2. Allocate per-thread instance of this variable
void* Var::SlowGet(absl::Span<Instance*>* instances) {
  ThreadInfo* t = per_thread.pointer();
  Instance* x = prototype_->Clone();
  x->thread_ = t;

  absl::MutexLock l(global_lock);
  const size_t id = id_;
  // Store pointer to x in thread's items_ array
  if (id >= instances->size()) {
    // ThreadInfo does not contain large enough arrays.
    t->items_.resize(id + 1, nullptr);
    *instances = absl::MakeSpan(t->items_);
  }
  assert(t->items_[id] == nullptr);
  t->items_[id] = x;

  // Store x in list of instances for this var
  if (instances_ == nullptr) {
    instances_ = new List;
  }
  instances_->push_back(x);

  return x->ptr_;
}

void Var::CopyInstances(std::vector<const Instance*>* copy) const {
  absl::ReaderMutexLock l(global_lock);
  if (instances_ == nullptr) {
    return;
  }
  copy->reserve(instances_->size());
  for (List::const_iterator p = instances_->begin(); p != instances_->end();
       ++p) {
    p->Ref();
    copy->push_back(&(*p));
  }
}
}  // end namespace internal
}  // end namespace local
}  // end namespace thread
