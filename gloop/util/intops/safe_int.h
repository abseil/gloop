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
// A "safe int" is a StrongInt<T> which does additional validation of the
// various arithmetic and logical operations, and reacts to overflows and
// underflow and invalid operations.  You can define the "safe int" types
// to react to errors in pre-defined ways or you can define your own policy
// classes.
//
// Usage:
//   DEFINE_SAFE_INT_TYPE(Name, NativeType, PolicyType);
//
//     Defines a new StrongInt type named 'Name' in the current namespace with
//     underflow/overflow checking on all operations, with configurable error
//     policy.
//
//     Name: The desired name for the new StrongInt typedef.  Must be unique
//         within the current namespace.
//     NativeType: The primitive integral type this StrongInt will hold, as
//         defined by std::is_integral (see <type_traits>).
//     PolicyType: The type of policy used by this StrongInt type.  A few
//         pre-built policy types are provided here, but the caller can
//         define any custom policy they desire.
//
// PolicyTypes:
//     LogFatalOnError: LOG(FATAL) when a error occurs.
//     LogDFatalOnError: LOG(DFATAL) when a error occurs.

#ifndef THIRD_PARTY_GLOOP_UTIL_INTOPS_SAFE_INT_H_
#define THIRD_PARTY_GLOOP_UTIL_INTOPS_SAFE_INT_H_

#include <limits.h>

#include <cstdint>
#include <limits>
#include <type_traits>

#include "absl/log/log.h"
#include "gloop/util/intops/strong_int.h"  // IWYU pragma: export

namespace util_intops {

// A StrongInt validator class for "safe" type enforcement.  For signed types,
// this checks for overflows and underflows as well as undefined- or
// implementation-defined behaviors. For unsigned type, this further disallows
// operations that would take advantage of unsigned wrap-around behavior and
// operations which would discard data unexpectedly.  This assumes two's
// complement representations, and that division truncates towards zero.
//
// For some more on overflow safety, see:
//   https://www.securecoding.cert.org/confluence/display/seccode/INT32-C.+Ensure+that+operations+on+signed+integers+do+not+result+in+overflow?showComments=false
template <typename ErrorType>
class SafeIntStrongIntValidator {
 public:
  template <typename T, typename U>
  static constexpr void ValidateInit(U arg) {
    if constexpr (std::is_floating_point_v<U>) {
      // If the argument is floating point, we can do a simple check to
      // make sure the value is in range.  It is undefined behavior to
      // convert to int from a float that is out of range.
      if (arg < std::numeric_limits<T>::min() ||
          arg > std::numeric_limits<T>::max()) {
        ErrorType::Error("SafeInt: init from out of bounds float", arg, "=");
      }
    } else if constexpr (std::is_convertible_v<U, T> &&
                         std::is_convertible_v<T, U>) {
      // If the initial value (type U) is changed by being converted to
      // and from the native type (type T), then it must be out of
      // bounds for type T.
      //
      // If T is unsigned and the argument is negative, then it is
      // clearly out of bounds for type T.
      //
      // If the initial value is greater than the max value for type T,
      // then it is clearly out of bounds for type T.  Before we check
      // that, though, we must ensure that the initial value is
      // positive, or else we could get unwanted promotion to unsigned,
      // making the test wrong.  If the initial value is negative, it
      // can't be larger than the max value for type T.
      if (static_cast<U>(static_cast<T>(arg)) != arg ||
          (!std::numeric_limits<T>::is_signed && arg < 0) ||
          (arg > 0 && arg > std::numeric_limits<T>::max())) {
        ErrorType::Error("SafeInt: init from out of bounds value", arg, "=");
      }
    } else {
      // If we can convert one way, but not the other, something strange is
      // happening.  We try to detect an omnivalent cast, i.e.
      // `template <class Any> operator Any();`, in which case we allow the
      // construction since this pattern should only be used for template
      // metaprogramming hence no value will actually be created.
      struct CannotExist {
        CannotExist() = delete;
      };
      static_assert(std::is_convertible_v<U, CannotExist>,
                    "Cannot validate SafeInt initialization from a type that "
                    "is not two-way convertible.");
    }
  }
  template <typename T>
  static constexpr void ValidateNegate(  // Signed types only.
      typename std::enable_if<std::numeric_limits<T>::is_signed, T>::type
          value) {
    // It T uses 2s complement, the negative max is non-negateable.
    if (std::numeric_limits<T>::min() + std::numeric_limits<T>::max() == -1 &&
        value == std::numeric_limits<T>::min()) {
      ErrorType::Error("SafeInt: overflow", value, -1, "*");
    }
  }
  template <typename T>
  static constexpr void ValidateBitNot(  // Unsigned types only.
      typename std::enable_if<!std::numeric_limits<T>::is_signed, T>::type
          value) {
    // Do nothing.
  }
  template <typename T>
  static constexpr void ValidateAdd(T lhs, T rhs) {
    // The same logic applies to signed and unsigned types.
    if ((rhs > 0) && (lhs > (std::numeric_limits<T>::max() - rhs))) {
      ErrorType::Error("SafeInt: overflow", lhs, rhs, "+");
    } else if ((rhs < 0) && (lhs < (std::numeric_limits<T>::min() - rhs))) {
      ErrorType::Error("SafeInt: underflow", lhs, rhs, "+");
    }
  }
  template <typename T>
  static constexpr void ValidateSubtract(T lhs, T rhs) {
    // The same logic applies to signed and unsigned types.
    if ((rhs > 0) && (lhs < (std::numeric_limits<T>::min() + rhs))) {
      ErrorType::Error("SafeInt: underflow", lhs, rhs, "-");
    } else if ((rhs < 0) && (lhs > (std::numeric_limits<T>::max() + rhs))) {
      ErrorType::Error("SafeInt: overflow", lhs, rhs, "-");
    }
  }
  template <typename T, typename U>
  static constexpr void ValidateMultiply(T lhs, U rhs) {
    if (!std::numeric_limits<T>::is_signed) {
      // Unsigned types only.
      if (rhs < 0) {
        ErrorType::Error("SafeInt: negation of unsigned type", lhs, rhs, "*");
      }
    }
    // Multiplication by 0 can never overflow/underflow, but handling 0 makes
    // the below code more complex.
    if (lhs == 0 || rhs == 0) {
      return;
    }
    // The remaining logic applies to signed and unsigned types.  Note that
    // while multiplication is commutative, the underlying StrongInt class
    // always calls this with T as StrongInt<T>::ValueType.
    if (lhs > 0) {
      if (rhs > 0) {
        if (lhs > (std::numeric_limits<T>::max() / rhs)) {
          ErrorType::Error("SafeInt: overflow", lhs, rhs, "*");
        }
      } else {
        if (rhs < (std::numeric_limits<T>::min() / lhs)) {
          ErrorType::Error("SafeInt: underflow", lhs, rhs, "*");
        }
      }
    } else {
      if (rhs > 0) {
        // Underflow could be tested by lhs < min / rhs, but that does not
        // work if rhs is an unsigned type. Instead we test rhs > min / lhs.
        // There is a special case for lhs = -1, which would overflow min / lhs.
        if ((lhs == -1 && rhs - 1 > std::numeric_limits<T>::max()) ||
            (lhs < -1 && rhs > std::numeric_limits<T>::min() / lhs)) {
          ErrorType::Error("SafeInt: underflow", lhs, rhs, "*");
        }
      } else {
        if ((lhs != 0) && (rhs < (std::numeric_limits<T>::max() / lhs))) {
          ErrorType::Error("SafeInt: overflow", lhs, rhs, "*");
        }
      }
    }
  }
  template <typename T, typename U>
  static constexpr void ValidateDivide(T lhs, U rhs) {
    // This applies to signed and unsigned types.
    if (rhs == 0) {
      ErrorType::Error("SafeInt: divide by zero", lhs, rhs, "/");
    }
    if (std::numeric_limits<T>::is_signed) {
      // Signed types only.
      if ((lhs == std::numeric_limits<T>::min()) && (rhs == -1)) {
        ErrorType::Error("SafeInt: overflow", lhs, rhs, "/");
      }
    } else {
      // Unsigned types only.
      if (rhs < 0) {
        ErrorType::Error("SafeInt: negation of unsigned type", lhs, rhs, "/");
      }
    }
  }
  template <typename T, typename U>
  static constexpr void ValidateModulo(T lhs, U rhs) {
    // This applies to signed and unsigned types.
    if (rhs == 0) {
      ErrorType::Error("SafeInt: divide by zero", lhs, rhs, "%");
    }
    if (std::numeric_limits<T>::is_signed) {
      // Signed types only.
      if ((lhs == std::numeric_limits<T>::min()) && (rhs == -1)) {
        ErrorType::Error("SafeInt: overflow", lhs, rhs, "%");
      }
    } else {
      // Unsigned types only.
      if (rhs < 0) {
        ErrorType::Error("SafeInt: negation of unsigned type", lhs, rhs, "%");
      }
    }
  }
  template <typename T>
  static constexpr void ValidateLeftShift(T lhs, int64_t rhs) {
    if (std::numeric_limits<T>::is_signed) {
      // Signed types only.
      if (lhs < 0) {
        ErrorType::Error("SafeInt: shift of negative value", lhs, rhs, "<<");
      }
    }
    // The remaining logic applies to signed and unsigned types.
    if (rhs < 0) {
      ErrorType::Error("SafeInt: shift by negative arg", lhs, rhs, "<<");
    }
    if (rhs >= (sizeof(T) * CHAR_BIT)) {
      ErrorType::Error("SafeInt: shift by large arg", lhs, rhs, "<<");
    }
    if (lhs > (std::numeric_limits<T>::max() >> rhs)) {
      ErrorType::Error("SafeInt: overflow", lhs, rhs, "<<");
    }
  }
  template <typename T>
  static constexpr void ValidateRightShift(T lhs, int64_t rhs) {
    if (std::numeric_limits<T>::is_signed) {
      // Signed types only.
      if (lhs < 0) {
        ErrorType::Error("SafeInt: shift of negative value", lhs, rhs, ">>");
      }
    }
    // The remaining logic applies to signed and unsigned types.
    if (rhs < 0) {
      ErrorType::Error("SafeInt: shift by negative arg", lhs, rhs, ">>");
    }
    if (rhs >= (sizeof(T) * CHAR_BIT)) {
      ErrorType::Error("SafeInt: shift by large arg", lhs, rhs, ">>");
    }
  }
  template <typename T>
  static constexpr void ValidateBitAnd(  // Unsigned types only.
      typename std::enable_if<!std::numeric_limits<T>::is_signed, T>::type lhs,
      typename std::enable_if<!std::numeric_limits<T>::is_signed, T>::type
          rhs) {
    // Do nothing.
  }
  template <typename T>
  static constexpr void ValidateBitOr(  // Unsigned types only.
      typename std::enable_if<!std::numeric_limits<T>::is_signed, T>::type lhs,
      typename std::enable_if<!std::numeric_limits<T>::is_signed, T>::type
          rhs) {
    // Do nothing.
  }
  template <typename T>
  static constexpr void ValidateBitXor(  // Unsigned types only.
      typename std::enable_if<!std::numeric_limits<T>::is_signed, T>::type lhs,
      typename std::enable_if<!std::numeric_limits<T>::is_signed, T>::type
          rhs) {
    // Do nothing.
  }
};

// A SafeIntStrongIntValidator policy class to LOG(FATAL) on errors.
struct LogFatalOnError {
  template <typename Tlhs, typename Trhs>
  static void Error(const char* error, Tlhs lhs, Trhs rhs, const char* op) {
    LOG(FATAL) << error << ": (" << lhs << " " << op << " " << rhs << ")";
  }
  template <typename Tval>
  static void Error(const char* error, Tval val, const char* op) {
    LOG(FATAL) << error << ": (" << op << val << ")";
  }
};

// A SafeIntStrongIntValidator policy class to LOG(DFATAL) on errors.
struct LogDfatalOnError {
  template <typename Tlhs, typename Trhs>
  static void Error(const char* error, Tlhs lhs, Trhs rhs, const char* op) {
    LOG(DFATAL) << error << ": (" << lhs << " " << op << " " << rhs << ")";
  }
  template <typename Tval>
  static void Error(const char* error, Tval val, const char* op) {
    LOG(DFATAL) << error << ": (" << op << val << ")";
  }
};

}  // namespace util_intops

// Defines the StrongInt using value_type and typedefs it to type_name, with
// strong checking of under/overflow conditions.
// The struct int_type_name ## _tag_ trickery is needed to ensure that a new
// type is created per type_name.
#define DEFINE_SAFE_INT_TYPE(type_name, value_type, policy_type)         \
  struct type_name##_safe_tag_ {                                         \
    static constexpr absl::string_view TypeName() { return #type_name; } \
  };                                                                     \
  typedef ::util_intops::StrongInt<                                      \
      type_name##_safe_tag_, value_type,                                 \
      ::util_intops::SafeIntStrongIntValidator<policy_type> >            \
      type_name;

#endif  // THIRD_PARTY_GLOOP_UTIL_INTOPS_SAFE_INT_H_
