#include "axiom/report.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <ostream>
#include <stdexcept>

namespace axiom {

namespace {

std::string jsonString(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string xmlEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

// Categorical palette for symmetry groups (fill, stroke).
const char* kGroupFill[] = {"#4e79a7", "#f28e2b", "#e15759", "#76b7b2", "#59a14f",
                            "#edc948", "#b07aa1", "#ff9da7", "#9c755f", "#86bcb6"};
const char* kGroupStroke[] = {"#a9c6e8", "#ffc58a", "#ffaaa9", "#c4ecea", "#a8dda0",
                              "#fff0a8", "#e3bfd9", "#ffd3d8", "#d6b8a6", "#cdeae6"};
constexpr int kPalette = 10;

}  // namespace

TraceWriter::TraceWriter(const std::string& path, const Problem& problem, std::uint64_t seed)
    : out_(path), problem_(problem) {
    if (!out_) throw std::runtime_error("cannot write trace '" + path + "'");
    out_ << "{\"type\":\"problem\",\"name\":" << jsonString(problem.name) << ",\"seed\":" << seed
         << ",\"blockArea\":" << problem.totalBlockArea() << ",\"blocks\":[";
    for (int i = 0; i < problem.blockCount(); ++i) {
        const Block& b = problem.blocks[i];
        out_ << (i ? "," : "") << '[' << jsonString(b.name) << ',' << b.width << ',' << b.height << ']';
    }
    out_ << "],\"groups\":[";
    for (size_t g = 0; g < problem.groups.size(); ++g) {
        const SymmetryGroup& grp = problem.groups[g];
        out_ << (g ? "," : "") << "{\"name\":" << jsonString(grp.name) << ",\"pairs\":[";
        for (size_t k = 0; k < grp.pairs.size(); ++k)
            out_ << (k ? "," : "") << '[' << grp.pairs[k].first << ',' << grp.pairs[k].second << ']';
        out_ << "],\"selfs\":[";
        for (size_t k = 0; k < grp.selfs.size(); ++k) out_ << (k ? "," : "") << grp.selfs[k];
        out_ << "]}";
    }
    out_ << "]}\n";
    out_.flush();
}

void TraceWriter::frame(const Progress& p, const Placement& pl) {
    out_ << "{\"type\":\"" << (p.final ? "final" : "frame") << "\",\"iter\":" << p.iteration
         << ",\"t\":" << std::fixed << std::setprecision(3) << p.elapsed << ",\"progress\":" << std::setprecision(4)
         << p.fraction << ",\"stage\":" << p.stage << ",\"temp\":" << std::scientific << std::setprecision(3)
         << p.temperature << std::fixed << std::setprecision(4) << ",\"accept\":" << p.acceptRate
         << ",\"cost\":" << p.currentCost << ",\"best\":" << p.bestCost << ",\"w\":" << pl.width
         << ",\"h\":" << pl.height << ",\"rects\":[";
    for (size_t i = 0; i < pl.rects.size(); ++i) {
        const Rect& r = pl.rects[i];
        out_ << (i ? "," : "") << r.x << ',' << r.y << ',' << r.w << ',' << r.h;
    }
    out_ << "],\"axes\":[";
    for (size_t g = 0; g < pl.axes.size(); ++g)
        out_ << (g ? "," : "") << "[\"" << (pl.axes[g] == Axis::Vertical ? 'v' : 'h') << "\"," << pl.axis2[g]
             << ']';
    out_ << "]}\n";
    out_.flush();
}

void writeSvg(std::ostream& out, const Problem& problem, const Placement& pl, const SvgOptions& opt) {
    const double W = static_cast<double>(std::max<Coord>(pl.width, 1));
    const double H = static_cast<double>(std::max<Coord>(pl.height, 1));
    const double scale = opt.maxSize / std::max(W, H);
    const double pad = 24, header = 56;
    const double imgW = W * scale + 2 * pad, imgH = H * scale + 2 * pad + header;
    auto X = [&](double x) { return pad + x * scale; };
    auto Y = [&](double y) { return header + pad + (H - y) * scale; };  // flip: y grows upwards

    const double util = 100.0 * static_cast<double>(problem.totalBlockArea()) / std::max<double>(1, pl.area());
    out << std::fixed << std::setprecision(2);
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << imgW << "\" height=\"" << imgH
        << "\" viewBox=\"0 0 " << imgW << ' ' << imgH << "\" font-family=\"Inter,Segoe UI,Helvetica,Arial,sans-serif\">\n";
    out << "<rect width=\"100%\" height=\"100%\" rx=\"14\" fill=\"#0f1419\"/>\n";
    const std::string title = opt.title.empty() ? problem.name : opt.title;
    out << "<text x=\"" << pad << "\" y=\"34\" fill=\"#e6edf3\" font-size=\"18\" font-weight=\"600\">"
        << xmlEscape(title) << "</text>\n";
    out << "<text x=\"" << imgW - pad << "\" y=\"34\" fill=\"#8b98a5\" font-size=\"13\" text-anchor=\"end\">"
        << problem.blockCount() << " blocks · " << problem.groups.size() << " sym groups · area "
        << pl.area() << " · util " << std::setprecision(1) << util << "%</text>\n"
        << std::setprecision(2);

    // Outline
    out << "<rect x=\"" << X(0) << "\" y=\"" << Y(H) << "\" width=\"" << W * scale << "\" height=\"" << H * scale
        << "\" fill=\"#151b23\" stroke=\"#30363d\" stroke-dasharray=\"4 4\"/>\n";

    for (int i = 0; i < problem.blockCount(); ++i) {
        const Rect& r = pl.rects[i];
        const int g = problem.groupOf[i];
        const char* fill = g < 0 ? "#2b3440" : kGroupFill[g % kPalette];
        const char* stroke = g < 0 ? "#57606a" : kGroupStroke[g % kPalette];
        const double opacity = g < 0 ? 1.0 : 0.88;
        out << "<rect x=\"" << X(r.x) << "\" y=\"" << Y(r.top()) << "\" width=\"" << r.w * scale << "\" height=\""
            << r.h * scale << "\" fill=\"" << fill << "\" fill-opacity=\"" << opacity << "\" stroke=\"" << stroke
            << "\" stroke-width=\"1\"><title>" << xmlEscape(problem.blocks[i].name) << " (" << r.x << ", " << r.y
            << ") " << r.w << "x" << r.h << (pl.rotated[i] ? " rotated" : "") << "</title></rect>\n";
        if (opt.labels) {
            const double fs = std::min({r.w * scale / (0.62 * std::max<size_t>(problem.blocks[i].name.size(), 1)),
                                        r.h * scale * 0.5, 12.0});
            if (fs >= 6)
                out << "<text x=\"" << X(r.x + r.w / 2.0) << "\" y=\"" << Y(r.y + r.h / 2.0) + fs * 0.35
                    << "\" font-size=\"" << fs << "\" fill=\"" << (g < 0 ? "#adbac7" : "#0b0f14")
                    << "\" text-anchor=\"middle\">" << xmlEscape(problem.blocks[i].name) << "</text>\n";
        }
    }

    if (opt.axes) {
        for (size_t g = 0; g < pl.axes.size(); ++g) {
            const SymmetryGroup& grp = problem.groups[g];
            Coord lo = std::numeric_limits<Coord>::max(), hi = std::numeric_limits<Coord>::min();
            auto extend = [&](int b) {
                const Rect& r = pl.rects[b];
                lo = std::min(lo, pl.axes[g] == Axis::Vertical ? r.y : r.x);
                hi = std::max(hi, pl.axes[g] == Axis::Vertical ? r.top() : r.right());
            };
            for (const auto& pr : grp.pairs) extend(pr.first), extend(pr.second);
            for (int s : grp.selfs) extend(s);
            const double a = pl.axis2[g] / 2.0;
            const char* c = kGroupStroke[g % kPalette];
            if (pl.axes[g] == Axis::Vertical)
                out << "<line x1=\"" << X(a) << "\" y1=\"" << Y(hi) - 6 << "\" x2=\"" << X(a) << "\" y2=\""
                    << Y(lo) + 6 << "\"";
            else
                out << "<line x1=\"" << X(lo) - 6 << "\" y1=\"" << Y(a) << "\" x2=\"" << X(hi) + 6 << "\" y2=\""
                    << Y(a) << "\"";
            out << " stroke=\"" << c << "\" stroke-width=\"1.6\" stroke-dasharray=\"6 4\"/>\n";
        }
    }
    out << "</svg>\n";
}

void saveSvg(const std::string& path, const Problem& problem, const Placement& placement, const SvgOptions& opt) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot write '" + path + "'");
    writeSvg(out, problem, placement, opt);
}

}  // namespace axiom
