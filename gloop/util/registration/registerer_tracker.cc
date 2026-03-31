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

#include "gloop/util/registration/registerer_tracker.h"

#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "gloop/util/registration/registerer.h"

// -vmodule=registerer_tracker=level controls the amount of logging.
// By default a summary of the total number of unused registered objects
// is printed at program exit when linked with this module.
// Log level 1 adds report of the files where the objects were declared,
// and the number of objects per file.
// Log level 2 adds the individual objects (which may have mangled C++ names).

namespace util_registration {
namespace internal {

void UnusedObjectReport() {
  std::map<std::string, std::set<std::string>> unused_objects;
  int total_unused = 0;
  for (const Registry* registry : Registry::GetAllRegistries()) {
    const std::vector<std::string> names = registry->GetNames();
    for (const auto& name : names) {
      const auto& item = registry->Lookup(name);
      if (!item.used) {
        if (unused_objects[item.filename].insert(name).second) {
          ++total_unused;
        }
      }
    }
  }
  if (total_unused == 0) return;

  LOG(INFO) << total_unused << " REGISTERed objects from "
            << unused_objects.size()
            << " files were unreferenced, "
               "set --vmodule=registerer_tracker=n for more details.";

  for (const auto& kv : unused_objects) {
    VLOG(1) << kv.first << " published " << kv.second.size()
            << " unreferenced objects.";
    for (const std::string& name : kv.second) {
      VLOG(2) << "Unreferenced object: " << name;
    }
  }
}
}  // namespace internal

}  // namespace util_registration
