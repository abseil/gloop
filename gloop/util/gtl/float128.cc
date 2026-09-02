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

#include "gloop/util/gtl/float128.h"

#include <math.h>
#include <stdlib.h>

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <ostream>
#include <string>

#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

// NOLINTBEGIN(google-runtime-int)
// NOLINTBEGIN(runtime/int)

namespace gtl {

Float128::StringRepBuffer Float128::ToString(const int width, const char fill,
                                             const int precision,
                                             const char conversion_char) const {
  assert(width >= 0);

  std::string format_str = "%";
  if (precision >= 0) {
    absl::StrAppend(&format_str, ".", precision);
  }
  format_str.push_back(conversion_char);

  // Allocate memory to store the converted string. We don't know how many bytes
  // to allocate, so make an educated guess.
  const int initial_allocation =
      std::max(std::max(kStringRepBufferSize, width), precision + 2);
  StringRepBuffer buf(initial_allocation);

  // Do the conversion, including writing a trailing null.
  int desired_nonnull_bytes =
      strfromf128(buf.data(), buf.size(), format_str.c_str(), data_);
  if (ABSL_PREDICT_TRUE(desired_nonnull_bytes < initial_allocation)) {
    // Hooray, we managed to do the conversion without running out of space.
    if (width > desired_nonnull_bytes) {
      // Left-pad. Since `buf` is guaranteed to hold at least `width` bytes, we
      // don't need to reallocate.
      assert(buf.size() >= static_cast<std::size_t>(width));
      const int padding = width - desired_nonnull_bytes;
      std::memmove(buf.data() + padding, buf.data(), desired_nonnull_bytes);
      std::fill_n(buf.begin(), padding, fill);
      desired_nonnull_bytes = width;
    }
  } else {
    // We didn't allocate enough to write all the nonnull bytes and the trailing
    // null, which means truncation occurred. However, we now know exactly how
    // many bytes we need, so reallocate to the exact right amount.
    const int padding = std::max(width - desired_nonnull_bytes, 0);
    const int data_bytes_needed = desired_nonnull_bytes + 1 /* trailing null */;
    buf.resize(padding + data_bytes_needed);

    // Fill in the padding.
    std::fill_n(buf.begin(), padding, fill);

    // Write the data after the padding.
    desired_nonnull_bytes = strfromf128(buf.data() + padding, data_bytes_needed,
                                        format_str.c_str(), data_);
    assert(desired_nonnull_bytes == data_bytes_needed - 1);
  }
  buf.resize(desired_nonnull_bytes);

#ifndef NDEBUG
  for (const char c : buf) {
    assert(c != '\0');
  }
#endif

  return buf;
}

std::optional<Float128> Float128::FromString(const std::string& str) {
  char* end = nullptr;
  Float128::CompilerQuad compiler_quad = strtof128(str.c_str(), &end);
  if (end == str.c_str()) {
    return std::nullopt;
  }
  return Float128(Float128::FromCompilerQuadTag{}, compiler_quad);
}

Float128 abs(Float128 x) { return fabs(x); }

Float128 fabs(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{}, __builtin_fabsf128(x.data_));
}

Float128 fmod(Float128 x, Float128 y) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_fmodf128(x.data_, y.data_));
}

Float128 remainder(Float128 x, Float128 y) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_remainderf128(x.data_, y.data_));
}

Float128 remquo(Float128 x, Float128 y, int* quotient) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_remquof128(x.data_, y.data_, quotient));
}

Float128 fma(Float128 x, Float128 y, Float128 z) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_fmaf128(x.data_, y.data_, z.data_));
}

Float128 fmax(Float128 x, Float128 y) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_fmaxf128(x.data_, y.data_));
}

Float128 fmin(Float128 x, Float128 y) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_fminf128(x.data_, y.data_));
}

Float128 fdim(Float128 x, Float128 y) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_fdimf128(x.data_, y.data_));
}

Float128 nan(const char* payload) {
  return Float128(Float128::FromCompilerQuadTag{}, __builtin_nanf128(payload));
}

Float128 exp(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{}, __builtin_expf128(x.data_));
}

Float128 exp2(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{}, __builtin_exp2f128(x.data_));
}

Float128 expm1(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_expm1f128(x.data_));
}

Float128 log(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{}, __builtin_logf128(x.data_));
}

Float128 log10(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_log10f128(x.data_));
}

Float128 log2(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{}, __builtin_log2f128(x.data_));
}

Float128 log1p(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_log1pf128(x.data_));
}

Float128 sqrt(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{}, __builtin_sqrtf128(x.data_));
}

Float128 cbrt(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{}, __builtin_cbrtf128(x.data_));
}

Float128 pow(Float128 base, Float128 exp) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_powf128(base.data_, exp.data_));
}

Float128 acosh(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_acoshf128(x.data_));
}

Float128 asinh(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_asinhf128(x.data_));
}

Float128 atanh(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_atanhf128(x.data_));
}

Float128 cos(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{}, __builtin_cosf128(x.data_));
}

Float128 sin(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{}, __builtin_sinf128(x.data_));
}

Float128 tan(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{}, __builtin_tanf128(x.data_));
}

Float128 ceil(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{}, __builtin_ceilf128(x.data_));
}

Float128 floor(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_floorf128(x.data_));
}

Float128 trunc(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_truncf128(x.data_));
}

Float128 round(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_roundf128(x.data_));
}

long lround(Float128 x) { return __builtin_lroundf128(x.data_); }

long long llround(Float128 x) { return __builtin_llroundf128(x.data_); }

Float128 rint(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{}, __builtin_rintf128(x.data_));
}

long lrint(Float128 x) { return __builtin_lrintf128(x.data_); }

long long llrint(Float128 x) { return __builtin_llrintf128(x.data_); }

Float128 frexp(Float128 x, int* absl_nonnull exponent) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_frexpf128(x.data_, exponent));
}

Float128 ldexp(Float128 x, int exponent) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_ldexpf128(x.data_, exponent));
}

Float128 modf(Float128 x, Float128* absl_nonnull integral_part) {
  // As of 2024-05-30, Clang erroneously believes that `__builtin_modff128`
  // can't take a `long double*` as its second argument on AArch64. Call
  // `modff128` rather than `__builtin_modff128` to work around it.
  return Float128(Float128::FromCompilerQuadTag{},
                  modff128(x.data_, &integral_part->data_));
}

int ilogb(Float128 x) { return __builtin_ilogbf128(x.data_); }

Float128 logb(Float128 x) {
  return Float128(Float128::FromCompilerQuadTag{}, __builtin_logbf128(x.data_));
}

Float128 nextafter(Float128 from, Float128 to) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_nextafterf128(from.data_, to.data_));
}

Float128 copysign(Float128 x, Float128 y) {
  return Float128(Float128::FromCompilerQuadTag{},
                  __builtin_copysignf128(x.data_, y.data_));
}

int fpclassify(Float128 x) {
  return __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL,
                              FP_ZERO, x.data_);
}

bool isfinite(Float128 x) { return __builtin_isfinite(x.data_); }

bool isinf(Float128 x) { return __builtin_isinf(x.data_); }

bool isnan(Float128 x) { return __builtin_isnan(x.data_); }

bool isnormal(Float128 x) { return __builtin_isnormal(x.data_); }

bool signbit(Float128 x) { return __builtin_signbit(x.data_); }

std::ostream& operator<<(std::ostream& out, Float128 x) {
  if (out.flags() & (std::ostream::left | std::ostream::internal |
                     std::ostream::showpoint | std::ostream::showpos)) {
    out.setstate(std::ostream::failbit);
    return out;
  }

  char fmt;
  int precision = out.precision();
  switch (static_cast<int>(out.flags() & std::ostream::floatfield)) {
    case 0:
      fmt = 'g';
      break;
    case std::ostream::scientific:
      fmt = 'e';
      break;
    case std::ostream::fixed:
      fmt = 'f';
      break;
    case std::ostream::scientific | std::ostream::fixed:
      fmt = 'a';
      precision = -1;  // std::hexfloat causes precision to be ignored.
      break;
  }
  if (out.flags() & std::ostream::uppercase) {
    fmt = absl::ascii_toupper(fmt);
  }

  Float128::StringRepBuffer buf =
      x.ToString(out.width(), out.fill(), precision, fmt);
  return out.write(buf.data(), buf.size());
}

absl::FormatConvertResult<absl::FormatConversionCharSet::kFloating |
                          absl::FormatConversionCharSet::v>
AbslFormatConvert(Float128 x, const absl::FormatConversionSpec& spec,
                  absl::FormatSink* absl_nonnull sink) {
  if (spec.has_left_flag() || spec.has_show_pos_flag() ||
      spec.has_sign_col_flag() || spec.has_alt_flag()) {
    return {false};
  }

  char fmt;
  switch (spec.conversion_char()) {
    case absl::FormatConversionChar::f:
      fmt = 'f';
      break;
    case absl::FormatConversionChar::F:
      fmt = 'F';
      break;
    case absl::FormatConversionChar::e:
      fmt = 'e';
      break;
    case absl::FormatConversionChar::E:
      fmt = 'E';
      break;
    case absl::FormatConversionChar::g:
    case absl::FormatConversionChar::v:
      fmt = 'g';
      break;
    case absl::FormatConversionChar::G:
      fmt = 'G';
      break;
    case absl::FormatConversionChar::a:
      fmt = 'a';
      break;
    case absl::FormatConversionChar::A:
      fmt = 'A';
      break;
    default:
      std::abort();
  }

  Float128::StringRepBuffer buf =
      x.ToString(std::max(spec.width(), 0), spec.has_zero_flag() ? '0' : ' ',
                 spec.precision(), fmt);
  sink->Append(absl::string_view(buf.data(), buf.size()));
  return {true};
}

}  // namespace gtl

// NOLINTEND(runtime/int)
// NOLINTEND(google-runtime-int)
