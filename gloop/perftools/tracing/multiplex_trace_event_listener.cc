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

#include "gloop/perftools/tracing/multiplex_trace_event_listener.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "gloop/base/examine_stack.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/trace_event_listener.h"
#include "gloop/perftools/tracing/trace_source_location.h"
#include "gloop/perftools/tracing/tracing_base.h"

namespace perftools::tracing::internal {
namespace {

// Note that events are dispatched using either `Dispatch()` or RDispatch`,
// where the 'R' stands for 'Release' / 'Reverse' dispatch. The former
// dispatches to `first_`, then 'second_`, the latter dispatches to `second_`,
// then `first_`. Events that form pairs (Begin / End, Resume / Suspend, Wait /
// Continue) use the reverse order for the 'closing' end. While this is not a
// strict requirement or promise, dispatching such events in LIFO order is
// typically a nicer default, think for example a CPU or resources tracer
// scoped 'outside' of an inner BEGIN / END pair.
class Mux final : public TraceEventListener {
 public:
  // kMaxDepth is the depth after which we will automatically balance.
  static constexpr size_t kMaxDepth = 25;

  // kMaxSize is the maximum listeners we retain when balancing. The total
  // number can exceed this as we don't check max items until we balance.
  // The purpose here is to avoid hostile unbounded multiplexing.
  static constexpr size_t kMaxSize = 1 << 20;

  Mux() = delete;
  Mux(const Mux&) = delete;
  Mux& operator=(const Mux&) = delete;

  Mux(TraceEventListener* first, TraceEventListener* second)
      : first_(first),
        second_(second),
        first_depth_(first->Depth()),
        second_depth_(second->Depth()) {
    if (Depth() > kMaxDepth) {
      LOG_EVERY_N_SEC(WARNING, 10)
          << "Depth exceeding maximum depth of " << kMaxDepth << "."
          << " Auto rebalancing multiplexer @" << base::CurrentStackTrace();
      AutoBalance();
    }
  }

  TraceEventListener* GetEventListener(SyncId sync_id) final {
    TraceEventListener* first = first_->GetEventListener(sync_id);
    TraceEventListener* second = second_->GetEventListener(sync_id);
    if (first == nullptr) {
      return second;
    } else if (second == nullptr) {
      return first;
    }
    return new Mux(first, second);
  }

  void ReleaseEventListener() final {
    RDispatch(&TraceEventListener::ReleaseEventListener);
#ifdef NDEBUG
    delete this;
#else
    // TODO: we like 100% testing coverage numbers.
    delete static_cast<TraceEventListener*>(this);
#endif
  }

  void OnTraceSpawn(SyncId sync_id, StringRef label) final {
    Dispatch(&TraceEventListener::OnTraceSpawn, sync_id, label);
  }

  void OnTraceBeginSync(SyncId sync_id, StringRef label) final {
    Dispatch(&TraceEventListener::OnTraceBeginSync, sync_id, label);
  }

  void OnTraceEnterSync(SyncId sync_id, StringRef label) final {
    Dispatch(&TraceEventListener::OnTraceEnterSync, sync_id, label);
  }

  void OnTraceSuspendSync(SyncId sync_id) final {
    RDispatch(&TraceEventListener::OnTraceSuspendSync, sync_id);
  }

  void OnTraceResumeSync(SyncId sync_id) final {
    Dispatch(&TraceEventListener::OnTraceResumeSync, sync_id);
  }

  void OnTraceEndSync(SyncId sync_id) final {
    RDispatch(&TraceEventListener::OnTraceEndSync, sync_id);
  }

  void OnTraceWait(BarrierId id, StringRef label) final {
    Dispatch(&TraceEventListener::OnTraceWait, id, label);
  }

  void OnTraceContinue(BarrierId id) final {
    RDispatch(&TraceEventListener::OnTraceContinue, id);
  }

  void OnTraceObserved(BarrierId id, StringRef label) final {
    Dispatch(&TraceEventListener::OnTraceObserved, id, label);
  }

  void OnTraceSignal(BarrierId id, StringRef label) final {
    Dispatch(&TraceEventListener::OnTraceSignal, id, label);
  }

  void OnTraceMark(StringRef label, TraceSourceLocation location) final {
    Dispatch(&TraceEventListener::OnTraceMark, label, location);
  }

  void OnTraceBeginRegion(StringRef label, TraceSourceLocation location) final {
    Dispatch(&TraceEventListener::OnTraceBeginRegion, label, location);
  }

  void OnTraceEndRegion() final {
    RDispatch(&TraceEventListener::OnTraceEndRegion);
  }

  void OnTraceSend(StringRef label, MsgOrigin origin, MsgId id,
                   MsgSequence sequence) final {
    Dispatch(&TraceEventListener::OnTraceSend, label, origin, id, sequence);
  }

  void OnTraceReceive(StringRef label, MsgOrigin origin, MsgId id,
                      MsgSequence sequence) final {
    Dispatch(&TraceEventListener::OnTraceReceive, label, origin, id, sequence);
  }

  void OnTraceSessionStart(StringRef label, MsgId id,
                           EndPoint end_point) final {
    Dispatch(&TraceEventListener::OnTraceSessionStart, label, id, end_point);
  }

  void OnTraceSessionEnd(StringRef label, MsgId id, EndPoint end_point) final {
    RDispatch(&TraceEventListener::OnTraceSessionEnd, label, id, end_point);
  }

  void OnTraceStreamingSend(MsgOrigin origin, MsgId id, MsgSequence sequence,
                            MsgFlags flags) final {
    Dispatch(&TraceEventListener::OnTraceStreamingSend, origin, id, sequence,
             flags);
  }

  void OnTraceStreamingReceive(MsgOrigin origin, MsgId id, MsgSequence sequence,
                               MsgFlags flags) final {
    Dispatch(&TraceEventListener::OnTraceStreamingReceive, origin, id, sequence,
             flags);
  }

  void OnTraceControlFlow(StringRef label, ControlFlowType type,
                          ControlFlowId id, ControlFlowSequence seq) final {
    Dispatch(&TraceEventListener::OnTraceControlFlow, label, type, id, seq);
  }

  std::pair<TraceEventListener*, bool> Extract(
      TraceEventListener* listener) final {
    if (listener == this) {
      return std::make_pair(nullptr, true);
    } else if (auto res = second_->Extract(listener); res.second) {
      if (res.first == nullptr) return DeleteThisAndReturn(first_);
      second_ = res.first;
      second_depth_ = res.first->Depth();
    } else if (res = first_->Extract(listener); res.second) {
      if (res.first == nullptr) return DeleteThisAndReturn(second_);
      first_ = res.first;
      first_depth_ = res.first->Depth();
    } else {
      return std::make_pair(this, false);
    }
    return std::make_pair(this, true);
  }

  void ExtractAll(std::vector<TraceEventListener*>& listeners) final {
    first_->ExtractAll(listeners);
    second_->ExtractAll(listeners);
    delete this;
  }

  bool Contains(TraceEventListener* listener) const override {
    if (listener == nullptr) return false;
    if (listener == this) return true;
    return second_->Contains(listener) || first_->Contains(listener);
  }

  size_t Depth() const final {
    return std::max(first_depth_, second_depth_) + 1;
  }

 private:
  ~Mux() final = default;

  // Dispatches to first_, then second_
  template <typename MemberFn, typename... Args>
  void Dispatch(MemberFn fn, Args&&... args) {
    (first_->*fn)(args...);
    (second_->*fn)(std::forward<Args>(args)...);
  }

  // Dispatches to second_, then first_
  template <typename MemberFn, typename... Args>
  void RDispatch(MemberFn fn, Args&&... args) {
    (second_->*fn)(args...);
    (first_->*fn)(std::forward<Args>(args)...);
  }

  // Deletes `self` and returns the remaining right or left leg.
  std::pair<TraceEventListener*, bool> DeleteThisAndReturn(
      TraceEventListener* first_or_second) {
    DCHECK_NE(first_or_second, nullptr);
    delete this;
    return std::make_pair(first_or_second, true);
  }

  // Rebuilds this Multiplexer by extracting all elements in order,
  // and building it back into a balanced tree.
  void AutoBalance() {
    struct Builder {
      TraceEventListener* Build(size_t begin, size_t end) {
        size_t count = end - begin;
        size_t mid = begin + count / 2;
        return (count < 2) ? listeners[begin]
                           : new Mux(Build(begin, mid), Build(mid, end));
      }
      std::vector<TraceEventListener*> listeners;
    } builder;

    // Extract all listeners in order
    first_->ExtractAll(builder.listeners);
    second_->ExtractAll(builder.listeners);

    // Bound the # items to kMaxSize
    size_t begin = 0;
    if (builder.listeners.size() > kMaxSize) {
      // Log error somewhat frequently and drop listeners.
      LOG_EVERY_N_SEC(ERROR, 10)
          << "Maximum number of listeners " << builder.listeners.size()
          << " exceeds maximum of " << kMaxSize << " @"
          << base::CurrentStackTrace();
      begin = builder.listeners.size() - kMaxSize;
      for (size_t i = 0; i < begin; ++i) {
        builder.listeners[i]->ReleaseEventListener();
      }
    }
    size_t mid = begin + (builder.listeners.size() - begin) / 2;

    first_ = builder.Build(begin, mid);
    first_depth_ = first_->Depth();
    second_ = builder.Build(mid, builder.listeners.size());
    second_depth_ = second_->Depth();
  }

  TraceEventListener* first_;
  TraceEventListener* second_;
  size_t first_depth_;
  size_t second_depth_;
};

}  // namespace

TraceEventListener* MultiplexTraceEventListener(TraceEventListener* first,
                                                TraceEventListener* second) {
  DCHECK_NE(first, nullptr);
  DCHECK_NE(second, nullptr);
  return new Mux(first, second);
}

}  // namespace perftools::tracing::internal
