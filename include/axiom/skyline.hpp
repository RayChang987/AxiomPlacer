#pragma once

#include <vector>

#include "axiom/problem.hpp"

namespace axiom {

/// Contour ("skyline") of already-placed blocks along the x axis.
///
/// Implemented as a lazily-allocated segment tree with range-assign /
/// range-max, so both operations run in O(log extent) regardless of how
/// fragmented the contour gets. Nodes live in a pool that is recycled by
/// reset(), so packing does not allocate in the steady state.
class Skyline {
   public:
    explicit Skyline(Coord extent = 1) { reset(extent); }

    /// Clears the contour and sets the domain to [0, extent).
    void reset(Coord extent);

    /// Maximum height over [l, r). Returns 0 for an empty range.
    Coord query(Coord l, Coord r) const;

    /// Sets the height over [l, r) to h.
    void assign(Coord l, Coord r, Coord h);

    Coord extent() const { return extent_; }

   private:
    struct Node {
        Coord max = 0;
        Coord tag = 0;
        bool tagged = false;
        int child[2] = {-1, -1};
    };

    int newNode(Coord value, bool tagged);
    Coord query(int node, Coord lo, Coord hi, Coord l, Coord r) const;
    void assign(int node, Coord lo, Coord hi, Coord l, Coord r, Coord h);

    std::vector<Node> pool_;
    Coord extent_ = 1;
};

}  // namespace axiom
