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

// JoinFutures(transform, future_1, future_2, ...) returns a future representing
// the result of calling functor(future_1.Get(), future_2.Get(), ...).  No
// thread is used to wait for the results;
// functor is run on the last thread to return a value into one of the argument
// futures. This means functor shouldn't block, but since 'functor()' returns a
// Future itself, that shouldn't be a problem.
//
// This is useful for setting up a graph of dependent RPCs and sending
// each one as soon as its inputs are ready.
//
// JoinFutures() keeps (copies of) the argument futures alive until
// the last copy of its result is destroyed, so it's legal to return
// pointers into the arguments.
//
// For example:
//
//   Future<StubbyResponse<MyResult> > SendMyRequest(
//         MyServer* stub,
//         StubbyResponse<SpellingResult>* spelling,
//         StubbyResponse<QRewriteResult>* qrewrite) {
//     shared_ptr<MyRequest> request(new MyRequest);
//     request.set_foo(spelling->response.foo());
//     request.set_bar(qrewrite->response.bar());
//     return CallRpc(stub, &MyServer::GetData, request);
//   }
//
//   Future<StubbyResponse<SpellingResult> > spelling =
//       CallRpc(spell_stub, &SpellServer::CorrectSpelling, query);
//   Future<StubbyResponse<QRewriteResult> > qrewrite =
//       CallRpc(qrewrite_stub, &QRewriteServer::RewriteQuery, query);
//
//   // Does not block.  Schedules the request to be sent when
//   // both spelling and qrewrite have returned.
//   Future<StubbyResponse<MyResult> > future =
//       JoinFutures(&SendMyRequest,
//                   PastUnowned(stub), spelling, qrewrite);
//   ...
//   Use(future.Get());
//   ...
//
//
// Some caveats:
//
// For now, cancelling the result of JoinFuture has no effect at all
// on the input Futures.  We'd like to propagate the cancellation if
// and only if the cancelled Future is the only remaining user of the
// input, but Future doesn't have a way of saying that.  We can't just
// copy a null future on top of the argument because we might be in
// the middle of calling Get() on it, which would crash.
// TODO: To fix this, we could add an "interrupter" argument
// to Get().  We'd call that to make a call to Get return early, and
// then we could clear the input, at which point autocancellation
// would do what we want.
//
// The result of JoinFuture is also un-stealable, which means that
// it's prone to deadlock if you call Get() from an Executor on which
// one of the inputs expects to run.  TODO: I believe I can
// make the result stealable with a better implementation.  A
// nice-to-have would be, if several threads call Get() on the result,
// to have them cooperate to steal work from the input Futures, but
// this is impossible without a change to the Future interface.

#ifndef THIRD_PARTY_GLOOP_CONCURRENT_FUTURES_JOIN_H_
#define THIRD_PARTY_GLOOP_CONCURRENT_FUTURES_JOIN_H_

#include <stddef.h>

#include <functional>
#include <tuple>

#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "gloop/concurrent/barrier/barrier.h"
#include "gloop/concurrent/futures/creation.h"
#include "gloop/util/tuple/components/for_each.h"

#endif  // THIRD_PARTY_GLOOP_CONCURRENT_FUTURES_JOIN_H_
