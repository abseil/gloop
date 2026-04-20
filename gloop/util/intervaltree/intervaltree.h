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

// Container class for managing sets of intervals.
// Each interval is a (begin, end, value) tuples.
//
// This supports overlapping intervals, consider to use
// //gloop/util/gtl/intervalmap if you only need one value per point.
//
// // Usage : Interval Tree

// IntervalTree<int, char> tree;  // Create an interval tree
// tree.InsertVal(1, 3, 'a');  // Insert an element (requires copy constructor)
// IntervalNode<int, char>* node = tree.Insert(2, 4);  // No copy constructor
// called  // NOLINT node->value = 'b';  // Assign the value of interval [2, 4]
// to 'b'

// // Usage : Interval Iterator
// // Create an iterator that finds all intervals intersecting [2, 3],
// // The last flag decide if its initialized to smallest (or largest)
// IntervalIterator<int, char> iter(&tree, 2, 3, INTERVAL_SMALLEST);
// CHECK_EQ(iter.Get()->value, 'a');
// CHECK_EQ(iter.Get()->begin, 1);
// CHECK_EQ(iter.Get()->end, 3);
// iter.Next();  // move to the next intersecting interval
// CHECK_EQ(iter.Get()->value, 'b');
// iter.Prev();  // move to the previous intersecting interval
// CHECK_EQ(iter.Get()->value, 'a');
// iter.Delete();  // delete the interval 'a' and move to the next element
// CHECK_EQ(iter.Get()->value, 'b');
// iter.Next();
// CHECK(iter.Get() == NULL);  // we already moved to the end
//
#ifndef THIRD_PARTY_GLOOP_UTIL_INTERVALTREE_INTERVALTREE_H_
#define THIRD_PARTY_GLOOP_UTIL_INTERVALTREE_INTERVALTREE_H_

// This data structure maintains a collection of intervals, each with a begin
// and end position together with an arbitrary user type.  The intervals
// are stored using the following ordering:
//
// An interval x is "less than" y if
//   x->begin < y->begin, or
//   x->begin == y->begin && x->end > y->end, or
//   x->begin == y->begin && x->end == y->end && (x is inserted before y)
//
// Iterators iterate through intervals from least to greatest using the above
// rules.
//
// To maintain the intervals, the IntervalTree class chooses between Vector
// and Tree automatically base on the usage pattern.
//
// Time complexity of methods:
// 1) Vector (non-overlapping keys)
//    Memory: O(n)
//    Insert (lazy): O(1)
//    First Iterator: O(n log n) (to process the insertion)
//    Successive Iterator: O(log n) for initialization
//    Next: O(1)
//    Prev: O(1)
//    First deletion: O(n) (causes an automatic change to interval tree)
// 2) Interval Tree
//    Memory: O(n), where n is the number of insertions.
//    Insert (lazy): O(1)
//    First Iterator: O(n log n)
//    Successive Iterator: O(log n) for initialization
//    Next: O(log n), or O(log d) where d is the distance in annotation order.
//    Prev: O(log^2 n), or (log^2 d).
//    Deletion: O(log n)
//    Reset: O(n)
//
// Notes:
// 1) All the intervals are considered closed intervals.
// 2) The memory of a deleted node is not freed immediately.
// 3) A Vector is initially chosen for non-overlapping keys, and an
//    Interval Tree otherwise. An IntervalTree backed by a Vector is
//    transformed into one backed by an Interval Tree during the first deletion.
// 4) The insertion of overlapping intervals, and alternations between inserts
//    and queries can cause the backing data structure to change to an
//    Interval Tree. This conversion is O(n) with number of insertions.
// 5) The conversion will occur at most once per tree.
// 6) After UpdateStructure() is called, you may concurrently
//    create IntervalIterators and call Next and Prev freely
//    as long as the tree is not concurrently being modified.
// 7) Insertion and Deletion do not invalidate iterators
//    (unless the iterator's node was deleted).
// 8) The data structure is stable. The relative ordering of keys is not changed
//    with insertions and deletions.

#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "gloop/base/arena.h"
#include "gloop/base/arena_allocator.h"

template <typename K, typename V, typename KeyLess>
class ConstIntervalIterator;
template <typename K, typename V, typename KeyLess>
class IntervalIterator;
template <typename K, typename V, typename KeyLess>
class IntervalTree;

template <typename K>
class IntervalTreeLimits {
 public:
  static K min() {
    // For integral types, numeric_limits<K>::lowest() is the same as
    // numeric_limits<K>::min(): both are the smallest (finite)
    // representable value. For floating-point types, min() is the smallest
    // positive normalised value and lowest() is the smallest finite
    // representable value.
    return std::numeric_limits<K>::lowest();
  }
  static K max() { return std::numeric_limits<K>::max(); }
};

template <typename K, typename V, typename KeyLess = std::less<K>>
class IntervalNode : public ArenaOnlyGladiator {
 public:
  // Create an interval [begin, end]
  //   Note: value will not be initialized.
  IntervalNode(const K& node_begin, const K& node_end)
      : begin(node_begin),
        end(node_end),
        value(),
        left_(nullptr),
        right_(nullptr),
        parent_(nullptr),
        max_end_(node_end),
        rank_(1) {}

  // Create an interval (begin, end, value)
  // with the value coming via copy semantincs.
  template <typename T, typename U>
  IntervalNode(const T& node_begin, const T& node_end, U&& val)
      : begin(node_begin),
        end(node_end),
        value(std::forward<U>(val)),
        left_(nullptr),
        right_(nullptr),
        parent_(nullptr),
        max_end_(node_end),
        rank_(1) {}

  inline V* ptr() { return &value; }

  // begin, end, value is defined without the trailing "_", because we need to
  // keep the interface identical to ChartItem
  K begin;
  K end;
  V value;  // Your data here

  // The maximum value of the datatype.
  static inline K max_value() { return IntervalTreeLimits<K>::max(); }

  // The minimum value of the datatype.
  static inline K min_value() { return IntervalTreeLimits<K>::min(); }

 private:
  friend class IntervalTree<K, V, KeyLess>;
  friend class IntervalIterator<K, V, KeyLess>;
  friend class ConstIntervalIterator<K, V, KeyLess>;

  IntervalNode<K, V, KeyLess>* left_;
  IntervalNode<K, V, KeyLess>* right_;
  IntervalNode<K, V, KeyLess>* parent_;

  K max_end_;  // The maximum end of the whole subtree
  int rank_;   // Rank_ implicitly stores the color (red or black) or a node.

  // is the node black?
  inline static bool IsBlack(const IntervalNode<K, V, KeyLess>* node) {
    if (node == nullptr || node->parent_ == nullptr) return true;
    if (node->parent_->rank_ == node->rank_ + 1) return true;
    return false;
  }

  // is the node red?
  inline static bool IsRed(const IntervalNode<K, V, KeyLess>* node) {
    return !IsBlack(node);
  }

  // check if the node and its parent violated the rank invariant
  inline static bool IsViolate(const IntervalNode<K, V, KeyLess>* node) {
    CHECK(node != nullptr);
    CHECK(node->parent_ != nullptr);
    CHECK_LE(node->parent_->rank_ - node->rank_, 2);
    return (node->parent_->rank_ - node->rank_ == 2);
  }

  // Swapping the pointers of the node
  //  Warning: This does not swap begin, end, max_end_. value
  // Essentially, this allows me to swap the value of two nodes without calling
  // V's assignment operator , and without invalidating any iterator.
  void SwapPtr(IntervalNode<K, V, KeyLess>* rhs);
};

template <typename K, typename V, typename KeyLess = std::less<K>>
class IntervalTree {
 public:
  // A value used only to distinguish the heap allocated constructor.
  struct HeapAllocatedNonce {};
  static constexpr HeapAllocatedNonce kHeapAllocated = {};

  typedef IntervalIterator<K, V, KeyLess> iterator;
  typedef ConstIntervalIterator<K, V, KeyLess> const_iterator;
  typedef IntervalNode<K, V, KeyLess> TreeNode;

  typedef KeyLess key_less;

  // Create an interval tree.  Duplicates are allowed.
  explicit IntervalTree(const KeyLess& less = KeyLess());

  // Same, but the caller manages the arena.  The supplied pointer is
  // not owned here and must remain live until this object is destroyed.
  //
  // NOTE that when IntervalTree::Reset() is called this arena will
  // not be reset; it is the caller's responsibility to release any
  // memory this object has allocated via the arena.
  explicit IntervalTree(UnsafeArena* arena, const KeyLess& less = KeyLess());

  // Create an interval tree that allocates memory on heap rather than using an
  // arena. It is appropriate for cases where minimizing the per-object memory
  // overhead over the lifetime of the object is important.
  explicit IntervalTree(HeapAllocatedNonce nonce,
                        const KeyLess& less = KeyLess());

  // This type is neither copyable nor movable.
  IntervalTree(const IntervalTree&) = delete;
  IntervalTree& operator=(const IntervalTree&) = delete;

  // Emplace an interval, the emplaced interval is returned.
  template <typename... Args>
  TreeNode* EmplaceVal(const K& begin, const K& end, Args&&... val);

  // Insert an interval, the inserted interval is returned.
  TreeNode* InsertVal(const K& begin, const K& end, const V& val);

  // Insert an interval, the inserted interval is returned.
  TreeNode* InsertVal(const K& begin, const K& end, V&& val);

  // Insertion for V without copy constructors
  TreeNode* Insert(const K& begin, const K& end);

  // Delete the node.
  // WARNING: if you call Delete(iter->Get()); you invalidate the iterator.
  // But if you use iter->Delete(), the iterator is moved to the next node.
  void Delete(TreeNode* node);

  // Return the number of intervals in the tree
  int size() const { return size_ + pending_.size() + sorted_.size(); }

  // Return boolean indicating emptiness.
  bool empty() const { return size() == 0; }

  // Check all the invariants
  // For each node (except the root or leaf)
  // 1) begin <= end, end <= max_end_
  // 2) left->end <= max_end_, right->end <= max_end_
  // 3) left <= this <= right in Annotation Order
  // 4) left->parent = this = right->parent
  // 5) rank+1 >= rank->parent >= rank >= 1
  bool CheckInvariants() const {
    if (GetRoot() != nullptr) CHECK(GetRoot()->parent_ == nullptr);
    CHECK_EQ(CheckSubtree(GetRoot()), size_);
    return true;
  }

  // Update structure make sure the appropriate data structure is used, and
  // that data structure is up to date.
  void UpdateStructure();

  // Reset the data structure.
  //
  // WARNING: If an arena was supplied to the constructor then it will
  // not be reset.
  void Reset();

  // Returns the entire interval tree in string format
  //  each line of the string contains <min, max, max_end, location> tuple
  // This only works if type V has a stream serialization.
  std::string DebugString() const;

  // Create a copy of interval tree in linear time.
  // The caller needs to provide a value copy function.
  // The caller owns the returned interval tree.
  // Before calling this function, the interval tree to be copied from should
  // be up-to-date. Caller can ensure the tree is up to date by calling
  // UpdateStructure() explicitly.
  // Return a copy of interval tree on success.
  // If data structure is not up to date, then return nullptr.
  typedef std::function<void(const V& src_value, V* dst_value)>
      ValueCopyFunction;
  IntervalTree<K, V, KeyLess>* MakeLinearCopy(
      const ValueCopyFunction& value_copy_function,
      UnsafeArena* arena = nullptr) const;

  ~IntervalTree();

 private:
  friend class IntervalIterator<K, V, KeyLess>;
  friend class ConstIntervalIterator<K, V, KeyLess>;

  typedef std::deque<TreeNode*> ItemDeque;
  typedef std::vector<TreeNode*> ItemVector;

  // Members used only in tree-mode

  TreeNode* tree_root_;  // The root of IntervalTree
  int size_;  // The size of the tree (exclude those in sorted and pending)

  // deleted_ is a vector that keep track of all the deleted node
  // (useful because nodes are not deleted until the interval tree is deleted,
  //  if user uses Insert, the code will try to use deleted node first.)
  ItemDeque deleted_;

  // Members used in both tree-mode and vector-mode

  // The arena if this object owns it, or null otherwise.
  std::unique_ptr<UnsafeArena> owned_arena_;

  // This arena is used to store all the nodes.  This pointer does not own it.
  UnsafeArena* arena_;

  // The comparator for this interval tree.
  KeyLess key_less_;

  // pending_ vector contains all the lazy inserted nodes.
  ItemVector pending_;  // items to be sorted in vec or inserted in tree

  // Marker decides if the current interval tree contains overlap
  // Note: This is only an approximation, because it's expensive to maintain
  // the exact status.  (Duplicate nodes and endpoints overlap does not count.)
  enum Overlap { MAYBE_OVERLAP = 0, NO_OVERLAP, YES_OVERLAP };
  Overlap overlap_status_;

  // The data structure maintained by the interval tree.
  //  The code will automatically decide which data structure is best
  enum Structure { INTERVAL_TREE = 0, VECTOR = 1 };
  Structure structure_status_;

  // Store if the data structure is up to date
  bool uptodate_;

  K min_range_;
  K max_range_;  //  The minimum and the maximum range
  // Note: these are upper bound and lower bound, they might not be exact.
  // Note2: for speed, min_range_ is not properly updated in VECTOR mode
  // (since it can often be obtained by sorted_->front()->begin()

  // Member only used in vector-mode

  // sorted_ vector contains a list of nodes in sorted order.
  ItemVector sorted_;  // items sorted

  struct InternalConstructorNonce {};  // Distinguish this from the others.
  // Internal constructor that the public constructors delegate to.
  IntervalTree(InternalConstructorNonce nonce, UnsafeArena* arena,
               const KeyLess& less);

  // Deletes node appropriately depending on whether it was heap or arena
  // allocated.
  void DeleteNode(IntervalNode<K, V, KeyLess>* node) const {
    if (HasHeapAllocatedNodes()) {
      ::delete node;
    } else {
      delete node;
    }
  }

  // Returns true if the nodes in the tree are being allocated on the heap
  // instead of, as is typical, in an arena.
  bool HasHeapAllocatedNodes() const { return arena_ == nullptr; }

  // Is the current data structure vector? or tree?
  bool IsVector() const { return structure_status_ == VECTOR; }
  bool IsIntervalTree() const { return structure_status_ == INTERVAL_TREE; }

  // Change the data structure from vector to interval tree
  // The first call to this function is very expensive.  Later calls are cheap.
  void ChangeToIntervalTree() {
    structure_status_ = INTERVAL_TREE;
    uptodate_ = false;
    UpdateStructure();
  }

  // This is a helper function for UpdateStructure
  void UpdateVector();

  // This function copy nodes.
  IntervalNode<K, V, KeyLess>* CreateNodeCopy(
      const IntervalNode<K, V, KeyLess>& node_copy_from,
      const ValueCopyFunction& value_copy_function,
      UnsafeArena* node_arena) const;

  // Describe in note 3 on top of the file.
  struct AnnotationOrder;
  // Compares only the begin index.
  struct StartingOrder;

  // Helper function for Process Pending, this builds the tree with nodes in
  // pending_[begin] to pending_[end], then returns the root.
  TreeNode* BuildTree(const int begin, const int end) const;

  inline bool LessThan(const K& begin, const K& end,
                       const TreeNode* node) const;

  inline bool GreaterThan(const K& begin, const K& end,
                          const TreeNode* node) const;

  inline bool EqualTo(const K& begin, const K& end, const TreeNode* node) const;

  inline bool LessThanOrEqualTo(const K& begin, const K& end,
                                const TreeNode* node) const;

  // Returns true if lhs <= rhs.
  inline bool KeyLessThanOrEqualTo(const K& lhs, const K& rhs) const;

  // Returns true if lhs < rhs.
  inline bool KeyLessThan(const K& lhs, const K& rhs) const;

  // Returns true if lhs >= rhs.
  inline bool KeyGreaterThanOrEqualTo(const K& lhs, const K& rhs) const;

  // Returns true if lhs == rhs.
  inline bool KeyEqualTo(const K& lhs, const K& rhs) const;

  // Returns the greater of lhs and rhs.
  inline const K& MaxKey(const K& lhs, const K& rhs) const;

  // Returns the smaller of lhs and rhs.
  inline const K& MinKey(const K& lhs, const K& rhs) const;

  // Check if the range (beg1, end1) overlap with (beg2, end2)
  // This inline gives about 6% improvement when profiled on segmenter_unittest
  inline bool Intersect(const K& begin1, const K& end1, const K& begin2,
                        const K& end2) const;

  inline static bool IsLeftChild(const TreeNode* node);

  // Rotate child with child->parent
  //   Warning: Rotate does not update treeRoot pointer
  // Roughly 11% gain to inline rotate when testing regtest
  TreeNode* Rotate(TreeNode* const child) const;

  // Find the node closest in rank to (begin, end),
  //   Note: when there is duplicate, this finds the most recently inserted
  //         However, if most recent is false, it finds the earliest insertion.
  TreeNode* Find(const K& begin, const K& end, TreeNode* const tree,
                 bool most_recent) const;

  // The red-black tree operations
  TreeNode* RBInsert(TreeNode* newInterval, TreeNode* const tree);

  // Insert the node into the appropriate data structure (lazily)
  TreeNode* SmartInsert(TreeNode* const newNode);

  // (For INTERVAL_TREE mode only)
  // Find the smallest node with [begin, end] range in tree's subtree
  //   a node with smaller begin index is smaller
  //   for nodes with the same begin index, wider interval is smaller
  //   for nodes with same begin and end index, earlier insertion is smaller
  TreeNode* FindSmallest(const K& begin, const K& end, TreeNode* const tree);

  // (For INTERVAL_TREE mode only)
  // Find the largest node with [begin, end] range in tree's subtree
  TreeNode* FindLargest(const K& begin, const K& end, TreeNode* const tree);

  // (For VECTOR mode only) Find the node that intersect the range begin, end
  int VecFindIntersect(const K& begin, const K& end,
                       const bool startSmallest) const;

  // (For VECTOR mode only)
  // Find the node (begin, end) if exists, otherwise find the next element
  int VecFindPos(const K& begin, const K& end) const;

  // Check invariants of node's subtree, and return the size of node's subtree
  // This is a helper function for CheckInvariants.
  int CheckSubtree(TreeNode* node) const;

  // Create a string of all the intervals in the subtree for debugging purpose
  std::string DebugTreeString(TreeNode* node, std::string loc) const;

  // Get the root of the interval tree
  inline TreeNode* GetRoot() const;

  // This is the non-recursive version of destroy, and it only uses a constant
  // amount of additional memory.
  void IterativeDestroyTree();

  // Create a copy of new tree recursively.
  IntervalNode<K, V, KeyLess>* RecursiveCopyTree(
      const IntervalNode<K, V, KeyLess>* root_node,
      const ValueCopyFunction& value_copy_function, UnsafeArena* arena) const;
};

// Flag for initializing the iterator to smallest or largest in range
enum IntervalIteratorStart { INTERVAL_SMALLEST, INTERVAL_LARGEST };

template <typename K, typename V, typename KeyLess = std::less<K>>
class IntervalIterator {
 public:
  typedef IntervalIteratorStart IterStart;

  typedef IntervalTree<K, V, KeyLess> Tree;
  typedef IntervalNode<K, V, KeyLess> TreeNode;

  // Create an iterator that finds intervals intersecting range (begin, end).
  // When passing INTERVAL_SMALLEST or INTERVAL_LARGEST, the iterator is
  // initialized to the smallest or largest interval in the range, respectively.
  //
  // Note: When specifying INTERVAL_LARGEST, you have to call ::Prev() instead
  // of ::Next() to advance the iterator.
  IntervalIterator(Tree* tree, const K& begin, const K& end,
                   const IterStart start);

  // This is here for easy implementation of copy constructor in DocChart
  explicit IntervalIterator(const IntervalIterator<K, V, KeyLess>& rhs);

  // Initialized the iterator with a node.
  // This is here for easy implementation of NextAfter in DocChart.
  // The range is [min, max], if you want other range, call ResetRange.
  IntervalIterator(Tree* tree, TreeNode* node);

  // Initialized the iterator with the interval [int_begin, int_end] if exists.
  // (if there are duplicate, init to the smallest [int_begin, int_end])
  // Otherwise, init to the smallest element after [int_begin, int_end]. This
  // is here for efficient and easy implementation of NextAfter in DocChart.
  // WARNING:
  // The range is always [min, max], if you want other range, call ResetRange
  IntervalIterator(Tree* tree, const K& begin, const K& end);

  // Accessors.
  TreeNode* Get() const { return node_; }
  V* ptr() const { return node_->ptr(); }

  // Extra accessors for convenience
  inline const K& begin() const { return node_->begin; }
  inline const K& end() const { return node_->end; }
  inline const V& value() const { return node_->value; }
  inline Tree* tree() { return tree_; }

  // Find the next smallest node intersecting [begin, end].
  //   this includes nodes inserted after you initialized the iterator.
  // If you reaches the end, NULL is returned.
  // (Order defined by Annotation Order)
  TreeNode* Next();

  // Find the previous largest node,
  //   WARNING: this is slightly more expensive than Next()
  TreeNode* Prev();

  // Delete the current node, iterator is moved to the next node.
  TreeNode* Delete();

  // Delete the current node, iterator is moved to the previous node.
  // Because "it.Delete(); it.Prev();" is NOT identical to "it.PrevDelete();"
  //   (when you are deleting the last node, it.Delete() moves it to NULL,
  //    and you can not call it.Prev() on a NULL iterator.)
  TreeNode* PrevDelete();

  // Change the range of your iterator.
  //   Note: This does not reset the iterator.  Specifically, node->Get()
  //         stays the same before and after reset.
  //   Note2: You may set the range so that your current node is out of range
  void ResetRange(const K& begin, const K& end) {
    begin_ = begin;
    end_ = end;
  }

 private:
  Tree* tree_;
  TreeNode* node_;  // the last node returned
  K begin_, end_;

  // The position of the node in the vector (only valid when Interval Tree is
  // a vector).  Position_ = -1 if the size of the vector is 0
  // Note: I can not use a vector iterator here because the sorted_ vector in
  // interval tree could be reallocated, which invalidate the iterator.
  int position_;

  // Helper function for initialization
  void Init(const K& begin, const K& end, const IterStart start);

  // Get the node from the position_ (for vector mode)
  inline TreeNode* GetNodeFromPos();

  // Check if node_ and pos_ contradict (for vector mode)
  inline bool Valid();
};

// Use const_cast to avoid duplicate code.
template <typename K, typename V, typename KeyLess = std::less<K>>
class ConstIntervalIterator {
 public:
  typedef IntervalTree<K, V, KeyLess> Tree;
  typedef IntervalNode<K, V, KeyLess> TreeNode;

  ConstIntervalIterator(const Tree* tree, const K& begin, const K& end,
                        const typename IntervalIterator<K, V>::IterStart start)
      : it(const_cast<Tree*>(tree), begin, end, start) {}

  ConstIntervalIterator(const Tree* tree, const K& begin, const K& end)
      : it(const_cast<Tree*>(tree), begin, end) {}

  inline const K begin() const { return it.begin(); }
  inline const K end() const { return it.end(); }
  inline const V& value() const { return it.value(); }

  const TreeNode* const Get() const { return it.Get(); }
  const TreeNode* const Next() { return it.Next(); }
  const TreeNode* const Prev() { return it.Prev(); }
  void ResetRange(const K& begin, const K& end) { it.ResetRange(begin, end); }

 private:
  IntervalIterator<K, V, KeyLess> it;
};

// include method bodies
#include "gloop/util/intervaltree/intervaltree-inl.h"  // IWYU pragma: export

#endif  // THIRD_PARTY_GLOOP_UTIL_INTERVALTREE_INTERVALTREE_H__
