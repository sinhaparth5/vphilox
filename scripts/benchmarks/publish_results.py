#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
# Copyright (c) 2026 Parth Sinha
"""Turn the archived benchmark JSON in results/ into published CSVs and plots.

Issue #55. Reads every ``results/<tag>.json`` written by
``scripts/benchmarks/run_matrix.sh`` (or by the ad-hoc runs that predate it),
plus the matching ``-environment.txt``, and writes:

    docs/benchmarks/raw/<tag>.csv    one row per aggregated benchmark
    docs/benchmarks/raw/machines.csv provenance, one row per archived run
    docs/benchmarks/plots/*.svg      the figures

Standard library only, on purpose. matplotlib is not installed on the Pi, the
GCP images, or this checkout, and a plot that needs a pip install is a plot
nobody regenerates. The SVGs are emitted as sorted, fixed-precision text so
re-running with unchanged inputs produces an empty diff.

**The cross-machine rule lives here.** RDTSC counts reference cycles, so
cycles/byte compares within one machine and never across two with different
nominal frequencies -- the Pi's perf_event counter and a Xeon's RDTSC are not
the same unit. Any figure that puts several machines on one axis therefore
plots a *ratio* measured within each machine, which is dimensionless and does
transfer. Absolute cycles/byte appears only in the CSVs, where the machine
column travels with it, and in the two single-machine figures.

Usage:  python3 scripts/benchmarks/publish_results.py [--check]

``--check`` regenerates into a temporary directory and exits non-zero if the
committed output would change, which is what CI can run.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import re
import shutil
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RESULTS = os.path.join(REPO, "results")
RAW_OUT = os.path.join("docs", "benchmarks", "raw")
PLOT_OUT = os.path.join("docs", "benchmarks", "plots")

# Human label, resolved backend, and measurement quality for each archived run.
#
# The backend is curated because every JSON here predates the context stamp that
# `bench_main.hpp` now writes; runs recorded from here on carry
# "vphilox_backend" and need no entry. Each value below was read back off the
# write-up for that run rather than inferred from the machine -- three of these
# were measured on AVX-512 hardware while the AVX-512 kernel was still a stub,
# so the CPU does not tell you what ran.
#
# The quality column is the one this project cannot leave implicit. Four of
# these runs do not meet the bar in CLAUDE.md -- an unpinned governor puts a
# moving turbo ceiling straight into cycles/byte, and a shared cloud vCPU hides
# steal time -- and they are archived because they built the harness, not
# because their numbers are publishable. The figures below use only the runs
# marked publishable.
RUNS = {
    "pi5-matrix":                    ("Pi 5 / NEON",       "neon",   "pinned bare metal"),
    "pi-arm-matrix":                 ("Pi 5 / scalar",     "scalar", "pinned bare metal"),
    "pi-arm-baseline":               ("Pi 5 / scalar",     "scalar", "pinned bare metal"),
    "pi-arm-scaling":                ("Pi 5 / scalar",     "scalar", "bare metal; unpinned by design"),
    "sapphire-rapids-matrix":        ("Sapphire Rapids",   "avx512", "cloud; dedicated vCPU"),
    "sapphire-rapids-float-convert": ("Sapphire Rapids",   "avx512", "cloud; dedicated vCPU"),
    "skylake-sp-matrix":             ("Skylake-SP",        "avx512", "cloud; dedicated vCPU"),
    "avx2-baseline":                 ("Tiger Lake / AVX2", "avx2",   "harness validation: unpinned governor"),
    "bulk-generate-baseline":        ("Tiger Lake / AVX2", "avx2",   "harness validation: unpinned governor"),
    "avx512-baseline":               ("Ice Lake SP",       "scalar", "harness validation: 2-vCPU cloud VM"),
}

PUBLISHABLE = {tag for tag, meta in RUNS.items() if not meta[2].startswith("harness")}

# The comparison rows, in the order the write-ups quote them.
MATRIX_ROWS = [
    ("BM_xoshiro256pp",  "xoshiro256++"),
    ("BM_pcg64",         "PCG64"),
    ("BM_mt19937",       "std::mt19937"),
    ("BM_philox_scalar", "Philox (unspecialised)"),
    ("BM_vphilox",       "vphilox (buffered)"),
    ("BM_vphilox_bulk",  "vphilox (generate_n)"),
]

SWEEP_SIZES = [8, 32, 256, 1024, 65536]

PALETTE = ["#1f4e79", "#c0504d", "#3f8f4f", "#8064a2", "#e08214", "#4f81bd"]
INK = "#222222"
GRID = "#d8d8d8"


# ----------------------------------------------------------------- loading

def load_runs():
    """Every results/<tag>.json, keyed by tag, with provenance attached."""
    runs = {}
    for name in sorted(os.listdir(RESULTS)):
        if not name.endswith(".json"):
            continue
        tag = name[: -len(".json")]
        with open(os.path.join(RESULTS, name), encoding="utf-8") as fh:
            doc = json.load(fh)
        runs[tag] = {
            "tag": tag,
            "context": doc.get("context", {}),
            "rows": aggregate_rows(doc.get("benchmarks", [])),
            "env": read_environment(tag),
        }
    return runs


def aggregate_rows(benchmarks):
    """{run_name: {'median': ..., 'cv': ..., 'gbps': ..., 'source': ...}}."""
    out = {}
    for b in benchmarks:
        run = b.get("run_name")
        agg = b.get("aggregate_name")
        if not run or agg not in ("median", "cv"):
            continue
        rec = out.setdefault(run, {})
        cpb = b.get("cycles_per_byte")
        if agg == "median":
            rec["median"] = cpb
            rec["gbps"] = b.get("bytes_per_second")
            rec["time_ns"] = b.get("real_time")
            label = b.get("label") or ""
            m = re.search(r"cycle_source=(\w+)", label)
            rec["source"] = m.group(1) if m else ""
        else:
            # Benchmark reports CV as a fraction; the project quotes percent.
            rec["cv_pct"] = cpb * 100.0 if cpb is not None else None
    return out


def read_environment(tag):
    """Pull the handful of provenance fields the environment files agree on."""
    path = os.path.join(RESULTS, tag + "-environment.txt")
    if not os.path.exists(path):
        # The older ad-hoc runs share one environment file per machine.
        alt = os.path.join(RESULTS, tag.rsplit("-", 1)[0] + "-environment.txt")
        path = alt if os.path.exists(alt) else None
    env = {}
    if not path:
        return env
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            for key, field in (
                ("git-sha:", "commit"),
                ("governor:", "governor"),
                ("compiler:", "compiler"),
                ("command:", "command"),
                ("resolved-backend:", "backend"),
                ("Model name:", "cpu"),
                ("Architecture:", "arch"),
            ):
                if line.startswith(key) and field not in env:
                    env[field] = line[len(key):].strip()
    return env


def label_of(run):
    return RUNS.get(run["tag"], (run["tag"],))[0]


def quality_of(run):
    meta = RUNS.get(run["tag"])
    return meta[2] if meta and len(meta) > 2 else ""


def backend_of(run):
    stamped = run["context"].get("vphilox_backend")
    if stamped:
        return stamped
    if run["env"].get("backend"):
        return run["env"]["backend"]
    meta = RUNS.get(run["tag"])
    return meta[1] if meta and len(meta) > 1 else "unknown"


# --------------------------------------------------------------------- CSV

def write_csvs(runs, outdir):
    rawdir = os.path.join(outdir, RAW_OUT)
    os.makedirs(rawdir, exist_ok=True)

    for tag, run in sorted(runs.items()):
        path = os.path.join(rawdir, tag + ".csv")
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            w = csv.writer(fh, lineterminator="\n")
            w.writerow(["benchmark", "cycles_per_byte", "cycles_per_byte_cv_pct",
                        "bytes_per_second", "median_time_ns", "cycle_source"])
            for name in sorted(run["rows"]):
                r = run["rows"][name]
                w.writerow([
                    name,
                    fmt(r.get("median"), 6),
                    fmt(r.get("cv_pct"), 4),
                    fmt(r.get("gbps"), 1),
                    fmt(r.get("time_ns"), 3),
                    r.get("source", ""),
                ])

    path = os.path.join(rawdir, "machines.csv")
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        w = csv.writer(fh, lineterminator="\n")
        w.writerow(["tag", "label", "backend", "quality", "arch", "cpu", "nominal_mhz",
                    "cycle_source", "date", "commit", "governor", "compiler", "command"])
        for tag, run in sorted(runs.items()):
            ctx, env = run["context"], run["env"]
            src = ""
            for r in run["rows"].values():
                if r.get("source"):
                    src = r["source"]
                    break
            w.writerow([
                tag, label_of(run), backend_of(run), quality_of(run),
                env.get("arch", ""), env.get("cpu", ""),
                ctx.get("mhz_per_cpu", ""), src,
                ctx.get("date", ""), env.get("commit", ""),
                env.get("governor", ""), env.get("compiler", ""), env.get("command", ""),
            ])


def fmt(value, places):
    if value is None:
        return ""
    return f"{value:.{places}f}"


# --------------------------------------------------------------------- SVG

class Svg:
    """A minimal SVG canvas. Enough for bars, lines, ticks and labels."""

    def __init__(self, width, height, title):
        self.w, self.h = width, height
        self.parts = [
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
            f'viewBox="0 0 {width} {height}" font-family="DejaVu Sans, Verdana, sans-serif">',
            f"<title>{esc(title)}</title>",
            f'<rect width="{width}" height="{height}" fill="#ffffff"/>',
        ]

    def rect(self, x, y, w, h, fill, opacity=1.0):
        self.parts.append(
            f'<rect x="{n(x)}" y="{n(y)}" width="{n(max(w, 0))}" height="{n(max(h, 0))}" '
            f'fill="{fill}" fill-opacity="{n(opacity)}"/>')

    def line(self, x1, y1, x2, y2, stroke=GRID, width=1.0, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.parts.append(
            f'<line x1="{n(x1)}" y1="{n(y1)}" x2="{n(x2)}" y2="{n(y2)}" '
            f'stroke="{stroke}" stroke-width="{n(width)}"{d}/>')

    def polyline(self, points, stroke, width=2.0, dash=None):
        pts = " ".join(f"{n(x)},{n(y)}" for x, y in points)
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.parts.append(
            f'<polyline points="{pts}" fill="none" stroke="{stroke}" '
            f'stroke-width="{n(width)}" stroke-linejoin="round"{d}/>')

    def dot(self, x, y, fill, r=3.0):
        self.parts.append(f'<circle cx="{n(x)}" cy="{n(y)}" r="{n(r)}" fill="{fill}"/>')

    def text(self, x, y, s, size=12, anchor="start", fill=INK, weight="normal"):
        self.parts.append(
            f'<text x="{n(x)}" y="{n(y)}" font-size="{n(size)}" text-anchor="{anchor}" '
            f'fill="{fill}" font-weight="{weight}">{esc(s)}</text>')

    def save(self, path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write("\n".join(self.parts) + "\n</svg>\n")


def n(v):
    """Fixed precision so regenerating an unchanged plot is an empty diff."""
    return f"{float(v):.2f}".rstrip("0").rstrip(".") or "0"


def esc(s):
    return (str(s).replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;").replace('"', "&quot;"))


def legend(svg, x, y, entries, size=12):
    for i, (name, colour) in enumerate(entries):
        yy = y + i * (size + 7)
        svg.rect(x, yy - size + 3, size - 3, size - 3, colour)
        svg.text(x + size + 3, yy, name, size=size)


# ----------------------------------------------------------------- figures

def plot_matrix_relative(runs, outdir):
    """Cycles/byte relative to std::mt19937, per machine. Lower is faster."""
    tags = ["pi5-matrix", "sapphire-rapids-matrix", "skylake-sp-matrix"]
    tags = [t for t in tags if t in runs and t in PUBLISHABLE]

    series = []
    for t in tags:
        rows = runs[t]["rows"]
        base = rows.get("BM_mt19937", {}).get("median")
        if not base:
            continue
        vals = {}
        for key, _ in MATRIX_ROWS:
            m = rows.get(key, {}).get("median")
            if m:
                vals[key] = m / base
        series.append((label_of(runs[t]), vals))

    left, right, top = 210, 60, 78
    row_h, group_gap = 20, 16
    bar_h = row_h - 5
    legend_h = len(series) * 19 + 14
    height   = top + len(MATRIX_ROWS) * (len(series) * row_h + group_gap) + 56 + legend_h
    width = 900
    plot_w = width - left - right

    top_val = max(v for _, vals in series for v in vals.values())
    xmax = math.ceil(top_val * 2) / 2.0

    svg = Svg(width, height, "Throughput relative to std::mt19937")
    svg.text(left - 190, 28, "Cost per byte, relative to std::mt19937 on the same machine",
             size=15, weight="bold")
    svg.text(left - 190, 48,
             "Lower is faster. Ratios, not cycles/byte: the absolute unit does not "
             "compare across machines.", size=11, fill="#555555")

    def xpos(v):
        return left + (v / xmax) * plot_w

    axis_y = height - legend_h - 32
    tick = 0.5
    t = 0.0
    while t <= xmax + 1e-9:
        svg.line(xpos(t), top - 8, xpos(t), axis_y, GRID)
        svg.text(xpos(t), axis_y + 16, f"{t:g}x", size=11, anchor="middle", fill="#555555")
        t += tick
    svg.line(xpos(1.0), top - 8, xpos(1.0), axis_y, "#999999", 1.5, dash="4 3")

    y = top
    for key, pretty in MATRIX_ROWS:
        svg.text(left - 10, y + bar_h - 5, pretty, size=12, anchor="end", weight="bold")
        for i, (machine, vals) in enumerate(series):
            v = vals.get(key)
            yy = y + i * row_h
            if v is None:
                svg.text(left + 4, yy + bar_h - 5, "not run", size=10, fill="#999999")
            else:
                svg.rect(left, yy, xpos(v) - left, bar_h, PALETTE[i])
                svg.text(xpos(v) + 6, yy + bar_h - 5, f"{v:.2f}x", size=10, fill="#555555")
        y += len(series) * row_h + group_gap

    legend(svg, left, axis_y + 46, [(m, PALETTE[i]) for i, (m, _) in enumerate(series)])
    svg.save(os.path.join(outdir, PLOT_OUT, "matrix-relative.svg"))


def plot_generate_n_sweep(runs, outdir):
    """The refill-buffer crossover: cost per call size, relative to bulk."""
    # bulk-generate-baseline has the richest sweep but an unpinned governor, so
    # it is deliberately not here -- see the quality column in machines.csv.
    tags = ["pi5-matrix", "sapphire-rapids-matrix", "skylake-sp-matrix"]
    tags = [t for t in tags if t in runs and t in PUBLISHABLE]

    series = []
    for t in tags:
        rows = runs[t]["rows"]
        bulk = rows.get("BM_vphilox_bulk", {}).get("median")
        buffered = rows.get("BM_vphilox", {}).get("median")
        if not bulk:
            continue
        pts = []
        for size in SWEEP_SIZES:
            m = rows.get(f"BM_vphilox_generate_n/{size}", {}).get("median")
            if m:
                pts.append((size, m / bulk))
        if pts:
            series.append((label_of(runs[t]), pts, (buffered / bulk) if buffered else None))

    width, height = 880, 470
    left, right, top, bottom = 78, 250, 84, 66
    plot_w, plot_h = width - left - right, height - top - bottom

    ymax = max(max(v for _, v in pts) for _, pts, _ in series)
    ymax = max(ymax, max((b for _, _, b in series if b), default=0))
    ymax = math.ceil(ymax * 10) / 10.0
    ymin = 1.0

    xs = [math.log2(s) for s in SWEEP_SIZES]
    x0, x1 = min(xs), max(xs)

    pad = 14.0

    def xpos(size):
        return left + pad + (math.log2(size) - x0) / (x1 - x0) * (plot_w - 2 * pad)

    def ypos(v):
        return top + plot_h - (v - ymin) / (ymax - ymin) * plot_h

    svg = Svg(width, height, "Cost per generate_n call size")
    svg.text(left - 60, 28, "What a small generate_n call costs, relative to the bulk path",
             size=15, weight="bold")
    svg.text(left - 60, 48,
             "1.0x is the raw kernel. Ratios within each machine, so the curves are "
             "comparable even though the cycles are not.", size=11, fill="#555555")

    step = 0.1 if (ymax - ymin) <= 1.0 else 0.25
    v = ymin
    while v <= ymax + 1e-9:
        svg.line(left, ypos(v), left + plot_w, ypos(v), GRID)
        svg.text(left - 8, ypos(v) + 4, f"{v:.2f}x", size=11, anchor="end", fill="#555555")
        v += step
    svg.line(left, ypos(1.0), left + plot_w, ypos(1.0), "#999999", 1.5)

    for size in SWEEP_SIZES:
        svg.text(xpos(size), top + plot_h + 22, str(size), size=11, anchor="middle",
                 fill="#555555")
    svg.text(left + plot_w / 2, top + plot_h + 46, "words per generate_n call",
             size=12, anchor="middle")

    for i, (machine, pts, buffered) in enumerate(series):
        colour = PALETTE[i]
        svg.polyline([(xpos(s), ypos(v)) for s, v in pts], colour)
        for s, v in pts:
            svg.dot(xpos(s), ypos(v), colour)
        if buffered and buffered <= ymax:
            svg.line(left, ypos(buffered), left + plot_w, ypos(buffered), colour, 1.0,
                     dash="5 4")

    entries = [(m, PALETTE[i]) for i, (m, _, _) in enumerate(series)]
    legend(svg, left + plot_w + 24, top + 12, entries)
    svg.text(left + plot_w + 24, top + 12 + len(entries) * 19 + 16,
             "dashed: operator() through", size=10, fill="#555555")
    svg.text(left + plot_w + 24, top + 12 + len(entries) * 19 + 30,
             "the refill buffer", size=10, fill="#555555")
    svg.save(os.path.join(outdir, PLOT_OUT, "generate-n-sweep.svg"))


def plot_thread_scaling(runs, outdir):
    """One machine, so absolute cycles/byte is the honest axis here."""
    run = runs.get("pi-arm-scaling")
    if not run:
        return
    rows = run["rows"]
    threads = [1, 2, 4, 8, 16, 32]
    variants = [
        ("BM_thread_scaling", 65536, "buffered, 256 KiB"),
        ("BM_thread_scaling_bulk", 65536, "generate_n, 256 KiB"),
        ("BM_thread_scaling", 1048576, "buffered, 4 MiB"),
        ("BM_thread_scaling_bulk", 1048576, "generate_n, 4 MiB"),
    ]
    series = []
    for prefix, ws, name in variants:
        pts = []
        for t in threads:
            r = rows.get(f"{prefix}/{t}/{ws}/real_time", {})
            if r.get("median"):
                pts.append((t, r["median"]))
        if pts:
            series.append((name, pts))
    if not series:
        return

    width, height = 880, 440
    left, right, top, bottom = 78, 240, 84, 62
    plot_w, plot_h = width - left - right, height - top - bottom

    vals = [v for _, pts in series for _, v in pts]
    ymax = math.ceil(max(vals) * 1.35 * 10) / 10.0
    ymin = 0.0

    xs = [math.log2(t) for t in threads]
    x0, x1 = min(xs), max(xs)

    pad = 14.0

    def xpos(t):
        return left + pad + (math.log2(t) - x0) / (x1 - x0) * (plot_w - 2 * pad)

    def ypos(v):
        return top + plot_h - (v - ymin) / (ymax - ymin) * plot_h

    svg = Svg(width, height, "Thread scaling on the Raspberry Pi 5")
    svg.text(left - 60, 28, "Aggregate cost per byte against thread count — Pi 5, 4 cores",
             size=15, weight="bold")
    svg.text(left - 60, 48,
             "Flat means linear scaling: more threads, same cost per byte. Two lines, not "
             "four \u2014 each working set lands on the other, which is the result.",
             size=11, fill="#555555")

    step = 0.5
    v = ymin
    while v <= ymax + 1e-9:
        svg.line(left, ypos(v), left + plot_w, ypos(v), GRID)
        svg.text(left - 8, ypos(v) + 4, f"{v:g}", size=11, anchor="end", fill="#555555")
        v += step
    svg.text(left - 58, top - 14, "cycles/byte", size=11, fill="#555555")

    for t in threads:
        svg.text(xpos(t), top + plot_h + 22, str(t), size=11, anchor="middle", fill="#555555")
    svg.text(left + plot_w / 2, top + plot_h + 44, "threads", size=12, anchor="middle")
    svg.line(xpos(4), top, xpos(4), top + plot_h, "#999999", 1.2, dash="4 3")
    svg.text(xpos(4) + 5, top + 14, "4 physical cores", size=10, fill="#777777")

    for i, (name, pts) in enumerate(series):
        colour = PALETTE[i]
        svg.polyline([(xpos(t), ypos(v)) for t, v in pts], colour,
                     dash="6 4" if "4 MiB" in name else None)
        for t, v in pts:
            svg.dot(xpos(t), ypos(v), colour)

    legend(svg, left + plot_w + 24, top + 12, [(nm, PALETTE[i]) for i, (nm, _) in enumerate(series)])
    svg.save(os.path.join(outdir, PLOT_OUT, "thread-scaling.svg"))


def plot_float_conversion(runs, outdir):
    """One machine again: the three conversion widths across working sets."""
    run = runs.get("sapphire-rapids-float-convert")
    if not run:
        return
    rows = run["rows"]
    sizes = [256, 2048, 65536, 4194304]
    variants = [("BM_convert_baseline", "baseline (scalar)"),
                ("BM_convert_avx2", "AVX2 clone"),
                ("BM_convert_avx512", "AVX-512 clone")]
    series = []
    for prefix, name in variants:
        pts = []
        for s in sizes:
            r = rows.get(f"{prefix}/{s}", {})
            if r.get("median"):
                pts.append((s, r["median"], r.get("cv_pct")))
        if pts:
            series.append((name, pts))
    if not series:
        return

    width, height = 880, 440
    left, right, top, bottom = 86, 240, 88, 66
    plot_w, plot_h = width - left - right, height - top - bottom

    vals = [v for _, pts in series for _, v, _ in pts]
    lo, hi = min(vals), max(vals)
    y0, y1 = math.log10(lo) - 0.12, math.log10(hi) + 0.12

    xs = [math.log2(s) for s in sizes]
    x0, x1 = min(xs), max(xs)

    pad = 14.0

    def xpos(s):
        return left + pad + (math.log2(s) - x0) / (x1 - x0) * (plot_w - 2 * pad)

    def ypos(v):
        return top + plot_h - (math.log10(v) - y0) / (y1 - y0) * plot_h

    svg = Svg(width, height, "Float conversion width against working set")
    svg.text(left - 68, 28, "Float conversion cost against working set — Sapphire Rapids",
             size=15, weight="bold")
    svg.text(left - 68, 48,
             "Log axes. The widths separate at 2 KiB and at 32 MiB, and converge at the "
             "512 KiB row where the loop is bandwidth-bound.", size=11, fill="#555555")

    dec = math.floor(y0)
    while dec <= y1:
        for mant in (1, 2, 5):
            v = mant * (10 ** dec)
            if y0 <= math.log10(v) <= y1:
                svg.line(left, ypos(v), left + plot_w, ypos(v), GRID)
                svg.text(left - 8, ypos(v) + 4, f"{v:g}", size=11, anchor="end", fill="#555555")
        dec += 1
    svg.text(left - 66, top - 16, "cycles/byte", size=11, fill="#555555")

    # 32 MiB is past L2 but still inside this host's 105 MiB L3, so the largest
    # row is named for its size rather than for a cache it does not leave.
    notes = {256: "2 KiB (tile)", 2048: "16 KiB", 65536: "512 KiB", 4194304: "32 MiB"}
    for s in sizes:
        svg.text(xpos(s), top + plot_h + 22, f"{s}", size=11, anchor="middle", fill="#555555")
        svg.text(xpos(s), top + plot_h + 37, notes[s], size=9.5, anchor="middle", fill="#999999")
    svg.text(left + plot_w / 2, top + plot_h + 58, "words converted per call",
             size=12, anchor="middle")

    for i, (name, pts) in enumerate(series):
        colour = PALETTE[i]
        svg.polyline([(xpos(s), ypos(v)) for s, v, _ in pts], colour)
        for s, v, cv in pts:
            svg.dot(xpos(s), ypos(v), colour)
            if cv is not None and cv > 1.0:
                svg.text(xpos(s) + 7, ypos(v) - 6, "!", size=11, fill="#c0504d", weight="bold")

    entries = [(nm, PALETTE[i]) for i, (nm, _) in enumerate(series)]
    legend(svg, left + plot_w + 24, top + 12, entries)
    svg.text(left + plot_w + 24, top + 12 + len(entries) * 19 + 16,
             "! over the 1% CV bar", size=10, fill="#c0504d")
    svg.save(os.path.join(outdir, PLOT_OUT, "float-conversion-widths.svg"))


# ---------------------------------------------------------------- driver

def generate(outdir):
    runs = load_runs()
    write_csvs(runs, outdir)
    plot_matrix_relative(runs, outdir)
    plot_generate_n_sweep(runs, outdir)
    plot_thread_scaling(runs, outdir)
    plot_float_conversion(runs, outdir)
    return runs


def tree(root):
    """Only the files this script owns. Hand-archived JSON shares raw/ and must
    not be reported stale just because nothing here generates it."""
    found = {}
    for sub in (RAW_OUT, PLOT_OUT):
        base = os.path.join(root, sub)
        if not os.path.isdir(base):
            continue
        for name in sorted(os.listdir(base)):
            if not name.endswith((".csv", ".svg")):
                continue
            with open(os.path.join(base, name), "rb") as fh:
                found[os.path.join(sub, name)] = fh.read()
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="fail if the committed output is stale")
    args = ap.parse_args()

    if not args.check:
        generate(REPO)
        print(f"wrote {RAW_OUT}/ and {PLOT_OUT}/")
        return 0

    tmp = tempfile.mkdtemp(prefix="vphilox-publish-")
    try:
        generate(tmp)
        want, have = tree(tmp), tree(REPO)
        stale = sorted(set(want) | set(have))
        bad = [p for p in stale if want.get(p) != have.get(p)]
        if bad:
            print("stale published output; re-run "
                  "scripts/benchmarks/publish_results.py:", file=sys.stderr)
            for p in bad:
                print("   ", p, file=sys.stderr)
            return 1
        print(f"published output is current ({len(want)} files)")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
