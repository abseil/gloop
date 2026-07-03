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

#include "gloop/base/context_test_ffi.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "base/security_context_handle.h"
#include "gloop/util/refcount/reffed_ptr.h"
#include "net/base/peer.h"
#include "security/context/public/restricted/peer_access.h"
#include "security/context/public/security_context.h"
#include "security/context/testing/fake_security_context.h"
#include "security/credentials/public/principal.h"

namespace base::internal::context_test {

namespace sc = ::security::context;
namespace sct = ::security::context::testing;

std::optional<std::string> GetPeerUsername(const SecurityContextHandle sc) {
  if (sc == nullptr) {
    return std::nullopt;
  }

  const net_base::Peer* const peer =
      sc::PeerAccess::PeerFromSecurityContext(*sc);

  if (peer == nullptr) {
    return std::nullopt;
  }

  return peer->primary_role();
}

absl::StatusOr<SecurityContextHandle> BuildSecurityContext(
    std::string peer_username) {
  const refcount::reffed_ptr<net_base::Peer> peer = net_base::NewFakePeer({
      .primary_role = std::move(peer_username),
  });

  ASSIGN_OR_RETURN(
      std::unique_ptr<sc::SecurityContext> security_context,
      sct::FakeSecurityContextBuilder::WithUser(
          security::credentials::UserPrincipal::FromMdbUser(peer->username()))
          ->SetPeer(peer)
          .BuildValidated());

  return SecurityContextHandle(std::move(security_context));
}

}  // namespace base::internal::context_test
