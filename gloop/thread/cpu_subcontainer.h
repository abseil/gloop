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

// A thin utility around containerutils.h to create a new CPU
// subcontainer with a unique name.
#ifndef THIRD_PARTY_GLOOP_THREAD_CPU_SUBCONTAINER_H_
#define THIRD_PARTY_GLOOP_THREAD_CPU_SUBCONTAINER_H_

#include <string>

namespace thread {

class Options;

class CpuSubContainer {
 public:
  // This type is neither copyable nor movable.
  CpuSubContainer(const CpuSubContainer&) = delete;
  CpuSubContainer& operator=(const CpuSubContainer&) = delete;

  // Create a new subcontainer with scheduling "options". If the container
  // name "preferred_name" is not already taken, use that as the new container
  // name. Else, append a suffix to preferred_name to uniquify the name.  The
  // caller must delete the subcontainer object once it has finished using it.
  //
  // This function returns NULL if the kernel fails to create a container.
  static CpuSubContainer* Create(const thread::Options& options,
                                 const std::string& preferred_name);

  // ~Subcontainer destroys the corresponding kernel subcontainer.
  // REQUIRES: no thread is in this container.
  ~CpuSubContainer();

  // Move the calling thread to the container.
  bool RegisterThread();

 private:
  explicit CpuSubContainer(const std::string& path);

  const std::string path_;
};

}  // end namespace thread

#endif  // THREAD_SUBCONTAINER_H_
