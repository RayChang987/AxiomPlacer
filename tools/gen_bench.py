#!/usr/bin/env python3
"""Generate a random symmetry-constrained placement instance.

Example:
    tools/gen_bench.py --blocks 200 --groups 6 --seed 42 > benchmarks/random_200.txt

Block sizes are drawn log-uniformly so instances mix large and small devices,
roughly like real analog layouts (big capacitors next to small transistors).
"""
import argparse
import math
import random


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--blocks", type=int, default=50, help="total number of blocks")
    ap.add_argument("--groups", type=int, default=3, help="number of symmetry groups")
    ap.add_argument("--sym-ratio", type=float, default=0.6, help="fraction of blocks in symmetry groups")
    ap.add_argument("--self-ratio", type=float, default=0.15, help="fraction of group members that are self-symmetric")
    ap.add_argument("--min-size", type=int, default=10)
    ap.add_argument("--max-size", type=int, default=300)
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()
    rng = random.Random(args.seed)

    def size():
        lo, hi = math.log(args.min_size), math.log(args.max_size)
        return int(math.exp(rng.uniform(lo, hi)))

    blocks = []  # (name, w, h)
    groups = []  # (name, [pairs], [selfs])
    sym_budget = int(args.blocks * args.sym_ratio)
    per_group = max(1, sym_budget // max(args.groups, 1))
    idx = 0

    def new_block(w, h):
        nonlocal idx
        name = f"b{idx}"
        idx += 1
        blocks.append((name, w, h))
        return name

    for g in range(args.groups):
        pairs, selfs = [], []
        members = 0
        while members < per_group:
            if rng.random() < args.self_ratio:
                # keep self-symmetric blocks even-sized so they centre exactly
                w, h = size() // 2 * 2 or 2, size() // 2 * 2 or 2
                selfs.append(new_block(w, h))
                members += 1
            else:
                w, h = size(), size()
                pairs.append((new_block(w, h), new_block(w, h)))
                members += 2
        groups.append((f"sg{g}", pairs, selfs))

    while idx < args.blocks:
        new_block(size(), size())

    print(f"NumHardBlocks {len(blocks)}")
    for name, w, h in blocks:
        print(f"HardBlock {name} {w} {h}")
    print()
    print(f"NumSymGroups {len(groups)}")
    for name, pairs, selfs in groups:
        print(f"SymGroup {name} {len(pairs) + len(selfs)}")
        for a, b in pairs:
            print(f"SymPair {a} {b}")
        for s in selfs:
            print(f"SymSelf {s}")


if __name__ == "__main__":
    main()
