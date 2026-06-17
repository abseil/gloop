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

#ifndef THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_TRACE_EVENT_LISTENER_H_
#define THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_TRACE_EVENT_LISTENER_H_

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/trace_source_location.h"
#include "gloop/perftools/tracing/tracing_base.h"

namespace perftools::tracing {

// `TraceEventListener` defines the interface for causality aware tracers.
//
// A `TraceEventListener` interface receives processing events through
// invocation of the various `On....` event methods. These events are
// emitted in the context of a synchronous execution context identified
// through a `SyncId`.
//
// A synchronous execution context is the maximal code path (function) executing
// on a single thread. For example, an RPC class method is the main (top level)
// synchronous execution of a request, as is the body of a fiber or function
// scheduled on an executor. Listeners are automatically propagated across all
// executions related (scheduled by) the traced thread(s).
//
// The `ReleaseEventListener` method is invoked after a synchronous trace
// completes, and there is no running code referencing the current listener.
// Implementations are free to manage the life cycle of tracers as they see fit.
// For example, a tracer can use heap allocations and invoke `delete this` in
// the `ReleaseEventListener` call or  use reference counting on a shared
// instance or if it doesn't require any state, use a singleton instance with
// an empty `ReleaseEventListener` implementation.
//
// The start and end of each synchronous execution will have `Begin` and
// `End` events with a unique `SyncId`. This includes the starting scope
// of the tracer. `Spawn` events are emitted when synchronous executions
// are scheduled.
//
// For example:
//
//   // Partial TraceEventListener (other events omitted for brevity)
//   class ConsoleListener : public TraceEventListener {
//    public:
//     void OnTraceBeginSync(SyncId sync_id, StringRef label) {
//       std::cout << "Begin " << sync_id << ": " << label << std::endl;
//     }
//     void OnTraceEndSync(SyncId sync_id) {
//       std::cout << "End " << sync_id << std::endl;
//     }
//     void OnTraceSpawn(SyncId sync_id StringRef label) {
//       std::cout << "Spawn " << sync_id << " " << label << std::endl;
//     }
//
//     TraceEventListener* GetEventListener(SyncId) override { return this; }
//     void ReleaseEventListener() override {}
//
//     static ConsoleListener* instance() {
//       static auto* listener = new ConsoleListener;
//       return listener;
//     }
//   };
//
//   int Main() {
//     WithTraceEventListener with_trace(ConsoleListener::instance());
//
//     thread::FiberOptions options;
//     options.SetInternedName("MyFiber");
//     thread::Fiber fiber(options) {
//       // Do nothing
//     };
//     fiber.Join())
//   }
//
// The above code will result in the following output:
//   Begin 1
//   Spawn 2
//   Begin 2
//   End 2
//   End 1
//
// The 'Spawn` event here is a causality edge between the parent and child
// fiber execution. For (potentially) blocking waits we have 'Wait', `Continue`,
// 'Signal` and `Observed` signals. For example, in the above code the main
// synchronous execution is blocked by the child fiber in the `Join` call.
// A `Wait` event is emitted with a `barrier_id` identifying the `OnJoin()`
// event at the start of the `Join()` call, and a `Continue` event when
// the event is signaled. Likewise, the fiber body will emit a `Signal' trace
// event at the end of the fiber execution as it signals the `OnJoin()` event.
//
// For example, we can add implementations for the events in our console
// listener as follows:
//
//   class ConsoleListener : public TraceEventListener {
//    public:
//     ...
//
//     void OnTraceWait(BarrierId barrier_id, StringRef label) {
//       std::cout << "Wait " << barrier_id << std::endl;
//     }
//     void OnTraceContinue(BarrierId barrier_id) {
//       std::cout << "Continue " << barrier_id << std::endl;
//     }
//     void OnTraceSignal(BarrierId barrier_id, StringRef label) {
//       std::cout << "Signal " << barrier_id << " " << label << std::endl;
//     }
//   };
//
// And then our output would look as follows:
//
//   Begin 1
//   Spawn 2
//   Wait 796478263
//   Begin 2
//   Signal 796478263
//   End 2
//   Continue 796478263
//   End 1
//

// The above listener doesn't have state, it simply prints to the console, so
// for our purposes we use a singleton and provide `GetEventListener` and
// `ReleaseEventListener()` returning `this` and doing nothing on release.
//
// However, there are cases where we want to maintain some state and not have a
// single shared instance for all thread executions.
//
// For this purpose we can use the `GetEventListener` / `ReleaseEventListener`
// function to install 'per synchronous execution` tracers. In our naive example
// we will use this to store the active sync_id. Note that this is a bit of a
// moot exercise as we can obtain this through the `active_sync_id()` function.
//
//   class ConsoleListener : public TraceEventListener {
//    public:
//     ConsoleListener(SyncId sync_id) : sync_id_(sync_id) {}
//
//     TraceEventListener* GetEventListener(SyncId sync_id) override {
//       return new ConsoleListener(sync_id);
//     }
//
//     void ReleaseEventListener() override {
//      delete this;
//    }
//  };
//
// And then we install the tracer as:
//
//   int Main() {
//     WithTraceEventListener with_trace(new ConsoleListener);
//
//     ...
//   }
//
// The above code assigns a specific listener to each synchronous execution.
// Implementations are free to pick any lifetime mechanism they see fit.
class TraceEventListener {
 public:
  virtual ~TraceEventListener() = default;

  // `BeginSync()` and `EndSync()` events are emitted at the start and end of
  // a synchronous code execution. The scoped code is typically the body of a
  // thread or fiber, an `invocable` scheduled on some executor, or the scoped
  // execution of some work submitted to a queue or thread-pool. `sync_id`
  // uniquely identifies the execution instance, not the code itself. Scheduled
  // code will have a corresponding `Spawn()` event with the same `sync_id`
  // value that is guaranteed to 'happen before' the begin event.
  //
  // However, not all code has a corresponding spawn event:
  // - The starting point of the execution trace forms the initial synchronous
  //   execution scope. This execution is always identified by `kMainSyncId`.
  //   Inside server applications, this execution is typically the RPC handler
  //   with the causality being the client sending a request.
  //
  // - Incoming messages or other external events are scheduled outside the
  //   scope and context of an execution tracer. Such executions appear "out of
  //   thin air", and the causality for these executions is only available in
  //   the context of the entire distributed query.
  //
  // `label` identifies the synchronous execution and typically holds either
  // the source location of the code scoping the execution, or an explicit
  // identifying value specified in the source code.
  virtual void OnTraceBeginSync(SyncId sync_id, StringRef label);
  virtual void OnTraceEndSync(SyncId sync_id);

  // `EnterSync()` is emitted to indicate execution passing into and out of
  // nested synchronous execution contexts. For example, a `LocalTraceSpan`
  // creates and scopes a new `TraceContext` and thus also a new synchronous
  // execution context. As these are nested child contexts, they do not affect
  // the per-thread synchronous execution state. The `EnterSync` events provide
  // the boundaries events for such transitions, and allows us to accurately
  // attribute latency to the scoped contexts and spans. For example:
  //
  //   void MyFunction() {
  //      // Emits `OnTraceEnterSync(<sync_id>, "Baz")` on construction with
  //      // <sync_id> identifying the nested child execution context.
  //      WithTraceContext with_tc(CreateChildTraceContext(), "Baz");
  //      ...
  //      ....
  //      // Emits `OnTraceEnterSync(<old_sync_id>)` on destruction with
  //      // <old_sync_id> identifying the previous execution context.
  //   }
  virtual void OnTraceEnterSync(SyncId sync_id, StringRef label);

  // `SuspendSync()` and `ResumeSync()` events are emitted before and after the
  // execution tracer is suspended. For example: some library code may perform a
  // lazy/once initialization that should not be included in currently active
  // execution trace. While rare, tracing can also be suspended as part of some
  // "thread stealing" logic, i.e.: some code temporarily swaps the thread
  // context for execution some work for an unrelated RPC, "stealing" the thread
  // to perform this work that may itself have a separate execution trace.
  virtual void OnTraceSuspendSync(SyncId sync_id);
  virtual void OnTraceResumeSync(SyncId sync_id);

  // `OnTraceSpawn()` is emitted on the scheduling of a synchronous execution
  // such as a thread or fiber. `sync_id` uniquely identifies the execution
  // and corresponding `BeginSync` and `EndSync` events. `Label' contains the
  // logical name for the scheduled execution, or empty if not  available.
  virtual void OnTraceSpawn(SyncId sync_id, StringRef label);

  // `Wait()`, `Continue()`, `Observed()` and Signal()` events are emitted for
  // synchronization events. For example, `absl::WaitForNotification()` will
  // result in a `Wait()` event being emitted. A different thread calling
  // `Notify()` will result in a `Signal()` event being emitted, unblocking the
  // waiting thread which is traced through a `Continue()` event.
  // Signals and waits are matched using a unique barrier id value, which is
  // most typically the address of the underlying synchronization object. The
  // barrier id may not always be available at the time of the `Wait` event, and
  // only be specified on the `Continue` event. For example, a thread could wait
  // for multiple objects, and the signaled object is then only known at the
  // `Continue()` event.
  // The `Observed()` event is emitted for non blocking observations of some
  // condition. For example, when `HasBeenNotified()` returns true, the
  // application has 'observed' the condition without a potentially blocking
  // wait, which may form a causality edge.
  virtual void OnTraceWait(BarrierId id, StringRef label);
  virtual void OnTraceContinue(BarrierId id);
  virtual void OnTraceObserved(BarrierId id, StringRef label);
  virtual void OnTraceSignal(BarrierId id, StringRef label);

  // `Send()` and `Receive()` events are emitted when request / response
  // messages are sent or received. Messages are typically unary RPC requests
  // and responses forming remote causality edges between client and server
  // applications. `id` uniquely identifies the message.
  //
  // Messages do not necessarily have to be actual messages sent over the wire:
  // some applications may emit send and receive events to force causality in
  // complex processes where automatically recording causality is infeasible.
  virtual void OnTraceSend(StringRef label, MsgOrigin origin, MsgId id);
  virtual void OnTraceReceive(StringRef label, MsgOrigin origin, MsgId id);

  // `SessionStart()` and `SessionFinish()` events are emitted when client and
  // server sessions start and finish for some given client/server session.
  // `EndPoint` identifies the end point and type of session.
  virtual void OnTraceSessionStart(StringRef label, MsgId id,
                                   EndPoint end_point);
  virtual void OnTraceSessionEnd(StringRef label, MsgId id, EndPoint end_point);

  // `StreamingSend()` and `StreamingReceive()` events are emitted when
  // streaming request / response messages are sent or received. `id` uniquely
  // identifies the streaming session and matches the `id` value of the client
  // and server `SessionStart()` / `SessionFinish()` events defining the
  // streaming session lifecycle. `sequence` is the sequence number assigned by
  // the RPC service identifying the message on both client and server side.
  virtual void OnTraceStreamingSend(MsgOrigin origin, MsgId id, MsgSequence seq,
                                    MsgFlags flags);
  virtual void OnTraceStreamingReceive(MsgOrigin origin, MsgId id,
                                       MsgSequence seq, MsgFlags flags);

  // The `Mark` event is emitted typically from an application annotation to
  // mark a specific condition or location in the executing code. For example,
  // an application could emit `tracing::Mark("Special cleanup triggered")`,
  virtual void OnTraceMark(StringRef label, TraceSourceLocation location);

  // `BeginRegion()` and `EndRegion()` events are emitted through scoped regions
  // in the executing code to add granularity to the trace. For example:
  //
  //   void SlowFunction() {
  //     tracing::Region region("SlowFunction");
  //     ...
  //   }
  //  virtual void OnTraceBeginRegion(StringRef label);
  virtual void OnTraceBeginRegion(StringRef label,
                                  TraceSourceLocation location);
  virtual void OnTraceEndRegion();

  // ControlFlow events are emitted from application annotations and indicate
  // locations where there is some type of client server interactions with
  // actors generally invisible to the current process.
  //
  // One common example is asynchronous streaming RPC requests and responses.
  // A client may send some 'N' requests to a remote server, which causes remote
  // code on that server to execute, and the server sends some 'M' responses
  // back to the client, where each of those responses indicate some partial or
  // full completion of the session represented by all requests and responses.
  //
  // ControlFlow events can also be used in cases where there is a complicated
  // execution path that is hard or expensive to instrument in full, but we have
  // clear 'start' and 'end' points where we can emit control flow events.
  //
  // The main use case for ControlFlow events is to include remote execution in
  // the local critical path. For example, a client may finalize some local RPC
  // once it receives all expected streaming responses from the server. This
  // means that the final response and some originating request are on the
  // critical path, i.e. the control flow from a latency perspective traverses
  // from the original request(s) to the finalizing response(s).
  //
  // `ControlFlowType::kSchedule` and `ControlFlowType::kContinue` are used to
  // annotate the client side causality of some application defined interaction
  // between a client and a server. For example, a client may emit a `kSchedule`
  // event upon sending the first of a set of streaming requests to a server,
  // and emit a 'kContinue` upon some final streaming response from the server.
  //
  // Applications can optionally add `kStart` and `kEnd` annotations for the
  // server side implementation of the interaction. Such a server could emit
  // a `kStart` event upon receiving a request initiating the client/server
  // operation, and a `kEnd` event upon completing the operation, typically
  // as part of sending some final response to the client.
  //
  // `id` contains a identifier that should be unique inside the current trace.
  //
  // Applications wanting to correlate client and server side annotations, i.e.,
  // correlate 'Schedule/Start' and 'End/Continue' events across the client
  // and server applications are responsible for using globally unique ids.
  //
  // ControlFlow sequences are an experimental feature, applications should
  // use 0 for the control flow sequence.
  virtual void OnTraceControlFlow(StringRef label, ControlFlowType type,
                                  ControlFlowId id, ControlFlowSequence seq);

  // `GetEventListener()` is invoked to obtain an event listener for a different
  // synchronous execution, for example, for the execution of a child thread.
  //
  // Implementations are free to pick any strategy here, commonly:
  // - singleton / infinite lifecycle.
  //   GetEventListener() returns `this`, ReleaseEventListener() is a no-op.
  // - separate instance per synchronous execution
  //   GetEventListener() returns a new (heap allocated) copy of the current
  //   instance, ReleaseEventListener() releases (frees) the instance.
  // - shared instance
  //   GetEventListener() adds a reference and returns `this`,
  //   ReleaseEventListener() removes a reference and frees on zero..
  //
  // Having separate listeners can be useful for listeners managing state: It
  // isolates such state per `sync_id`, and additionally removes the need for
  // synchronizing access to this instance specific state.
  //
  // Applications can prevent the current instance from being propagated by
  // returning `nullptr`. This is useful for use cases where an application
  // may want to only trace a narrowly scoped (current) execution and not have
  // the trace listener propagate to child executions.
  // The default implementation returns `nullptr`.
  //
  // A possible implementation could look as follows:
  //
  //  class MyListener final : public TraceEventListener {
  //   public:
  //    explicit MyListener(MyListener* parent) : parent_(parent) {}
  //
  //    TraceEventListener* GetEventListener(SyncId sync_id) override {
  //      return new MyListener(this);
  //    }
  //
  //    void ReleaseEventListener() override {
  //      if (parent_ != nullptr) {
  //        parent_->MergeData(this);
  //      } else {
  //        ExportData();
  //      }
  //      delete this;
  //    }
  //  };
  //
  // See also `ReleaseEventListener()`
  virtual TraceEventListener* GetEventListener(SyncId sync_id) = 0;

  // `ReleaseEventListener` is invoked on an instance after the synchronous
  // scope it is bound to runs to completion which is directly after the
  // `OnTraceEndSync()` event is emitted. This method is also invoked in the
  // rare circumstance that an application causes a synchronous context to be
  // deleted in a suspended state: i.e.; a context that was never properly
  // restored through a call to `RestoreTraceContext()`.
  virtual void ReleaseEventListener() = 0;

  // `GetBridgingEventListener()` is invoked to allow listeners to "bridge"
  // processing events across different trace contexts.
  //
  // Trace event listeners are normally bound to the same (distributed) trace
  // context. However, we support the concept of 'linked traces' where a new
  // trace context is initiated for some part of the distributed execution, and
  // trace annotations are captured inside the linked trace.
  //
  // Linked traces can by default not emit processing events as processing
  // events require consistent synchronous recordings: if we'd emit events
  // like `OnTraceMark()` in isolation on the linked trace, then it is missing
  // context of required synchronous execution such as the Begin and End events
  // of the entire synchronous execution (i.e., "thread") inside which the
  // linked trace is created, resulting in inconsistent events.
  //
  // `GetBridgingEventListener()` allows listeners to provide a specific
  // listener instance to serve as a proxy for such events, where the proxy
  // is responsible for recording the trace events as if they were recorded
  // in the original trace event listener context / trace context.
  //
  // Listeners that already provide "per sync context" instances through for
  // example heap allocated (child) instances could directly return `this`.
  // They must however make sure to reference count the instance as the tracing
  // framework will invoke `ReleaseEventListener` on the proxy to end its
  // lifecycle. Since calls to the proxy and the original listener are strictly
  // synchronized, such a proxy listener could use a simple counter as per the
  // below example:
  //
  //   class MyListener : public TraceEventListener {
  //    public:
  //     TraceEventListener* GetEventListener(SyncId sync_id) final {
  //       // Create a new child listener for each synchronous context.
  //       return new MyListener(*this, sync_id);
  //     }
  //
  //     TraceEventListener* GetBridgingEventListener(StringRef label) final {
  //       ++bridges_;
  //       return this;
  //     }
  //
  //     void ReleaseEventListener() final {
  //       if (bridges_-- == 0) {
  //         delete this;
  //       }
  //     }
  //
  //    private:
  //     int bridges_ = 0;
  //   };
  //
  // The default implementation returns `null`, meaning that by default
  // no processing events are recorded inside the linked trace.
  virtual TraceEventListener* GetBridgingEventListener(StringRef label);

  // Extracts `listener` from this instance.
  //
  // Returns `{<new_root>, true}` on success, `{this, false}` if
  // `listener` is not equal to, or embedded inside this instance.
  //
  // The default implementation is implemented as:
  //
  //   return (listener == this) ? std::make_pair(nullptr, true)
  //                             : std::make_pair(this, false);
  //
  // `listener` is allowed to be nullptr in which case this method must
  // always return `{this, false}` and leave the current instance unchanged.
  //
  // This method is intended to be overridden by (internal) container type
  // implementations only. Applications should not override this method.
  //
  // For example, the below code composes a listener using the 'multiplex'
  // logic provided by the tracing library, and after doing something with
  // it, removes the added listener from the multiplexed listener.
  //
  //   void RunWithSpecialListener(TraceEventListener* listener) {
  //     // listener_  owns `special` after the multiplex call
  //     TraceEventListener* special = CreateSpecialListener();
  //     listener_ = MultiplexTraceEventListener(listener_, special);
  //
  //     DoStuff(...);
  //
  //     if (auto res = listener_->Extract(special); res.second) {
  //       // We regained ownership and got a new root listener.
  //       listener_ = res.first;
  //       special->ReleaseEventListener();
  //     } else {
  //       LOG(DFATAL) << "Special listener went AWOL";
  //     }
  //   }
  //
  virtual std::pair<TraceEventListener*, bool> Extract(
      TraceEventListener* listener);

  // `CascadingRelease(listener)` is identical to `Extract(listener)` except
  // that it invokes `listener->ReleaseEventListener()` on success. Calling
  // `CascadingRelease()` with `listener` equal to the current instance is
  // equivalent to calling `ReleaseEventListener()`, and the implementation
  // must in that case return `{nullptr, true}`
  std::pair<TraceEventListener*, bool> CascadingRelease(
      TraceEventListener* listener);

  // Returns true if this instance or any event listeners below this instance
  // matches `listener`. Regular TraceEventListener implementations should use
  // the provided default implementation.
  virtual bool Contains(TraceEventListener* listener) const;

  // Returns the depth of this instance.
  // This method is intended to be overridden by (internal) container type
  // implementations only. Applications should not override this method.
  virtual size_t Depth() const;

  // Extracts all listeners at and below this listener into `listeners`.
  // This function transfers any and all ownership of this instance as well as
  // any contained listeners to `listeners`, invalidating `this` instance.
  // The order of the extracted items is not strictly defined, but is typically
  // in LIFO order. For example, `MultiplexTraceEventListener` defines a LIFO
  // ordering on `first` vs `second`: 'second' should be considered the 'added'
  // or most recent item and considered 'first to be extracted'.
  // This method is intended to be overridden by (internal) container type
  // implementations only. Applications should not override this method nor
  // invoke it without the explicit permission of the Dapper team.
  virtual void ExtractAll(std::vector<TraceEventListener*>& listeners);

  // Releaser is a 'deleter' class type used as a deleter type for unique_ptr.
  struct Releaser {
    void operator()(TraceEventListener* listener) const {
      if (listener != nullptr) listener->ReleaseEventListener();
    }
  };
};

// TraceEventListenerPtr is a smart pointer class managing explicit ownership.
using TraceEventListenerPtr =
    std::unique_ptr<TraceEventListener, TraceEventListener::Releaser>;

inline std::pair<TraceEventListener*, bool>
TraceEventListener::CascadingRelease(TraceEventListener* listener) {
  auto res = Extract(listener);
  if (res.second) listener->ReleaseEventListener();
  return res;
}

}  // namespace perftools::tracing

#endif  // THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_TRACE_EVENT_LISTENER_H_
