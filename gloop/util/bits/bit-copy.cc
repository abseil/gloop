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

#include "gloop/util/bits/bit-copy.h"

#include "absl/base/nullability.h"

// This bit-copy uses the unsigned char as its unit type,
// which works for any external data type
void BitCopy(void* absl_nonnull vdest, int dest_offset,
             const void* absl_nonnull vsrc, int src_offset, int nbits) {
  if (nbits == 0) return;

  typedef unsigned char BitCopyWord;
  static const int kWordBits = sizeof(BitCopyWord) * 8;

  BitCopyWord* dest = static_cast<BitCopyWord*>(vdest);
  const BitCopyWord* src = static_cast<const BitCopyWord*>(vsrc);
  dest += dest_offset / kWordBits;
  src += src_offset / kWordBits;
  dest_offset %= kWordBits;
  src_offset %= kWordBits;

  // words to be changed
  int ndest = (dest_offset + nbits + (kWordBits - 1)) / kWordBits;

  // words to be accessed
  int nsrc = (src_offset + nbits + (kWordBits - 1)) / kWordBits;
  const BitCopyWord* src_end = src + nsrc;

  // masks for the unchanged bits in initial and final words changed in dest
  BitCopyWord start_mask = (1 << dest_offset) - 1;
  BitCopyWord end_mask = -((1 << ((dest_offset + nbits - 1) % kWordBits)) << 1);

  if (ndest == 1) {
    // first dest word is also last
    start_mask |= end_mask;
  }

  int x = src_offset - dest_offset;
  int i = 0;
  int j = (x != 0);
  int diff = (x + kWordBits) % kWordBits;
  int idiff = (kWordBits - diff) % kWordBits;
  src -= (x < 0);

  dest[i] &= start_mask;
  dest[i] |= ((x >= 0 ? (src[i] >> diff) : 0) |
              (src + j < src_end ? (src[j] << idiff) : 0)) &
             ~start_mask;

  for (++i, ++j; i < ndest - 1; ++i, ++j) {
    dest[i] = (src[i] >> diff) | (src[j] << idiff);
  }

  if (i < ndest) {
    dest[i] &= end_mask;
    dest[i] |=
        ((src[i] >> diff) | (src + j < src_end ? (src[j] << idiff) : 0)) &
        ~end_mask;
  }
}
