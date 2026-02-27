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

// Copyright 2004 Google Inc.
// All Rights Reserved.
//

// Functions that copy specified bits from one location to another

#ifndef THIRD_PARTY_GLOOP_UTIL_BITS_BIT_COPY_H_
#define THIRD_PARTY_GLOOP_UTIL_BITS_BIT_COPY_H_

#include "absl/base/nullability.h"

// Copy the (src_offset + i)th bit of src to the (dest_offset + i)th
// bit of vdest, for i = [0, nbits).
// This is a generic function that works for any type of vdest and src
// as long as the source/destination positions are allocated in byte level,
// e.g., if copying the 10th bit of src to the 20th bit of vdest,
// then the src should have at least two bytes, and the vdest
// should have at least 3 bytes.
//
// Portability Note: BitCopy assumes data are stored in byte arrays.  So do
// not used BitCopy to copy bits between two scalar values wider than 8-bit.
// For example, this works on a little-endian machine but fails on a big-endian
// machine:
//
// uint32 a, b:
// BitCopy(&a, 0, &b, 8, 4);
//
// To copy bits between scalars, use Bits::CopyBits().
void BitCopy(void* absl_nonnull vdest, int dest_offset,
             const void* absl_nonnull src, int src_offset, int nbits);

#endif  // THIRD_PARTY_GLOOP_UTIL_BITS_BIT_COPY_H_
