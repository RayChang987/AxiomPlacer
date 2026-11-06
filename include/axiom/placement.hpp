#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "axiom/problem.hpp"

namespace axiom {

struct Rect {
    Coord x = 0, y = 0, w = 0, h = 0;
    Coord right() const { return x + w; }
    Coord top() const { return y + h; }
};

/// Orientation of a symmetry axis line.
enum class Axis : unsigned char {
    Vertical,    ///< axis is the line x = c; pairs are mirrored left/right
    Horizontal,  ///< axis is the line y = c; pairs are mirrored up/down
};

inline Axis flipped(Axis a) { return a == Axis::Vertical ? Axis::Horizontal : Axis::Vertical; }
inline const char* toString(Axis a) { return a == Axis::Vertical ? "vertical" : "horizontal"; }

/// Absolute positions of all blocks.
struct Placement {
    std::vector<Rect> rects;     ///< indexed by block id; w/h already account for rotation
    std::vector<char> rotated;   ///< 1 if the block is rotated by 90 degrees
    std::vector<Axis> axes;      ///< indexed by group id
    std::vector<Coord> axis2;    ///< twice the axis coordinate (axes may sit on half units)
    Coord width = 0;
    Coord height = 0;

    Coord area() const { return width * height; }
};

/// Writes the result format described in docs/FORMAT.md.
void writePlacement(std::ostream& out, const Problem& problem, const Placement& placement);
void savePlacement(const std::string& path, const Problem& problem, const Placement& placement);

}  // namespace axiom
