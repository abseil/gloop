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

#include "gloop/perftools/tracing/string_base.h"

#include <cstring>
#include <ostream>

#include "absl/base/const_init.h"
#include "absl/base/no_destructor.h"
#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "gloop/util/gtl/lockfree_hashmap.h"

namespace perftools::tracing {

using InternMap = gtl::LockFreeHashMap<absl::string_view, bool>;
static absl::NoDestructor<absl::Mutex> intern_mutex(absl::kConstInit);
static absl::NoDestructor<InternMap> intern_map;

// This implementation uses a regular hash_set synchronized using a Mutex.
// The amortized cost becomes a cache miss for lookup heavy
// for looking up already interned values, minimizing the amortized cost.
absl::string_view StringBase::InternString(absl::string_view s) {
  // Early exit on empty string views.
  if (s.empty()) return absl::string_view("");

  // Fast path: already interned.
  if (auto it = intern_map->find(s); it != intern_map->end()) {
    return it->first;
  }

  // Create a heap allocated entry
  char* p = static_cast<char*>(::operator new(s.size()));
  memcpy(p, s.data(), s.size());
  s = absl::string_view(p, s.size());

  // Insert synchronized: we may have a benign race.
  absl::MutexLock lock(*intern_mutex);
  auto [it, inserted] = intern_map->insert({s, true});
  if (!inserted) {
    // Value already existed, we had a benign race with another thread.
    ::operator delete(p);
  }
  return it->first;
}

StringBase::Raw StringBase::Raw::CreateValue(absl::string_view s) {
  // We use explicit calls to ::new and sized ::delete as the standard
  // defaults to plain deletes for new T[] / delete[] if T is a trivial
  // type: https://en.cppreference.com/w/cpp/memory/new/operator_delete
  char* d = static_cast<char*>(::operator new(s.size()));
  if (!s.empty()) memcpy(d, s.data(), s.size());
  return Raw{Value(d), Metadata(ContentType::kDynamic, s.size())};
}

StringBase::Raw StringBase::Raw::CopyValue(Raw raw) {
  DCHECK_EQ(raw.metadata.type, ContentType::kDynamic);
  DCHECK_NE(raw.value.str, nullptr);
  return CreateValue({raw.value.str, raw.metadata.length_or_id});
}

void StringBase::Raw::DeleteValue(Raw raw) noexcept {
  DCHECK_EQ(raw.metadata.type, ContentType::kDynamic);
  auto* p = const_cast<char*>(raw.value.str);
#if defined(__cpp_sized_deallocation)
  ::operator delete(p, raw.metadata.length_or_id);
#else
  ::operator delete(p);
#endif
}

std::ostream& operator<<(std::ostream& stream, StringBase::Type type) {
  switch (type) {
    case StringBase::kEmpty:
      return stream << "Empty";
    case StringBase::kStringId:
      return stream << "StringId";
    case StringBase::kSourceLocation:
      return stream << "SourceLocation";
    case StringBase::kString:
      return stream << "String";
  }
  ABSL_UNREACHABLE();
  return stream << "UnknownType";
}

std::ostream& operator<<(std::ostream& stream, StringBase::ContentType type) {
  switch (type) {
    case StringBase::ContentType::kEmpty:
      return stream << "Empty";
    case StringBase::ContentType::kStringId:
      return stream << "StringId";
    case StringBase::ContentType::kSourceLocation:
      return stream << "SourceLocation";
    case StringBase::ContentType::kDynamic:
      return stream << "Dynamic";
    case StringBase::ContentType::kLiteral:
      return stream << "Literal";
    case StringBase::ContentType::kImmortal:
      return stream << "Immortal";
  }
  ABSL_UNREACHABLE();
  return stream << "UnknownContentType";
}

}  // namespace perftools::tracing
