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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_LAZY_STATIC_PTR_TEST_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_LAZY_STATIC_PTR_TEST_H_

#include <string>

#include "gloop/util/gtl/lazy_static_ptr.h"

// These globals are shared by the lazy_static_ptr_test[123].cc sources.  They
// are declared here to make sure their types match.

namespace gtl {

extern const LazyStaticPtr<std::string, const char*> g_file_one_name;
extern const LazyStaticPtr<std::string, const char (&)[11]> g_file_three_name;

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_LAZY_STATIC_PTR_TEST_H_
