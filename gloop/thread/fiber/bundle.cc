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

#include "gloop/thread/fiber/bundle.h"

#include <atomic>
#include <cstddef>
#include <limits>
#include <memory>
#include <utility>

#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "gloop/thread/fiber/channel.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/util/functional/with_context.h"

namespace thread {

Bundle::Bundle() : Bundle(Fiber::Current()->options()) {}

Bundle::Bundle(const FiberOptions& options)
    : bundle_fiber_(options, Fiber::Current()) {}

void Bundle::AddImpl(Invocable f) {
  DCHECK(IsDescendantAdd());
  Detach(internal::FiberHelpers::CreateChildFiber(&bundle_fiber_.GetFiber(),
                                                  std::move(f)));
}

// Used in dbg mode to ensure we do not allow cross-tree insertions.
bool Bundle::IsDescendantAdd() {
  Fiber* fiber = Fiber::Current();

  while (fiber != nullptr) {
    if (fiber == bundle_fiber_.GetFiber().parent()) return true;
    fiber = fiber->parent();
  }

  return false;  // Reached root.
}

// Below it is sufficient to Join/Cancel against our placeholder fiber as all
// fibers belonging to this bundle will be descendants of it.
void Bundle::JoinAll() {
  bundle_fiber_.Finish();
  bundle_fiber_.GetFiber().Join();
}

void Bundle::CancelAll() { bundle_fiber_.GetFiber().Cancel(); }

bool Bundle::Cancelled() const { return bundle_fiber_.GetFiber().Cancelled(); }

Case Bundle::OnCancel() const { return bundle_fiber_.GetFiber().OnCancel(); }

Case Bundle::OnJoinable() {
  bundle_fiber_.Finish();
  return bundle_fiber_.GetFiber().OnJoinable();
}

static void MaintainBundle(std::shared_ptr<Reader<Invocable>> reader) {
  Bundle children;
  Invocable item;
  while (reader->Read(&item)) {
    children.Add(std::move(item));
  }
  children.JoinAll();
}

BundleProxy::BundleProxy(Bundle* bundle)
    : bundle_(bundle),
      new_work_(std::make_shared<Channel<Invocable>>(
          std::numeric_limits<size_t>::max())) {
  bundle_->Add(absl::bind_front(
      &MaintainBundle,
      std::shared_ptr<Reader<Invocable>>{new_work_, new_work_->reader()}));
}

BundleProxy::~BundleProxy() {
  DCHECK(finished_.load(std::memory_order_relaxed));
}

void BundleProxy::Add(Invocable f) {
  DCHECK(!finished_.load(std::memory_order_relaxed));
  new_work_->writer()->Write(
      util::functional::WithCurrentContext(std::move(f)));
}

void BundleProxy::Finished() {
  DCHECK(!finished_.load(std::memory_order_relaxed));
  finished_.store(true, std::memory_order_relaxed);
  new_work_->writer()->Close();
  // The line above may wake up the proxied bundle's JoinAll() and lead
  // to the proxy object going out of scope, so _this_ must not be accessed.
}

void BundleProxy::CancelAll() {
  DCHECK(!finished_.load(std::memory_order_relaxed));
  bundle_->CancelAll();
}

}  // namespace thread
