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

// Implementation of the shrunk-array.h interface.

//////////////
// Data layout

// For the experiments that led up to this design, see
// experimental/users/amc/instant/.
//
// This implementation shrinks the input array by rearranging the data
// into three sections using two levels of indirection.  There is an
// index section, which refers to a main section, which refers to a
// lexicon section.
//
// The index section starts at a base offset (recorded in the
// decode_key) and grows forward.  The lexicon section starts at the
// base offset and grows backward.  The main section starts at the end
// of the index section and grows forward.
//
//     lexicon_section <base_offset> index_section main_section
//
// The main section is a sequence of bit-fields, one field for each
// position in the original raw array, in the same order.  The lexicon
// section contains four lexicons numbered 0 to 3, any of which can be
// empty, each of which is a sequence of entries (which are bit-fields).
// The index section is a sequence of block descriptors, one for each
// 128-position block of the input array.  Here is a slightly expanded
// view of the whole shrunk array (asterisk means zero-or-more):
//
//     lex3 lex2 lex1 lex0 <base_offset> block_descriptor* main_field*
//
// The base offset, and the offsets in the block descriptors (see
// below), are measured in bits from the start of the shrunk array.
// These offsets are currently 51-bit unsigned numbers, but if this
// becomes too limiting, things could be rearranged to allow 64-bit
// offsets, at a cost of 1/2 bit per input position.
//
// The bit at offset n is defined to be bit n%64 of shrunk_array[n>>6],
// where bit 0 means the least significant bit.  This definition makes
// the native byte-order irrelevant.

// Block descriptor:
//
// Neither the input array nor the main section is block-oriented; both
// are sequences without natural boundaries.  Only the index section is
// block-oriented.  Each block descriptor describes a conceptual block
// of 128 fields of the main section (which correspond to a conceptual
// block of 128 positions of the input array).  A block descriptor is
// composed of five uint64_t elements:

// element 0: profile tag map for the first quarter of the block
// element 1: profile tag map for the second quarter of the block
// element 2: bits 13..63: offset of the start of the second quarter
//                         of the block in the main section
//            bits  0..12: size (in bits) of the second and third
//                         quarters of the block in the main section
// element 3: profile tag map for the third quarter of the block
// element 4: profile tag map for the fourth quarter of the block

// The number of input positions is typically not a multiple of 128, and
// therefore the last block descriptor typically contains some unused
// tags.  Tags in element 4 corresponding to non-existent positions are
// used for the main section, which is allowed to start at an unaligned
// offset.  For example, if the number of positions is 125, then the
// last 6 bits of element 4 will not be needed for tags, and the main
// section can start 6 bits before the end of the block descriptor.  If
// the tags in element 3 are all unused, main section begins immediately
// after element 2.

// Profile tag map:
//
// Each profile tag map contains 32 two-bit tags, one for each position
// in the quarter-block.  Each tag indicates which of four profiles
// (numbered 0 to 3) is used to encode that position.

// Profile:
//
// A profile consists of a main field width and a lexicon entry width.
// If a position is encoded using profile i, whose main width is M and
// whose lexicon width is L, that means the position occupies an M-bit
// field in the main section, which is an index to an L-bit entry in
// lexicon i, which contains the original input value for the position.
// Exception:  If the lexicon width is zero, then the main field is a
// literal value, not a lexicon index.

// Example:
//
// Supposed we want to look up the value at position 221.  The index
// section looks like:

//     block descriptor for positions 0..127
//     block descriptor for positions 128..255
//     block descriptor for positions 256..383
//     ...

// Zoom in on that second block descriptor:

//     profile tag map for positions 128..159
//     profile tag map for positions 160..191
//     offset & size
//     profile tag map for positions 192..223
//     profile tag map for positions 224..255

// The offset tells us where position 160 starts in the main section.
// The size tells us how many bits are used by positions 160..223 in the
// main section.  Therefore offset + size is the start of position 224
// in the main section.  We're looking for position 221, so we need to
// subtract the widths of positions 223, 222, and 221.
//
// Zoom in on the third profile tag map (for positions 192..223):

//     most significant                              least significant
//     00 10 01 01 11 10 00 00 11 ... 11 10 00 11 10 01 01 00 01 11 10
//     ^^position 223                                   position 192^^

// Position 221 uses profile 1.  Suppose our table of profile widths is:

//     main width 0 = 5   lexicon width 0 = 22
//     main width 1 = 8   lexicon width 1 = 19
//     main width 2 = 20  lexicon width 2 = 0
//     main width 3 = 9   lexicon width 3 = 11
//                        lexicon width 4 = 14 |
//                        lexicon width 5 = 18 | ignore 4..6 for now
//                        lexicon width 6 = 23 |

// So position 221 has an 8-bit field in the main section, which points
// to a 19-bit entry in lexicon 1.  Where does the main field start?
// Positions 223, 222, and 221 use profiles 0, 2, and 1 respectively,
// so in the main section they use 5, 20, and 8 bits, respectively,
// which is 33 bits total, so we can subtract 33 bits from the offset
// for position 224 (found above) to get the offset for position 221,
// and read the 8 bits starting there to get the value of the main field
// for position 221.  Let's say that value is 105.  If the lexicon width
// were zero, we would be done, and 105 would be the final value.  But
// the lexicon width is 19, not zero, so 105 is an index into lexicon 1.
// This example will be continued below.

// Lexicons
//
// Lexicons 0, 1, and 2 contain as many entries as possible, given
// the corresponding main field width, which allows the start of each
// lexicon to be calculated given only the base offset and the profile
// widths.  The last lexicon (lexicon 3) is free to have fewer entries
// than profile 3's main width would allow.  Another unique feature of
// the last lexicon is that it is subdivided into four sub-lexicons with
// four separate widths, either all zero or all nonzero.  So although
// there are four main widths numbered 0 to 3 (like the profiles they
// belong to), there are seven lexicon widths numbered 0 to 6, with
// lexicon widths 3 to 6 all being used by lexicon 3.  In particular,
// lexicon width (3 + j%4) is used for entry j of lexicon 3.  The
// intention is that the last profile be used for the largest lexicon,
// where its additional degrees of freedom (four widths and arbitrary
// size versus one width and power-of-two size) can have the most
// impact.

// Example (continued):
//
// Profile 0 has a main width of 5 and a lexicon width of 22, therefore
// lexicon 0 is 32 * 22 = 704 bits, implying that lexicon 1 starts
// 704 bits below the base offset and grows downward.  Every entry of
// lexicon 1 is 19 bits wide, and we need entry 105, so we read 19 bits
// starting 704 + 105*19 = 2699 bits below the base offset, and the
// result is the value of position 221.
//
// Now suppose position 221 had used profile 3 rather than profile 1, so
// that it was entry 105 of lexicon 3 that we were looking for.  Zoom in
// on lexicon 3:

//     entry 6  entry 5  entry 4  entry 3  entry 2  entry 1  entry 0
// ... 18 bits  14 bits  11 bits  23 bits  18 bits  14 bits  11 bits

// The entries of lexicon 3 cycle through four widths (lexicon widths
// 3..6), and each cycle contains 11 + 14 + 18 + 23 = 66 bits.  So
// entries 0..103 contain 104/4 * 66 = 1716 bits.  Entries 104 and 105
// contain 11 + 14 = 25 bits.  Lexicons 0, 1, and 2 contain 32*22 +
// 256*19 + 1048576*0 = 5568 bits.  So we read 14 bits starting 5568 +
// 1716 + 25 = 7309 bits below the base offset.

// Omitted sections:
//
// The index section can be omitted (to speed decoding or to improve
// compression for data that doesn't benefit from it), in which case the
// last profile (profile 3) is used for all positions.  When the index
// section is present, the base offset must be a multiple of 64, so that
// the block descriptors are aligned with the array elements.
//
// The lexicon section can be omitted simply by forcing all the lexicon
// widths to zero.

// Decode key:
//
// The decode key contains the base offset, the four main widths,
// and the seven lexicon widths, in an array of two 64-bit unsigned
// integers, as follows:

// decode_key[0]: bits  0..6:  main width 3
//                bits  7..12: lexicon width 6 (minus one)
//                bits 13..63: base offset
//
// decode_key[1]: bits  0..6:  main width 0
//                bits  7..13: lexicon width 0
//                bits 14..20: main width 1
//                bits 21..27: lexicon width 1
//                bits 28..34: main width 2
//                bits 35..41: lexicon width 2
//                bits 42..48: lexicon width 3
//                bits 49..54: lexicon width 4 (minus one)
//                bits 55..60: lexicon width 5 (minus one)
//                bits 61..62: reserved (must be zero)
//                bit  63:     index_section_present flag

// Main widths and lexicon widths both range from 0 to 64 inclusive.  To
// help enforce the requirement that the four widths of lexicon 3 are
// all zero or all nonzero, and to avoid having a width straddle the two
// integers, and to leave a couple bits reserved for future extension,
// lexicon widths 4, 5, and 6, if they are nonzero, are decremented
// before being stored, so that they fit in 6 bits each.  Lexicon width
// 3 still occupies 7 bits so the decoder can distinguish the all-zero
// case from the all-one case.  The fields are ordered so that when the
// caller chooses complexity 1, the only two nonzero fields (base offset
// and main width 3) are both in decode_key[0], so the caller need not
// preserve decode_key[1], which is guaranteed to be zero.

// Complexity parameter:
//
// The decoder performs at most one memory fetch from each section
// (index, main, lexicon).  The index and lexicon sections are optional,
// and the complexity parameter bounds the number of fetches by
// eliminating one or both of those sections.  At complexity 3, both are
// allowed; at complexity 1, neither is allowed; at complexity 2, either
// is allowed, but not both.

#include "gloop/util/coding/shrunk-array.h"

#include <stddef.h>  // for ptrdiff_t, size_t
#include <stdint.h>  // for uintptr_t
#include <string.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <type_traits>
#include <vector>

#include "absl/base/casts.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/numeric/bits.h"
#include "absl/types/span.h"
#include "gloop/base/port.h"
#include "gloop/util/bits/bits.h"
#include "gloop/util/endian/endian.h"

// The use of uintptr_t is conditioned on a #if, which confuses IWYU, so
// we mention it here unconditionally.
using ::uintptr_t;

namespace {

// The following are constants, not parameters.  Things might break
// if they were changed.  The purpose is to make the code more
// understandable, not configurable.

static const int kPositionInTagMapBits = 5,
                 kPositionsPerTagMap = 1 << kPositionInTagMapBits,
                 kPositionInTagMapMask = kPositionsPerTagMap - 1,
                 kPositionsPerHalfBlock = kPositionsPerTagMap * 2,
                 kPositionsPerBlock = kPositionsPerTagMap * 4,
                 kPositionInBlockMask = kPositionsPerBlock - 1,
                 kHalfBlockSizeMax = 1 << (kPositionInTagMapBits + 1 + 6),
                 kHalfBlockSizeBits = kPositionInTagMapBits + 1 + 6 + 1,
                 kProfileBits = 64 / kPositionsPerTagMap,
                 kNumProfiles = 1 << kProfileBits,
                 kProfileMask = kNumProfiles - 1, kSublexBits = 2,
                 kNumSublexicons = 1 << kSublexBits,
                 kSublexMask = kNumSublexicons - 1,
                 kNumLexWidths = kNumProfiles + kNumSublexicons - 1;

static const int64_t kint64one = 1;
static const uint64_t kuint64one = 1,
                      kOffsetMax = std::numeric_limits<uint64_t>::max() >>
                                   kHalfBlockSizeBits;

// Writer<raw_type> implements ShrunkArray::Write().
template <typename raw_type>
class Writer {
 public:
  static_assert(std::is_unsigned_v<raw_type>, "raw_type must be unsigned");

  Writer(absl::Span<const raw_type> input, int complexity,
         uint64_t decode_key[2], std::vector<uint64_t>* shrunk_vector);

  // This type is neither copyable nor movable.
  Writer(const Writer&) = delete;
  Writer& operator=(const Writer&) = delete;

  ~Writer();
  void DoIt();

 private:
  //////////////////
  // Data structures

  // A ValueFreq is a raw value and the number of times it occurs.
  struct ValueFreq {
    raw_type value;
    int64_t freq;
  };

  // A map from values to their frequencies.
  using ValueFreqMap = absl::flat_hash_map<raw_type, int64_t>;

  // An array of ValueInfo is intended to parallel a sorted array of
  // ValueFreq, and contains cumulative information about the values.
  // The ValueInfo array contains one more element than the ValueFreq
  // array.

  struct ValueInfo {
    // Positions occupied by all prior values, not including this value.
    int64_t positions_prior;

    // The maximum of all prior values, not including this value.
    // Zero for the first element.
    raw_type value_max_prior;

    // Bits needed to represent value_max_prior.
    // Zero for the first element.
    int width_max_prior;

    // Index into values_remaining_at_rank_of_width_[][] (see
    // below), or negative if not needed for this element.
    int values_remaining_rank;
  };

  // When a set of positions is encoded using only direct (non-lexicon)
  // profiles, each profile has a main field width, and is used to
  // encode all values that fit in its own width but do not fit in the
  // width of the next narrower profile.  A two-dimensional array of
  // DirectSplit tells the optimal field widths for the profiles.  Let
  // the profiles be numbered sequentially from 0 in order of increasing
  // field width.  If there are p+1 profiles being used to encode values
  // whose maximum width is w, then obviously the width of profile p is
  // w. table[p][w].bits is the total number of bits needed by all the
  // profiles, and w1 = table[p][w].next_width is the width of profile
  // p-1.  You can then consult table[p-1][w1] to get the next narrower
  // width, and so on, down to table[1].  In table[0] bits is valid, but
  // next_width is undefined.

  struct DirectSplit {
    int64_t bits;    // Total bits used by this and all narrower profiles.
    int next_width;  // Width of next narrower profile.
  };

  // A map from raw values to lexicon indices.  The least significant
  // kProfileBits bits of a lexicon index are a profile number, and the
  // other bits are an entry number within that profile's lexicon.

  using ValueLexMap = absl::flat_hash_map<raw_type, uint64_t>;

  //////////////
  // Subroutines

  // InitStats() initializes some or all of the stats (see below),
  // depending on complexity_.  Requires in_.size() > 0.

  void InitStats();

  // ChoseParams() modifies the search state (see below) as it chooses
  // the encoding parameters.  Requires that InitStats() has already
  // been called.

  void ChooseParams();

  // EncodeArray() appends the shrunk representation of the input array
  // to the output vector, using the encoding parameters chosen by
  // ChoseParams(), which must have already been called.

  void EncodeArray();

  // Most of EncodeArray() is divided into more manageable pieces.
  // InitEncodingState() populates direct_{begin,end,last}_,
  // which are needed by the others.  EncodeLexicons() populates
  // lexicon_index_for_value_, which are needed by EncodeIndex() and
  // EncodeMain().

  void InitEncodingState();
  void EncodeLexicons();
  void EncodeIndex();
  void EncodeMain();

  // EncodeValue(value, profile, main_field) determines the profile
  // to use for the given value, and the main field to be written
  // (either a lexicon index or the value itself), and writes them into
  // *profile and *main_field.  It does not touch out_.  Requires that
  // InitEncodingState() and EncodeLexicons() have already been called.
  // If main_field is NULL, it is not written.

  void EncodeValue(raw_type value, int* profile, uint64_t* main_field);
  // Reads value in position pos and encodes it into profile and main_field.
  void EncodeValueFromPos(size_t pos, int* profile, uint64_t* main_field);

  // MakeDecodeKey() writes into decode_key_[0..2] the encoding
  // parameters chosen by ChoseParams(), which must have already been
  // called.

  void MakeDecodeKey();

  // MoreFrequentThenSmaller(p1, p2) takes two ValueFreqs and returns true
  // if the first is more frequent than the second, or in case of a tie,
  // if the first value is less than the second.

  static bool MoreFrequentThenSmaller(const ValueFreq& p1, const ValueFreq& p2);

  // AppendBitField(data, width) appends the data (which may be nonzero
  // only in the bottom width bits) to the output vector out_, as a bit
  // field of the given width.  The total number of bits appended by
  // DoIt() must be a multiple of 64.  The width must be in the range
  // 0..64.

  void AppendBitField(uint64_t data, int width);

  // BestDirectSplit(positions_of_width, p, w) consults
  // direct_split_table_[0..p][width_min_..w] and
  // positions_of_width[width_min_..w] and returns the optimal split
  // between profile p+1 (of width w) and profile p; that is, it returns
  // the split to be stored in table[p+1][w].  See also DirectSplit
  // above.  Requires p >= 0 and w >= width_min_.

  DirectSplit BestDirectSplit(const int64_t positions_of_width[65], int p,
                              int w);

  // FillDirectSplitTable(positions_of_width, p_max) fills in
  // direct_split_table_[0..p_max][width_min_..width_max_] based on
  // positions_of_width[width_min_..width_max_].  Requires p_max >= 0.

  void FillDirectSplitTable(const int64_t positions_of_width[65], int p_max);

  // LastLexSize(values_lex, values_lex_of_width, &width_lex)
  // returns the number of lexicon bits required for the last
  // lexicon (the one with more than one entry width) to hold
  // the values in values_lex_of_width[width_min_..width_max_]
  // (which is like values_of_width_[] (see above) but counts
  // only the values to be included in the lexicon), and
  // overwrites width_lex[0 .. kNumSublexicons-1] with the lexicon
  // entry widths.  Requires that values_lex == the sum of
  // values_lex_of_width[width_min_..width_max_].

  int64_t LastLexSize(int64_t values_lex, const int64_t values_lex_of_width[65],
                      int width_lex[kNumSublexicons]);

  // FindBestLexicons(profiles_remaining, width_start, values_covered,
  // bits_to_beat, &bits_main, &bits_lex, &width_main, &width_lex)
  // explores the parameter space recursively.
  //
  // Inputs:
  //
  // values_covered:  The first values_covered values in
  // sorted_values_repeated_ have already been covered by simple
  // (non-last) lexicons.
  //
  // profiles_remaining:  The remaining repeated values, and all
  // the non-repeated values, are to be covered by the remaining
  // profiles_remaining profiles, of which at least one profile (the
  // last) will be indirect (will use a lexicon).
  //
  // width_start:  All remaining indirect profiles will have a main
  // width of at least width_start.
  //
  // bits_to_beat:  The remaining profiles must use strictly fewer than
  // bits_to_beat bits for their main bits plus lexicon bits.  Solutions
  // using bits_to_beat bits or more would not beat the current best
  // known parameters, and will not be pursued.
  //
  // Outputs:
  //
  // Returns true iff a solution is found.  If a solution is found, the
  // following output parameters will be filled in, otherwise they will
  // be untouched.
  //
  // bits_main:  The number of main bits use by the profiles_remaining
  // profiles.
  //
  // bits_lex:  The number of lexicon bits used by the
  // profiles_remaining profiles.
  //
  // width_main[0 .. profiles_remaining-1]:  The main field widths of
  // the profiles_remaining profiles.
  //
  // width_lex[0 .. profiles_remaining-1+kNumSublexicons-1]:  The
  // lexicon entry widths of the profiles_remaining profiles.  The last
  // profile occupies the last kNumSublexicons of those array elements.
  //
  // The last profile is always indirect (uses a lexicon).  Among the
  // other profiles, the indirect profiles (if any) come first, in
  // increasing order of main width, followed by the direct profiles (if
  // any), in increasing order of main width.
  //
  // Preconditions:
  //
  // values_covered <= values_repeated_
  //
  // If profiles_remaining > 1, direct_split_table_ must have been
  // filled using values_nonrepeated_of_width_.
  //
  // Heuristics:
  //
  // This function does not find the true absolute best set of lexicons,
  // because the search space is exponential.  It drastically narrows
  // the search space using the following heuristics, which have been
  // found to save much time without sacrificing much space, for the
  // original test data.
  //
  // 1. Lexicons are filled in a certain order:  The most frequent
  // values (breaking ties in favor of smaller values) go into the
  // smallest lexicon, and so on.
  //
  // 2. All values that appear more than once go in lexicons, unless
  // there are no lexicons.
  //
  // 3. Only the last lexicon may contain values that appear only once,
  // and it takes them in decreasing order.

  bool FindBestLexicons(int profiles_remaining, int width_start,
                        int64_t values_covered, int64_t bits_to_beat,
                        int64_t* bits_main, int64_t* bits_lex, int width_main[],
                        int width_lex[]);

  // Returns number of positions in input array.
  int64_t positions() const { return in_.size(); }

  template <typename Func>
  void ForEachValueFreq(const Func& func);

  //////////
  // Inputs

  const absl::Span<const raw_type> in_;
  const int complexity_;
  uint64_t* const decode_key_;
  std::vector<uint64_t>* const out_;

  ////////
  // Stats
  //
  // Set only by InitStats().  Some are destroyed by DoIt() after they
  // are no longer needed.

  // Number of positions that would be covered by the index if there
  // were an index.  Sometimes needs to be greater than positions().
  int64_t positions_indexed_;

  // Widths of smallest and largest values.
  int width_min_;
  int width_max_;

  // Number of widths in range [min..max].
  int width_span_;

  // Number of distinct values.
  int64_t values_;

  // For each width 0..64, how many distinct values of that width
  // appear, and how many positions they occupy.
  int64_t values_of_width_[65];
  int64_t positions_of_width_[65];

  // The frequency of each value (how many times it occurs):
  ValueFreqMap freq_of_value_map_;
  std::vector<int64_t> freq_of_value_vec_;

  // How many distinct values appear more than once.
  int64_t values_repeated_;

  // For each width 0..64, how many values of that width appear exactly
  // once.
  int64_t values_nonrepeated_of_width_[65];

  // Distinct values that appear more than once, sorted by decreasing
  // frequency, then by increasing value.
  std::unique_ptr<ValueFreq[]> sorted_values_repeated_;

  // Additional information about the values in sorted_values_repeated_
  // (see ValueInfo above). values_remaining_at_rank_of_width_[
  // value_info_[i].values_remaining_rank ][w] is the number of
  // repeated values of width w, excluding all values at indices < i of
  // sorted_values_repeated_. value_info_[i].values_remaining_rank is
  // valid (nonnegative) only where it will be useful; that is, where i
  // is the sum of kNumProfiles-1 or fewer powers of two; elsewhere it
  // is invalid (negative).

  std::unique_ptr<ValueInfo[]> value_info_;
  std::unique_ptr<int64_t[][65]> values_remaining_at_rank_of_width_;

  ///////////////
  // Search state
  //
  // Modified only by ChooseParams().

  int64_t best_bits_index_;            // Size of index section in bits.
  int64_t best_bits_main_;             // Size of main section in bits.
  int64_t best_bits_lex_;              // Size of lexicon section in bits.
  int best_width_main_[kNumProfiles];  // Main field width of each profile,
                                       // -1 for unused profiles.
  int best_width_lex_[kNumLexWidths];  // Lexicon entry widths of each profile,
                                       // -1 for unused and direct profiles.
                                       // The last profile occupies the last
                                       // kNumSublexicons array elements.
  // If best_bits_index_ is zero, then only the last profile is used,
  // and other profiles' widths (both main and lex) are zero.

  // Table of optimal encoding parameters using only direct profiles (no
  // lexicons).  Computed and recomputed by FillDirectSplitTable().  See
  // also DirectSplit above.

  DirectSplit direct_split_table_[kNumProfiles - 1][65];

  /////////////////
  // Encoding state
  //
  // Modified only by EncodeArray() and its subroutines.

  // Base offset, see Data Layout above.
  uint64_t offset_base_;

  // The least significant bits_buffered_ bits of bit_buffer_ are
  // waiting to be appended to out_.  The other bits of bit_buffer_ are
  // zero.  The buffer is flushed whenever it fills, so bits_buffered_
  // is always in the range 0..63.

  int bits_buffered_;
  uint64_t bit_buffer_;

  // The range of direct profiles is [direct_begin_, direct_end_).
  // Recall that FindBestLexicons() guarantees that the direct profiles
  // will be a contiguous range.

  int direct_begin_;
  int direct_end_;
  int direct_last_;  // direct_end_ - 1

  // For p in [direct_begin_, direct_end_), direct_value_max_[p] is the
  // maximum value that fits in best_width_main_[p].

  raw_type direct_value_max_[kNumProfiles];

  // For each value that appears in a lexicon, an index to the lexicon
  // entry.

  ValueLexMap lexicon_index_for_value_;
  // For every position p, contains lexicon_index_for_value_[in_[p]].
  // This is purely for optimization purposes not to query flat_hash_map
  // twice for every position (during EncodeMain and EncodeIndex).
  std::vector<uint64_t> vec_lexicon_index_for_value_;
};

// Helper to read a uint64_t.  It comes two flavors depending on
// alignment.  The aligned flavor takes a uint64_t pointer, whereas
// the unaligned one takes a char pointer.

template <typename DataType>
inline uint64_t ReadUint64(const DataType* address);

template <>
inline uint64_t ReadUint64<uint64_t>(const uint64_t* address) {
  return *address;
}

template <>
inline uint64_t ReadUint64<char>(const char* address) {
  return UNALIGNED_LOAD64(address);
}

// Helper class to handle pointers to uint64s that may be unaligned or
// byte-swapped, or both.  DataType is uint64_t for aligned data or char
// for unaligned data.

template <bool host_order, typename DataType>
class ConstUint64Ptr {
 public:
  explicit ConstUint64Ptr(const DataType* address) : address_(address) {}
  ConstUint64Ptr() : address_(nullptr) {}

  ConstUint64Ptr operator+(ptrdiff_t i) const {
    const ptrdiff_t scale = sizeof(uint64_t) / sizeof(DataType);
    return ConstUint64Ptr(address_ + i * scale);
  }

  ConstUint64Ptr& operator+=(ptrdiff_t i) { return *this = *this + i; }

  // There are many other pointer-arithmetic operators
  // we could provide, but we don't currently use them.

  uint64_t operator*() const {
    uint64_t u = ReadUint64(address_);
    return host_order ? u : LittleEndian::ToHost64(u);
  }

  uint64_t operator[](ptrdiff_t i) const { return *(*this + i); }

  const DataType* raw() const { return address_; }

 private:
  const DataType* address_;
  // This type is copyable and assignable.
};

// ReadBitField(origin, offset, width) returns the unsigned integer
// value of the bit field of the specified width (1 to 64 inclusive) at
// the given bit offset from the given array pointer.  The bit order is
// defined to be little-endian (the least significant bit a uint64_t is
// considered to be the first bit) regardless of the native byte-order.
// The offset is unsigned because the return value is often used as the
// offset for a future call, and we don't want the caller to have to use
// a cast in that common case.  Array elements will be accessed if and
// only if the requested field overlaps them, so the function is safe
// for reading fields near the beginning and end of the array.

template <bool host_order, typename DataType>
inline uint64_t ReadBitField(const ConstUint64Ptr<host_order, DataType> origin,
                             const uint64_t offset, const int width) {
  DCHECK_LE(width, 64);
  DCHECK_GE(width, 1);

  // Mask to apply after the field is aligned (begins at bit 0):
  const uint64_t mask = std::numeric_limits<uint64_t>::max() >> (64 - width);

  // Shift distance to align the field, for each direction:
  const unsigned int shift_down = offset & 63, shift_up = -offset & 63;

  // The array element containing the first bit of the field:
  const uint64_t bottom = origin[offset >> 6];

  // The array element containing the last bit of the field:
  const uint64_t top = origin[(offset + width - 1) >> 6];

  return ((bottom >> shift_down) | (top << shift_up)) & mask;

  // Implementation rationale:  Often top and bottom are fetched from
  // the same array element, in which case the bits that survive
  // (top << shift_up) are discarded by the mask.  We could detect
  // this case and avoid some work, but the unpredictable branch would
  // cost about as much time as it saved.  On suitable platforms, when
  // width <= 57 (implying the field intersects at most 8 bytes), it's
  // tempting to use a single unaligned load that might straddle array
  // elements, but that could exceed the array bounds.
}

#if defined(NEED_ALIGNED_LOADS) || \
    (defined(__arm__) && UINTPTR_MAX <= 0xFFFFFFFF)  // {

// 64-bit unaligned loads are expensive, so it's worth doing a little
// extra work to avoid them.  If the byte order matches the bit order
// (which is little-endian), we can redraw the array element boundaries
// to coincide with the physical boundaries.

template <>
inline uint64_t ReadBitField<false, char>(
    const ConstUint64Ptr<false, char> origin, uint64_t offset,
    const int width) {
  constexpr size_t kSize = sizeof(uint64_t);
  static_assert((kSize & (kSize - 1)) == 0,
                "sizeof(uint64_t) is not a power of two");
  const uintptr_t address = reinterpret_cast<uintptr_t>(origin.raw());
  const uintptr_t misalignment = address & (kSize - 1);
  ConstUint64Ptr<false, uint64_t> aligned_origin(
      reinterpret_cast<uint64_t*>(address - misalignment));
  offset += misalignment * (64 / kSize);
  return ReadBitField<false, uint64_t>(aligned_origin, offset, width);
}

#ifdef IS_LITTLE_ENDIAN  // {
// If host order is little endian, it's the same as non-host order.
template <>
inline uint64_t ReadBitField<true, char>(
    const ConstUint64Ptr<true, char> origin, const uint64_t offset,
    const int width) {
  return ReadBitField<false, char>(ConstUint64Ptr<false, char>(origin.raw()),
                                   offset, width);
}
#endif  // }

#endif  // }

// The glue code down-casts reader pointers to ReaderImpl pointers, so we need
// to specify the base class.
//
template <class Base>
class ReaderImpl : public Base {
 protected:
  using Base::kHostOrder;
  using typename Base::ShrunkData;

 public:
  typedef ConstUint64Ptr<kHostOrder, ShrunkData> const_uint64_ptr;

  ReaderImpl();

  // This type is neither copyable nor movable.
  ReaderImpl(const ReaderImpl&) = delete;
  ReaderImpl& operator=(const ReaderImpl&) = delete;

  ~ReaderImpl() override;
  void Bind(const_uint64_ptr shrunk_array, const_uint64_ptr decode_key);
  void Bind1(const_uint64_ptr shrunk_array, const_uint64_ptr decode_key);
  uint64_t Get(size_t position) { return GetInternal<true>(position, this); }
  uint64_t SharedGet(size_t position) const {
    return GetInternal<false>(position, nullptr);
  }

 private:
  // GetInternal<use_cache>(position, writable) returns the original
  // input value at the given position.  If use_cache is true, it uses
  // and updates the index decoding state cache (see below).  Requires
  // writable == use_cache ? this : nullptr.
  template <bool use_cache>
  uint64_t GetInternal(uint64_t position, ReaderImpl* writable) const;

  // ReadLexicon(profile, index) returns the specified entry of the
  // specified profile's lexicon, or the index itself if the profile has
  // no lexicon.

  uint64_t ReadLexicon(int profile, uint64_t index) const;

  // IndexLookup<use_cache>(position, &profile, &offset_main, writable)
  // determines the profile of the given position and its offset in
  // the main section.  If use_cache is true, it initializes the
  // index decoding state cache (see below) in *writable.  Requires
  // index_present_.

  template <bool use_cache>
  void IndexLookup(uint64_t position, int* profile, uint64_t* offset_main,
                   ReaderImpl* writable) const;

  // Wrapper around ReadBitField<>().
  uint64_t ReadBitField(const_uint64_ptr origin, uint64_t offset,
                        int width) const {
    return ::ReadBitField<kHostOrder, ShrunkData>(origin, offset, width);
  }

  const_uint64_ptr shrunk_array_;

  // Extracted from the decode_key:
  int width_main_[kNumProfiles];
  int width_lex_[kNumLexWidths];
  bool index_present_;

  // offset_lex_[p] is the offset (in bits, from the start of the shrunk
  // array) to the point from which lexicon p grows downward.  Notice
  // that offset_lex_[0] is the base offset.

  uint64_t offset_lex_[kNumProfiles];

  // width_sum_lex_last_[s] is the sum of sub-lexicon widths 0..s.
  int width_sum_lex_last_[kNumSublexicons];

  // Cache of index decoding state to speed up sequential reads, used only
  // if index_present_:
  const_uint64_ptr tag_map_ptr_;  // Pointer to current profile tag map.
  uint64_t tag_map_;              // The tag map shifted, with the current
                                  // position in the bottom two bits.
  uint64_t offset_main_;          // Offset in bits to the next main field.
  uint64_t position_;             // Current position.
};

//////////////////////////////////
// Function definitions for Writer

// The number of bits needed to distinguish n values:
inline int BitsNeededToDistinguish(const int64_t n) {
  return n <= 1 ? 0 : Bits::Log2FloorNonZero64(n - 1) + 1;
}

// The number of bits needed to represent the value n, for various types of n:
inline int BitsNeededToRepresent(const uint64_t n) {
  return Bits::Log2Floor64(n) + 1;
}
inline int BitsNeededToRepresent(const uint32_t n) {
  return Bits::Log2Floor(n) + 1;
}
inline int BitsNeededToRepresent(const uint16_t n) {
  return Bits::Log2Floor(n) + 1;
}
inline int BitsNeededToRepresent(const uint8_t n) {
  return Bits::Log2Floor(n) + 1;
}

template <typename raw_type>
template <typename Func>
void Writer<raw_type>::ForEachValueFreq(const Func& func) {
  if (!freq_of_value_vec_.empty()) {
    for (size_t value = 0; value < freq_of_value_vec_.size(); ++value) {
      if (freq_of_value_vec_[value] == 0) continue;
      func(value, freq_of_value_vec_[value]);
    }
  } else {
    for (const auto& [value, freq] : freq_of_value_map_) func(value, freq);
  }
}

template <typename raw_type>
Writer<raw_type>::Writer(absl::Span<const raw_type> input, const int complexity,
                         uint64_t* const decode_key,
                         std::vector<uint64_t>* const shrunk_vector)
    : in_(input),
      complexity_(complexity),
      decode_key_(decode_key),
      out_(shrunk_vector) {
  CHECK_LE(positions(), std::numeric_limits<int64_t>::max())
      << ": ShrunkArray raw input too big";
  CHECK_GE(complexity_, 1);
  CHECK_LE(complexity_, 3);
}

template <typename raw_type>
Writer<raw_type>::~Writer() {}

template <typename raw_type>
inline bool Writer<raw_type>::MoreFrequentThenSmaller(const ValueFreq& p1,
                                                      const ValueFreq& p2) {
  return p1.freq > p2.freq || (p1.freq == p2.freq && p1.value < p2.value);
}

template <typename raw_type>
void Writer<raw_type>::InitStats() {
  CHECK_GT(positions(), 0);

  // Complexity 1 is trivial, and needs only width_max_.

  if (complexity_ == 1) {
    width_max_ = BitsNeededToRepresent(*absl::c_max_element(in_));
    return;
  }

  // Complexity 3 needs everything.  Complexity 2 needs almost
  // everything (see below).

  // Determine how much padding the end of the index would need.
  const int64_t half_blocks = (positions() - 1) / kPositionsPerHalfBlock + 1;
  if (half_blocks & 1) {
    positions_indexed_ = half_blocks * kPositionsPerHalfBlock;
  } else {
    const int64_t quarter_blocks = (positions() - 1) / kPositionsPerTagMap + 1;
    positions_indexed_ =
        quarter_blocks & 1 ? quarter_blocks * kPositionsPerTagMap : positions();
  }

  // Calculate value frequencies and extreme values.

  raw_type value_max;
  {
    const auto [min, max] = absl::c_minmax_element(in_);
    width_min_ = BitsNeededToRepresent(*min);
    value_max = *max;
    width_max_ = BitsNeededToRepresent(value_max);
    width_span_ = width_max_ - width_min_ + 1;
  }

  // Initialize counters for non-repeated values of each width.

  std::fill_n(&positions_of_width_[width_min_], width_span_, 0);
  std::fill_n(&values_of_width_[width_min_], width_span_, 0);
  std::fill_n(&values_nonrepeated_of_width_[width_min_], width_span_, 0);

  // Compute frequencies of values.

  uint64_t values_repeated = 0;
  constexpr size_t kSizeMultiplier = 10;
  // We don't want to build very long arrays of frequencies. The constant 10
  // comes from benchmarking on the index entries dumped from tera-doc.
  if (value_max < kSizeMultiplier * positions()) {
    uint64_t values = 0;
    freq_of_value_vec_.resize(value_max + 1, 0);
    for (const raw_type value : in_) {
      auto freq = ++freq_of_value_vec_[value];
      values += freq == 1;
      values_repeated += freq == 2;
    }
    values_ = values;
  } else {
    for (const raw_type value : in_) {
      auto freq = ++freq_of_value_map_[value];
      values_repeated += freq == 2;
    }
    // If a value is stored in freq_of_value_map_ it is non-zero.
    // This is guaranteed by construction, as the first operation we do to when
    // accessing the map for the first time is to increment the returned value.
    values_ = freq_of_value_map_.size();
  }
  values_repeated_ = values_repeated;

  // Collect and sort repeated values, and count non-repeated values of
  // each width.

  sorted_values_repeated_.reset(
      new ValueFreq[std::max(values_repeated_, int64_t{1})]);
  int64_t i = 0;
  // Partition values with respect to their frequency.
  ForEachValueFreq([&](raw_type value, int64_t freq) {
    DCHECK_GE(freq, 1);
    if (freq == 1) {
      ++values_nonrepeated_of_width_[BitsNeededToRepresent(value)];
    } else {
      sorted_values_repeated_[i++] = ValueFreq{value, freq};
    }
  });

  CHECK_EQ(i, values_repeated_);

  std::sort(sorted_values_repeated_.get(),
            sorted_values_repeated_.get() + values_repeated_,
            MoreFrequentThenSmaller);

  // Summarize the distribution of values and positions by width, for
  // the full set and for prefixes of the sorted array of repeated
  // values.

  // For complexity 2, only the first element of value_info_ is needed.
  const int64_t values_annotated = complexity_ == 2 ? 0 : values_repeated_;

  value_info_.reset(new ValueInfo[values_annotated + 1]);
  int64_t positions_prior = 0;
  int width_max_prior = 0, ranks = 0;
  uint64_t value_max_prior = 0, value_threshold = 0;

  value_info_[0].positions_prior = positions_prior;
  value_info_[0].value_max_prior = value_max_prior;
  value_info_[0].width_max_prior = width_max_prior;
  value_info_[0].values_remaining_rank = ranks++;

  for (int64_t i = 1; i <= values_annotated; ++i) {
    ValueFreq vf = sorted_values_repeated_[i - 1];
    value_max_prior =
        std::max(value_max_prior, static_cast<uint64_t>(vf.value));
    if (value_max_prior > value_threshold) {
      width_max_prior = BitsNeededToRepresent(value_max_prior);
      value_threshold =
          std::numeric_limits<uint64_t>::max() >> (64 - width_max_prior);
    }
    value_info_[i].value_max_prior = value_max_prior;
    value_info_[i].width_max_prior = width_max_prior;
    positions_prior += vf.freq;
    value_info_[i].positions_prior = positions_prior;
    value_info_[i].values_remaining_rank =
        absl::popcount(static_cast<uint32_t>(i)) > kNumProfiles - 1 ? -1
                                                                    : ranks++;
  }

  values_remaining_at_rank_of_width_.reset(new int64_t[ranks][65]);

  for (int64_t i = values_repeated_;;) {
    if (i <= values_annotated) {
      const int rank = value_info_[i].values_remaining_rank;
      if (rank >= 0) {
        memcpy(values_remaining_at_rank_of_width_[rank] + width_min_,
               values_of_width_ + width_min_,
               width_span_ * sizeof *values_of_width_);
      }
    }

    if (--i < 0) break;
    ValueFreq vf = sorted_values_repeated_[i];
    const int width = BitsNeededToRepresent(vf.value);
    positions_of_width_[width] += vf.freq;
    ++values_of_width_[width];
  }

  for (int w = width_min_; w <= width_max_; ++w) {
    positions_of_width_[w] += values_nonrepeated_of_width_[w];
    values_of_width_[w] += values_nonrepeated_of_width_[w];
  }
}

template <typename raw_type>
typename Writer<raw_type>::DirectSplit Writer<raw_type>::BestDirectSplit(
    const int64_t* const positions_of_width, const int p, const int w) {
  CHECK_GE(p, 0);
  CHECK_GE(w, width_min_);

  // Start with nothing in profile p+1.
  int64_t bits = 0;
  int next_w = w;
  DirectSplit best;
  best.next_width = next_w;
  best.bits = direct_split_table_[p][next_w].bits;

  // Try adding more to profile p+1.
  while (next_w > width_min_) {
    bits += positions_of_width[next_w--] * w;
    const int64_t bits_total = bits + direct_split_table_[p][next_w].bits;
    if (bits_total < best.bits) {
      best.bits = bits_total;
      best.next_width = next_w;
    }
  }

  return best;
}

template <typename raw_type>
void Writer<raw_type>::FillDirectSplitTable(
    const int64_t* const positions_of_width, const int p_max) {
  CHECK_GE(p_max, 0);
  int64_t positions_sum = 0;

  for (int w = width_min_; w <= width_max_; ++w) {
    positions_sum += positions_of_width[w];
    direct_split_table_[0][w].bits = positions_sum * w;
  }

  for (int p = 0; p < p_max; ++p) {
    for (int w = width_min_; w <= width_max_; ++w) {
      direct_split_table_[p + 1][w] = BestDirectSplit(positions_of_width, p, w);
    }
  }
}

template <typename raw_type>
int64_t Writer<raw_type>::LastLexSize(const int64_t values_lex,
                                      const int64_t* const values_lex_of_width,
                                      int* const width_lex) {
  // Fill the sub-lexicons, putting the smallest values in the first
  // sub-lexicon, and so on.

  int w = width_min_ - 1;
  int64_t values_pending = 0, bits_lex = 0;

  for (int s = 0; s < kNumSublexicons; ++s) {
    const int rounding_bias = kNumSublexicons - 1 - s;
    int64_t values = (values_lex + rounding_bias) >> kSublexBits;
    while (values_pending < values) values_pending += values_lex_of_width[++w];
    // The data layout requires lexicon entry widths to be nonzero:
    const int width_sublex = std::max(w, 1);
    width_lex[s] = width_sublex;
    bits_lex += values * width_sublex;
    values_pending -= values;
  }

  DCHECK_EQ(values_pending, 0);
  DCHECK_LE(w, width_max_);
  return bits_lex;
}

template <typename raw_type>
bool Writer<raw_type>::FindBestLexicons(
    int profiles_remaining, const int width_start, const int64_t values_covered,
    const int64_t bits_to_beat, int64_t* const bits_main,
    int64_t* const bits_lex, int* const width_main, int* const width_lex) {
  DCHECK_GE(profiles_remaining, 1);
  DCHECK_LE(values_covered, values_repeated_);

  // We will use exactly one profile in this call, leaving the rest to a
  // recursive call.
  --profiles_remaining;

  const int64_t values_repeated_uncovered = values_repeated_ - values_covered;
  const int64_t values_uncovered = values_ - values_covered;
  const ValueInfo* const vi = value_info_.get() + values_covered;
  const int64_t positions_covered = vi->positions_prior;
  DCHECK_GE(vi->values_remaining_rank, 0);
  const int64_t* const values_remaining_of_width =
      values_remaining_at_rank_of_width_[vi->values_remaining_rank];

  int64_t best_bits_main = -1, best_bits_lex = -1, best_bits_total;
  int64_t values_lex, values_lex_of_width[65];
  int try_width_main, try_width_lex[kNumSublexicons];

  // The greatest main width among the direct profiles, or -1 if we
  // don't use any direct profiles:
  int best_width_direct_max = -1;

  if (profiles_remaining == 0) {
    // We must use the last lexicon, and it must include all the
    // nonrepeated values.

    for (int w = width_min_; w <= width_max_; ++w) {
      values_lex_of_width[w] =
          values_remaining_of_width[w] + values_nonrepeated_of_width_[w];
    }

    values_lex = values_uncovered;
    try_width_main = BitsNeededToDistinguish(values_lex);
    best_bits_main = (positions() - positions_covered) * try_width_main;
    best_bits_lex = LastLexSize(values_lex, values_lex_of_width, try_width_lex);
    best_bits_total = best_bits_main + best_bits_lex;
    if (best_bits_total < bits_to_beat) {
      width_main[0] = try_width_main;
      memcpy(width_lex, try_width_lex, sizeof try_width_lex);
    }
  } else {
    // We may use the last lexicon, or we may use a non-last lexicon.
    // First consider using the last lexicon, which may include some of
    // the nonrepeated values.

    const int p = profiles_remaining - 1;  // last direct profile
    memcpy(values_lex_of_width + width_min_,
           values_remaining_of_width + width_min_,
           width_span_ * sizeof *values_lex_of_width);
    values_lex = values_repeated_uncovered;
    int64_t positions_lex =
        positions() - positions_covered - (values_ - values_repeated_);
    best_bits_total = bits_to_beat;

    int64_t try_bits_total, try_bits_main, try_bits_lex;

    // Start out including none of the nonrepeated values, then try
    // including the widest ones, then the next-widest ones, and so on.

    for (int w = width_max_; w >= width_min_; --w) {
      // We need enough lexicon entries to satisfy width_start.
      if (values_lex < (kint64one << width_start)) goto next_w;

      // Bits needed for positions containing lexicon indices:
      try_width_main = BitsNeededToDistinguish(values_lex);
      try_bits_main = positions_lex * try_width_main;

      // try_width_main and positions_lex are nondecreasing,
      // so if they already use too many bits, stop the loop.
      if (try_bits_main >= best_bits_total) break;

      // Add the bits needed for positions containing literal values.
      try_bits_main += direct_split_table_[p][w].bits;

      // If we already use too many bits without the
      // lexicon, don't bother computing the lexicon size.
      if (try_bits_main >= best_bits_total) goto next_w;

      // Add the bits needed for the lexicon.
      try_bits_lex =
          LastLexSize(values_lex, values_lex_of_width, try_width_lex);
      try_bits_total = try_bits_lex + try_bits_main;

      if (try_bits_total < best_bits_total) {
        best_bits_total = try_bits_total;
        best_bits_lex = try_bits_lex;
        best_bits_main = try_bits_main;
        width_main[profiles_remaining] = try_width_main;
        memcpy(width_lex + profiles_remaining, try_width_lex,
               sizeof try_width_lex);
        best_width_direct_max = w;
      }

    next_w:
      const int64_t delta = values_nonrepeated_of_width_[w];
      values_lex_of_width[w] += delta;
      values_lex += delta;
      positions_lex += delta;
    }

    // Now consider using a non-last lexicon.

    for (int w = width_start, values_lex = kint64one << w;
         // Our self-imposed pruning constraints require a non-last
         // lexicon to contain only repeated values...
         values_lex <= values_repeated_uncovered &&
         // ...and to leave enough values that the last lexicon's main
         // width can be at least w.
         values_lex + (values_lex >> 1) < values_uncovered;
         ++w, values_lex = kint64one << w) {
      const ValueInfo* const next_vi = vi + values_lex;
      positions_lex = next_vi->positions_prior - positions_covered;
      const int64_t try_bits_main = positions_lex * w;
      if (try_bits_main >= best_bits_total) continue;
      int try_width_lex = next_vi->width_max_prior;
      DCHECK_LE(vi->value_max_prior, next_vi->value_max_prior);

      // If value_max_prior is different at both ends of the range
      // covered by the lexicon, then try_width_lex is the correct width
      // for the lexicon entries; otherwise, it may be an overestimate,
      // so tighten it up.

      if (vi->value_max_prior == next_vi->value_max_prior) {
        DCHECK_GE(next_vi->values_remaining_rank, 0);
        const int64_t* const next_values_remaining_of_width =
            values_remaining_at_rank_of_width_[next_vi->values_remaining_rank];
        while (values_remaining_of_width[try_width_lex] ==
               next_values_remaining_of_width[try_width_lex]) {
          --try_width_lex;
          DCHECK_GE(try_width_lex, width_min_);
        }
      }

      const int64_t try_bits_lex = values_lex * try_width_lex;
      const int64_t next_bits_to_beat =
          best_bits_total - try_bits_main - try_bits_lex;
      if (next_bits_to_beat <= 0) continue;

      int64_t next_bits_main, next_bits_lex;
      int next_width_main[kNumProfiles - 1], next_width_lex[kNumLexWidths - 1];

      if (FindBestLexicons(profiles_remaining, w, values_covered + values_lex,
                           next_bits_to_beat, &next_bits_main, &next_bits_lex,
                           next_width_main, next_width_lex)) {
        best_bits_main = try_bits_main + next_bits_main;
        best_bits_lex = try_bits_lex + next_bits_lex;
        DCHECK_GT(best_bits_total, best_bits_main + best_bits_lex);
        best_bits_total = best_bits_main + best_bits_lex;
        width_main[0] = w;
        memcpy(width_main + 1, next_width_main,
               profiles_remaining * sizeof *width_main);
        width_lex[0] = try_width_lex;
        memcpy(width_lex + 1, next_width_lex,
               (profiles_remaining + kNumSublexicons - 1) * sizeof *width_lex);
        best_width_direct_max = -1;

        // Notice that width_lex[0] could be zero, which would mean this
        // profile isn't an indirect profile at all, but is actually a
        // direct profile.  It's okay to let that happen, because if
        // width_lex[0] is zero, the lexicon must contain only one value
        // (zero), and therefore width_main[0] is also zero, and the
        // decoder will still get the correct value (zero).
      }
    }
  }

  if (best_bits_total >= bits_to_beat) return false;

  if (best_width_direct_max >= 0) {
    int p = profiles_remaining - 1;
    int w = width_main[p] = best_width_direct_max;
    width_lex[p] = -1;
    while (--p >= 0) {
      w = width_main[p] = direct_split_table_[p + 1][w].next_width;
      width_lex[p] = -1;
    }
  }

  *bits_main = best_bits_main;
  *bits_lex = best_bits_lex;
  return true;
}

template <typename raw_type>
void Writer<raw_type>::ChooseParams() {
  best_bits_index_ = 0;
  best_bits_main_ = 0;
  best_bits_lex_ = 0;
  for (int i = 0; i < kNumProfiles; ++i) best_width_main_[i] = -1;
  for (int i = 0; i < kNumLexWidths; ++i) best_width_lex_[i] = -1;

  /////////////////////////
  // one-profile strategies

  // direct:
  best_bits_main_ = positions() * width_max_;
  best_width_main_[kNumProfiles - 1] = width_max_;

  // All remaining modes require more than 1 memory access to decode.
  if (complexity_ == 1) return;

  // indirect:
  int64_t bits_to_beat = best_bits_main_;
  FindBestLexicons(1,  // profiles_remaining
                   0,  // width_start
                   0,  // values_covered
                   bits_to_beat, &best_bits_main_, &best_bits_lex_,
                   best_width_main_ + kNumProfiles - 1,
                   best_width_lex_ + kNumProfiles - 1);

  //////////////////////////
  // four-profile strategies

  // With more than one profile, we need an index.

  const int64_t blocks = (positions() - 1) / kPositionsPerBlock + 1;
  const int64_t bits_index =
      blocks * 64 +                       // for the offset/size fields
      positions_indexed_ * kProfileBits;  // for the profile tag maps
  bits_to_beat = best_bits_main_ + best_bits_lex_ - bits_index;

  // We could, for every candidate solution, compute the number of pad
  // bits needed to align the index, but it's not worth the trouble.
  // Just assume we'll need the most possible pad bits (63), and
  // subtract them from the budget now.  At worst, we will miss an
  // opportunity to save 63 bits.

  bits_to_beat -= 63;

  // all-direct:
  FillDirectSplitTable(positions_of_width_, kNumProfiles - 2);
  DirectSplit split =
      BestDirectSplit(positions_of_width_, kNumProfiles - 2, width_max_);
  if (split.bits < bits_to_beat) {
    bits_to_beat = split.bits;
    best_bits_main_ = split.bits;
    best_bits_lex_ = 0;
    best_bits_index_ = bits_index;
    for (int i = 0; i < kNumLexWidths; ++i) best_width_lex_[i] = -1;
    int p = kNumProfiles;
    best_width_main_[--p] = width_max_;
    int w = best_width_main_[--p] = split.next_width;
    while (--p >= 0) {
      w = best_width_main_[p] = direct_split_table_[p + 1][w].next_width;
    }
  }

  // All remaining modes require more than 2 memory accesses to decode.
  if (complexity_ == 2) return;

  // some-indirect:
  FillDirectSplitTable(values_nonrepeated_of_width_, kNumProfiles - 2);
  if (FindBestLexicons(kNumProfiles,
                       0,  // width_start
                       0,  // values_covered
                       bits_to_beat, &best_bits_main_, &best_bits_lex_,
                       best_width_main_, best_width_lex_)) {
    best_bits_index_ = bits_index;
  }
}

template <typename raw_type>
void Writer<raw_type>::InitEncodingState() {
  // Initialize bit buffer.
  bit_buffer_ = 0;
  if (best_bits_index_ > 0) {
    // Need padding to align the index section.
    const uint64_t u = best_bits_lex_;
    bits_buffered_ = -u & 63;
  } else {
    bits_buffered_ = 0;
  }

  // Calculate base offset.
  CHECK_LE(out_->size(), (kOffsetMax - bits_buffered_ - best_bits_lex_) >> 6)
      << ": ShrunkArray capacity exceeded";
  offset_base_ = (static_cast<uint64_t>(out_->size()) << 6) + bits_buffered_ +
                 best_bits_lex_;

  // Find the range of direct profiles.
  direct_end_ = kNumProfiles - (best_width_lex_[kNumProfiles - 1] >= 0);
  for (direct_begin_ = best_bits_index_ > 0 ? 0 : kNumProfiles - 1;
       direct_begin_ < direct_end_ && best_width_lex_[direct_begin_] >= 0;
       ++direct_begin_) {
  }
  direct_last_ = direct_end_ - 1;

  // Calculate the maximum representable value for each direct profile.
  for (int p = direct_begin_; p < direct_end_; ++p) {
    const int w = best_width_main_[p];
    const raw_type allones = -1;
    // Now we simply want to do:
    // direct_value_max_[p] = ~(allones << w);
    // But w could be all the bits, and we can't shift all the bits at
    // once, so we have to contort it to this:
    direct_value_max_[p] = w == 0 ? 0 : ~((allones << 1) << (w - 1));
  }
}

template <typename raw_type>
void Writer<raw_type>::AppendBitField(const uint64_t data, const int width) {
  DCHECK_GE(width, 0);
  DCHECK_LE(width, 64);
  if (width < 64) DCHECK_EQ(0, data >> width);  // Can't shift by 64 bits.
  DCHECK_LT(bits_buffered_, 64);
  bit_buffer_ |= data << bits_buffered_;
  bits_buffered_ += width;
  if (bits_buffered_ >= 64) {
    out_->push_back(bit_buffer_);
    bits_buffered_ -= 64;
    bit_buffer_ = (data >> 1) >> (width - bits_buffered_ - 1);
    // Again, can't shift by 64 bits.  How annoying.
  }
}

template <typename raw_type>
void Writer<raw_type>::EncodeLexicons() {
  // Verify that the lexicon indices will fit in lexicon_index_for_value_.

  for (int p = 0; p < kNumProfiles; ++p) {
    if (best_width_lex_[p] >= 0) {
      CHECK_LE(best_width_main_[p], 64 - kProfileBits);
    }
  }

  // Write out the lexicons in reverse order, constructing the map from
  // values to lexicon indices for use when writing the index and main
  // sections.

  // First do the last lexicon, which is unlike the others.
  int profile = kNumProfiles - 1;

  // Count the number of values in non-last lexicons.
  int64_t values_lex_nonlast = 0;
  for (int p = 0; p <= kNumProfiles - 2; ++p) {
    if (best_width_lex_[p] >= 0) {
      values_lex_nonlast += kint64one << best_width_main_[p];
    }
  }

  if (best_width_lex_[profile] < 0) {
    lexicon_index_for_value_.rehash(values_lex_nonlast);
    // absl::flat_hash_map::rehash() takes the desired number of elements,
    // not the desired number of buckets.
  } else {
    // The last lexicon includes all repeated values that aren't in
    // other lexicons, and includes all nonrepeated values that are too
    // wide for the widest direct profile.  Calculate how many entries
    // that is.

    CHECK_LE(values_lex_nonlast, values_repeated_);
    const int width_direct_max =
        // main width of widest direct profile,
        // or width_min_ - 1 if there are no direct profiles
        direct_begin_ == direct_end_ ? width_min_ - 1
                                     : best_width_main_[direct_last_];
    CHECK_GE(width_direct_max + 1, width_min_);

    const int64_t values_lex_last =
        std::accumulate(values_nonrepeated_of_width_ + (width_direct_max + 1),
                        values_nonrepeated_of_width_ + (width_max_ + 1),
                        values_repeated_ - values_lex_nonlast);
    CHECK_LE(values_lex_last, kint64one << best_width_main_[profile]);
    lexicon_index_for_value_.rehash(values_lex_nonlast + values_lex_last);
    // absl::flat_hash_map::rehash() takes the desired number of elements,
    // not the desired number of buckets.

    // Copy the repeated values into a to-be-sorted array.

    std::unique_ptr<raw_type[]> sorted_values_lex(
        new raw_type[values_lex_last]);
    int64_t i = 0;
    while (i + values_lex_nonlast < values_repeated_) {
      sorted_values_lex[i] =
          sorted_values_repeated_[values_lex_nonlast + i].value;
      ++i;
    }

    // Copy the nonrepeated values that don't fit in width_direct_max.
    // Generally we can test this by shifting the value by
    // width_direct_max, but we can't shift by -1 or 64.  When it's 64,
    // all values fit, so we copy none.  When it's -1 (or whenever it's
    // less than width_min_) no values fit, so we copy all nonrepeated
    // values.

    if (width_direct_max < 64) {
      ForEachValueFreq([&](uint64_t value, int64_t freq) {
        if (freq != 1) return;  // Never copy repeated values.
        if (width_direct_max < width_min_ || value >> width_direct_max > 0) {
          sorted_values_lex[i++] = value;
        }
      });
    }

    CHECK_EQ(i, values_lex_last);
    std::sort(sorted_values_lex.get(),
              sorted_values_lex.get() + values_lex_last);

    // Divide the sorted array into kNumSublexicons partitions, of
    // the same sizes that were calculated by LastLexSize.  We will
    // interleave these partitions from back to front, so initialize
    // i_for_sublex[j] to the end of partition j.

    int64_t i_for_sublex[kNumSublexicons];
    for (int sublex = kNumSublexicons - 1; sublex >= 0; --sublex) {
      i_for_sublex[sublex] = i;
      const int rounding_bias = kNumSublexicons - 1 - sublex;
      i -= (values_lex_last + rounding_bias) >> kSublexBits;
    }

    CHECK_EQ(i, 0);

    // Write out the lexicon and update the map.

    for (i = values_lex_last - 1; i >= 0; --i) {
      const int sublex = i & kSublexMask;
      const raw_type value = sorted_values_lex[--i_for_sublex[sublex]];
      AppendBitField(value, best_width_lex_[profile + sublex]);
      auto& v = lexicon_index_for_value_[value];
      v = (static_cast<uint64_t>(i) << kProfileBits) | profile;
      // Since profile == 3, we should check that v < 2^64 - 1 for latter
      // distinguishing between lexicon (> 0) and non-lexicon values (0).
      // If v == 2^64 - 1 (i == 2^62 - 1), then v + 1 == 0, and we cannot
      // distinguish.
      CHECK_LT(v, std::numeric_limits<uint64_t>::max());
    }

    CHECK_EQ(i_for_sublex[0], 0);
  }

  // The other lexicons are much simpler.

  int64_t i = values_lex_nonlast;
  while (--profile >= 0) {
    const int w = best_width_lex_[profile];
    if (w < 0) continue;
    const int64_t values_lex = kint64one << best_width_main_[profile];
    const int64_t start = i - values_lex;
    while (i > start) {
      const raw_type value = sorted_values_repeated_[--i].value;
      AppendBitField(value, w);
      lexicon_index_for_value_[value] =
          (static_cast<uint64_t>(i - start) << kProfileBits) | profile;
      // Note that lexicon_index_for_value_[value] < 2^64 - 1 since profile < 3.
    }
  }
  CHECK_EQ(i, 0);

  vec_lexicon_index_for_value_.clear();
  vec_lexicon_index_for_value_.reserve(positions());
  for (const raw_type value : in_) {
    auto it = lexicon_index_for_value_.find(value);
    if (it != lexicon_index_for_value_.end()) {
      vec_lexicon_index_for_value_.push_back(it->second + 1);
    } else {
      vec_lexicon_index_for_value_.push_back(0);
    }
  }
}

template <typename raw_type>
inline void Writer<raw_type>::EncodeValue(const raw_type value,
                                          int* const profile,
                                          uint64_t* const main_field) {
  typename ValueLexMap::const_iterator it =
      lexicon_index_for_value_.find(value);

  if (it != lexicon_index_for_value_.end()) {
    uint64_t index = it->second;
    *profile = index & kProfileMask;
    if (main_field) *main_field = index >> kProfileBits;
  } else {
    DCHECK_LE(direct_begin_, direct_last_);
    int p;
    for (p = direct_begin_; p < direct_last_ && value > direct_value_max_[p];
         ++p) {
    }
    *profile = p;
    if (main_field) *main_field = value;
  }
}

template <typename raw_type>
inline void Writer<raw_type>::EncodeValueFromPos(size_t pos, int* const profile,
                                                 uint64_t* const main_field) {
  auto index = vec_lexicon_index_for_value_[pos];

  if (index) {
    --index;
    *profile = index & kProfileMask;
    if (main_field) *main_field = index >> kProfileBits;
  } else {
    DCHECK_LE(direct_begin_, direct_last_);
    auto value = in_[pos];
    int p;
    for (p = direct_begin_; p < direct_last_ && value > direct_value_max_[p];
         ++p) {
    }
    *profile = p;
    if (main_field) *main_field = value;
  }
}

template <typename raw_type>
void Writer<raw_type>::EncodeIndex() {
  CHECK_EQ(bits_buffered_, 0);
  CHECK_EQ(bit_buffer_, 0);

  uint64_t tag_map = 0;  // profile tag map for the current quarter-block
  uint64_t offset =
      offset_base_ + best_bits_index_;  // offset of next main field
  uint64_t offset_q1 = 0;               // offset of second quarter of block

  // We will use in_[0] as padding at the end of the index.
  // We can because we know the input is not empty.
  CHECK_GE(positions(), 1);

  for (int64_t position = 0; position < positions_indexed_; ++position) {
    int profile;
    if (ABSL_PREDICT_TRUE(position < positions())) {
      EncodeValueFromPos(position, &profile, nullptr);
    } else {
      EncodeValue(in_[0], &profile, nullptr);
    }

    // Incrementally construct the profile tag map.
    const int shift = (position & kPositionInTagMapMask) * kProfileBits;
    tag_map |= static_cast<uint64_t>(profile) << shift;
    offset += best_width_main_[profile];

    // Write out complete array elements.
    if (shift == kPositionInTagMapMask * kProfileBits) {
      const int quarter = (position >> kPositionInTagMapBits) & 3;
      if (quarter == 0) {
        CHECK_LE(offset, kOffsetMax) << ": ShrunkArray capacity exceeded";
        offset_q1 = offset;
      } else if (quarter == 2) {
        // Third tag map is preceded by the offset and size fields.
        const int size = offset - offset_q1;
        DCHECK_LE(size, kHalfBlockSizeMax);
        out_->push_back((offset_q1 << kHalfBlockSizeBits) | size);
      }
      out_->push_back(tag_map);
      tag_map = 0;
    }
  }

  // If we ended on an odd half-block boundary, we still owe the offset
  // and size fields.  The size will not be used, so leave it zero.

  if ((positions_indexed_ & kPositionInBlockMask) == kPositionsPerHalfBlock) {
    out_->push_back(offset_q1 << kHalfBlockSizeBits);
  }

  // Buffer any unwritten bits.
  bit_buffer_ = tag_map;
  bits_buffered_ = (positions_indexed_ & kPositionInTagMapMask) * kProfileBits;
}

template <typename raw_type>
void Writer<raw_type>::EncodeMain() {
  for (int64_t i = 0; i < positions(); ++i) {
    int profile;
    uint64_t main_field;
    EncodeValueFromPos(i, &profile, &main_field);
    AppendBitField(main_field, best_width_main_[profile]);
  }
}

template <typename raw_type>
void Writer<raw_type>::EncodeArray() {
  InitEncodingState();
  EncodeLexicons();
  if (best_bits_index_ > 0) EncodeIndex();
  EncodeMain();
  if (bits_buffered_ > 0) out_->push_back(bit_buffer_);
}

// Convert int to uint64_t, clamping negative values to zero.
// Needed for unused widths in MakeDecodeKey() below.
inline uint64_t clamp(int n) { return static_cast<uint64_t>(std::max(0, n)); }

template <typename raw_type>
void Writer<raw_type>::MakeDecodeKey() {
  // Verify that the field values are within the correct ranges.

  CHECK_EQ(offset_base_ << 13 >> 13, offset_base_);

  for (int p = 0; p < kNumProfiles; ++p) {
    CHECK_GE(best_width_main_[p], -1);
    CHECK_LE(best_width_main_[p], 64);
    CHECK_GE(best_width_lex_[p], -1);
    CHECK_LE(best_width_lex_[p], 64);
  }

  // The last lexicon widths are either all positive or all -1, never 0.

  for (int j = kNumProfiles - 1; j < kNumLexWidths; ++j) {
    CHECK_NE(best_width_lex_[j], 0);
  }

  if (best_width_lex_[kNumProfiles - 1] == -1) {
    for (int j = kNumProfiles; j < kNumLexWidths; ++j) {
      CHECK_EQ(best_width_lex_[j], -1);
    }
  } else {
    for (int j = kNumProfiles; j < kNumLexWidths; ++j) {
      CHECK_LE(best_width_lex_[j], 64);
      CHECK_GE(best_width_lex_[j], 1);
    }
  }

  CHECK_EQ(kNumProfiles, 4);
  CHECK_EQ(kNumLexWidths, 7);

  decode_key_[0] = (clamp(best_width_main_[3])) |
                   (clamp(best_width_lex_[6] - 1) << 7) | (offset_base_ << 13);
  decode_key_[1] =
      (clamp(best_width_main_[0])) | (clamp(best_width_lex_[0]) << 7) |
      (clamp(best_width_main_[1]) << 14) | (clamp(best_width_lex_[1]) << 21) |
      (clamp(best_width_main_[2]) << 28) | (clamp(best_width_lex_[2]) << 35) |
      (clamp(best_width_lex_[3]) << 42) |
      (clamp(best_width_lex_[4] - 1) << 49) |
      (clamp(best_width_lex_[5] - 1) << 55) |
      (static_cast<uint64_t>(best_bits_index_ > 0) << 63);
}

template <typename raw_type>
void Writer<raw_type>::DoIt() {
  CHECK_GE(positions(), 0);

  // Dismiss empty input, to avoid awkward corner cases.
  if (positions() == 0) {
    decode_key_[0] = decode_key_[1] = 0;
    return;
  }

  InitStats();
  if (width_max_ != 0) {
    CHECK_LE(positions(), std::numeric_limits<int64_t>::max() / width_max_)
        << ": ShrunkArray raw input too big";
  }
  ChooseParams();
  value_info_.reset();
  values_remaining_at_rank_of_width_.reset();
  EncodeArray();
  MakeDecodeKey();
}

//////////////////////////////////////
// Function definitions for ReaderImpl

// Every ReaderImpl method is called from exactly one place, so they
// might as well all be inline.

template <class Base>
inline ReaderImpl<Base>::ReaderImpl() {
  CHECK_EQ(kNumProfiles, 4);
  CHECK_EQ(kNumLexWidths, 7);
}

template <class Base>
inline ReaderImpl<Base>::~ReaderImpl() {}

template <class Base>
inline void ReaderImpl<Base>::Bind(const_uint64_ptr const shrunk_array,
                                   const_uint64_ptr const decode_key) {
  shrunk_array_ = shrunk_array;

  const uint64_t dk0 = decode_key[0];
  width_main_[3] = dk0 & 0x7F;
  width_lex_[6] = ((dk0 >> 7) & 0x3F) + 1;
  uint64_t offset;
  offset_lex_[0] = offset = dk0 >> 13;

  const uint64_t dk1 = decode_key[1];
  int wm, wl;
  width_main_[0] = wm = dk1 & 0x7F;
  width_lex_[0] = wl = (dk1 >> 7) & 0x7F;
  offset_lex_[1] = offset -= (kuint64one << wm) * wl;
  width_main_[1] = wm = (dk1 >> 14) & 0x7F;
  width_lex_[1] = wl = (dk1 >> 21) & 0x7F;
  offset_lex_[2] = offset -= (kuint64one << wm) * wl;
  width_main_[2] = wm = (dk1 >> 28) & 0x7F;
  width_lex_[2] = wl = (dk1 >> 35) & 0x7F;
  offset_lex_[3] = offset -= (kuint64one << wm) * wl;
  width_lex_[3] = wl = (dk1 >> 42) & 0x7F;
  int sum;
  width_sum_lex_last_[0] = sum = wl;
  width_lex_[4] = wl = ((dk1 >> 49) & 0x3F) + 1;
  width_sum_lex_last_[1] = sum += wl;
  width_lex_[5] = wl = ((dk1 >> 55) & 0x3F) + 1;
  width_sum_lex_last_[2] = sum += wl;
  width_sum_lex_last_[3] = sum += width_lex_[6];
  index_present_ = dk1 >> 63;

  // All valid positions are less than raw_size, which is a uint64_t, so
  // kuint64max is an invalid position that cannot be passed to Get().
  position_ = std::numeric_limits<uint64_t>::max();

#if !defined(NDEBUG)
  if (index_present_) {
    // Index must be 64-bit aligned.
    CHECK_EQ(offset_lex_[0] & 63, 0);
  } else {
    // Profiles 0..2 are unused, and their widths must be zero.
    CHECK_EQ(dk1 << 22, 0);
  }

  if (width_lex_[3] == 0) {
    // Lexicon 3 is unused, so lexicon widths 4..6 will never be used.
    // They must have been stored as zero (before being incremented).
    CHECK_EQ(width_lex_[4], 1);
    CHECK_EQ(width_lex_[5], 1);
    CHECK_EQ(width_lex_[6], 1);
  }

  // Reserved bits must be zero.
  CHECK_EQ((dk1 >> 61) & 3, 0);
#endif
}

template <class Base>
inline void ReaderImpl<Base>::Bind1(const_uint64_ptr const shrunk_array,
                                    const_uint64_ptr const decode_key) {
  // decode_key[1] is assumed to be zero, which implies no index and no
  // lexicons.

  shrunk_array_ = shrunk_array;
  const uint64_t dk0 = decode_key[0];
  width_main_[3] = dk0 & 0x7F;
  offset_lex_[3] = dk0 >> 13;
  width_lex_[0] = 0;
  width_lex_[1] = 0;
  width_lex_[2] = 0;
  width_lex_[3] = 0;
  index_present_ = false;

  // Other member variables are unused when there is no index and no
  // lexicons, so we need not initialize them.

  // No lexicons, so lexicon width 6 must have been stored as zero.
  DCHECK_EQ((dk0 >> 7) & 0x3F, 0);
}

template <class Base>
inline uint64_t ReaderImpl<Base>::ReadLexicon(const int profile,
                                              const uint64_t index) const {
  int width = width_lex_[profile];
  if (width == 0) return index;

  if (profile < kNumProfiles - 1) {
    // Non-last lexicons are very simple.
    return ReadBitField(shrunk_array_,
                        offset_lex_[profile] - (index + 1) * width, width);
  }

  // The last lexicon has sub-lexicons to deal with.
  DCHECK_EQ(profile, kNumProfiles - 1);
  const int sublex = index & kSublexMask;
  width = width_lex_[kNumProfiles - 1 + sublex];
  const uint64_t offset =
      offset_lex_[kNumProfiles - 1] -
      (index >> kSublexBits) * width_sum_lex_last_[kNumProfiles - 1] -
      width_sum_lex_last_[sublex];
  return ReadBitField(shrunk_array_, offset, width);
}

template <class Base>
template <bool use_cache>
inline void ReaderImpl<Base>::IndexLookup(const uint64_t position,
                                          int* const profile,
                                          uint64_t* const offset_main,
                                          ReaderImpl* const writable) const {
  DCHECK(index_present_);
  if (use_cache) writable->position_ = position;
  const uint64_t block = position >> (kPositionInTagMapBits + 2);
  const int position_in_block = position & kPositionInBlockMask;
  const int position_in_tag_map = position & kPositionInTagMapMask;
  const const_uint64_ptr descriptor =
      shrunk_array_ + (offset_lex_[0] >> 6) + block * 5;
  const uint64_t offset_and_size = descriptor[2];

  // The calculation differs slightly depending on which quarter
  // of the block we're in.  Since this is a performance-critical
  // function, we'll use parameterized formulas rather than
  // unpredictable branches to handle the differences.

  const int quarter = position_in_block >> kPositionInTagMapBits;
  const int half = position_in_block >> (kPositionInTagMapBits + 1);
  const const_uint64_ptr tag_map_ptr = descriptor + quarter + half;
  if (use_cache) writable->tag_map_ptr_ = tag_map_ptr;
  uint64_t tag_map = *tag_map_ptr;
  const int shift = position_in_tag_map * kProfileBits;
  const uint64_t shifted_tag_map = tag_map >> shift;
  if (use_cache) writable->tag_map_ = shifted_tag_map;
  *profile = shifted_tag_map & 3;
  // Ignore the size field if we're in the first half of the block.
  const uint64_t offset =
      (offset_and_size >> 13) +
      (offset_and_size & 0x1FFF & -static_cast<uint64_t>(half));

  // Now we need to adjust the offset by the main field widths of some
  // of the positions in the profile tag map.  If we're measuring
  // forward from the offset (second and fourth quarters), we care only
  // about tags earlier than position.  If we're measuring backward
  // (first and third quarters), those are the positions we don't care
  // about.

  const int forward = quarter & 1;
  const uint64_t forward_mask = -static_cast<uint64_t>(forward);
  tag_map &= (std::numeric_limits<uint64_t>::max() << shift) ^ forward_mask;
  const int positions_ignored =
      ((position ^ forward_mask) & kPositionInTagMapMask) + forward;

  // Count the main bits spanned by the positions of interest.
  // Counts non-overlapping occurrences of each possible two-bit
  // pattern (00, 01, 10, 11) in tag_map.

  constexpr uint64_t kOddBits = 0x5555555555555555;  // 0b010101...

  const int count_00 = absl::popcount((~tag_map & (~tag_map >> 1)) & kOddBits),
            count_01 = absl::popcount((tag_map & (~tag_map >> 1)) & kOddBits),
            count_10 = absl::popcount((~tag_map & (tag_map >> 1)) & kOddBits),
            count_11 = absl::popcount((tag_map & (tag_map >> 1)) & kOddBits);

  DCHECK_EQ(32, count_00 + count_01 + count_10 + count_11) << tag_map;
  DCHECK_EQ(absl::popcount(tag_map), count_01 + count_10 + 2 * count_11)
      << tag_map;

  uint64_t delta = (count_00 - positions_ignored) * width_main_[0] +
                   count_01 * width_main_[1] + count_10 * width_main_[2] +
                   count_11 * width_main_[3];

  // The delta needs to be negated if we're measuring backward.
  // Equivalently (and more conveniently), negate it if we're measuring
  // forward, then subtract it from the offset.

  delta = (delta ^ forward_mask) + forward;
  *offset_main = offset - delta;
  if (use_cache) writable->offset_main_ = *offset_main;
}

template <class Base>
template <bool use_cache>
inline uint64_t ReaderImpl<Base>::GetInternal(
    const uint64_t position, ReaderImpl* const writable) const {
  uint64_t offset;
  int profile, width;

  if (!index_present_) {
    profile = kNumProfiles - 1;
    width = width_main_[kNumProfiles - 1];
    offset = offset_lex_[kNumProfiles - 1] + position * width;
  } else {
    if (!use_cache || position != position_) {
      IndexLookup<use_cache>(position, &profile, &offset, writable);
    } else {
      // Finish advancing the cached state from last time.
      if ((position_ & kPositionInTagMapMask) != 0) {
        writable->tag_map_ >>= kProfileBits;
      } else {
        const int quarter = (position_ >> kPositionInTagMapBits) & 3;
        writable->tag_map_ptr_ += 1 + (quarter == 2);
        writable->tag_map_ = *tag_map_ptr_;
      }

      profile = tag_map_ & 3;
      offset = offset_main_;
    }

    width = width_main_[profile];

    // Start advancing the cached state for next time, but don't advance
    // tag_map_ptr_ and tag_map_ yet, because we don't know whether the
    // next position is valid.

    if (use_cache) {
      ++writable->position_;
      writable->offset_main_ += width;
    }
  }

  const uint64_t main_field =
      width == 0 ? 0 : ReadBitField(shrunk_array_, offset, width);
  return ReadLexicon(profile, main_field);
}

}  // anonymous namespace

///////
// Glue

void ShrunkArray::Write(const uint64_t raw_array[], size_t raw_size,
                        int complexity, uint64_t decode_key[2],
                        std::vector<uint64_t>* shrunk_vector) {
  Writer<uint64_t> writer(absl::MakeConstSpan(raw_array, raw_size), complexity,
                          decode_key, shrunk_vector);
  writer.DoIt();
}

void ShrunkArray::Write(const uint32_t raw_array[], size_t raw_size,
                        int complexity, uint64_t decode_key[2],
                        std::vector<uint64_t>* shrunk_vector) {
  Writer<uint32_t> writer(absl::MakeConstSpan(raw_array, raw_size), complexity,
                          decode_key, shrunk_vector);
  writer.DoIt();
}

void ShrunkArray::Write(const uint16_t raw_array[], size_t raw_size,
                        int complexity, uint64_t decode_key[2],
                        std::vector<uint64_t>* shrunk_vector) {
  Writer<uint16_t> writer(absl::MakeConstSpan(raw_array, raw_size), complexity,
                          decode_key, shrunk_vector);
  writer.DoIt();
}

void ShrunkArray::Write(const uint8_t raw_array[], size_t raw_size,
                        int complexity, uint64_t decode_key[2],
                        std::vector<uint64_t>* shrunk_vector) {
  Writer<uint8_t> writer(absl::MakeConstSpan(raw_array, raw_size), complexity,
                         decode_key, shrunk_vector);
  writer.DoIt();
}

const uint64_t* ShrunkArray::Copy(absl::Span<const uint64_t> shrunk_vector) {
  uint64_t* shrunk_array = new uint64_t[shrunk_vector.size()];
  std::copy(shrunk_vector.begin(), shrunk_vector.end(), shrunk_array);
  return shrunk_array;
}

namespace {
namespace impl {

typedef ReaderImpl<ShrunkArray::Reader> Reader;
typedef ReaderImpl<ShrunkArray::UnalignedLittleEndianReader>
    UnalignedLittleEndianReader;

}  // namespace impl
}  // anonymous namespace

// Reader

ShrunkArray::Reader::~Reader() {}

ShrunkArray::Reader* ShrunkArray::Reader::New() { return new impl::Reader; }

// There is no need for Bind(), Bind1(), and Get() to be virtual,
// because there is really only one reader type.

void ShrunkArray::Reader::Bind(const uint64_t* shrunk_array,
                               const uint64_t decode_key[2]) {
  absl::down_cast<impl::Reader*>(this)->Bind(
      impl::Reader::const_uint64_ptr(shrunk_array),
      impl::Reader::const_uint64_ptr(decode_key));
}

void ShrunkArray::Reader::Bind1(const uint64_t* shrunk_array,
                                const uint64_t decode_key[1]) {
  absl::down_cast<impl::Reader*>(this)->Bind1(
      impl::Reader::const_uint64_ptr(shrunk_array),
      impl::Reader::const_uint64_ptr(decode_key));
}

uint64_t ShrunkArray::Reader::Get(size_t position) {
  return absl::down_cast<impl::Reader*>(this)->Get(position);
}

uint64_t ShrunkArray::Reader::SharedGet(size_t position) const {
  return absl::down_cast<const impl::Reader*>(this)->SharedGet(position);
}

ShrunkArray::Reader::Reader() {}

// UnalignedLittleEndianReader

ShrunkArray::UnalignedLittleEndianReader::~UnalignedLittleEndianReader() {}

ShrunkArray::UnalignedLittleEndianReader*
ShrunkArray::UnalignedLittleEndianReader::New() {
  return new impl::UnalignedLittleEndianReader;
}

void ShrunkArray::UnalignedLittleEndianReader::Bind(const void* shrunk_array,
                                                    const void* decode_key) {
  absl::down_cast<impl::UnalignedLittleEndianReader*>(this)->Bind(
      impl::UnalignedLittleEndianReader::const_uint64_ptr(
          reinterpret_cast<const char*>(shrunk_array)),
      impl::UnalignedLittleEndianReader::const_uint64_ptr(
          reinterpret_cast<const char*>(decode_key)));
}

void ShrunkArray::UnalignedLittleEndianReader::Bind1(const void* shrunk_array,
                                                     const void* decode_key) {
  absl::down_cast<impl::UnalignedLittleEndianReader*>(this)->Bind1(
      impl::UnalignedLittleEndianReader::const_uint64_ptr(
          reinterpret_cast<const char*>(shrunk_array)),
      impl::UnalignedLittleEndianReader::const_uint64_ptr(
          reinterpret_cast<const char*>(decode_key)));
}

uint64_t ShrunkArray::UnalignedLittleEndianReader::Get(size_t position) {
  return absl::down_cast<impl::UnalignedLittleEndianReader*>(this)->Get(
      position);
}

uint64_t ShrunkArray::UnalignedLittleEndianReader::SharedGet(
    size_t position) const {
  return absl::down_cast<const impl::UnalignedLittleEndianReader*>(this)
      ->SharedGet(position);
}

ShrunkArray::UnalignedLittleEndianReader::UnalignedLittleEndianReader() {}
