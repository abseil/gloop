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

#include "gloop/strings/interntable_benchmark.h"

#include "absl/base/attributes.h"

namespace strings {

ABSL_CONST_INIT const int kMinForManyTableSizes = 100;
ABSL_CONST_INIT const float kMultiplierForManyTableSizes = 1.11;
ABSL_CONST_INIT const int kMaxForManyTableSizes = 1 << 18;

}  // namespace strings
