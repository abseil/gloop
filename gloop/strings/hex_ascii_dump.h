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

// Utilities for dealing with hex+ASCII dumps.

#ifndef STRINGS_HEX_ASCII_DUMP_H__
#define STRINGS_HEX_ASCII_DUMP_H__

#include <string>

#include "absl/strings/string_view.h"

namespace strings {

// Given a binary buffer, return a hex+ASCII dump in the style of
// tcpdump's -X and -XX options:
// "0x0000:  0090 69bd 5400 000d 610f 0189 0800 4500  ..i.T...a.....E.\n"
// "0x0010:  001c fb98 4000 4001 7e18 d8ef 2301 455d  ....@.@.~...#.E]\n"
// "0x0020:  7fe2 0800 6bcb 0bc6 806e                 ....k....n\n"
std::string StringToHexASCIIDump(absl::string_view in_buffer);

// Like StringToHexASCIIDump(), but the generated offset labels start
// at 'offset'. The offset argument does *not* affect the portion of
// 'in_buffer' that is dumped. This can be used to dump a string
// holding bytes from somewhere other than the beginning of a larger
// block.
//
//   string data = my_huge_data_source->GetDataAtOffset(0x180, 42);
//   string dump = StringToHexASCIIDumpAtOffset(0x180, data);
//
// If the data in the example of StringToHexASCIIDump() had been
// passed to this function with an offset argument of 0x0180, the
// output would look like this:
//
// "0x0180:  0090 69bd 5400 000d 610f 0189 0800 4500  ..i.T...a.....E.\n"
// "0x0190:  001c fb98 4000 4001 7e18 d8ef 2301 455d  ....@.@.~...#.E]\n"
// "0x01A0:  7fe2 0800 6bcb 0bc6 806e                 ....k....n\n"
std::string StringToHexASCIIDumpAtOffset(int offset,
                                         absl::string_view in_buffer);

// Parse a string containing a hex+ASCII dump in the style of
// StringToHexASCIIDump() and turn it into a raw binary string. If the
// string does not contain a properly formatted hex+ASCII dump, returns
// false and fills in a message describing the syntax violation in
// 'error'.
bool HexASCIIDumpToString(const std::string& in_dump, std::string* out_string,
                          std::string* error);

// Like HexASCIIDumpToString, except that if the string does not
// contain a properly formatted hex+ASCII dump, dies with a CHECK
// failure describing the syntax violation.
std::string HexASCIIDumpToStringOrDie(const std::string& in_dump);

}  // namespace strings

#endif  // STRINGS_HEX_ASCII_DUMP_H__
