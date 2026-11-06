#pragma once

#include <memory>
#include <random>
#include <string>
#include <vector>

#include "axiom/bstar_tree.hpp"
#include "axiom/placement.hpp"
#include "axiom/problem.hpp"
#include "axiom/skyline.hpp"

namespace axiom {

/// Static description of a symmetry island (one per symmetry group).
///
/// Only one *representative* per symmetry pair is packed; the mirror block is
/// derived. Self-symmetric blocks are packed at half size and stretched across
/// the axis afterwards.
struct IslandSpec {
    struct Rep {
        int block = -1;
        int mirror = -1;  ///< mirror block of a pair, -1 for self-symmetric
        bool rotatable = true;
        bool self() const { return mirror < 0; }
    };
    int group = -1;
    std::vector<Rep> reps;  ///< self-symmetric reps first, then pairs
    int selfCount = 0;
};

/// Packed island in its own frame, origin at the lower-left corner.
struct IslandLayout {
    Coord width = 0, height = 0;
    Coord axis2 = 0;          ///< twice the axis coordinate within the island frame
    std::vector<int> blocks;  ///< block ids, ordered by increasing top edge
    std::vector<Rect> rects;  ///< island-local rectangles, same order as `blocks`
    std::vector<char> rotated;
};

/// Mutable part of an island: the half-plane B*-tree, axis and rotations.
struct IslandState {
    BStarTree tree;             ///< items are rep indices
    Axis axis = Axis::Vertical;
    std::vector<char> rotated;  ///< per rep

    /// Must be called after any mutation so the cached layout is rebuilt.
    void touch() { cache.reset(); }

    mutable std::shared_ptr<const IslandLayout> cache;
};

/// A complete solution: a top-level B*-tree whose items are *modules*
/// (free blocks and whole symmetry islands) plus one tree per island.
/// This is the hierarchical B*-tree (HB*-tree) representation.
struct FloorplanState {
    BStarTree top;
    std::vector<char> rotated;  ///< per module (ignored for islands)
    std::vector<IslandState> islands;
};

/// Turns FloorplanStates into coordinates. Holds scratch buffers, so it is
/// cheap to call repeatedly but not thread-safe.
class Floorplanner {
   public:
    explicit Floorplanner(const Problem& problem);

    const Problem& problem() const { return problem_; }
    int moduleCount() const { return static_cast<int>(freeBlocks_.size() + islands_.size()); }
    int freeBlockCount() const { return static_cast<int>(freeBlocks_.size()); }
    int islandCount() const { return static_cast<int>(islands_.size()); }
    bool isIsland(int module) const { return module >= freeBlockCount(); }
    int islandOfModule(int module) const { return module - freeBlockCount(); }
    int blockOfModule(int module) const { return freeBlocks_[module]; }
    const IslandSpec& island(int i) const { return islands_[i]; }

    /// A reasonable starting point: islands as simple chains, modules packed
    /// into rows of roughly square aspect ratio.
    FloorplanState initialState(std::mt19937_64& rng);

    /// Packs and returns the bounding-box area (the cost function).
    Coord evaluate(const FloorplanState& state);

    /// Packs and returns full block coordinates.
    Placement realize(const FloorplanState& state);

    /// Whether an island state satisfies the structural constraints that make
    /// its symmetric expansion legal (self-symmetric blocks on the axis).
    bool islandValid(int island, const IslandState& state) const;

    const IslandLayout& layout(int island, const IslandState& state);

    /// Human-readable notes about the instance (e.g. odd-sized self-symmetric
    /// blocks that cannot be centred exactly).
    const std::vector<std::string>& warnings() const { return warnings_; }

   private:
    template <bool kRealize>
    Coord pack(const FloorplanState& state, Placement* out);
    bool parityConsistent(int island, const IslandState& state) const;
    void blockSize(int block, bool rotated, Coord& w, Coord& h) const;

    const Problem& problem_;
    std::vector<int> freeBlocks_;
    std::vector<IslandSpec> islands_;
    std::vector<char> strictParity_;
    std::vector<std::string> warnings_;

    // scratch
    Skyline sky_, islandSky_;
    Coord skyExtent_ = 1, islandSkyExtent_ = 1;
    std::vector<int> order_, islandOrder_;
    std::vector<Coord> slotX_, slotW_, islandX_, islandW_;
    std::vector<Rect> half_;
};

/// Returns a list of constraint violations (empty if the placement is legal).
std::vector<std::string> validate(const Problem& problem, const Placement& placement);

}  // namespace axiom
