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

// This is a simple implementation of a CSV file parser, which attempts to
// comply with the quasi-spec given in RFC 4180.

#ifndef THIRD_PARTY_GLOOP_UTIL_CSV_PARSER_H_
#define THIRD_PARTY_GLOOP_UTIL_CSV_PARSER_H_

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "gloop/strings/bytestream.h"
namespace strings {
class ByteSource {
 public:
  virtual ~ByteSource() {}
  virtual size_t Available() const = 0;
  virtual absl::string_view Peek() = 0;
  virtual void Skip(size_t n) = 0;
};
class ArrayByteSource : public ByteSource {
 public:
  explicit ArrayByteSource(absl::string_view s) : input_(s) {}
  size_t Available() const override { return input_.size(); }
  absl::string_view Peek() override { return input_; }
  void Skip(size_t n) override { input_.remove_prefix(n); }

 private:
  absl::string_view input_;
};
}  // namespace strings

namespace util {
namespace csv {

// These classes parse a RFC 4180-compliant CSV file and return the results
// via an iterator-based interface.  The input to the Iterator is a generic
// ByteSource, which the parser will own and delete at the end of its
// lifetime.  The Parser will only hold as much as one Record (line) of the
// CSV file in memory at a time, so it is efficient at parsing large files.
// csv::Parser is thread-unsafe.
//
// The primary method of interacting with the parser is through the iterator
// interface.  Obtain an iterator by calling Parser::begin(), and use it in
// the usual fashion.  You can even use a range-based for loop!
//
// (Note: You may obtain multiple iterators from the parser, but because the
// Parser iterator has input semantics, advancing one iterator also advances
// any outstanding iterators as well.)
//
// Usage:
//
//   using ::util::csv::Parser;
//   using ::util::csv::Record;
//
//   std::unique_ptr<strings::ByteSource> source = ...;
//   for (const Record& record : Parser(std::move(source))) {
//     absl::Status status = record.status();
//     const std::vector<std::string>& fields = record.fields();
//   }  // At this point the parser deletes the source.
//
// You can parse the entire file and put the results in a vector of Records
// as such:
//
//   File *csv_file;
//   CHECK_OK(file::Open(filename, "r", &csv_file, file::Defaults()));
//   FileCloser closer(csv_file);
//   Parser parser(file::NewFileByteSource(csv_file));
//   std::vector<Record> results(parser.begin(), parser.end());
//
// The parser expects input to be well-formed, as per RFC 4180.  However,
// not all CSV producers create such input, so we allow a few exceptions:
//  - Separate records in the same input stream may have different numbers
//    of fields.  How to handle this case is left to the caller.
//  - For the purposes of the format, we consider any of '\n', '\r', and '\r\n'
//    to be valid newline delimiters.  They will be collapsed throughout the
//    file, even when quoted.
//  - The last record in a file may omit the trailing newline.
//  - Fields may be partially quoted.  i.e., quotes do not have to appear at
//    the beginning of a field, but any quote within a field will be assumed
//    to start a quoted part of that field.
//  - There is an option to not treat quotes as special.
//  - There is an option to parse using MySQL-style escaping. This is similar to
//    RFC-4180 except that quotes are backslash escaped within a
//    quoted field.
//
// An attempt to parse the empty file, will result in parser.begin() ===
// parser.end(), returning no Records.  Empty lines will return one Record
// with a single empty field.
//
// Errors are reported per-record through the Record::status() field.  If
// the parser encounters an error when looking at a record, it will return
// that record with the appropriate error code, and continue parsing from
// the next line delimiter in the input file.
//
// Future Work TODO:
//  - One of the more requested interfaces would be returning the values of
//    a record mapped to a set of column labels.  This interface shouldn't be
//    too difficult to build, but we'll defer it until the vector-based iterator
//    interface is released.

class Record;

// A class which represents a single line in the CSV file.
// Note: Records may span text file lines if newline characters are
// appropriately quoted.
class Record {
 public:
  // Returns the fields in this record.  The reference is valid for the life
  // of the Record object which produced it.
  const std::vector<std::string>& fields() const& { return fields_; }

  // Moves the fields out of the record.
  std::vector<std::string>&& fields() && { return std::move(fields_); }

  // Returns the number of this record, counted from 0 as the first record in
  // the input
  int64_t number() const { return number_; }

  // Returns the status of the record.  Parsing of a record will halt on
  // the first error, which will be returned through this status object.
  // The error code will be util::error::INTERNAL.
  absl::Status status() const { return status_; }

 private:
  friend class Parser;

  explicit Record(int64_t number) : number_(number) {}

  absl::Status status_;
  int64_t number_;
  std::vector<std::string> fields_;
};

// See file-level comments above for description and use
class Parser {
 public:
  enum Mode : int {
    // Normal mode per RFC4180.
    RFC4180,
    // Treat quotes as literal quotes (i.e. not a special character).
    LITERAL_QUOTES,
    // Allow MySQL-style escaping, which is similar to RFC-4180 except that
    // quotes are backslash-escaped within quoted fields. See
    // https://goo.gl/YSSTHf for a discussion of why it's difficult to export
    // RFC4180 compliant CSV with MySQL.
    MYSQL_ESCAPING,
  };

  class Iterator : public std::iterator<std::input_iterator_tag, Record> {
   public:
    Iterator(Parser* parser, bool is_end) : parser_(parser), is_end_(is_end) {}

    Iterator& operator++();

    // These references are only valid until the iterator is next incremented.
    const Record& operator*() const { return parser_->record_; }
    const Record* operator->() const { return &parser_->record_; }

    bool operator==(const Iterator& that) const {
      // The parser may have been advanced by some other iterator, so we
      // need to possibly update our is_end_ state...but we can't do that
      // here since this is a const method.  Fortunately, this is the
      // only method that reads the is_end_ value, so we can just fake it
      // using these two temporary variables (they should get inlined into
      // the return expression).
      bool at_end = is_end_ || !parser_->has_more_data_;
      bool that_at_end = that.is_end_ || !that.parser_->has_more_data_;

      // Iterators are equal they belong to the same parser, and are
      // either both at the end, or not both at the end.
      return (parser_ == that.parser_) && (at_end == that_at_end);
    }

    bool operator!=(const Iterator& that) const { return !(*this == that); }

   private:
    Parser* const parser_;
    bool is_end_;
  };

  typedef Iterator iterator_type;
  typedef const Record value_type;

  // Initializes the Parser with the given field delimiter and parse mode.
  explicit Parser(std::unique_ptr<strings::ByteSource> source, char delim = ',',
                  Mode mode = Mode::RFC4180);
  // Same as above. Takes ownership of `source`.
  // NOTE: Prefer the previous c'tor, which takes a std::unique_ptr, because
  // then the ownership transfer is explicit at the call site.
  explicit Parser(strings::ByteSource* source, char delim = ',',
                  Mode mode = Mode::RFC4180);

  // This type is neither copyable nor movable.
  Parser(const Parser&) = delete;
  Parser& operator=(const Parser&) = delete;

  // begin() will always return an iterator to the current position in
  // the parser, regardless of whether data has already been consumed or
  // not.  Advancing one iterator will advance all outstanding iterators.
  Iterator begin() const { return begin_iter_; }
  Iterator end() const { return end_iter_; }

 private:
  void Advance();
  bool PopulateCurrentString();

  // These are all initialized by the constructors
  std::unique_ptr<strings::ByteSource> source_;
  absl::string_view::size_type orig_str_size_;
  absl::string_view current_str_;
  Record record_;
  bool has_more_data_;
  const char delim_;
  const Mode mode_;
  int64_t record_count_;
  Iterator begin_iter_;
  Iterator end_iter_;
};

// Parses a single CSV-encoded line with the same behavior as the Parser above.
// Returns the resulting record.  The trailing newline delimiter is allowed,
// but not required.  If multiple records are present, only the first one is
// returned.  The single argument variant uses ',' as the delimiter.
//
// By convention, an attempt to parse the empty line will result in a
// Record which contains one empty field.
Record ParseLine(absl::string_view line);
Record ParseLine(absl::string_view line, char delim,
                 Parser::Mode mode = Parser::Mode::RFC4180);
}  // namespace csv
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_CSV_PARSER_H_
