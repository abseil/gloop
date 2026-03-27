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

// gtl::Float128: An IEEE quadruple-precision float
//
// `gtl::Float128` is a quadruple-precision floating-point type. Its semantics
// are defined by IEEE 754 (ISO 60559), which means it has all the same pitfalls
// as `float` and `double`: it's not a real number, operations on it are neither
// associative nor distributive, negative zero exists, subtracting two numbers
// can trigger catastrophic cancellation, and so on. Furthermore, on most
// architectures, it's not hardware-accelerated, so it will be slower than
// `float` or `double`.
//
// In certain situations, however, `gtl::Float128` is a more appropriate numeric
// type than either `float` or `double`. It has both greater precision and
// greater range than those types; unlike `float` or `double`, it can represent
// any 64-bit integer without precision loss. So if you need to take an
// `int64_t`, slip into the floating-point domain, do some math, and then return
// to the `int64_t` domain, all without loss of precision, this is the type for
// you! For example,
//
//     int64_t ScaleToFullRange(int64_t value, int64_t min, int64_t max) {
//       return static_cast<int64_t>(gtl::Float128(value - min) / (max - min) *
//                                   std::numeric_limits<int64_t>::max());
//     }
//
// is safe and works as expected.
//
// Here's a brief comparison showing how `gtl::Float128` stacks up against the
// built-in `float` and `double` types.
//
//              type   precision              maximum finite value (approx.)
//     -------------   --------------------   ------------------------------
//             float    24 bits /  9 digits   3.4 × 10^38
//            double    53        17          1.8 × 10^308
//     gtl::Float128   113        34          1.2 × 10^4932
//
// API design and missing pieces
// -----------------------------
//
// `gtl::Float128`'s API is stable (and mostly compatible with the standard
// floating-point API). Currently, conversion to and from built-in types,
// `gtl::Float128` arithmetic, and a few free functions like `gtl::fabs` are
// implemented. `gtl::Float128` also has a complete `std::numeric_limits`
// specialization, so you can say things like
// `std::numeric_limits<gtl::Float128>::infinity()` to get an infinite value.
//
// We expect more free functions to be defined in spring 2024. Keep an eye on
// b/309832802.
//
// `gtl::Float128` does not support `absl::StrCat` or `absl::Substitute`
// directly. `gtl::Float128` does support `absl::StrFormat`, though, so you can
// call `absl::StrFormat("%v", my_float_128)` to get a string representation.
// `gtl::Float128` also supports STL stream insertion, so you can pass a
// `gtl::Float128` to a `LOG`.
//
// Specializing the STL type traits produces undefined behavior, so
// `std::is_floating_point_v<gtl::Float128>` is today and will forever remain
// `false`. We may implement `gtl`-namespaced versions of certain type traits or
// concepts; if you need them, please file a bug with <link>.
//
// We do not plan to implement exotic features of the standard floating-point
// API, such as implicit conversion to `bool` or Bessel functions.
//
// `gtl::Float128` and `long double`
// ---------------------------------
//
// `gtl::Float128` is intended to be a replacement for `long double` in google3.
// `long double` does not behave the same everywhere: it represents an
// extended-precision (80-bit) float on Intel CPUs, a slightly and subtly
// different extended-precision float on AMD CPUs, an IEEE double on 32-bit ARM
// CPUs, and an IEEE quad on 64-bit ARM CPUs. As Google adopts more diverse CPUs
// into prod, we cannot afford to rely on chip-specific features like `long
// double`. Use `gtl::Float128` instead!

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_FLOAT128_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_FLOAT128_H_

#include <features.h>

#include <bit>
#include <cmath>
#include <compare>
#include <iosfwd>
#include <limits>
#include <optional>
#include <string>

#include "absl/base/config.h"
#include "absl/base/nullability.h"
#include "absl/container/inlined_vector.h"
#include "absl/numeric/int128.h"
#include "absl/strings/str_format.h"
#include "gtest/gtest_prod.h"

#if defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 27)
#define GTL_FLOAT128_INTERNAL_HAVE_FULL_LIBC_SUPPORT 1
#else
// We're on GRTEv4, which is missing some quad-precision functions. Make parts
// of the API unavailable. TODO: b/205579037 - remove this.
#define GTL_FLOAT128_INTERNAL_HAVE_FULL_LIBC_SUPPORT 0
#endif  // __GLIBC_PREREQ(2, 27)
#else   // defined(__GLIBC_PREREQ)
#define GTL_FLOAT128_INTERNAL_HAVE_FULL_LIBC_SUPPORT 1
#endif  // defined(__GLIBC_PREREQ)

// NOLINTBEGIN(google-explicit-constructor)
// NOLINTBEGIN(google-runtime-int)
// NOLINTBEGIN(google3-readability-class-member-naming)
// NOLINTBEGIN(runtime/int)

namespace gtl {

// An IEEE quadruple-precision floating-point value. This type can store any
// finite values whose absolute value is less than about 1.1897e4932, as well as
// positive and negative infinities and NaN. For normal values, it provides 113
// bits of precision, or about 34 decimal digits.
//
// There are no public member functions on this type. Use `gtl`-namespaced free
// functions defined below. (They're all analogous to their `std`-namespaced
// counterparts--`gtl::fdim` works like `std::fdim`, `gtl::fmax` like
// `std::fmax`, etc.)
//
// A specialization of `std::numeric_limits` exists for this type, so you can
// say things like `std::numeric_limits<gtl::Float128>::infinity()` to get an
// infinite value.
//
// This type is thread-compatible.
class Float128 final {
 public:
  // Converts the given string (optionally followed or preceded by ASCII
  // whitespace) into a Float128, which may be rounded on overflow or underflow.
  // See https://en.cppreference.com/w/c/string/byte/strtof for details about
  // the allowed formats, except FromString() is locale-independent and will
  // always use the "C" locale.
#if GTL_FLOAT128_INTERNAL_HAVE_FULL_LIBC_SUPPORT
  static std::optional<Float128> FromString(const std::string&);
#endif

  // Constructs a Float128 representing zero.
  Float128() = default;

  // Constructors from other arithmetic types. These are guaranteed not to lose
  // precision.
  constexpr Float128(int);
  constexpr Float128(unsigned int);
  constexpr Float128(long);
  constexpr Float128(unsigned long);
  constexpr Float128(long long);
  constexpr Float128(unsigned long long);
  constexpr Float128(float);
  constexpr Float128(double);
  constexpr Float128(long double);

  // Constructors from 128-bit integers. These may lose precision.
  constexpr explicit Float128(absl::int128);
  constexpr explicit Float128(absl::uint128);
#ifdef ABSL_HAVE_INTRINSIC_INT128
  constexpr explicit Float128(__int128);
  constexpr explicit Float128(unsigned __int128);
#endif

  // Assignment operators from other arithmetic types. These are guaranteed not
  // to lose precision. Assignment from 128-bit integers is deliberately
  // unimplemented; instead, explicitly construct a Float128 and assign that.
  constexpr Float128& operator=(int);
  constexpr Float128& operator=(unsigned int);
  constexpr Float128& operator=(long);
  constexpr Float128& operator=(unsigned long);
  constexpr Float128& operator=(long long);
  constexpr Float128& operator=(unsigned long long);
  constexpr Float128& operator=(float);
  constexpr Float128& operator=(double);
  constexpr Float128& operator=(long double);

  // Float128 is copyable and movable.
  Float128(const Float128&) = default;
  Float128& operator=(const Float128&) = default;

  // Conversion operators to other arithmetic types.
  constexpr explicit operator char() const;
  constexpr explicit operator signed char() const;
  constexpr explicit operator unsigned char() const;
  constexpr explicit operator short() const;
  constexpr explicit operator unsigned short() const;
  constexpr explicit operator int() const;
  constexpr explicit operator unsigned int() const;
  constexpr explicit operator long() const;
  constexpr explicit operator unsigned long() const;
  constexpr explicit operator long long() const;
  constexpr explicit operator unsigned long long() const;
  constexpr explicit operator absl::int128() const;
  constexpr explicit operator absl::uint128() const;
#ifdef ABSL_HAVE_INTRINSIC_INT128
  constexpr explicit operator __int128() const;
  constexpr explicit operator unsigned __int128() const;
#endif
  constexpr explicit operator float() const;
  constexpr explicit operator double() const;
  constexpr explicit operator long double() const;

  // Mutating arithmetic operators. (Non-mutating operators are declared as free
  // functions below.)
  constexpr Float128& operator+=(Float128);
  constexpr Float128& operator-=(Float128);
  constexpr Float128& operator*=(Float128);
  constexpr Float128& operator/=(Float128);
  constexpr Float128& operator++();
  constexpr Float128 operator++(int);
  constexpr Float128& operator--();
  constexpr Float128 operator--(int);

 private:
// An architecture-portable spelling for the compiler's built-in IEEE quad type.
#ifdef __aarch64__
  static_assert(std::numeric_limits<long double>::is_iec559 &&
                std::numeric_limits<long double>::digits == 113 &&
                std::numeric_limits<long double>::radix == 2 &&
                std::numeric_limits<long double>::min_exponent == -16381 &&
                std::numeric_limits<long double>::max_exponent == 16384);
  using CompilerQuad = long double;
#else
  using CompilerQuad = __float128;
#endif

  // An InlinedVector<char> sized to hold every useful Float128. This means it
  // can hold 1 negative sign, 36 decimal digits of mantissa, 1 radix point, 1
  // letter 'e', 1 negative sign, 5 decimal digits of exponent, and one extra
  // byte. (The extra byte is to avoid reallocation when calling string
  // formatting operations that write a trailing null.)
  static constexpr int kStringRepBufferSize = 1 +   // negative sign
                                              1 +   // radix point
                                              36 +  // mantissa
                                              1 +   // 'e'
                                              1 +   // negative sign
                                              5 +   // exponent
                                              1;    // extra byte
  using StringRepBuffer = absl::InlinedVector<char, kStringRepBufferSize>;

  struct FromCompilerQuadTag {};
  friend class std::numeric_limits<Float128>;
  FRIEND_TEST(Float128NumericLimitsTest, LimitsAreUnchanged);

  constexpr explicit Float128(FromCompilerQuadTag, CompilerQuad);

#if GTL_FLOAT128_INTERNAL_HAVE_FULL_LIBC_SUPPORT
  StringRepBuffer ToString(int width, char fill, int precision,
                           char conversion_char) const;
#endif

  friend constexpr Float128 operator+(Float128);
  friend constexpr Float128 operator-(Float128);
  friend constexpr Float128 operator+(Float128, Float128);
  friend constexpr Float128 operator-(Float128, Float128);
  friend constexpr Float128 operator*(Float128, Float128);
  friend constexpr Float128 operator/(Float128, Float128);
  friend constexpr bool operator==(Float128, Float128);
  friend constexpr bool operator!=(Float128, Float128);
  friend constexpr bool operator>(Float128, Float128);
  friend constexpr bool operator<(Float128, Float128);
  friend constexpr bool operator>=(Float128, Float128);
  friend constexpr bool operator<=(Float128, Float128);
  friend constexpr std::partial_ordering operator<=>(Float128, Float128);

  // <link> start by_regex=\w+\(
  friend Float128 acosh(Float128);
  friend Float128 asinh(Float128);
  friend Float128 atanh(Float128);
  friend Float128 cbrt(Float128);
  friend Float128 ceil(Float128);
  friend Float128 copysign(Float128, Float128);
  friend Float128 cos(Float128);
  friend Float128 exp(Float128);
  friend Float128 exp2(Float128);
  friend Float128 expm1(Float128);
  friend Float128 fabs(Float128);
  friend Float128 fdim(Float128, Float128);
  friend Float128 floor(Float128);
  friend Float128 fmax(Float128, Float128);
  friend Float128 fmin(Float128, Float128);
  friend Float128 fmod(Float128, Float128);
  friend int fpclassify(Float128);
  friend Float128 frexp(Float128, int* absl_nonnull);
  friend int ilogb(Float128);
  friend bool isfinite(Float128);
  friend bool isinf(Float128);
  friend bool isnan(Float128);
  friend bool isnormal(Float128);
  friend Float128 ldexp(Float128, int);
  friend long long llrint(Float128);
  friend long long llround(Float128);
  friend Float128 log(Float128);
  friend Float128 log10(Float128);
  friend Float128 log1p(Float128);
  friend Float128 log2(Float128);
  friend Float128 logb(Float128);
  friend long lrint(Float128);
  friend long lround(Float128);
  friend Float128 nan(const char*);
  friend Float128 nextafter(Float128, Float128);
  friend Float128 pow(Float128, Float128);
  friend Float128 remainder(Float128, Float128);
  friend Float128 remquo(Float128, Float128, int*);
  friend Float128 rint(Float128);
  friend Float128 round(Float128);
  friend bool signbit(Float128);
  friend Float128 sin(Float128);
  friend Float128 sqrt(Float128);
  friend Float128 tan(Float128);
  friend Float128 trunc(Float128);
  // <link> end

#if GTL_FLOAT128_INTERNAL_HAVE_FULL_LIBC_SUPPORT
  friend Float128 modf(Float128, Float128* absl_nonnull);
  friend Float128 fma(Float128, Float128, Float128);
#endif

  friend std::ostream& operator<<(std::ostream&, Float128);
  friend absl::FormatConvertResult<absl::FormatConversionCharSet::kFloating |
                                   absl::FormatConversionCharSet::v>
  AbslFormatConvert(Float128, const absl::FormatConversionSpec&,
                    absl::FormatSink* absl_nonnull);
  friend auto ArbitraryFloat128();

  CompilerQuad data_;
};

// Arithmetic operations on Float128.
constexpr Float128 operator+(Float128);  // unary +
constexpr Float128 operator-(Float128);  // unary - (negation)
constexpr Float128 operator+(Float128, Float128);
constexpr Float128 operator-(Float128, Float128);
constexpr Float128 operator*(Float128, Float128);
constexpr Float128 operator/(Float128, Float128);

// Basic relations on Float128.
constexpr bool operator==(Float128, Float128);
constexpr bool operator!=(Float128, Float128);
constexpr bool operator>(Float128, Float128);
constexpr bool operator<(Float128, Float128);
constexpr bool operator>=(Float128, Float128);
constexpr bool operator<=(Float128, Float128);
constexpr std::partial_ordering operator<=>(Float128, Float128);

// Library functions. In addition to the overloads declared here, each of these
// functions has overloads for `float` and `double`, so something like
//
//     template <typename T>
//     int LogMinusOne(T x) {
//       return gtl::ilogb(x) - 1;
//     }
//
// will work even on built-in types.

// Basic library functions. These functions are all analogous to their
// `std::`-namespaced counterparts.
Float128 abs(Float128);  // alias for fabs
Float128 fabs(Float128);
Float128 fmod(Float128, Float128);
Float128 remainder(Float128, Float128);
Float128 remquo(Float128, Float128, int*);
#if GTL_FLOAT128_INTERNAL_HAVE_FULL_LIBC_SUPPORT
Float128 fma(Float128, Float128, Float128);
#endif
Float128 fmax(Float128, Float128);
Float128 fmin(Float128, Float128);
Float128 fdim(Float128, Float128);
Float128 nan(const char*);

// Exponential functions. These functions are all analogous to their
// `std::`-namespaced counterparts.
Float128 exp(Float128);
Float128 exp2(Float128);
Float128 expm1(Float128);
Float128 log(Float128);
Float128 log10(Float128);
Float128 log2(Float128);
Float128 log1p(Float128);

// Power functions. These functions are all analogous to their
// `std::`-namespaced counterparts.
Float128 sqrt(Float128);
Float128 cbrt(Float128);
Float128 pow(Float128, Float128);

// Trigonometric functions. These functions are all analogous to their
// `std::`-namespaced counterparts.
Float128 acosh(Float128);
Float128 asinh(Float128);
Float128 atanh(Float128);
Float128 cos(Float128);
Float128 sin(Float128);
Float128 tan(Float128);

// Rounding functions. These functions are all analogous to their
// `std::`-namespaced counterparts.
Float128 ceil(Float128);
Float128 floor(Float128);
Float128 trunc(Float128);
Float128 round(Float128);
long lround(Float128);
long long llround(Float128);
Float128 rint(Float128);
long lrint(Float128);
long long llrint(Float128);

// Floating-point manipulation functions. These functions are all analogous to
// their `std::`-namespaced counterparts.
Float128 frexp(Float128, int* absl_nonnull);
Float128 ldexp(Float128, int);
#if GTL_FLOAT128_INTERNAL_HAVE_FULL_LIBC_SUPPORT
Float128 modf(Float128, Float128* absl_nonnull);
#endif
int ilogb(Float128);
Float128 logb(Float128);
Float128 nextafter(Float128, Float128);
Float128 copysign(Float128, Float128);

// Classification of floats. These functions are all analogous to their
// `std::`-namespaced counterparts.
int fpclassify(Float128);
bool isfinite(Float128);
bool isinf(Float128);
bool isnan(Float128);
bool isnormal(Float128);
bool signbit(Float128);

}  // namespace gtl

// ==== Implementation details ====

namespace std {

// TODO: b/300297576 - Once libc++ has specializations for all possible
// CompilerQuad types on all the architectures we care about, rework this
// `numeric_limits` class to simply forward to that specialization.
template <>
class numeric_limits<gtl::Float128> {
 private:
  static constexpr gtl::Float128 FromCompilerQuad(
      gtl::Float128::CompilerQuad f) {
    return gtl::Float128(gtl::Float128::FromCompilerQuadTag{}, f);
  }

  static constexpr gtl::Float128 FromIeeeQuadBits(absl::uint128 bits) {
    return FromCompilerQuad(std::bit_cast<gtl::Float128::CompilerQuad>(bits));
  }

 public:
  static constexpr bool is_specialized = true;
  static constexpr bool is_signed = true;
  static constexpr bool is_integer = false;
  static constexpr bool is_exact = false;
  static constexpr bool has_infinity = true;
  static constexpr bool has_quiet_NaN = true;
  static constexpr bool has_signaling_NaN = true;
  static constexpr std::float_denorm_style has_denorm = std::denorm_present;
  static constexpr bool has_denorm_loss = false;
  static constexpr std::float_round_style round_style = std::round_to_nearest;
  static constexpr bool is_iec559 = true;
  static constexpr bool is_bounded = true;
  static constexpr bool is_modulo = false;
  static constexpr int digits = 113;
  static constexpr int digits10 = 33;
  static constexpr int max_digits10 = 36;
  static constexpr int radix = 2;
  static constexpr int min_exponent = -16'381;
  static constexpr int min_exponent10 = -4'931;
  static constexpr int max_exponent = 16'384;
  static constexpr int max_exponent10 = 4'932;
  static constexpr bool traps = false;
  static constexpr bool tinyness_before = false;

  static constexpr gtl::Float128 min() noexcept {
    return FromCompilerQuad(0x1.0p-16'382q);
  }

  static constexpr gtl::Float128 lowest() noexcept { return -max(); }

  static constexpr gtl::Float128 max() noexcept {
    return FromCompilerQuad(0x1.ffff'ffff'ffff'ffff'ffff'ffff'ffffp16'383q);
  }

  static constexpr gtl::Float128 epsilon() noexcept {
    return FromCompilerQuad(0x1.0p-112q);
  }

  static constexpr gtl::Float128 round_error() noexcept {
    return FromCompilerQuad(0.5q);
  }

  static constexpr gtl::Float128 infinity() noexcept {
    return FromIeeeQuadBits(absl::MakeUint128(0x7fff'0000'0000'0000, 0));
  }

  static constexpr gtl::Float128 quiet_NaN() noexcept {
    return FromIeeeQuadBits(absl::MakeUint128(0x7fff'8000'0000'0000, 0));
  }

  static constexpr gtl::Float128 signaling_NaN() noexcept {
    return FromIeeeQuadBits(absl::MakeUint128(0x7fff'4000'0000'0000, 0));
  }

  static constexpr gtl::Float128 denorm_min() noexcept {
    return FromCompilerQuad(0x1.0p-16'494q);
  }
};

}  // namespace std

namespace gtl {

// Require that constructors from arithmetic types do not lose precision.

static_assert(std::numeric_limits<unsigned long long>::radix <=
              std::numeric_limits<Float128>::radix);
static_assert(std::numeric_limits<unsigned long long>::digits <=
              std::numeric_limits<Float128>::digits);
static_assert(std::numeric_limits<long double>::digits <=
              std::numeric_limits<Float128>::digits);
static_assert(std::numeric_limits<long double>::radix ==
              std::numeric_limits<Float128>::radix);
static_assert(std::numeric_limits<long double>::min_exponent >=
              std::numeric_limits<Float128>::min_exponent);
static_assert(std::numeric_limits<long double>::max_exponent <=
              std::numeric_limits<Float128>::max_exponent);

constexpr Float128::Float128(int x) : data_(x) {}
constexpr Float128::Float128(unsigned int x) : data_(x) {}
constexpr Float128::Float128(long x) : data_(x) {}
constexpr Float128::Float128(unsigned long x) : data_(x) {}
constexpr Float128::Float128(long long x) : data_(x) {}
constexpr Float128::Float128(unsigned long long x) : data_(x) {}
constexpr Float128::Float128(float x) : data_(x) {}
constexpr Float128::Float128(double x) : data_(x) {}
constexpr Float128::Float128(long double x) : data_(x) {}

#ifdef ABSL_HAVE_INTRINSIC_INT128
constexpr Float128::Float128(absl::int128 x)
    : Float128(static_cast<__int128>(x)) {}
constexpr Float128::Float128(absl::uint128 x)
    : Float128(static_cast<unsigned __int128>(x)) {}
constexpr Float128::Float128(__int128 x) : data_(x) {}
constexpr Float128::Float128(unsigned __int128 x) : data_(x) {}
#else
constexpr Float128::Float128(absl::int128 x)
    : data_(static_cast<CompilerQuad>(absl::Int128Low64(x)) +
            static_cast<CompilerQuad>(absl::Int128High64(x)) * 0x1.0p64q) {}
constexpr Float128::Float128(absl::uint128 x)
    : data_(static_cast<CompilerQuad>(absl::Uint128Low64(x)) +
            static_cast<CompilerQuad>(absl::Uint128High64(x)) * 0x1.0p64q) {}
#endif  // ABSL_HAVE_INTRINSIC_INT128

constexpr Float128& Float128::operator=(int x) {
  data_ = x;
  return *this;
}

constexpr Float128& Float128::operator=(unsigned int x) {
  data_ = x;
  return *this;
}

constexpr Float128& Float128::operator=(long x) {
  data_ = x;
  return *this;
}

constexpr Float128& Float128::operator=(unsigned long x) {
  data_ = x;
  return *this;
}

constexpr Float128& Float128::operator=(long long x) {
  data_ = x;
  return *this;
}

constexpr Float128& Float128::operator=(unsigned long long x) {
  data_ = x;
  return *this;
}

constexpr Float128& Float128::operator=(float x) {
  data_ = x;
  return *this;
}

constexpr Float128& Float128::operator=(double x) {
  data_ = x;
  return *this;
}

constexpr Float128& Float128::operator=(long double x) {
  data_ = x;
  return *this;
}

constexpr Float128::operator char() const { return static_cast<char>(data_); }

constexpr Float128::operator signed char() const {
  return static_cast<signed char>(data_);
}

constexpr Float128::operator unsigned char() const {
  return static_cast<unsigned char>(data_);
}

constexpr Float128::operator short() const { return static_cast<short>(data_); }

constexpr Float128::operator unsigned short() const {
  return static_cast<unsigned short>(data_);
}

constexpr Float128::operator int() const { return static_cast<int>(data_); }

constexpr Float128::operator unsigned int() const {
  return static_cast<unsigned int>(data_);
}

constexpr Float128::operator long() const { return static_cast<long>(data_); }

constexpr Float128::operator unsigned long() const {
  return static_cast<unsigned long>(data_);
}

constexpr Float128::operator long long() const {
  return static_cast<long long>(data_);
}

constexpr Float128::operator unsigned long long() const {
  return static_cast<unsigned long long>(data_);
}

#ifdef ABSL_HAVE_INTRINSIC_INT128
constexpr Float128::operator absl::int128() const {
  return static_cast<__int128_t>(*this);
}

constexpr Float128::operator absl::uint128() const {
  return static_cast<__uint128_t>(*this);
}

constexpr Float128::operator __int128() const {
  return static_cast<__int128>(data_);
}

constexpr Float128::operator unsigned __int128() const {
  return static_cast<unsigned __int128>(data_);
}
#else
constexpr Float128::operator absl::int128() const {
  int64_t high = static_cast<int64_t>(data_ * 0x1.0p-64q);
  CompilerQuad low_part = data_ - static_cast<CompilerQuad>(high) * 0x1.0p64q;
  uint64_t low;
  if (low_part < 0.0q) {
    // Round down.
    high -= 1;
    low = static_cast<uint64_t>(low_part + 0x1.0p64q);
  } else {
    low = static_cast<uint64_t>(low_part);
  }

  return absl::MakeInt128(high, low);
}

constexpr Float128::operator absl::uint128() const {
  uint64_t high = static_cast<uint64_t>(data_ * 0x1.0p-64q);
  uint64_t low = static_cast<uint64_t>(data_ - static_cast<CompilerQuad>(high) *
                                                   0x1.0p64q);
  return absl::MakeUint128(high, low);
}
#endif  // ABSL_HAVE_INTRINSIC_INT128

constexpr Float128::operator float() const { return static_cast<float>(data_); }

constexpr Float128::operator double() const {
  return static_cast<double>(data_);
}

constexpr Float128::operator long double() const {
  return static_cast<long double>(data_);
}

constexpr Float128& Float128::operator+=(Float128 x) {
  data_ += x.data_;
  return *this;
}

constexpr Float128& Float128::operator-=(Float128 x) {
  data_ -= x.data_;
  return *this;
}

constexpr Float128& Float128::operator*=(Float128 x) {
  data_ *= x.data_;
  return *this;
}

constexpr Float128& Float128::operator/=(Float128 x) {
  data_ /= x.data_;
  return *this;
}

constexpr Float128& Float128::operator++() {
  ++data_;
  return *this;
}

constexpr Float128 Float128::operator++(int) {
  Float128 original = *this;
  data_++;
  return original;
}

constexpr Float128& Float128::operator--() {
  --data_;
  return *this;
}

constexpr Float128 Float128::operator--(int) {
  Float128 original = *this;
  data_--;
  return original;
}

constexpr Float128::Float128(Float128::FromCompilerQuadTag,
                             Float128::CompilerQuad x)
    : data_(x) {}

constexpr Float128 operator+(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{}, +x.data_);
}

constexpr Float128 operator-(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{}, -x.data_);
}

constexpr Float128 operator+(Float128 x, Float128 y) {
  return Float128(Float128::FromCompilerQuadTag{}, x.data_ + y.data_);
}

constexpr Float128 operator-(Float128 x, Float128 y) {
  return Float128(Float128::FromCompilerQuadTag{}, x.data_ - y.data_);
}

constexpr Float128 operator*(Float128 x, Float128 y) {
  return Float128(Float128::FromCompilerQuadTag{}, x.data_ * y.data_);
}

constexpr Float128 operator/(Float128 x, Float128 y) {
  return Float128(Float128::FromCompilerQuadTag{}, x.data_ / y.data_);
}

constexpr bool operator==(Float128 x, Float128 y) { return x.data_ == y.data_; }

constexpr bool operator!=(Float128 x, Float128 y) { return x.data_ != y.data_; }

constexpr bool operator>(Float128 x, Float128 y) { return x.data_ > y.data_; }

constexpr bool operator<(Float128 x, Float128 y) { return x.data_ < y.data_; }

constexpr bool operator>=(Float128 x, Float128 y) { return x.data_ >= y.data_; }

constexpr bool operator<=(Float128 x, Float128 y) { return x.data_ <= y.data_; }

constexpr std::partial_ordering operator<=>(Float128 x, Float128 y) {
  return x.data_ <=> y.data_;
}

// Library function overloads for built-in types.
inline float abs(float x) { return std::abs(x); }
inline double abs(double x) { return std::abs(x); }
inline float fabs(float x) { return std::fabs(x); }
inline double fabs(double x) { return std::fabs(x); }
inline float fmod(float x, float y) { return std::fmod(x, y); }
inline double fmod(double x, double y) { return std::fmod(x, y); }
inline float remainder(float x, float y) { return std::remainder(x, y); }
inline double remainder(double x, double y) { return std::remainder(x, y); }
inline float remquo(float x, float y, int* quotient) {
  return std::remquo(x, y, quotient);
}
inline double remquo(double x, double y, int* quotient) {
  return std::remquo(x, y, quotient);
}
inline float fma(float x, float y, float z) { return std::fma(x, y, z); }
inline double fma(double x, double y, double z) { return std::fma(x, y, z); }
inline float fmax(float x, float y) { return std::fmax(x, y); }
inline double fmax(double x, double y) { return std::fmax(x, y); }
inline float fmin(float x, float y) { return std::fmin(x, y); }
inline double fmin(double x, double y) { return std::fmin(x, y); }
inline float fdim(float x, float y) { return std::fdim(x, y); }
inline double fdim(double x, double y) { return std::fdim(x, y); }
inline float ceil(float x) { return std::ceil(x); }
inline double ceil(double x) { return std::ceil(x); }
inline float floor(float x) { return std::floor(x); }
inline double floor(double x) { return std::floor(x); }
inline float trunc(float x) { return std::trunc(x); }
inline double trunc(double x) { return std::trunc(x); }
inline float round(float x) { return std::round(x); }
inline double round(double x) { return std::round(x); }
inline long lround(float x) { return std::lround(x); }
inline long lround(double x) { return std::lround(x); }
inline long long llround(float x) { return std::llround(x); }
inline long long llround(double x) { return std::llround(x); }
inline float rint(float x) { return std::rint(x); }
inline double rint(double x) { return std::rint(x); }
inline long lrint(float x) { return std::lrint(x); }
inline long lrint(double x) { return std::lrint(x); }
inline long long llrint(float x) { return std::llrint(x); }
inline long long llrint(double x) { return std::llrint(x); }
inline float exp(float x) { return std::exp(x); }
inline double exp(double x) { return std::exp(x); }
inline float exp2(float x) { return std::exp2(x); }
inline double exp2(double x) { return std::exp2(x); }
inline float expm1(float x) { return std::expm1(x); }
inline double expm1(double x) { return std::expm1(x); }
inline float log(float x) { return std::log(x); }
inline double log(double x) { return std::log(x); }
inline float log10(float x) { return std::log10(x); }
inline double log10(double x) { return std::log10(x); }
inline float log2(float x) { return std::log2(x); }
inline double log2(double x) { return std::log2(x); }
inline float log1p(float x) { return std::log1p(x); }
inline double log1p(double x) { return std::log1p(x); }
inline float sqrt(float x) { return std::sqrt(x); }
inline double sqrt(double x) { return std::sqrt(x); }
inline float cbrt(float x) { return std::cbrt(x); }
inline double cbrt(double x) { return std::cbrt(x); }
inline float pow(float base, float exp) { return std::pow(base, exp); }
inline double pow(double base, double exp) { return std::pow(base, exp); }
inline float acosh(float x) { return std::acosh(x); }
inline double acosh(double x) { return std::acosh(x); }
inline float asinh(float x) { return std::asinh(x); }
inline double asinh(double x) { return std::asinh(x); }
inline float atanh(float x) { return std::atanh(x); }
inline double atanh(double x) { return std::atanh(x); }
inline float cos(float x) { return std::cos(x); }
inline double cos(double x) { return std::cos(x); }
inline float sin(float x) { return std::sin(x); }
inline double sin(double x) { return std::sin(x); }
inline float tan(float x) { return std::tan(x); }
inline double tan(double x) { return std::tan(x); }
inline float frexp(float x, int* absl_nonnull exponent) {
  return std::frexp(x, exponent);
}
inline double frexp(double x, int* absl_nonnull exponent) {
  return std::frexp(x, exponent);
}
inline float ldexp(float x, int exponent) { return std::ldexp(x, exponent); }
inline double ldexp(double x, int exponent) { return std::ldexp(x, exponent); }
inline float modf(float x, float* absl_nonnull integer) {
  return std::modf(x, integer);
}
inline double modf(double x, double* absl_nonnull integer) {
  return std::modf(x, integer);
}
inline int ilogb(float x) { return std::ilogb(x); }
inline int ilogb(double x) { return std::ilogb(x); }
inline float logb(float x) { return std::logb(x); }
inline double logb(double x) { return std::logb(x); }
inline float nextafter(float x, float y) { return std::nextafter(x, y); }
inline double nextafter(double x, double y) { return std::nextafter(x, y); }
inline float copysign(float x, float y) { return std::copysign(x, y); }
inline double copysign(double x, double y) { return std::copysign(x, y); }
inline int fpclassify(float x) { return std::fpclassify(x); }
inline int fpclassify(double x) { return std::fpclassify(x); }
inline bool isfinite(float x) { return std::isfinite(x); }
inline bool isfinite(double x) { return std::isfinite(x); }
inline bool isinf(float x) { return std::isinf(x); }
inline bool isinf(double x) { return std::isinf(x); }
inline bool isnan(float x) { return std::isnan(x); }
inline bool isnan(double x) { return std::isnan(x); }
inline bool isnormal(float x) { return std::isnormal(x); }
inline bool isnormal(double x) { return std::isnormal(x); }
inline bool signbit(float x) { return std::signbit(x); }
inline bool signbit(double x) { return std::signbit(x); }

}  // namespace gtl

// NOLINTEND(runtime/int)
// NOLINTEND(google3-readability-class-member-naming)
// NOLINTEND(google-runtime-int)
// NOLINTEND(google-explicit-constructor)

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_FLOAT128_H_
