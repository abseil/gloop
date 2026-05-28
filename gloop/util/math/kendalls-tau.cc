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

#include "gloop/util/math/kendalls-tau.h"

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/container/fixed_array.h"
#include "absl/log/check.h"
#include "absl/types/span.h"

KendallsTau::KendallsTau(const uint64_t num_concordant_pairs,
                         const uint64_t num_discordant_pairs,
                         const uint64_t num_tied_pairs,
                         const uint64_t num_extra_x, const uint64_t num_extra_y,
                         const uint64_t num_missing_x,
                         const uint64_t num_missing_y)
    : concordant_(num_concordant_pairs),
      discordant_(num_discordant_pairs),
      tied_(num_tied_pairs),
      extra_x_(num_extra_x),
      extra_y_(num_extra_y),
      missing_x_(num_missing_x),
      missing_y_(num_missing_y) {}

namespace {

// Computes:
//   \sum_{i=0..max_i-1}{\sum{j=max(i+1, min_j)..n-1}{T(i,j)}}
// Where `T(i,j)` is the contribution from pair `(i,j)`.
template <bool x_is_sorted>
KendallsTau MakeBruteForceImpl(const absl::Span<const double> x,
                               const absl::Span<const double> y,
                               const int max_i, const int min_j) {
  // !!!IMPORTANT!!!
  // The exact implementation of this function is designed to promote
  // auto-vectorization within Clang. Exercise caution when tweaking.

  // A pair of observations (xi, yi) and (xj, yj) is concordant if the
  // ranks of both elements agree. (that is, if both xi > xj and yi > yj
  // or if xi < xj and yi < yj)
  uint64_t num_concordant_pairs = 0;
  // They are discordant if xi > xj and yi < yj or if xi < xj and yi > yj.
  uint64_t num_discordant_pairs = 0;
  // If the pairs are equivalent, the pair is discarded.
  uint64_t num_tied_pairs = 0;
  // If yi = yj. "extra-x" is calculated as num_equal_y - num_tied_pairs.
  uint64_t num_equal_y = 0;
  // If xi = xj. "extra-y" is calculated as num_equal_x - num_tied_pairs.
  uint64_t num_equal_x = 0;

  for (int i = 0; i + 1 < max_i; ++i) {
    for (int j = std::max(i + 1, min_j); j < x.size(); ++j) {
      const auto x_order = x[i] <=> x[j];
      const auto y_order = y[i] <=> y[j];
      const bool x_lt = x_order == std::partial_ordering::less;
      const bool x_gt =
          x_is_sorted ? false : x_order == std::partial_ordering::greater;
      const bool y_lt = y_order == std::partial_ordering::less;
      const bool y_gt = y_order == std::partial_ordering::greater;
      num_concordant_pairs += (x_lt && y_lt) || (x_gt && y_gt);
      num_discordant_pairs += (x_lt && y_gt) || (x_gt && y_lt);

      const bool x_eq = x_order == std::partial_ordering::equivalent;
      const bool y_eq = y_order == std::partial_ordering::equivalent;
      num_equal_x += x_eq;
      num_equal_y += y_eq;
      num_tied_pairs += x_eq && y_eq;
    }
  }

  return KendallsTau(num_concordant_pairs, num_discordant_pairs, num_tied_pairs,
                     /*num_extra_x=*/num_equal_y - num_tied_pairs,
                     /*num_extra_y=*/num_equal_x - num_tied_pairs,
                     /*num_missing_x=*/0, /*num_missing_y=*/0);
}

KendallsTau MakeBruteForce(KendallsTau::InputOrdering ordering,
                           const absl::Span<const double> x,
                           const absl::Span<const double> y, const int max_i,
                           const int min_j) {
  switch (ordering) {
    case KendallsTau::InputOrdering::kUnsorted:
      return MakeBruteForceImpl</*x_is_sorted=*/false>(x, y, max_i, min_j);
    case KendallsTau::InputOrdering::kSortedByX:
    case KendallsTau::InputOrdering::kSortedByXThenY:
      return MakeBruteForceImpl</*x_is_sorted=*/true>(x, y, max_i, min_j);
  }
}

// Sorts `x` and `y` jointly on `x`, breaking ties on `y`. Writes the output to
// `reordered_x` and `reordered_y`.
void SortJoinedUnsorted(const int n, absl::Span<const double> x,
                        absl::Span<const double> y,
                        absl::Span<double> reordered_x,
                        absl::Span<double> reordered_y) {
  absl::FixedArray<int> shuffle_buffer(n);
  absl::Span<int> shuffle(shuffle_buffer.data(), n);
  absl::c_iota(shuffle, 0);
  absl::c_sort(shuffle, [x, y](int i, int j) {
    if (x[i] < x[j]) return true;
    if (x[i] > x[j]) return false;
    return y[i] < y[j];
  });

  for (size_t i = 0; i < n; i++) {
    reordered_x[i] = x[shuffle[i]];
    reordered_y[i] = y[shuffle[i]];
  }
}

// Same as `SortJoinedUnsorted`, but assumes that `x` is sorted.
void SortJoinedAssumingXSorted(const int n, absl::Span<const double> x,
                               absl::Span<const double> y,
                               double* reordered_y) {
  memcpy(reordered_y, y.data(), n * sizeof(double));
  // Find `y` ranges with sames values of `x` and sort them.
  const auto sort_range = [reordered_y](int begin, int end) {
    const int range_len = end - begin;
    if (range_len > 1) {
      absl::Span<double> range(reordered_y + begin, range_len);
      absl::c_sort(range);
    }
  };
  int start = 0;
  for (int i = 1; i < n; ++i) {
    if (x[i] != x[start]) {
      sort_range(start, i);
      start = i;
    }
  }
  sort_range(start, n);
}

// A pair of jointly sorted inputs.
class JointlySorted {
 public:
  JointlySorted(KendallsTau::InputOrdering ordering, const int n,
                absl::Span<const double> x, absl::Span<const double> y)
      : y_storage_(n) {
    y_ = absl::MakeSpan(y_storage_.data(), n);

    switch (ordering) {
      case KendallsTau::InputOrdering::kUnsorted: {
        x_storage_.emplace(n);
        SortJoinedUnsorted(n, x, y, absl::MakeSpan(*x_storage_),
                           absl::MakeSpan(y_storage_));
        x_ = absl::MakeSpan(x_storage_->data(), n);
        return;
      }
      case KendallsTau::InputOrdering::kSortedByX:
        x_ = x;
        SortJoinedAssumingXSorted(n, x, y, y_storage_.data());
        return;
      case KendallsTau::InputOrdering::kSortedByXThenY:
        x_ = x;
        memcpy(y_storage_.data(), y.data(), n * sizeof(double));
        return;
    }
  }

  absl::Span<const double> x() const { return x_; }
  absl::Span<double> y() const { return y_; }

 private:
  // Points either to the input or to `x_storage_`.
  absl::Span<const double> x_;
  // Points to y_storage_;
  absl::Span<double> y_;
  // Temporary storage for sorted inputs.
  std::optional<absl::FixedArray<double>> x_storage_;  // If needed.
  absl::FixedArray<double> y_storage_;
};

// Same as `std::merge(left, mid, mid, end, out)`, but counts discordant pairs.
uint64_t MergeRangeCountingDiscordantPairs(const double* left,
                                           const double* const mid,
                                           const double* end, double* out) {
  uint64_t num_discordant_pairs = 0;
  const double* right = mid;
  while (left < mid && right < end) {
    if (*left <= *right) {
      // Concordant order.
      *out = *left;
      ++left;
    } else {
      // Discordant order.
      *out = *right;
      ++right;
      num_discordant_pairs += mid - left;
    }
    ++out;
  }
  while (left < mid) {
    *out = *left;
    ++left;
    ++out;
  }
  while (right < end) {
    *out = *right;
    ++right;
    ++out;
  }
  return num_discordant_pairs;
}

// Does a single merge-sort step with ranges of size `size` over buffer `y`,
// writing the result to `y_out`.
uint64_t MergeCountingDiscordantPairs(const int size, absl::Span<double> y,
                                      absl::Span<double> y_out) {
  uint64_t num_discordant_pairs = 0;
  const int y_size = y.size();
  const int half_size = size / 2;
  for (int i = 0; i < y_size; i += size) {
    const int left_index = i;
    const int mid_index = std::min(i + half_size, y_size);
    const int end_index = std::min(i + size, y_size);
    num_discordant_pairs += MergeRangeCountingDiscordantPairs(
        y.data() + left_index, y.data() + mid_index, y.data() + end_index,
        y_out.data() + i);
  }
  return num_discordant_pairs;
}

uint64_t MergeSortCountingDiscordantPairs(absl::Span<double> y) {
  // We ping-pong between the source buffer and a temporary buffer.
  const int n = y.size();
  absl::FixedArray<double> tmp(n);
  absl::Span<double> y_span(y.data(), n);
  absl::Span<double> y2_span(tmp.data(), n);

  // `log(n)` merge steps.
  uint64_t num_discordant_pairs = 0;
  for (int size = 2; size < n * 2; size *= 2) {
    num_discordant_pairs += MergeCountingDiscordantPairs(size, y_span, y2_span);
    std::swap(y_span, y2_span);
  }

  // If we stopped at at a "ping" step, copy back the temporary buffer to the
  // source.
  if (y.data() != y_span.data()) {
    memcpy(y.data(), y_span.data(), n * sizeof(double));
  }
  return num_discordant_pairs;
}

// Counts tied pairs in sorted span `x` (i.e., pairs where `x[i]==x[j]`).
int CountTiedPairs(const absl::Span<const double> x) {
  if (x.size() <= 1) return 0;

  int tied = 0;
  // The previous value.
  double prev = x[0];
  // How many times `prev` has been repeated.
  int run_len = 1;
  for (int i = 1; i < x.size(); ++i) {
    DCHECK_LE(prev, x[i]) << "out of order elements found at: " << i;
    if (prev == x[i]) {
      ++run_len;
    } else {
      if (run_len > 1) {
        tied += run_len * (run_len - 1) / 2;
      }
      run_len = 1;
      prev = x[i];
    }
  }
  tied += run_len * (run_len - 1) / 2;
  return tied;
}

// Counts tied pairs in zipped sorted span `x`,`y` (i.e., pairs where
// `(x[i],y[i])==(x[j],y[j])`).
int CountTiedPairs(const absl::Span<const double> x,
                   const absl::Span<const double> y) {
  if (x.size() <= 1) return 0;

  int tied = 0;
  // The previous value.
  double prev_x = x[0];
  double prev_y = y[0];
  // How many times `(prev_x, prev_y)` has been repeated.
  int run_len = 1;
  for (int i = 1; i < x.size(); ++i) {
    if (prev_x == x[i] && prev_y == y[i]) {
      ++run_len;
    } else {
      if (run_len > 1) {
        tied += run_len * (run_len - 1) / 2;
      }
      run_len = 1;
      prev_x = x[i];
      prev_y = y[i];
    }
  }
  tied += run_len * (run_len - 1) / 2;
  return tied;
}

KendallsTau MakeKnight(KendallsTau::InputOrdering ordering,
                       const absl::Span<const double> src_x,
                       const absl::Span<const double> src_y) {
  const int n = src_x.size();
  // Sort `x` and `y` in `x` order, breaking ties using `y`.
  const JointlySorted jointly_sorted(ordering, n, src_x, src_y);
  absl::Span<const double> x = jointly_sorted.x();
  absl::Span<double> y = jointly_sorted.y();
  // Count pairs tied on `x`.
  const uint64_t num_equal_x = CountTiedPairs(x);
  // Count pairs tied on both `x` and `y`.
  const uint64_t num_tied_pairs = CountTiedPairs(x, y);
  // Sort `y` and count discordant pairs.
  const uint64_t num_discordant_pairs = MergeSortCountingDiscordantPairs(y);
  // Count pairs tied on `y`.
  const uint64_t num_equal_y = CountTiedPairs(y);

  const int num_pairs = n * (n - 1) / 2;
  const uint64_t num_concordant_pairs =
      num_pairs -
      (num_discordant_pairs + num_equal_x + num_equal_y - num_tied_pairs);

  return KendallsTau(num_concordant_pairs, num_discordant_pairs, num_tied_pairs,
                     /*num_extra_x=*/num_equal_y - num_tied_pairs,
                     /*num_extra_y=*/num_equal_x - num_tied_pairs,
                     /*num_missing_x=*/0, /*num_missing_y=*/0);
}

}  // namespace

KendallsTau KendallsTau::Make(absl::Span<const double> x,
                              absl::Span<const double> y, Algorithm algo,
                              InputOrdering ordering) {
#ifndef NDEBUG
  CHECK_EQ(x.size(), y.size());
  for (int i = 0; i < x.size(); ++i) {
    CHECK(!std::isnan(x[i]));
    CHECK(!std::isnan(y[i]));
  }

  switch (ordering) {
    case KendallsTau::InputOrdering::kUnsorted:
      break;
    case KendallsTau::InputOrdering::kSortedByX:
      CHECK(absl::c_is_sorted(x));
      break;
    case KendallsTau::InputOrdering::kSortedByXThenY:
      for (int i = 1; i < x.size(); ++i) {
        CHECK(x[i - 1] <= x[i]);
        CHECK(x[i - 1] < x[i] || y[i - 1] <= y[i]);
      }
      break;
  }
#endif

  switch (algo) {
    case Algorithm::kBruteForce:
      return MakeBruteForce(ordering, x, y, x.size(), 0);
    case Algorithm::kKnight:
      return MakeKnight(ordering, x, y);
  }
}

uint64_t KendallsTau::GetNumElementsInIntersection() const {
  int64_t intersection;
  const uint64_t total =
      concordant_ + discordant_ + tied_ + extra_x_ + extra_y_;
  // since the total number of pairs is N(N-1)/2, we can back out N...
  intersection = (1 + static_cast<int64_t>(sqrt(1 + (8 * total)))) / 2;
  CHECK_EQ(intersection * (intersection - 1) / 2, total);
  return intersection;
}

double KendallsTau::GetCorrelationEstimate() const {
  double tau;
  const uint64_t sum = concordant_ + discordant_;
  // Avoid division by zero. If sum = 0, concordant_ = 0 and discordant_ = 0,
  // thus we should return 0.
  if (sum == 0) {
    CHECK_EQ(0, concordant_);
    CHECK_EQ(0, discordant_);
    tau = 0;
  } else {
    // cast to signed ints to support negative correlations
    tau = (static_cast<int64_t>(concordant_) -
           static_cast<int64_t>(discordant_)) /
          (sqrt(sum + extra_x_) * sqrt(sum + extra_y_));
  }
  return tau;
}

double KendallsTau::GetCorrelationVariance() const {
  const uint64_t intersection = GetNumElementsInIntersection();
  return (4 * intersection + 10) /
         static_cast<double>(9 * intersection * (intersection - 1));
}

KendallsTau KendallsTau::DropBack(absl::Span<const double> x,
                                  absl::Span<const double> y, int k,
                                  InputOrdering ordering) const {
  // `x` and `y` must correspond to the current value of the statistic.
  DCHECK_EQ(KendallsTau::Make(x, y), *this)
      << "invalid `x` and `y` for incremental computation";

  const int n = x.size();
  if (k <= 0) return *this;
  if (k >= n - 1) return KendallsTau(0, 0, 0, 0, 0, 0, 0);

  // The original `KendallsTau` value `KT(n)` is the sum of partial values
  // `T(i,j)` for all pairs whose indices `(i,j)` fall in the upper triangular
  // part `U` of the matrix of indices:
  //                j
  //      +==================+
  //      |\                 |
  //      |  \               |
  //      |    \       U     |
  //      |      \           |
  //   i  |        \         |
  //      |          \       |
  //      |            \     |
  //      |              \   |
  //      |                \ |
  //      +==================+
  //
  //  KT(n) = \sum_{i=0..n-1}{\sum{j=i+1..n-1}{T(i,j)}}
  //
  // To remove the back `k` values, we simply observe that:
  //
  //  KT(n) = \sum_{i=0..n-1}{\sum{j=i+1..n-1}{T(i,j)}}
  //        = \sum_{i=0..n-k-1}{\sum{j=i+1..n-1}{T(i,j)}}
  //          + \sum_{i=n-k..n-1}{\sum{j=i+1..n-1}{T(i,j)}}
  //        = \sum_{i=0..n-k-1}{\sum{j=i+1..n-k-1}{T(i,j)}}
  //          + \sum_{i=0..n-k-1}{\sum{j=n-k..n-1}{T(i,j)}}
  //          + \sum_{i=n-k..n-1}{\sum{j=i+1..n-1}{T(i,j)}}
  //        = KT(n-k)
  //          + \sum_{i=0..n-k-1}{\sum{j=n-k..n-1}{T(i,j)}}
  //          + \sum_{i=n-k..n-1}{\sum{j=i+1..n-1}{T(i,j)}}
  //        = KT(n-k)
  //          + \sum_{i=0..n-1}{\sum{j=min(n-k,i+1)..n-1}{T(i,j)}}
  //
  //  So `KT(n-k)` is the original values (`KT(n)`) minus the band denoted by
  //  the second term above.  Graphically, this second term is the band marked
  //  by (a) below:
  //                j
  //      +==================+
  //      |\               aa|
  //      |  \             aa|
  //      |    \       U   aa|
  //      |      \         aa|
  //   i  |        \       aa|
  //      |          \     aa|
  //      |            \   aa|
  //      |              \ aa|
  //      |                \a|
  //      +==================+

  const KendallsTau removed_band = MakeBruteForce(ordering, x, y, n, n - k);
  return KendallsTau(
      concordant_ - removed_band.concordant_,
      discordant_ - removed_band.discordant_, tied_ - removed_band.tied_,
      extra_x_ - removed_band.extra_x_, extra_y_ - removed_band.extra_y_, 0, 0);
}

KendallsTau KendallsTau::DropFront(absl::Span<const double> x,
                                   absl::Span<const double> y, int k,
                                   InputOrdering ordering) const {
  // `x` and `y` must correspond to the current value of the statistic.
  DCHECK_EQ(KendallsTau::Make(x, y), *this)
      << "invalid `x` and `y` for incremental computation";

  const int n = x.size();
  if (k <= 0) return *this;
  if (k >= n - 1) return KendallsTau(0, 0, 0, 0, 0, 0, 0);

  // Applying the same argument as in `DropBack`, we're dropping the top band of
  // the matrix:
  //                j
  //      +==================+
  //      |\aaaaaaaaaaaaaaaaa|
  //      |  \aaaaaaaaaaaaaaa|
  //      |    \       U     |
  //      |      \           |
  //   i  |        \         |
  //      |          \       |
  //      |            \     |
  //      |              \   |
  //      |                \ |
  //      +==================+
  const KendallsTau removed_band = MakeBruteForce(ordering, x, y, k + 1, 0);
  return KendallsTau(
      concordant_ - removed_band.concordant_,
      discordant_ - removed_band.discordant_, tied_ - removed_band.tied_,
      extra_x_ - removed_band.extra_x_, extra_y_ - removed_band.extra_y_, 0, 0);
}
