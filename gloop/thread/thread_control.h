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

#ifndef THIRD_PARTY_GLOOP_THREAD_THREAD_CONTROL_H_
#define THIRD_PARTY_GLOOP_THREAD_THREAD_CONTROL_H_

// Forward declare friend classes of DeprecatedThreadControl
class SandboxUtil;
class DeprecatedSingleThreadedTest;

namespace thread {

class DeprecatedThreadControl {
 public:
  // Indicate whether modules are allowed to make background threads
  static bool BackgroundThreadsAllowed();

  // AvoidBackgroundThreads() should be called before InitGoogle(). It
  // will stop background threads from being created in modules that
  // adhere to the API. However, modules are free to ignore this API
  // and create threads anyway. Consequently there may only be a few
  // low-level modules which adhere to it.
  // WARNING: THIS IS A TEMPORARY FEATURE AND WILL BE REMOVED.
  // All programs should be considered multi-threaded, this is a
  // temporary workaround to support a small number of programs that
  // need to be single threaded. Once those programs are able to support
  // multi-threaded execution, this workaround will be removed.
  // See <link> for more information.
  static void AvoidBackgroundThreads();

  // Allowlist classes that can call AvoidBackgroundThreads()
  friend class ::SandboxUtil;
  friend class ::DeprecatedSingleThreadedTest;
};

}  // end namespace thread
#endif  // THIRD_PARTY_GLOOP_THREAD_THREAD_CONTROL_H_
