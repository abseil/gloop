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

// The Writer class writes CSV data in a format based on RFC 4180:
//   http://tools.ietf.org/html/rfc4180
// Writer output will be compliant with RFC 4180 except that by default,
// record delimiters are '\n' instead of '\r\n'.
// Additionally, the caller can override the
// field delimiter to be something other than comma.
//
// EXAMPLE WRITING SEQUENCE CONTAINERS
//
// This example uses StringByteSink for output, but any ByteSink implementation
// can be provided to the Writer constructor.
// When passing a DefaultOrder functor to the Writer constructor, WriteRecord()
// can accept any sequence container (vector, list, etc.) of char*, string, or
// string_view objects.
//
//   string output;
//   ::util::csv::Writer<::util::csv::DefaultOrder> writer(
//       /*num_fields=*/3,
//       ::util::csv::DefaultOrder(),
//       absl::make_unique<strings::StringByteSink>(&output));
//   for (const auto& record : caller_data) {
//     if (!writer.WriteRecord(record).ok()) {
//       LOG(ERROR) << writer.status();
//       // handle error
//     }
//   }
//
// EXAMPLE WRITING MAP CONTAINERS
//
// When passing a HeaderOrder functor to the Writer constructor, WriteRecord()
// can accept any sequence container or std::map of char*, string, or
// string_view objects. The headers provided to the HeaderOrder constructor
// define the field names and field order. The header names must match the keys
// of the std::map object passed to WriteRecord().
//
//   std::vector<string> headers {"Element", "Atomic Number", "Symbol"};
//   string output;
//   ::util::csv::Writer<::util::csv::HeaderOrder> writer(
//       /*num_fields=*/3,
//       ::util::csv::HeaderOrder(headers),
//       absl::make_unique<strings::StringByteSink>(&output));
//   // Write the headers as the first record (optional)
//   if (!writer.WriteRecord(headers).ok()) {
//       LOG(ERROR) << writer.status();
//       // handle error
//   }
//   // record is a std::map of char*, string, or string_view key-value types
//   for (const auto& record : caller_data) {
//     if (!writer.WriteRecord(record).ok()) {
//       LOG(ERROR) << writer.status();
//       // handle error
//     }
//   }
//
// TERMINOLOGY
//
// - field: a cell of tabular data
// - record: a row of tabular data
// - headers: the optional first row which defines the field names
//
// OPTIONS AND CHARACTERISTICS OF THE OUTPUT
//
// - Writer uses comma as the default field delimiter, but it can be overridden.
// - Each field of a record, except the last, is followed by the field
//   delimiter.
// - Writer terminates each record (also known as record delimiter),
//   including the last, with LF('\n') by default, but it can be overridden to
//   use CRLF('\r\n') by calling SetCRLFRecordDelimiter.
// - The caller can optionally add headers. Example output:
//     First Name,Last Name
//     Bonnie,Parker
//     Clyde,Barrow
// - Writer escapes all double quotes in a field by preceding them with
//   another double quote. See example below.
// - Space characters, including leading and trailing space, are written to
//   the output like other characters. The second field of the following record
//   has both a leading and trailing space:
//     a, b ,c,d,e
// - By default, writer will enclose a field with double quotes if and only if
//   it contains any of the following special characters:
//     * field delimiter : default is comma(,), but caller can override
//     * newline : \n
//     * carriage return : \r
//     * double quote : "
//   Example output using default field delimiter:
//     simple,"with, comma", with white space
//     "one field on
//     two lines","with "" double quote",end of second record
// - The default quoting behavior described above can be overridden by passing
//   a custom field formatter to the Writer's constructor.
//
// OUTPUT OBJECT
//
// Writer uses the strings::ByteSink interface for output. There are various
// ready-to-use implementations of this interface, or the caller could implement
// one. After each record is written, Writer will call Flush() on the
// caller's ByteSink object unless SetAutoFlush(false) was called. Auto flush
// can be disabled to allow the caller to manage flushing. A Writer object takes
// ownership of the sink object and will delete it upon Writer destruction.
//
// ERROR HANDLING
//
// All Writer functions that may result in an error due to caller data offer two
// ways to "catch" and access the error:
// - Writer returns absl::Status from the function.
// - Caller can call Writer::status() to get the latest Status object.
//
// This creates a convenient pattern for callers to handle errors:
//   if (!writer.WriteRecord(data).ok()) {
//     LOG(ERROR) << writer.status();
//     // handle error
//   }
//
// If the caller would rather terminate the process on error, use the CHECK_OK
// macro defined in util/task/status.h:
//   CHECK_OK(writer.WriteRecord(data));
//
// THREAD SAFETY
//
// This class is thread-unsafe. It is the caller’s responsibility to
// synchronize access to objects of this type when being shared across multiple
// threads.

#ifndef THIRD_PARTY_GLOOP_UTIL_CSV_WRITER_H_
#define THIRD_PARTY_GLOOP_UTIL_CSV_WRITER_H_

#include <cstddef>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/die_if_null.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/strings/bytestream.h"

namespace util {
namespace csv {

// The Writer class is described above.
//
// In most cases, the FieldOrder template parameter will be one of these
// classes defined below:
//
//   DefaultOrder - Use this FieldOrder if you will be writing records using
//   sequence containers. Writer:WriteRecord() can accept any array or sequence
//   container (vector, list, etc.) of char*, string, or string_view objects.
//
//   HeaderOrder - Use this FieldOrder if you will be writing records using maps
//   where the key names of the maps match the CSV header names.
//   Writer:WriteRecord() can accept any array, sequence container, or std::map
//   container of char*, string, or string_view objects. The headers provided to
//   the HeaderOrder constructor define the field names and field order. The
//   header names must match the keys of the std::map object passed to
//   WriteRecord().
//
// Custom FieldOrder objects can be created and used. The only requirement of
// this functor is that it has a function call operator that takes a record of
// type T, transforms the input record into a sequence container (e.g. vector)
// of char*, string, or string_view objects of correctly ordered fields, and
// returns this object. Type T is the same type that will be provided to
// Writer::WriteRecord(). If a vector of string_view is to be returned for
// example, then its signature should be like:
//   template <typename T>
//   std::vector<string_view> operator()(const T& record) const;
//
template <typename FieldOrder>
class Writer {
 public:
  // Basic constructor.
  // Args:
  //   num_fields: The number of fields per record.
  //   order: A FieldOrder object whose type matches the class template
  //       parameter. An instance of either DefaultOrder or HeaderOrder functors
  //       which are decleared below.
  //   sink: Writer will write all output to this ByteSink.
  Writer(size_t num_fields, FieldOrder order,
         std::unique_ptr<strings::ByteSink> sink);

  // Same as above, but takes ownership of "sink".
  // NOTE: Prefer the previous c'tor, which takes a std::unique_ptr, because
  // then the ownership transfer is explicit at the call site.
  Writer(size_t num_fields, FieldOrder order, strings::ByteSink* sink);

  // Constructor to override field delimiter.
  // Args:
  //   num_fields: The number of fields per record.
  //   order: Same as constructor above.
  //   field_delimiter: Writer will use this to delimit fields in the output.
  //       Default is comma.
  //   sink: Writer will write all output to this ByteSink.
  Writer(size_t num_fields, FieldOrder order, char field_delimiter,
         std::unique_ptr<strings::ByteSink> sink);

  // Same as above, but takes ownership of "sink".
  // NOTE: Prefer the previous c'tor, which takes a std::unique_ptr, because
  // then the ownership transfer is explicit at the call site.
  Writer(size_t num_fields, FieldOrder order, char field_delimiter,
         strings::ByteSink* sink);

  // Constructor to override field delimiter and field formatter.
  // Args:
  //   num_fields: The number of fields per record.
  //   order: Same as constructor above.
  //   field_delimiter: Writer will use this to delimit fields in the output.
  //       Default is comma.
  //   sink: Writer will write all output to this ByteSink.
  //   field_formatter: Writer will use this to format the field (putting in
  //       quotes and encode special characters if necessary).
  Writer(size_t num_fields, FieldOrder order, char field_delimiter,
         std::unique_ptr<strings::ByteSink> sink,
         std::function<void(absl::string_view, strings::ByteSink*)>
             field_formatter);

  // Same as above, but takes ownership of "sink".
  // NOTE: Prefer the previous c'tor, which takes a std::unique_ptr, because
  // then the ownership transfer is explicit at the call site.
  Writer(size_t num_fields, FieldOrder order, char field_delimiter,
         strings::ByteSink* sink,
         std::function<void(absl::string_view, strings::ByteSink*)>
             field_formatter);

  // This type is neither copyable nor movable.
  Writer(const Writer&) = delete;
  Writer& operator=(const Writer&) = delete;

  // Writes the record terminated with newline to the sink and calls Flush()
  // on the sink unless SetAutoFlush(false) was called.
  //
  // The number of fields supplied and resolved by the class's FieldOrder
  // parameter must match num_fields passed to the constructor, or an
  // error will be returned.
  //
  // If headers have been supplied to the FieldOrder object passed to the
  // Writer constructor, this record must be consistent with the headers.
  //
  // Writing records with no fields is ok; each call to WriteRecord will
  // result in writing only a newline.
  //
  // Args:
  //   record: fields to be written
  // Returns:
  //   Status object indicating success or failure. Also see status().
  template <typename C>
  absl::Status WriteRecord(const C& record);

  // Returns the last Status object returned from this instance.
  absl::Status status() const { return status_; }

  // Sets whether the Writer should flush the given sink after each record
  // is written. By default, auto flush is enabled.
  void SetAutoFlush(bool auto_flush) { auto_flush_ = auto_flush; }

  // Sets whether to use "\r\n" as the record delimiter rather than the
  // default of "\n".
  void SetCRLFRecordDelimiter(bool use_crlf) {
    record_delimiter_ = use_crlf ? "\r\n" : "\n";
  }

 private:
  // sink provided to constructor
  std::unique_ptr<strings::ByteSink> sink_;
  // Expected size of each record, else error
  size_t num_fields_;
  // functor provided to constructor that defines field order when when writing
  // to the sink with associative containers
  FieldOrder order_;
  // field delimiter
  char field_delimiter_;
  // record delimiter
  std::string record_delimiter_;
  // Status object last returned
  absl::Status status_;
  // Whether to auto Flush the ByteSink in WriteRecord
  bool auto_flush_;
  // field formatter. Responsible for properly formatting the field and
  // appending the result to the specified sink.
  std::function<void(absl::string_view, strings::ByteSink*)> field_formatter_;
};

// Implementation of a FieldOrder functor that returns the fields in their given
// order. This is used for writing sequence containers like vector, deque,
// list, array, etc.
class DefaultOrder {
 public:
  // Called by the Writer class to order the given record fields.
  // This FieldOrder does not order the fields.
  // Args:
  //   record: sequence container record being written
  // Returns:
  //   ordered output record to be written
  template <typename SeqCont>
  std::vector<absl::string_view> operator()(const SeqCont& record) const;
};

namespace internal {

template <typename T, typename = void>
inline constexpr bool is_map_like = false;

template <class T>
inline constexpr bool is_map_like<T, std::void_t<typename T::mapped_type,  //
                                                 typename T::key_type>> = true;

}  // namespace internal

// Implementation of a FieldOrder functor that returns the fields in the order
// specified by the headers vector that was passed to the constructor. Using
// this FieldOrder functor allows the caller to pass std::map objects to
// Writer::WriteRecord(). This functor also works like DefaultOrder when writing
// sequence containers.
class HeaderOrder {
 public:
  // Primary constructor
  // Args:
  //   headers: The ordered header names.
  //       If std::map is used to write records, these names define the keys
  //       of the map values to be written, and Writer outputs fields in the
  //       order defined by these headers.
  explicit HeaderOrder(const std::vector<std::string>& headers)
      : headers_(headers) {}

  // Called by the Writer class to order the given record fields.
  // The behavior depends on the type of the record:
  // - If the record is a map like object, this FieldOrder orders the
  //   associative container fields using the headers supplied to the
  //   constructor. In this case the map keys must be a superset of the headers
  //   supplied to the HeaderOrder constructor, so that a value for each header
  //   name can be found.
  // - If the record is a sequence, this FieldOrder does not order the fields.
  // Args:
  //   record: Map or Sequence record being written.
  // Returns:
  //   ordered output record to be written
  template <typename R>
  std::vector<absl::string_view> operator()(const R& record) const;

 private:
  std::vector<std::string> headers_;
};

// Helper function to return a single CSV record without newline termination.
//
// NOTE when writing many records:
// This function should be avoided when writing many records, because it is
// wasteful to create extra strings in each iteration of CSV data generation
// and this function can not verify that all records are the same size.
//
// Args:
//   record: Sequence container of fields to be written
// Returns:
//   CSV formatted string that is not newline terminated
template <typename SeqCont>
std::string WriteRecordToString(const SeqCont& record);

// Default quoting behavior of the CSV writer. A field will only be quoted when
// it contains the default set of special characters: field delimiter, newline
// '\n', carriage return '\r' and double quote : '"'.
std::function<void(absl::string_view, strings::ByteSink*)>
StandardFieldFormatter(char field_delimiter);

// -----------------------------------------------------------------------------
// IMPLEMENTATION DETAILS BELOW - Move along, nothing more to see here
// -----------------------------------------------------------------------------

// Basic Writer constructor
template <typename FieldOrder>
Writer<FieldOrder>::Writer(size_t num_fields, FieldOrder order,
                           std::unique_ptr<strings::ByteSink> sink)
    : Writer(num_fields, order, ',', std::move(sink)) {}

// Writer constructor that overrides field delimiter
template <typename FieldOrder>
Writer<FieldOrder>::Writer(size_t num_fields, FieldOrder order,
                           char field_delimiter,
                           std::unique_ptr<strings::ByteSink> sink)
    : Writer(num_fields, order, field_delimiter, std::move(sink),
             StandardFieldFormatter(field_delimiter)) {}

template <typename FieldOrder>
Writer<FieldOrder>::Writer(
    size_t num_fields, FieldOrder order, char field_delimiter,
    std::unique_ptr<strings::ByteSink> sink,
    std::function<void(absl::string_view, strings::ByteSink*)> field_formatter)
    : sink_(std::move(sink)),
      num_fields_(num_fields),
      order_(order),
      field_delimiter_(field_delimiter),
      record_delimiter_("\n"),
      auto_flush_(true),
      field_formatter_(std::move(field_formatter)) {}

// Constructors taking a bare pointer.
template <typename FieldOrder>
Writer<FieldOrder>::Writer(size_t num_fields, FieldOrder order,
                           strings::ByteSink* sink)
    : Writer(num_fields, order, absl::WrapUnique(ABSL_DIE_IF_NULL(sink))) {}

template <typename FieldOrder>
Writer<FieldOrder>::Writer(size_t num_fields, FieldOrder order,
                           char field_delimiter, strings::ByteSink* sink)
    : Writer(num_fields, order, field_delimiter,
             absl::WrapUnique(ABSL_DIE_IF_NULL(sink)),
             StandardFieldFormatter(field_delimiter)) {}

template <typename FieldOrder>
Writer<FieldOrder>::Writer(
    size_t num_fields, FieldOrder order, char field_delimiter,
    strings::ByteSink* sink,
    std::function<void(absl::string_view, strings::ByteSink*)> field_formatter)
    : Writer(num_fields, order, field_delimiter,
             absl::WrapUnique(ABSL_DIE_IF_NULL(sink)), field_formatter) {}

// Writes record to sink
template <typename FieldOrder>
template <typename C>
absl::Status Writer<FieldOrder>::WriteRecord(const C& record) {
  // Order the fields
  auto ordered_record = order_(record);

  // Verify the record size
  if (num_fields_ != ordered_record.size()) {
    status_ = absl::InvalidArgumentError(absl::StrCat(
        "Expected ", num_fields_, " fields but got ", ordered_record.size()));
    return status_;
  }

  // write the fields of the record, end with record delimiter
  // and possibly flush
  for (size_t i = 0; i < ordered_record.size(); ++i) {
    if (i != 0) {
      sink_->Append(absl::string_view(&field_delimiter_, 1));
    }
    field_formatter_(ordered_record[i], sink_.get());
  }
  sink_->Append(record_delimiter_);
  if (auto_flush_) {
    sink_->Flush();
  }
  status_ = absl::OkStatus();
  return status_;
}

// Orders the record
template <typename SeqCont>
std::vector<absl::string_view> DefaultOrder::operator()(
    const SeqCont& record) const {
  using std::begin;
  using std::end;
  return std::vector<absl::string_view>(begin(record), end(record));
}

// Orders the record
template <typename R>
std::vector<absl::string_view> HeaderOrder::operator()(const R& record) const {
  if constexpr (internal::is_map_like<R>) {
    // The record is a map like object
    std::vector<absl::string_view> ordered;
    ordered.reserve(record.size());
    for (const std::string& field_name : headers_) {
      auto it = record.find(field_name);
      if (it != record.end()) {
        ordered.push_back(it->second);
      }
      // else could be keys expected to ignore, or missing keys that will
      // result in a record size error in the Writer class
    }
    return ordered;
  } else {
    // The record is not a map, we assume an already ordered sequence of values
    using std::begin;
    using std::end;
    return std::vector<absl::string_view>(begin(record), end(record));
  }
}

// Helper function to return a formatted record with a customized delimiter.
template <typename SeqCont>
std::string WriteRecordToString(const SeqCont& record, char field_delimiter) {
  std::string out;
  Writer<DefaultOrder> writer(record.size(), DefaultOrder(), field_delimiter,
                              new strings::StringByteSink(&out));
  // this write should never fail, so ensure it
  CHECK_OK(writer.WriteRecord(record));
  // remove the terminating newline added by WriteRecord
  out.resize(out.size() - 1);
  return out;
}

// Helper function to return a formatted record
template <typename SeqCont>
std::string WriteRecordToString(const SeqCont& record) {
  return WriteRecordToString(record, ',');
}

}  // namespace csv
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_CSV_WRITER_H_
