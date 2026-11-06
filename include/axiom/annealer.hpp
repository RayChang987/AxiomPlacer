#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

#include "axiom/floorplan.hpp"

namespace axiom {

/// Perturbations applied to an HB*-tree.
enum class Move : int {
    TopShift,            ///< move a subtree of the top-level tree elsewhere
    TopSwap,             ///< exchange two modules
    TopSwapChildren,     ///< mirror the children of one top-level node
    RotateBlock,         ///< rotate a free block by 90 degrees
    IslandShift,         ///< move a subtree inside a symmetry island
    IslandSwap,          ///< exchange two representatives inside an island
    IslandSwapChildren,  ///< mirror the children of one island node
    IslandRotate,        ///< rotate a symmetry pair (or self-symmetric block)
    IslandFlipAxis,      ///< switch an island between vertical and horizontal axis
    Count
};

const char* toString(Move m);

/// One cooling stage. The temperature decays geometrically from `startTemp`
/// to `endTemp`; each stage restarts from the best solution found so far.
struct Stage {
    double share = 1.0;  ///< fraction of the total budget spent in this stage
    double startTemp = 1.0;
    double endTemp = 1e-3;
};

struct AnnealOptions {
    double timeLimit = 60.0;     ///< seconds; ignored when `iterations` > 0
    std::uint64_t iterations = 0;
    std::uint64_t seed = 0;
    /// Cost differences are measured relative to the current area and scaled
    /// by this factor before the Metropolis test.
    double costScale = 450.0;
    std::vector<Stage> stages = {
        {1.0, 20.0, 1e-2},
        {1.0, 10.0, 1e-3},
        {1.0, 1.0, 1e-3},
        {1.0, 1.0, 1e-6},
    };
    std::array<double, static_cast<int>(Move::Count)> moveWeights = {
        30,  // TopShift
        16,  // TopSwap
        4,   // TopSwapChildren
        10,  // RotateBlock
        16,  // IslandShift
        10,  // IslandSwap
        4,   // IslandSwapChildren
        6,   // IslandRotate
        4,   // IslandFlipAxis
    };
};

/// Snapshot passed to progress observers.
struct Progress {
    std::uint64_t iteration = 0;
    double elapsed = 0;      ///< seconds since start
    double fraction = 0;     ///< overall progress in [0, 1]
    int stage = 0;
    double temperature = 0;
    Coord currentCost = 0;
    Coord bestCost = 0;
    double acceptRate = 0;   ///< over the last reporting window
    bool final = false;
};

/// Simulated annealing over HB*-tree states.
class Annealer {
   public:
    using Observer = std::function<void(const Progress&, const FloorplanState& current)>;

    Annealer(Floorplanner& planner, AnnealOptions options);

    /// Runs the full schedule and returns the best state found. The observer
    /// (if any) is invoked roughly every `observeEvery` seconds and once at
    /// the very end with `final = true` and the best state.
    FloorplanState run(FloorplanState initial, const Observer& observer = {}, double observeEvery = 0.1);

    /// Applies one random move of the given kind. Returns false if the move
    /// was not applicable or produced an illegal island (state is then
    /// unspecified and must be discarded).
    bool perturb(FloorplanState& state, Move move);

    const std::array<std::uint64_t, static_cast<int>(Move::Count)>& tried() const { return tried_; }
    const std::array<std::uint64_t, static_cast<int>(Move::Count)>& accepted() const { return accepted_; }

   private:
    bool shift(BStarTree& tree);
    int pickIsland();

    Floorplanner& planner_;
    AnnealOptions opt_;
    std::mt19937_64 rng_;
    std::discrete_distribution<int> moveDist_;
    std::discrete_distribution<int> islandDist_;
    std::vector<std::pair<int, int>> slots_;  // scratch: (slot, side)
    std::vector<char> moving_;
    std::vector<int> stack_;
    std::array<std::uint64_t, static_cast<int>(Move::Count)> tried_{}, accepted_{};
};

}  // namespace axiom
