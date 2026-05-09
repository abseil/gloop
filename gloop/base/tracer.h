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

// Tracer is an interface that programmers can use to annotate the current
// span of a distributed trace, as captured by the current TraceContext.
// This is described in more detail below.
//
// Most users do not need to use Tracer or TraceContext directly, rather
// they can use the efficient convenience wrappers TRACELITERAL and
// TRACEPRINTF to generate annotations, like so:
//
//    MyStubbyHandler() {
//      ...
//      TRACEPRINTF("I have %d biscuits", num_biscuits);
//      ...
//      TRACELITERAL("received response");
//      ...
//    }
//
// While these are convenient, they are not the most efficient method
// for adding trace annotations.  The same could be achieved as follows:
//
//    MyStubbyHandler() {
//      ...
//      if (base::TraceContext* tc = TraceContext::Current();
//          tc->has_tracer()) {
//        tc->tracer()->text().Printf(...);
//        tc->tracer()->PrintLiteral(...);
//      }
//      ...
//    }
//
// Note that the variadic Printf() needs to be called via a channel object,
// described below. The technical reason for this is to capture the source
// location of the annotation.
//
// Please see //perftools/tracing/public/tracebuffer.h for complete
// benchmark data concerning the annotation methods.
//
// Tracer annotations have an associated 'channel_id', an int32_t label
// used to group and prioritize annotations in a distributed trace.
// The preceding example annotations use the default TEXT_CHANNEL.  A
// few 'well-known' channels are available, along with instructions for
// defining your own channels in:
//
//   //perftools/tracing/proto/channel_id.proto
//
// To make annotations in a particular channel, Tracer provides
// methods for accessing a TraceChannel object, which provides the
// same printing interface as Tracer.  For example:
//
//    trace->verbose().PrintLiteral("This goes to the VERBOSE_CHANNEL");
//    trace->text().PrintLiteral("This goes to the TEXT_CHANNEL");
//    trace->channel(MY_CHANNEL).PrintLiteral("This goes to MY_CHANNEL");
//

#ifndef THIRD_PARTY_GLOOP_BASE_TRACER_H_
#define THIRD_PARTY_GLOOP_BASE_TRACER_H_

#include <stdarg.h>
#include <stddef.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/macros.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"  // IWYU pragma: keep
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/source_location.h"
#include "gloop/base/port.h"
#include "gloop/base/reference_tracker.h"  // IWYU pragma: keep
#include "gloop/base/time/time_unix_nanos.h"
#include "gloop/base/tracecontext.h"
#include "gloop/base/tracing_types.h"
#include "gloop/base/walltime.h"
#include "gloop/base/xray/tracing_annotations.h"
#include "gloop/perftools/tracing/format_to_buffer_sink.h"
#include "gloop/perftools/tracing/trace_source_location.h"

ABSL_DECLARE_FLAG(bool, tracer_debug_refcounts);

class IOBuffer;
class TraceRecord;

namespace base {
class Tracer;
}  // namespace base

namespace dapper {
struct InitiatorIdBitsAccess;
}  // namespace dapper

namespace google {
namespace protobuf {
class Message;
}  // namespace protobuf
}  // namespace google

namespace perftools::tracing {

class SharedFateBitAccess;
class HighValueTraceBitAccess;
class HTTPTraceInfo;
class LinkContextsImpl;
class LinkedTraceSpan;
class NoopMetadataTracer;
class RequestTraceSpanTraits;
class RpcTraceSpanState;
class SharedFateAccess;
class SimpleTraceSpanTraits;
class SimpleTracer;
class StandardTracerAnnotationSource;
class StringHashInitiatorAccess;
class TraceBuffer;
class TraceBufferIterator;
class TraceBufferLockedIterator;
class TraceConsumerManager;
class TracerCpuProfileAccess;
class TraceDecimationApi;
class TracerNotificationAccess;
class WrappedTracer;
class TracerPredicates;

class TraceStreamer;
template <typename T>
class TraceSpan;

namespace typed_annotation {
class Map;
}

namespace logging {
struct TraceRecordGenerator;
}  // namespace logging

namespace testing {
struct TestOnlyAccess;
}

bool Sample(TraceContext* tc);
void EnableSpeculativeTracing(TraceContext* tc);

}  // namespace perftools::tracing

namespace perftools::tracing {
class InitiatorIdBitsAccess;
class ClientRootedTracingCookie;
class BatchedTraceContext;
}  // namespace perftools::tracing

namespace statsrequest {
class Request;
class RequestzState;
}  // namespace statsrequest

namespace base {

// An abstract interface for simple classes that optionally consume a
// "finalized" tracer (i.e., a tracer which has lost all of its references but
// has not yet been deleted).
class TraceConsumer;
// An abstract interface that goes hand in hand with TraceEntrySource.
// An instance of a TraceEntrySource ultimately emits its stored entries
// into an instance of a TraceEntrySink. Clients of Tracer do
// not need to implement this interface. Implementers of Tracer
// will need to implement this interface.
class TraceEntrySink;
// An abstract interface for a source of trace entries. This
// is for clients that need to have custom control of entries
// that they want to add to the Tracer. Instead of being limited
// to adding individual text entries, they can add their own
// TraceEntrySink and then accumulate entries in the source in
// whatever way they like.
class TraceEntrySource;

// TraceChannel provides a mechanism to print to a particular channel
// in a Tracer. If you print directly into a Tracer, the data will go
// to the "text" channel. However you can get a different channel
// (e.g., the "verbose" channel) and print into that.
// Annotation-writing functions that can capture source location do it
// directly. The variadic Printf() relies on the source location captured when
// creating the channel.
// TraceChannel objects should not be stored, and should be obtained from
// Tracer whenever an annotation needs to be created.
class TraceChannel {
 public:
  TraceChannel(Tracer* tracer,
               perftools::tracing::channels::ChannelID channel_id,
               absl::SourceLocation location = absl::SourceLocation::current())
      : tracer_(tracer), channel_id_(channel_id), source_location_(location) {}

  // Not copyable or movable - obtain a new instance every time making an
  // annotation.
  TraceChannel(const TraceChannel&) = delete;
  TraceChannel& operator=(const TraceChannel&) = delete;

  // Public methods are rvalue-ref-qualified to enforce the "use it once"
  // contract.

  // Adds a printf-style, StrFormat-formatted trace entry using 'channel_id' to
  // 'tracer', with an associated timestamp. Uses source location captured in
  // the TraceChannel constructor.
  template <typename... Args>
  void Printf(const absl::FormatSpec<Args...>& format, const Args&... args) &&;

  // Adds a TraceStringFormatter-generated trace entry using 'channel_id'
  // to 'tracer', with an associated timestamp.
  void PrintFormattedString(
      TraceStringFormatter formatter,
      absl::SourceLocation location = absl::SourceLocation::current()) &&;

  // Adds a string literal trace entry using 'channel_id' to 'tracer',
  // with an associated timestamp.
  void PrintLiteral(
      const char* literal,
      absl::SourceLocation location = absl::SourceLocation::current()) &&;

  // Adds a string entry using 'channel_id' to 'tracer', with an
  // associated timestamp.  Makes a copy of the contents of "s", so
  // unlike PrintLiteral, the passed in argument does not need to remain
  // live past the end of this call.
  void PrintString(absl::string_view s, absl::SourceLocation location =
                                            absl::SourceLocation::current()) &&;

  // Adds a protocol message trace entry (by copying 'msg') to
  // 'tracer', with an associated timestamp.  Since constructing
  // protocol messages may be expensive, and trace annotations are not
  // always logged, be sure to check tracer->is_traced() before
  // deciding to make a protocol message trace annotation.  For
  // performance reasons, large protocol message (see kMaxProtoLength
  // in <path>) annotations will
  // read "TypeName [%d bytes]".
  //
  // Also applies filtering to messages using <path>-filter.h
  // logic.
  template <class MessageT>
  void CopyAndPrintProtocolMessage(
      const MessageT& msg, absl::Time time = absl::Now(),
      perftools::tracing::TraceSourceLocation location =
          perftools::tracing::TraceSourceLocation::current()) &&;

  template <class MessageT>
  ABSL_DEPRECATE_AND_INLINE()
  void CopyAndPrintProtocolMessage(
      const MessageT& msg,
      perftools::tracing::TraceSourceLocation location) && {
    std::move(*this).CopyAndPrintProtocolMessage(msg, absl::Now(), location);
  }

 private:
  Tracer* tracer_;
  perftools::tracing::channels::ChannelID channel_id_;
  const absl::SourceLocation source_location_;
};

// An optional object attached to a Tracer. If present, it is called to decide
// whether to keep or untrace a child span. See <link> for
// design details.
// Implementations must be thread-safe since child spans could be created in
// multiple threads.
class ChildSampler {
 public:
  struct Decision {
    // If has_value(), the child span is to be kept sampled, and its
    // probability, relative to the parent, is recorded.
    // For example, if the ChildSampler drops 9 children out of 10, the
    // relative probability is 0.1.
    // When `relative_probability`==0, the child is kept traced; this case means
    // that the decision is made in a non-uniform way, and probability is not
    // usable for statistical analysis.
    std::optional<double> relative_probability;
  };

  virtual ~ChildSampler() = default;

  virtual Decision MakeDecision(absl::string_view family,
                                absl::string_view span_name) = 0;
};

// TracerNotification is an interface for registering a callback to be invoked
// right before a Tracer is destroyed. These callbacks may revive the Tracer and
// delay the destruction of the Tracer, allowing the application to continue
// adding annotations without perturbing the Tracer's natural lifetime.
//
// To use this interface, create a subclass of TracerNotification and override
// the TakeOwnershipBeforeDestroy method. The notification object should
// generally be a stateless singleton.
//
// Usage:
//   class CustomNotification : public TracerNotification {
//    public:
//     bool TakeOwnershipBeforeDestroy(base::Tracer* tracer) override {
//       // Do something with the tracer. Return true if this Tracer was
//       // revived.
//     }
//   };
//   base::Tracer* tracer = TraceContext::Current()->tracer();
//   CustomNotification* g_notification = GetGlobalNotification();
//   tracer->NotifyBeforeDestroy(&g_notification,
//                               TracerNotificationAccess::Get());
//
// A TracerNotification may be registered on any Tracer, but only Tracers which
// are ref-counted may be revived (and have their destruction delayed). This
// generally means only Dapper-sampled Tracers are revivable.
// TODO: b/358194618 - Revisit this requirement once all Tracers are
// ref-counted.
//
// For more details, see <link>.
class TracerNotification {
 public:
  virtual ~TracerNotification() = default;

  // Called when the Tracer's ref count first reaches zero. For a ref-counted
  // Tracer, TakeOwnershipBeforeDestroy may increment the ref count to take
  // ownership of the Tracer, thus interrupting the destruction process and
  // delaying it for a future time. The notification object does not own the
  // Tracer, so it should not be deleted in this method.
  //
  // Returns true if the Tracer was revived and should not be destroyed.
  [[nodiscard]] virtual bool TakeOwnershipBeforeDestroy(
      base::Tracer* tracer) = 0;
};

// Tracer is an interface that programmers can use to annotate the current
// span of a distributed trace, as captured by the current TraceContext.
// Most users should use the convenient TRACELITERAL/PRINTF wrappers
// defined below for efficient and simple Tracer access.
//
// There are two intended implementations of Tracer.
// - statsrequest::Request (which predates this interface)
// - NoopTracer which does nothing at all.
//
// By default, all contexts have a NoopTracer attached, so annotations have no
// visible effect. However, a uniform random sample of these contexts will have
// a real Request Tracer. These sampled contexts will have their traces show up
// in the /requestz HTTP interface, and the traces will also be logged for
// collection and offline analysis.  The sampling frequency is controlled by
// FLAGS_dapper_trace_period.
//
// The Tracer interface is a subset of that of statsrequest::Request.  We do
// not provide public access to Start() or Stop() as Tracer is intended to let
// programmers only annotate the current trace.
//
// This superclass does support reference counting via the Ref() and Unref()
// methods, though only the TraceContext [friend] class is intended or allowed
// to call those methods.
//
// Usage:
//   // Start handling a request. The Tracer propagates via
//   // TraceContext across threads and callbacks.
//   ...
//   Tracer* tracer = TraceContext::Current()->tracer();
//   ...
//   // These calls do nothing if Tracer is a NoopTracer, otherwise these
//   // will go to TEXT_CHANNEL:
//   tracer->PrintLiteral("completed stage 1");
//   ...
//   tracer->text().Printf("Finished stage 2, read %d bytes", read_bytes);
//   ...
//   // This will go to VERBOSE_CHANNEL:
//   tracer->verbose().PrintLiteral("Extra verbose info");
//   ...
//   // These will go to MY_CHANNEL:
//   tracer->channel(MY_CHANNEL).PrintLiteral("Started stage 3");
//   tracer->channel(MY_CHANNEL).Printf("Finished stage 3, read %d bytes",
//                                      read_bytes);
//   ...
//   // To avoid a virtual function call in untraced contexts, it is more
//   // efficient to test tracer->is_traced():
//   if (tracer->is_traced()) {
//     tracer->channel(MY_CHANNEL).Printf("Entering stage 3, purging table %s",
//                                        table_name);
//     tracer->channel(MY_CHANNEL).PrintLiteral("Purged table");
//   }
//   ...
//   tracer->text().Printf("All done, wrote %d bytes", written_bytes);
//   ...
//   // Finished handling an RPC request.
//
// When the tracer's reference count falls to zero (i.e., there are no pending
// contexts that are still handling this request), the tracer will delete
// itself. If this is a sampled context with a real Request Tracer, then the
// tracer will also log itself to a trace log.
//
// Tracer is thread-safe, except for the protected mutators (e.g.,
// SetStartimeNow, SetStartTime) and public mutators (e.g.,
// SetStopTimeNow), which should only be called when the Tracer is
// visible to only one thread.  Subclasses must also be thread-safe.
// Subclasses may--for debugging purposes--read the underlying fields
// (e.g., call get_stop_time()) assuming it holds a Tracer reference.
class Tracer {
 public:
  // A passkey type (see passkey idiom in https://abseil.io/tips/134) used to
  // get access to the mutable underlying TraceBuffer.
  //
  // Friends of this type can create TraceBufferAccess objects to pass as the
  // required argument to Tracer::GetTraceBuffer;
  class TraceBufferAccess {
   private:
    constexpr explicit TraceBufferAccess() = default;

    // For test-only access please use
    // http://<path>
    //
    // Do not add new friends here without first discussing with <internal
    // team>.
    friend struct ::perftools::tracing::logging::TraceRecordGenerator;
    friend class ::perftools::tracing::SimpleTracer;
    friend class ::perftools::tracing::StandardTracerAnnotationSource;
    friend class ::perftools::tracing::TraceConsumerManager;
    friend class ::perftools::tracing::TraceBuffer;
    friend class ::perftools::tracing::TraceBufferIterator;
    friend class ::perftools::tracing::TraceBufferLockedIterator;
    friend struct ::perftools::tracing::testing::TestOnlyAccess;
    friend class ::statsrequest::Request;
    friend class ::statsrequest::RequestzState;
  };

  // A passkey type (see passkey idiom in https://abseil.io/tips/134) used to
  // get access to the per-Tracer CPU usage counter.
  class CpuProfileAccess {
   private:
    constexpr explicit CpuProfileAccess() = default;

    // To get access, please add yourself to the visibility list at
    // //perftools/tracing/internal:tracer_cpu_profile_access.
    friend class ::perftools::tracing::TracerCpuProfileAccess;
  };

  // A passkey type (see passkey idiom in https://abseil.io/tips/134) used to
  // gain access to registering Tracer destruction notifications.
  class NotificationAccess {
   private:
    constexpr explicit NotificationAccess() = default;

    // To get access, please add yourself to the visibility list at
    // //perftools/tracing/internal:tracer_notification_access.
    friend class ::perftools::tracing::TracerNotificationAccess;
    friend class ::base::Tracer;
  };

  // A passkey type (see passkey idiom in https://abseil.io/tips/134) used to
  // gain access to directly setting the ChildSampler on the Tracer.
  class ChildSamplerAccess {
   private:
    ChildSamplerAccess() = default;

    friend class ::perftools::tracing::TraceDecimationApi;
    friend struct ::perftools::tracing::testing::TestOnlyAccess;
  };

  virtual ~Tracer();

  // Add a printf-style, StrFormat-formatted trace entry using the TEXT_CHANNEL
  // (with an associated timestamp)
  template <typename... Args>
  ABSL_DEPRECATE_AND_INLINE()
  void Printf(const absl::FormatSpec<Args...>& format, const Args&... args) {
    text().Printf(format, args...);
  }

  // Adds a TraceStringFormatter-generated trace entry using the TEXT_CHANNEL
  // (with an associated timestamp)
  void PrintFormattedString(
      TraceStringFormatter formatter,
      absl::SourceLocation location = absl::SourceLocation::current()) {
    text().PrintFormattedString(formatter, location);
  }

  // Add a trace entry that consists of the specified string literal
  // using the TEXT_CHANNEL (with an associated timestamp)
  // This function stores the pointer but not the content. It should only be
  // used with compile-time string literals.
  void PrintLiteral(const char* literal, absl::SourceLocation location =
                                             absl::SourceLocation::current()) {
    text().PrintLiteral(literal, location);
  }

  // Adds a trace entry that consists of the specified string using
  // the TEXT_CHANNEL (with an associated timestamp).  Makes a copy of
  // the contents of "string", so unlike PrintLiteral, the passed in
  // argument does not need to remain live past the end of this call.
  void PrintString(absl::string_view s, absl::SourceLocation location =
                                            absl::SourceLocation::current()) {
    text().PrintString(s, location);
  }

  using BaseChannelIDValues = ::base::ChannelIDValues;

  // Returns a TraceChannel object for printing to channels other than
  // the default TEXT_CHANNEL.
  TraceChannel channel(
      ::perftools::tracing::channels::ChannelID channel_id,
      absl::SourceLocation location = absl::SourceLocation::current()) {
    return TraceChannel(this, channel_id, location);
  }

  // Returns a TraceChannel object which prints to TEXT_CHANNEL.
  TraceChannel text(
      absl::SourceLocation location = absl::SourceLocation::current()) {
    return channel(BaseChannelIDValues::TEXT_CHANNEL, location);
  }

  // Returns a TraceChannel object which prints to VERBOSE_CHANNEL.
  TraceChannel verbose(
      absl::SourceLocation location = absl::SourceLocation::current()) {
    return channel(BaseChannelIDValues::VERBOSE_CHANNEL, location);
  }

  // Attaches a TraceEntrySource instance |source| to this tracer.
  // All trace entries accumulated in |source| will be merged with
  // other entries in |this| in timestamp order for display and logging.
  // |source| remains owned by the caller and can be referenced from
  // an arbitrary thread during the lifetime of this tracer.
  virtual void Attach(TraceEntrySource* source) = 0;

  // This method returns true if the Tracer is considered interesting
  // at this point in time, for use by TraceEntrySource implementations
  // which need to Detach() themselves before the Tracer reference
  // count falls to zero.
  virtual bool IsInteresting() const {
    // We must consider any request which is traced by any mechanism as
    // "interesting", because all these mechanisms will have TraceEntrySources
    // attached, and not detaching them can lead to dangling references.
    return is_traced_any_kind(TraceContext::SkeletalTracingAccess());
  }

  // Detaches a TraceEntrySource instance |source| from this tracer.
  // The boolean |save_clone| indicates whether the tracer should clone
  // this source for later display/logging.  Typically, the value of
  // IsInteresting() is passed for |save_clone|.
  virtual void Detach(TraceEntrySource* source, bool save_clone) = 0;

  // Sets the maximum size in bytes to be used by this trace.
  // The default size and the behavior when the trace exceeds
  // this size is defined entirely by the subclasses. This
  // interface makes no guarantees.
  virtual void SetMaxBytesToKeep(int n) = 0;

  // Sets the maximum size in bytes to be used by each entry in this trace.
  // The default size and the behavior when a trace entry exceeds
  // this size is defined entirely by the subclasses. This
  // interface makes no guarantees.
  virtual void SetMaxBytesToKeepPerEntry(int n) = 0;

  // Sets the source location of the current span, encoded for 8-byte storage as
  // described in <link>
  // Tracers that support delivering this payload to Dapper override it,
  // otherwise it's a no-op. This must be called immediately after creating the
  // tracer, in a single-threaded context, since the underlying storage is not
  // thread-safe.
  virtual void SetSourceLocation(uint64_t encoded_source_location) {}

  // Returns the encoded source location of the current span. A tracer that
  // stores the source location overrides this, and returns the stored value.
  virtual std::optional<uint64_t> source_location() const {
    return std::nullopt;
  }

  // Returns a string containing this Tracer's annotations.
  virtual std::string ToString() const = 0;

  // Emit pre-formatted HTML for containing the Tracer's annotations.
  // The default implementation of this method does nothing.
  // TODO: Ideally this would live in a higher-level directory
  // and be implemented by helper routines based on the
  // TraceBufferIterator, but at this point base::Tracer does not
  // expose access to the TraceEntrySources.
  virtual void EmitPreformattedHTML(IOBuffer* output) const;

  // Invokes `TraceEntrySource::Emit(sink)` for each attached TraceEntrySource
  // yielding the source annotations in unspecified order. When
  // `skip_unowned_sources` is true no unowned sources (i.e., sources that have
  // not been `Detach`ed with save_clone=true) will be dereferenced.
  virtual void EmitTraceEntrySources(TraceEntrySink* sink,
                                     bool skip_unowned_sources) const = 0;

  // Invokes `TraceEntrySource::Emit(sink)` for each attached TraceEntrySource
  // yielding the source annotations in unspecified order. Unowed sources (i.e.,
  // those that have not been `Detach`ed with save_clone=true) will be skipped
  // if the tracer is no longer alive (i.e., `IsAlive() == false`)
  void EmitTraceEntrySources(TraceEntrySink* sink) const {
    const bool skip_unowned_sources = !IsAlive();
    EmitTraceEntrySources(sink, skip_unowned_sources);
  }

  // The name is used to identify this Tracer for trace logging.  The
  // usual value is 'Family.Interface' (e.g., BTI_TabletServer.Apply).
  virtual std::string name() const = 0;

  // Accessors for trace span attributes.
  uint64_t span_id() const { return span_id_; }
  uint64_t parent_span_id() const { return parent_span_id_; }
  uint64_t trace_id() const { return trace_id_; }
  uint32_t trace_mask() const {
    return trace_mask_.load(std::memory_order_relaxed);
  }
  int32_t span_type() const { return span_type_; }

  // inverse_sampling_probability is the multiplicative inverse of the
  // sampling probability.  This was formerly called "trace_period".
  double inverse_sampling_probability() const {
    return inverse_sampling_probability_;
  }

  // Timestamp accessors.  The Tracer object has three associated
  // timestamp values, where:
  //
  //   start_time <= stop_time <= unref_time
  //
  // The meaning of these timestamps varies depending on whether it is
  // manual (you allocate and start/stop using implementation methods,
  // see stats/request/request.h) or automatic (reference-counted, see
  // tracecontext.h).
  //
  // Summary:
  //
  //   "start"  The time when the Tracer is first activated.  This is
  //            set by the implementation.
  //
  //   "stop"   The time when the request (or whatever is being traced)
  //            is finished.  This is usually set by an implementation
  //            method (e.g., statsrequest::Request::Stop()) but may
  //            also be set explicitly via SetStopTimeNow(), for
  //            example:
  //
  //            TraceContext::Current()->tracer()->SetStopTimeNow();
  //
  //            This is especially useful for automatic tracers where
  //            deferred work can keep the tracer active despite
  //            having finished the request.  If the application does
  //            not set the stop time, it will be filled-in by the
  //            implementation, at unref_time, when the tracer dies.
  //
  //   "unref"  For manual Tracers, unref_time is equal to stop_time.
  //            For automatic (reference-counted) requests), this is
  //            the time at which the last reference to the Tracer is
  //            dropped.

  // Return true if the start/stop/unref time are already set.
  bool has_start_time() const { return start_time_ != absl::UnixEpoch(); }
  bool has_stop_time() const {
    return stop_time_.load(std::memory_order_relaxed) != TimeUnixNanos();
  }
  bool has_unref_time() const {
    return unref_time_.load(std::memory_order_relaxed) != TimeUnixNanos();
  }

  // Accessor for the start time.
  absl::Time get_start_time() const {
    ABSL_RAW_DCHECK(has_start_time(), "Start time is not set");
    return start_time_;
  }
  // Accessor for the start time (as WallTime).
  ABSL_DEPRECATE_AND_INLINE()
  WallTime start_time() const { return base::ToWallTime(get_start_time()); }

  // Accessor for the stop time.
  absl::Time get_stop_time() const {
    ABSL_RAW_DCHECK(has_start_time(), "Start time is not set");
    ABSL_RAW_DCHECK(has_stop_time(), "Stop time is not set");
    return stop_time_.load(std::memory_order_relaxed).AsTime();
  }
  ABSL_DEPRECATE_AND_INLINE() WallTime stop_time() const {
    return base::ToWallTime(get_stop_time());
  }

  // Sets the stop time.  This operation is ignored if the stop
  // time was previously set.
  ABSL_XRAY_ALWAYS_INSTRUMENT void SetStopTime(absl::Time time);

  // Sets the stop time to `Now()`. This operation is ignored if the stop
  // time was previously set. Requires that the tracer has been started.
  void SetStopTimeNow() { SetStopTime(absl::Now()); }

  ABSL_DEPRECATE_AND_INLINE() void set_stop_time(absl::Time time) {
    SetStopTime(time);
  }

  // Accessor for the unref time.
  absl::Time get_unref_time() const {
    ABSL_RAW_DCHECK(has_start_time(), "Start time is not set");
    return unref_time_.load(std::memory_order_relaxed).AsTime();
  }

  // Returns true if this tracer is started.
  bool IsStarted() const { return has_start_time(); }

  // Returns true if this tracer is started and not stopped.
  bool IsRunning() const { return has_start_time() && !has_stop_time(); }

  // Returns true if this tracer is started and still alive.  It may
  // have a stop time although the tracer is still active.
  bool IsAlive() const { return has_start_time() && !has_unref_time(); }

  // If the Tracer does not have a stop time, returns the time elapsed
  // since this trace started.  If it does have a stop time, returns
  // `get_stop_time() - get_start_time()`.
  //
  // REQUIRES: IsStarted()
  absl::Duration Elapsed() const;

  ABSL_DEPRECATE_AND_INLINE()
  double ElapsedSeconds() const { return absl::ToDoubleSeconds(Elapsed()); }

  // Sets the status for this tracer. The default implementation invokes
  // `SetErrorStatus()` when `status` is not ok. Note that the default
  // implementation of `SetErrorStatus()` does nothing.
  virtual void set_status(absl::Status status);

  // Returns the status value of this tracer if supported. The default
  // implementation returns a status with code `absl::StatusCode::kUnknown` when
  // `GetErrorStatus()` is true and ok otherwise. Note that the default
  // implementation of `GetErrorStatus()` always return false.
  virtual absl::Status status() const;

  // Indicates to the implementation that some kind of error condition
  // has occurred.  The default implementation of this method does nothing.
  ABSL_DEPRECATED("Use Tracer::set_status and Tracer::status")
  virtual void SetErrorStatus();

  // Returns true if the implementation supports errors and SetErrorStatus()
  // has previously been called.  The default implementation returns false.
  ABSL_DEPRECATED("Use Tracer::set_status and Tracer::status")
  virtual bool GetErrorStatus() const;

  // Returns true iff this Tracer was created in a traced context.
  bool is_traced() const {
    return trace_mask() & TraceContext::kTraceMaskRPCTracingOn;
  }

  // Returns true iff this Tracer was created in a traced context.
  // TODO: Find a more intuitive name and update the comments.
  bool is_traced_or_speculatively_traced() const {
    return trace_mask() & (TraceContext::kTraceMaskRPCTracingOn |
                           TraceContext::kTraceMaskSpeculativeCollectingOn);
  }

  // Returns true if the traced context was selected by layer-local sampling.
  // Only true when is_traced() is true.
  bool is_layer_local_sampled() const {
    return trace_mask() & TraceContext::kTraceMaskLayerLocalSamplingOn;
  }

  // Returns true if this Tracer is skeletally traced.
  bool is_skeletally_traced(TraceContext::SkeletalTracingAccess) const {
    return trace_mask() & TraceContext::kTraceMaskSkeletalTracingOn;
  }

  // Returns true if this Tracer is *only* skeletally traced but not normally
  // traced.
  bool is_only_skeletally_traced(TraceContext::SkeletalTracingAccess) const {
    return (trace_mask() & (TraceContext::kTraceMaskUserVisibleTracing |
                            TraceContext::kTraceMaskSkeletalTracingOn)) ==
           TraceContext::kTraceMaskSkeletalTracingOn;
  }

  // Returns true if this Tracer is traced by any kind of sampling process
  // (normally, speculatively, or skeletally) which would send data to Dapper.
  bool is_traced_any_kind(TraceContext::SkeletalTracingAccess) const {
    return trace_mask() & TraceContext::kTraceMaskDapperTracing;
  }

  // Returns the relative inverse skeletal sampling probability. The relative
  // probability is only set if this span initiated skeletal tracing or changed
  // its probability (e.g. via trace decimation). Otherwise, leave this unset.

  // This returns 0 if this is not skeletally traced, or if this Tracer did not
  // change the skeletal sampling probability.
  double inverse_relative_skeletal_sampling_probability(
      TraceContext::SkeletalTracingAccess) const {
    return inverse_relative_skeletal_sampling_probability_;
  }

  // Returns tracing initiator ID. The format of this ID is not public; the code
  // outside of tracing framework should treat it as opaque 64-bit identifier.
  // For the meaning of specific bits used inside the framework, refer to
  // <link>

  uint64_t initiator_id() const {
    return initiator_id_.load(std::memory_order_relaxed);
  }

  bool is_high_value_trace() const {
    return (initiator_id() & kHighValueTrace) != 0;
  }

  bool is_speculative_root() const {
    return (initiator_id() & kSpeculativeRoot) != 0;
  }

  // This does nothing today, but used to be useful for debugging in special
  // compilation modes. If you are here because you are debugging a leaked
  // tracer in a situation where you are able to recompile the binary, see
  // cl/882422446 for a minor side quest that will allow you to resurrect
  // this debugging ability.
  void GetTraceContextStackTraces(
      std::vector<base::ReferenceTracker::StackTrace>* traces) const;

  // Returns the underlying annotation map of the tracer, or nullptr. In
  // particular, it could return nullptr even when is_traced() is true, so
  // always check.
  //
  // Uses of Tracer::GetAnnotationMap() must be restricted to the public API in
  // //perftools/tracing/public/annotation_map.h.  The caller should not keep a
  // reference to this object because once the refcount drops to 0,
  // NotifyTraceConsumers() iterates over the TraceBuffer WITHOUT
  // HOLDING LOCKS for performance reasons.
  [[deprecated(
      "Use GetTypedAnnotationMap() for key-value annotations (<link>) or "
      "GetTraceBuffer() for TraceBuffer access.")]]
  virtual perftools::tracing::TraceBuffer* GetAnnotationMap() = 0;

  // Returns the underlying TraceBuffer of the tracer if one exists or nullptr
  // otherwise. Access is restricted to callers that can create an instance of
  // ::base::Tracer::TraceBufferAccess to pass as a parameter. Note that a
  // test-only instance of TraceBufferAccess is available in
  // http://<path>
  //
  // The caller should not keep a reference to this object because once the
  // refcount drops to 0, NotifyTraceConsumers() iterates over the
  // TraceBuffer WITHOUT HOLDING LOCKS for performance reasons.
  virtual const perftools::tracing::TraceBuffer* GetTraceBuffer(
      TraceBufferAccess) const {
    return nullptr;
  }

  // Returns the tracer's typed annotation map if present.  The caller should
  // not keep a reference to this object because once the refcount drops to 0,
  // NotifyTraceConsumers() iterates over the TraceBuffer WITHOUT
  // HOLDING LOCKS for performance reasons.
  virtual perftools::tracing::typed_annotation::Map* GetTypedAnnotationMap() {
    return nullptr;
  }
  const perftools::tracing::typed_annotation::Map* GetTypedAnnotationMap()
      const {
    return const_cast<Tracer*>(this)->GetTypedAnnotationMap();
  }

  // Attach |c| to the list of TraceConsumers associated with this tracer
  // instance. Once the Tracer object loses all of its references, each
  // attached TraceConsumer will have an opportunity to inspect the Tracer and
  // its TraceRecord serialization.
  //
  // See the TraceConsumer interface definition below for more context.
  virtual void AttachTraceConsumer(TraceConsumer* c) = 0;

  // Installs a child sampler for this Tracer.
  //
  // NOTE: Users should invoke `perftools::tracing::Decimate` or one of its
  // variations, rather than invoking this method directly. See
  // http://<path>
  //
  // ChildSampler accessors are virtual since only some implementations of a
  // Tracer keep track of the child sampler object. Default implementation just
  // drops the object instead of keeping it around.
  //
  // NOTE: child sampler field is not thread-safe. If using it, set it before
  // the affected TraceContext is propagated to any other threads, preferably
  // immediately following span creation.
  virtual void SetChildSampler(std::unique_ptr<ChildSampler>,
                               ChildSamplerAccess) {}
  virtual ChildSampler* GetChildSampler() const { return nullptr; }

  // Returns the current reference count for testing purposes only.
  int32_t RefCountForTesting() const {
    return ref_count_.load(std::memory_order_acquire);
  }

  // Returns the total number of ticks accumulated in this Tracer. This value
  // is meaningless until it is scaled by the profiler's tick period. It is to
  // be used only by Dapper/Census internal libraries only.
  int32_t cpu_profile_ticks(Tracer::CpuProfileAccess) const {
    return cpu_profile_ticks_.load(std::memory_order_relaxed);
  }

  // Increments the CPU ticks counter by the specified number of ticks.
  // This is safe to call from async signal contexts.
  void add_cpu_profile_ticks_signal_safe(int ticks, Tracer::CpuProfileAccess) {
    cpu_profile_ticks_.fetch_add(ticks, std::memory_order_relaxed);
  }

  // Registers a notification to be called just before this Tracer's final
  // ref is released by the application.
  //
  // This is used to allow systems like Census to do additional work with the
  // Tracer without changing the Tracer's organic lifetime as recorded by
  // start/stop time.
  //
  // The notification will be called just before the Tracer's final ref is
  // released by the application. If the Tracer is ref-counted, the notification
  // may take a reference on this Tracer to prevent it from being destroyed, but
  // then the owner of the notification is responsible for releasing that
  // reference, thus allowing the Tracer to report its data to Dapper and be
  // destroyed.
  //
  // The Tracer does not own the notification pointer and will not delete it
  // after it's called. Only a single notification can be registered at a time.
  // Any subsequent attempts to override an existing notification with a
  // different one will fail.
  //
  // Requirements for callers to check before calling this function:
  //   - The notification must be a global pointer which never gets deleted.
  //   - The Tracer must be traced. This feature only works for ref-counted
  //     Tracers, and all Dapper-traced Tracers are ref-counted.
  //
  // This function returns true if either:
  //   - if the notification was successfully registered, OR
  //   - if the existing and new notifications were the same.
  // Otherwise, this function returns false.
  //
  // If this function returns true, then the notification will be called just
  // before the Tracer is about to be destroyed.
  //
  // For more details, see <link>.
  [[nodiscard]] bool NotifyBeforeDestroy(TracerNotification* new_notify,
                                         NotificationAccess);

 protected:
  // Tracing initiator ID bit definitions. For details, refer to
  // <link>.
  static constexpr uint64_t kInitiatorIdFormatMask{uint64_t{1} << 63};
  static constexpr uint64_t kInitiatedByLinkContexts{uint64_t{1} << 34};
  static constexpr uint64_t kTraceInitiatingSpan{uint64_t{1} << 33};
  static constexpr uint64_t kAdoptedInitiatorId{uint64_t{1} << 36};
  static constexpr uint64_t kQueryCostTracingEnabled{uint64_t{1} << 37};
  static constexpr uint64_t kHighValueTrace{uint64_t{1} << 38};
  static constexpr uint64_t kTracingCookie{uint64_t{1} << 39};
  static constexpr uint64_t kBucketedSampling{uint64_t{1} << 40};
  static constexpr uint64_t kBatchSampling{uint64_t{1} << 41};
  static constexpr uint64_t kSharedFate{uint64_t{1} << 42};
  static constexpr uint64_t kSpeculativeRoot{uint64_t{1} << 43};

  // Initiator ID type is stored in bits 32 and 35, and the initiator value
  // (whose interpretation depends on the type) in bits 0..31.
  static constexpr uint64_t kInitiatorTypeMask{uint64_t{0b1001} << 32};
  static constexpr uint64_t kInitiatorValueMask{0xFFFFFFFF};

  // Supported values for Initiator ID type.
  static constexpr uint64_t kInitiatorTypeWellKnown{uint64_t{0b0000} << 32};
  static constexpr uint64_t kInitiatorTypeProdUid{uint64_t{0b0001} << 32};
  static constexpr uint64_t kInitiatorTypeClientDevice{uint64_t{0b1000} << 32};
  static constexpr uint64_t kInitiatorTypeStringHash{uint64_t{0b1001} << 32};

  // The complete list of well-known initiator ID values can be found at
  // <link>.
  //
  // The value returned by initiator_id() when the context is not sampled (i.e.
  // is_traced() returns false).
  static constexpr uint64_t kNotSampled{0};
  // The value returned by initiator_id() when the context is sampled, but
  // initiator ID could not be obtained from the parent.
  static constexpr uint64_t kInitiatorNotSetByParent{1};

  friend class TraceChannel;

  // Default constructor initializes itself using an empty context.
  Tracer() = default;

  // Alternate constructor initializes itself from |tc|.
  explicit Tracer(const TraceContext& tc);

  // Adds a TraceStringFormatter-generated trace entry for 'channel_id'
  // (with an associated timestamp)
  void ChannelPrintFormattedString(
      perftools::tracing::channels::ChannelID channel_id,
      TraceStringFormatter formatter,
      absl::SourceLocation location = absl::SourceLocation::current()) {
    ChannelPrintFormattedStringImpl(channel_id, formatter, location);
  }

  // Overridable implementation of ChannelPrintFormattedString. Source location
  // captured by the default parameter of ChannelPrintFormattedStringImpl.
  virtual void ChannelPrintFormattedStringImpl(
      perftools::tracing::channels::ChannelID channel_id,
      TraceStringFormatter formatter, absl::SourceLocation location) = 0;

  // If any of the attached trace consumers are interested in this tracer,
  // serialize this tracer and pass it to every interested trace consumer.
  // Avoid serializing the tracer if no consumers are interested.
  //
  // See the IsInterestedIn() and ConsumeSerialized() methods in TraceConsumer.
  //
  // Every subclass of Tracer must implement this behavior.
  virtual void NotifyTraceConsumers() = 0;

  // Add a trace entry that consists of the specified string literal
  // for 'channel_id' (with an associated timestamp).
  //
  // Subclass implementer should override ChannelPrintLiteralImpl.
  void ChannelPrintLiteral(
      perftools::tracing::channels::ChannelID channel_id, const char* literal,
      absl::SourceLocation location = absl::SourceLocation::current()) {
    ChannelPrintLiteralImpl(channel_id, literal, location);
  }

  // Overridable implementation of ChannelPrintLiteral. Source location captured
  // by the default parameter of ChannelPrintLiteral.
  virtual void ChannelPrintLiteralImpl(
      perftools::tracing::channels::ChannelID channel_id, const char* literal,
      absl::SourceLocation location) = 0;

  // Add a trace entry that consists of the specified string
  // for 'channel_id' (with an associated timestamp).  Must
  // make a copy of the contents of "s" (unlike ChannelPrintLiteral).
  //
  // Subclass implementer should override ChannelPrintStringViewImpl.
  void ChannelPrintStringView(
      perftools::tracing::channels::ChannelID channel_id, absl::string_view s,
      absl::SourceLocation location = absl::SourceLocation::current()) {
    ChannelPrintStringViewImpl(channel_id, s, location);
  }

  // Overridable implementation of ChannelPrintStringView. Source location
  // captured by the default parameter of ChannelPrintStringView.
  virtual void ChannelPrintStringViewImpl(
      perftools::tracing::channels::ChannelID channel_id, absl::string_view s,
      absl::SourceLocation) = 0;

  // Adds a protocol message trace entry (by copying 'msg') to 'tracer', with
  // the provided timestamp and source location. Since constructing protocol
  // messages may be expensive, and trace annotations are not always logged, be
  // sure to check tracer->is_traced() before deciding to make a protocol
  // message trace annotation. For performance reasons, large protocol message
  // will be truncated to a "TypeName [%d bytes]" value.
  // See kMaxProtoLength in //perftools/tracing/internal/tracebuffer.cc.
  //
  // Derived classed should override ChannelPrintProtoImpl.
  void ChannelPrintProto(
      perftools::tracing::channels::ChannelID channel_id,
      const google::protobuf::Message& msg, absl::Time time,
      perftools::tracing::TraceSourceLocation location =
          perftools::tracing::TraceSourceLocation::current()) {
    ChannelPrintProtoImpl(channel_id, msg, time, location);
  }

  // Overridable implementation of ChannelPrintProto. Source location captured
  // by the default parameter of ChannelPrintProto.
  virtual void ChannelPrintProtoImpl(perftools::tracing::channels::ChannelID,
                                     const google::protobuf::Message&,
                                     absl::Time,
                                     perftools::tracing::TraceSourceLocation) {}

  // Sets the start time of this tracer to `Now()`. Implementations should call
  // `SetStartTimeNow()` or `SetStartTime()` exactly once at initialization.
  // This function is not thread safe.
  void SetStartTimeNow() { SetStartTime(absl::Now()); }

  // Sets the start time of this tracer to `time`. Implementations should call
  // `SetStartTimeNow()` or `SetStartTime()` exactly once at initialization.
  // This function is intended to be used only by noop / dummy tracers, and
  // implementations needing specific control on the start time of tracers.
  // `SetStartTimeNow()` is the preferred way to initialize the start time.
  // This function is not thread safe.
  ABSL_XRAY_ALWAYS_INSTRUMENT
  void SetStartTime(absl::Time time);

  // Sets the 'unref time' of this instance to `Now()`. This method must be
  // called by implementations from `OnRefCountZero()` which is invoked when
  // the final reference on this instance is released. This method also sets the
  // stop time if no stop time has yet been set. The `unref_time` must be set
  // exactly once. Requires that the tracer has been started.
  void SetUnrefTimeNow() { SetUnrefTime(absl::Now()); }

  // Sets the 'unref time' of this instance to `time`
  // Applications should prefer to use `SetUnrefTimeNow()` instead, this
  // function is intended mostly for testing purposes.
  // See `SetUnrefTimeNow()` for more information.
  ABSL_XRAY_ALWAYS_INSTRUMENT void SetUnrefTime(absl::Time time) {
    ABSL_RAW_DCHECK(time != absl::UnixEpoch(), "Invalid unref time");
    ABSL_RAW_DCHECK(has_start_time(), "Start time is not set");
    ABSL_RAW_DCHECK(!has_unref_time(), "Unref time already set");
    auto unref_time = base::TimeUnixNanos::FromTime(time);
    if (!has_stop_time()) {
      stop_time_.store(unref_time, std::memory_order_relaxed);
    }
    unref_time_.store(unref_time, std::memory_order_relaxed);
    XRAY_CAPTURE_RPC_UNREF(this);  // should be after setting unref_time_
  }

  ABSL_DEPRECATE_AND_INLINE() void set_unref_time(absl::Time time) {
    // Fully qualify for inlining.
    base::Tracer::SetUnrefTime(time);
  }

  // Only the TraceContext and subclasses are allowed to Ref and Unref
  // the Tracer.  Use by subclasses is reserved for keeping the object
  // alive after Stop (e.g., for /rpcz).
  friend class ::TraceContext;
  // TracerNotifications may need to resurrect the Tracer after its ref count
  // reaches zero, but before it gets destroyed, so they are allowed to call
  // Ref() and Unref() as well.
  friend class ::perftools::tracing::WrappedTracer;

  void Ref(void* owner) {
    tracker_.Ref(owner);
    ref_count_.fetch_add(1, std::memory_order_relaxed);
  }

  void Unref(void* owner) {
    tracker_.Unref(owner);
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) - 1 == 0) {
      UnrefSlow();
    }
  }

  bool UnrefNoDelete(void* owner) {
    tracker_.Unref(owner);
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) - 1 == 0) {
      return true;
    }
    return false;
  }

  void SwapRefOwner(void* old_owner, void* new_owner) {
    tracker_.Ref(new_owner);
    tracker_.Unref(old_owner);
  }

  // This should only be used in special cases where this data is not
  // available for the constructor, for example when Tracers are re-used.
  void set_tracer_attributes(uint64_t span_id, uint64_t parent_span_id,
                             uint64_t trace_id, uint32_t trace_mask) {
    span_id_ = span_id;
    parent_span_id_ = parent_span_id;
    trace_id_ = trace_id;
    trace_mask_.store(trace_mask, std::memory_order_relaxed);
    initiator_id_.store(is_traced() ? kInitiatorNotSetByParent : kNotSampled,
                        std::memory_order_relaxed);
  }

  // These need access to tracing_initiator() and/or initiator ID.
  // TraceSpan also needs set_is_leaf() and set_inverse_sampling_probability().
  template <typename T>
  friend class perftools::tracing::TraceSpan;
  friend class perftools::tracing::LinkContextsImpl;
  friend class perftools::tracing::LinkedTraceSpan;
  friend class perftools::tracing::SharedFateAccess;
  friend class perftools::tracing::SharedFateBitAccess;
  friend class perftools::tracing::StringHashInitiatorAccess;
  friend class perftools::tracing::HighValueTraceBitAccess;
  friend struct perftools::tracing::logging::TraceRecordGenerator;
  friend bool perftools::tracing::Sample(TraceContext* tc);
  friend void perftools::tracing::EnableSpeculativeTracing(TraceContext* tc);
  // Need access to add_child()/num_children().
  friend class perftools::tracing::TraceBuffer;
  friend class perftools::tracing::TraceStreamer;
  // Need access to set_span_type().
  friend class perftools::tracing::RequestTraceSpanTraits;
  friend class perftools::tracing::RpcTraceSpanState;
  friend class perftools::tracing::SimpleTraceSpanTraits;
  // Allow NoopMetadataTracer to set initiator_id and
  // inverse_sampling_probability.
  friend class perftools::tracing::NoopMetadataTracer;
  // Allow utils access initiator ID bits definition.
  friend class perftools::tracing::InitiatorIdBitsAccess;
  friend class perftools::tracing::TracerPredicates;
  // Allow Dapper OTLP converter to access initiator ID bits.
  friend struct dapper::InitiatorIdBitsAccess;
  friend class perftools::tracing::HTTPTraceInfo;
  // Allows tracing cookie code to set initiator bits correctly.
  friend class perftools::tracing::ClientRootedTracingCookie;
  // Allows initiator ID "batch sampled" bit to be set.
  friend class perftools::tracing::BatchedTraceContext;

  // Adds 1 to the total number of children for this span.
  void add_child() { num_children_.fetch_add(1, std::memory_order_release); }

  // Adds num_children to the total number of children for this span.
  void add_children(int32_t num_children) {
    num_children_.fetch_add(num_children, std::memory_order_release);
  }

  // Returns the number of children for this span. 0 means this trace span is
  // believed to be a leaf (i.e., has no child spans).
  int32_t num_children() const {
    return num_children_.load(std::memory_order_acquire);
  }

  // Meant for use by special trace span classes in special cases
  // when constructing Tracers.  These are NOT thread-safe and should only
  // be called before exposing the Tracer to other threads.
  // TODO: These values should be passed to the Tracer constructor.
  void set_span_type(int32_t span_type) {
    span_type_ = span_type;  // see TraceRecord::SpanType
  }

  void set_inverse_sampling_probability(double inverse_sampling_probability) {
    inverse_sampling_probability_ = inverse_sampling_probability;
  }

  void set_inverse_relative_skeletal_sampling_probability(
      double inverse_relative_sampling_probability,
      TraceContext::SkeletalTracingAccess) {
    inverse_relative_skeletal_sampling_probability_ =
        inverse_relative_sampling_probability;
  }

  // tracing_initiator is true if this span initiated tracing (i.e., it turned
  // on the kTraceMaskRPCTracingOn bit). This is used as a hint to the Dapper
  // pipeline for use when displaying the trace and deciding which process is
  // the "owner" of the trace for quota purposes.
  bool tracing_initiator() const {
    return initiator_id() & kTraceInitiatingSpan;
  }

  // Calculates and sets initiator ID value and metadata for the span initiating
  // the trace.
  void set_initiator_id();

  // Sets the "batch sampling" bit in the tracing initiator ID metadata.
  void set_initiated_by_batch_sampling();

  // Sets the initiator ID value to be the hash of a string and
  // updates the type selector in the initiator ID metadata accordingly.
  // Since this class cannot depend on Fingerprint2011 directly (dependency
  // cycle), it's done in a friend class, which is visibility-limited to the
  // frameworks implementing <link>.
  void set_initiator_id_string(uint32_t hash);

  // Propagates the initiator ID value and metadata from the parent span.
  void set_inherited_initiator_id(uint64_t value);
  // Propagates the initiator ID from the parent trace to the root span of the
  // child trace.
  void set_initiator_id_on_child_trace(uint64_t value);
  // Sets the initiator ID value and metadata to indicate that the parent's
  // initiator ID has been lost (due to incomplete instrumentation).
  void set_invalid_inherited_initiator_id();
  // Enables the query cost tracing feature, which is expressed as a bit in the
  // tracing initiator ID metadata. See <link> and
  // <link>.
  void enable_query_cost_tracing();
  // Sets the "high value trace" bit in the tracing initiator ID metadata.
  void set_high_value_trace();
  // Sets the "speculative root" bit in the tracing initiator ID metadata.
  void set_speculative_root();
  // Sets the "bucketed sampling" bit in the tracing initiator ID metadata.
  void set_initiated_by_bucketed_sampling();
  // Sets the "tracing cookie" bit in the tracing initiator ID metadata. Must
  // be called by the code applying the tracing cookie, therefore the
  // "initiating span" bit is also set.
  void set_initiated_by_tracing_cookie();
  // Enables this trace to have shared fate with all child traces by setting the
  // kSharedFate initiator id bit which forces the collector and depot writer
  // code to use the shared fate bits of the trace id.
  // IMPORTANT: this function is temporary and for internal use only!
  // TODO: b/474392195 - remove this after downsampling and sharding by the
  // shared fate bits in the trace id is the default.
  void internal_enable_shared_fate();

  // Updates the trace mask safely.
  void UpdateMask(uint32_t add, uint32_t remove);

 private:
  // Constant for time conversion computations.
  static constexpr double kNumNanosPerSecond = 1'000'000'000;

  // This is called during UnrefSlow() after the reference count drops
  // to zero.  The reference count will be raised to 1 prior to
  // calling this method, then dereferenced prior to exiting
  // UnrefSlow().  OnRefCountZero() may take references to the
  // object to keep it alive, subsequent Unref() calls will delete the
  // operation properly without calling Stop() again.
  // SetUnrefTimeNow() or SetUnrefTime() and NotifyTraceConsumers() must be
  // called inside the implementation of OnRefCountZero().
  virtual void OnRefCountZero() {
    Ref(this);
    SetUnrefTimeNow();
    NotifyTraceConsumers();
    Unref(this);
  }

  // Slow path for Unref(), which calls OnRefCountZero() if no stop
  // time has been set.
  void UnrefSlow() ABSL_ATTRIBUTE_COLD;

  // Attributes of this trace span.
  uint64_t span_id_ = 0;
  uint64_t parent_span_id_ = 0;
  uint64_t trace_id_ = 0;
  // Initiator ID is composed of a value in the lower 32 bits and metadata in
  // the higher 32 bits. The metadata describes how to interpret the value.
  std::atomic<uint64_t> initiator_id_ = kNotSampled;
  std::atomic<uint32_t> trace_mask_ = 0;
  int32_t span_type_ = 0;

  // The reciprocal of the sampling probability at the time this Tracer
  // was attached to a uniformly sampled trace span.  If not in a sampled
  // context (and if set_inverse_sampling_probability() is not otherwise
  // called) it defaults to 0.0.
  double inverse_sampling_probability_ = 0.0;

  // The reciprocal of the *relative* skeletal sampling probability. The actual
  // sampling probability is calculated by multiplying it with the relative
  // probabilities of all its ancestors.
  // If this Tracer is not skeletally sampled, or if this Tracer did not change
  // the skeletal sampling probability, this is 0.0.
  double inverse_relative_skeletal_sampling_probability_ = 0.0;

  absl::Time start_time_ = absl::UnixEpoch();

  // We want to enforce that atomic access of TimeUnixNanos is always lock-
  // free, but some platforms don't support lock-free access on 64 bit values.
  // So we assert on lock-free access only if int64_t is always lock-free.
  static_assert(!std::atomic<int64_t>::is_always_lock_free ||
                std::atomic<base::TimeUnixNanos>::is_always_lock_free);

  // Time when stopped; empty (unix epoch) if not stopped.
  std::atomic<base::TimeUnixNanos> stop_time_ = {};

  // Unref time; empty (unix epoch) if still referenced.
  std::atomic<base::TimeUnixNanos> unref_time_ = {};

  // Count of TraceContexts that reference this tracer.
  std::atomic<int32_t> ref_count_ = 0;

  // The number of children for this span. This is simply used as a hint to thr
  // dapper UI and collectors. Note: we do not care about the ordering of memory
  // operations on num_children_ with respect to other memory operations since
  // this is just a UI hint that gets logged and doesn't actually affect
  // anything in this process.
  std::atomic<int32_t> num_children_ = 0;

  // The number of CPU profiling ticks that has been spent while this Tracer was
  // installed in the current thread. Consumers *must not* use this value
  // directly, as it has no meaning without a profiling tick frequency. This is
  // meant to be used by Dapper/Census internal libraries only.
  std::atomic<int32_t> cpu_profile_ticks_ = 0;

  // An optional callback to invoked the first time the Tracer's reference count
  // drops to zero. This allows an application to capture the Tracer without
  // disrupting its natural lifetime as recorded in start_time/stop_time.
  // This should only be used by Census. For more details, see
  // <link>.
  std::atomic<TracerNotification*> notification_ = nullptr;

  // A struct that encapsulates the reference tracking done when the
  // ENABLE_TRACER_REF_TRACKING macro is defined. Centralizing logic here
  // reduces the number of #ifdef statements required to optionally support
  // reference tracking.
  //
  // In normal builds this is a no-op.
#ifdef ENABLE_TRACER_REF_TRACKING
  struct Tracker {
    std::unique_ptr<ReferenceTracker> ref_tracker =
        absl::GetFlag(FLAGS_tracer_debug_refcounts)
            ? std::make_unique<ReferenceTracker>()
            : nullptr;

    void Ref(void* owner) {
      if (ref_tracker != nullptr) {
        ref_tracker->Ref(owner);
      }
    }

    void Unref(void* owner) {
      if (ref_tracker != nullptr) {
        ref_tracker->Unref(owner);
      }
    }

    void GetReferenceTraces(
        std::vector<ReferenceTracker::StackTrace>* traces) const {
      if (ref_tracker != nullptr) {
        ref_tracker->GetReferenceTraces(traces);
      }
    }
  };
#else
  struct Tracker {
    void StartTracking() {}  // empty
    void Unref(void*) {}     // empty
    void Ref(void*) {}       // empty
    void GetReferenceTraces(std::vector<ReferenceTracker::StackTrace>*) const {
    }  // empty
  };
#endif

  // When the ENABLE_TRACER_REF_TRACKING macro is defined, an object that tracks
  // who has references to this tracer, to help debug context leaks.
  //
  // When ENABLE_TRACER_REF_TRACKING is not defined, this struct is empty.
  ABSL_ATTRIBUTE_NO_UNIQUE_ADDRESS Tracker tracker_;

#if LANG_CXX11
  Tracer(const Tracer&) = delete;
  Tracer& operator=(const Tracer&) = delete;
#else  // C++98
  Tracer(const Tracer&);
  Tracer& operator=(const Tracer&);
#endif
};

// Add a printf-style, StrFormat-formatted trace entry using 'channel_id' to
// 'tracer', with an associated timestamp.
template <typename... Args>
void TraceChannel::Printf(const absl::FormatSpec<Args...>& format,
                          const Args&... args) && {
  auto fmt = [&](char* buf, size_t len) -> int {
    perftools::tracing::FormatToBufferSink sink({buf, len});
    return absl::Format(&sink, format, args...)
               ? static_cast<int>(sink.total_size())
               : -1;
  };
  tracer_->ChannelPrintFormattedString(channel_id_, fmt, source_location_);
}

inline void TraceChannel::PrintFormattedString(
    TraceStringFormatter formatter, absl::SourceLocation location) && {
  tracer_->ChannelPrintFormattedStringImpl(channel_id_, formatter, location);
}

// Adds a string literal trace entry using 'channel_id' to 'tracer',
// with an associated timestamp.
inline void TraceChannel::PrintLiteral(const char* literal,
                                       absl::SourceLocation location) && {
  tracer_->ChannelPrintLiteral(channel_id_, literal, location);
}

// Adds a string trace entry using 'channel_id' and 's' to 'tracer',
// with an associated timestamp.
inline void TraceChannel::PrintString(absl::string_view s,
                                      absl::SourceLocation location) && {
  tracer_->ChannelPrintStringView(channel_id_, s, location);
}

// Adds a protocol message trace entry (by copying 'msg') to
// 'tracer', with an associated timestamp.  Since constructing
// protocol messages may be expensive, and trace annotations are not
// always logged, be sure to check tracer->is_traced() before
// deciding to make a protocol message trace annotation.  For
// performance reasons, large protocol message (see kMaxProtoLength
// in <path>) annotations will
// read "TypeName [%d bytes]".
template <class MessageT>
inline void TraceChannel::CopyAndPrintProtocolMessage(
    const MessageT& msg, absl::Time time,
    perftools::tracing::TraceSourceLocation location) && {
  tracer_->ChannelPrintProtoImpl(channel_id_, msg, time, location);
}

}  // namespace base

// Wrappers that are more convenient than manually accessing the
// current context's tracer and more efficient than unconditionally
// calling the Tracer annotation methods.
//
// Typical usage:
//    TRACELITERAL("foo");
//    TRACEPRINTF("here's a %s", "biscuit");
//
// A note on overhead:
// In the common case (the current context is not traced),
// these are ~9 ns no-ops on noconas and opterons, assuming
// no cache misses for accessing the current context.
// This is around as fast as an L2 cache hit, so while
// this may not be suitable for a tight inner loop, these
// should be fast enough for general use without having
// to worry about overheads.
//
// TRACELITERAL is usually faster than TRACEPRINTF in traced contexts,
// since Tracer implementations can copy pointers rather
// than entire strings. Also, see the traced benchmarks in
// stats/request/request_unittest.
//
// More detail from recent measurements:
// - Untraced context: all wrappers take ~9ns on nocona & opteron.
// - Traced context (i.e., the Tracer is a real statsrequest::Request):
//   - TRACELITERAL:                 41ns on opteron,   84ns on nocona
//   - TRACEPRINTF:
//     - short string (<30 chars):  524ns on opteron,  829ns on nocona
//     - long string (100+ chars): 1625ns on opteron, 2314ns on nocona

inline void TRACELITERAL(
    const char* literal,
    absl::SourceLocation location = absl::SourceLocation::current()) {
  const TraceContext* tc = TraceContext::Current();
  if (tc->CanRecordAnnotations()) {
    tc->tracer()->PrintLiteral(literal, location);
  }
}

// This is a macro so that (s) is not evaluated unless a tracer is
// attached.
#define TRACESTRING(s)                                               \
  do {                                                               \
    const TraceContext* current_context__ = TraceContext::Current(); \
    if (current_context__->CanRecordAnnotations()) {                 \
      current_context__->tracer()->PrintString(                      \
          s, absl::SourceLocation::current());                       \
    }                                                                \
  } while (0)

// We're forced to use a macro here since GCC will not inline
// a variadic function.
#define TRACEPRINTF(format, ...)                                         \
  do {                                                                   \
    const TraceContext* current_context__ = TraceContext::Current();     \
    if (current_context__->CanRecordAnnotations()) {                     \
      current_context__->tracer()->text().Printf((format), __VA_ARGS__); \
    }                                                                    \
  } while (0)

namespace base {

// Clients of Tracer can define their own TraceEntrySource subclasses
// and Attach() an instance of their trace entry source to a Tracer. This
// allows them to add and manage trace entries however and whenever they like,
// tuned to their needs.
//
// All they need to provide to a Tracer implementation:
// - an implementation of Emit() that gives access to all trace entries
//   accumulated so far
// - an implementation of Clone() that allows trace entries to persist
//   in some client-defined way if TraceEntrySource instances are outlived
//   by their hosting Tracers (e.g., if a TraceEntrySource instance contains
//   pointers to data that is only valid for a certain duration).
//
// TraceEntrySource implementations must be thread-safe.
class TraceEntrySource {
 public:
  virtual ~TraceEntrySource();

  // Emits all trace entries from "this" source into "*sink".
  // This method may be invoked from any arbitrary thread.
  virtual void Emit(TraceEntrySink* sink) = 0;

  // Returns null or a new TraceEntrySource object to replace this
  // TraceEntrySource. Called on all attached trace entry sources when a
  // Tracer's contents have to survive past the original lifetimes of the
  // attached entry sources.
  //
  // May return null in which case any data stored in this TraceEntrySource
  // will not be available after the source goes out of scope.
  //
  // May return a pointer to a new TraceEntrySource, whose ownership will pass
  // to the hosting Tracer.
  virtual TraceEntrySource* Clone() = 0;
};

// Clients of Tracer need not implement the TraceEntrySink interface,
// they only need to use it from their TraceEntrySource::Emit() implementation.
//
// Tracer subclass implementors should provide their own implementation
// of TraceEntrySink, which must be thread-safe.
class TraceEntrySink {
 public:
  virtual ~TraceEntrySink();

  // Adds the specified text at the specified time using the specified
  // channel_id.  A common channel is BaseChannelIDValues::TEXT_CHANNEL.  A
  // complete list of channel IDs may be found in:
  // //perftools/tracing/proto/channel_id.proto
  // Does not support source location.
  virtual void Emit(perftools::tracing::channels::ChannelID channel_id,
                    absl::Time time, const char* item, int item_length) = 0;

  // Same, but with source location support. Sinks able to record source
  // location should override this.
  virtual void EmitWithSourceLocation(
      perftools::tracing::channels::ChannelID channel_id, absl::Time time,
      absl::SourceLocation loc, const char* item, int item_length) {
    // Fallback for sinks that do not record source location.
    Emit(channel_id, time, item, item_length);
  };

  // EmitLazy takes a function-ptr and void*.  Implementations of
  // TraceEntrySink may choose to implement this method, for example,
  // in order to drop the annotation before paying the cost of
  // formatting for complex annotations.  It is not necessary to
  // define this method, if not, the default implementation will
  // (unconditionally) format the string and call Emit().
  //
  // The "arg" remains owned by the caller (i.e., a TraceEntrySource) and
  // only remains valid for the duration of the call to EmitLazy.
  virtual void EmitLazy(perftools::tracing::channels::ChannelID channel_id,
                        absl::Time time,
                        void (*function_ptr)(void* arg, std::string* out),
                        void* arg);
};

// An abstract interface for simple classes that optionally consume a
// "finalized" tracer (i.e., a tracer which has lost all of its references but
// has not yet been deleted).
class TraceConsumer {
 public:
  // TraceView provides a subset of the base::Tracer API for a given tracer.
  // This allows us some flexibility in when we actually attach things like
  // annotations to tracer objects.
  class TraceView {
   public:
    explicit TraceView(base::Tracer& tracer) : tracer_(tracer) {}

    std::string name() const { return tracer_.name(); }

    const perftools::tracing::typed_annotation::Map* GetTypedAnnotationMap()
        const {
      return tracer_.GetTypedAnnotationMap();
    }

    absl::Time get_start_time() const { return tracer_.get_start_time(); }

    absl::Time get_stop_time() const { return tracer_.get_stop_time(); }

    bool GetErrorStatus() const { return tracer_.GetErrorStatus(); }

    absl::Duration Elapsed() const { return tracer_.Elapsed(); }

    ABSL_DEPRECATE_AND_INLINE()
    double ElapsedSeconds() const { return absl::ToDoubleSeconds(Elapsed()); }

    bool is_traced_any_kind(TraceContext::SkeletalTracingAccess access) const {
      return tracer_.is_traced_any_kind(access);
    }

    uint64_t trace_id() const { return tracer_.trace_id(); }

    // TODO: b/343214111 - remove this function once all use cases are gone.
    std::string ToString() const { return tracer_.ToString(); }

   private:
    friend class TraceConsumer;
    base::Tracer& tracer_;
  };

  virtual ~TraceConsumer();

  // ShouldSerialize() should return true iff this consumer is interested in
  // examining the serialized form of |tracer|. If this method returns true,
  // ConsumeSerialized() will be called with a serialized tracer.
  virtual bool ShouldSerialize(const TraceView& trace_info) const = 0;

  // If and only if the TraceConsumer returned true from
  // ConsiderForSerialization() above, Consume() will be called with
  // |serialized| set to the serialized representation of the accumulated
  // entries in the Tracer.
  virtual void ConsumeSerialized(const TraceRecord& serialized) = 0;
};

// A strictly do-nothing implementation of the Tracer interface
// that is attached to untraced TraceContexts.
Tracer* GetNoopTracer();

// Returns a heap allocated strictly do-nothing implementation of the Tracer
// for testing purposes. The returned tracer can be adopted directly into
// a TraceContext using perftools::tracing::AdoptTracer()
std::unique_ptr<Tracer> GetNoopTracerForTesting();

}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_TRACER_H_
