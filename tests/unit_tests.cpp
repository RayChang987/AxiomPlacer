// Self-contained unit tests (no external framework required).

#include <algorithm>
#include <cstdio>
#include <functional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "axiom/annealer.hpp"
#include "axiom/bstar_tree.hpp"
#include "axiom/floorplan.hpp"
#include "axiom/problem.hpp"
#include "axiom/skyline.hpp"

using namespace axiom;

namespace {

int failures = 0;
int checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++checks;                                                                \
        if (!(cond)) {                                                           \
            ++failures;                                                          \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

void testSkylineAgainstBruteForce() {
    std::mt19937_64 rng(7);
    const Coord extent = 257;
    Skyline sky(extent);
    std::vector<Coord> ref(extent, 0);
    for (int step = 0; step < 20000; ++step) {
        Coord l = rng() % extent, r = rng() % (extent + 1);
        if (l > r) std::swap(l, r);
        if (rng() % 2) {
            const Coord h = rng() % 1000;
            sky.assign(l, r, h);
            for (Coord x = l; x < r; ++x) ref[x] = h;
        } else {
            Coord expect = 0;
            for (Coord x = l; x < r; ++x) expect = std::max(expect, ref[x]);
            CHECK(sky.query(l, r) == expect);
        }
        if (step % 5000 == 4999) {
            sky.reset(extent);
            std::fill(ref.begin(), ref.end(), 0);
        }
    }
}

void testBStarTreeMoves() {
    std::mt19937_64 rng(11);
    BStarTree t(12);
    t.buildRows({{0, 1, 2, 3}, {4, 5, 6}, {7, 8, 9, 10, 11}});
    CHECK(t.consistent());
    for (int step = 0; step < 5000; ++step) {
        const int a = rng() % 12, b = rng() % 12;
        switch (rng() % 4) {
            case 0: t.swapItems(a, b); break;
            case 1: t.swapChildren(a); break;
            case 2: t.mirror(); break;
            case 3: {
                if (a == t.root()) break;
                t.detach(a);
                for (int s = 0; s < 12; ++s) {
                    if (t.inSubtree(s, a)) continue;
                    if (t.left(s) == BStarTree::kNone) {
                        t.attach(a, s, BStarTree::kLeft);
                        break;
                    }
                    if (t.right(s) == BStarTree::kNone) {
                        t.attach(a, s, BStarTree::kRight);
                        break;
                    }
                }
                break;
            }
        }
        CHECK(t.consistent());
    }
}

const char* kInstance = R"(
NumHardBlocks 11
HardBlock a 30 20
HardBlock b 30 20
HardBlock c 14 40
HardBlock d 14 40
HardBlock s 21 11
HardBlock t 13 9
HardBlock u 25 25
HardBlock v 40 10
HardBlock w 7 33
HardBlock e 12 18
HardBlock f 12 18
NumSymGroups 2
SymGroup g0 4
SymPair a b
SymPair c d
SymSelf s
SymSelf t
SymGroup g1 1
SymPair e f
)";

void testParser() {
    std::istringstream in(kInstance);
    const Problem p = parseProblem(in, "unit");
    CHECK(p.blockCount() == 11);
    CHECK(p.groups.size() == 2);
    CHECK(p.groups[0].pairs.size() == 2 && p.groups[0].selfs.size() == 2);
    CHECK(p.groupOf[p.findBlock("u")] == -1);
    CHECK(p.groupOf[p.findBlock("e")] == 1);

    std::istringstream bad("NumHardBlocks 1\nHardBlock x 1 1\nNumSymGroups 1\nSymGroup g 1\nSymPair x y\n");
    bool threw = false;
    try {
        parseProblem(bad);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

void testRandomStatesAreLegal() {
    std::istringstream in(kInstance);
    const Problem p = parseProblem(in, "unit");
    Floorplanner planner(p);
    std::mt19937_64 rng(3);
    FloorplanState st = planner.initialState(rng);
    for (int i = 0; i < planner.islandCount(); ++i) CHECK(planner.islandValid(i, st.islands[i]));
    CHECK(validate(p, planner.realize(st)).empty());

    AnnealOptions opt;
    opt.seed = 5;
    Annealer annealer(planner, opt);
    int legal = 0;
    for (int step = 0; step < 20000; ++step) {
        FloorplanState cand = st;
        const auto m = static_cast<Move>(rng() % static_cast<int>(Move::Count));
        if (!annealer.perturb(cand, m)) continue;
        const Placement pl = planner.realize(cand);
        const auto errors = validate(p, pl);
        CHECK(errors.empty());
        if (!errors.empty()) {
            for (const auto& e : errors) std::fprintf(stderr, "  %s (after %s)\n", e.c_str(), toString(m));
            return;
        }
        CHECK(planner.evaluate(cand) == pl.area());
        st = std::move(cand);
        ++legal;
    }
    CHECK(legal > 1000);
}

void testAnnealImproves() {
    std::istringstream in(kInstance);
    const Problem p = parseProblem(in, "unit");
    Floorplanner planner(p);
    std::mt19937_64 rng(1);
    FloorplanState init = planner.initialState(rng);
    const Coord before = planner.evaluate(init);
    AnnealOptions opt;
    opt.iterations = 30000;
    opt.seed = 9;
    Annealer annealer(planner, opt);
    const FloorplanState best = annealer.run(init);
    const Placement pl = planner.realize(best);
    CHECK(validate(p, pl).empty());
    CHECK(pl.area() <= before);
    CHECK(pl.area() >= p.totalBlockArea());
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"skyline", testSkylineAgainstBruteForce},
        {"bstar-tree", testBStarTreeMoves},
        {"parser", testParser},
        {"random-states-legal", testRandomStatesAreLegal},
        {"anneal-improves", testAnnealImproves},
    };
    for (const auto& [name, fn] : tests) {
        const int before = failures;
        fn();
        std::printf("[%s] %s\n", failures == before ? " ok " : "FAIL", name);
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
