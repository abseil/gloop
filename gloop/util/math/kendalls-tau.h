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

//  Kendall's Tau is a non-parametric (or rank) correlation metric
//  described on pp.642-3 of the Numerical Recipes.  Briefly, given N
//  data points (xi,yi) where xi is f1(i) and yi is f2(i), the tau
//  estimates a correlation between f1 and f2, a real number in
//  [-1,+1].
//
//  Consider all N(N-1)/2 pairs of data points (these are pairs of
//  pairs now).  For the typical {(xi,yi),(xj,yj)}, we consider
//  ((xi,yi),(xj,yj)) "concordant" if the relative ordering of xi and
//  xj matches that of yi and yj; the element is considered
//  "discordant" if the relative ordering is reversed.  If xi=xj and
//  yi=yj, the pair is thrown away.  And otherwise, if xi=xj, the pair
//  is termed "extra-y," and if yi=yj, the pair is termed "extra-x."
//
//  The estimate of the correlation between f1 and f2, is then
//
//  tau = (concordant-discordant) / (
//           sqrt(concordant + discordant + extra-y) *
//           sqrt(concordant + discordant + extra-x))
//
//  with a variance Var(tau) = (4N+10)/(9N(N-1)).
//
//  In addition to the standard labels, we also keep track of the
//  "missing-x" and "missing-y" pairs in places where only one of the
//  two functions' outputs was available; these are useful for
//  estimating the intersection and the union of the sets f1 and f2.
//
//  Note that there are THREE Kendall tau statistics (tau-a, tau-b, and tau-c),
//  which differ in how they handle ties. This library uses tau-b, which is the
//  same correlation statistic reported by R's method='kendall'.

#ifndef THIRD_PARTY_GLOOP_UTIL_MATH_KENDALLS_TAU_H_
#define THIRD_PARTY_GLOOP_UTIL_MATH_KENDALLS_TAU_H_

#include <cstdint>

#include "absl/strings/str_format.h"
#include "absl/types/span.h"

class KendallsTau {
 public:
  enum class Algorithm {
    // O(n^2) brute-force algorithm. Works well for small sizes (n < 30).
    // No extra allocations.
    kBruteForce,
    // O(n *log(n)) algorithm from Knight's
    // "A Computer Method for Calculating Kendall's Tau with Ungrouped Data".
    // Allocates `5 * n * sizeof(double)` bytes of temporary memory.
    kKnight
  };

  // If the input is known to be in a particular order, passing `ordering`
  // typically improves runtime. Passing `kUnsorted` always works and is a safe
  // default. If your input is not in any particular order, do not sort it just
  // to call `Make()`, but pass `kUnsorted` instead.
  enum class InputOrdering {
    // Input is in no particular order.
    kUnsorted,
    // Input is sorted by increasing `x`.
    kSortedByX,
    // Input is sorted by increasing `x` then increasing `y`.
    kSortedByXThenY,
  };

  KendallsTau(uint64_t num_concordant_pairs, uint64_t num_discordant_pairs,
              uint64_t num_tied_pairs, uint64_t num_extra_x,
              uint64_t num_extra_y, uint64_t num_missing_x,
              uint64_t num_missing_y);

  // Spans x and y must have equal number of elements, and none of the elements
  // should be NaN.
  static KendallsTau Make(absl::Span<const double> x,
                          absl::Span<const double> y,
                          Algorithm algo = Algorithm::kBruteForce,
                          InputOrdering ordering = InputOrdering::kUnsorted);

  // Given `x` and `y`, the two vectors passed in `Make`, incrementally removes
  // the `k` last observations from the statistic and returns the result.
  // In essence `Make(x, y).DropBack(x, y, k)` is the same
  // as `Make(x.subspan(0, x.size() - k), y.subspan(0, x.size() - k))`.
  // This runs in `O(n*k)`.
  // This works regardless of whether or not `x` and `y` are sorted. However,
  // dropping the last entries probably makes sense only if the order of `y` is
  // meaningful.
  [[nodiscard]] KendallsTau DropBack(
      absl::Span<const double> x, absl::Span<const double> y, int k,
      InputOrdering ordering = InputOrdering::kUnsorted) const;

  // Given `x` and `y`, the two vectors passed in `Make`, incrementally removes
  // the `k` first observations from the statistic and returns the result.
  // In essence `Make(x, y).DropFront(x, y, k)` is the same
  // as `Make(x.subspan(k), y.subspan(k))`.
  // This runs in `O(n*k)`.
  // This works regardless of whether or not `x` and `y` are sorted. However,
  // dropping the first entries probably makes sense only if the order of `x` is
  // meaningful.
  [[nodiscard]] KendallsTau DropFront(
      absl::Span<const double> x, absl::Span<const double> y, int k,
      InputOrdering ordering = InputOrdering::kUnsorted) const;

  uint64_t GetNumElementsInIntersection() const;
  double GetCorrelationEstimate() const;
  double GetCorrelationVariance() const;

  uint64_t GetNumElementsInUnion() const {
    return GetNumElementsInIntersection() + missing_x_ + missing_y_;
  }

  friend bool operator==(const KendallsTau& lhs,
                         const KendallsTau& rhs) = default;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const KendallsTau& p) {
    absl::Format(&sink,
                 "KendallsTau(concordant: %u, discordant: %u, tied: %u, "
                 "extra_x: %u, extra_y: %u, missing_x: %u, missing_y: %u)",
                 p.concordant_, p.discordant_, p.tied_, p.extra_x_, p.extra_y_,
                 p.missing_x_, p.missing_y_);
  }

 private:
  uint64_t concordant_;
  uint64_t discordant_;
  uint64_t tied_;
  uint64_t extra_x_;
  uint64_t extra_y_;
  uint64_t missing_x_;
  uint64_t missing_y_;
};

#endif  // THIRD_PARTY_GLOOP_UTIL_MATH_KENDALLS_TAU_H_
