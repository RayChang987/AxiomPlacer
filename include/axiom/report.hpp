#pragma once

#include <fstream>
#include <iosfwd>
#include <string>

#include "axiom/annealer.hpp"
#include "axiom/placement.hpp"
#include "axiom/problem.hpp"

namespace axiom {

/// Streams the optimisation run as JSON Lines for the web viewer
/// (viewer/index.html). The first line describes the instance, every
/// following line is a snapshot of the current placement.
class TraceWriter {
   public:
    TraceWriter(const std::string& path, const Problem& problem, std::uint64_t seed);
    void frame(const Progress& progress, const Placement& placement);
    bool ok() const { return static_cast<bool>(out_); }

   private:
    std::ofstream out_;
    const Problem& problem_;
};

struct SvgOptions {
    int maxSize = 900;       ///< longest side of the image in pixels
    bool labels = true;
    bool axes = true;
    std::string title;
};

/// Renders a placement as a standalone SVG image.
void writeSvg(std::ostream& out, const Problem& problem, const Placement& placement, const SvgOptions& opt = {});
void saveSvg(const std::string& path, const Problem& problem, const Placement& placement, const SvgOptions& opt = {});

}  // namespace axiom
