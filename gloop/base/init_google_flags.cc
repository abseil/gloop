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

#include "gloop/base/init_google_flags.h"

#include <string>

#include "absl/flags/flag.h"
#include "absl/strings/string_view.h"

ABSL_FLAG(std::string, chroot, "",
          "If root, chroot to this directory.  "
          "If \"env\", use (and erase) env var $CHROOT instead.");

#if defined(__APPLE__) || defined(__ANDROID__)
// Turn on silent_inits by default for non google server platforms  because in
// general this is just a massive amount of unnecessary log spam in builds.
static const bool silent_init_flag_default = true;
#else
static const bool silent_init_flag_default = false;
#endif  // defined(__APPLE__) || defined(__ANDROID__)

ABSL_FLAG(bool, silent_init, silent_init_flag_default,
          "No log message on initialization");
