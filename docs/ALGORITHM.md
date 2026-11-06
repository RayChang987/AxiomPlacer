# How AxiomPlacer works

AxiomPlacer combines three ideas:

1. **symmetry islands**: every symmetry group is packed as one connected
   unit, so matched devices end up close together,
2. a **hierarchical B\*-tree** (HB\*-tree): a B\*-tree of *modules*, where a
   module is either a free block or a whole island with its own B\*-tree,
3. **simulated annealing** over that representation, with a contour-aware
   packer that evaluates every candidate in `O(n log W)`.

```
                 top-level B*-tree
                ┌──────────────────┐
                │  m30 ─── m110    │   modules: free blocks …
                │   │              │
                │ [sg0] ─── m27    │   … and symmetry islands
                └───┬──────────────┘
                    │ island B*-tree (representative half only)
                    ▼
              ┌───────────────┐
              │ m1 ─ m5 ─ m10 │  ──mirror──►  full island
              │ │             │
              │ m7            │
              └───────────────┘
```

## B\*-tree packing

A B\*-tree encodes a compacted (left-and-down) placement. The root sits at the
origin. A node's **left child** goes immediately to its right, and its
**right child** goes directly above it at the same x. Each block's y is the
height of the *contour* (the skyline of everything placed so far) over the
block's x-span.

The contour is a lazily allocated segment tree with range-assign and range-max
(`include/axiom/skyline.hpp`). Its nodes come from a pool that is reused
between evaluations, so packing does not allocate memory once it is warmed up.

`BStarTree` (`include/axiom/bstar_tree.hpp`) separates **slots** (tree
positions) from **items** (what sits there). Swapping two blocks is then just
an exchange of labels. A subtree move is a detach followed by an attach. The
whole state is a handful of `int` vectors, which makes copy-and-revert in the
annealer trivial.

## Symmetry islands

For each group only one *representative* per pair is stored in the island's
B\*-tree, together with the self-symmetric blocks at half size. The
representative half is packed like any B\*-tree and then mirrored:

| axis       | half plane     | pair `(a, b)`                          | self-symmetric `s`               |
|------------|----------------|----------------------------------------|----------------------------------|
| vertical   | right of axis  | `a` at `(W/2+g+x, y)`, `b` at `(W/2−x−w, y)` | stretched across the axis   |
| horizontal | above the axis | `a` at `(x, H/2+g+y)`, `b` at `(x, H/2−y−h)` | stretched across the axis   |

(`g` ∈ {0, 1} is a one-unit gap that keeps odd-sized self-symmetric blocks
exactly centred.)

A self-symmetric block must touch the axis. In B\*-tree terms it must lie on
the **right-chain** from the root (x = 0) for a vertical axis, or on the
**left-chain** (y = 0) for a horizontal one. `Floorplanner::islandValid`
checks this in `O(depth)`, and the annealer simply rejects moves that break
it. Flipping an island's axis mirrors its whole tree (swaps every left/right
child), which turns one chain into the other, so the invariant holds.

### Contour-aware island drop

When the top-level packer places an island, it does **not** rest the island's
bounding box on the contour. It computes the lowest offset at which no
*individual block* of the island collides with the skyline:

```
y = max over blocks r of ( contour(x + r.x, x + r.right) − r.y )
```

The island can therefore slide down into notches under its overhangs. The
contour is then updated block by block (in order of increasing top edge, so
that plain range-assignment is correct).

## Simulated annealing

`Annealer` (`include/axiom/annealer.hpp`) perturbs a copy of the current state
with one of nine moves:

| move | effect |
|------|--------|
| `top-shift` | detach a random subtree of the top-level tree, re-attach it at a random free child position |
| `top-swap` | exchange two modules |
| `top-swap-children` | swap the left/right subtrees of a top-level node |
| `rotate-block` | rotate a free block by 90° |
| `island-shift` | `top-shift` inside a symmetry island |
| `island-swap` | exchange two representatives of an island |
| `island-swap-children` | swap the subtrees of an island node |
| `island-rotate` | rotate a pair (both blocks) or an eligible self-symmetric block |
| `island-flip-axis` | switch an island between vertical and horizontal axis |

Candidates are accepted with the Metropolis criterion on the *relative* area
change `Δ = (A' − A) / A · 450`, so the same temperatures work for instances
whose areas differ by orders of magnitude.

The schedule has four geometric cooling stages (20 → 1e-2, 10 → 1e-3,
1 → 1e-3, 1 → 1e-6) that share the time budget equally. Each stage restarts
from the best solution found so far, which works as cheap reheating and
helps the search escape the plateau reached by the previous stage.

Each island's packed layout is cached in its state (`IslandState::cache`). A
move that touches only the top level reuses all island layouts unchanged.

## References

* Y.-C. Chang, Y.-W. Chang, G.-M. Wu, S.-W. Wu, "B\*-Trees: A New
  Representation for Non-Slicing Floorplans", DAC 2000.
* P.-H. Lin, Y.-W. Chang, S.-C. Lin, "Analog Placement Based on
  Symmetry-Island Formulation", IEEE TCAD 28(6), 2009.
