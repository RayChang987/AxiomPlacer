#include "axiom/skyline.hpp"

#include <algorithm>

namespace axiom {

void Skyline::reset(Coord extent) {
    extent_ = std::max<Coord>(extent, 1);
    pool_.clear();
    newNode(0, false);
}

int Skyline::newNode(Coord value, bool tagged) {
    Node n;
    n.max = value;
    n.tag = value;
    n.tagged = tagged;
    pool_.push_back(n);
    return static_cast<int>(pool_.size()) - 1;
}

Coord Skyline::query(Coord l, Coord r) const {
    l = std::max<Coord>(l, 0);
    r = std::min(r, extent_);
    if (l >= r) return 0;
    return query(0, 0, extent_, l, r);
}

Coord Skyline::query(int node, Coord lo, Coord hi, Coord l, Coord r) const {
    if (node < 0) return 0;
    const Node& n = pool_[node];
    if (n.tagged) return n.tag;  // the whole range has one height
    if (l <= lo && hi <= r) return n.max;
    const Coord mid = lo + (hi - lo) / 2;
    Coord best = 0;
    if (l < mid) best = std::max(best, query(n.child[0], lo, mid, l, r));
    if (r > mid) best = std::max(best, query(n.child[1], mid, hi, l, r));
    return best;
}

void Skyline::assign(Coord l, Coord r, Coord h) {
    l = std::max<Coord>(l, 0);
    r = std::min(r, extent_);
    if (l >= r) return;
    assign(0, 0, extent_, l, r, h);
}

void Skyline::assign(int node, Coord lo, Coord hi, Coord l, Coord r, Coord h) {
    if (l <= lo && hi <= r) {
        pool_[node].max = pool_[node].tag = h;
        pool_[node].tagged = true;
        return;
    }
    // Push a pending assignment (or materialise missing children) before descending.
    for (int side = 0; side < 2; ++side) {
        const Node& n = pool_[node];
        const bool tagged = n.tagged;
        const Coord tag = n.tag;
        if (pool_[node].child[side] < 0) {
            const int c = newNode(tagged ? tag : 0, tagged);
            pool_[node].child[side] = c;
        } else if (tagged) {
            Node& c = pool_[pool_[node].child[side]];
            c.max = c.tag = tag;
            c.tagged = true;
        }
    }
    pool_[node].tagged = false;

    const Coord mid = lo + (hi - lo) / 2;
    if (l < mid) assign(pool_[node].child[0], lo, mid, l, r, h);
    if (r > mid) assign(pool_[node].child[1], mid, hi, l, r, h);
    pool_[node].max = std::max(pool_[pool_[node].child[0]].max, pool_[pool_[node].child[1]].max);
}

}  // namespace axiom
