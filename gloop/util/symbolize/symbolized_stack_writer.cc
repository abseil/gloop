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

#include "gloop/util/symbolize/symbolized_stack_writer.h"

#include <stddef.h>
#include <stdint.h>

#include <charconv>
#include <memory>
#include <optional>
#include <string>
#include <system_error>  // NOLINT(build/c++11)  // b/283050693
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "gloop/thread/thread.h"
#include "gloop/util/symbolize/symbolize.h"

namespace util {
class SymbolizedStackWriterImpl : public ThreadStackWriter {
 public:
  explicit SymbolizedStackWriterImpl(
      const SymbolMap& symbol_map, std::string* output,
      std::optional<absl::AnyInvocable<void(absl::string_view)>>&&
          per_thread_cb)
      : symbol_map_(symbol_map),
        symbolize_creator_thread_(false),
        output_(output),
        prev_thread_start_(0),
        per_thread_cb_(std::move(per_thread_cb)) {
    if (symbol_map_.binary_is_stripped()) {
      output_->append("Stripped binary. No symbols available.\n");
    }
  }

  SymbolizedStackWriterImpl(const SymbolizedStackWriterImpl&) = delete;
  SymbolizedStackWriterImpl& operator=(const SymbolizedStackWriterImpl&) =
      delete;

  void set_symbolize_creator_thread(bool symbolize_creator_thread) {
    symbolize_creator_thread_ = symbolize_creator_thread;
  }

  void Write(const char* buf, int len) override {
    Write(buf, len, ThreadStackWriterOptions{.symbolize = true});
  }

  void Write(const char* buf, int len,
             const ThreadStackWriterOptions& options) override {
    static constexpr absl::string_view kCreator = "creator:";
    bool symbolize_addresses = options.symbolize;
    size_t cur_thread_start = 0;

    // Initializing this isn't necessary with the intended invocations of this
    // function (once per thread stack).
    absl::string_view thread_type_label;
    const char* const end = buf + len;
    for (const char* cur = buf; cur < end;) {
      UpdateThreadTypeLabel(thread_type_label,
                            absl::string_view(cur, end - cur));
      const char* const before_skip = cur;
      // Skip spaces.
      while (cur < end && *cur == ' ') {
        ++cur;
      }
      // 1) Handle an address.  Addresses must start with "0x".
      if (symbolize_addresses &&
          absl::StartsWith(absl::string_view(cur, end - cur), "0x")) {
        uint64_t pos;
        const std::from_chars_result result =
            std::from_chars(cur + 2, end, pos, 16);
        if (result.ec == std::errc()) {  // Conversion is done successfully.
          const std::string name =
              symbol_map_.GetDemangledSymbolAtPosition(pos);
          absl::StrAppendFormat(output_, "  0x%08x: %s\n", pos,
                                name.empty() ? absl::string_view("(unknown)")
                                             : absl::string_view(name));
          cur = result.ptr;  // Skip the address.
          // Ignore LFs in stack traces.
          while (cur < end && *cur == '\n') {
            ++cur;
          }
          continue;
        }
      }
      // 2) Append spaces.
      output_->append(before_skip, cur);
      // 3) Handle creator thread.
      if (absl::StartsWith(absl::string_view(cur, end - cur), kCreator)) {
        if (symbolize_creator_thread_) {
          cur += kCreator.size();
          output_->append(kCreator.data(), kCreator.size());
          *output_ += '\n';
          continue;
        }
        symbolize_addresses = false;
      }
      // 4) Append a single character if it hasn't reached the end.
      if (cur < end) {
        if (absl::StartsWith(absl::string_view(cur, end - cur), "---\n")) {
          // End of thread header.
          cur_thread_start = output_->size() + 4;
        }
        *output_ += *cur++;
      }
    }

    if (cur_thread_start > 0) {
      const absl::string_view cur_thread_bt =
          absl::string_view(*output_).substr(cur_thread_start);
      bool same_as_last_thread = false;
      if (per_thread_cb_.has_value()) {
        // When the callback is set, we record ONLY one thread's stack
        // per callback.
        if (last_stack_bt_ == cur_thread_bt) {
          same_as_last_thread = true;
        } else {
          last_stack_bt_.assign(cur_thread_bt.data(), cur_thread_bt.size());
        }
      } else if (!cur_thread_bt.empty() &&
                 absl::StartsWith(
                     absl::string_view(*output_).substr(prev_thread_start_),
                     cur_thread_bt)) {
        same_as_last_thread = true;
      }
      if (same_as_last_thread) {
        // Don't bother printing two identical backtraces in a row
        // (which often happens if we have a pool of idle threads).
        // It is easier to read if we just abbreviate the dump:
        output_->erase(cur_thread_start);
        output_->append("  [same as previous ");
        output_->append(thread_type_label);
        output_->append("]\n");
      } else {
        prev_thread_start_ = cur_thread_start;
      }
    } else {
      prev_thread_start_ = cur_thread_start;
    }
    if (per_thread_cb_.has_value()) {
      (*per_thread_cb_)(*output_);
      output_->clear();
    }
  }

 private:
  static void UpdateThreadTypeLabel(absl::string_view& label,
                                    absl::string_view header) {
    if (absl::StartsWith(header, "--- Thread ")) {
      label = "thread";
    } else if (absl::StartsWith(header, "--- Inactive CoThread ")) {
      // See <link> and <link>.h.
      label = "CoThread";
    }
  }

  const SymbolMap& symbol_map_;
  bool symbolize_creator_thread_;
  std::string* output_;
  size_t prev_thread_start_;
  std::optional<absl::AnyInvocable<void(absl::string_view)>> per_thread_cb_;
  std::string last_stack_bt_;
};

SymbolizedStackWriter::SymbolizedStackWriter(std::string* output)
    : impl_(std::make_unique<SymbolizedStackWriterImpl>(
          SymbolMap::GetCached(), output, std::nullopt)) {}

SymbolizedStackWriter::SymbolizedStackWriter(const SymbolMap& symbol_map,
                                             std::string* output)
    : impl_(std::make_unique<SymbolizedStackWriterImpl>(symbol_map, output,
                                                        std::nullopt)) {}

SymbolizedStackWriter::SymbolizedStackWriter(
    absl::AnyInvocable<void(absl::string_view)>&& per_thread_cb)
    : impl_(std::make_unique<SymbolizedStackWriterImpl>(
          SymbolMap::GetCached(), &thread_output_, std::move(per_thread_cb))) {}

SymbolizedStackWriter::SymbolizedStackWriter(
    const SymbolMap& symbol_map,
    absl::AnyInvocable<void(absl::string_view)>&& per_thread_cb)
    : impl_(std::make_unique<SymbolizedStackWriterImpl>(
          symbol_map, &thread_output_, std::move(per_thread_cb))) {}

SymbolizedStackWriter::~SymbolizedStackWriter() = default;

void SymbolizedStackWriter::Write(const char* buf, int len) {
  impl_->Write(buf, len);
}

void SymbolizedStackWriter::Write(const char* buf, int len,
                                  const ThreadStackWriterOptions& options) {
  impl_->Write(buf, len, options);
}

void SymbolizedStackWriter::set_symbolize_creator_thread(
    bool symbolize_creator_thread) {
  impl_->set_symbolize_creator_thread(symbolize_creator_thread);
}
}  // namespace util
