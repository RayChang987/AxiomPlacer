// AxiomPlacer command-line front end.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <string>

#include "axiom/annealer.hpp"
#include "axiom/floorplan.hpp"
#include "axiom/placement.hpp"
#include "axiom/problem.hpp"
#include "axiom/report.hpp"

namespace {

constexpr const char* kVersion = "1.0.0";

void usage(std::FILE* f) {
    std::fprintf(f,
                 "AxiomPlacer %s - symmetry-aware analog placement\n"
                 "\n"
                 "Usage: axiom-placer <input.txt> <output.out> [options]\n"
                 "\n"
                 "Options:\n"
                 "  -t, --time <sec>      time budget in seconds (default: 30)\n"
                 "  -n, --iters <n>       iteration budget instead of time (deterministic with --seed)\n"
                 "  -s, --seed <n>        random seed (default: random)\n"
                 "      --trace <file>    record the optimisation as JSON Lines for viewer/index.html\n"
                 "      --trace-fps <n>   snapshots per second in the trace (default: 10)\n"
                 "      --svg <file>      render the final placement as SVG\n"
                 "      --stats           print per-move acceptance statistics\n"
                 "  -q, --quiet           only print the final result\n"
                 "  -h, --help            show this help\n"
                 "  -V, --version         show version\n",
                 kVersion);
}

struct Args {
    std::string input, output, trace, svg;
    double time = 30;
    std::uint64_t iters = 0;
    std::uint64_t seed = 0;
    bool seeded = false;
    double traceFps = 10;
    bool quiet = false;
    bool stats = false;
};

[[noreturn]] void die(const std::string& msg) {
    std::fprintf(stderr, "axiom-placer: %s\n", msg.c_str());
    std::exit(2);
}

Args parseArgs(int argc, char** argv) {
    Args a;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) die("missing value for " + arg);
            return argv[++i];
        };
        auto number = [&](const std::string& v) {
            try {
                return std::stod(v);
            } catch (const std::exception&) {
                die("invalid number '" + v + "' for " + arg);
            }
        };
        if (arg == "-h" || arg == "--help") {
            usage(stdout);
            std::exit(0);
        } else if (arg == "-V" || arg == "--version") {
            std::printf("axiom-placer %s\n", kVersion);
            std::exit(0);
        } else if (arg == "-t" || arg == "--time") {
            a.time = number(value());
        } else if (arg == "-n" || arg == "--iters") {
            a.iters = static_cast<std::uint64_t>(number(value()));
        } else if (arg == "-s" || arg == "--seed") {
            a.seed = static_cast<std::uint64_t>(number(value()));
            a.seeded = true;
        } else if (arg == "--trace") {
            a.trace = value();
        } else if (arg == "--trace-fps") {
            a.traceFps = number(value());
        } else if (arg == "--svg") {
            a.svg = value();
        } else if (arg == "--stats") {
            a.stats = true;
        } else if (arg == "-q" || arg == "--quiet") {
            a.quiet = true;
        } else if (!arg.empty() && arg[0] == '-') {
            die("unknown option " + arg + " (see --help)");
        } else {
            positional.push_back(arg);
        }
    }
    if (positional.size() != 2) {
        usage(stderr);
        std::exit(2);
    }
    a.input = positional[0];
    a.output = positional[1];
    if (a.time <= 0) die("--time must be positive");
    if (a.traceFps <= 0) die("--trace-fps must be positive");
    if (!a.seeded) a.seed = std::random_device{}();
    return a;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace axiom;
    const Args args = parseArgs(argc, argv);

    try {
        const Problem problem = loadProblem(args.input);
        Floorplanner planner(problem);
        for (const auto& w : planner.warnings()) std::fprintf(stderr, "warning: %s\n", w.c_str());

        if (!args.quiet) {
            std::fprintf(stderr, "AxiomPlacer %s | %s: %d blocks, %zu symmetry groups | seed %llu\n", kVersion,
                         problem.name.c_str(), problem.blockCount(), problem.groups.size(),
                         static_cast<unsigned long long>(args.seed));
        }

        AnnealOptions opt;
        opt.timeLimit = args.time;
        opt.iterations = args.iters;
        opt.seed = args.seed;

        std::mt19937_64 initRng(args.seed ^ 0x9e3779b97f4a7c15ULL);
        FloorplanState init = planner.initialState(initRng);
        const Coord initArea = planner.evaluate(init);

        std::unique_ptr<TraceWriter> trace;
        if (!args.trace.empty()) trace = std::make_unique<TraceWriter>(args.trace, problem, args.seed);

        const double blockArea = static_cast<double>(problem.totalBlockArea());
        double lastPrint = -1;
        auto observer = [&](const Progress& p, const FloorplanState& current) {
            if (trace) trace->frame(p, planner.realize(current));
            if (!args.quiet && (p.elapsed - lastPrint >= 0.5 || p.final)) {
                lastPrint = p.elapsed;
                std::fprintf(stderr,
                             "\r  [%5.1f%%] stage %d  T=%-9.3g  area %-12lld best %-12lld util %5.1f%%  %6.1fk it",
                             100 * p.fraction, p.stage + 1, p.temperature, static_cast<long long>(p.currentCost),
                             static_cast<long long>(p.bestCost), 100 * blockArea / p.bestCost, p.iteration / 1e3);
                std::fflush(stderr);
            }
        };

        Annealer annealer(planner, opt);
        const auto t0 = std::chrono::steady_clock::now();
        const FloorplanState best = annealer.run(std::move(init), observer, 1.0 / args.traceFps);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (!args.quiet) std::fprintf(stderr, "\n");

        const Placement result = planner.realize(best);
        const auto errors = validate(problem, result);
        savePlacement(args.output, problem, result);
        if (!args.svg.empty()) {
            SvgOptions so;
            so.title = "AxiomPlacer · " + problem.name;
            saveSvg(args.svg, problem, result, so);
        }

        if (args.stats) {
            std::fprintf(stderr, "  %-22s %12s %12s %8s\n", "move", "tried", "accepted", "rate");
            for (int m = 0; m < static_cast<int>(Move::Count); ++m) {
                const auto t = annealer.tried()[m], a = annealer.accepted()[m];
                std::fprintf(stderr, "  %-22s %12llu %12llu %7.2f%%\n", toString(static_cast<Move>(m)),
                             static_cast<unsigned long long>(t), static_cast<unsigned long long>(a),
                             t ? 100.0 * a / t : 0.0);
            }
        }

        std::printf("%s: area %lld (%lld x %lld), utilization %.2f%%, initial %lld, %.1fs%s\n",
                    problem.name.c_str(), static_cast<long long>(result.area()),
                    static_cast<long long>(result.width), static_cast<long long>(result.height),
                    100.0 * blockArea / result.area(), static_cast<long long>(initArea), secs,
                    errors.empty() ? "" : "  [INVALID]");
        for (const auto& e : errors) std::fprintf(stderr, "error: %s\n", e.c_str());
        return errors.empty() ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "axiom-placer: %s\n", e.what());
        return 2;
    }
}
