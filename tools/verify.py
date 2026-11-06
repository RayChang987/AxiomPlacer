#!/usr/bin/env python3
"""Independent checker for AxiomPlacer results.

Usage: verify.py <problem.txt> <result.out>

Checks that every block is placed exactly once with non-negative coordinates,
that no two blocks overlap, that every symmetry group is mirror-symmetric about
a single vertical or horizontal axis, and that the reported area matches the
bounding box. Exits with status 0 if the result is legal.
"""
import sys
from pathlib import Path


def parse_problem(path):
    tok = Path(path).read_text().split()
    i = 0

    def take():
        nonlocal i
        i += 1
        return tok[i - 1]

    assert take() == "NumHardBlocks", "expected NumHardBlocks"
    blocks = {}
    for _ in range(int(take())):
        assert take() == "HardBlock", "expected HardBlock"
        name = take()
        blocks[name] = (int(take()), int(take()))
    groups = []
    if i < len(tok):
        assert take() == "NumSymGroups", "expected NumSymGroups"
        for _ in range(int(take())):
            assert take() == "SymGroup", "expected SymGroup"
            gname, n = take(), int(take())
            pairs, selfs = [], []
            for _ in range(n):
                kind = take()
                if kind == "SymPair":
                    pairs.append((take(), take()))
                elif kind == "SymSelf":
                    selfs.append(take())
                else:
                    raise ValueError(f"unknown symmetry entry {kind}")
            groups.append((gname, pairs, selfs))
    return blocks, groups


def parse_result(path):
    lines = [l.split() for l in Path(path).read_text().splitlines() if l.strip()]
    assert lines[0][0] == "Area", "first line must be 'Area <value>'"
    area = int(lines[0][1])
    assert lines[1][0] == "NumHardBlocks", "second line must be 'NumHardBlocks <n>'"
    n = int(lines[1][1])
    placed = {}
    for parts in lines[2 : 2 + n]:
        name, x, y, rot = parts[0], int(parts[1]), int(parts[2]), int(parts[3])
        if name in placed:
            raise ValueError(f"block {name} placed twice")
        placed[name] = (x, y, rot)
    return area, n, placed


def symmetric(rects, pairs, selfs, vertical):
    """Doubled-coordinate symmetry test about one vertical/horizontal axis."""
    axis2 = None

    def c2(r):  # doubled centre across the axis
        x, y, w, h = r
        return 2 * x + w if vertical else 2 * y + h

    def along(r):
        return r[1] if vertical else r[0]

    for a, b in pairs:
        ra, rb = rects[a], rects[b]
        if ra[2:] != rb[2:] or along(ra) != along(rb):
            return False
        s = c2(ra) + c2(rb)
        if s % 2:
            return False
        if axis2 is None:
            axis2 = s // 2
        elif s // 2 != axis2:
            return False
    for s in selfs:
        if axis2 is None:
            axis2 = c2(rects[s])
        elif c2(rects[s]) != axis2:
            return False
    return True


def verify(problem_path, result_path):
    blocks, groups = parse_problem(problem_path)
    area, n, placed = parse_result(result_path)
    errors = []
    if n != len(blocks):
        errors.append(f"NumHardBlocks is {n}, expected {len(blocks)}")
    missing = set(blocks) - set(placed)
    if missing:
        errors.append(f"missing blocks: {sorted(missing)[:10]}")
    unknown = set(placed) - set(blocks)
    if unknown:
        errors.append(f"unknown blocks: {sorted(unknown)[:10]}")
    if errors:
        return errors, None

    rects = {}
    for name, (x, y, rot) in placed.items():
        w, h = blocks[name]
        if rot not in (0, 1):
            errors.append(f"{name}: rotation flag must be 0 or 1")
        if rot:
            w, h = h, w
        if x < 0 or y < 0:
            errors.append(f"{name}: negative coordinates ({x}, {y})")
        rects[name] = (x, y, w, h)

    order = sorted(rects, key=lambda k: rects[k][0])
    for i, a in enumerate(order):
        ax, ay, aw, ah = rects[a]
        for b in order[i + 1 :]:
            bx, by, bw, bh = rects[b]
            if bx >= ax + aw:
                break
            if by < ay + ah and ay < by + bh:
                errors.append(f"overlap: {a} and {b}")

    for gname, pairs, selfs in groups:
        if not (symmetric(rects, pairs, selfs, True) or symmetric(rects, pairs, selfs, False)):
            errors.append(f"symmetry group {gname} is not symmetric")

    width = max(x + w for x, y, w, h in rects.values())
    height = max(y + h for x, y, w, h in rects.values())
    if width * height != area:
        errors.append(f"reported area {area} != bounding box {width}x{height} = {width * height}")
    return errors, (width, height, sum(w * h for w, h in blocks.values()))


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    try:
        errors, info = verify(sys.argv[1], sys.argv[2])
    except (AssertionError, ValueError, IndexError) as e:
        print(f"FAIL  malformed input: {e}")
        return 1
    if errors:
        for e in errors[:20]:
            print(f"FAIL  {e}")
        return 1
    w, h, block_area = info
    print(f"PASS  area {w * h} ({w} x {h}), utilization {100 * block_area / (w * h):.2f}%")
    return 0


if __name__ == "__main__":
    sys.exit(main())
