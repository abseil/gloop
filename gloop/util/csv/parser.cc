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

#include "gloop/util/csv/parser.h"

#include <stddef.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "gloop/strings/bytestream.h"

namespace util {
namespace csv {

enum ParseState {
  FIELD_START,
  IN_FIELD,
  IN_QUOTED_FIELD,
  QUOTED_QUOTE,
  QUOTED_BACKSLASH_ESCAPE,
  ERROR,
  END_OF_RECORD
};

Parser::Iterator& Parser::Iterator::operator++() {
  parser_->Advance();

  if (!parser_->has_more_data_) is_end_ = true;
  return *this;
}

Parser::Parser(strings::ByteSource* source, const char delim, const Mode mode)
    : Parser(absl::WrapUnique(source), delim, mode) {}

Parser::Parser(std::unique_ptr<strings::ByteSource> source, const char delim,
               const Mode mode)
    : source_(std::move(source)),
      orig_str_size_(0),
      record_(0),
      has_more_data_(true),
      delim_(delim),
      mode_(mode),
      record_count_(0),
      begin_iter_(this, source_->Available() == 0),
      end_iter_(this, true) {
  Advance();
}

void Parser::Advance() {
  ParseState state = FIELD_START;
  std::string current_field;

  record_ = Record(record_count_++);

  while (state != ERROR && state != END_OF_RECORD) {
    // If the following conditional is met, that indicates an EOF condition.
    if (current_str_.empty() && !PopulateCurrentString()) {
      // We add the last remaining field if it is non-empty, or we already have
      // existing fields in the Record.  This avoids the problem where a line
      // ends on an empty field.
      if (!current_field.empty() || !record_.fields_.empty())
        record_.fields_.push_back(current_field);
      if (!record_.fields_.empty()) return;

      has_more_data_ = false;
      return;
    }

    // At this point, we know that current_str_ has data.
    char next_char = current_str_[0];
    current_str_.remove_prefix(1);

    // Look for a possible '\r\n' which we treat as a '\n'.
    if (next_char == '\r') {
      if (current_str_.empty() && !PopulateCurrentString()) {
        // We've reached EOF, but don't need to discover that yet, so do
        // nothing

      } else if (current_str_[0] == '\n') {
        // We found the newline character, so we just consume it.
        current_str_.remove_prefix(1);
      }
      // Otherwise, we found some other character and just ignore it.

      next_char = '\n';
    }

    // This implements a state machine.  It looks a bit gnarly, but is
    // pretty fast.
    switch (state) {
      case FIELD_START:
        DCHECK(current_field.empty());
        if (mode_ != LITERAL_QUOTES && next_char == '"') {
          state = IN_QUOTED_FIELD;
        } else if (next_char == delim_) {
          record_.fields_.push_back(current_field);
          current_field.clear();
          state = FIELD_START;
        } else if (next_char == '\n') {
          record_.fields_.push_back(current_field);
          state = END_OF_RECORD;
        } else {
          current_field.push_back(next_char);
          state = IN_FIELD;
        }
        break;

      case IN_FIELD:
        if (mode_ != LITERAL_QUOTES && next_char == '"') {
          state = IN_QUOTED_FIELD;
        } else if (next_char == delim_) {
          record_.fields_.push_back(current_field);
          current_field.clear();
          state = FIELD_START;
        } else if (next_char == '\n') {
          record_.fields_.push_back(current_field);
          state = END_OF_RECORD;
        } else {
          current_field.push_back(next_char);
        }
        break;

      case IN_QUOTED_FIELD:
        DCHECK_NE(LITERAL_QUOTES, mode_)
            << "Got into a quotes state while ignoring quotes.";
        if (next_char == '"') {
          state = QUOTED_QUOTE;
        } else if (next_char == '\\' && mode_ == MYSQL_ESCAPING) {
          state = QUOTED_BACKSLASH_ESCAPE;
        } else {
          current_field.push_back(next_char);
        }
        break;

      case QUOTED_BACKSLASH_ESCAPE:
        DCHECK_EQ(MYSQL_ESCAPING, mode_);
        current_field.push_back(next_char);
        state = IN_QUOTED_FIELD;
        break;

      case QUOTED_QUOTE:
        DCHECK_NE(LITERAL_QUOTES, mode_)
            << "Got into a quotes state while ignoring quotes.";
        if (next_char == '"' && mode_ != MYSQL_ESCAPING) {
          current_field.push_back('"');
          state = IN_QUOTED_FIELD;
        } else if (next_char == delim_) {
          record_.fields_.push_back(current_field);
          current_field.clear();
          state = FIELD_START;
        } else if (next_char == '\n') {
          record_.fields_.push_back(current_field);
          state = END_OF_RECORD;
        } else {
          current_field.push_back(next_char);
          state = IN_FIELD;
        }
        break;

      case ERROR:
      case END_OF_RECORD:
        break;  // We won't ever hit this, but it is here for completeness.
    }

    if (state == ERROR) {
      // In the event of an error, we read to the end of the line (or EOF).
      while (!(next_char == '\n' || next_char == '\r')) {
        if (current_str_.empty() && !PopulateCurrentString()) break;

        next_char = current_str_[0];
        current_str_.remove_prefix(1);
      }
    }
  }
}

// Get the next StringPiece from the source, and put it in current_str_.
// Returns false on EOF, true otherwise.
bool Parser::PopulateCurrentString() {
  // Fetch the next StringPiece from the Source.
  source_->Skip(orig_str_size_);
  size_t avail = source_->Available();
  if (avail == 0) {
    // Nothing is available, so we return EOF.
    orig_str_size_ = 0;
    return false;
  }

  // The byte source may give us a StringPiece that is longer than it is
  // valid,  so we trim off the excess.
  current_str_ = source_->Peek();
  if (avail < current_str_.size())
    current_str_.remove_suffix(current_str_.size() - avail);
  orig_str_size_ = current_str_.size();

  return true;
}

Record ParseLine(absl::string_view line) { return ParseLine(line, ','); }

Record ParseLine(absl::string_view line, const char delim, Parser::Mode mode) {
  // In the special case that the given line is empty, this is equivalent
  // to the string "\n"
  if (line.empty()) line = "\n";

  Parser parser(new strings::ArrayByteSource(line), delim, mode);
  return *parser.begin();
}

}  // namespace csv
}  // namespace util
