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

// This file exists in an attempt to be ahead of lazy_static_ptr_test2.cc in
// link order, to test that order-of-initialization of global variables doesn't
// affect whether LazyStaticPtr variables work.

#include <string>

#include "gloop/util/gtl/lazy_static_ptr.h"
#include "gloop/util/gtl/lazy_static_ptr_test.h"

namespace gtl {

constexpr LazyStaticPtr<std::string, const char*> g_file_one_name("File One");

// Note that the following definition won't work, see test2.
// string g_file_one_string = "File One";

}  // namespace gtl
