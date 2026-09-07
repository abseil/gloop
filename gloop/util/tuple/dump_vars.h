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

// This file is a historical include that redirects to the dump_vars.h header in
// a separate directory that plays nicely with <link>, which has stricter
// dependency rules. Use that header in new code, particulary code that has
// components dependency rules.
//
// IWYU pragma: private, include "util/tuple/components/dump_vars.h"

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_DUMP_VARS_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_DUMP_VARS_H_

#include "gloop/util/tuple/components/dump_vars.h"  // IWYU pragma: keep

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_DUMP_VARS_H_
