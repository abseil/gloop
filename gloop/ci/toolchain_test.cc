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

#include <iostream>
#include <memory>
#include <string>
#include <vector>

// Prevent Clang from optimizing away memory access or sanitizer
// instrumentation.
void __attribute__((noinline)) TouchMemory(int* ptr) {
  volatile int* vptr = ptr;
  *vptr = 42;
}

int main(int argc, char** argv) {
  // 1. Long string to bypass Short String Optimization (SSO) and exercise
  // stdlib heap.
  std::string s =
      "gloop toolchain test with long string to bypass SSO allocation limit";
  std::cout << s << '\n';

  // 2. Dynamic heap allocation and pointer access to ensure ASAN/MSAN/TSAN
  // hooks.
  auto vec = std::make_unique<std::vector<int>>(100, 7);
  TouchMemory(vec->data());

  // 3. Volatile indexing and arithmetic to ensure UBSAN checks are retained.
  volatile int idx = argc > 0 ? 0 : 1;
  (*vec)[idx] += 1;

  return 0;
}
