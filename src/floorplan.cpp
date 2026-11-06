#include "axiom/floorplan.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace axiom {

namespace {

/// Size of a self-symmetric block's half along the axis normal.
Coord halfExtent(Coord e) { return std::max<Coord>(e / 2, 1); }

}  // namespace

Floorplanner::Floorplanner(const Problem& problem) : problem_(problem) {
    for (int b = 0; b < problem.blockCount(); ++b)
        if (problem.groupOf[b] < 0) freeBlocks_.push_back(b);

    Coord islandSkySpan = 1;
    for (int g = 0; g < static_cast<int>(problem.groups.size()); ++g) {
        const SymmetryGroup& grp = problem.groups[g];
        IslandSpec spec;
        spec.group = g;
        Coord span = 1;
        for (int b : grp.selfs) {
            const Block& blk = problem.blocks[b];
            IslandSpec::Rep rep;
            rep.block = b;
            // Rotating a self-symmetric block is only safe if it keeps the
            // parity of the extent across the axis (see parityConsistent()).
            rep.rotatable = (blk.width - blk.height) % 2 == 0;
            spec.reps.push_back(rep);
            span += std::max(blk.width, blk.height);
        }
        spec.selfCount = static_cast<int>(grp.selfs.size());
        for (const auto& pr : grp.pairs) {
            IslandSpec::Rep rep;
            rep.block = pr.first;
            rep.mirror = pr.second;
            spec.reps.push_back(rep);
            const Block& blk = problem.blocks[pr.first];
            span += std::max(blk.width, blk.height);
        }
        islandSkySpan = std::max(islandSkySpan, span);
        islands_.push_back(std::move(spec));
    }

    // An island can only be strictly symmetric if all its self-symmetric
    // blocks agree on the parity of their extent across the axis.
    strictParity_.assign(islands_.size(), 1);
    for (int i = 0; i < islandCount(); ++i) {
        IslandState probe;
        probe.rotated.assign(islands_[i].reps.size(), 0);
        bool any = false;
        for (Axis a : {Axis::Vertical, Axis::Horizontal}) {
            probe.axis = a;
            any = any || parityConsistent(i, probe);
        }
        if (!any) {
            strictParity_[i] = 0;
            warnings_.push_back("symmetry group '" + problem.groups[islands_[i].group].name +
                                "' mixes odd and even self-symmetric blocks; "
                                "some cannot be centred exactly on integer coordinates");
        }
    }

    Coord total = 1;
    for (const Block& b : problem.blocks) total += 2 * std::max(b.width, b.height);
    skyExtent_ = total + 2 * static_cast<Coord>(problem.groups.size());
    islandSkyExtent_ = islandSkySpan;
    sky_.reset(skyExtent_);
    islandSky_.reset(islandSkyExtent_);
}

void Floorplanner::blockSize(int block, bool rotated, Coord& w, Coord& h) const {
    const Block& b = problem_.blocks[block];
    w = rotated ? b.height : b.width;
    h = rotated ? b.width : b.height;
}

bool Floorplanner::parityConsistent(int island, const IslandState& st) const {
    const IslandSpec& spec = islands_[island];
    int parity = -1;
    for (int r = 0; r < spec.selfCount; ++r) {
        Coord w, h;
        blockSize(spec.reps[r].block, st.rotated[r], w, h);
        const int p = static_cast<int>((st.axis == Axis::Vertical ? w : h) % 2);
        if (parity < 0) parity = p;
        else if (parity != p) return false;
    }
    return true;
}

bool Floorplanner::islandValid(int island, const IslandState& st) const {
    const IslandSpec& spec = islands_[island];
    // Self-symmetric reps must touch the axis: with a vertical axis they have
    // to sit at x = 0 of the half plane (the right-chain from the root), with a
    // horizontal axis at y = 0 (the left-chain).
    const auto side = st.axis == Axis::Vertical ? BStarTree::kRight : BStarTree::kLeft;
    int onAxis = 0;
    for (int s = st.tree.root(); s != BStarTree::kNone; s = st.tree.child(s, side))
        if (spec.reps[st.tree.item(s)].self()) ++onAxis;
    if (onAxis != spec.selfCount) return false;
    return !strictParity_[island] || parityConsistent(island, st);
}

const IslandLayout& Floorplanner::layout(int island, const IslandState& st) {
    if (st.cache) return *st.cache;

    const IslandSpec& spec = islands_[island];
    const bool vertical = st.axis == Axis::Vertical;
    const int k = static_cast<int>(spec.reps.size());

    // Gap between the two halves: 1 when self-symmetric blocks have an odd
    // extent across the axis (the axis then lies on a half unit).
    Coord gap = 0;
    if (spec.selfCount > 0) {
        Coord w, h;
        blockSize(spec.reps[0].block, st.rotated[0], w, h);
        gap = (vertical ? w : h) % 2;
    }

    // 1. Pack the representative half plane with a plain B*-tree.
    half_.assign(k, Rect{});
    islandX_.assign(k, 0);
    islandW_.assign(k, 0);
    islandSky_.reset(islandSkyExtent_);
    st.tree.preorder(islandOrder_);
    Coord halfW = 0, halfH = 0;
    for (int s : islandOrder_) {
        const int par = st.tree.parent(s);
        const Coord x = par == BStarTree::kNone ? 0
                        : st.tree.left(par) == s ? islandX_[par] + islandW_[par]
                                                 : islandX_[par];
        const int r = st.tree.item(s);
        Coord w, h;
        blockSize(spec.reps[r].block, st.rotated[r], w, h);
        if (spec.reps[r].self()) (vertical ? w : h) = halfExtent(vertical ? w : h);
        const Coord y = islandSky_.query(x, x + w);
        islandSky_.assign(x, x + w, y + h);
        half_[r] = Rect{x, y, w, h};
        islandX_[s] = x;
        islandW_[s] = w;
        halfW = std::max(halfW, x + w);
        halfH = std::max(halfH, y + h);
    }

    // 2. Mirror it across the axis.
    auto L = std::make_shared<IslandLayout>();
    L->width = vertical ? 2 * halfW + gap : halfW;
    L->height = vertical ? halfH : 2 * halfH + gap;
    L->axis2 = vertical ? 2 * halfW + gap : 2 * halfH + gap;

    struct Item {
        int block;
        Rect rect;
        char rotated;
    };
    std::vector<Item> items;
    items.reserve(2 * k);
    for (int r = 0; r < k; ++r) {
        const IslandSpec::Rep& rep = spec.reps[r];
        const Rect& hr = half_[r];
        Coord w, h;
        blockSize(rep.block, st.rotated[r], w, h);
        if (rep.self()) {
            const Rect full = vertical ? Rect{halfW - w / 2, hr.y, w, h} : Rect{hr.x, halfH - h / 2, w, h};
            items.push_back({rep.block, full, st.rotated[r]});
        } else if (vertical) {
            items.push_back({rep.block, Rect{halfW + gap + hr.x, hr.y, w, h}, st.rotated[r]});
            items.push_back({rep.mirror, Rect{halfW - hr.x - w, hr.y, w, h}, st.rotated[r]});
        } else {
            items.push_back({rep.block, Rect{hr.x, halfH + gap + hr.y, w, h}, st.rotated[r]});
            items.push_back({rep.mirror, Rect{hr.x, halfH - hr.y - h, w, h}, st.rotated[r]});
        }
    }
    // Sorting by top edge lets the packer update the contour with plain
    // range-assignments: later (higher) rectangles overwrite lower ones.
    std::sort(items.begin(), items.end(),
              [](const Item& a, const Item& b) { return a.rect.top() < b.rect.top(); });
    for (const Item& it : items) {
        L->blocks.push_back(it.block);
        L->rects.push_back(it.rect);
        L->rotated.push_back(it.rotated);
    }
    st.cache = L;
    return *L;
}

template <bool kRealize>
Coord Floorplanner::pack(const FloorplanState& st, Placement* out) {
    const int n = moduleCount();
    sky_.reset(skyExtent_);
    st.top.preorder(order_);
    slotX_.assign(n, 0);
    slotW_.assign(n, 0);
    if (kRealize) {
        out->rects.assign(problem_.blockCount(), Rect{});
        out->rotated.assign(problem_.blockCount(), 0);
        out->axes.assign(problem_.groups.size(), Axis::Vertical);
        out->axis2.assign(problem_.groups.size(), 0);
    }

    Coord maxX = 0, maxY = 0;
    for (int s : order_) {
        const int par = st.top.parent(s);
        const Coord x = par == BStarTree::kNone ? 0
                        : st.top.left(par) == s ? slotX_[par] + slotW_[par]
                                                : slotX_[par];
        slotX_[s] = x;
        const int m = st.top.item(s);

        if (!isIsland(m)) {
            const int b = freeBlocks_[m];
            Coord w, h;
            blockSize(b, st.rotated[m], w, h);
            const Coord y = sky_.query(x, x + w);
            sky_.assign(x, x + w, y + h);
            slotW_[s] = w;
            maxX = std::max(maxX, x + w);
            maxY = std::max(maxY, y + h);
            if (kRealize) {
                out->rects[b] = Rect{x, y, w, h};
                out->rotated[b] = st.rotated[m];
            }
            continue;
        }

        const int i = islandOfModule(m);
        const IslandLayout& L = layout(i, st.islands[i]);
        // Drop the island as low as its actual outline allows, rather than
        // resting its bounding box on the contour.
        Coord y = 0;
        for (const Rect& r : L.rects) y = std::max(y, sky_.query(x + r.x, x + r.right()) - r.y);
        for (const Rect& r : L.rects) sky_.assign(x + r.x, x + r.right(), y + r.top());
        slotW_[s] = L.width;
        maxX = std::max(maxX, x + L.width);
        maxY = std::max(maxY, y + L.height);
        if (kRealize) {
            for (size_t j = 0; j < L.blocks.size(); ++j) {
                const Rect& r = L.rects[j];
                out->rects[L.blocks[j]] = Rect{x + r.x, y + r.y, r.w, r.h};
                out->rotated[L.blocks[j]] = L.rotated[j];
            }
            const int g = islands_[i].group;
            out->axes[g] = st.islands[i].axis;
            out->axis2[g] = 2 * (st.islands[i].axis == Axis::Vertical ? x : y) + L.axis2;
        }
    }
    if (kRealize) {
        out->width = maxX;
        out->height = maxY;
    }
    return maxX * maxY;
}

Coord Floorplanner::evaluate(const FloorplanState& st) { return pack<false>(st, nullptr); }

Placement Floorplanner::realize(const FloorplanState& st) {
    Placement p;
    pack<true>(st, &p);
    return p;
}

FloorplanState Floorplanner::initialState(std::mt19937_64& rng) {
    FloorplanState st;

    // Islands: self-symmetric reps form the chain along the axis, pairs
    // continue the path in random directions.
    st.islands.resize(islands_.size());
    for (int i = 0; i < islandCount(); ++i) {
        const IslandSpec& spec = islands_[i];
        IslandState& is = st.islands[i];
        const int k = static_cast<int>(spec.reps.size());
        is.tree = BStarTree(k);
        is.rotated.assign(k, 0);
        is.axis = Axis::Vertical;
        if (strictParity_[i] && !parityConsistent(i, is)) is.axis = Axis::Horizontal;
        const auto chain = is.axis == Axis::Vertical ? BStarTree::kRight : BStarTree::kLeft;

        std::vector<int> items(k);
        std::iota(items.begin(), items.end(), 0);
        std::shuffle(items.begin() + spec.selfCount, items.end(), rng);
        std::vector<BStarTree::Side> sides(k, chain);
        for (int j = spec.selfCount; j < k; ++j)
            sides[j] = (rng() & 1) ? BStarTree::kLeft : BStarTree::kRight;
        is.tree.buildPath(items, sides);
    }

    // Top level: modules sorted by height, packed into rows whose width
    // targets a roughly square outline.
    const int n = moduleCount();
    std::vector<Coord> mw(n), mh(n);
    for (int m = 0; m < n; ++m) {
        if (isIsland(m)) {
            const IslandLayout& L = layout(islandOfModule(m), st.islands[islandOfModule(m)]);
            mw[m] = L.width;
            mh[m] = L.height;
        } else {
            blockSize(freeBlocks_[m], false, mw[m], mh[m]);
        }
    }
    std::vector<int> modules(n);
    std::iota(modules.begin(), modules.end(), 0);
    std::stable_sort(modules.begin(), modules.end(), [&](int a, int b) { return mh[a] > mh[b]; });

    const double target = std::sqrt(1.2 * static_cast<double>(problem_.totalBlockArea()));
    std::vector<std::vector<int>> rows(1);
    double rowWidth = 0;
    for (int m : modules) {
        if (!rows.back().empty() && rowWidth + mw[m] > target) {
            rows.emplace_back();
            rowWidth = 0;
        }
        rows.back().push_back(m);
        rowWidth += static_cast<double>(mw[m]);
    }
    st.top = BStarTree(n);
    st.top.buildRows(rows);
    st.rotated.assign(n, 0);
    return st;
}

// ---------------------------------------------------------------------------

std::vector<std::string> validate(const Problem& problem, const Placement& pl) {
    std::vector<std::string> errors;
    auto err = [&](const std::string& m) {
        if (errors.size() < 50) errors.push_back(m);
    };
    const int n = problem.blockCount();
    if (static_cast<int>(pl.rects.size()) != n) {
        err("placement has wrong number of blocks");
        return errors;
    }

    Coord maxX = 0, maxY = 0;
    for (int i = 0; i < n; ++i) {
        const Block& b = problem.blocks[i];
        const Rect& r = pl.rects[i];
        const Coord w = pl.rotated[i] ? b.height : b.width;
        const Coord h = pl.rotated[i] ? b.width : b.height;
        if (r.w != w || r.h != h) err("block '" + b.name + "' has inconsistent size/rotation");
        if (r.x < 0 || r.y < 0) err("block '" + b.name + "' has negative coordinates");
        maxX = std::max(maxX, r.right());
        maxY = std::max(maxY, r.top());
    }
    if (maxX != pl.width || maxY != pl.height) err("reported outline does not match the blocks");

    // Overlap check: sweep over x so the common case is near-linear.
    std::vector<int> byX(n);
    std::iota(byX.begin(), byX.end(), 0);
    std::sort(byX.begin(), byX.end(), [&](int a, int b) { return pl.rects[a].x < pl.rects[b].x; });
    for (int a = 0; a < n; ++a) {
        const Rect& ra = pl.rects[byX[a]];
        for (int b = a + 1; b < n && pl.rects[byX[b]].x < ra.right(); ++b) {
            const Rect& rb = pl.rects[byX[b]];
            if (rb.y < ra.top() && ra.y < rb.top())
                err("blocks '" + problem.blocks[byX[a]].name + "' and '" + problem.blocks[byX[b]].name +
                    "' overlap");
        }
    }

    // Symmetry: every group must be symmetric about one vertical or one
    // horizontal axis. Work in doubled coordinates so half units are exact.
    for (const SymmetryGroup& g : problem.groups) {
        bool ok = false;
        for (Axis axis : {Axis::Vertical, Axis::Horizontal}) {
            const bool v = axis == Axis::Vertical;
            auto c2 = [&](int b) {  // doubled centre across the axis
                const Rect& r = pl.rects[b];
                return v ? 2 * r.x + r.w : 2 * r.y + r.h;
            };
            auto along = [&](int b) { return v ? pl.rects[b].y : pl.rects[b].x; };
            bool good = true;
            Coord axis2 = 0;
            bool haveAxis = false;
            auto onAxis = [&](Coord value2) {
                if (!haveAxis) {
                    axis2 = value2;
                    haveAxis = true;
                }
                return value2 == axis2;
            };
            for (const auto& pr : g.pairs) {
                const Rect &a = pl.rects[pr.first], &b = pl.rects[pr.second];
                if (a.w != b.w || a.h != b.h || along(pr.first) != along(pr.second)) good = false;
                if (!onAxis((c2(pr.first) + c2(pr.second)) / 2)) good = false;
                if ((c2(pr.first) + c2(pr.second)) % 2 != 0) good = false;
            }
            for (int s : g.selfs)
                if (!onAxis(c2(s))) good = false;
            if (good) {
                ok = true;
                break;
            }
        }
        if (!ok) err("symmetry group '" + g.name + "' is not symmetric");
    }
    return errors;
}

}  // namespace axiom
