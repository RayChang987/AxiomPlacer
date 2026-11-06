# Problem definition & file formats

## The problem

Analog circuits are sensitive to mismatch: two transistors of a differential
pair, or the halves of a current mirror, behave identically only if they see
the same surroundings. Layouts therefore place such devices
**mirror-symmetrically**. AxiomPlacer solves the following placement problem.

**Given**

* a set of rectangular *hard blocks*, each with a fixed width and height, and
* a set of *symmetry groups*. Each group contains
  * *symmetry pairs* `(a, b)`: two blocks of identical size that must be mirror
    images of each other, and/or
  * *self-symmetric blocks*: blocks that must be centred on the axis.

**Find** the lower-left corner `(x, y)` of every block and whether it is
rotated by 90°, such that

1. **no two blocks overlap** (touching edges is fine),
2. all coordinates are **non-negative**,
3. every symmetry group is symmetric about **one** axis that is either
   vertical (`x = c`) or horizontal (`y = c`). Different groups may use
   different axes and orientations. For a vertical axis this means:
   * pair `(a, b)`: `y_a = y_b` and `(x_a + w_a/2) + (x_b + w_b/2) = 2c`,
   * self-symmetric `s`: `x_s + w_s/2 = c`,

   and the horizontal case is the same with x and y swapped,

**minimising** the area of the bounding box `[0, max(x+w)] × [0, max(y+h)]`.

The axis may lie on a half unit (for example when a self-symmetric block has
an odd width). All checks are therefore done in doubled coordinates.

## Input (`.txt`)

Whitespace-separated tokens. Lines are only for readability.

```
NumHardBlocks <n>
HardBlock <name> <width> <height>        # repeated n times

NumSymGroups <g>                         # optional section
SymGroup <name> <k>                      # k = number of entries below
SymPair <blockA> <blockB>
SymSelf <block>
...
```

Rules enforced by the parser:

* block names are unique; widths and heights are positive integers,
* both blocks of a `SymPair` have the same dimensions,
* a block appears in at most one symmetry entry.

Example:

```
NumHardBlocks 5
HardBlock inA   40 20
HardBlock inB   40 20
HardBlock tail  30 12
HardBlock bias  25 25
HardBlock cap   60 35

NumSymGroups 1
SymGroup diffpair 2
SymPair inA inB
SymSelf tail
```

## Output (`.out`)

```
Area <bounding box area>
NumHardBlocks <n>
<name> <x> <y> <rotated>                 # one line per block, rotated is 0 or 1
```

`x` and `y` are the lower-left corner **after** rotation; a rotated block
occupies `height × width` of its input size.

Use `tools/verify.py <input.txt> <result.out>` to check a result
independently of the placer.

## Trace (`.jsonl`)

`--trace` writes one JSON object per line and the viewer reads it.

| `type`    | fields |
|-----------|--------|
| `problem` | `name`, `seed`, `blockArea`, `blocks: [[name, w, h], …]`, `groups: [{name, pairs: [[a, b], …], selfs: […]}]` (block indices) |
| `frame`   | `iter`, `t` (s), `progress` (0–1), `stage`, `temp`, `accept` (acceptance ratio since the previous frame), `cost`, `best`, `w`, `h`, `rects` (flat `x, y, w, h` per block), `axes: [["v"\|"h", 2·axis], …]` |
| `final`   | same as `frame`, holding the best solution |
