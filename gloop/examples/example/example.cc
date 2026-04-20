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

#include "example/example.h"

#include <string>

#include "absl/status/status.h"
#include "example/example.pb.h"
#include "gloop/net/base/ipaddress.h"
#include "gloop/util/math/mathutil.h"
#include "gloop/util/status/codes.pb.h"
#include "gloop/util/status/ret_check.h"
#include "gloop/util/status/status.h"
#include "gloop/util/status/status.pb.h"

namespace gloop {
namespace examples {

using gloop::examples::Example;
using util::StatusProto;
using util::error::Code;

absl::Status GetOKStatus() {
  RET_CHECK_OK(absl::OkStatus());
  return absl::OkStatus();
}

std::string GetExampleMessage() {
  Example example;
  example.set_code(Code::OK);
  StatusProto* status_proto = example.mutable_status();
  status_proto->set_canonical_code(Code::CANCELLED);

  absl::Status status = GetOKStatus();
  return "Hello from Gloop Example! Example code: " +
         std::to_string(example.code()) + ", Example status.canonical_code: " +
         std::to_string(example.status().canonical_code()) +
         ", util::StatusToString(status): " + util::StatusToString(status) +
         ", net_base::IPAddress::Loopback6(): " +
         net_base::IPAddress::Loopback6().ToString() +
         ", MathUtil::kPi: " + std::to_string(MathUtil::kPi);
}

}  // namespace examples
}  // namespace gloop
