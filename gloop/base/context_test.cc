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

// Context Core API implementation (see <link>)

#include "gloop/base/context.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "benchmark/benchmark.h"
#include "gloop/base/callback.h"
#include "gloop/base/censushandle.h"
#include "gloop/base/context_access.h"
#include "gloop/base/tracecontext.h"
#include "gloop/perftools/tracing/mock_trace_event_listener.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/thread/threadpool.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/functional/to_callback.h"
#include "gloop/util/refcount/reffed_ptr.h"
#include "gloop/util/status/status_macros.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using perftools::tracing::MockTraceEventListener;
using perftools::tracing::StringRef;
using ::testing::_;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Return;

ABSL_DECLARE_FLAG(bool, harden_with_context);

namespace base {

namespace {
#if BASE_CONTEXT_HAVE_SECURITYCONTEXT

// If we have Census tagging, then we can meaningfully test Census handle
// manipulation; if not, we should skip those tests.  We use
// BASE_CONTEXT_HAVE_SECURITYCONTEXT as a proxy for whether we have Census
// tagging available: it's not ideal, but in practice it works ok.
CensusHandle GetTestCensusHandle() {
  return stats_census::SetTags(CensusHandle(), {{"my_key", "my_val"}});
}

// Declare an alias to avoid some ifdef clutter when we cannot include peer.h.
using PeerOrFakePeer = net_base::Peer;

// Build a security context using the supplied peer and return it as a handle.
static absl::StatusOr<SecurityContextHandle> BuildSecurityContextHandle(
    const refcount::reffed_ptr<net_base::Peer>& peer) {
  ABSL_ASSIGN_OR_RETURN(
      std::unique_ptr<SecurityContext> sc,
      FakeUnvalidatedSecurityContextBuilder::WithPeer(peer.get())
          ->BuildUnvalidated());

  return SecurityContextHandle(std::move(sc));
}

#else
class FakePeer;

using PeerOrFakePeer = FakePeer;
#endif

}  // namespace

class ContextTest : public ::testing::Test {
 public:
  ContextTest()
#if BASE_CONTEXT_HAVE_SECURITYCONTEXT
      ,
      dummy_peer_(net_base::NewFakePeer({.primary_role = "dummy_role",
                                         .host = "dummy_host",
                                         .protocol = "dummy_protocol",
                                         .security_level = net_base::SSL_NONE})
                      .release())
#endif
  {
  }

  ~ContextTest() {
#if BASE_CONTEXT_HAVE_SECURITYCONTEXT
    dummy_peer_->Unref();
#endif
  }

  void TearDown() {
    Context d;
    RestoreCurrentContext(&d);
  }

#if BASE_CONTEXT_HAVE_SECURITYCONTEXT
  // Make a security context. Caller retains its reference to peer and ownership
  // of euc; both can be released immediately after this function returns.
  SecurityContext* MakeSecurity(const net_base::Peer* peer,
                                const EndUserCredentialsProto* euc) {
    auto builder = security::context::testing::
        FakeUnvalidatedSecurityContextBuilder::WithPeer(peer);
    if (euc) {
      builder->SetEndUserCreds(EndUserCreds::WrapProtoNoCopy(euc));
    }
    return builder->BuildUnvalidated().value().release();
  }

  std::unique_ptr<SecurityContext> GetTestSecurityContext() {
    SecurityContextParams params;
    params.set_peer(dummy_peer_);
    return absl::WrapUnique(
        security::context::BuildLegacyUnvalidated(params, nullptr));
  }
#else
  std::nullptr_t GetTestSecurityContext() { return nullptr; }
#endif

  const TraceContext* GetTestTraceContext() { return &test_tc_; }

  Context* GetTestContext() {
    return new Context(
        ContextBuilder(BackgroundContext())
            .set_trace_context(TraceContext(*GetTestTraceContext()))
            .set_deadline(kTestDeadline)
            .BuildValue());
  }

  Context GetTestContextValue() {
    return ContextBuilder(BackgroundContext())
        .set_trace_context(TraceContext(*GetTestTraceContext()))
        .set_deadline(kTestDeadline)
        .BuildValue();
  }

  const CensusHandle& GetCurrentHandle() {
    return CensusAccess::GetCurrentHandle();
  }

  void SwapCurrentHandle(CensusHandle* h) {
    CensusAccess::SwapCurrentHandle(h);
  }

 protected:
  TraceContext test_tc_;
  PeerOrFakePeer* dummy_peer_;

  // An arbitrary point in time, used when a test specifies a deadline.
  static const absl::Time kTestDeadline;
};

const absl::Time ContextTest::kTestDeadline =
    absl::UnixEpoch() + absl::Seconds(1);

namespace {

MATCHER_P(EqualsContext, expected, "") {
#if BASE_CONTEXT_HAVE_SECURITYCONTEXT
  if (arg.security() != expected.security()) {
    *result_listener << "security() mismatch";
    return false;
  }
  if (!stats_census::CensusHandlesEqual(arg.census_handle(),
                                        expected.census_handle())) {
    *result_listener << "census_handle() mismatch";
    return false;
  }
#endif
  if (arg.trace_context().rpc_id() != expected.trace_context().rpc_id()) {
    *result_listener << "trace_context().rpc_id() mismatch";
    return false;
  }
  if (arg.trace_context().global_id() != expected.trace_context().global_id()) {
    *result_listener << "trace_context().global_id() mismatch";
    return false;
  }
  if (arg.thread_status() != expected.thread_status()) {
    *result_listener << "thread_status() mismatch";
    return false;
  }
  if (arg.deadline() != expected.deadline()) {
    *result_listener << "deadline() mismatch";
    return false;
  }
  return true;
}

#if BASE_CONTEXT_HAVE_SECURITYCONTEXT
const net_base::Peer* peer(const Context& c) {
  return c.security() ? security::context::PeerAccess::PeerFromSecurityContext(
                            *c.security())
                      : nullptr;
}
#endif

static std::unique_ptr<Context> original_context;

TEST_F(ContextTest, DefaultInitIsConstExprAndDefaulted) {
  // Unfortunately type_traits<> does not provide a `is_constexpr_constructible`
  // or other alternative, and hand-rolling our own is ... likely painful (if at
  // all possible). So simply use the power of ABSL_CONST_INIT
  // Just check deadline to avoid 'unused' warnings.
  ABSL_CONST_INIT static Context context;  // NOLINT
  EXPECT_EQ(context.deadline(), absl::InfiniteFuture());
}

TEST_F(ContextTest, ThreadContextEqualsThreadContext) {
  std::unique_ptr<Context> context(GetTestContext());
  base::WithContext wc(*context);
  EXPECT_THAT(Context(Context::kThread), EqualsContext(*context));
}

// Validate initial parameters on CurrentContext().  Must be first test.
TEST_F(ContextTest, TestInitialState) {
  // We first test that a context initialized via the Context::kDefault
  // constructor is initialized as expected; then compare that with the
  // BackgroundContext().
  Context default_context;

  // DEFAULT: TraceContext constructed via TraceContext(TraceContext::kDefault)
  EXPECT_EQ(default_context.trace()->rpc_id(), 0);
  EXPECT_EQ(default_context.trace()->global_id(), 0);

  // DEFAULT: Deadline is infinite future.
  EXPECT_EQ(default_context.deadline(), absl::InfiniteFuture());

  // DEFAULT: Thread status is null.
  EXPECT_THAT(default_context.thread_status(), IsNull());

  // The background context is specified to be Context::kDefault constructed.
  EXPECT_THAT(default_context, EqualsContext(BackgroundContext()));

  // The (equivalent) Contexts below should all be default also.
  EXPECT_THAT(CurrentContext(), EqualsContext(BackgroundContext()));
  Context copy_current_thread_context(Context::kThread);
  EXPECT_THAT(default_context, EqualsContext(copy_current_thread_context));

  // Finally, take a copy of the current context so that 'TestFinalState' can
  // verify it was not perturbed by any test.
  original_context = std::make_unique<Context>(Context::kThread);
}

TEST_F(ContextTest, CopyAndAssign) {
  std::unique_ptr<Context> handle(GetTestContext());

  Context copy(*handle);
  EXPECT_THAT(*handle, EqualsContext(copy));

  Context eq;
  eq = *handle;
  EXPECT_THAT(*handle, EqualsContext(eq));

  // Do a reverse assignment to check internal reference counting
  *handle = eq;
  EXPECT_THAT(*handle, EqualsContext(eq));

  // Self-assignment
  Context self(GetTestContextValue());
  self = *&self;  // Avoid -Wself-assign.
}

TEST_F(ContextTest, MoveConstructor) {
  const Context ctx(GetTestContextValue());
  Context copied(ctx);
  Context moved(std::move(copied));
  EXPECT_THAT(ctx, EqualsContext(moved));
#if BASE_CONTEXT_HAVE_SECURITYCONTEXT
  // NOLINTNEXTLINE - we want to test that security was modified.
  EXPECT_NE(copied.security(), moved.security()) << "Security was moved.";
#endif  // BASE_CONTEXT_HAVE_SECURITYCONTEXT
}

TEST_F(ContextTest, MoveAssignment) {
  const Context ctx(GetTestContextValue());
  Context copied(ctx);
  Context moved;
  moved = std::move(copied);
  EXPECT_THAT(ctx, EqualsContext(moved));
#if BASE_CONTEXT_HAVE_SECURITYCONTEXT
  // NOLINTNEXTLINE - we want to test that security was modified.
  EXPECT_EQ(nullptr, copied.security()) << "Security was moved.";
#endif  // BASE_CONTEXT_HAVE_SECURITYCONTEXT

  // Self-assignment.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
#endif
  moved = std::move(moved);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  // NOLINTNEXTLINE - ClangTidy complains that the object was moved.
  EXPECT_THAT(ctx, EqualsContext(moved));
}

TEST_F(ContextTest, Initializers) {
  Context def;
  EXPECT_THAT(def, EqualsContext(BackgroundContext()));

  Context th(Context::kThread);
  EXPECT_THAT(th, EqualsContext(BackgroundContext()));

  {
    std::unique_ptr<Context> handle(GetTestContext());
    WithContext wc(*handle);
    Context th(Context::kThread);
    EXPECT_THAT(*handle, EqualsContext(th));
  }
}

TEST_F(ContextTest, AssignWithThreadStatus) {
  base::Context c1;
  base::Context c2;
  base::Context c3;
  base::Context c4;

  c1.set_thread_status("status 1");
  c2.set_thread_status("status 2");
  c3.set_thread_status("status 3");
  c4.set_thread_status("status 4");

  c1 = c2;
  c3 = std::move(c4);

  EXPECT_STREQ(c1.thread_status(), "status 2");
  EXPECT_STREQ(c3.thread_status(), "status 4");
}

TEST_F(ContextTest, AccessorsAndMutators) {
  Context ctx;
#if BASE_CONTEXT_HAVE_SECURITYCONTEXT
  const CensusHandle h = GetTestCensusHandle();
  ctx.set_census_handle(h);
  EXPECT_FALSE(stats_census::IsDefaultHandle(h));
#endif  // BASE_CONTEXT_HAVE_SECURITYCONTEXT
  ctx.set_thread_status("my status");
  {
    WithContext wc(ctx);
#if BASE_CONTEXT_HAVE_SECURITYCONTEXT
    EXPECT_TRUE(
        stats_census::CensusHandlesEqual(h, CurrentContext().census_handle()));
#endif  // BASE_CONTEXT_HAVE_SECURITYCONTEXT
    EXPECT_EQ("my status", CurrentContext().thread_status());
  }
}

TEST_F(ContextTest, Swap) {
  std::unique_ptr<Context> a(GetTestContext());
  Context a_value(*a);
  std::unique_ptr<Context> b(
      new Context(ContextBuilder(BackgroundContext()).BuildValue()));

  using std::swap;
  swap(*a, *b);
  EXPECT_THAT(a_value, EqualsContext(*b));
  EXPECT_THAT(BackgroundContext(), EqualsContext(*a));

  swap(*a, *b);
  EXPECT_THAT(BackgroundContext(), EqualsContext(*b));
  EXPECT_THAT(a_value, EqualsContext(*a));

  *b = std::move(*a);
  EXPECT_THAT(a_value, EqualsContext(*b));
}

#if BASE_CONTEXT_HAVE_SECURITYCONTEXT

TEST_F(ContextTest, SwapSecurity) {
  std::unique_ptr<Context> a(GetTestContext());

  refcount::reffed_ptr<net_base::Peer> inner_peer(
      net_base::NewFakePeer({.primary_role = "DIFFERENT",
                             .host = "dummy_host",
                             .protocol = "dummy_protocol",
                             .security_level = net_base::SSL_NONE})
          .release());
  SecurityContextParams params;
  params.set_peer(inner_peer.get());
  SecurityContext* inner_sc =
      security::context::BuildLegacyUnvalidated(params, nullptr);

  std::unique_ptr<Context> b(
      new Context(ContextBuilder(BackgroundContext())
                      .set_security_context(absl::WrapUnique(inner_sc))
                      .BuildValue()));

  using std::swap;
  swap(*a, *b);
  EXPECT_EQ("DIFFERENT", security::context::PeerAccess::PeerFromSecurityContext(
                             *a->security())
                             ->username());
  EXPECT_EQ(
      "dummy_role",
      security::context::PeerAccess::PeerFromSecurityContext(*b->security())
          ->username());

  swap(*a, *b);
  EXPECT_EQ(
      "dummy_role",
      security::context::PeerAccess::PeerFromSecurityContext(*a->security())
          ->username());
  EXPECT_EQ("DIFFERENT", security::context::PeerAccess::PeerFromSecurityContext(
                             *b->security())
                             ->username());

  *b = std::move(*a);
  EXPECT_EQ(
      "dummy_role",
      security::context::PeerAccess::PeerFromSecurityContext(*b->security())
          ->username());
}

TEST_F(ContextTest, SwapWithSecurityContext) {
  // Previous is null, new is null.
  {
    const SecurityContextHandle a;
    const WithContext wc{
        ContextBuilder(BackgroundContext())
            .set_security_context(a)
            .BuildValue(),
    };

    const SecurityContextHandle b;
    WithSecurityContext wsc(b);
    EXPECT_EQ(b.get(), CurrentContext().security());
  }

  // Previous is null, new is non-null.
  {
    const SecurityContextHandle a;
    const WithContext wc{
        ContextBuilder(BackgroundContext())
            .set_security_context(a)
            .BuildValue(),
    };

    ASSERT_OK_AND_ASSIGN(const SecurityContextHandle b,
                         BuildSecurityContextHandle(net_base::NewFakePeer({})));

    WithSecurityContext wsc(b);
    EXPECT_EQ(b.get(), CurrentContext().security());
  }

  // Previous is non-null, new is null.
  {
    ASSERT_OK_AND_ASSIGN(const SecurityContextHandle a,
                         BuildSecurityContextHandle(net_base::NewFakePeer({})));

    const WithContext wc{
        ContextBuilder(BackgroundContext())
            .set_security_context(a)
            .BuildValue(),
    };

    const SecurityContextHandle b;

    WithSecurityContext wsc(b);
    EXPECT_EQ(b.get(), CurrentContext().security());
  }

  // Previous is non-null, new is non-null.
  {
    ASSERT_OK_AND_ASSIGN(const SecurityContextHandle a,
                         BuildSecurityContextHandle(net_base::NewFakePeer({})));

    const WithContext wc{
        ContextBuilder(BackgroundContext())
            .set_security_context(a)
            .BuildValue(),
    };

    ASSERT_OK_AND_ASSIGN(const SecurityContextHandle b,
                         BuildSecurityContextHandle(net_base::NewFakePeer({})));

    WithSecurityContext wsc(b);
    EXPECT_EQ(b.get(), CurrentContext().security());
  }
}

#endif  // BASE_CONTEXT_HAVE_SECURITYCONTEXT

TEST_F(ContextTest, CopiesAndWithContextDontLeak) {
  const Context& initial_state = CurrentContext();
  Context* c = new Context(ContextBuilder(initial_state).BuildValue());
  delete c;

  std::unique_ptr<Context> handle(GetTestContext());
  c = new Context(ContextBuilder(initial_state).BuildValue());
  {
    WithContext wc(*c);
  }
  {
    WithContext wc(*c);
  }
  delete c;
}

#if BASE_CONTEXT_HAVE_SECURITYCONTEXT
TEST_F(ContextTest, TestCreateAndSetSecurityContext) {
  {
    std::unique_ptr<Context> handle(GetTestContext());
    WithContext new_scope(*handle);
    EXPECT_EQ(peer(CurrentContext())->primary_role(), "dummy_role");

    {
      // Replace just the security context, retaining the trace context
      net_base::Peer* inner_peer =
          net_base::NewFakePeer({.primary_role = "DIFFERENT",
                                 .host = "dummy_host",
                                 .protocol = "dummy_protocol",
                                 .security_level = net_base::SSL_NONE})
              .release();

      SecurityContextParams params;
      params.set_peer(inner_peer);
      std::unique_ptr<Context> inner_context(new Context(
          ContextBuilder(CurrentContext())
              .set_security_context(absl::WrapUnique(
                  security::context::BuildLegacyUnvalidated(params, nullptr)))
              .BuildValue()));
      WithContext inner_scope(*inner_context);
      ASSERT_EQ(peer(CurrentContext())->primary_role(), "DIFFERENT");
      inner_peer->Unref();
    }

    // Replace just the trace context, retaining the security context
    {
      std::unique_ptr<Context> inner_context(
          new Context(ContextBuilder(CurrentContext())
                          .set_trace_context(TraceContext(2, 0, 0, 0))
                          .BuildValue()));
      WithContext inner_scope(*inner_context);
      EXPECT_EQ(rpc_id(CurrentContext()), 2);
      ASSERT_EQ(peer(CurrentContext())->primary_role(), "dummy_role");
    }

    // Clear the security context by setting it to null.
    {
      const WithContext inner_context{
          ContextBuilder(CurrentContext())
              .set_security_context(nullptr)
              .BuildValue(),
      };

      EXPECT_THAT(peer(CurrentContext()), IsNull());
    }

    // Ensure that the previous context was restored
    ASSERT_EQ(peer(CurrentContext())->primary_role(), "dummy_role");
  }

  // Ensure that the initial application context was restored.
  EXPECT_THAT(peer(CurrentContext()), IsNull());
  EXPECT_EQ(rpc_id(CurrentContext()), 0);
}

// Tests NULL argument to set_security_context.
TEST_F(ContextTest, TestSetNullSecurityContext) {
  std::unique_ptr<Context> handle(GetTestContext());
  WithContext new_scope(*handle);
  {
    // Replace security context with NULL, leave trace context alone.
    std::unique_ptr<Context> inner_context(
        new Context(ContextBuilder(CurrentContext())
                        .set_security_context(nullptr)
                        .BuildValue()));
    WithContext inner_scope(*inner_context);
    EXPECT_EQ(rpc_id(CurrentContext()), 1);
    EXPECT_THAT(CurrentContext().security(), IsNull());
  }
  // Previous context is restored.
  EXPECT_EQ(peer(CurrentContext())->primary_role(), "dummy_role");
  EXPECT_EQ(rpc_id(CurrentContext()), 1);
}
#endif  // BASE_CONTEXT_HAVE_SECURITYCONTEXT

// Tests building Contexts with deadlines.
TEST_F(ContextTest, BuildWithDeadline) {
  // An arbitrary deadline, != default, != kTestTime, deadline.
  absl::Time arbitrary_deadline = kTestDeadline + absl::Seconds(1);

  // We should be able to set and run with this deadline.
  std::unique_ptr<Context> c1(new Context(ContextBuilder(BackgroundContext())
                                              .set_deadline(arbitrary_deadline)
                                              .BuildValue()));
  EXPECT_EQ(arbitrary_deadline, c1->deadline());

  // A copy built from c1 should duplicate the deadline.
  std::unique_ptr<Context> c2(new Context(ContextBuilder(*c1).BuildValue()));
  EXPECT_EQ(arbitrary_deadline, c2->deadline());

  // Running within c1 above, we should still be able to build a 'clean' context
  // relative to the BackgroundContext().
  {
    WithContext wc(*c1);
    EXPECT_EQ(arbitrary_deadline, CurrentContext().deadline());

    std::unique_ptr<Context> bgcopy(
        new Context(ContextBuilder(BackgroundContext()).BuildValue()));
    EXPECT_EQ(absl::InfiniteFuture(), bgcopy->deadline());
    {
      WithContext wc(*bgcopy);
      EXPECT_EQ(absl::InfiniteFuture(), CurrentContext().deadline());
    }
  }
}

// Test that building a heap-allocated Context gives the same
// result as returning it by value.
TEST_F(ContextTest, TestBuildValueEquivalence) {
  std::unique_ptr<Context> context_ref(GetTestContext());
  Context context_value(GetTestContextValue());
#if BASE_CONTEXT_HAVE_SECURITYCONTEXT
  ASSERT_EQ(security::context::PeerAccess::PeerFromSecurityContext(
                *context_ref->security())
                ->username(),
            security::context::PeerAccess::PeerFromSecurityContext(
                *context_value.security())
                ->username());
#endif
  ASSERT_EQ(context_ref->trace()->rpc_id(), context_value.trace()->rpc_id());
  ASSERT_EQ(context_ref->deadline(), context_value.deadline());
}

// Creating a builder without calling Build() should not leak.
TEST_F(ContextTest, TestNoBuild) {
  std::unique_ptr<Context> handle(GetTestContext());
  WithContext new_scope(*handle);
  ContextBuilder b1(CurrentContext());
}

#if BASE_CONTEXT_HAVE_SECURITYCONTEXT
void CheckEucValidity(absl::Notification* n) {
  const EndUserCredentialsProto* euc =
      security::context::CurrentEuc(security::context::FOR_TEST);
  ASSERT_THAT(euc, NotNull());
  // Make a copy to ensure the pointer is still valid.
  EndUserCredentialsProto copy(*euc);
  ASSERT_EQ(copy.credential(0).type(), AuthenticatorProto::GAIA_MINT);
  ASSERT_EQ(copy.credential(0).gaia_mint_wrapper(), "fake-gaia-mint");
  n->Notify();
}

// Simulate the pattern of an incoming Stubby RPC which spawns
// a child callback that outlives the RPC (run in a separate thread).
// The original Context is never invalidated (never has EUC removed).
//
// We ensure that the long-lived child can still access its EUC correctly
// even after the parent RPC is completed.
//
// This test was verified to fail prior to CL 45405075 being submitted and
// to succeed afterwards.
TEST_F(ContextTest, TestLongLivedCallback) {
  auto euc = std::make_unique<EndUserCredentialsProto>();
  AuthenticatorProto* auth = euc->add_credential();
  auth->set_type(AuthenticatorProto::GAIA_MINT);
  auth->set_gaia_mint_wrapper("fake-gaia-mint");
  SecurityContext* rsc = MakeSecurity(dummy_peer_, euc.get());
  absl::Notification n;

  Closure* check_euc;
  {
    std::unique_ptr<Context> handle(
        new Context(ContextBuilder(BackgroundContext())
                        .set_security_context(absl::WrapUnique(rsc))
                        .BuildValue()));
    WithContext new_scope(*handle);
    // Inherits the current context, including EUC
    check_euc = ::util::functional::ToCallback([&n] { CheckEucValidity(&n); });
  }
  euc.reset();

  ThreadPool pool(2, ThreadPool::Options{.name_prefix = "testpool"});
  pool.Schedule(util::functional::FromCallback(check_euc));
  EXPECT_TRUE(
      n.WaitForNotificationWithTimeout(absl::Seconds(2)));  // give up after 2s
}

// Checks that the current security context has the indicated GaiaMint.
void ExpectGaiaMintContext(std::string expected_mint_wrapper) {
  const EndUserCredentialsProto* euc =
      security::context::CurrentEuc(security::context::FOR_TEST);
  ASSERT_THAT(euc, NotNull());
  EXPECT_GT(euc->credential_size(), 0);
  EXPECT_EQ(euc->credential(0).gaia_mint_wrapper(), expected_mint_wrapper);
}
#endif  // BASE_CONTEXT_HAVE_SECURITYCONTEXT

void ExpectDeadline(absl::Time deadline) {
  EXPECT_EQ(CurrentContext().deadline(), deadline);
}

// A permanent callback does not capture the current context at creation.
// When invoked, it runs with the then-current context.
TEST_F(ContextTest, TestPermanentCallback) {
  std::unique_ptr<Closure> permanent_cb;
#if BASE_CONTEXT_HAVE_SECURITYCONTEXT
  EndUserCredentialsProto euc1;
  AuthenticatorProto* auth1 = euc1.add_credential();
  auth1->set_type(AuthenticatorProto::GAIA_MINT);
  auth1->set_gaia_mint_wrapper("abc");
  std::unique_ptr<SecurityContext> sec1(MakeSecurity(dummy_peer_, &euc1));
  EndUserCredentialsProto euc2;
  AuthenticatorProto* auth2 = euc2.add_credential();
  auth2->set_type(AuthenticatorProto::GAIA_MINT);
  auth2->set_gaia_mint_wrapper("xyz");
  std::unique_ptr<SecurityContext> sec2(MakeSecurity(dummy_peer_, &euc2));

  {
    WithSecurityContext c(std::move(sec1));
    EXPECT_THAT(CurrentContext().security(), NotNull());
    permanent_cb.reset(::util::functional::ToPermanentCallback(
        absl::bind_front(&ExpectGaiaMintContext, "xyz")));
  }
  {
    WithSecurityContext c(std::move(sec2));
    EXPECT_THAT(CurrentContext().security(), NotNull());
    permanent_cb->Run();  // runs with 'sec2' context
  }
#endif  // BASE_CONTEXT_HAVE_SECURITYCONTEXT
  {
    permanent_cb.reset(::util::functional::ToPermanentCallback(
        absl::bind_front(&ExpectDeadline, kTestDeadline)));
    std::unique_ptr<Context> handle(GetTestContext());
    WithContext wc(*handle);
    permanent_cb->Run();
  }
}

TEST_F(ContextTest, WithDeadline) {
  absl::Time deadline = absl::Now() + absl::Seconds(60);
  std::unique_ptr<Context> handle(new Context(
      ContextBuilder(CurrentContext()).set_deadline(deadline).BuildValue()));
  WithContext wc(*handle);
  ASSERT_EQ(CurrentContext().deadline(), deadline);

  {
    absl::Time new_deadline = deadline - absl::Seconds(30);
    WithDeadline sd(new_deadline);
    EXPECT_EQ(CurrentContext().deadline(), new_deadline);

    {
      absl::Time nested_new_deadline = new_deadline - absl::Seconds(10);
      WithDeadline nested(nested_new_deadline);
      EXPECT_EQ(CurrentContext().deadline(), nested_new_deadline);
    }
  }

  {
    absl::Time new_deadline = deadline + absl::Seconds(30);
    WithDeadline sd(new_deadline);
    EXPECT_EQ(CurrentContext().deadline(), deadline);
  }
}

#if BASE_CONTEXT_HAVE_SECURITYCONTEXT
TEST_F(ContextTest, WithCensusHandle) {
  CensusHandle h = GetTestCensusHandle();
  EXPECT_FALSE(stats_census::IsDefaultHandle(h));
  EXPECT_TRUE(stats_census::IsDefaultHandle(CurrentContext().census_handle()));
  {
    WithCensusHandle wch(h);
    EXPECT_TRUE(
        stats_census::CensusHandlesEqual(h, CurrentContext().census_handle()));
  }
}
#endif  // BASE_CONTEXT_HAVE_SECURITYCONTEXT

TEST_F(ContextTest, WithThreadStatus) {
  EXPECT_THAT(CurrentContext().thread_status(), IsNull());
  {
    WithThreadStatus w("my_status");
    EXPECT_STREQ(CurrentContext().thread_status(), "my_status");
  }
  EXPECT_THAT(CurrentContext().thread_status(), IsNull());
}

TEST_F(ContextTest, WithContextHardenedAgainstIllegalScope) {
  absl::SetFlag(&FLAGS_harden_with_context, true);
  // Note that we are ok if this test dangles a default context.
  Context context1;
  Context context2;

  std::optional<WithContext> wc1(context1);
  WithContext wc2(context2);
  EXPECT_DEATH_IF_SUPPORTED(wc1 = std::nullopt, ".*");
}

TEST_F(ContextTest, WithContextHardeningDisableThroughFlag) {
  absl::SetFlag(&FLAGS_harden_with_context, false);
  Context context1;
  Context context2;

  std::optional<WithContext> wc1(context1);
  WithContext wc2(context2);
  wc1 = std::nullopt;
}

////////////////////////////////////////////////////////////////////////
// Benchmarks
////////////////////////////////////////////////////////////////////////

}  // anonymous namespace
}  // namespace base
