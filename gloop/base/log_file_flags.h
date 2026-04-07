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

#ifndef THIRD_PARTY_GLOOP_BASE_LOG_FILE_FLAGS_H_
#define THIRD_PARTY_GLOOP_BASE_LOG_FILE_FLAGS_H_

#include <string>

#include "absl/flags/declare.h"
#include "absl/log/internal/config.h"

// If specified, or if the `GOOGLE_LOG_DIR` environment variable is set,
// logfiles are written into this directory instead of the default logging
// directory.
ABSL_DECLARE_FLAG(std::string, log_dir);

// When logging a new message, force a flush if any buffered messages are older
// than this. NOTE: this does *not* imply buffered log messages get flushed
// after this many seconds, or ever -- they are not flushed unless a new log
// message arrives.
ABSL_DECLARE_FLAG(int, logbufsecs);

// Sets the maximum log file size in MB.  Defaults to 200 MB unless the
// `GOOGLE_MAX_LOG_MB` environment variable is set.
ABSL_DECLARE_FLAG(int, max_log_size);

// If true, logging will cease if the disk appears to be full.  Defaults to
// false.
ABSL_DECLARE_FLAG(bool, stop_logging_if_full_disk);

// If specified, a symbolic link to each logfile is put in this directory.
ABSL_DECLARE_FLAG(std::string, log_link);

#endif  // THIRD_PARTY_GLOOP_BASE_LOG_FILE_FLAGS_H_
