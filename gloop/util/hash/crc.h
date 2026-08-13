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

#ifndef THIRD_PARTY_GLOOP_UTIL_HASH_CRC_H_
#define THIRD_PARTY_GLOOP_UTIL_HASH_CRC_H_

#include <stddef.h>

#include <cstdint>

// This class implements CRCs (aka Rabin Fingerprints).
// Treats the input as a polynomial with coefficients in Z(2),
// and finds the remainder when divided by a primitive polynomial
// of the appropriate length.
// It handles all CRC sizes from 8 to 128 bits.
// The input string is prefixed with a "1" bit, and has "degree" "0" bits
// appended to it before the remainder is found.   This ensures that
// short strings are scrambled somewhat.

// A polynomial is represented by the bit pattern formed by its coefficients,
// but with the highest order bit not stored.
// The highest degree coefficient is stored in the lowest numbered bit
// in the lowest addressed byte.   Thus, in what follows, the highest degree
// coefficient that is stored is in the low order bit of "lo" or "*lo".

// Hardware acceleration is used when available.

// Typical usage:
//
// // prepare to do 64-bit CRCs using the default polynomial.  No rolling hash.
// CRC *crc = CRC::Default(64, 0);
// ...
// uint64_t lo;   // declare a lo,hi pair to hold the CRC
// uint64_t hi;
// crc->Empty(&lo, &hi);      // Initialize to CRC of empty string
// crc->Extend(&lo, &hi, "hello", 5);     // Get CRC of "hello"
// ...
//
// // prepare to use a 32-bit rolling hash over 6 bytes
// CRC *crc = CRC::Default(32, 6);
// ...
// uint64_t lo;   // declare a lo,hi pair to hold the CRC
// uint64_t hi;
// crc->Empty(&lo, &hi);      // Initialize to CRC of empty string
// crc->Extend(&lo, &hi, data, 6);     // Get CRC of first 6 bytes
// for (int i = 6; i != sizeof (data); i++) {
//   crc->Roll(&lo, &hi, data[i-6], data[i]); // Move window by one byte
//   // lo,hi is CRC of bytes data[i-5...i]
// }
// ...
//
// We recommend that if a CRC is to be stored or transmitted, it first be
// passed through Scramble().  When retrieved, the scrambled value can be
// passed through Unscramble() to recover the true CRC value.  This is because
// another software layer may CRC the data with the same CRC polynomial.
// Scramble() provides a non-linear reversible function suitable to decouple
// the two computations; without it there is a danger that efficacy of the
// outer CRC will be reduced by linear interactions with the inner CRC.
//
// See <link> for more information, including how you might use
// this library to compute a CRC in parallel.

class CRC {
 public:
  // Initialize all the tables for CRC's of a given bit length "degree"
  // using a default polynomial of the given length.
  //
  // The argument "roll_length" is used by subsequent calls to
  // Roll().
  // Returns a handle that MUST NOT be destroyed with delete.
  // The default polynomials are those in POLYS[8...128].
  // Handles returned by Default() MUST NOT be deleted.
  // Identical calls to Default() yield identical handles.
  static CRC* Default(int degree, size_t roll_length);

  // Initialize all the tables for CRC's of a given bit length "degree"
  // using an arbitrary CRC polynomial.
  // Normally, you would use Default() instead of New()---see above.
  //
  // Requires that "lo,hi" contain a primitive polynomial of degree "degree"
  // Requires 8 <= degree && degree <= 128
  // Any primitive polynomial of the correct degree will work.
  // See the POLYS array for suitable primitive polynomials.
  //
  // The argument "roll_length" is used by subsequent calls to
  // Roll().
  // Each call to New() yields a pointer to a new object
  // that may be deallocated with delete.
  static CRC* New(uint64_t lo, uint64_t hi, int degree, size_t roll_length);

  virtual ~CRC();

  // Place the CRC of the empty string in "*lo,*hi"
  virtual void Empty(uint64_t* lo, uint64_t* hi) const = 0;

  // If "*lo,*hi" is the CRC of bytestring A, place the CRC of
  // the bytestring formed from the concatenation of A and the "length"
  // bytes at "bytes" into "*lo,*hi".
  virtual void Extend(/*INOUT*/ uint64_t* lo, /*INOUT*/ uint64_t* hi,
                      const void* bytes, int64_t length) const = 0;

  // Equivalent to Extend(lo, hi, bytes, length) where "bytes"
  // points to an array of "length" zero bytes.
  virtual void ExtendByZeroes(/*INOUT*/ uint64_t* lo, /*INOUT*/ uint64_t* hi,
                              int64_t length) const = 0;

  // If *pxlo,*pxhi is the CRC (as defined by *crc) of some string X,
  // and ylo,yhi is the CRC of some string Y that is ylen bytes long, set
  // *pxlo,*pxhi to the CRC of the concatenation of X followed by Y.
  virtual void Concat(/*INOUT*/ uint64_t* pxlo, /*INOUT*/ uint64_t* pxhi,
                      uint64_t ylo, uint64_t yhi, int64_t ylen);

  // Apply a non-linear transformation to "*lo,*hi" so that
  // it is safe to CRC the result with the same polynomial without
  // any reduction of error-detection ability in the outer CRC.
  // Unscramble() performs the inverse transformation.
  // It is strongly recommended that CRCs be scrambled before storage or
  // transmission, and unscrambled at the other end before futher manipulation.
  virtual void Scramble(/*INOUT*/ uint64_t* lo,
                        /*INOUT*/ uint64_t* hi) const = 0;
  virtual void Unscramble(/*INOUT*/ uint64_t* lo,
                          /*INOUT*/ uint64_t* hi) const = 0;

  // If "*lo,*hi" is the CRC of a byte string of length "roll_length"
  // (which is an argument to New() and Default()) that consists of
  // byte "o_byte" followed by string S, set "*lo,*hi" to the CRC of
  // the string that consists of S followed by the byte "i_byte".
  virtual void Roll(/*INOUT*/ uint64_t* lo, /*INOUT*/ uint64_t* hi,
                    uint8_t o_byte, uint8_t i_byte) const = 0;

  // POLYS[] is an array of valid triples that may be given to New()
  static const struct Poly {
    uint64_t lo;  // first half suitable CRC polynomial
    uint64_t hi;  // second half of suitable CRC polynomial
    int degree;   // degree of suitable CRC polynomial
  }* const POLYS;
  // It is guaranteed that no two entries in POLYS[] are identical,
  // that POLYS[i] contains a polynomial of degree i for 8 <= i <= 128,
  // that POLYS[0] and POLYS[1] contains polynomials of degree 32,
  // that POLYS[2] and POLYS[3] contains polynomials of degree 64,
  // that POLYS[4] and POLYS[5] contains polynomials of degree 96, and
  // that POLYS[6] and POLYS[7] contains polynomials of degree 128.

  static const int N_POLYS;  // Number of elements in POLYS array.

  // Standard() returns an implementation of a CRC with a commonly-used
  // polynomial.  Returns a handle that MUST NOT be destroyed with delete.
  // Identical calls to Standard() yield identical handles.
  // These standard CRCs are guaranteed distinct from those in POLYS[].
  // For valid names, see table below, which was derived from the Wikipedia CRC
  // page.  The standard uses of these polynomials may use different start
  // states from this implementation, and so may need adjustment.
  enum CRCName {  // Valid names for Standard(), with polynomial.
    // The Ethernet CRC polynomial (the one most commonly used) is "CRC32".
    // The CRC implemented by Intel's SSE4.2 crc32 instruction is "CRC32C".
    CRC_8_ATM,        // x8+x2+x+1
    CRC_8_CCITT,      // x8+x7+x3+x2+1
    CRC_8_DALLAS,     // x8+x5+x4+1
    CRC_8,            // x8+x7+x6+x4+x2+1
    CRC_8_SAE,        // x8+x4+x3+x2+1
    CRC_10,           // x10+x9+x5+x4+x+1
    CRC_11,           // x11+x9+x8+x7+x2+1
    CRC_12,           // x12+x11+x3+x2+x+1
    CRC_15_CAN,       // x15+x14+x10+x8+x7+x4+x3+1
    CRC_16_CCITT,     // x16+x12+x5+1
    CRC_16_DNP,       // x16+x13+x12+x11+x10+x8+x6+x5+x2+1
    CRC_16,           // x16+x15+x2+1
    CRC_24_RADIX_64,  // x24+x23+x18+x17+x14+x11+x10+x7+x6+x5+x4+x3+x+1
    CRC_30,           // x30+x29+x21+x20+x15+x13+x12+x11+x8+x7+x6+x2+x+1
    CRC_32,           // x32+x26+x23+x22+x16+x12+x11+x10+x8+x7+x5+x4+x2+x+1
    CRC_32C,          // x32+x28+x27+x26+x25+x23+x22+x20+x19+x18+x14+x13+x11+
                      // x10+x9+x8+x6+1
    CRC_32K,          // x32+x30+x29+x28+x26+x20+x19+x17+x16+x15+x11+x10+x7+
                      // x6+x4+x2+x+1
    CRC_64_ISO,       // x64+x4+x3+x+1
    CRC_64_ECMA,      // x64+x62+x57+x55+x54+x53+x52+x47+x46+x45+x40+x39+x38+
                      // x37+x35+x33+x32+x31+x29+x27+x24+x23+x22+x21+x19+x17+
                      // x13+x12+x10+x9+x7+x4+x+1
  };
  static CRC* Standard(CRCName name, size_t roll_length);

 protected:
  CRC();  // Clients may not call constructor;
          // use Default(), Standard(), or New() instead.

 private:
  CRC(const CRC&) = delete;
  CRC& operator=(const CRC&) = delete;
};

#endif  // THIRD_PARTY_GLOOP_UTIL_HASH_CRC_H_
