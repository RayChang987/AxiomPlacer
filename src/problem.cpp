#include "axiom/problem.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace axiom {

namespace {

class Reader {
   public:
    explicit Reader(std::istream& in) : in_(in) {}

    std::string word(const char* what) {
        std::string s;
        if (!(in_ >> s)) fail(std::string("unexpected end of file, expected ") + what);
        return s;
    }

    void keyword(const std::string& expected) {
        const std::string got = word(expected.c_str());
        if (got != expected) fail("expected '" + expected + "' but found '" + got + "'");
    }

    long long integer(const char* what) {
        const std::string s = word(what);
        try {
            size_t pos = 0;
            const long long v = std::stoll(s, &pos);
            if (pos != s.size()) throw std::invalid_argument(s);
            return v;
        } catch (const std::exception&) {
            fail(std::string("expected integer ") + what + " but found '" + s + "'");
        }
    }

    [[noreturn]] static void fail(const std::string& msg) {
        throw std::runtime_error("parse error: " + msg);
    }

   private:
    std::istream& in_;
};

}  // namespace

int Problem::findBlock(const std::string& n) const {
    const auto it = index_.find(n);
    return it == index_.end() ? -1 : it->second;
}

Coord Problem::totalBlockArea() const {
    Coord sum = 0;
    for (const Block& b : blocks) sum += b.width * b.height;
    return sum;
}

Problem parseProblem(std::istream& in, const std::string& name) {
    Reader r(in);
    Problem p;
    p.name = name;

    r.keyword("NumHardBlocks");
    const long long n = r.integer("block count");
    if (n <= 0) Reader::fail("block count must be positive");
    p.blocks.reserve(n);
    for (long long i = 0; i < n; ++i) {
        r.keyword("HardBlock");
        Block b;
        b.name = r.word("block name");
        b.width = r.integer("block width");
        b.height = r.integer("block height");
        if (b.width <= 0 || b.height <= 0) Reader::fail("block '" + b.name + "' has non-positive size");
        if (!p.index_.emplace(b.name, static_cast<int>(i)).second)
            Reader::fail("duplicate block name '" + b.name + "'");
        p.blocks.push_back(std::move(b));
    }
    p.groupOf.assign(p.blocks.size(), -1);

    std::string tok;
    if (!(in >> tok)) return p;  // symmetry section is optional
    if (tok != "NumSymGroups") Reader::fail("expected 'NumSymGroups' but found '" + tok + "'");
    const long long g = r.integer("group count");

    auto claim = [&](const std::string& blockName, int group) {
        const int id = p.findBlock(blockName);
        if (id < 0) Reader::fail("unknown block '" + blockName + "' in symmetry group");
        if (p.groupOf[id] != -1) Reader::fail("block '" + blockName + "' appears in more than one symmetry slot");
        p.groupOf[id] = group;
        return id;
    };

    for (long long gi = 0; gi < g; ++gi) {
        r.keyword("SymGroup");
        SymmetryGroup grp;
        grp.name = r.word("group name");
        const long long m = r.integer("group size");
        const int gid = static_cast<int>(gi);
        for (long long k = 0; k < m; ++k) {
            const std::string kind = r.word("SymPair or SymSelf");
            if (kind == "SymPair") {
                const int a = claim(r.word("block name"), gid);
                const int b = claim(r.word("block name"), gid);
                const Block &ba = p.blocks[a], &bb = p.blocks[b];
                if (ba.width != bb.width || ba.height != bb.height)
                    Reader::fail("symmetry pair '" + ba.name + "'/'" + bb.name + "' have different sizes");
                grp.pairs.emplace_back(a, b);
            } else if (kind == "SymSelf") {
                grp.selfs.push_back(claim(r.word("block name"), gid));
            } else {
                Reader::fail("expected 'SymPair' or 'SymSelf' but found '" + kind + "'");
            }
        }
        if (grp.pairs.empty() && grp.selfs.empty()) Reader::fail("symmetry group '" + grp.name + "' is empty");
        p.groups.push_back(std::move(grp));
    }
    return p;
}

Problem loadProblem(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open '" + path + "'");
    std::string name = path;
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    const auto dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    return parseProblem(in, name);
}

}  // namespace axiom
