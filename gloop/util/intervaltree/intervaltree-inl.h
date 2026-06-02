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

// Inline methods, not meant to be included by client code.

#ifndef THIRD_PARTY_GLOOP_UTIL_INTERVALTREE_INTERVALTREE_INL_H_
#define THIRD_PARTY_GLOOP_UTIL_INTERVALTREE_INTERVALTREE_INL_H_

// IWYU pragma: private, include "gloop/util/intervaltree/intervaltree.h"

#include <stddef.h>

#include <algorithm>
#include <iosfwd>
#include <iterator>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/die_if_null.h"
#include "absl/log/log.h"
#include "gloop/base/arena.h"
#include "gloop/base/arena_allocator.h"
#include "gloop/util/intervaltree/intervaltree.h"

// Swap two nodes without calling V's assignment operator.
template <typename K, typename V, typename KeyLess>
void IntervalNode<K, V, KeyLess>::SwapPtr(IntervalNode<K, V, KeyLess>* rhs) {
  // This function does not work correctly when this and rhs are connected.
  CHECK(left_ == nullptr || left_ != rhs);
  CHECK(rhs->left_ == nullptr || rhs->left_ != this);
  CHECK(rhs->right_ == nullptr || rhs->right_ != this);

  // Handle the special case when rhs is the right child of this
  bool bChild = (right_ != nullptr && right_ == rhs);
  IntervalNode<K, V, KeyLess>* rhsright = rhs->right_;
  IntervalNode<K, V, KeyLess>* thisparent = parent_;

  // Change the pointers of the node that points to rhs
  if (rhs->parent_ != nullptr) {
    if (rhs->parent_->left_ == rhs) {
      rhs->parent_->left_ = this;
    } else {
      CHECK_EQ(rhs->parent_->right_, rhs);
      rhs->parent_->right_ = this;
    }
  }

  if (rhs->left_ != nullptr) rhs->left_->parent_ = this;
  if (rhs->right_ != nullptr) rhs->right_->parent_ = this;

  // Change the pointers of node that points to this
  if (parent_ != nullptr) {
    if (parent_->left_ == this) {
      parent_->left_ = rhs;
    } else {
      CHECK_EQ(parent_->right_, this);
      parent_->right_ = rhs;
    }
  }

  if (left_ != nullptr) left_->parent_ = rhs;
  if (right_ != nullptr) right_->parent_ = rhs;

  // Swap the pointers of the node
  std::swap(left_, rhs->left_);
  std::swap(right_, rhs->right_);
  std::swap(parent_, rhs->parent_);

  // Fix the pointer for the special case when right_ == rhs;
  if (bChild) {
    right_ = rhsright;
    parent_ = rhs;
    rhs->right_ = this;
    rhs->parent_ = thisparent;
  }

  std::swap(rank_, rhs->rank_);
}

template <typename K, typename V, typename KeyLess>
IntervalTree<K, V, KeyLess>::IntervalTree(const KeyLess& less)
    : IntervalTree(new UnsafeArena(256 * sizeof(TreeNode)), less) {
  owned_arena_.reset(arena_);
}

template <typename K, typename V, typename KeyLess>
IntervalTree<K, V, KeyLess>::IntervalTree(UnsafeArena* arena,
                                          const KeyLess& less)
    : IntervalTree(InternalConstructorNonce(), ABSL_DIE_IF_NULL(arena), less) {}

template <typename K, typename V, typename KeyLess>
IntervalTree<K, V, KeyLess>::IntervalTree(HeapAllocatedNonce nonce,
                                          const KeyLess& less)
    : IntervalTree(InternalConstructorNonce(), /*arena=*/nullptr, less) {}

template <typename K, typename V, typename KeyLess>
IntervalTree<K, V, KeyLess>::IntervalTree(InternalConstructorNonce,
                                          UnsafeArena* arena,
                                          const KeyLess& less)
    : tree_root_(nullptr),
      size_(0),
      owned_arena_(),
      arena_(arena),
      key_less_(less),
      overlap_status_(NO_OVERLAP),
      structure_status_(VECTOR),
      uptodate_(true),
      min_range_(TreeNode::max_value()),
      max_range_(TreeNode::min_value()) {}

// Check all the invariants of the tree described in intervaltree.h
// (CheckInvariants's comment)
template <typename K, typename V, typename KeyLess>
int IntervalTree<K, V, KeyLess>::CheckSubtree(TreeNode* node) const {
  if (node == nullptr) return 0;

  CHECK(KeyLessThanOrEqualTo(node->begin, node->end));
  CHECK(KeyLessThanOrEqualTo(node->end, node->max_end_));

  if (node->left_ != nullptr) {
    CHECK_NE(node->left_, node);
    CHECK_EQ(node->left_->parent_, node);
    CHECK(KeyGreaterThanOrEqualTo(node->max_end_, node->left_->max_end_));
    if (!EqualTo(node->left_->begin, node->left_->end, node)) {
      CHECK(LessThan(node->left_->begin, node->left_->end, node));
    }
    CHECK_GE(node->rank_, node->left_->rank_);
    CHECK_GE(node->left_->rank_ + 1, node->rank_);
  }

  if (node->right_ != nullptr) {
    CHECK_NE(node->right_, node);
    CHECK_EQ(node->right_->parent_, node);
    CHECK(KeyGreaterThanOrEqualTo(node->max_end_, node->right_->max_end_));
    if (!EqualTo(node->begin, node->end, node->right_)) {
      CHECK(LessThan(node->begin, node->end, node->right_));
    }
    CHECK_GE(node->rank_, node->right_->rank_);
    CHECK_GE(node->right_->rank_ + 1, node->rank_);
  }

  if (node->right_ == nullptr && node->left_ == nullptr) {
    CHECK_EQ(node->rank_, 1);
  }

  if (node->parent_ != nullptr) {
    CHECK_NE(node->parent_, node);
  }

  if (node->parent_ != nullptr && node->parent_->parent_ != nullptr) {
    CHECK_NE(node->rank_, node->parent_->parent_->rank_);
  }

  return CheckSubtree(node->left_) + 1 + CheckSubtree(node->right_);
}

template <typename K, typename V, typename KeyLess>
std::string IntervalTree<K, V, KeyLess>::DebugString() const {
  // Output the status of the current data structure
  std::stringstream ss;
  ss << "\nSorted_: ";
  for (typename ItemVector::const_iterator iter = sorted_.begin();
       iter != sorted_.end(); ++iter)
    ss << "\n  " << (*iter)->begin << '/' << (*iter)->end << '/'
       << (*iter)->value;

  ss << "\nPending_: ";
  for (typename ItemVector::const_iterator iter = pending_.begin();
       iter != pending_.end(); ++iter)
    ss << "\n  " << (*iter)->begin << '/' << (*iter)->end << '/'
       << (*iter)->value;

  ss << "\nIsVec: " << IsVector() << " IsTree: " << IsIntervalTree();
  ss << " Min Max " << min_range_ << ' ' << max_range_;
  ss << "\nOverlap Status: ";
  switch (overlap_status_) {
    case MAYBE_OVERLAP:
      ss << "Maybe";
      break;
    case YES_OVERLAP:
      ss << "Yes";
      break;
    case NO_OVERLAP:
      ss << "No";
      break;
  }

  return DebugTreeString(GetRoot(), "") + ss.str();
}

template <typename K, typename V, typename KeyLess>
std::string IntervalTree<K, V, KeyLess>::DebugTreeString(
    TreeNode* node, std::string loc) const {
  if (node == nullptr) return "";
  if (loc.size() >= 5) return "\n" + loc + "5+";

  // Note(Chris0804): I can not use StringAppendF because I don't know
  // the type of node->value_ (type V must have a stream serialization)
  std::stringstream ss;
  ss << DebugTreeString(node->left_, loc + "L");
  ss << '\n'
     << node->begin << '/' << node->end << '/' << node->max_end_ << '/' << loc
     << '/' << node->value;
  ss << '/' << '(' << node->rank_ << ')';
  ss << DebugTreeString(node->right_, loc + "R");

  return ss.str();
};

template <typename K, typename V, typename KeyLess>
IntervalNode<K, V, KeyLess>* IntervalTree<K, V, KeyLess>::RecursiveCopyTree(
    const IntervalNode<K, V, KeyLess>* root_node,
    const ValueCopyFunction& value_copy_function, UnsafeArena* arena) const {
  TreeNode* newNode = nullptr;
  if (root_node) {
    newNode = CreateNodeCopy(*root_node, value_copy_function, arena);
    newNode->left_ =
        RecursiveCopyTree(root_node->left_, value_copy_function, arena);
    newNode->right_ =
        RecursiveCopyTree(root_node->right_, value_copy_function, arena);
    if (newNode->left_) newNode->left_->parent_ = newNode;
    if (newNode->right_) newNode->right_->parent_ = newNode;
  }
  return newNode;
}

template <typename K, typename V, typename KeyLess>
IntervalTree<K, V, KeyLess>* IntervalTree<K, V, KeyLess>::MakeLinearCopy(
    const ValueCopyFunction& value_copy_function, UnsafeArena* arena) const {
  if (!uptodate_) return nullptr;
  IntervalTree<K, V, KeyLess>* tree =
      arena ? new IntervalTree<K, V, KeyLess>(arena)
            : new IntervalTree<K, V, KeyLess>();
  tree->uptodate_ = true;
  tree->overlap_status_ = this->overlap_status_;
  tree->key_less_ = this->key_less_;
  tree->structure_status_ = this->structure_status_;
  tree->min_range_ = this->min_range_;
  tree->max_range_ = this->max_range_;
  tree->size_ = this->size_;
  if (IsVector()) {
    if (this->sorted_.size() > 0) {
      ItemVector& new_vec = tree->sorted_;
      new_vec.resize(this->sorted_.size());
      int cnt = 0;
      for (const auto& node : this->sorted_) {
        new_vec[cnt] = CreateNodeCopy(*node, value_copy_function, tree->arena_);
        ++cnt;
      }
    }
  } else if (IsIntervalTree() && this->size_ > 0) {
    tree->tree_root_ =
        RecursiveCopyTree(this->tree_root_, value_copy_function, tree->arena_);
  }
  return tree;
}

template <typename K, typename V, typename KeyLess>
IntervalNode<K, V, KeyLess>* IntervalTree<K, V, KeyLess>::GetRoot() const {
  DCHECK(tree_root_ == nullptr || tree_root_->parent_ == nullptr);
  return tree_root_;
}

// All intervals are treated as closed interval
template <typename K, typename V, typename KeyLess>
bool IntervalTree<K, V, KeyLess>::Intersect(const K& begin1, const K& end1,
                                            const K& begin2,
                                            const K& end2) const {
  DCHECK(KeyLessThanOrEqualTo(begin1, end1));
  DCHECK(KeyLessThanOrEqualTo(begin2, end2));

  return (KeyLessThanOrEqualTo(begin2, end1) &&
          KeyLessThanOrEqualTo(begin1, end2));
}

template <typename K, typename V, typename KeyLess>
int IntervalTree<K, V, KeyLess>::VecFindIntersect(
    const K& begin, const K& end, const bool startSmallest) const {
  CHECK_EQ(overlap_status_, NO_OVERLAP);
  CHECK_EQ(structure_status_, VECTOR);
  if (sorted_.empty()) return -1;

  // Remember, if we use vector storage, the annotations are non-overlapping,
  // but we still have to worry about those pesky zero-length annotations.

  if (startSmallest) {
    // We are looking for the *first* item which overlaps the range.
    // Start at the last position *before* the start and move forward until
    // we find something that matches or have passed the end of the window.

    typename ItemVector::const_iterator iter = std::lower_bound(
        sorted_.begin(), sorted_.end(), begin, StartingOrder(this));

    if (iter != sorted_.begin()) --iter;

    // Start with the *first* annotation at that last position.
    while (iter != sorted_.begin() &&
           KeyEqualTo((*(iter - 1))->begin, (*iter)->begin)) {
      --iter;
    }

    // Now find the first annotation that matches, or give up entirely.
    while (!Intersect((*iter)->begin, (*iter)->end, begin, end)) {
      ++iter;
      if (iter == sorted_.end() || KeyLessThan(end, (*iter)->begin)) return -1;
    }

    return std::distance(sorted_.begin(), iter);
  } else {
    // Reverse iteration: looking for the *last* item which overlaps the range.
    // That must come before the first item to start after the range end.

    typename ItemVector::const_iterator iter = std::upper_bound(
        sorted_.begin(), sorted_.end(), end, StartingOrder(this));

    if (iter == sorted_.begin()) return -1;

    --iter;
    if (!Intersect((*iter)->begin, (*iter)->end, begin, end)) return -1;

    return std::distance(sorted_.begin(), iter);
  }
}

// Return the position of interval [begin, end] if exist.
// Otherwise returns the position of the smallest element after [begin, end].
// If such node does not exist, return -1.
template <typename K, typename V, typename KeyLess>
int IntervalTree<K, V, KeyLess>::VecFindPos(const K& begin,
                                            const K& end) const {
  CHECK_EQ(overlap_status_, NO_OVERLAP);
  CHECK(IsVector());

  typename ItemVector::const_iterator iter = std::lower_bound(
      sorted_.begin(), sorted_.end(), begin, StartingOrder(this));

  while (iter != sorted_.end() && GreaterThan(begin, end, *iter)) ++iter;

  if (iter == sorted_.end()) return -1;
  return std::distance(sorted_.begin(), iter);
}

// Return the node that is
// 1) smallest node larger than node.
// 2) intersect the interval [begin, end]
// Note: Only work in tree-mode
//       (no check because static function has no access to members.)
template <typename K, typename V, typename KeyLess>
IntervalNode<K, V, KeyLess>* IntervalTree<K, V, KeyLess>::FindSmallest(
    const K& begin, const K& end, TreeNode* const node) {
  if (node == nullptr) return nullptr;

  TreeNode* root = node;
  // The data structure is designed so that it is easy to check the existence
  // of a interval that intersects with (begin, end) in the LEFT subtree.

  // Loop invariant: If there exists an answer, then the answer is always
  //                 in the subtree of root.

  for (;;) {
    // Traverse left whenever there exists a intersection in the left
    // (or there is no intersection on the right).
    if (root->left_ != nullptr) {
      if (Intersect(TreeNode::min_value(), root->left_->max_end_, begin, end)) {
        root = root->left_;
        continue;
      }
    }

    // If the execution reaches here, then the answer is not in root->left_
    if (Intersect(begin, end, root->begin, root->end)) {
      return root;
    }

    // Since the answer is not in the left subtree, try the right subtree
    if (root->right_ != nullptr) {
      root = root->right_;
      continue;
    }

    // If the answer is not in neither left nor right subtree, there is no
    // answer.
    return nullptr;
  }
}

// Return the node that is
// 1) smallest node smaller than node.
// 2) intersect the interval [begin, end]
template <typename K, typename V, typename KeyLess>
IntervalNode<K, V, KeyLess>* IntervalTree<K, V, KeyLess>::FindLargest(
    const K& begin, const K& end, TreeNode* const node) {
  if (node == nullptr) return nullptr;

  TreeNode* root = node;

  // Remember the best answer so far
  TreeNode* result = nullptr;

  // Remember the largest subtree that has a valid interval
  TreeNode* result_tree = nullptr;

  // This is different from FindSmallest because there is no easy way to
  // tell if there is an answer in the right subtree or not.
  // As a result, when the code traverse right (root = root->right),
  // there might not be any answer in root's subtree.

  // Loop invariant: if there exists an answer, then the largest answer is
  //                 either result, or in result_tree's subtree.
  // lower is a lower bound on all the nodes in root's subtree
  K lower = TreeNode::min_value();
  for (;;) {
    DCHECK(result == nullptr || result_tree == nullptr);

    // If there is an answer in the left subtree, remember it in case if
    // we can not find anything in the right subtree.

    if (root->left_ != nullptr) {
      // A quick check: Could the largest node be a node in the left subtree?
      // This check has a one-sided error on false. (May return true on false)
      if (Intersect(lower, root->left_->max_end_, begin, end)) {
        // Accurately check if there exists a valid answer in the left subtree
        if (FindSmallest(begin, end, root->left_) != nullptr) {
          result_tree = root->left_;
          result = nullptr;
        }
      }
    }

    if (Intersect(begin, end, root->begin, root->end)) {
      result = root;
      result_tree = nullptr;
    }

    // Try to find the result in the right subtree.  Since there is no easy
    // way to check the existence of an answer, let's just traverse it and
    // if I am lucky, there would be an answer.
    if (root->right_ != nullptr) {
      lower = root->begin;
      root = root->right_;
    } else {
      break;
    }
  }

  // If the largest answer is in a tree, recurse.  Otherwise, return it.
  if (result_tree == nullptr) {
    return result;
  } else {
    DCHECK(result == nullptr);
    result = FindLargest(begin, end, result_tree);
    DCHECK(result != nullptr);

    return result;
  }
}

template <typename K, typename V, typename KeyLess>
bool IntervalTree<K, V, KeyLess>::KeyLessThanOrEqualTo(const K& lhs,
                                                       const K& rhs) const {
  return !key_less_(rhs, lhs);
}

template <typename K, typename V, typename KeyLess>
bool IntervalTree<K, V, KeyLess>::KeyLessThan(const K& lhs,
                                              const K& rhs) const {
  return key_less_(lhs, rhs);
}

template <typename K, typename V, typename KeyLess>
bool IntervalTree<K, V, KeyLess>::KeyGreaterThanOrEqualTo(const K& lhs,
                                                          const K& rhs) const {
  return !key_less_(lhs, rhs);
}

template <typename K, typename V, typename KeyLess>
bool IntervalTree<K, V, KeyLess>::KeyEqualTo(const K& lhs, const K& rhs) const {
  return !key_less_(lhs, rhs) && !key_less_(rhs, lhs);
}

template <typename K, typename V, typename KeyLess>
const K& IntervalTree<K, V, KeyLess>::MaxKey(const K& lhs, const K& rhs) const {
  return KeyLessThan(lhs, rhs) ? rhs : lhs;
}

template <typename K, typename V, typename KeyLess>
const K& IntervalTree<K, V, KeyLess>::MinKey(const K& lhs, const K& rhs) const {
  return KeyLessThan(lhs, rhs) ? lhs : rhs;
}

template <typename K, typename V, typename KeyLess>
bool IntervalTree<K, V, KeyLess>::LessThan(const K& begin, const K& end,
                                           const TreeNode* node) const {
  if (key_less_(begin, node->begin)) return true;
  if (key_less_(node->begin, begin)) return false;
  if (key_less_(node->end, end)) return true;

  return false;
}

template <typename K, typename V, typename KeyLess>
bool IntervalTree<K, V, KeyLess>::GreaterThan(const K& begin, const K& end,
                                              const TreeNode* node) const {
  if (key_less_(begin, node->begin)) return false;
  if (key_less_(node->begin, begin)) return true;
  if (key_less_(end, node->end)) return true;

  return false;
}

template <typename K, typename V, typename KeyLess>
bool IntervalTree<K, V, KeyLess>::EqualTo(const K& begin, const K& end,
                                          const TreeNode* node) const {
  return KeyEqualTo(begin, node->begin) && KeyEqualTo(end, node->end);
}

template <typename K, typename V, typename KeyLess>
bool IntervalTree<K, V, KeyLess>::LessThanOrEqualTo(
    const K& begin, const K& end, const TreeNode* node) const {
  return !GreaterThan(begin, end, node);
}

template <typename K, typename V, typename KeyLess>
bool IntervalTree<K, V, KeyLess>::IsLeftChild(const TreeNode* node) {
  return (node->parent_->left_ == node);
}

template <typename K, typename V, typename KeyLess>
IntervalNode<K, V, KeyLess>* IntervalTree<K, V, KeyLess>::Rotate(
    TreeNode* const child) const {
  /*
  // If I use multi-lines style comment, cpplint complains.
  // But if I don't use multi-lines style comment, complier throws warning.
  // (because it thinks my ascii tree is poorly formatted comment)
  //
  // This is a standard rotation in any Binary Search Tree.
  //
  // If x is a left child, then rotation on x is:
  //
  //       y                       x
  //      / \     rotate(x)       / \
  //     x   C       =>          A   y
  //    / \                         / \
  //   A   B                       B   C
  //
  // where x and y are nodes, A, B, C are subtrees.
  */

  DCHECK(child->parent_ != nullptr);

  TreeNode* father = child->parent_;

  child->parent_ = father->parent_;
  if (father->parent_ != nullptr) {
    if (father->parent_->left_ == father) {
      father->parent_->left_ = child;
    } else {
      DCHECK_EQ(father->parent_->right_, father);
      father->parent_->right_ = child;
    }
  }

  if (father->left_ == child) {
    father->left_ = child->right_;
    if (father->left_ != nullptr) father->left_->parent_ = father;

    child->right_ = father;
    father->parent_ = child;
  } else {
    DCHECK_EQ(father->right_, child);

    father->right_ = child->left_;
    if (father->right_ != nullptr) father->right_->parent_ = father;

    child->left_ = father;
    father->parent_ = child;
  }

  // Update the max_end for both father and child, so max_end_ is always
  // the maximum end index for its subtree.  Observe that no other max_end_
  // changed during the rotation.

  father->max_end_ = father->end;
  if (father->left_ != nullptr)
    father->max_end_ = MaxKey(father->max_end_, father->left_->max_end_);
  if (father->right_ != nullptr)
    father->max_end_ = MaxKey(father->max_end_, father->right_->max_end_);

  child->max_end_ = MaxKey(child->max_end_, father->max_end_);

  DCHECK(KeyGreaterThanOrEqualTo(child->max_end_, child->end));
  DCHECK(child->right_ == nullptr ||
         KeyGreaterThanOrEqualTo(child->max_end_, child->right_->max_end_));
  DCHECK(child->left_ == nullptr ||
         KeyGreaterThanOrEqualTo(child->max_end_, child->left_->max_end_));

  return child;
}

template <typename K, typename V, typename KeyLess>
IntervalNode<K, V, KeyLess>* IntervalTree<K, V, KeyLess>::Find(
    const K& begin, const K& end, TreeNode* const tree,
    bool most_recent) const {
  DCHECK(tree != nullptr);

  // This is essentially a binary search to find the interval (begin, end),
  // if there are duplicated (begin, end) interval, returns the largest
  // in the inorder traversal.
  TreeNode* p = tree;
  TreeNode* equ = nullptr;

  TreeNode* valid = tree;
  while (p != nullptr) {
    valid = p;

    const K pbegin = p->begin;
    if (KeyLessThan(begin, pbegin)) {
      p = p->left_;
    } else if (KeyLessThan(pbegin, begin)) {
      p = p->right_;
    } else {
      // if we are here, then (begin == p->begin_)
      const K pend = p->end;
      if (KeyLessThan(end, pend)) {
        p = p->right_;
      } else if (KeyLessThan(pend, end)) {
        p = p->left_;
      } else {
        // if we are here, then (begin, end) == (p->begin, p->end)
        equ = p;
        p = (most_recent ? p->right_ : p->left_);
      }
    }
  }

  p = (equ ? equ : valid);

  return p;
}

template <typename K, typename V, typename KeyLess>
IntervalNode<K, V, KeyLess>* IntervalTree<K, V, KeyLess>::RBInsert(
    TreeNode* newInterval, TreeNode* const tree) {
  // Since these tree related data are not initialized in the constructor
  // initialize them here.
  newInterval->rank_ = 1;
  newInterval->parent_ = nullptr;
  newInterval->left_ = nullptr;
  newInterval->right_ = nullptr;
  newInterval->max_end_ = newInterval->end;

  // If this is the first element inserted
  if (tree == nullptr) {
    DCHECK_EQ(newInterval->rank_, 1);
    DCHECK(newInterval->parent_ == nullptr);
    DCHECK(newInterval->left_ == nullptr);
    DCHECK(newInterval->right_ == nullptr);
    DCHECK(KeyEqualTo(newInterval->max_end_, newInterval->end));

    size_ = 1;
    tree_root_ = newInterval;
    return newInterval;
  }

  K begin = newInterval->begin;
  K end = newInterval->end;

  // Find the location to insert, break tie by finding the largest node
  // in the inorder traversal.  (Tie breaking is essential to keep the
  // data structure stable)
  TreeNode* loc = Find(begin, end, tree, true);

  if (EqualTo(begin, end, loc)) {
    // Find the leaf successor
    if (loc->right_ != nullptr) {
      loc = loc->right_;
      while (loc->left_ != nullptr) loc = loc->left_;
    }
  }

  // Attach the inserted node, the inserted node already exist,
  // it is important to insert on the right side (to keep the data structure
  // stable).
  if (LessThan(begin, end, loc)) {
    DCHECK(loc->left_ == nullptr);
    loc->left_ = newInterval;
    newInterval->parent_ = loc;
  } else {
    DCHECK(loc->right_ == nullptr);
    loc->right_ = newInterval;
    newInterval->parent_ = loc;
  }

  // Fix all the max_end_ index
  while (loc != nullptr && KeyLessThan(loc->max_end_, newInterval->end)) {
    loc->max_end_ = newInterval->end;
    loc = loc->parent_;
  }

  // Fix the rank invariants
  // This is carefully implemented so that it calls rotation at most twice
  // (The old splay implementation spent 20% of the time in rotation)
  TreeNode* tofix = newInterval;

  // This is the implementation described in "Data Structures and Network
  // Algorithms" by Robert E. Tarjan.

  // Loop invariants:
  // 1) father = tofix->parent_
  // 2) grandpa = father->parent_
  // 3) The only place that could violate the rank invariants is when
  //    tofix->rank_ = father->rank_ = grandpa->rank_ (i.e. two red nodes
  //    in a row.)
  for (;;) {
    TreeNode* father = tofix->parent_;
    if (father == nullptr) break;

    TreeNode* grandpa = father->parent_;
    if (grandpa == nullptr) break;

    // Is the rank invariants fixed?
    if (grandpa->rank_ != tofix->rank_) {
      DCHECK(grandpa->rank_ == tofix->rank_ + 1 ||
             (father->rank_ == tofix->rank_ + 1 &&
              grandpa->rank_ == tofix->rank_ + 2));
      break;
    } else {
      TreeNode* uncle =
          (IsLeftChild(father) ? grandpa->right_ : grandpa->left_);

      // If uncle is a black node
      if (uncle == nullptr || uncle->rank_ != grandpa->rank_) {
        // Fix the invariants with rotations
        if (IsLeftChild(tofix) && IsLeftChild(father)) {
          // Right rotate father
          Rotate(father);
        } else if (!IsLeftChild(tofix) && !IsLeftChild(father)) {
          // Left rotate father
          Rotate(father);
        } else {
          // A left rotate and a right rotate, or vice versa
          Rotate(tofix);
          Rotate(tofix);
        }
        // Since rotation does not update tree_root_, we need to update it
        if (tree_root_->parent_ != nullptr) {
          tree_root_ = tree_root_->parent_;
          DCHECK(tree_root_->parent_ == nullptr);
        }

        break;
      } else {  // If uncle is a red node
        // tofix, father, grandpa and uncle all have the same rank_
        // (i.e. tofix, father, uncle are all red)
        DCHECK_EQ(tofix->rank_, father->rank_);
        DCHECK_EQ(father->rank_, uncle->rank_);
        DCHECK_EQ(uncle->rank_, grandpa->rank_);

        // change father and uncle to black, and grandpa to red
        ++grandpa->rank_;

        // now grandpa might violate the rank invariants because
        // grandpa's parent and grandpa's grandpa might both be red
        tofix = grandpa;
      }
    }
  }

  ++size_;
  return newInterval;
}

// Lazy insert the interval into the data structure.
// Note: If all the elements are in sorted order (before this insertion),
// the data structure tries a little harder to keep everything in sorted order.
template <typename K, typename V, typename KeyLess>
IntervalNode<K, V, KeyLess>* IntervalTree<K, V, KeyLess>::SmartInsert(
    TreeNode* const newNode) {
  // Data structure is a vector when there is no overlap after...
  // 1) removing all the 0-width intervals.
  // 2) removing all the duplicate intervals. (keep only 1 copy)

  if (IsVector()) {
    // If everything is sorted, let's try to maintain "sortness" a little
    // harder by handling the 0-width interval.
    // That is, there are many insertions in the order, (X, X), (X, Y),
    // the code attempts to put (X, Y) before (X, X).
    if (pending_.empty()) {
      // Is newNode bigger than all non-zero width intervals?
      if (KeyLessThanOrEqualTo(max_range_, newNode->begin)) {
        // location to insert
        typename ItemVector::iterator loc = sorted_.end();
        // handle the case when the largest interval is 0-width interval
        if (!sorted_.empty() && KeyEqualTo(max_range_, newNode->begin) &&
            !KeyEqualTo(newNode->begin, newNode->end)) {
          // If we are here, then the sorted_->back() is a 0 width interval.
          // and newNode is almost sorted.
          // e.g. sorted_->back() is (5, 5), and newNode is (5, 7)
          --loc;
          // The number of nodes we need to search is equal to the number
          // of identical 0 width interval.
          // Since we rarely have to search for more than 3 nodes, uses
          // linear search instead of binary search.
          // Note: Insertion of nodes in "almost" sorted order is still O(1)
          // amortized, because each 0 width interval can appear at most once
          // in the linear search.
          while (loc != sorted_.begin() &&
                 KeyEqualTo((*loc)->begin, newNode->begin) &&
                 KeyEqualTo((*loc)->begin, (*loc)->end))
            --loc;
          if (loc != sorted_.begin() || !KeyEqualTo((*loc)->begin, max_range_))
            ++loc;
        }
        sorted_.insert(loc, newNode);
        // Update only max_range_, but not min_range_
        // Note: min_range_ is only used when the data structure is a tree.
        // so it is updated in UpdateVector, not here.
        max_range_ = newNode->end;
        return newNode;
      }  // end-if (max_range_ <= newNode->begin)
    }  // end-if (pending_->empty())

    // If the newnode is the biggest so far, insert it into sorted
    TreeNode* highest = sorted_.back();
    if (LessThanOrEqualTo(highest->begin, highest->end, newNode)) {
      sorted_.push_back(newNode);
      max_range_ = MaxKey(max_range_, newNode->end);
      overlap_status_ = MAYBE_OVERLAP;
      uptodate_ = false;
      return newNode;
    }

    // We can't put it into sorted, (either because it's not in sorted_
    // order, or pending_ is not empty.)
    // Once we have some nodes in pending, everything has to go to pending
    // to keep the data structure stable. (Unless we can prove that there is
    // no duplicate copy of newNode, e.g. when newNode is biggest.)
    pending_.push_back(newNode);
    uptodate_ = false;
    // min_range_ is updated in UpdateVector, not here.
    max_range_ = MaxKey(max_range_, newNode->end);
    return newNode;

  } else {  // !IsVector()
    // This is an Interval Tree
    min_range_ = MinKey(min_range_, newNode->begin);
    max_range_ = MaxKey(max_range_, newNode->end);
    return RBInsert(newNode, GetRoot());
  }
}

// Insert an interval into the data structure.
template <typename K, typename V, typename KeyLess>
IntervalNode<K, V, KeyLess>* IntervalTree<K, V, KeyLess>::Insert(const K& begin,
                                                                 const K& end) {
  TreeNode* newNode;
  if (HasHeapAllocatedNodes()) {
    // ::new is used to allocate on the heap to work around ArenaOnlyGladiator's
    // restrictions.
    newNode = ::new TreeNode(begin, end);
  } else if (deleted_.empty()) {
    // If there are no deleted notes allocate the node using the arena.
    newNode = new (0, arena_) TreeNode(begin, end);
  } else {
    // Otherwise there are deleted nodes and we use one of them.
    newNode = ::new (deleted_.back()) TreeNode(begin, end);
    deleted_.pop_back();
  }

  return SmartInsert(newNode);
}

// Insert an interval into the data structure.
template <typename K, typename V, typename KeyLess>
template <typename... Args>
IntervalNode<K, V, KeyLess>* IntervalTree<K, V, KeyLess>::EmplaceVal(
    const K& begin, const K& end, Args&&... val) {
  TreeNode* newNode;
  if (HasHeapAllocatedNodes()) {
    // ::new is used to allocate on the heap to work around HeapOnlyGladiator's
    // restrictions.
    newNode = ::new TreeNode(begin, end, std::forward<Args>(val)...);
  } else if (deleted_.empty()) {
    // If there are no deleted notes allocate the node using the arena.
    newNode = base::NewInArena<TreeNode>(arena_, begin, end,
                                         std::forward<Args>(val)...);
  } else {
    // Otherwise there are deleted nodes and we use one of them.
    newNode = ::new (deleted_.back())
        TreeNode(begin, end, std::forward<Args>(val)...);
    deleted_.pop_back();
  }
  return SmartInsert(newNode);
}

// Insert an interval into the data structure.
template <typename K, typename V, typename KeyLess>
IntervalNode<K, V, KeyLess>* IntervalTree<K, V, KeyLess>::InsertVal(
    const K& begin, const K& end, const V& val) {
  return EmplaceVal(begin, end, val);
}

// Insert an interval into the data structure.
template <typename K, typename V, typename KeyLess>
IntervalNode<K, V, KeyLess>* IntervalTree<K, V, KeyLess>::InsertVal(
    const K& begin, const K& end, V&& val) {
  return EmplaceVal(begin, end, std::move(val));
}

// This is the canonical order of annotations inside a single docchart.
// Order by increasing begin, then by decreasing end, then stable.
template <typename K, typename V, typename KeyLess>
struct IntervalTree<K, V, KeyLess>::AnnotationOrder {
  explicit AnnotationOrder(const IntervalTree<K, V, KeyLess>* tree)
      : tree_(tree) {}

  template <typename A, typename B>
  bool operator()(A const* x, B const* y) const {
    if (tree_->KeyLessThan(x->begin, y->begin)) return true;

    if (tree_->KeyEqualTo(x->begin, y->begin) &&
        tree_->KeyLessThan(y->end, x->end)) {
      return true;
    }
    return false;
  }

  const IntervalTree<K, V, KeyLess>* tree_;
};

template <typename K, typename V, typename KeyLess>
struct IntervalTree<K, V, KeyLess>::StartingOrder {
  explicit StartingOrder(const IntervalTree<K, V, KeyLess>* tree)
      : tree_(tree) {}

  bool operator()(TreeNode const* x, const K& begin) const {
    return tree_->KeyLessThan(x->begin, begin);
  }
  bool operator()(const K& begin, TreeNode const* x) const {
    return tree_->KeyLessThan(begin, x->begin);
  }

  const IntervalTree<K, V, KeyLess>* tree_;
};

// Build a balanced binary search tree from the sorted vector
template <typename K, typename V, typename KeyLess>
IntervalNode<K, V, KeyLess>* IntervalTree<K, V, KeyLess>::BuildTree(
    const int begin, const int end) const {
  DCHECK_LE(begin, end);

  // Haha, it's easy to build a one node tree
  if (begin == end) {
    TreeNode* leaf = sorted_[begin];

    leaf->max_end_ = leaf->end;
    leaf->left_ = nullptr;
    leaf->right_ = nullptr;
    leaf->rank_ = 1;

    return leaf;
  }

  // Two nodes tree is easy too.
  if (end - begin == 1) {
    TreeNode* child = sorted_[begin];
    TreeNode* father = sorted_[end];

    child->max_end_ = child->end;
    child->left_ = nullptr;
    child->right_ = nullptr;
    child->parent_ = father;
    father->left_ = child;
    father->right_ = nullptr;

    if (KeyLessThan(father->end, child->end))
      father->max_end_ = child->end;
    else
      father->max_end_ = father->end;

    father->rank_ = 1;
    child->rank_ = 1;

    return father;
  }

  // If there is at least 3 nodes, split in the middle, then recurse
  int middle = (end + begin) / 2;
  TreeNode* root = sorted_[middle];
  TreeNode* left = BuildTree(begin, middle - 1);
  TreeNode* right = BuildTree(middle + 1, end);

  root->left_ = left;
  root->right_ = right;
  left->parent_ = root;
  right->parent_ = root;

  if (left->rank_ == right->rank_) {
    root->rank_ = left->rank_ + 1;
  } else {
    root->rank_ = std::max(left->rank_, right->rank_);
  }

  root->max_end_ = MaxKey(left->max_end_, root->end);
  root->max_end_ = MaxKey(right->max_end_, root->max_end_);

  return root;
}

// Merge and sort all the elements in vector.
// Update overlap_status_, and convert to IntervalTree if there is overlap.
// Update min_range_
template <typename K, typename V, typename KeyLess>
void IntervalTree<K, V, KeyLess>::UpdateVector() {
  CHECK_EQ(structure_status_, VECTOR);

  if (pending_.empty()) {
    if (overlap_status_ == NO_OVERLAP) return;
  } else {
    // before we merge pending into sort_ vector, there is no overlap
    // but after we merge, we have no idea if overlap exists
    if (overlap_status_ == NO_OVERLAP) overlap_status_ = MAYBE_OVERLAP;
  }

  if (sorted_.empty()) {
    std::stable_sort(pending_.begin(), pending_.end(), AnnotationOrder(this));
    std::swap(sorted_, pending_);
  } else if (!pending_.empty()) {
    // If there is only a small amount of insertions, change to IntervalTree
    //   5 is a fairly arbitrary number
    const int SWITCH_CUTOFF = 1;
    if (pending_.size() < (sorted_.size() / SWITCH_CUTOFF)) {
      min_range_ = sorted_.front()->begin;
      ChangeToIntervalTree();
      return;
    } else {
      // TODO: if sorted list is small, it might be better
      // to merge first before sorting.  Profile later to decide which method
      // is better
      std::stable_sort(pending_.begin(), pending_.end(), AnnotationOrder(this));

      // If there is a large amount of insertion, keep the data structure as a
      // vector
      ItemVector nsorted;
      std::merge(sorted_.begin(), sorted_.end(), pending_.begin(),
                 pending_.end(), std::back_insert_iterator<ItemVector>(nsorted),
                 AnnotationOrder(this));
      std::swap(sorted_, nsorted);
      pending_.clear();
    }
  }

  min_range_ = sorted_.front()->begin;

  // Update the overlap_status_
  if (overlap_status_ == MAYBE_OVERLAP && IsVector()) {
    if (sorted_.size() <= 1) {
      overlap_status_ = NO_OVERLAP;
    } else {
      overlap_status_ = NO_OVERLAP;

      // Check for overlap
      K current_max = sorted_[0]->end;

      for (int i = 1; i < sorted_.size(); ++i) {
        // If there is overlap
        if (KeyLessThan(sorted_[i]->begin, current_max)) {
          // Filter out overlap caused by (X, Y), (X, X)
          if (KeyEqualTo(sorted_[i]->begin, sorted_[i]->end) &&
              KeyEqualTo(sorted_[i]->begin, sorted_[i - 1]->end)) {
            continue;
          }

          // Filter out overlap caused by duplicated node.(e.g. (X, Y), (X, Y))
          if (KeyEqualTo(sorted_[i]->begin, sorted_[i - 1]->begin) &&
              KeyEqualTo(sorted_[i]->end, sorted_[i - 1]->end)) {
            continue;
          }

          // We found an real overlap.
          CHECK(Intersect(sorted_[i - 1]->begin, sorted_[i - 1]->end,
                          sorted_[i]->begin, sorted_[i]->end));
          overlap_status_ = YES_OVERLAP;
          ChangeToIntervalTree();
          break;
        }

        current_max = MaxKey(current_max, sorted_[i]->end);
      }
    }
  }
}

// This function copy nodes.
template <typename K, typename V, typename KeyLess>
IntervalNode<K, V, KeyLess>* IntervalTree<K, V, KeyLess>::CreateNodeCopy(
    const IntervalNode<K, V, KeyLess>& node_copy_from,
    const ValueCopyFunction& value_copy_function,
    UnsafeArena* node_arena) const {
  IntervalNode<K, V, KeyLess>* newNode;

  // If the arena is nullptr, allocate on the heap. Otherwise use the arena.
  if (node_arena == nullptr) {
    newNode = ::new TreeNode(node_copy_from.begin, node_copy_from.end);
  } else {
    newNode =
        new (0, node_arena) TreeNode(node_copy_from.begin, node_copy_from.end);
  }
  value_copy_function(node_copy_from.value, &(newNode->value));
  newNode->max_end_ = node_copy_from.max_end_;
  newNode->rank_ = node_copy_from.rank_;
  return newNode;
}

// This function make sure the data structure is up to date.  Specificall,
// 1) Process all the nodes in pending_
// 1.1) (In vector mode), merge the pending_ nodes into sorted list
// 1.2) (In tree mode), insert the pending_ into the tree.
// 2) If there is overlap, convert the data structure to a tree
template <typename K, typename V, typename KeyLess>
void IntervalTree<K, V, KeyLess>::UpdateStructure() {
  if (uptodate_) return;
  if (structure_status_ == VECTOR) {
    DCHECK_EQ(size_, 0);
    DCHECK(tree_root_ == nullptr);
    // if there are overlaps or unprocessed nodes, process them into sorted_
    if (!pending_.empty() || overlap_status_ != NO_OVERLAP) UpdateVector();
    if (overlap_status_ == YES_OVERLAP && IsVector()) {
      structure_status_ = INTERVAL_TREE;
      uptodate_ = false;
      return UpdateStructure();
    }
  } else if (structure_status_ == INTERVAL_TREE) {
    // If no tree has been built
    if (tree_root_ == nullptr) {
      // first insertion always go into sorted
      DCHECK(!sorted_.empty());
      tree_root_ = BuildTree(0, sorted_.size() - 1);
      tree_root_->parent_ = nullptr;
      min_range_ = sorted_.front()->begin;
      size_ = sorted_.size();
      // Remove nodes from vector as they have been transferred to RB
      // tree.
      sorted_.clear();
    }

    if (!pending_.empty()) {
      for (typename ItemVector::iterator piter = pending_.begin();
           piter != pending_.end(); ++piter) {
        min_range_ = MinKey(min_range_, (*piter)->begin);
        RBInsert(*piter, GetRoot());
      }
      pending_.clear();
    }
  }
  uptodate_ = true;
}

// Delete a node from the redblack tree.
template <typename K, typename V, typename KeyLess>
void IntervalTree<K, V, KeyLess>::Delete(TreeNode* node) {
  DCHECK(node != nullptr);

  // One can not delete in Vector mode
  if (IsVector()) {
    // Convert the data structure to Interval Tree
    ChangeToIntervalTree();
  }

  // Find the edge to contract

  TreeNode* todel = nullptr;
  if (node->left_ == nullptr || node->right_ == nullptr) {
    // Great! we got an edge with only one child. (Contract this edge)
    todel = node;
  } else {  // find the sucessor, swap with it, then delete sucessor
    todel = node->right_;
    while (todel->left_ != nullptr) todel = todel->left_;

    if (tree_root_ == node) tree_root_ = todel;
    node->SwapPtr(todel);
    std::swap(todel, node);
  }

  // Contract the edge

  // todel must have a missing child, so we can contract the todel edge
  CHECK(todel->left_ == nullptr || todel->right_ == nullptr);

  // get the non-missing child, if exist
  TreeNode* child = todel->right_ ? todel->right_ : todel->left_;
  TreeNode* father = todel->parent_;

  // Fix the rank of the node that violates the invariants after deletion
  TreeNode* tofix = nullptr;

  if (father == nullptr) {
    // todel is the root with only 1 children
    CHECK_EQ(todel, tree_root_);
    tree_root_ = child;
    if (child != nullptr) {
      child->parent_ = nullptr;
    } else {
      // if root has no children, then we are deletion the last node
      CHECK(size_ == 1);
    }
  } else {  // todel is not the root
    // if todel is a leaf (i.e. todel have no child)
    if (child == nullptr) {
      IsLeftChild(todel) ? (father->left_ = child) : (father->right_ = child);
      if (father->rank_ == 2) tofix = father;
    } else {  // todel has exactly 1 child
      IsLeftChild(todel) ? (father->left_ = child) : (father->right_ = child);
      child->parent_ = father;
      if (father->rank_ - child->rank_ == 2) tofix = father;
    }
  }

  // Fix the max_end invariants

  while (father != nullptr) {
    father->max_end_ = father->end;
    if (father->left_ != nullptr)
      father->max_end_ = MaxKey(father->max_end_, father->left_->max_end_);
    if (father->right_ != nullptr)
      father->max_end_ = MaxKey(father->max_end_, father->right_->max_end_);

    father = father->parent_;
  }

  // Fix the rank_ invariant (with at most 3 rotations)

  // From this point forward, child is the children of tofix that does not
  // violate the rank invariant
  if (tofix != nullptr) {
    if (tofix->left_ == nullptr || TreeNode::IsViolate(tofix->left_)) {
      child = tofix->right_;
    } else {
      child = tofix->left_;
    }
  }

  while (tofix != nullptr) {
    CHECK_EQ(child->parent_, tofix);

    // Check if tofix is indeed violation the rank invariants
    TreeNode* badchild = (IsLeftChild(child) ? tofix->right_ : tofix->left_);
    CHECK((badchild == nullptr && tofix->rank_ == 2) ||
          badchild->rank_ + 2 == tofix->rank_);

    if (TreeNode::IsBlack(child)) {
      TreeNode* fargrandchild =
          (IsLeftChild(child) ? child->left_ : child->right_);
      TreeNode* closegrandchild =
          (IsLeftChild(child) ? child->right_ : child->left_);

      // Case 1, both children are black
      // Too many black nodes here to fix, try to fix it in later iterations
      if (TreeNode::IsBlack(fargrandchild) &&
          TreeNode::IsBlack(closegrandchild)) {
        --tofix->rank_;
        if (tofix->parent_ == nullptr) break;
        if (TreeNode::IsViolate(tofix)) {
          child = IsLeftChild(tofix) ? tofix->parent_->right_
                                     : tofix->parent_->left_;
          tofix = tofix->parent_;
          continue;
        } else {
          break;
        }
      }

      // Case 2, both children are red
      if (TreeNode::IsRed(fargrandchild) && TreeNode::IsRed(closegrandchild)) {
        --tofix->rank_;
        ++child->rank_;
        Rotate(child);
        if (child->parent_ == nullptr) tree_root_ = child;
        break;
      }

      // Case 3.
      // IsBlack(closegrandchild) && IsRed(fargrandchild)
      if (TreeNode::IsRed(fargrandchild)) {
        bool is_black = TreeNode::IsBlack(closegrandchild);
        CHECK(is_black);
        --tofix->rank_;
        ++child->rank_;
        Rotate(child);
        if (child->parent_ == nullptr) tree_root_ = child;
        break;
      }

      // Case 4
      // IsRed && IsBlack
      bool is_red = TreeNode::IsRed(closegrandchild);
      bool is_black = TreeNode::IsBlack(fargrandchild);
      CHECK(is_red);
      CHECK(is_black);

      --tofix->rank_;
      ++closegrandchild->rank_;
      Rotate(closegrandchild);
      Rotate(closegrandchild);
      if (closegrandchild->parent_ == nullptr) tree_root_ = closegrandchild;
      break;

    } else {  // IsRed(child)
      if (IsLeftChild(child)) {
        Rotate(child);
        if (child->parent_ == nullptr) tree_root_ = child;
        child = tofix->left_;
      } else {
        Rotate(child);
        if (child->parent_ == nullptr) tree_root_ = child;
        child = tofix->right_;
      }
      continue;
    }
  }

  // invoke destructor.
  DeleteNode(todel);
  if (!HasHeapAllocatedNodes()) {
    deleted_.push_back(todel);
  }
  --size_;
}

// Reset the data structure
template <typename K, typename V, typename KeyLess>
void IntervalTree<K, V, KeyLess>::Reset() {
  if (!std::is_trivially_destructible<TreeNode>::value ||
      HasHeapAllocatedNodes()) {
    IterativeDestroyTree();
  }
  deleted_.clear();

  typename ItemVector::iterator piter;
  for (piter = pending_.begin(); piter != pending_.end(); ++piter) {
    DeleteNode(*piter);
  }
  pending_.clear();

  typename ItemVector::iterator siter;
  for (siter = sorted_.begin(); siter != sorted_.end(); ++siter) {
    DeleteNode(*siter);
  }
  sorted_.clear();

  size_ = 0;
  tree_root_ = nullptr;
  overlap_status_ = NO_OVERLAP;
  structure_status_ = VECTOR;
  min_range_ = TreeNode::max_value();
  max_range_ = TreeNode::min_value();
  uptodate_ = true;

  if (!HasHeapAllocatedNodes() && arena_ == owned_arena_.get()) {
    owned_arena_->Reset();
  }
}

template <typename K, typename V, typename KeyLess>
IntervalTree<K, V, KeyLess>::~IntervalTree() {
  Reset();
}

// Free all the memory in a tree with only 2 additional pointers.
// Note: This function should free the same nodes as,
//
// void RecursiveDestroyTree(TreeNode* node = tree_root_) {
//   if (node != NULL)
//      RecursiveDestroyTree(node->left_)
//      RecursiveDestoryTree(node->right_)
//      delete node;
//   }
// }
template <typename K, typename V, typename KeyLess>
void IntervalTree<K, V, KeyLess>::IterativeDestroyTree() {
  // Perform a Depth First Search (DFS) on the tree and uses only 2 pointers.
  TreeNode* node = GetRoot();  // node will traverse the tree
  TreeNode* del = nullptr;

  // If there is no node in the tree, exit
  if (node == nullptr) return;

  // Main idea: search for leaf node (nodes with no left or right children)
  // then delete it.
  for (;;) {
    // Find a leaf node
    if (node->left_ != nullptr) {
      node = node->left_;
      continue;
    }
    if (node->right_ != nullptr) {
      node = node->right_;
      continue;
    }

    // It's a leaf node, so delete it
    del = node;
    node = del->parent_;

    if (node == nullptr) {
      DeleteNode(del);
      break;
    }

    if (del == node->left_) {
      node->left_ = nullptr;
    } else {
      node->right_ = nullptr;
    }
    DeleteNode(del);
  }
}

template <typename K, typename V, typename KeyLess>
void IntervalIterator<K, V, KeyLess>::Init(const K& begin, const K& end,
                                           const IterStart start) {
  const bool startSmallest = (start == INTERVAL_SMALLEST);
  // If begin > end, iterator always returns NULL.
  if (!tree_ || tree_->KeyLessThan(end_, begin_)) {
    node_ = nullptr;
    position_ = -1;  // not needed, but I don't feel like leaving this uninit.
    return;
  }

  if (!tree_->uptodate_) tree_->UpdateStructure();

  if (tree_->IsVector()) {
    position_ = tree_->VecFindIntersect(begin, end, startSmallest);
    node_ = GetNodeFromPos();
  } else {           // The data structure is a Tree
    position_ = -1;  // not needed, but I don't feel like leaving this uninit.
    if (startSmallest) {
      node_ = tree_->FindSmallest(begin_, end_, tree_->GetRoot());
    } else {
      node_ = tree_->FindLargest(begin_, end_, tree_->GetRoot());
    }
  }
}

template <typename K, typename V, typename KeyLess>
IntervalIterator<K, V, KeyLess>::IntervalIterator(Tree* tree, const K& begin,
                                                  const K& end,
                                                  const IterStart start)
    : tree_(tree), begin_(begin), end_(end) {
  Init(begin, end, start);
}

template <typename K, typename V, typename KeyLess>
IntervalIterator<K, V, KeyLess>::IntervalIterator(const IntervalIterator& rhs)
    : tree_(rhs.tree_),
      node_(rhs.node_),
      begin_(rhs.begin_),
      end_(rhs.end_),
      position_(rhs.position_) {}

template <typename K, typename V, typename KeyLess>
IntervalIterator<K, V, KeyLess>::IntervalIterator(Tree* tree, TreeNode* node)
    : tree_(tree),
      node_(node),
      begin_(TreeNode::min_value()),
      end_(TreeNode::max_value()),
      position_(-1) {
  if (!tree_->uptodate_) tree_->UpdateStructure();

  if (tree_->IsVector()) {
    if (node == nullptr) {
      position_ = tree_->sorted_.size();
    } else {
      position_ = tree_->VecFindPos(node->begin, node->end);
      // When there are duplicate, FindPos find the smallest one.
      while (node_ != GetNodeFromPos()) {
        DCHECK(tree_->KeyEqualTo(node_->begin, GetNodeFromPos()->begin));
        ++position_;
      }
    }
  }
}

template <typename K, typename V, typename KeyLess>
IntervalIterator<K, V, KeyLess>::IntervalIterator(Tree* tree, const K& begin,
                                                  const K& end)
    : tree_(tree),
      begin_(TreeNode::min_value()),
      end_(TreeNode::max_value()),
      position_(-1) {
  CHECK(tree_->KeyLessThanOrEqualTo(begin, end));

  if (!tree_->uptodate_) tree_->UpdateStructure();

  if (tree_->IsVector()) {
    position_ = tree_->VecFindPos(begin, end);
    node_ = GetNodeFromPos();
    return;
  }

  CHECK(tree_->IsIntervalTree());

  if (tree_->GetRoot() == nullptr) {
    node_ = nullptr;
    return;
  }

  // Try to find the node (begin, end), if there are duplicate, return
  // the first one.
  node_ = tree_->Find(begin, end, tree_->GetRoot(), false);

  if (node_ != nullptr) {
    if (begin == node_->begin && end == node_->end) {
      // Great, we found the node
    } else {
      // If Find can not find the node, then it would have either the
      // predecessor or successor.  If it's predecessor, find the next
      if (!tree_->LessThan(begin, end, node_)) Next();
    }
  }
}

template <typename K, typename V, typename KeyLess>
IntervalNode<K, V, KeyLess>* IntervalIterator<K, V, KeyLess>::GetNodeFromPos() {
  DCHECK(tree_->IsVector());
  if (position_ == -1) {
    return nullptr;
  } else {
    return tree_->sorted_[position_];
  }
}

template <typename K, typename V, typename KeyLess>
bool IntervalIterator<K, V, KeyLess>::Valid() {
  DCHECK(tree_->IsVector());
  if (position_ == -1 || position_ >= tree_->sorted_.size()) {
    return false;
  } else {
    // This is the same as return (node_ == GetNodeFromPos()), but
    // saved one comparison.
    return node_ == tree_->sorted_[position_];
  }
}

template <typename K, typename V, typename KeyLess>
IntervalNode<K, V, KeyLess>* IntervalIterator<K, V, KeyLess>::Prev() {
  if (node_ == nullptr) {
    LOG(ERROR) << "The iterator already ended!";
    return nullptr;
  }

  // To the user, Prev should never change the Interval Tree, but because of
  // the lazy inserts and the possibility of changing to Interval Tree
  // Prev could actually change the data structure.
  if (!tree_->uptodate_) tree_->UpdateStructure();

  if (tree_->IsVector()) {
    if (!Valid()) {
      // If insertions and deletions performed on the tree,
      // switch to Interval Tree
      tree_->ChangeToIntervalTree();
      return Prev();
    }

    // This is grodulous.  Empty annotations come after non-empty annotations
    // at the same position.  So it's tricky to know when we're done iterating.
    // We stop after the *second* before the window begin that we see; because
    // the vector is non-overlapping, no item before that can touch the window.
    K marker = begin_;
    do {
      --position_;
      if (position_ < 0) {
        node_ = nullptr;
      } else {
        node_ = tree_->sorted_[position_];
        if (node_->begin < marker) {
          if (marker < begin_)
            node_ = nullptr;
          else
            marker = node_->begin;
        }
      }
    } while (node_ != nullptr && node_->end < begin_);

    return node_;
  }

  CHECK(tree_->IsIntervalTree());

  // Quickly handle the common case (i.e. iterator is asking for everything.)

  // If this iterator is asking for everything, quickly gives it.
  if (tree_->KeyLessThanOrEqualTo(begin_, tree_->min_range_) &&
      tree_->KeyGreaterThanOrEqualTo(end_, tree_->max_range_)) {
    // Return the largest node in the left subtree, if exist
    if (node_->left_ != nullptr) {
      node_ = node_->left_;
      while (node_->right_ != nullptr) node_ = node_->right_;
      return node_;
    }

    // Find the largest ancestor of node_ that is smaller than node_
    while (node_->parent_ != nullptr && Tree::IsLeftChild(node_))
      node_ = node_->parent_;
    node_ = node_->parent_;

    return node_;
  }

  // TODO : make this more efficient by maintaining the upper
  // and lower bound, so we can remove extra call to FindLargest

  TreeNode* result = tree_->FindLargest(begin_, end_, node_->left_);

  while (result == nullptr) {
    // Find the largest ancestor of node_ that is smaller than node_
    while (node_ != nullptr) {
      if (node_->parent_ != nullptr && !Tree::IsLeftChild(node_)) {
        node_ = node_->parent_;
        break;
      }
      node_ = node_->parent_;
    }

    if (node_ == nullptr) break;

    // Is this a valid node to return?
    if (tree_->Intersect(begin_, end_, node_->begin, node_->end)) {
      result = node_;
      break;
    }
    result = tree_->FindLargest(begin_, end_, node_->left_);
  }
  node_ = result;

  return node_;
}

template <typename K, typename V, typename KeyLess>
IntervalNode<K, V, KeyLess>* IntervalIterator<K, V, KeyLess>::Next() {
  if (node_ == nullptr) {
    LOG(ERROR) << "The iterator already ended!\n";
    return nullptr;
  }

  // While it is not necessary to check, (I can just call UpdateStructure)
  // the overhead of a function call for every Next is too big
  if (!tree_->uptodate_) tree_->UpdateStructure();

  // Handle the case when data structure is an vector
  if (tree_->IsVector()) {
    if (!Valid()) {
      // If there are updates on the tree, switch to Interval Tree
      tree_->ChangeToIntervalTree();
      return Next();
    }

    // We do this loop because of zero-length annotations, which
    // could mean there are non-matching annotations in the midst of the range.
    do {
      ++position_;
      if (position_ == tree_->sorted_.size()) {
        node_ = nullptr;  // End of the line
      } else {
        // "node_ = GetNodeFromPos" will work here, but this line below saved
        // a comparison.
        node_ = tree_->sorted_[position_];
        if (tree_->KeyLessThan(end_, node_->begin))
          node_ = nullptr;  // We've gone past the end of the window
      }
    } while (node_ != nullptr && tree_->KeyLessThan(node_->end, begin_));

    return node_;
  }

  // Quickly handle the common case (i.e. iterator is asking for everything.)

  // If this iterator is asking for everything, quickly gives it.
  if (tree_->KeyLessThanOrEqualTo(begin_, tree_->min_range_) &&
      tree_->KeyGreaterThanOrEqualTo(end_, tree_->max_range_)) {
    // Return the smallest node in the right subtree, if exist
    if (node_->right_ != nullptr) {
      node_ = node_->right_;
      while (node_->left_ != nullptr) node_ = node_->left_;
      return node_;
    }

    // Find the smallest ancestor of node_ that is larger than node_
    while (node_->parent_ != nullptr && !Tree::IsLeftChild(node_))
      node_ = node_->parent_;
    node_ = node_->parent_;

    return node_;
  }

  // Handle all other cases

  TreeNode* result = nullptr;
  if (node_->right_ != nullptr &&
      tree_->Intersect(node_->begin, node_->right_->max_end_, begin_, end_)) {
    result = tree_->FindSmallest(begin_, end_, node_->right_);
  }

  while (result == nullptr) {
    // Find the smallest ancestor of node_ that is larger than node_
    while (node_->parent_ != nullptr && !Tree::IsLeftChild(node_))
      node_ = node_->parent_;
    node_ = node_->parent_;

    if (node_ == nullptr) break;

    // Is this a valid node to return?
    if (tree_->Intersect(begin_, end_, node_->begin, node_->end)) {
      return node_;
    }

    // Is the next node we want in the right subtree?
    if (node_->right_ != nullptr &&
        tree_->Intersect(node_->begin, node_->right_->max_end_, begin_, end_)) {
      result = tree_->FindSmallest(begin_, end_, node_->right_);
    }
  }
  node_ = result;
  return node_;
}

// Delete the current node, and move to the next intersecting node.
template <typename K, typename V, typename KeyLess>
IntervalNode<K, V, KeyLess>* IntervalIterator<K, V, KeyLess>::Delete() {
  CHECK(tree_ != nullptr);
  TreeNode* toDel = Get();
  CHECK(toDel != nullptr);
  Next();
  tree_->Delete(toDel);
  return Get();
}

// Delete the current node, and move to the previous intersecting node.
template <typename K, typename V, typename KeyLess>
IntervalNode<K, V, KeyLess>* IntervalIterator<K, V, KeyLess>::PrevDelete() {
  CHECK(tree_ != nullptr);
  TreeNode* toDel = Get();
  CHECK(toDel != nullptr);
  Prev();
  tree_->Delete(toDel);
  return Get();
}

#endif  // THIRD_PARTY_GLOOP_UTIL_INTERVALTREE_INTERVALTREE_INL_H__
