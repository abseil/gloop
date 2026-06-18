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

//
// Implementation details:
//
// Fingerprint2011() is a form of Murmur2 on strings up to 32 bytes
// and a form of CityHash for longer strings.  It could have been one
// or the other throughout.  The main advantage of the combination is
// that CityHash has a bunch of special cases for short strings that
// don't need to be replicated here.  The logic of murmur is
// frozen, so we simply call it, but the logic of CityHash64 isn't frozen,
// so we duplicate most of the relevant portions here.  We may duplicate
// more in the future, as necessary, to keep Fingerprint2011() from changing.

#include "gloop/util/hash/fingerprint2011.h"

#include <algorithm>  // for swap
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>  // for pair, make_pair

#include "absl/base/optimization.h"
#include "absl/base/prefetch.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "gloop/strings/cord_bytestream.h"
#include "gloop/util/endian/endian.h"  // for LittleEndian
#include "gloop/util/hash/murmur.h"    // for MurmurHash64WithSeed

// Some primes between 2^63 and 2^64 for various uses.
static const uint64_t k0 = 0xa5b85c5e198ed849ULL;
static const uint64_t k1 = 0x8d58ac26afe12e47ULL;
static const uint64_t k2 = 0xc47b6e9e3a970ed3ULL;

// Bitwise right rotate.  Requires shift to be a manifest constant in [1, 63].
static inline uint64_t Rotate(uint64_t val, int shift) {
  assert(shift >= 1);
  assert(shift <= 63);
  return (val >> shift) | (val << (64 - shift));
}

static inline uint64_t ShiftMix(uint64_t val) { return val ^ (val >> 47); }

static inline uint64_t HashLen16(uint64_t u, uint64_t v) {
  const uint64_t kMul = 0xc6a4a7935bd1e995ULL;
  uint64_t a = (v ^ u) * kMul;
  a ^= (a >> 47);
  uint64_t b = (u ^ a) * kMul;
  b ^= (b >> 47);
  b *= kMul;
  return b;
}

// Return a 16-byte hash for 48 input bytes.  Quick and dirty.
// Callers do best to use "random-looking" values for a and b.
static inline std::pair<uint64_t, uint64_t> WeakHashLen32WithSeeds(
    uint64_t w, uint64_t x, uint64_t y, uint64_t z, uint64_t a, uint64_t b) {
  a += w;
  b = Rotate(b + a + z, 51);
  uint64_t c = a;
  a += x;
  a += y;
  b += Rotate(a, 23);
  return std::make_pair(a + z, b + c);
}

// Return a 16-byte hash for s[0] ... s[31], a, and b.  Quick and dirty.
static inline std::pair<uint64_t, uint64_t> WeakHashLen32WithSeeds(
    const char* s, uint64_t a, uint64_t b) {
  return WeakHashLen32WithSeeds(
      LittleEndian::Load64(s), LittleEndian::Load64(s + 8),
      LittleEndian::Load64(s + 16), LittleEndian::Load64(s + 24), a, b);
}

// Return an 8-byte hash for a string.  Requires 33 <= len <= 64.
static inline uint64_t HashLen33to64(const char* s, size_t len) {
  assert(len >= 33);
  assert(len <= 64);
  uint64_t z = LittleEndian::Load64(s + 24);
  uint64_t a =
      LittleEndian::Load64(s) + (len + LittleEndian::Load64(s + len - 16)) * k0;
  uint64_t b = Rotate(a + z, 52);
  uint64_t c = Rotate(a, 37);
  a += LittleEndian::Load64(s + 8);
  c += Rotate(a, 7);
  a += LittleEndian::Load64(s + 16);
  uint64_t vf = a + z;
  uint64_t vs = b + Rotate(a, 31) + c;
  a = LittleEndian::Load64(s + 16) + LittleEndian::Load64(s + len - 32);
  z = LittleEndian::Load64(s + len - 8);
  b = Rotate(a + z, 52);
  c = Rotate(a, 37);
  a += LittleEndian::Load64(s + len - 24);
  c += Rotate(a, 7);
  a += LittleEndian::Load64(s + len - 16);
  uint64_t wf = a + z;
  uint64_t ws = b + Rotate(a, 31) + c;
  uint64_t r = ShiftMix((vf + ws) * k2 + (wf + vs) * k0);
  return ShiftMix(r * k0 + vs) * k2;
}

// Add exactly 64 bytes from s to the fingerprint.
static inline void Fingerprint2011Helper64Bytes(
    const char* s, uint64_t* x, uint64_t* y, uint64_t* z,
    std::pair<uint64_t, uint64_t>* v, std::pair<uint64_t, uint64_t>* w) {
  *x = Rotate(*x + *y + v->first + LittleEndian::Load64(s + 16), 37) * k1;
  *y = Rotate(*y + v->second + LittleEndian::Load64(s + 48), 42) * k1;
  *x ^= w->second;
  *y ^= v->first;
  *z = Rotate(*z ^ w->first, 33);
  *v = WeakHashLen32WithSeeds(s, v->second * k1, *x + w->first);
  *w = WeakHashLen32WithSeeds(s + 32, *z + w->second, *y);
  std::swap(*z, *x);
}

// For strings over 64 bytes we hash the end first, and then as we
// loop we keep 56 bytes of state: v, w, x, y, and z.
// x is initialized from the start of the string.
// s_len is the length of s, providing 64 bytes from the end of the string.
// overall_len is the size of the full string being fingerprinted.
static inline void Fingerprint2011HelperTail(const char* s, size_t s_len,
                                             size_t overall_len,
                                             std::pair<uint64_t, uint64_t>* v,
                                             std::pair<uint64_t, uint64_t>* w,
                                             uint64_t* x, uint64_t* y,
                                             uint64_t* z) {
  *y = LittleEndian::Load64(s + s_len - 16) ^ k1;
  *z = LittleEndian::Load64(s + s_len - 56) ^ k0;
  *v = WeakHashLen32WithSeeds(s + s_len - 64, overall_len, *y);
  *w = WeakHashLen32WithSeeds(s + s_len - 32, overall_len * k1, k0);
  *z += ShiftMix(v->second) * k1;
  *x = Rotate(*z + *x, 39) * k1;
  *y = Rotate(*y, 33) * k1;
}

// Return an 8-byte hash for a string.  Requires len >= 33.
static inline uint64_t Fingerprint2011Helper(const char* s, size_t len) {
  assert(len >= 33);
  if (len <= 64) {
    return HashLen33to64(s, len);
  }

  // For strings over 64 bytes we hash the end first, and then as we
  // loop we keep 56 bytes of state: v, w, x, y, and z.
  uint64_t x = LittleEndian::Load64(s);
  uint64_t y;
  uint64_t z;
  std::pair<uint64_t, uint64_t> v;
  std::pair<uint64_t, uint64_t> w;
  Fingerprint2011HelperTail(s, len, len, &v, &w, &x, &y, &z);

  // Decrease len to the nearest multiple of 64, and operate on 64-byte chunks.
  len = (len - 1) & ~static_cast<size_t>(63);
  do {
    if (len >= 3 * ABSL_CACHELINE_SIZE) {
      absl::PrefetchToLocalCache(s + 3 * ABSL_CACHELINE_SIZE);
    }
    Fingerprint2011Helper64Bytes(s, &x, &y, &z, &v, &w);
    s += 64;
    len -= 64;
  } while (len != 0);
  return HashLen16(HashLen16(v.first, w.first) + ShiftMix(y) * k1 + z,
                   HashLen16(v.second, w.second) + x);
}

static inline uint64_t Fingerprint2011Helper(const absl::Cord& s) {
  char buffer[64];
  strings::CordReader reader(s);
  uint64_t x;
  reader.ReadN(sizeof(x), buffer);
  x = LittleEndian::Load64(buffer);

  reader.Reset(s);
  reader.Skip(s.size() - 64);
  reader.ReadN(64, buffer);

  uint64_t y;
  uint64_t z;
  std::pair<uint64_t, uint64_t> v;
  std::pair<uint64_t, uint64_t> w;
  Fingerprint2011HelperTail(buffer, 64, s.size(), &v, &w, &x, &y, &z);

  // Decrease len to the nearest multiple of 64, and operate on 64-byte chunks.
  size_t len = (s.size() - 1) & ~static_cast<size_t>(63);
  reader.Reset(s);
  do {
    if (reader.Peek().size() >= 64) {
      Fingerprint2011Helper64Bytes(reader.Peek().data(), &x, &y, &z, &v, &w);
      reader.Skip(64);
    } else {
      reader.ReadN(64, buffer);
      Fingerprint2011Helper64Bytes(buffer, &x, &y, &z, &v, &w);
    }
    len -= 64;
  } while (len != 0);
  return HashLen16(HashLen16(v.first, w.first) + ShiftMix(y) * k1 + z,
                   HashLen16(v.second, w.second) + x);
}

uint64_t Fingerprint2011(absl::string_view sv) {
  const char* s = sv.data();
  size_t len = sv.size();

  uint64_t u = len >= 8 ? LittleEndian::Load64(s) : k0;
  uint64_t v = len >= 9 ? LittleEndian::Load64(s + len - 8) : k0;
  uint64_t result = len <= 32 ? util_hash::MurmurHash64WithSeed(
                                    absl::string_view(s, len), k0 ^ k1 ^ k2)
                              : Fingerprint2011Helper(s, len);
  result = HashLen16(result + v, u);
  return ABSL_PREDICT_TRUE(result >= 2) ? result
                                        : result + ~static_cast<uint64_t>(1);
}

uint64_t Fingerprint2011(const absl::Cord& s) {
  if (s.size() <= 64) {
    return Fingerprint2011(absl::Cord(s).Flatten());
  }
  uint64_t u, v;
  char buffer[sizeof(u)];
  strings::CordReader reader(s);
  reader.ReadN(sizeof(u), buffer);
  u = LittleEndian::Load64(buffer);
  reader.Skip(s.size() - 16);
  reader.ReadN(sizeof(u), buffer);
  v = LittleEndian::Load64(buffer);

  uint64_t result = Fingerprint2011Helper(s);
  result = HashLen16(result + v, u);
  return ABSL_PREDICT_TRUE(result >= 2) ? result
                                        : result + ~static_cast<uint64_t>(1);
}

uint64_t FingerprintCat2011(uint64_t fp1, uint64_t fp2) {
  const uint64_t kMul1 = 0xc6a4a7935bd1e995ULL;
  const uint64_t kMul2 = 0x228876a7198b743ULL;
  uint64_t a = fp1 * kMul1 + fp2 * kMul2;
  // Note: The following line also makes sure we never return 0 or 1, because we
  // will only add something to 'a' if there are any MSBs (the remaining bits
  // after the shift) being 0, in which case wrapping around would not happen.
  return a + (~a >> 47);
}
