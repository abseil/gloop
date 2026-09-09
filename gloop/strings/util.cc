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

// TODO: visit each const_cast.  Some of them are no longer necessary
// because last Single Unix Spec.

#include "gloop/strings/util.h"

#include <sys/types.h>

#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include "absl/algorithm/container.h"
#include "absl/base/internal/raw_logging.h"  // NOLINT
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#ifdef _MSC_VER
#include <string.h>
#endif

#ifdef _WIN32
#ifdef min  // windows.h defines this to something silly
#undef min
#endif
#endif

namespace {

// define the portable version of strcasecmp() and strncasecmp() because they
// don't exist in MSVC.
inline int PortableStrCaseCmp(const char* s1, const char* s2) {
#ifdef _MSC_VER
  return _stricmp(s1, s2);
#else
  return strcasecmp(s1, s2);
#endif
}

inline int PortableStrnCaseCmp(const char* s1, const char* s2, size_t n) {
#ifdef _MSC_VER
  return _strnicmp(s1, s2, n);
#else
  return strncasecmp(s1, s2, n);
#endif
}

}  // namespace

char* strnstr(const char* haystack, const char* needle, size_t haystack_len) {
  if (*needle == '\0') {
    return const_cast<char*>(haystack);
  }
  size_t needle_len = strlen(needle);
  char* where;
  while ((where = strnchr(haystack, *needle, haystack_len)) != nullptr) {
    if (where - haystack + needle_len > haystack_len) {
      return nullptr;
    }
    if (strncmp(where, needle, needle_len) == 0) {
      return where;
    }
    haystack_len -= where + 1 - haystack;
    haystack = where + 1;
  }
  return nullptr;
}

const char* strnprefix(const char* haystack, ptrdiff_t haystack_size,
                       const char* needle, ptrdiff_t needle_size) {
  if (needle_size > haystack_size) {
    return nullptr;
  } else {
    if (strncmp(haystack, needle, needle_size) == 0) {
      return haystack + needle_size;
    } else {
      return nullptr;
    }
  }
}

const char* strncaseprefix(const char* haystack, ptrdiff_t haystack_size,
                           const char* needle, ptrdiff_t needle_size) {
  if (needle_size > haystack_size) {
    return nullptr;
  } else {
    if (PortableStrnCaseCmp(haystack, needle, needle_size) == 0) {
      return haystack + needle_size;
    } else {
      return nullptr;
    }
  }
}

char* strcasesuffix(char* str, const char* suffix) {
  const size_t lenstr = strlen(str);
  const size_t lensuffix = strlen(suffix);
  char* strbeginningoftheend = str + lenstr - lensuffix;

  if (lenstr >= lensuffix &&
      0 == PortableStrCaseCmp(strbeginningoftheend, suffix)) {
    return (strbeginningoftheend);
  } else {
    return nullptr;
  }
}

const char* strnsuffix(const char* haystack, ptrdiff_t haystack_size,
                       const char* needle, ptrdiff_t needle_size) {
  if (needle_size > haystack_size) {
    return nullptr;
  } else {
    const char* start = haystack + haystack_size - needle_size;
    if (strncmp(start, needle, needle_size) == 0) {
      return start;
    } else {
      return nullptr;
    }
  }
}

const char* strncasesuffix(const char* haystack, ptrdiff_t haystack_size,
                           const char* needle, ptrdiff_t needle_size) {
  if (needle_size > haystack_size) {
    return nullptr;
  } else {
    const char* start = haystack + haystack_size - needle_size;
    if (PortableStrnCaseCmp(start, needle, needle_size) == 0) {
      return start;
    } else {
      return nullptr;
    }
  }
}

char* strchrnth(const char* str, const char c, int n) {
  if (str == nullptr) return nullptr;
  if (n <= 0) return const_cast<char*>(str);
  const char* sp;
  int k = 0;
  for (sp = str; *sp != '\0'; sp++) {
    if (*sp == c) {
      ++k;
      if (k >= n) break;
    }
  }
  return (k < n) ? nullptr : const_cast<char*>(sp);
}

char* AdjustedLastPos(const char* str, char separator, int n) {
  if (str == nullptr) return nullptr;
  const char* pos = nullptr;
  if (n > 0) pos = strchrnth(str, separator, n);

  // if n <= 0 or separator appears fewer than n times, get the last occurrence
  if (pos == nullptr) pos = strrchr(str, separator);
  return const_cast<char*>(pos);
}

// ----------------------------------------------------------------------
// Misc. routines
// ----------------------------------------------------------------------

bool IsAscii(absl::string_view str) {
  for (char c : str) {
    if (!absl::ascii_isascii(c)) {
      return false;
    }
  }
  return true;
}

namespace strings {

bool IsPrint(absl::string_view str) {
  return absl::c_all_of(str, absl::ascii_isprint);
}

}  // namespace strings

char* gstrcasestr(const char* haystack, const char* needle) {
  char c, sc;
  size_t len;

  if ((c = *needle++) != 0) {
    c = absl::ascii_tolower(c);
    len = strlen(needle);
    do {
      do {
        if ((sc = *haystack++) == 0) return nullptr;
      } while (absl::ascii_tolower(sc) != c);
    } while (PortableStrnCaseCmp(haystack, needle, len) != 0);
    haystack--;
  }
  // This is a const violation but strstr() also returns a char*.
  return const_cast<char*>(haystack);
}

const char* gstrncasestr(const char* haystack, const char* needle, size_t len) {
  char c, sc;

  if ((c = *needle++) != 0) {
    c = absl::ascii_tolower(c);
    size_t needle_len = strlen(needle);
    do {
      do {
        if (len-- <= needle_len || 0 == (sc = *haystack++)) return nullptr;
      } while (absl::ascii_tolower(sc) != c);
    } while (PortableStrnCaseCmp(haystack, needle, needle_len) != 0);
    haystack--;
  }
  return haystack;
}

char* gstrncasestr_split(const char* str, const char* prefix, char non_alpha,
                         const char* suffix, size_t n) {
  size_t prelen = prefix == nullptr ? 0 : strlen(prefix);
  size_t suflen = suffix == nullptr ? 0 : strlen(suffix);

  // adjust the string and its length to avoid unnecessary searching.
  // an added benefit is to avoid unnecessary range checks in the if
  // statement in the inner loop.
  if (suflen + prelen >= n) return nullptr;
  str += prelen;
  n -= prelen;
  n -= suflen;

  const char* where = nullptr;

  // for every occurrence of non_alpha in the string ...
  while ((where = static_cast<const char*>(memchr(str, non_alpha, n))) !=
         nullptr) {
    // ... test whether it is followed by suffix and preceded by prefix
    if ((!suflen || PortableStrnCaseCmp(where + 1, suffix, suflen) == 0) &&
        (!prelen || PortableStrnCaseCmp(where - prelen, prefix, prelen) == 0)) {
      return const_cast<char*>(where - prelen);
    }
    // if not, advance the pointer, and adjust the length according
    n -= (where + 1) - str;
    str = where + 1;
  }

  return nullptr;
}

char* strcasestr_alnum(const char* haystack, const char* needle) {
  const char* haystack_ptr;
  const char* needle_ptr;

  // Skip non-alnums at beginning
  while (!absl::ascii_isalnum(*needle))
    if (*needle++ == '\0') return const_cast<char*>(haystack);
  needle_ptr = needle;

  // Skip non-alnums at beginning
  while (!absl::ascii_isalnum(*haystack))
    if (*haystack++ == '\0') return nullptr;
  haystack_ptr = haystack;

  while (*needle_ptr != '\0') {
    // Non-alnums - advance
    while (!absl::ascii_isalnum(*needle_ptr))
      if (*needle_ptr++ == '\0') return const_cast<char*>(haystack);

    while (!absl::ascii_isalnum(*haystack_ptr))
      if (*haystack_ptr++ == '\0') return nullptr;

    if (absl::ascii_tolower(*needle_ptr) ==
        absl::ascii_tolower(*haystack_ptr)) {
      // Case-insensitive match - advance
      needle_ptr++;
      haystack_ptr++;
    } else {
      // No match - rollback to next start point in haystack
      haystack++;
      while (!absl::ascii_isalnum(*haystack))
        if (*haystack++ == '\0') return nullptr;
      haystack_ptr = haystack;
      needle_ptr = needle;
    }
  }
  return const_cast<char*>(haystack);
}

int CountSubstring(absl::string_view text, absl::string_view substring) {
  ABSL_RAW_CHECK(!substring.empty(), "");

  int count = 0;
  absl::string_view::size_type curr = 0;
  while (absl::string_view::npos != (curr = text.find(substring, curr))) {
    ++count;
    ++curr;
  }
  return count;
}

const char* strstr_delimited(const char* haystack, const char* needle,
                             char delim) {
  if (!needle || !haystack) return nullptr;
  if (*needle == '\0') return haystack;

  // We check for 4 different cases:
  // (1) The haystack equals the needle.
  // (2) The haystack starts with needle + delimiter (e.g. "foo,")
  // (3) The haystack contains delimiter + needle + delimiter (e.g. ",foo,")
  // (4) The haystack ends with delimiter + needle (e.g. ",foo")
  // Case (3), the most expensive check, is done with absl::string_view::find(),
  // which uses the efficient memmatch() function.

  // Construct a delimited needle, e.g. ",foo,"
  const absl::string_view delim_sp = absl::string_view(&delim, 1);
  const std::string delim_needle = absl::StrCat(delim_sp, needle, delim_sp);

  const size_t n = delim_needle.length();
  const size_t needle_length = n - 2;

  // Construct absl::string_views of the haystack, the needle, needle +
  // delimiter and delimiter + needle.
  const absl::string_view haystack_sp = haystack;
  const absl::string_view needle_sp =
      absl::string_view(delim_needle.data() + 1, n - 2);
  const absl::string_view first_needle =
      absl::string_view(delim_needle.data() + 1, n - 1);
  const absl::string_view last_needle =
      absl::string_view(delim_needle.data(), n - 1);

  // Cases (1) and (2)
  if (haystack_sp == needle_sp || absl::StartsWith(haystack_sp, first_needle)) {
    return haystack;
  }

  // Case (3)
  absl::string_view::size_type i =
      haystack_sp.find(absl::string_view(delim_needle));
  if (i != absl::string_view::npos) {
    return haystack + i + 1;  // (+1 to skip the delimiter)
  }

  // Case (4)
  if (absl::EndsWith(haystack_sp, last_needle)) {
    return haystack_sp.data() + haystack_sp.size() - needle_length;
  }

  return nullptr;
}

// ----------------------------------------------------------------------
// Older versions of libc have a buggy strsep.
// ----------------------------------------------------------------------

char* gstrsep(char** stringp, const char* delim) {
  char* s;
  const char* spanp;
  int c, sc;
  char* tok;

  if ((s = *stringp) == nullptr) return nullptr;

  tok = s;
  while (true) {
    c = *s++;
    spanp = delim;
    do {
      if ((sc = *spanp++) == c) {
        if (c == 0)
          s = nullptr;
        else
          s[-1] = 0;
        *stringp = s;
        return tok;
      }
    } while (sc != 0);
  }
  // unreachable
}

char* strdup_with_new(const char* the_string) {
  if (the_string == nullptr)
    return nullptr;
  else
    return strndup_with_new(the_string, strlen(the_string));
}

char* strndup_with_new(const char* the_string, size_t max_length) {
  if (the_string == nullptr) return nullptr;

  char* result = new char[max_length + 1];
  result[max_length] = '\0';  // terminate the string because strncpy might not
  return strncpy(result, the_string, max_length);
}

const char* ScanForFirstWord(const char* the_string, const char** end_ptr) {
  ABSL_RAW_CHECK(end_ptr != nullptr, "Precondition violated");

  if (the_string == nullptr)  // empty string
    return nullptr;

  const char* curr = the_string;
  while ((*curr != '\0') && absl::ascii_isspace(*curr))  // skip initial spaces
    ++curr;

  if (*curr == '\0')  // no valid word found
    return nullptr;

  // else has a valid word
  const char* first_word = curr;

  // now locate the end of the word
  while ((*curr != '\0') && !absl::ascii_isspace(*curr)) ++curr;

  *end_ptr = curr;
  return first_word;
}

namespace strings {

absl::string_view ScanForFirstWord(absl::string_view input) {
  const char* curr = input.data();
  const char* const end = curr + input.size();

  // Skip initial spaces to locate the start of the word.
  while ((curr < end) && absl::ascii_isspace(*curr)) ++curr;
  const char* const word = curr;

  // Skip subsequent non-spaces to locate the end of the word.
  while ((curr < end) && !absl::ascii_isspace(*curr)) ++curr;
  return absl::string_view(word, curr - word);
}

}  // namespace strings

const char* AdvanceIdentifier(const char* str) {
  // Not using isalpha and isalnum so as not to rely on the locale.
  // We could have used absl::ascii_isalpha and absl::ascii_isalnum.
  char ch = *str++;
  if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_'))
    return nullptr;
  while (true) {
    ch = *str;
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9') || ch == '_'))
      return str;
    str++;
  }
}

bool AdvanceIdentifier(absl::string_view* str) {
  if (str->empty()) return false;
  char ch = str->front();
  if (!absl::ascii_isalpha(ch) && ch != '_') return false;
  str->remove_prefix(1);
  for (; !str->empty(); str->remove_prefix(1)) {
    ch = str->front();
    if (!absl::ascii_isalnum(ch) && ch != '_') break;
  }
  return true;
}

bool IsIdentifier(absl::string_view str) {
  return AdvanceIdentifier(&str) && str.empty();
}

void UniformInsertString(std::string* s, ptrdiff_t interval,
                         const char* separator) {
  const size_t separator_len = strlen(separator);

  if (interval < 1 ||      // invalid interval
      s->empty() ||        // nothing to do
      separator_len == 0)  // invalid separator
    return;

  size_t num_inserts = (s->size() - 1) / interval;  // -1 to avoid append at end
  if (num_inserts == 0)                             // nothing to do
    return;

  std::string tmp;
  tmp.reserve(s->size() + num_inserts * separator_len + 1);

  for (size_t i = 0; i < num_inserts; ++i) {
    // append this interval
    tmp.append(*s, i * interval, interval);
    // append a separator
    tmp.append(separator, separator_len);
  }

  // append the tail
  const size_t tail_pos = num_inserts * interval;
  tmp.append(*s, tail_pos, s->size() - tail_pos);

  s->swap(tmp);
}

ptrdiff_t FindNth(absl::string_view s, char c, int n) {
  absl::string_view::size_type pos = absl::string_view::npos;

  for (int i = 0; i < n; ++i) {
    pos = s.find_first_of(c, pos + 1);
    if (pos == absl::string_view::npos) {
      break;
    }
  }
  return pos;
}

ptrdiff_t ReverseFindNth(absl::string_view s, char c, int n) {
  if (n <= 0) {
    return -1;
  }
  absl::string_view::size_type pos = s.size();
  for (int i = 0; i < n; ++i) {
    if (pos == 0) {
      return -1;
    }
    pos = s.find_last_of(c, static_cast<absl::string_view::size_type>(pos - 1));
    if (pos ==
        static_cast<absl::string_view::size_type>(absl::string_view::npos)) {
      return -1;
    }
  }
  return pos;
}

namespace strings {

// FindEol()
// Returns the location of the next end-of-line sequence.

absl::string_view FindEol(absl::string_view s) {
  for (size_t i = 0; i < s.length(); ++i) {
    if (s[i] == '\n') {
      return absl::string_view(s.data() + i, 1);
    }
    if (s[i] == '\r') {
      if (i + 1 < s.length() && s[i + 1] == '\n') {
        return absl::string_view(s.data() + i, 2);
      } else {
        return absl::string_view(s.data() + i, 1);
      }
    }
  }
  return absl::string_view(s.data() + s.length(), 0);
}

bool ContainsWhitespace(absl::string_view s) {
  return absl::c_any_of(s, absl::ascii_isspace);
}

}  // namespace strings

bool OnlyWhitespace(absl::string_view s) {
  return absl::c_all_of(s, absl::ascii_isspace);
}

void PrefixSuccessor(std::string* prefix) {
  // We can increment the last character in the string and be done
  // unless that character is 255 (0xff), in which case we have to erase the
  // last character and increment the previous character, unless that
  // is 255, etc. If the string is empty or consists entirely of
  // 255's, we just return the empty string.
  while (!prefix->empty()) {
    char& c = prefix->back();
    if (c == '\xff') {  // char literal avoids signed/unsigned.
      prefix->pop_back();
    } else {
      ++c;
      break;
    }
  }
}

std::string ImmediateSuccessor(absl::string_view s) {
  // Return the input string, with an additional NUL byte appended.
  std::string out;
  out.reserve(s.size() + 1);
  out.append(s.data(), s.size());
  out.push_back('\0');
  return out;
}

void FindShortestSeparator(absl::string_view start, absl::string_view limit,
                           std::string* separator) {
  // Find length of common prefix
  size_t min_length = std::min(start.size(), limit.size());
  size_t diff_index = 0;
  while ((diff_index < min_length) &&
         (start[diff_index] == limit[diff_index])) {
    diff_index++;
  }

  if (diff_index >= min_length) {
    // Handle the case where either string is a prefix of the other
    // string, or both strings are identical.
    separator->assign(start.data(), start.size());
    return;
  }

  if (diff_index + 1 == start.size()) {
    // If the first difference is in the last character, do not bother
    // incrementing that character since the separator will be no
    // shorter than "start".
    separator->assign(start.data(), start.size());
    return;
  }

  if (start[diff_index] == '\xff') {  // char literal avoids signed/unsigned.
    // Avoid overflow when incrementing start[diff_index]
    separator->assign(start.data(), start.size());
    return;
  }

  separator->assign(start.data(), diff_index);
  separator->push_back(start[diff_index] + 1);
  if (*separator >= limit) {
    // Never pick a separator that causes confusion with "limit"
    separator->assign(start.data(), start.size());
  }
}

int SafeSnprintf(char* str, size_t size, const char* format, ...) {
  va_list printargs;
  va_start(printargs, format);
  int ncw = vsnprintf(str, size, format, printargs);
  va_end(printargs);
  return (ncw >= 0 && static_cast<size_t>(ncw) < size) ? ncw : 0;
}

bool GetlineFromStdioFile(FILE* file, std::string* str, char delim) {
  str->erase();
  while (true) {
    if (feof(file) || ferror(file)) {
      return false;
    }
    int c = getc(file);
    if (c == EOF) return false;
    if (c == delim) return true;
    str->push_back(c);
  }
}
