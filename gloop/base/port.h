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

#ifndef THIRD_PARTY_GLOOP_BASE_PORT_H_
#define THIRD_PARTY_GLOOP_BASE_PORT_H_

// This file contains things that are not used in Abseil but still needed
// for Gloop users. It is structured into the following high-level categories:
// - Platform specific requirement
//   - MSVC
// - Endianness
// - Hash
// - Global variables
// - Type alias
// - Predefined system/language macros
// - Predefined system/language functions
// - Performance optimization (alignment)
// - Obsolete

#include <inttypes.h>  // IWYU pragma: keep
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// -----------------------------------------------------------------------------
// MSVC Specific Requirements
// -----------------------------------------------------------------------------

#ifdef COMPILER_MSVC /* if Visual C++ */

#include <intrin.h>
#include <process.h>  // _getpid()

// clang-format off
#include <winsock2.h>  // Must come before <windows.h>
#include <windows.h>
// clang-format on

#undef ERROR
#undef DELETE
#undef DIFFERENCE
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001

// This compiler flag can be easily overlooked on MSVC.
// _CHAR_UNSIGNED gets set with the /J flag.
#ifndef _CHAR_UNSIGNED
#error chars must be unsigned!  Use the /J flag on the compiler command line.  // NOLINT
#endif

// Allow comparisons between signed and unsigned values.
//
// Lots of Google code uses this pattern:
//   for (int i = 0; i < container.size(); ++i)
// Since size() returns an unsigned value, this warning would trigger
// frequently.  Very few of these instances are actually bugs since containers
// rarely exceed MAX_INT items.  Unfortunately, there are bugs related to
// signed-unsigned comparisons that have been missed because we disable this
// warning.  For example:
//   const long stop_time = os::GetMilliseconds() + kWaitTimeoutMillis;
//   while (os::GetMilliseconds() <= stop_time) { ... }
#pragma warning(disable : 4018)  // level 3
#pragma warning(disable : 4267)  // level 3

// Don't warn about unused local variables.
//
// Google3 code uses ABSL_ATTRIBUTE_UNUSED, which is a macro for a gcc and clang
// extension to silence particular instances of this warning.  There's no way
// to define ABSL_ATTRIBUTE_UNUSED to quiet particular instances of this warning
// in VC++, so we disable it globally.  Currently, there aren't many false
// positives, so perhaps we can address those in the future and re-enable these
// warnings, which sometimes catch real bugs.
#pragma warning(disable : 4101)  // level 3

// Allow initialization and assignment to a smaller type without warnings about
// possible loss of data.
//
// There is a distinct warning, 4267, that warns about size_t conversions to
// smaller types, but we don't currently disable that warning.
//
// Correct code can be written in such a way as to avoid false positives
// by making the conversion explicit, but Google code isn't usually that
// verbose.  There are too many false positives to address at this time.  Note
// that this warning triggers at levels 2, 3, and 4 depending on the specific
// type of conversion.  By disabling it, we not only silence minor narrowing
// conversions but also serious ones.
#pragma warning(disable : 4244)  // level 2, 3, and 4

// Allow silent truncation of double to float.
//
// Silencing this warning has caused us to miss some subtle bugs.
#pragma warning(disable : 4305)  // level 1

// Allow a constant to be assigned to a type that is too small.
//
// I don't know why we allow this at all.  I can't think of a case where this
// wouldn't be a bug, but enabling the warning breaks many builds today.
#pragma warning(disable : 4307)  // level 2

// Allow passing the this pointer to an initializer even though it refers
// to an uninitialized object.
//
// Some observer implementations rely on saving the this pointer.  Those are
// safe because the pointer is not dereferenced until after the object is fully
// constructed.  This could however, obscure other instances.  In the future, we
// should look into disabling this warning locally rather globally.
#pragma warning(disable : 4355)  // level 1 and 4

// Allow implicit coercion from an integral type to a bool.
//
// These could be avoided by making the code more explicit, but that's never
// been the style here, so there would be many false positives.  It's not
// obvious if a true positive would ever help to find an actual bug.
#pragma warning(disable : 4800)  // level 3

#endif  // COMPILER_MSVC

// -----------------------------------------------------------------------------
// Utility Macros
// -----------------------------------------------------------------------------

// OS_IOS
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#ifndef OS_IOS
#define OS_IOS 1
#endif
#endif  // defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#endif  // defined(__APPLE__)

// LANG_CXX11
// GXX_EXPERIMENTAL_CXX0X is defined by gcc and clang up to at least
// gcc-4.7 and clang-3.1 (2011-12-13).  __cplusplus was defined to 1
// in gcc before 4.7 (Crosstool 16) and clang before 3.1, but is
// defined according to the language version in effect thereafter.
// Microsoft Visual Studio 14 (2015) sets __cplusplus==199711 despite
// reasonably good C++11 support, so we set LANG_CXX for it and
// newer versions (_MSC_VER >= 1900).
#if (defined(__GXX_EXPERIMENTAL_CXX0X__) || __cplusplus >= 201103L || \
     (defined(_MSC_VER) && _MSC_VER >= 1900))
// DEPRECATED: Do not key off LANG_CXX11. Instead, write more accurate condition
// that checks whether the C++ feature you need is available or missing, and
// define a more specific feature macro (GOOGLE_HAVE_FEATURE_FOO). You can check
// http://en.cppreference.com/w/cpp/compiler_support for compiler support on C++
// features.
// Define this to 1 if the code is compiled in C++11 mode; leave it
// undefined otherwise.  Do NOT define it to 0 -- that causes
// '#ifdef LANG_CXX11' to behave differently from '#if LANG_CXX11'.
#define LANG_CXX11 1
#endif

// This sanity check can be removed when all references to
// LANG_CXX11 is removed from the code base.
#if defined(__cplusplus) && !defined(LANG_CXX11) && !defined(SWIG)
#error "LANG_CXX11 is required."
#endif

// GOOGLE_OBSCURE_SIGNAL
#if defined(__APPLE__)
// No SIGPWR on MacOSX.  SIGINFO seems suitably obscure.
#define GOOGLE_OBSCURE_SIGNAL SIGINFO
#else
/* We use SIGPWR since that seems unlikely to be used for other reasons. */
#define GOOGLE_OBSCURE_SIGNAL SIGPWR
#endif

// ATTRIBUTE_NO_SANITIZE_FLOAT_DIV_BY_ZERO
//
// Tells the UndefinedBehaviorSanitizer to ignore floating point division by
// zero within a given function. Useful for cases when floating point division
// by zero is being used intentionally.
// Note: Supported in GCC as of gcc 8.1, and in Clang as of clang 3.7.
#if defined(__GNUC__) && defined(UNDEFINED_BEHAVIOR_SANITIZER)
#define ATTRIBUTE_NO_SANITIZE_FLOAT_DIV_BY_ZERO \
  __attribute__((no_sanitize("float-divide-by-zero")))
#else
#define ATTRIBUTE_NO_SANITIZE_FLOAT_DIV_BY_ZERO
#endif

// -----------------------------------------------------------------------------
// Endianness
// -----------------------------------------------------------------------------

// IS_LITTLE_ENDIAN, IS_BIG_ENDIAN
#if defined __linux__ || defined OS_ANDROID || defined(__ANDROID__)
// TODO: http://b/21460321; use one of OS_ANDROID or __ANDROID__.
// _BIG_ENDIAN
#include <endian.h>

#elif defined(__APPLE__)

// BIG_ENDIAN
#include <machine/endian.h>  // NOLINT(build/include)

/* Let's try and follow the Linux convention */
#define __BYTE_ORDER BYTE_ORDER
#define __LITTLE_ENDIAN LITTLE_ENDIAN
#define __BIG_ENDIAN BIG_ENDIAN

#endif

// defines __BYTE_ORDER for MSVC
#ifdef COMPILER_MSVC
#define __BYTE_ORDER __LITTLE_ENDIAN
#define IS_LITTLE_ENDIAN
#else

// define the macros IS_LITTLE_ENDIAN or IS_BIG_ENDIAN
// using the above endian definitions from endian.h if
// endian.h was included
#ifdef __BYTE_ORDER
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define IS_LITTLE_ENDIAN
#endif

#if __BYTE_ORDER == __BIG_ENDIAN
#define IS_BIG_ENDIAN
#endif

#else  // __BYTE_ORDER

#if defined(__LITTLE_ENDIAN__)
#define IS_LITTLE_ENDIAN
#elif defined(__BIG_ENDIAN__)
#define IS_BIG_ENDIAN
#endif

#endif  // __BYTE_ORDER
#endif  // COMPILER_MSVC

// byte swap functions (bswap_16, bswap_32, bswap_64).

// The following guarantees declaration of the byte swap functions
#ifdef COMPILER_MSVC
#include <stdlib.h>  // NOLINT(build/include)

#define bswap_16(x) _byteswap_ushort(x)
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)

#elif defined(__APPLE__)
// Mac OS X / Darwin features
#include <libkern/OSByteOrder.h>

#define bswap_16(x) OSSwapInt16(x)
#define bswap_32(x) OSSwapInt32(x)
#define bswap_64(x) OSSwapInt64(x)

#elif defined(__GLIBC__)
#include <byteswap.h>  // IWYU pragma: export

#else

static inline uint16_t bswap_16(uint16_t x) {
#ifdef __cplusplus
  return static_cast<uint16_t>(((x & 0xFF) << 8) | ((x & 0xFF00) >> 8));
#else
  return (uint16_t)(((x & 0xFF) << 8) | ((x & 0xFF00) >> 8));  // NOLINT
#endif  // __cplusplus
}
#define bswap_16(x) bswap_16(x)
static inline uint32_t bswap_32(uint32_t x) {
  return (((x & 0xFF) << 24) | ((x & 0xFF00) << 8) | ((x & 0xFF0000) >> 8) |
          ((x & 0xFF000000) >> 24));
}
#define bswap_32(x) bswap_32(x)
static inline uint64_t bswap_64(uint64_t x) {
  return (((x & (uint64_t)0xFF) << 56) | ((x & (uint64_t)0xFF00) << 40) |
          ((x & (uint64_t)0xFF0000) << 24) | ((x & (uint64_t)0xFF000000) << 8) |
          ((x & (uint64_t)0xFF00000000) >> 8) |
          ((x & (uint64_t)0xFF0000000000) >> 24) |
          ((x & (uint64_t)0xFF000000000000) >> 40) |
          ((x & (uint64_t)0xFF00000000000000) >> 56));
}
#define bswap_64(x) bswap_64(x)

#endif

// -----------------------------------------------------------------------------
// Global Variables
// -----------------------------------------------------------------------------

// PATH_SEPARATOR
// Define the OS's path separator
//
// NOTE: Assuming the path separator at compile time is discouraged.
// Prefer instead to be tolerant of both possible separators whenever possible.
#ifdef __cplusplus  // C won't merge duplicate const variables at link time
// Some headers provide a macro for this (GCC's system.h), remove it so that we
// can use our own.
#undef PATH_SEPARATOR
#if defined(_WIN32)
const char PATH_SEPARATOR = '\\';
#else
const char PATH_SEPARATOR = '/';
#endif  // _WIN32
#endif  // __cplusplus

// -----------------------------------------------------------------------------
// Type Alias
// -----------------------------------------------------------------------------

// uint, ushort, ulong
#if defined __linux__
// The uint mess:
// mysql.h sets _GNU_SOURCE which sets __USE_MISC in <features.h>
// sys/types.h typedefs uint if __USE_MISC
// mysql typedefs uint if HAVE_UINT not set
// The following typedef is carefully considered, and should not cause
//  any clashes
#if !defined(__USE_MISC)
#if !defined(HAVE_UINT)
#define HAVE_UINT 1
typedef unsigned int uint;
#endif  // !HAVE_UINT
#if !defined(HAVE_USHORT)
#define HAVE_USHORT 1
typedef unsigned short ushort;  // NOLINT
#endif                          // !HAVE_USHORT
#if !defined(HAVE_ULONG)
#define HAVE_ULONG 1
typedef unsigned long ulong;  // NOLINT
#endif                        // !HAVE_ULONG
#endif                        // !__USE_MISC

#endif  // __linux__

#ifdef COMPILER_MSVC /* if Visual C++ */
// VC++ doesn't understand "uint"
#ifndef HAVE_UINT
#define HAVE_UINT 1
typedef unsigned int uint;
#endif  // !HAVE_UINT
#endif  // COMPILER_MSVC

#ifdef _MSC_VER
// uid_t
// MSVC doesn't have uid_t
typedef int uid_t;

// pid_t
// Defined all over the place.
typedef int pid_t;
#endif  // _MSC_VER

// mode_t
#ifdef COMPILER_MSVC
// From stat.h
typedef unsigned int mode_t;
#endif  // COMPILER_MSVC

// sig_t
#ifdef COMPILER_MSVC
typedef void (*sig_t)(int);
#endif  // COMPILER_MSVC

// Use these macros after a % in a printf format string
// to get correct 32/64 bit behavior, like this:
// size_t size = records.size();
// printf("%" PRIuS "\n", size);
#define PRIdS "zd"
#define PRIxS "zx"
#define PRIuS "zu"
#define PRIXS "zX"
#define PRIoS "zo"

#define GPRIuPTHREAD "lu"
#define GPRIxPTHREAD "lx"
#if defined(__APPLE__)
#define PRINTABLE_PTHREAD(pthreadt) reinterpret_cast<uintptr_t>(pthreadt)
#else
#define PRINTABLE_PTHREAD(pthreadt) pthreadt
#endif

#ifdef PTHREADS_REDHAT_WIN32
#include <pthread.h>  // NOLINT(build/include)

#include <iosfwd>  // NOLINT(build/include)

// pthread_t is not a simple integer or pointer on Win32
std::ostream& operator<<(std::ostream& out, const pthread_t& thread_id);
#endif

// -----------------------------------------------------------------------------
// Predefined System/Language Macros
// -----------------------------------------------------------------------------

// EXFULL
#if defined(__APPLE__)
// Linux has this in <linux/errno.h>
#define EXFULL ENOMEM  // not really that great a translation...
#endif                 // __APPLE__
#ifdef COMPILER_MSVC
// This actually belongs in errno.h but there's a name conflict in errno
// on WinNT. They (and a ton more) are also found in Winsock2.h, but
// if'd out under NT. We need this subset at minimum.
#define EXFULL ENOMEM  // not really that great a translation...
#endif                 // COMPILER_MSVC

// __ptr_t
#if defined(__APPLE__)
// Linux has this in <sys/cdefs.h>
#define __ptr_t void*
#endif  // __APPLE__
#ifdef COMPILER_MSVC
// From glob.h
#define __ptr_t void*
#endif

// HUGE_VALF
#ifdef COMPILER_MSVC
#include <math.h>  // for HUGE_VAL

#ifndef HUGE_VALF
#define HUGE_VALF (static_cast<float>(HUGE_VAL))
#endif
#endif  // COMPILER_MSVC

// MAP_ANONYMOUS
#if defined(__APPLE__)
// For mmap, Linux defines both MAP_ANONYMOUS and MAP_ANON and says MAP_ANON is
// deprecated. In Darwin, MAP_ANON is all there is.
#if !defined MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif  // !MAP_ANONYMOUS
#endif  // __APPLE__

// PATH_MAX
// You say tomato, I say atotom
#ifdef _MSC_VER
#define PATH_MAX MAX_PATH
#endif

// -----------------------------------------------------------------------------
// Predefined System/Language Functions
// -----------------------------------------------------------------------------

// strtoq, strtouq, atoll
#ifdef COMPILER_MSVC
#define strtoq _strtoi64
#define strtouq _strtoui64
#define atoll _atoi64
#endif  // COMPILER_MSVC

#ifdef COMPILER_MSVC
// You say tomato, I say _tomato
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define strdup _strdup
#define tempnam _tempnam
#define chdir _chdir
#define getpid _getpid
#define getcwd _getcwd
#define putenv _putenv
#define tzname _tzname
#endif  // COMPILER_MSVC

// random, srandom
#ifdef COMPILER_MSVC
// You say tomato, I say toma
inline int random() { return rand(); }
inline void srandom(unsigned int seed) { srand(seed); }
#endif  // COMPILER_MSVC

// bcopy, bzero
#if defined(COMPILER_MSVC) && !defined(__clang__)
// You say juxtapose, I say transpose
#define bcopy(s, d, n) memcpy(d, s, n)
// Really from <string.h>
inline void bzero(void* s, int n) { memset(s, 0, n); }
#endif  // defined(COMPILER_MSVC) && !defined(__clang__)

// gethostbyname
#if defined(OS_WINDOWS) || defined(__APPLE__)
// gethostbyname() *is* thread-safe for Windows native threads. It is also
// safe on Mac OS X and iOS, where it uses thread-local storage, even though the
// manpages claim otherwise. For details, see
// http://lists.apple.com/archives/Darwin-dev/2006/May/msg00008.html
#else
// gethostbyname() is not thread-safe.  So disallow its use.  People
// should either use the HostLookup::Lookup*() methods, or gethostbyname_r()
#define gethostbyname gethostbyname_is_not_thread_safe_DO_NOT_USE
#endif

// -----------------------------------------------------------------------------
// Performance Optimization
// -----------------------------------------------------------------------------

// Alignment

// Unaligned APIs

// Portable handling of unaligned loads, stores, and copies. These are simply
// constant-length memcpy calls.
//
// TODO: These APIs are forked in Abseil, see
// "absl/base/internal/unaligned_access.h".
//
// The unaligned API is C++ only.  The declarations use C++ features
// (namespaces, inline) which are absent or incompatible in C.
#if defined(__cplusplus)

namespace base {

// Can't use ATTRIBUTE_NO_SANITIZE_MEMORY because this file is included before
// attributes.h is.
#ifdef __has_attribute
#if __has_attribute(no_sanitize_memory)
#define NO_SANITIZE_MEMORY __attribute__((no_sanitize_memory))
#endif  // __has_attribute(no_sanitize_memory)
#endif  // defined __has_attribute

#ifndef NO_SANITIZE_MEMORY
#define NO_SANITIZE_MEMORY /**/
#endif

template <typename T>
T NO_SANITIZE_MEMORY UnalignedLoad(const void* p) {
  T t;
  memcpy(&t, p, sizeof t);
  return t;
}

#undef NO_SANITIZE_MEMORY

template <typename T>
void UnalignedStore(void* p, T t) {
  memcpy(p, &t, sizeof t);
}
}  // namespace base

inline uint16_t UNALIGNED_LOAD16(const void* p) {
  return base::UnalignedLoad<uint16_t>(p);
}

inline uint32_t UNALIGNED_LOAD32(const void* p) {
  return base::UnalignedLoad<uint32_t>(p);
}

inline uint64_t UNALIGNED_LOAD64(const void* p) {
  return base::UnalignedLoad<uint64_t>(p);
}

inline void UNALIGNED_STORE16(void* p, uint16_t v) {
  base::UnalignedStore(p, v);
}

inline void UNALIGNED_STORE32(void* p, uint32_t v) {
  base::UnalignedStore(p, v);
}

inline void UNALIGNED_STORE64(void* p, uint64_t v) {
  base::UnalignedStore(p, v);
}

#endif  // defined(__cplusplus), end of unaligned API

// aligned_malloc, aligned_free
#if defined(__cplusplus)

#if defined(COMPILER_MSVC)
inline void* aligned_malloc(size_t size, size_t minimum_alignment) {
  return _aligned_malloc(size, minimum_alignment);
}

inline void aligned_free(void* aligned_memory) {
  _aligned_free(aligned_memory);
}

#else
inline void* aligned_malloc(size_t size, size_t minimum_alignment) {
  // posix_memalign requires that the requested alignment be at least
  // sizeof(void*). In this case, fall back on malloc which should return memory
  // aligned to at least the size of a pointer.
  const size_t required_alignment = sizeof(void*);
  if (minimum_alignment < required_alignment) return malloc(size);
  void* ptr = nullptr;
  if (posix_memalign(&ptr, minimum_alignment, size) == 0) return ptr;
  return nullptr;
}

inline void aligned_free(void* aligned_memory) { free(aligned_memory); }

#endif

#endif  // defined(__cplusplus)

#endif  // THIRD_PARTY_GLOOP_BASE_PORT_H_
