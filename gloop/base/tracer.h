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

#include "absl/types/source_location.h"
#include "gloop/base/tracecontext.h"
#include "gloop/base/tracer_internal.h"  // IWYU pragma: export

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

#endif  // THIRD_PARTY_GLOOP_BASE_TRACER_H_
