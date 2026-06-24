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

// Bundles represent a variable-sized set of child fibers.  Adding work to a
// bundle is equivalent to creating a new child fiber, however the management of
// joining and cancellation is simplified.
//
// Bundles automatically manage the lifetime of the sub-fibers they contain.  As
// children associated with a bundle complete they will be automatically
// Join()-ed and freed.  This can be useful in structuring long-lived
// work-loops where it is desirable to immediately release children as they
// complete, e.g.:
//
//   Bundle b;
//   while (!done) {
//     ...
//     b.Add(...);
//   }
//   b.JoinAll();
//
// (It would usually be inconvenient to Join() within the loop body as this
// could potentially prevent new work from being admitted.)

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_BUNDLE_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_BUNDLE_H_

#include <atomic>
#include <memory>
#include <type_traits>
#include <utility>

#include "gloop/thread/fiber/channel.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/select.h"

namespace thread {

class FiberOptions;

class Bundle {
 public:
  // Construct a bundle belonging to Fiber::Current().  Any fibers created in
  // this bundle (via Add()) will be considered descendants of Fiber::Current()
  // for the purposes of scheduling and cancellation, and will be configured
  // with Fiber::Current()->options().
  //
  // If the active base::Context has a deadline, *this will be automatically
  // cancelled at its expiry.
  Bundle();

  // As above, but allows overriding the options used for fibers created by this
  // bundle.
  explicit Bundle(const FiberOptions& options);

  Bundle(const Bundle&) = delete;
  Bundle& operator=(const Bundle&) = delete;

  // REQUIRES: JoinAll() must have been called.
  ~Bundle() = default;

  // Start a new fiber and add it to this bundle.  The new fiber will be a
  // descendant of the Fiber which constructed *this and thus may outlive the
  // caller Fiber in the case that it is itself a descendant of the constructing
  // Fiber.  It will be automatically Join()-ed and its storage released as soon
  // as "function" completes.
  //
  // It is always required to call JoinAll() before destroying a bundle to
  // ensure that all children have had the chance to complete.
  //
  // The new fiber will execute within (a copy of) the active base::Context at
  // the point Add() is called.  This may include a deadline.
  //
  // REQUIRES: May not be called if JoinAll() or OnJoinable() have been invoked.
  // May only be called by the Fiber that created *this, or any of its child
  // Fiber descendants.
  template <typename F, typename = std::invoke_result_t<F>>
  void Add(F&& f) {
    AddImpl(std::forward<F>(f));
  }

  // Join all fibers, and any descendants, belonging to this bundle.  Returns
  // immediately if the bundle is empty (i.e. no children currently exist).
  // It is illegal to Add() children after calling JoinAll(). Must be called
  // prior to releasing *this, by the Fiber that created *this.
  void JoinAll();

  // Cancel all fibers, and any descendants, belonging to this bundle.  Fibers
  // added after cancellation will be created in a cancelled state.
  void CancelAll();

  // Returns whether the bundle has been cancelled (as per CancelAll()).
  bool Cancelled() const;

  // Returns a token that may be used to Select() against the cancellation
  // of the bundle.
  Case OnCancel() const;

  // Returns a token that may be used to Select() against the completion of all
  // fibers belonging to this Bundle.
  // It is illegal to Add() children after calling OnJoinable().
  // Note: Callers must still ensure JoinAll() is called.
  Case OnJoinable();

 private:
  void AddImpl(Invocable f);

  bool IsDescendantAdd();
  internal::DynamicFiber bundle_fiber_;
};

// BundleProxy provides a handle that a Fiber may generate to allow external
// execution to delegate execution to an owned Bundle.  The delegated
// execution is added as a new child to the existing Bundle that *this is
// acting as a proxy for.  It will participate in all Fiber/Bundle semantics,
// including inherited cancellation.  This is useful for interacting with
// frameworks built on asynchronous callbacks, such as Stubby2.
//
// Example:
//   Bundle bundle;
//   BundleProxy proxy(&bundle);
//   CallRemoteRPC([&proxy]() {
//     proxy.Add([]{ /* some work to be executed as part of bundle. */ });
//     proxy.Finished();
//   });
//   bundle.JoinAll();
class BundleProxy {
 public:
  // REQUIRES: May only be called by the Fiber that created "bundle", or
  // any of its child fiber descendants.
  explicit BundleProxy(Bundle* bundle);

  BundleProxy(const BundleProxy&) = delete;
  BundleProxy& operator=(const BundleProxy&) = delete;

  // REQUIRES: Finished() has been called.
  ~BundleProxy();

  // Adds new work to the bundle this object proxies. As Bundle::JoinAll()
  // waits for all of its proxies to be finished, proxies can actually
  // continue adding work to their bundles after JoinAll() is called on their
  // 'parent' bundles. This is a difference between Bundle::Add() and
  // BundleProxy::Add().
  //
  // The new fiber will execute within (a copy of) the active base::Context at
  // the point Add() is called.  This may include a deadline.
  //
  // REQUIRES: Finished() has not been called.
  void Add(Invocable f);

  // Marks this proxy as finished. Bundles wait for all of their proxies
  // to be finished in JoinAll().
  void Finished();

  // REQUIRES: Finished() has not been called.
  void CancelAll();

 private:
  Bundle* const bundle_;
  const std::shared_ptr<Channel<Invocable>> new_work_;
  std::atomic<bool> finished_{false};
};

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_BUNDLE_H_
