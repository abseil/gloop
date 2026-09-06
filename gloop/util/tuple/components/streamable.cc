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

#include "gloop/util/tuple/components/streamable.h"

#include <cstddef>

#include "absl/base/attributes.h"  // IWYU pragma: keep
#include "absl/log/check.h"

namespace util::tuple {

extern const default_writer_t<> default_writer = {};
extern const strappend_t strappend = {};

namespace internal_streamable {

namespace {

constexpr size_t kMaxRecursionDepth = 32;

ABSL_CONST_INIT const thread_local recursion_tracker*
    g_recursion_tracker_stack = nullptr;

}  // namespace

recursion_tracker::recursion_tracker(::size_t type_id, const void* obj)
    : type_id_(type_id), hook_(kNone), prev_hook_(nullptr) {
  size_t size = 0;
  for (const recursion_tracker* p = g_recursion_tracker_stack; p != nullptr;
       p = p->next_, ++size) {
    if (!prev_hook_ && p->type_id_ == type_id && p->obj_ == obj) {
      prev_hook_ = &p->hook_;
    }
  }
  if (size == kMaxRecursionDepth) {
    obj_ = nullptr;
    next_ = nullptr;
    return;
  }
  DCHECK_LT(size, kMaxRecursionDepth);
  obj_ = obj;
  next_ = g_recursion_tracker_stack;
  g_recursion_tracker_stack = this;
}

recursion_tracker::~recursion_tracker() {
  if (obj_ != nullptr) {
    DCHECK_EQ(g_recursion_tracker_stack, this);
    g_recursion_tracker_stack = next_;
  }
}

}  // namespace internal_streamable
}  // namespace util::tuple
