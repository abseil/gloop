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

#ifndef THIRD_PARTY_GLOOP_BASE_TRACING_TYPES_H_
#define THIRD_PARTY_GLOOP_BASE_TRACING_TYPES_H_

#include <stddef.h>

#include <cstdint>

#include "absl/functional/function_ref.h"

namespace perftools {
namespace tracing {
namespace channels {

// The file //perftools/tracing/proto/channel_id.proto contains a
// ChannelIDValues enum with well-known channel ID values, along with
// instructions for defining your own channel IDs.
using ChannelID = int32_t;

}  // namespace channels
}  // namespace tracing
}  // namespace perftools

namespace base {

#ifndef SWIG
// Type-erased function for use with PrintFormattedString. The type erased
// formatter binds the `Printf(...)` arguments, and when invoked formats the
// bound arguments into the caller provided buffer.
//
// The formatter must format up to `len` bytes into `buf`, and return the
// total formatted size (excluding any terminating zeros), which may be
// higher than the provided `len`, or a negative value in case of error.
//
// The formatter can be called more than once: the internal implementation
// may optimize to format into a local stack buffer first as the assumption
// is that most formatted strings will be relatively short.
using TraceStringFormatter = absl::FunctionRef<int(char* buf, size_t len)>;
#endif  // !SWIG

// This fake enum class is needed to retain the implicit conversion between the
// enum type and the base type, without leaking all enumerators into base::.
namespace channel_id_values_internal {

// See //perftools/tracing/proto/channel_id.proto for the real enum.
// When new values are added here, please update the test in
// //perftools/tracing/proto:channel_id_test.

enum Impl : int32_t {
  TEXT_CHANNEL = 0,
  VERBOSE_CHANNEL = 10,
};

}  // namespace channel_id_values_internal

using ChannelIDValues = channel_id_values_internal::Impl;

}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_TRACING_TYPES_H_
