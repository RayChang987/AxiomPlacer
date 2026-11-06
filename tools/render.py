#!/usr/bin/env python3
"""Render AxiomPlacer traces with matplotlib.

    render.py run.jsonl --gif run.gif          # animated optimisation (placement + area curve)
    render.py run.jsonl --png final.png        # final placement with its convergence curve
    render.py run.jsonl --curve curve.png      # convergence curve only

Requires matplotlib (and Pillow for GIF output).
"""
import argparse
import json
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.animation import FuncAnimation, PillowWriter  # noqa: E402
from matplotlib.patches import Rectangle  # noqa: E402

GROUP_FILL = ["#4e79a7", "#f28e2b", "#e15759", "#76b7b2", "#59a14f",
              "#edc948", "#b07aa1", "#ff9da7", "#9c755f", "#86bcb6"]
GROUP_EDGE = ["#a9c6e8", "#ffc58a", "#ffaaa9", "#c4ecea", "#a8dda0",
              "#fff0a8", "#e3bfd9", "#ffd3d8", "#d6b8a6", "#cdeae6"]
BG, PANEL, GRID, TEXT, MUTED = "#0f1419", "#151b23", "#2a313c", "#e6edf3", "#8b98a5"
FREE, FREE_EDGE, ACCENT = "#2b3440", "#57606a", "#4493f8"


def load(path):
    problem, frames = None, []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            if rec["type"] == "problem":
                problem = rec
            elif rec["type"] in ("frame", "final"):
                frames.append(rec)
    if problem is None or not frames:
        sys.exit(f"{path}: not an AxiomPlacer trace")
    group_of = [-1] * len(problem["blocks"])
    for gi, g in enumerate(problem["groups"]):
        for a, b in g["pairs"]:
            group_of[a] = group_of[b] = gi
        for s in g["selfs"]:
            group_of[s] = gi
    problem["group_of"] = group_of
    return problem, frames


def style_axes(ax):
    ax.set_facecolor(PANEL)
    for s in ax.spines.values():
        s.set_color(GRID)
    ax.tick_params(colors=MUTED, labelsize=8)


def draw_placement(ax, problem, frame, extent, labels=True):
    ax.clear()
    ax.set_facecolor(BG)
    ax.set_aspect("equal")
    ax.axis("off")
    W, H = extent
    ax.set_xlim(-0.02 * W, 1.02 * W)
    ax.set_ylim(-0.02 * H, 1.02 * H)
    ax.add_patch(Rectangle((0, 0), frame["w"], frame["h"], fill=True, fc=PANEL, ec=GRID, ls="--", lw=1))
    r = frame["rects"]
    span = max(W, H)
    for i, (name, _, _) in enumerate(problem["blocks"]):
        x, y, w, h = r[4 * i: 4 * i + 4]
        g = problem["group_of"][i]
        fc = FREE if g < 0 else GROUP_FILL[g % 10]
        ec = FREE_EDGE if g < 0 else GROUP_EDGE[g % 10]
        ax.add_patch(Rectangle((x, y), w, h, fc=fc, ec=ec, lw=0.6, alpha=1 if g < 0 else 0.9))
        if labels and min(w, h) > span * 0.035 and w > span * 0.012 * len(name):
            ax.text(x + w / 2, y + h / 2, name, ha="center", va="center", fontsize=6,
                    color="#adbac7" if g < 0 else "#0b0f14")
    for gi, (d, a2) in enumerate(frame.get("axes", [])):
        g = problem["groups"][gi]
        members = [b for p in g["pairs"] for b in p] + g["selfs"]
        if d == "v":
            lo = min(r[4 * b + 1] for b in members)
            hi = max(r[4 * b + 1] + r[4 * b + 3] for b in members)
            ax.plot([a2 / 2, a2 / 2], [lo, hi], ls="--", lw=1.2, color=GROUP_EDGE[gi % 10])
        else:
            lo = min(r[4 * b] for b in members)
            hi = max(r[4 * b] + r[4 * b + 2] for b in members)
            ax.plot([lo, hi], [a2 / 2, a2 / 2], ls="--", lw=1.2, color=GROUP_EDGE[gi % 10])


def draw_curve(ax, frames, upto=None):
    ax.clear()
    style_axes(ax)
    t = [f["t"] for f in frames]
    cur = [f["cost"] for f in frames]
    best = [f["best"] for f in frames]
    lo = best[-1] * 0.99
    srt = sorted(cur)
    hi = max(srt[int(len(srt) * 0.97)], lo * 1.02)
    ax.plot(t, cur, color=MUTED, lw=0.7, alpha=0.7, label="current")
    ax.plot(t, best, color=ACCENT, lw=1.8, label="best")
    for i in range(1, len(frames)):
        if frames[i]["stage"] != frames[i - 1]["stage"]:
            ax.axvline(t[i], color=GRID, lw=0.8, ls=":")
    if upto is not None:
        ax.axvline(t[upto], color=TEXT, lw=0.8, alpha=0.6)
    ax.set_ylim(lo, hi)
    ax.set_xlim(0, t[-1])
    ax.set_xlabel("time (s)", color=MUTED, fontsize=8)
    ax.set_ylabel("area", color=MUTED, fontsize=8)
    ax.ticklabel_format(axis="y", style="sci", scilimits=(0, 0))
    ax.yaxis.get_offset_text().set_color(MUTED)
    ax.yaxis.get_offset_text().set_fontsize(7)
    ax.legend(loc="upper right", fontsize=7, facecolor=PANEL, edgecolor=GRID, labelcolor=TEXT)


def headline(fig, problem, frame):
    util = 100 * problem["blockArea"] / frame["best"]
    fig.texts.clear()
    fig.text(0.02, 0.955, f"AxiomPlacer · {problem['name']}", color=TEXT, fontsize=12, weight="bold")
    fig.text(0.98, 0.955,
             f"best area {frame['best']:,} · util {util:.1f}% · T {frame['temp']:.1e} · {frame['t']:.1f}s",
             color=MUTED, fontsize=9, ha="right")


def figure():
    fig = plt.figure(figsize=(10, 5.4), dpi=110, facecolor=BG)
    gs = fig.add_gridspec(1, 2, width_ratios=[1.35, 1], left=0.02, right=0.97, bottom=0.11, top=0.9, wspace=0.12)
    return fig, fig.add_subplot(gs[0]), fig.add_subplot(gs[1])


def extent_of(frames):
    return max(f["w"] for f in frames), max(f["h"] for f in frames)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("trace")
    ap.add_argument("--gif")
    ap.add_argument("--png")
    ap.add_argument("--curve")
    ap.add_argument("--frames", type=int, default=90, help="max frames in the GIF")
    ap.add_argument("--fps", type=int, default=12)
    args = ap.parse_args()
    problem, frames = load(args.trace)

    if args.png:
        fig, a1, a2 = figure()
        final = frames[-1]
        draw_placement(a1, problem, final, (final["w"], final["h"]))
        draw_curve(a2, frames)
        headline(fig, problem, final)
        fig.savefig(args.png, facecolor=BG)
        plt.close(fig)

    if args.curve:
        fig, ax = plt.subplots(figsize=(6, 3.2), dpi=120, facecolor=BG)
        draw_curve(ax, frames)
        fig.tight_layout()
        fig.savefig(args.curve, facecolor=BG)
        plt.close(fig)

    if args.gif:
        # Sample frames more densely at the start, where the layout changes most.
        n = len(frames)
        k = min(args.frames, n)
        idx = sorted({min(n - 1, int(round((i / max(k - 1, 1)) ** 1.6 * (n - 1)))) for i in range(k)})
        idx += [n - 1] * (args.fps * 2)  # hold the final result
        ext = extent_of(frames)
        fig, a1, a2 = figure()

        def update(j):
            f = frames[idx[j]]
            draw_placement(a1, problem, f, ext, labels=False)
            draw_curve(a2, frames, idx[j])
            headline(fig, problem, f)

        anim = FuncAnimation(fig, update, frames=len(idx))
        anim.save(args.gif, writer=PillowWriter(fps=args.fps))
        plt.close(fig)


if __name__ == "__main__":
    main()
