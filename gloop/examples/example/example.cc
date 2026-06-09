#include "example/example.h"

#include <string>

#include "absl/status/status.h"
#include "example/example.pb.h"
#include "gloop/base/strtoint.h"
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
         ", MathUtil::kPi: " + std::to_string(MathUtil::kPi) +
         ", atoi32(): " + std::to_string(atoi32("64"));
}

}  // namespace examples
}  // namespace gloop
