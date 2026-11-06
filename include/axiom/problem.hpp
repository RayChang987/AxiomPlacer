#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace axiom {

using Coord = std::int64_t;

/// A rectangular hard block (an analog device).
struct Block {
    std::string name;
    Coord width = 0;
    Coord height = 0;
};

/// A set of blocks that must be placed mirror-symmetric about one common axis.
struct SymmetryGroup {
    std::string name;
    std::vector<std::pair<int, int>> pairs;  ///< (block, mirror block)
    std::vector<int> selfs;                  ///< blocks centred on the axis

    int blockCount() const { return static_cast<int>(2 * pairs.size() + selfs.size()); }
};

/// A placement instance: blocks plus symmetry constraints.
struct Problem {
    std::string name;
    std::vector<Block> blocks;
    std::vector<SymmetryGroup> groups;
    /// groupOf[b] is the symmetry group containing block b, or -1.
    std::vector<int> groupOf;

    int blockCount() const { return static_cast<int>(blocks.size()); }
    int findBlock(const std::string& name) const;
    Coord totalBlockArea() const;

   private:
    friend Problem parseProblem(std::istream&, const std::string&);
    std::unordered_map<std::string, int> index_;
};

/// Parses the text benchmark format described in docs/FORMAT.md.
/// Throws std::runtime_error with a descriptive message on malformed input.
Problem parseProblem(std::istream& in, const std::string& name = "");
Problem loadProblem(const std::string& path);

}  // namespace axiom
