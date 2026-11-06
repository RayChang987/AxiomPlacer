#include "axiom/bstar_tree.hpp"

#include <cassert>
#include <utility>

namespace axiom {

BStarTree::BStarTree(int n) : parent_(n, kNone), item_(n), slot_(n), child_(n) {
    for (int i = 0; i < n; ++i) item_[i] = slot_[i] = i;
    // Default topology: a single left chain (one row).
    root_ = n > 0 ? 0 : kNone;
    for (int i = 1; i < n; ++i) {
        parent_[i] = i - 1;
        child_[i - 1][kLeft] = i;
    }
}

void BStarTree::buildRows(const std::vector<std::vector<int>>& rows) {
    const int n = size();
    for (int s = 0; s < n; ++s) {
        parent_[s] = kNone;
        child_[s][kLeft] = child_[s][kRight] = kNone;
    }
    // Slot i holds the i-th item in row-major order.
    int next = 0;
    int prevRowHead = kNone;
    root_ = kNone;
    for (const auto& row : rows) {
        int prev = kNone;
        for (size_t k = 0; k < row.size(); ++k) {
            const int s = next++;
            item_[s] = row[k];
            slot_[row[k]] = s;
            if (k == 0) {
                if (prevRowHead == kNone) {
                    root_ = s;
                } else {
                    parent_[s] = prevRowHead;
                    child_[prevRowHead][kRight] = s;
                }
                prevRowHead = s;
            } else {
                parent_[s] = prev;
                child_[prev][kLeft] = s;
            }
            prev = s;
        }
    }
    assert(next == n && "rows must contain every item exactly once");
}

void BStarTree::buildPath(const std::vector<int>& items, const std::vector<Side>& sides) {
    const int n = size();
    assert(static_cast<int>(items.size()) == n && sides.size() == items.size());
    for (int s = 0; s < n; ++s) {
        parent_[s] = kNone;
        child_[s][kLeft] = child_[s][kRight] = kNone;
        item_[s] = items[s];
        slot_[items[s]] = s;
    }
    root_ = n > 0 ? 0 : kNone;
    for (int s = 1; s < n; ++s) {
        parent_[s] = s - 1;
        child_[s - 1][sides[s]] = s;
    }
}

void BStarTree::swapItems(int a, int b) {
    const int sa = slot_[a], sb = slot_[b];
    std::swap(item_[sa], item_[sb]);
    slot_[a] = sb;
    slot_[b] = sa;
}

void BStarTree::swapChildren(int slot) { std::swap(child_[slot][kLeft], child_[slot][kRight]); }

void BStarTree::mirror() {
    for (int s = 0; s < size(); ++s) swapChildren(s);
}

void BStarTree::detach(int slot) {
    const int p = parent_[slot];
    assert(p != kNone && "cannot detach the root");
    if (child_[p][kLeft] == slot) child_[p][kLeft] = kNone;
    else child_[p][kRight] = kNone;
    parent_[slot] = kNone;
}

void BStarTree::attach(int slot, int parentSlot, Side side) {
    assert(child_[parentSlot][side] == kNone);
    child_[parentSlot][side] = slot;
    parent_[slot] = parentSlot;
}

bool BStarTree::inSubtree(int slot, int ancestor) const {
    for (int s = slot; s != kNone; s = parent_[s])
        if (s == ancestor) return true;
    return false;
}

void BStarTree::preorder(std::vector<int>& out) const {
    out.clear();
    if (root_ == kNone) return;
    // Explicit stack: push right first so the left subtree is visited first.
    static thread_local std::vector<int> stack;
    stack.clear();
    stack.push_back(root_);
    while (!stack.empty()) {
        const int s = stack.back();
        stack.pop_back();
        out.push_back(s);
        if (child_[s][kRight] != kNone) stack.push_back(child_[s][kRight]);
        if (child_[s][kLeft] != kNone) stack.push_back(child_[s][kLeft]);
    }
}

bool BStarTree::consistent() const {
    if (size() == 0) return root_ == kNone;
    if (root_ == kNone || parent_[root_] != kNone) return false;
    for (int s = 0; s < size(); ++s) {
        if (slot_[item_[s]] != s) return false;
        for (int side = 0; side < 2; ++side) {
            const int c = child_[s][side];
            if (c != kNone && parent_[c] != s) return false;
        }
    }
    std::vector<int> order;
    preorder(order);
    return static_cast<int>(order.size()) == size();
}

}  // namespace axiom
