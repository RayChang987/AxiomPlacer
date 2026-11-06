#include "axiom/placement.hpp"

#include <fstream>
#include <ostream>
#include <stdexcept>

namespace axiom {

void writePlacement(std::ostream& out, const Problem& problem, const Placement& placement) {
    out << "Area " << placement.area() << '\n';
    out << "NumHardBlocks " << problem.blockCount() << '\n';
    for (int i = 0; i < problem.blockCount(); ++i) {
        const Rect& r = placement.rects[i];
        out << problem.blocks[i].name << ' ' << r.x << ' ' << r.y << ' '
            << (placement.rotated[i] ? 1 : 0) << '\n';
    }
}

void savePlacement(const std::string& path, const Problem& problem, const Placement& placement) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot write '" + path + "'");
    writePlacement(out, problem, placement);
}

}  // namespace axiom
