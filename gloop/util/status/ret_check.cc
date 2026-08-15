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

#include "gloop/util/status/ret_check.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/die_if_null.h"
#include "absl/status/status.h"
#include "absl/types/source_location.h"
#include "gloop/util/status/status.h"
#include "gloop/util/status/status_builder.h"

ABSL_FLAG(bool, ret_check_abort_on_failure, false,
          "Forces RET_CHECK* macros to behave like CHECK and crash the "
          "process on failure.  It can be useful in some test cases, but not "
          "recommended in production.");

namespace util {
namespace internal_status_macros_ret_check {

absl::StatusBuilder RetCheckFailSlowPath(absl::SourceLocation location) {
  return InternalErrorBuilder(location)
             .Log(absl::GetFlag(FLAGS_ret_check_abort_on_failure)
                      ? absl::LogSeverity::kFatal
                      : absl::LogSeverity::kError)
             .EmitStackTrace()
         << "RET_CHECK failure (" << location.file_name() << ":"
         << location.line() << ") ";
}

absl::StatusBuilder RetCheckFailSlowPath(absl::SourceLocation location,
                                         std::string* condition) {
  std::unique_ptr<std::string> cleanup(condition);
  return RetCheckFailSlowPath(location) << *condition << " ";
}

absl::StatusBuilder RetCheckFailSlowPath(absl::SourceLocation location,
                                         const char* condition) {
  return RetCheckFailSlowPath(location) << condition << " ";
}

absl::StatusBuilder RetCheckFailSlowPath(absl::SourceLocation location,
                                         const char* condition,
                                         const absl::Status& status) {
  return RetCheckFailSlowPath(location)
         << condition << " returned " << ::util::StatusToString(status) << " ";
}

CheckOpMessageBuilder::CheckOpMessageBuilder(const char* exprtext)
    : stream_(new std::ostringstream) {
  *stream_ << exprtext << " (";
}

CheckOpMessageBuilder::~CheckOpMessageBuilder() { delete stream_; }

std::ostream* CheckOpMessageBuilder::ForVar2() {
  *stream_ << " vs. ";
  return stream_;
}

std::string* CheckOpMessageBuilder::NewString() {
  *stream_ << ")";
  return new std::string(stream_->str());
}

void MakeCheckOpValueString(std::ostream* os, char v) {
  if (v >= 32 && v <= 126) {
    (*os) << "'" << v << "'";
  } else {
    (*os) << "char value " << int{v};
  }
}

void MakeCheckOpValueString(std::ostream* os, signed char v) {
  if (v >= 32 && v <= 126) {
    (*os) << "'" << v << "'";
  } else {
    (*os) << "signed char value " << int{v};
  }
}

void MakeCheckOpValueString(std::ostream* os, unsigned char v) {
  if (v >= 32 && v <= 126) {
    (*os) << "'" << v << "'";
  } else {
    (*os) << "unsigned char value " << int{v};
  }
}

void MakeCheckOpValueString(std::ostream* os, std::nullptr_t) {
  (*os) << "nullptr";
}

void MakeCheckOpValueString(std::ostream* os, const char* v) {
  if (v == nullptr) {
    (*os) << "nullptr";
  } else {
    (*os) << v;
  }
}

void MakeCheckOpValueString(std::ostream* os, const signed char* v) {
  if (v == nullptr) {
    (*os) << "nullptr";
  } else {
    (*os) << v;
  }
}

void MakeCheckOpValueString(std::ostream* os, const unsigned char* v) {
  if (v == nullptr) {
    (*os) << "nullptr";
  } else {
    (*os) << v;
  }
}

void MakeCheckOpValueString(std::ostream* os, char* v) {
  if (v == nullptr) {
    (*os) << "nullptr";
  } else {
    (*os) << v;
  }
}

void MakeCheckOpValueString(std::ostream* os, signed char* v) {
  if (v == nullptr) {
    (*os) << "nullptr";
  } else {
    (*os) << v;
  }
}

void MakeCheckOpValueString(std::ostream* os, unsigned char* v) {
  if (v == nullptr) {
    (*os) << "nullptr";
  } else {
    (*os) << v;
  }
}

}  // namespace internal_status_macros_ret_check
}  // namespace util
