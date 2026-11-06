#include "axiom/annealer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>

namespace axiom {

const char* toString(Move m) {
    switch (m) {
        case Move::TopShift: return "top-shift";
        case Move::TopSwap: return "top-swap";
        case Move::TopSwapChildren: return "top-swap-children";
        case Move::RotateBlock: return "rotate-block";
        case Move::IslandShift: return "island-shift";
        case Move::IslandSwap: return "island-swap";
        case Move::IslandSwapChildren: return "island-swap-children";
        case Move::IslandRotate: return "island-rotate";
        case Move::IslandFlipAxis: return "island-flip-axis";
        default: return "?";
    }
}

Annealer::Annealer(Floorplanner& planner, AnnealOptions options)
    : planner_(planner), opt_(std::move(options)), rng_(opt_.seed) {
    // Disable moves that cannot apply to this instance.
    auto w = opt_.moveWeights;
    auto zero = [&](Move m) { w[static_cast<int>(m)] = 0; };
    if (planner_.moduleCount() < 2) {
        zero(Move::TopShift);
        zero(Move::TopSwap);
        zero(Move::TopSwapChildren);
    }
    if (planner_.freeBlockCount() == 0) zero(Move::RotateBlock);
    if (planner_.islandCount() == 0) {
        zero(Move::IslandShift);
        zero(Move::IslandSwap);
        zero(Move::IslandSwapChildren);
        zero(Move::IslandRotate);
        zero(Move::IslandFlipAxis);
    }
    if (std::accumulate(w.begin(), w.end(), 0.0) <= 0) w[static_cast<int>(Move::IslandFlipAxis)] = 1;
    moveDist_ = std::discrete_distribution<int>(w.begin(), w.end());

    std::vector<double> iw;
    for (int i = 0; i < planner_.islandCount(); ++i)
        iw.push_back(static_cast<double>(planner_.island(i).reps.size()));
    if (iw.empty()) iw.push_back(1);
    islandDist_ = std::discrete_distribution<int>(iw.begin(), iw.end());
}

int Annealer::pickIsland() { return islandDist_(rng_); }

bool Annealer::shift(BStarTree& tree) {
    const int n = tree.size();
    if (n < 2) return false;
    int v = static_cast<int>(rng_() % (n - 1));
    if (v >= tree.root()) ++v;  // uniform over non-root slots
    tree.detach(v);

    // Mark the detached subtree.
    moving_.assign(n, 0);
    stack_.assign(1, v);
    while (!stack_.empty()) {
        const int s = stack_.back();
        stack_.pop_back();
        moving_[s] = 1;
        if (tree.left(s) != BStarTree::kNone) stack_.push_back(tree.left(s));
        if (tree.right(s) != BStarTree::kNone) stack_.push_back(tree.right(s));
    }

    // Every free child position outside the detached subtree is a candidate.
    slots_.clear();
    for (int s = 0; s < n; ++s) {
        if (moving_[s]) continue;
        if (tree.left(s) == BStarTree::kNone) slots_.emplace_back(s, BStarTree::kLeft);
        if (tree.right(s) == BStarTree::kNone) slots_.emplace_back(s, BStarTree::kRight);
    }
    const auto& pick = slots_[rng_() % slots_.size()];
    tree.attach(v, pick.first, static_cast<BStarTree::Side>(pick.second));
    return true;
}

bool Annealer::perturb(FloorplanState& st, Move move) {
    auto randint = [&](int n) { return static_cast<int>(rng_() % static_cast<std::uint64_t>(n)); };
    const int nm = planner_.moduleCount();

    switch (move) {
        case Move::TopShift: return shift(st.top);
        case Move::TopSwap: {
            if (nm < 2) return false;
            const int a = randint(nm);
            int b = randint(nm - 1);
            if (b >= a) ++b;
            st.top.swapItems(a, b);
            return true;
        }
        case Move::TopSwapChildren: {
            const int s = randint(nm);
            if (st.top.left(s) == BStarTree::kNone && st.top.right(s) == BStarTree::kNone) return false;
            st.top.swapChildren(s);
            return true;
        }
        case Move::RotateBlock: {
            if (planner_.freeBlockCount() == 0) return false;
            const int m = randint(planner_.freeBlockCount());
            const Block& b = planner_.problem().blocks[planner_.blockOfModule(m)];
            if (b.width == b.height) return false;
            st.rotated[m] ^= 1;
            return true;
        }
        default: break;
    }

    // Island moves.
    if (planner_.islandCount() == 0) return false;
    const int i = pickIsland();
    const IslandSpec& spec = planner_.island(i);
    IslandState& is = st.islands[i];
    const int k = is.tree.size();
    switch (move) {
        case Move::IslandShift:
            if (!shift(is.tree)) return false;
            break;
        case Move::IslandSwap: {
            if (k < 2) return false;
            const int a = randint(k);
            int b = randint(k - 1);
            if (b >= a) ++b;
            is.tree.swapItems(a, b);
            break;
        }
        case Move::IslandSwapChildren: {
            const int s = randint(k);
            if (is.tree.left(s) == BStarTree::kNone && is.tree.right(s) == BStarTree::kNone) return false;
            is.tree.swapChildren(s);
            break;
        }
        case Move::IslandRotate: {
            const int r = randint(k);
            const Block& b = planner_.problem().blocks[spec.reps[r].block];
            if (!spec.reps[r].rotatable || b.width == b.height) return false;
            is.rotated[r] ^= 1;
            break;
        }
        case Move::IslandFlipAxis:
            // Mirroring the tree turns the axis chain (right-chain <-> left-chain)
            // into the one required by the new axis.
            is.axis = flipped(is.axis);
            is.tree.mirror();
            break;
        default: return false;
    }
    is.touch();
    return planner_.islandValid(i, is);
}

FloorplanState Annealer::run(FloorplanState initial, const Observer& observer, double observeEvery) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto seconds = [&] { return std::chrono::duration<double>(Clock::now() - start).count(); };

    const bool byIterations = opt_.iterations > 0;
    std::uint64_t iter = 0;
    auto progress = [&](double elapsed) {
        return byIterations ? static_cast<double>(iter) / static_cast<double>(opt_.iterations)
                            : elapsed / std::max(opt_.timeLimit, 1e-9);
    };

    double shareSum = 0;
    for (const Stage& s : opt_.stages) shareSum += s.share;
    if (opt_.stages.empty() || shareSum <= 0) {
        opt_.stages = {Stage{}};
        shareSum = 1;
    }

    FloorplanState cur = std::move(initial);
    Coord curCost = planner_.evaluate(cur);
    FloorplanState best = cur, cand = cur;
    Coord bestCost = curCost;

    Progress pr;
    std::uint64_t windowTried = 0, windowAccepted = 0;
    double lastObserved = -1e9;
    auto observe = [&](double elapsed, bool force) {
        if (!observer || (!force && elapsed - lastObserved < observeEvery)) return;
        lastObserved = elapsed;
        pr.iteration = iter;
        pr.elapsed = elapsed;
        pr.fraction = std::min(1.0, progress(elapsed));
        pr.currentCost = curCost;
        pr.bestCost = bestCost;
        pr.acceptRate = windowTried ? static_cast<double>(windowAccepted) / windowTried : 0;
        windowTried = windowAccepted = 0;
        observer(pr, cur);
    };

    std::uniform_real_distribution<double> unit(0.0, 1.0);
    double stageBegin = 0;
    for (int k = 0; k < static_cast<int>(opt_.stages.size()); ++k) {
        const Stage& stage = opt_.stages[k];
        const double stageEnd = k + 1 == static_cast<int>(opt_.stages.size())
                                    ? 1.0
                                    : stageBegin + stage.share / shareSum;
        cur = best;  // every stage restarts from the best solution so far
        curCost = bestCost;
        pr.stage = k;

        while (true) {
            const double elapsed = seconds();
            const double frac = progress(elapsed);
            if (frac >= stageEnd) break;
            const double t = std::clamp((frac - stageBegin) / std::max(stageEnd - stageBegin, 1e-12), 0.0, 1.0);
            const double temp = stage.startTemp * std::pow(stage.endTemp / stage.startTemp, t);
            pr.temperature = temp;
            observe(elapsed, false);

            ++iter;
            cand = cur;
            const auto move = static_cast<Move>(moveDist_(rng_));
            ++tried_[static_cast<int>(move)];
            if (!perturb(cand, move)) continue;

            const Coord cost = planner_.evaluate(cand);
            const double delta = (static_cast<double>(cost) - static_cast<double>(curCost)) /
                                 static_cast<double>(curCost) * opt_.costScale;
            ++windowTried;
            if (delta <= 0 || unit(rng_) < std::exp(-delta / temp)) {
                std::swap(cur, cand);
                curCost = cost;
                ++accepted_[static_cast<int>(move)];
                ++windowAccepted;
                if (cost < bestCost) {
                    bestCost = cost;
                    best = cur;
                }
            }
        }
        stageBegin = stageEnd;
        observe(seconds(), true);
    }

    if (observer) {
        cur = best;
        curCost = bestCost;
        pr.final = true;
        observe(seconds(), true);
    }
    return best;
}

}  // namespace axiom
