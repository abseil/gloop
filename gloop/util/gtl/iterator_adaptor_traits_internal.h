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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_ITERATOR_ADAPTOR_TRAITS_INTERNAL_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_ITERATOR_ADAPTOR_TRAITS_INTERNAL_H_

#include <type_traits>

namespace gtl {
namespace internal_gtl {

// Trait specialized by stateless GTL view adaptors (such as `key_view_t`,
// `value_view_t`, `deref_view_t`, and stateless `projection_view_t`) to
// decompose the view into `std::pair<const Container&, Extractor>`, allowing
// `gtl::InputView` to bind directly to the underlying container and stateless
// extractor rather than the temporary view adaptor object.
template <typename T>
struct UnpackStaticView;

// Trait marking built-in GTL subobject/pointee extractors (`FirstExtractor`,
// `SecondExtractor`, `DereferencingExtractor`, `DereferencingSecondExtractor`)
// whose `operator()` takes `T&&` by forwarding reference without materializing
// a converted parameter temporary. When applied to an lvalue container element,
// the extracted reference refers directly to persistent storage in the
// container, so `gtl::InputView` can point to or convert it without copying the
// full element into `Preserver`.
template <typename T>
struct ExtractorGuaranteedNoTemporaries : std::false_type {};

}  // namespace internal_gtl
}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_ITERATOR_ADAPTOR_TRAITS_INTERNAL_H_
