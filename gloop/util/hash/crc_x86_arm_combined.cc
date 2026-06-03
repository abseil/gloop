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

// Hardware accelerated CRC32 computation on Intel architecture.

#include <stddef.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/call_once.h"  // IWYU pragma: keep
#include "absl/base/nullability.h"
#include "absl/crc/crc32c.h"
#include "absl/numeric/int128.h"
#include "absl/strings/string_view.h"
#include "gloop/util/hash/crc.h"
#include "gloop/util/hash/crc_internal.h"

namespace crc_internal {

// Implementation details not exported outside of file
namespace {

// Some machines have CRC acceleration hardware.
// We can do a faster version of Extend() on such machines.
class CRC32AcceleratedX86ARMCombined : public CRC32 {
 public:
  CRC32AcceleratedX86ARMCombined() { this->is_default_ = false; }
  ~CRC32AcceleratedX86ARMCombined() override {}
  void Extend(uint64_t* lo, uint64_t* absl_nullable hi, const void* bytes,
              int64_t length) const override;
  void ExtendByZeroes(uint64_t* lo, uint64_t* hi,
                      int64_t length) const override;

 private:
  CRC32AcceleratedX86ARMCombined(const CRC32AcceleratedX86ARMCombined&) =
      delete;
  CRC32AcceleratedX86ARMCombined& operator=(
      const CRC32AcceleratedX86ARMCombined&) = delete;
};

// Lookup the crc32c poly in standard_poly_list[].
void LookupCRC32CPoly(const CRC::Poly** crc32c_poly) {
  *crc32c_poly = LookupStandardPolyByName(CRC::CRC_32C);
}

void CRC32AcceleratedX86ARMCombined::ExtendByZeroes(uint64_t* lo, uint64_t* hi,
                                                    int64_t length) const {
  if (length <= 0) return;
  constexpr uint32_t kCrc32Xor = 0xffffffffU;
  absl::crc32c_t crc = static_cast<absl::crc32c_t>(*lo ^ kCrc32Xor);
  *lo = static_cast<uint32_t>(absl::ExtendCrc32cByZeroes(crc, length)) ^
        kCrc32Xor;
}

ABSL_ATTRIBUTE_HOT
void CRC32AcceleratedX86ARMCombined::Extend(uint64_t* lo,
                                            uint64_t* absl_nullable hi,
                                            const void* bytes,
                                            int64_t length) const {
  if (length <= 0) return;
  constexpr uint32_t kCrc32Xor = 0xffffffffU;
  absl::crc32c_t crc = static_cast<absl::crc32c_t>(*lo ^ kCrc32Xor);
  *lo = static_cast<uint32_t>(absl::ExtendCrc32c(
            crc, absl::string_view(static_cast<const char*>(bytes), length))) ^
        kCrc32Xor;
}

}  // namespace

CRCImpl* absl_nullable TryNewCRC32AcceleratedX86ARMCombined(uint64_t lo,
                                                            uint64_t hi,
                                                            int degree) {
  static const CRC::Poly* crc32c_poly;
  static absl::once_flag once;
  absl::call_once(once, &LookupCRC32CPoly, &crc32c_poly);
  if (crc32c_poly != nullptr && crc32c_poly->lo == lo &&
      crc32c_poly->hi == hi && crc32c_poly->degree == degree) {
    return new CRC32AcceleratedX86ARMCombined();
  } else {
    return nullptr;
  }
}

std::vector<std::unique_ptr<CRCImpl>> NewCRC32AcceleratedX86ARMCombinedAll() {
  auto ret = std::vector<std::unique_ptr<CRCImpl>>();
  ret.push_back(std::make_unique<CRC32AcceleratedX86ARMCombined>());

  // Only initialize parts that are needed for testing.
  for (auto& impl : ret) {
    impl->poly_ = absl::MakeUint128(0ull, 2197175160ull);
  }
  return ret;
}

}  // end namespace crc_internal
