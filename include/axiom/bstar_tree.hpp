#pragma once

#include <vector>

namespace axiom {

/// B*-tree topology over n items.
///
/// The tree is made of n *slots* linked by parent/left/right indices, and
/// every slot holds exactly one *item* (a block, a symmetry island, ...).
/// Structural moves rearrange slots while item swaps only relabel them, which
/// keeps every perturbation O(1) or O(n) and makes the whole state cheap to
/// copy for simulated annealing.
///
/// Packing convention: the left child of a slot is placed immediately to the
/// right of it, the right child directly above it (same x).
class BStarTree {
   public:
    static constexpr int kNone = -1;
    enum Side { kLeft = 0, kRight = 1 };

    BStarTree() = default;
    explicit BStarTree(int n);

    int size() const { return static_cast<int>(item_.size()); }
    int root() const { return root_; }
    int parent(int slot) const { return parent_[slot]; }
    int child(int slot, Side s) const { return child_[slot][s]; }
    int left(int slot) const { return child_[slot][kLeft]; }
    int right(int slot) const { return child_[slot][kRight]; }
    int item(int slot) const { return item_[slot]; }
    int slotOf(int item) const { return slot_[item]; }

    /// Builds a tree from rows: each row is a left-chain of items and the first
    /// item of every row is the right child of the previous row's first item.
    void buildRows(const std::vector<std::vector<int>>& rows);

    /// Builds a single path: items[0] is the root and items[k] hangs under
    /// items[k-1] on sides[k] (sides[0] is ignored).
    void buildPath(const std::vector<int>& items, const std::vector<Side>& sides);

    /// Exchanges the positions of two items.
    void swapItems(int a, int b);

    /// Exchanges the left and right subtree of a slot.
    void swapChildren(int slot);

    /// Mirrors the whole tree (swaps children of every slot).
    void mirror();

    /// Cuts the subtree rooted at `slot` (not the root) from its parent.
    void detach(int slot);

    /// Hangs a detached subtree under `parentSlot` on the given (empty) side.
    void attach(int slot, int parentSlot, Side side);

    /// True if `slot` lies in the subtree rooted at `ancestor`.
    bool inSubtree(int slot, int ancestor) const;

    /// Slots visited in preorder (root, left subtree, right subtree).
    void preorder(std::vector<int>& out) const;

    /// Checks that links are mutually consistent and every slot is reachable.
    bool consistent() const;

   private:
    int root_ = kNone;
    std::vector<int> parent_;
    std::vector<int> item_, slot_;
    struct Children {
        int v[2] = {kNone, kNone};
        int& operator[](int i) { return v[i]; }
        int operator[](int i) const { return v[i]; }
    };
    std::vector<Children> child_;
};

}  // namespace axiom
