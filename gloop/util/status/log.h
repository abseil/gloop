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

// LOG_IF_ERROR macro to just log but otherwise ignore a status.

#ifndef THIRD_PARTY_GLOOP_UTIL_STATUS_LOG_H_
#define THIRD_PARTY_GLOOP_UTIL_STATUS_LOG_H_

#include <utility>

#include "absl/base/log_severity.h"  // IWYU pragma: export
#include "absl/status/status.h"
#include "absl/types/source_location.h"  // IWYU pragma: keep
#include "gloop/util/status/status_builder.h"
#include "gloop/util/status/status_macros.h"

// The LOG_IF_ERROR macro logs but otherwise ignores the returned status.
//
// E.g.
// LOG_IF_ERROR(WARNING, fd.Close());
// will log
// W0503 09:54:49.094197 9825 log.h:17] generic::internal: Failed
//
// LOG_IF_ERROR is a StatusBuilder so additional messages can be added:
// LOG_IF_ERROR(ERROR, DoSomething()).SetPrepend()
//    << "When trying to DoSomething: ";
// See
// https://github.com/abseil/gloop/tree/main/gloop/util/status/status_builder.h;rcl=890490215;l=124
// for documentation.

#define LOG_IF_ERROR(level, expr)                                              \
  STATUS_MACROS_IMPL_ELSE_BLOCKER_                                             \
  if (::util::status_macro_internal::StatusAdaptorForMacros status_adaptor = { \
          (expr), ::absl::SourceLocation::current()}) {                        \
  } else /* NOLINT */                                                          \
    ::util::status_macro_internal::StatusBuilderHolder{                        \
        status_adaptor.Consume()}                                              \
        .sb()                                                                  \
        .Log(::base_logging::level)

namespace util {
namespace status_macro_internal {

// Forces the coercion to Status at end of statement which will trigger the
// actual logging to occur.
class StatusBuilderHolder {
 public:
  explicit StatusBuilderHolder(StatusBuilder&& sb) : sb_(std::move(sb)) {}
  StatusBuilder& sb() { return sb_; }

  ~StatusBuilderHolder() {
    // Conversion to Status triggers the builder.
    // See //gloop/util/task/status_builder.h?l=376&rcl=244872841 for details.
    absl::Status(std::move(sb_)).IgnoreError();
  }

 private:
  StatusBuilder sb_;
};

}  // namespace status_macro_internal
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_STATUS_LOG_H_
