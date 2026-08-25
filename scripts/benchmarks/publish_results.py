#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
# Copyright (c) 2026 Parth Sinha
"""Turn the archived benchmark JSON in results/ into published CSVs and plots.

Issue #55. Reads every ``results/<tag>.json`` written by
``scripts/benchmarks/run_matrix.sh`` (or by the ad-hoc runs that predate it),
plus the matching ``-environment.txt``, and writes:

    docs/benchmarks/raw/<tag>.csv    one row per aggregated benchmark
    docs/benchmarks/raw/machines.csv provenance, one row per archived run
    docs/benchmarks/plots/*.png      what the write-ups display
    docs/benchmarks/plots/*.pdf      the same figure, vector, for the paper
    docs/benchmarks/plots/*.svg      the checked source both are made from

Three formats because they have three jobs. PNG is what a reader sees: GitHub
renders it in the README, in the mobile app and inside a PR diff, and it needs
no font installed. PDF is what LaTeX takes -- ``\\includegraphics`` cannot read
an SVG without ``--shell-escape`` and an Inkscape round trip, so a paper needs
a real vector format and PDF is the native one. SVG is the source: it is what
the rasteriser turns into the PNG, and together with the PDF it is what
``--check`` polices.

Standard library only, on purpose. matplotlib is not installed on the Pi, the
GCP images, or this checkout, and a plot that needs a pip install is a plot
nobody regenerates. That extends to the PDF, which is written here rather than
converted by an external tool, so it stays byte-reproducible. The SVG and PDF
are emitted as sorted, fixed-precision text, so re-running with unchanged
inputs produces an empty diff. Only the PNG needs an outside program, and it is
the one output ``--check`` deliberately ignores.

**The cross-machine rule lives here.** RDTSC counts reference cycles, so
cycles/byte compares within one machine and never across two with different
nominal frequencies -- the Pi's perf_event counter and a Xeon's RDTSC are not
the same unit. Any figure that puts several machines on one axis therefore
plots a *ratio* measured within each machine, which is dimensionless and does
transfer. Absolute cycles/byte appears only in the CSVs, where the machine
column travels with it, and in the three single-machine figures.

Usage:  python3 scripts/benchmarks/publish_results.py [--check] [--no-png]

``--check`` regenerates into a temporary directory and exits non-zero if the
committed output would change, which is what CI can run.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import re
import shutil
import subprocess
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
# moving turbo ceiling straight into cycles/byte, a shared cloud vCPU hides
# steal time, and a four-core desktop host cannot hold an unpinned 48-row curve
# under the CV bar -- and they are archived because they built the harness, not
# because their numbers are publishable. The figures below use only the runs
# marked publishable.
#
# A run can also be publishable while one of its columns is not: the tigerlake
# i-cache arms are quoted for icache_mpki, and the co-located arm says so in its
# own label rather than leaving a reader to assume its cycles/byte is quotable.
RUNS = {
    "pi5-matrix":                    ("Pi 5 / NEON",       "neon",   "pinned bare metal"),
    "pi-arm-matrix":                 ("Pi 5 / scalar",     "scalar", "pinned bare metal"),
    "pi-arm-baseline":               ("Pi 5 / scalar",     "scalar", "pinned bare metal"),
    "pi-arm-scaling":                ("Pi 5 / scalar",     "scalar", "bare metal; unpinned by design"),
    "sapphire-rapids-matrix":        ("Sapphire Rapids",   "avx512", "cloud; dedicated vCPU"),
    "sapphire-rapids-float-convert": ("Sapphire Rapids",   "avx512", "cloud; dedicated vCPU"),
    "skylake-sp-matrix":             ("Skylake-SP",        "avx512", "cloud; dedicated vCPU"),
    "cascadelake-matrix":            ("Cascade Lake 4v",   "avx512", "cloud; dedicated vCPU"),
    # The three pinned-backend runs behind issue #27. Same binary, same CPU,
    # VPHILOX_BACKEND forced -- so they are one measurement in three files and
    # belong in a comparison, not on the cross-machine figures.
    "cascadelake-scalar":            ("Cascade Lake 4v",   "scalar", "cloud; dedicated vCPU"),
    "cascadelake-avx2":              ("Cascade Lake 4v",   "avx2",   "cloud; dedicated vCPU"),
    "cascadelake-avx512":            ("Cascade Lake 4v",   "avx512", "cloud; dedicated vCPU"),
    # A second Cascade Lake, two sockets and sixteen physical cores. Same part
    # (family 6 model 85 stepping 7, 2.80 GHz nominal) but a whole-socket
    # instance rather than two cores of a shared one, which is why it gets its
    # own label: cycles/byte does not transfer between them, and the AVX-512
    # width conversion measured here disagrees with the 4-vCPU host.
    "cascadelake-32v-avx512-matrix": ("Cascade Lake 32v",  "avx512", "cloud; whole 2-socket host"),
    "cascadelake-32v-avx2-matrix":   ("Cascade Lake 32v",  "avx2",   "cloud; whole 2-socket host"),
    "cascadelake-32v-scalar-matrix": ("Cascade Lake 32v",  "scalar", "cloud; whole 2-socket host"),
    # Thread scaling. The unpinned run follows the documented protocol and is
    # the one to quote for the curve shape; its high CV above eight threads is
    # itself the finding, because the scheduler is free to land two workers on
    # one core's hyperthread siblings. The placement runs below fix that
    # placement by hand, which is what identifies the knee (issue #51).
    "cascadelake-32v-scaling":              ("Cascade Lake 32v", "avx512",
                                             "cloud; unpinned by design"),
    "cascadelake-32v-placement-phys-2socket": ("Cascade Lake 32v", "avx512",
                                             "cloud; one thread per physical core; 2 sockets"),
    "cascadelake-32v-placement-phys-1socket": ("Cascade Lake 32v", "avx512",
                                             "cloud; one thread per physical core; 1 socket"),
    "cascadelake-32v-placement-ht-1socket":   ("Cascade Lake 32v", "avx512",
                                             "cloud; hyperthread siblings; 1 socket"),
    "cascadelake-32v-placement-ht-2socket":   ("Cascade Lake 32v", "avx512",
                                             "cloud; every logical CPU"),
    "avx2-baseline":                 ("Tiger Lake / AVX2", "avx2",   "harness validation: unpinned governor"),
    "bulk-generate-baseline":        ("Tiger Lake / AVX2", "avx2",   "harness validation: unpinned governor"),
    "avx512-baseline":               ("Ice Lake SP",       "scalar", "harness validation: 2-vCPU cloud VM"),
    # Tiger Lake laptop, bare metal, turbo disabled so the core clock equals the
    # TSC rate (base 3.1 GHz) and cycles/byte is true core cycles rather than
    # reference cycles. This is the host that finally satisfied #51's placement
    # gate off a VM: 38.9-40.6% co-location penalty against the 15% threshold.
    #
    # The i-cache arms answer #53 and are quoted for icache_mpki only -- the
    # counter ioctls land in wall time, so their bytes_per_second is not
    # comparable with any scaling curve.
    "tigerlake-icache-phys-icache":  ("Tiger Lake / AVX-512", "avx512",
                                      "bare metal; one thread per physical core"),
    "tigerlake-icache-ht-icache":    ("Tiger Lake / AVX-512", "avx512",
                                      "bare metal; hyperthread siblings; cycles/byte above CV bar"),
    # The #52 runtime comparison. 65536 words is 256 KiB per worker, which stays
    # cache-resident at four workers; the 1 MiB size does not (4 MiB per worker
    # against an 8 MiB L3), so it measures #51's memory limit instead of the
    # port contention this issue is about.
    "tigerlake-omp-l1-default-scaling": ("Tiger Lake / AVX-512", "avx512",
                                      "bare metal; OpenMP vs std::thread, default wait policy"),
    "tigerlake-omp-l1-passive-scaling": ("Tiger Lake / AVX-512", "avx512",
                                      "bare metal; OpenMP vs std::thread, OMP_WAIT_POLICY=passive"),
    # The full 1..32 curve under the documented protocol. Archived for the shape
    # and for provenance, NOT quotable row by row: a four-core desktop host
    # cannot hold an unpinned 48-row curve under the 1% bar, and 30 of 48 rows
    # missed it. The focused runs above are the ones the write-up quotes.
    "tigerlake-scaling":             ("Tiger Lake / AVX-512", "avx512",
                                      "harness validation: 4-core desktop host, most rows above CV bar"),
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
# ------------------------------------------------------------- typography

# Helvetica / Helvetica-Bold advance widths in 1/1000 em, for printable ASCII.
# They are here for one reason: PDF has no text-anchor, so the canvas must
# measure a string before it can centre or right-align it. Measuring also lets
# legends and value labels size their own boxes instead of guessing, which is
# most of why the old figures crowded.
#
# Every backend therefore draws in the Helvetica / Arial / Liberation Sans
# metric family, identical on every platform CI touches. A prettier face would
# have to be embedded and subset to keep the PDF self-contained, and would then
# disagree with the SVG about where a centred label starts. Legibility here is
# bought with size, weight and spacing rather than with a typeface.
_W_REG = (
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,
    1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,
    333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
    556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584)
_W_BOLD = (
    278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 333, 333, 584, 584, 584, 611,
    975, 722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 333, 278, 333, 584, 556,
    333, 556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556, 278, 889, 611, 611,
    611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584)

# The handful of non-ASCII characters the captions use, mapped to their
# WinAnsiEncoding byte and Helvetica width. Anything outside this set is
# transliterated rather than silently dropped -- a missing minus sign in an
# axis label is the kind of error nobody notices in review.
_WINANSI = {
    "–": (0x96, 556, 556),   # en dash
    "—": (0x97, 1000, 1000),  # em dash
    "×": (0xD7, 584, 584),   # multiplication sign
    "÷": (0xF7, 584, 584),   # division sign
    "±": (0xB1, 584, 584),
    "°": (0xB0, 400, 400),
    "µ": (0xB5, 556, 611),
    "→": (0x2D, 333, 333),   # arrow -> hyphen; Helvetica has no arrow
    "‘": (0x91, 222, 278),
    "’": (0x92, 222, 278),
    "“": (0x93, 333, 500),
    "”": (0x94, 333, 500),
}


def text_width(s, size, bold=False):
    """Advance width of `s` at `size`, in the same units the canvas draws in."""
    table = _W_BOLD if bold else _W_REG
    total = 0
    for ch in str(s):
        o = ord(ch)
        if 32 <= o <= 126:
            total += table[o - 32]
        elif ch in _WINANSI:
            total += _WINANSI[ch][2 if bold else 1]
        else:
            total += table[ord("?") - 32]
    return total * size / 1000.0


# ------------------------------------------------------------ design tokens

# Okabe-Ito, the colourblind-safe qualitative set, reordered so the first four
# -- the count most figures here need -- are the most widely separated. Every
# entry clears 3:1 against white, which is the WCAG bar for a graphical object
# carrying meaning.
PALETTE = ["#0072B2", "#D55E00", "#009E73", "#7B52AB", "#B07A00", "#1F8A9E"]

# Series are also distinguished by marker shape, so the figures survive
# greyscale printing and every form of colour blindness. Colour alone is never
# the only encoding.
MARKERS = ["circle", "square", "triangle", "diamond", "down", "cross"]

INK       = "#16191D"   # titles
INK_BODY  = "#454B54"   # decks and axis titles
INK_MUTED = "#6B7280"   # tick labels, annotations
GRID      = "#E6E9ED"   # gridlines: present, never competing with the data
AXIS      = "#B9BFC7"
BAND      = "#F5F7F9"   # zebra band behind a row group
REFERENCE = "#8A9099"   # the "1.0x" / "single thread" datum lines
PAPER     = "#FFFFFF"

# 8pt spacing rhythm, and a type scale with real steps between the sizes.
S = 8.0
FS_TITLE, FS_DECK, FS_AXIS, FS_TICK, FS_VALUE, FS_LEGEND, FS_NOTE = 17, 12, 12, 11.5, 11, 12, 10.5


# ------------------------------------------------------------------ canvas

class Canvas:
    """Records draw calls once and renders them to SVG and to PDF.

    Two backends off one op list rather than two drawing routines, because the
    figures have to agree: the PNG in the docs and the PDF in the paper must be
    the same picture, and the only way to guarantee that is for neither to have
    its own layout code. Coordinates are top-left origin in points; the PDF
    writer flips y on the way out.
    """

    def __init__(self, width, height, title):
        self.w, self.h, self.title = float(width), float(height), title
        self.ops = []

    # -- primitives ------------------------------------------------------

    def rect(self, x, y, w, h, fill, opacity=1.0, rx=0.0):
        if w > 0 and h > 0:
            self.ops.append(("rect", float(x), float(y), float(w), float(h), fill,
                             float(opacity), float(rx)))

    def line(self, x1, y1, x2, y2, stroke=GRID, width=1.0, dash=None):
        self.ops.append(("line", float(x1), float(y1), float(x2), float(y2), stroke,
                         float(width), dash))

    def polyline(self, points, stroke, width=2.0, dash=None):
        pts = [(float(x), float(y)) for x, y in points]
        if len(pts) >= 2:
            self.ops.append(("poly", pts, stroke, float(width), dash))

    def marker(self, x, y, fill, shape="circle", r=4.0, halo=PAPER):
        """A data point. `halo` is a ring in the paper colour so overlapping
        series stay countable where the lines cross."""
        self.ops.append(("marker", float(x), float(y), fill, shape, float(r), halo))

    def text(self, x, y, s, size=FS_TICK, anchor="start", fill=INK_BODY, bold=False):
        s = str(s)
        w = text_width(s, size, bold)
        x = float(x) - (w if anchor == "end" else w / 2.0 if anchor == "middle" else 0.0)
        self.ops.append(("text", x, float(y), s, float(size), fill, bool(bold)))

    def vtext(self, x, y, s, size=FS_AXIS, fill=INK_BODY, bold=False):
        """Rotated -90 degrees and centred on y: the y-axis title."""
        s = str(s)
        self.ops.append(("vtext", float(x), float(y) + text_width(s, size, bold) / 2.0,
                         s, float(size), fill, bool(bold)))

    # -- output ----------------------------------------------------------

    def save(self, stem):
        """Write <stem>.svg and <stem>.pdf, and return self so the caller can
        hand the op list to a rasteriser."""
        os.makedirs(os.path.dirname(stem), exist_ok=True)
        self.stem = stem
        _write(stem + ".svg", self._svg())
        _write(stem + ".pdf", self._pdf(), binary=True)
        return self

    # -- SVG -------------------------------------------------------------

    def _svg(self):
        out = [
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{n(self.w)}" '
            f'height="{n(self.h)}" viewBox="0 0 {n(self.w)} {n(self.h)}" '
            f'font-family="Helvetica, Arial, \'Liberation Sans\', sans-serif" '
            f'font-variant-numeric="tabular-nums" role="img" '
            f'aria-label="{esc(self.title)}">',
            f"<title>{esc(self.title)}</title>",
            f'<rect width="{n(self.w)}" height="{n(self.h)}" fill="{PAPER}"/>',
        ]
        for op in self.ops:
            kind = op[0]
            if kind == "rect":
                _, x, y, w, h, fill, opacity, rx = op
                r = f' rx="{n(rx)}"' if rx else ""
                o = f' fill-opacity="{n(opacity)}"' if opacity != 1.0 else ""
                out.append(f'<rect x="{n(x)}" y="{n(y)}" width="{n(w)}" height="{n(h)}" '
                           f'fill="{fill}"{o}{r}/>')
            elif kind == "line":
                _, x1, y1, x2, y2, stroke, width, dash = op
                d = f' stroke-dasharray="{dash}"' if dash else ""
                out.append(f'<line x1="{n(x1)}" y1="{n(y1)}" x2="{n(x2)}" y2="{n(y2)}" '
                           f'stroke="{stroke}" stroke-width="{n(width)}"'
                           f' stroke-linecap="round"{d}/>')
            elif kind == "poly":
                _, pts, stroke, width, dash = op
                d = f' stroke-dasharray="{dash}"' if dash else ""
                s = " ".join(f"{n(x)},{n(y)}" for x, y in pts)
                out.append(f'<polyline points="{s}" fill="none" stroke="{stroke}" '
                           f'stroke-width="{n(width)}" stroke-linejoin="round" '
                           f'stroke-linecap="round"{d}/>')
            elif kind == "marker":
                _, x, y, fill, shape, r, halo = op
                if halo:
                    out.append(_svg_shape(x, y, r + 1.6, shape, halo))
                out.append(_svg_shape(x, y, r, shape, fill))
            elif kind == "text":
                _, x, y, s, size, fill, bold = op
                wt = ' font-weight="600"' if bold else ""
                out.append(f'<text x="{n(x)}" y="{n(y)}" font-size="{n(size)}" '
                           f'fill="{fill}"{wt}>{esc(s)}</text>')
            elif kind == "vtext":
                _, x, y, s, size, fill, bold = op
                wt = ' font-weight="600"' if bold else ""
                out.append(f'<text x="{n(x)}" y="{n(y)}" font-size="{n(size)}" '
                           f'fill="{fill}"{wt} transform="rotate(-90 {n(x)} {n(y)})">'
                           f'{esc(s)}</text>')
        out.append("</svg>")
        return "\n".join(out) + "\n"

    # -- PDF -------------------------------------------------------------

    def _pdf(self):
        """A single-page PDF 1.4 using the base-14 fonts.

        LaTeX cannot \\includegraphics an SVG without shell-escape and an
        Inkscape round trip, so the paper needs a real vector format; PDF is
        the one it takes natively. Writing it here rather than shelling out to
        a converter keeps this script standard-library only, keeps the figure
        byte-reproducible so --check can police it, and means the PDF is
        generated from the same draw calls as everything else rather than
        being a translation of a translation.
        """
        c = []
        c.append("q 1 w 1 J 1 j")
        for op in self.ops:
            kind = op[0]
            if kind == "rect":
                _, x, y, w, h, fill, opacity, _rx = op
                c.append(f"q {_pdf_rgb(fill)} rg")
                if opacity != 1.0:
                    # A named ExtGState per opacity would be tidier; every
                    # translucent fill here sits on white, so compositing it
                    # by hand keeps the file to a single resource dictionary.
                    c[-1] = f"q {_pdf_rgb(_blend(fill, PAPER, opacity))} rg"
                c.append(f"{n(x)} {n(self.h - y - h)} {n(w)} {n(h)} re f Q")
            elif kind == "line":
                _, x1, y1, x2, y2, stroke, width, dash = op
                c.append(f"q {_pdf_rgb(stroke)} RG {n(width)} w {_pdf_dash(dash)}")
                c.append(f"{n(x1)} {n(self.h - y1)} m {n(x2)} {n(self.h - y2)} l S Q")
            elif kind == "poly":
                _, pts, stroke, width, dash = op
                c.append(f"q {_pdf_rgb(stroke)} RG {n(width)} w {_pdf_dash(dash)}")
                head = f"{n(pts[0][0])} {n(self.h - pts[0][1])} m"
                tail = " ".join(f"{n(x)} {n(self.h - y)} l" for x, y in pts[1:])
                c.append(f"{head} {tail} S Q")
            elif kind == "marker":
                _, x, y, fill, shape, r, halo = op
                if halo:
                    c.append(_pdf_shape(x, self.h - y, r + 1.6, shape, halo))
                c.append(_pdf_shape(x, self.h - y, r, shape, fill))
            elif kind in ("text", "vtext"):
                _, x, y, s, size, fill, bold = op
                font = "/F2" if bold else "/F1"
                yy = self.h - y
                place = (f"1 0 0 1 {n(x)} {n(yy)} Tm" if kind == "text"
                         else f"0 1 -1 0 {n(x)} {n(yy)} Tm")
                c.append(f"BT {font} {n(size)} Tf {_pdf_rgb(fill)} rg {place} "
                         f"({_pdf_str(s)}) Tj ET")
        c.append("Q")
        stream = "\n".join(c).encode("latin-1")

        objs = [
            b"<</Type/Catalog/Pages 2 0 R>>",
            b"<</Type/Pages/Kids[3 0 R]/Count 1>>",
            (f"<</Type/Page/Parent 2 0 R/MediaBox[0 0 {n(self.w)} {n(self.h)}]"
             f"/Resources<</Font<</F1 5 0 R/F2 6 0 R>>>>/Contents 4 0 R>>").encode("latin-1"),
            b"<</Length " + str(len(stream)).encode() + b">>\nstream\n" + stream + b"\nendstream",
            b"<</Type/Font/Subtype/Type1/BaseFont/Helvetica/Encoding/WinAnsiEncoding>>",
            b"<</Type/Font/Subtype/Type1/BaseFont/Helvetica-Bold/Encoding/WinAnsiEncoding>>",
        ]
        # No /CreationDate and a content-derived /ID: two runs over unchanged
        # inputs must produce identical bytes or --check reports every figure
        # stale on every commit.
        doc = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
        offsets = []
        for i, body in enumerate(objs, start=1):
            offsets.append(len(doc))
            doc += f"{i} 0 obj\n".encode() + body + b"\nendobj\n"
        xref = len(doc)
        doc += f"xref\n0 {len(objs) + 1}\n0000000000 65535 f \n".encode()
        for off in offsets:
            doc += f"{off:010d} 00000 n \n".encode()
        ident = hashlib.sha256(stream).hexdigest()[:32].upper()
        doc += (f"trailer\n<</Size {len(objs) + 1}/Root 1 0 R/ID[<{ident}><{ident}>]>>\n"
                f"startxref\n{xref}\n%%EOF\n").encode()
        return bytes(doc)


def _write(path, data, binary=False):
    if binary:
        with open(path, "wb") as fh:
            fh.write(data)
    else:
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(data)


def _svg_shape(x, y, r, shape, fill):
    if shape == "circle":
        return f'<circle cx="{n(x)}" cy="{n(y)}" r="{n(r)}" fill="{fill}"/>'
    pts = _shape_points(x, y, r, shape)
    s = " ".join(f"{n(px)},{n(py)}" for px, py in pts)
    return f'<polygon points="{s}" fill="{fill}"/>'


def _pdf_shape(x, y, r, shape, fill):
    if shape == "circle":
        k = r * 0.5523
        return (f"q {_pdf_rgb(fill)} rg {n(x - r)} {n(y)} m "
                f"{n(x - r)} {n(y + k)} {n(x - k)} {n(y + r)} {n(x)} {n(y + r)} c "
                f"{n(x + k)} {n(y + r)} {n(x + r)} {n(y + k)} {n(x + r)} {n(y)} c "
                f"{n(x + r)} {n(y - k)} {n(x + k)} {n(y - r)} {n(x)} {n(y - r)} c "
                f"{n(x - k)} {n(y - r)} {n(x - r)} {n(y - k)} {n(x - r)} {n(y)} c f Q")
    pts = _shape_points(x, y, r, shape, flip=True)
    body = " ".join(f"{n(px)} {n(py)} l" for px, py in pts[1:])
    return (f"q {_pdf_rgb(fill)} rg {n(pts[0][0])} {n(pts[0][1])} m {body} h f Q")


def _shape_points(x, y, r, shape, flip=False):
    """Polygon vertices for the non-circular markers. `flip` negates the y
    offsets for PDF's bottom-up axis; the shapes are symmetric except the
    triangles, which must keep pointing the way the legend swatch does."""
    sy = -1.0 if flip else 1.0
    if shape == "square":
        a = r * 0.88
        offs = [(-a, -a), (a, -a), (a, a), (-a, a)]
    elif shape == "triangle":
        a = r * 1.16
        offs = [(0, -a), (a * 0.92, a * 0.72), (-a * 0.92, a * 0.72)]
    elif shape == "down":
        a = r * 1.16
        offs = [(0, a), (a * 0.92, -a * 0.72), (-a * 0.92, -a * 0.72)]
    elif shape == "diamond":
        a = r * 1.22
        offs = [(0, -a), (a, 0), (0, a), (-a, 0)]
    else:  # cross
        a, b = r * 1.15, r * 0.38
        offs = [(-b, -a), (b, -a), (b, -b), (a, -b), (a, b), (b, b),
                (b, a), (-b, a), (-b, b), (-a, b), (-a, -b), (-b, -b)]
    return [(x + dx, y + sy * dy) for dx, dy in offs]


def _pdf_rgb(hex_colour):
    r, g, b = _rgb(hex_colour)
    return f"{n(r / 255.0)} {n(g / 255.0)} {n(b / 255.0)}"


def _rgb(hex_colour):
    h = hex_colour.lstrip("#")
    return int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)


def _blend(fg, bg, alpha):
    f, b = _rgb(fg), _rgb(bg)
    return "#" + "".join(f"{round(f[i] * alpha + b[i] * (1 - alpha)):02x}" for i in range(3))


def _pdf_dash(dash):
    if not dash:
        return "[] 0 d"
    return "[" + " ".join(dash.split()) + "] 0 d"


def _pdf_str(s):
    out = []
    for ch in s:
        o = ord(ch)
        if 32 <= o <= 126:
            out.append("\\" + ch if ch in "()\\" else ch)
        elif ch in _WINANSI:
            out.append(f"\\{_WINANSI[ch][0]:03o}")
        else:
            out.append("?")
    return "".join(out)


def n(v):
    """Fixed precision so regenerating an unchanged plot is an empty diff."""
    s = f"{float(v):.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("", "-0") else s


def esc(s):
    return (str(s).replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;").replace('"', "&quot;"))
# ------------------------------------------------------------ figure frame

# One page width for every figure. Equal widths mean the five plots stack in
# the README without the reader re-calibrating their eye between them, and 980
# points renders to a 1960 px PNG at 2x, which is sharp on a retina display and
# still under a megabyte.
PAGE_W = 980.0
MARGIN = 4 * S


def header(cv, title, deck, width=PAGE_W):
    """Title, deck, and a hairline rule. Returns the y to start the next block.

    The deck is wrapped here rather than being pre-split into hand-placed
    lines, so editing a caption cannot silently push it off the canvas.
    """
    y = 3.5 * S
    cv.text(MARGIN, y, title, size=FS_TITLE, fill=INK, bold=True)
    y += 2.5 * S
    for line in _wrap(deck, width - 2 * MARGIN - 6 * S, FS_DECK):
        cv.text(MARGIN, y, line, size=FS_DECK, fill=INK_BODY)
        y += FS_DECK + 5
    y += S * 0.5
    cv.line(MARGIN, y, width - MARGIN, y, GRID, 1.0)
    return y + 2 * S


def _wrap(s, max_w, size, bold=False):
    lines, cur = [], ""
    for word in str(s).split():
        trial = word if not cur else cur + " " + word
        if text_width(trial, size, bold) > max_w and cur:
            lines.append(cur)
            cur = word
        else:
            cur = trial
    if cur:
        lines.append(cur)
    return lines


def legend(cv, x, y, entries, width=PAGE_W):
    """A horizontal, wrapping legend directly under the deck.

    Above the data rather than stranded in a right-hand gutter: it reads in the
    same left-to-right pass as the title, and it gives the plot area the ~200
    points the old gutter was spending on five short strings. `entries` are
    (label, colour, marker, dash) -- marker None draws a bar swatch instead, and
    dash draws the line style the series actually uses, so the legend answers
    "which of these is the dashed one" without a footnote.
    """
    avail = width - MARGIN - x
    pen_x, rows = x, 1
    for label, colour, marker, dash in entries:
        w = 26 + 6 + text_width(label, FS_LEGEND) + 3 * S
        if pen_x > x and pen_x + w - 3 * S > x + avail:
            pen_x, rows = x, rows + 1
            y += FS_LEGEND + 1.5 * S
        cy = y - FS_LEGEND * 0.34
        if marker is None:
            cv.rect(pen_x, cy - 5, 22, 10, colour, rx=2)
            tx = pen_x + 22 + 7
        else:
            cv.line(pen_x, cy, pen_x + 26, cy, colour, 2.4, dash=dash)
            cv.marker(pen_x + 13, cy, colour, marker, r=4.2)
            tx = pen_x + 26 + 7
        cv.text(tx, y, label, size=FS_LEGEND, fill=INK_BODY)
        pen_x = tx + text_width(label, FS_LEGEND) + 3 * S
    return y + FS_LEGEND + 1.5 * S


class Axes:
    """A plot rectangle plus the scales, grid and ticks that go with it.

    Gridlines are drawn before the data and in a colour two steps lighter than
    the old ones: a benchmark figure is read for the shape of the curves, and
    a grid dark enough to be counted is a grid competing with them.
    """

    def __init__(self, cv, x, y, w, h):
        self.cv, self.x, self.y, self.w, self.h = cv, x, y, w, h

    # -- scales ---------------------------------------------------------

    def linear_y(self, lo, hi):
        self._y = lambda v: self.y + self.h - (v - lo) / (hi - lo) * self.h
        self.ylo, self.yhi = lo, hi
        return self._y

    def log_y(self, lo, hi):
        a, b = math.log10(lo), math.log10(hi)
        self._y = lambda v: self.y + self.h - (math.log10(v) - a) / (b - a) * self.h
        self.ylo, self.yhi = lo, hi
        return self._y

    def log_x(self, lo, hi, pad=4.0 * S):
        a, b = math.log2(lo), math.log2(hi)
        self._x = lambda v: self.x + pad + (math.log2(v) - a) / (b - a) * (self.w - 2 * pad)
        return self._x

    def linear_x(self, lo, hi, pad=0.0):
        self._x = lambda v: self.x + pad + (v - lo) / (hi - lo) * (self.w - 2 * pad)
        return self._x

    # -- furniture ------------------------------------------------------

    def hgrid(self, values, label):
        for v in values:
            yy = self._y(v)
            self.cv.line(self.x, yy, self.x + self.w, yy, GRID, 1.0)
            self.cv.text(self.x - S, yy + FS_TICK * 0.36, label(v), size=FS_TICK,
                         anchor="end", fill=INK_MUTED)

    def xticks(self, values, label, sub=None):
        for v in values:
            xx = self._x(v)
            self.cv.text(xx, self.y + self.h + 2.5 * S, label(v), size=FS_TICK,
                         anchor="middle", fill=INK_MUTED)
            if sub:
                self.cv.text(xx, self.y + self.h + 4.4 * S, sub(v), size=FS_NOTE,
                             anchor="middle", fill="#9AA1AA")

    def xtitle(self, s, deep=False):
        self.cv.text(self.x + self.w / 2.0, self.y + self.h + (6.6 if deep else 5.0) * S,
                     s, size=FS_AXIS, anchor="middle", fill=INK_BODY)

    def ytitle(self, s):
        self.cv.vtext(self.x - 6.0 * S, self.y + self.h / 2.0, s, size=FS_AXIS, fill=INK_BODY)

    def baseline(self):
        self.cv.line(self.x, self.y + self.h, self.x + self.w, self.y + self.h, AXIS, 1.2)

    def datum(self, v, text, align="left", below=False):
        """A horizontal reference line -- 1.0x, or the single-thread cost.

        Labelled in place. The old figures drew this line in the same weight as
        the grid, which made the one line the reader is supposed to measure
        against indistinguishable from the ones they are not.
        """
        if abs(v - self.ylo) < 1e-9:
            return  # it would land exactly on the axis line
        yy = self._y(v)
        self.cv.line(self.x, yy, self.x + self.w, yy, REFERENCE, 1.4, dash="5 4")
        tw = text_width(text, FS_NOTE)
        tx = self.x + self.w - tw - S if align == "right" else self.x + S
        ty = yy + FS_NOTE + 6 if below else yy - 6
        self.cv.rect(tx - 4, ty - FS_NOTE - 2, tw + 8, FS_NOTE + 7, PAPER, 0.9, rx=2)
        self.cv.text(tx, ty, text, size=FS_NOTE, fill=INK_MUTED)


def series_style(i):
    return PALETTE[i % len(PALETTE)], MARKERS[i % len(MARKERS)]


def draw_series(ax, pts, colour, marker, dash=None, width=2.4):
    ax.cv.polyline([(ax._x(a), ax._y(b)) for a, b in pts], colour, width, dash=dash)
    for a, b in pts:
        ax.cv.marker(ax._x(a), ax._y(b), colour, marker, r=4.2)


# ----------------------------------------------------------------- figures

def plot_matrix_relative(runs, outdir):
    """Cycles/byte relative to std::mt19937, per machine. Lower is faster."""
    tags = ["pi5-matrix", "sapphire-rapids-matrix", "skylake-sp-matrix", "cascadelake-matrix",
            "cascadelake-32v-avx512-matrix"]
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
    if not series:
        return

    row_h, bar_h, group_gap = 20.0, 14.0, 2.0 * S
    label_w = 24.0 * S
    left = MARGIN + label_w
    plot_w = PAGE_W - left - 9.0 * S

    top_val = max(v for _, vals in series for v in vals.values())
    xmax = math.ceil(top_val * 2) / 2.0

    group_h = len(series) * row_h + group_gap
    body_h = len(MATRIX_ROWS) * group_h
    # Two probe passes: the header and legend wrap to a height that depends on
    # the strings, so the canvas cannot be sized until they have been laid out.
    probe = Canvas(PAGE_W, 10, "")
    y0 = legend(probe, MARGIN, header(probe, "t", DECK_MATRIX),
                [(m, PALETTE[i], None, None) for i, (m, _) in enumerate(series)])
    height = y0 + body_h + 8.5 * S

    cv = Canvas(PAGE_W, height, "Throughput relative to std::mt19937")
    y = header(cv, "Cost per byte, relative to std::mt19937 on the same machine", DECK_MATRIX)
    y = legend(cv, MARGIN, y, [(m, PALETTE[i], None, None) for i, (m, _) in enumerate(series)])
    y += S

    ax = Axes(cv, left, y, plot_w, body_h)
    ax.linear_x(0.0, xmax)

    ticks = [i * 0.5 for i in range(int(xmax / 0.5) + 1)]
    for v in ticks:
        cv.line(ax._x(v), y, ax._x(v), y + body_h, GRID, 1.0)
        cv.text(ax._x(v), y + body_h + 2.5 * S, f"{v:g}x", size=FS_TICK, anchor="middle",
                fill=INK_MUTED)
    ax.baseline()
    ax.xtitle("cycles per byte ÷ std::mt19937 on the same machine — lower is faster")

    yy = y
    for gi, (key, pretty) in enumerate(MATRIX_ROWS):
        ours = key.startswith("BM_vphilox")
        # The two vphilox rows carry a heavier band and an accent rule: this
        # figure exists to be read against them, and hunting for them among six
        # identically-weighted groups is work the figure should be doing.
        cv.rect(MARGIN, yy - 3, PAGE_W - 2 * MARGIN, group_h - group_gap + 6,
                BAND, 1.0 if ours else 0.55, rx=3)
        if ours:
            cv.rect(MARGIN, yy - 3, 3, group_h - group_gap + 6, PALETTE[0], 0.55, rx=1.5)
        cv.text(left - 1.5 * S, yy + (group_h - group_gap) / 2 + FS_AXIS * 0.36, pretty,
                size=FS_AXIS, anchor="end", fill=INK if ours else INK_BODY, bold=ours)
        for i, (_machine, vals) in enumerate(series):
            v = vals.get(key)
            by = yy + i * row_h + (row_h - bar_h) / 2
            if v is None:
                cv.text(left + S, by + bar_h - 3, "not run", size=FS_NOTE, fill="#9AA1AA")
                continue
            cv.rect(left, by, ax._x(v) - left, bar_h, PALETTE[i], rx=1.5)
            cv.text(ax._x(v) + 6, by + bar_h - 3, f"{v:.2f}x", size=FS_VALUE, fill=INK_MUTED)
        yy += group_h

    # Drawn last so it sits over the bars it is there to be compared against.
    cv.line(ax._x(1.0), y, ax._x(1.0), y + body_h, REFERENCE, 1.4, dash="5 4")
    return cv.save(os.path.join(outdir, PLOT_OUT, "matrix-relative"))


DECK_MATRIX = ("Lower is faster. Every bar is a ratio measured inside one machine, never a "
               "raw cycles/byte: RDTSC counts reference cycles, so the absolute unit does not "
               "carry across hosts. The dashed rule is std::mt19937 itself.")


def plot_generate_n_sweep(runs, outdir):
    """The refill-buffer crossover: cost per call size, relative to bulk."""
    # bulk-generate-baseline has the richest sweep but an unpinned governor, so
    # it is deliberately not here -- see the quality column in machines.csv.
    tags = ["pi5-matrix", "sapphire-rapids-matrix", "skylake-sp-matrix", "cascadelake-matrix",
            "cascadelake-32v-avx512-matrix"]
    tags = [t for t in tags if t in runs and t in PUBLISHABLE]

    series = []
    for t in tags:
        rows = runs[t]["rows"]
        bulk = rows.get("BM_vphilox_bulk", {}).get("median")
        buffered = rows.get("BM_vphilox", {}).get("median")
        if not bulk:
            continue
        pts = [(s, rows[f"BM_vphilox_generate_n/{s}"]["median"] / bulk)
               for s in SWEEP_SIZES
               if rows.get(f"BM_vphilox_generate_n/{s}", {}).get("median")]
        if pts:
            series.append((label_of(runs[t]), pts, (buffered / bulk) if buffered else None))
    if not series:
        return

    deck = ("1.00x is the raw kernel writing straight into the caller's buffer. A short call "
            "cannot amortise the refill, so the cost climbs to the left; the dashed line per "
            "machine is where operator() sits. Ratios within each machine.")
    ymax = math.ceil(max(max(v for _, v in pts) for _, pts, _ in series) * 10) / 10.0
    ymax = max(ymax, math.ceil(max((b for _, _, b in series if b), default=0) * 10) / 10.0)

    entries = [(m, PALETTE[i], MARKERS[i], None) for i, (m, _, _) in enumerate(series)]
    probe = Canvas(PAGE_W, 10, "")
    y0 = legend(probe, MARGIN, header(probe, "t", deck), entries)
    plot_h = 340.0
    height = y0 + plot_h + 8.0 * S

    cv = Canvas(PAGE_W, height, "Cost per generate_n call size")
    y = header(cv, "What a short generate_n call costs, against the bulk path", deck)
    y = legend(cv, MARGIN, y, entries)
    y += 1.5 * S

    left = MARGIN + 7.5 * S
    ax = Axes(cv, left, y, PAGE_W - left - MARGIN, plot_h)
    ax.linear_y(1.0, ymax)
    ax.log_x(SWEEP_SIZES[0], SWEEP_SIZES[-1])

    step = 0.1 if (ymax - 1.0) <= 1.0 else 0.25
    ax.hgrid(_frange(1.0, ymax, step), lambda v: f"{v:.2f}x")
    ax.xticks(SWEEP_SIZES, lambda v: f"{v:,}")
    ax.baseline()
    ax.xtitle("words per generate_n call")
    ax.ytitle("cost ÷ bulk kernel — 1.00x is the raw kernel")

    for i, (_m, pts, buffered) in enumerate(series):
        colour, marker = series_style(i)
        if buffered and buffered <= ymax:
            cv.line(left, ax._y(buffered), left + ax.w, ax._y(buffered), colour, 1.3, dash="2 4")
        draw_series(ax, pts, colour, marker)
    cv.text(left + S, y + 1.6 * S, "dotted: operator() through the refill buffer",
            size=FS_NOTE, fill=INK_MUTED)
    return cv.save(os.path.join(outdir, PLOT_OUT, "generate-n-sweep"))


def _frange(lo, hi, step):
    out, v = [], lo
    while v <= hi + 1e-9:
        out.append(round(v, 6))
        v += step
    return out


def plot_thread_scaling(runs, outdir):
    """One machine, so absolute cycles/byte is the honest axis here."""
    run = runs.get("pi-arm-scaling")
    if not run:
        return
    rows = run["rows"]
    threads = [1, 2, 4, 8, 16, 32]
    variants = [
        ("BM_thread_scaling", 65536, "buffered — 256 KiB", None),
        ("BM_thread_scaling_bulk", 65536, "generate_n — 256 KiB", None),
        ("BM_thread_scaling", 1048576, "buffered — 4 MiB", "7 4"),
        ("BM_thread_scaling_bulk", 1048576, "generate_n — 4 MiB", "7 4"),
    ]
    series = []
    for prefix, ws, name, dash in variants:
        pts = [(t, rows[f"{prefix}/{t}/{ws}/real_time"]["median"]) for t in threads
               if rows.get(f"{prefix}/{t}/{ws}/real_time", {}).get("median")]
        if pts:
            series.append((name, pts, dash))
    if not series:
        return

    deck = ("Flat means linear scaling: twice the threads, twice the output, same cost per "
            "byte. Two visible lines rather than four — each working set lands on the "
            "other, which is the result. Past four threads the cores are oversubscribed.")
    ymax = math.ceil(max(v for _, pts, _ in series for _, v in pts) * 1.15 * 10) / 10.0

    entries = [(nm, PALETTE[i], MARKERS[i], d) for i, (nm, _, d) in enumerate(series)]
    probe = Canvas(PAGE_W, 10, "")
    y0 = legend(probe, MARGIN, header(probe, "t", deck), entries)
    plot_h = 330.0
    height = y0 + plot_h + 8.0 * S

    cv = Canvas(PAGE_W, height, "Thread scaling on the Raspberry Pi 5")
    y = header(cv, "Aggregate cost per byte against thread count — Pi 5, 4 cores", deck)
    y = legend(cv, MARGIN, y, entries)
    y += 1.5 * S

    left = MARGIN + 7.5 * S
    ax = Axes(cv, left, y, PAGE_W - left - MARGIN, plot_h)
    ax.linear_y(0.0, ymax)
    ax.log_x(1, 32)
    ax.hgrid(_frange(0.0, ymax, 0.5), lambda v: f"{v:g}")
    ax.xticks(threads, str)
    ax.baseline()
    ax.xtitle("worker threads")
    ax.ytitle("aggregate cycles/byte")

    xc = ax._x(4)
    cv.line(xc, y, xc, y + plot_h, REFERENCE, 1.4, dash="5 4")
    cv.text(xc + 6, y + 1.7 * S, "4 physical cores", size=FS_NOTE, fill=INK_MUTED)

    for i, (_nm, pts, dash) in enumerate(series):
        colour, marker = series_style(i)
        draw_series(ax, pts, colour, marker, dash=dash)
    return cv.save(os.path.join(outdir, PLOT_OUT, "thread-scaling"))


def plot_scaling_placement(runs, outdir):
    """Where the threads sit, on one machine, at a fixed thread count.

    The unpinned curve conflates three separate limits because the scheduler
    picks the placement. Fixing the placement by hand separates them: with one
    thread per physical core the cost per byte is flat to sixteen cores, and
    the knee only appears when two workers share a core's hyperthread siblings
    or when one socket's memory controllers have to feed eight of them.
    """
    variants = [
        ("cascadelake-32v-placement-phys-2socket", 65536,
         "1/core, 2 sockets — 256 KiB", [1, 2, 4, 8, 16], None),
        ("cascadelake-32v-placement-phys-2socket", 1048576,
         "1/core, 2 sockets — 4 MiB", [1, 2, 4, 8, 16], "7 4"),
        ("cascadelake-32v-placement-phys-1socket", 65536,
         "1/core, 1 socket — 256 KiB", [1, 2, 4, 8], None),
        ("cascadelake-32v-placement-phys-1socket", 1048576,
         "1/core, 1 socket — 4 MiB", [1, 2, 4, 8], "7 4"),
        ("cascadelake-32v-placement-ht-1socket", 65536,
         "hyperthread siblings — 256 KiB", [8, 16], None),
        ("cascadelake-32v-placement-ht-1socket", 1048576,
         "hyperthread siblings — 4 MiB", [8, 16], "7 4"),
    ]
    series = []
    for tag, ws, name, threads, dash in variants:
        run = runs.get(tag)
        if not run:
            continue
        pts = [(t, run["rows"][f"BM_thread_scaling_bulk/{t}/{ws}/real_time"]["median"])
               for t in threads
               if run["rows"].get(f"BM_thread_scaling_bulk/{t}/{ws}/real_time", {}).get("median")]
        if pts:
            series.append((name, pts, dash))
    if not series:
        return

    deck = ("generate_n, aggregate cost per byte. One thread per physical core stays flat all "
            "the way to sixteen; putting the same sixteen workers on eight cores' hyperthread "
            "siblings costs about 35% each. Dashed lines are the 4 MiB working set.")
    ymax = math.ceil(max(v for _, pts, _ in series for _, v in pts) * 1.06 * 10) / 10.0

    entries = [(nm, PALETTE[i], MARKERS[i], d) for i, (nm, _, d) in enumerate(series)]
    probe = Canvas(PAGE_W, 10, "")
    y0 = legend(probe, MARGIN, header(probe, "t", deck), entries)
    plot_h = 340.0
    height = y0 + plot_h + 8.0 * S

    cv = Canvas(PAGE_W, height, "Where the scaling knee comes from")
    y = header(cv, "Thread placement, not thread count — Cascade Lake, 2 sockets, "
                   "16 cores", deck)
    y = legend(cv, MARGIN, y, entries)
    y += 1.5 * S

    left = MARGIN + 7.5 * S
    ax = Axes(cv, left, y, PAGE_W - left - MARGIN, plot_h)
    ax.linear_y(0.0, ymax)
    ax.log_x(1, 16)
    ax.hgrid(_frange(0.0, ymax, 0.1), lambda v: f"{v:.1f}")
    ax.xticks([1, 2, 4, 8, 16], str)
    ax.baseline()
    ax.xtitle("worker threads")
    ax.ytitle("aggregate cycles/byte")

    # The single-thread cost is the line every series would sit on if the
    # generator scaled perfectly; drawing it makes each departure readable
    # without comparing two numbers by eye.
    base = series[0][1][0][1]
    ax.datum(base, f"single thread, {base:.4f} cycles/byte", align="right", below=True)

    for i, (_nm, pts, dash) in enumerate(series):
        colour, marker = series_style(i)
        draw_series(ax, pts, colour, marker, dash=dash)
    return cv.save(os.path.join(outdir, PLOT_OUT, "scaling-placement"))


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
        pts = [(s, rows[f"{prefix}/{s}"]["median"], rows[f"{prefix}/{s}"].get("cv_pct"))
               for s in sizes if rows.get(f"{prefix}/{s}", {}).get("median")]
        if pts:
            series.append((name, pts))
    if not series:
        return

    deck = ("Both axes are logarithmic. The three widths separate at 16 KiB and again at "
            "32 MiB, and converge at the 512 KiB row where the loop is bandwidth-bound rather "
            "than issue-bound. Ringed points missed this project's 1% CV bar.")
    vals = [v for _, pts in series for _, v, _ in pts]
    lo, hi = min(vals) / 1.35, max(vals) * 1.35

    entries = [(nm, PALETTE[i], MARKERS[i], None) for i, (nm, _) in enumerate(series)]
    probe = Canvas(PAGE_W, 10, "")
    y0 = legend(probe, MARGIN, header(probe, "t", deck), entries)
    plot_h = 330.0
    height = y0 + plot_h + 10.0 * S

    cv = Canvas(PAGE_W, height, "Float conversion width against working set")
    y = header(cv, "Float conversion cost against working set — Sapphire Rapids", deck)
    y = legend(cv, MARGIN, y, entries)
    y += 1.5 * S

    left = MARGIN + 7.5 * S
    ax = Axes(cv, left, y, PAGE_W - left - MARGIN, plot_h)
    ax.log_y(lo, hi)
    ax.log_x(sizes[0], sizes[-1])

    decades = []
    d = math.floor(math.log10(lo))
    while d <= math.ceil(math.log10(hi)):
        for mant in (1, 2, 5):
            v = mant * (10.0 ** d)
            if lo <= v <= hi:
                decades.append(v)
        d += 1
    ax.hgrid(decades, lambda v: f"{v:g}")

    # 32 MiB is past L2 but still inside this host's 105 MiB L3, so the largest
    # row is named for its size rather than for a cache it does not leave.
    notes = {256: "2 KiB (tile)", 2048: "16 KiB", 65536: "512 KiB", 4194304: "32 MiB"}
    ax.xticks(sizes, lambda v: f"{v:,}", sub=lambda v: notes[v])
    ax.baseline()
    ax.xtitle("words converted per call", deep=True)
    ax.ytitle("cycles/byte")

    # Rings first, series on top: a point flagged for noise still has to show
    # its own colour and shape, so the flag is drawn behind it as a halo.
    noisy = False
    for _nm, pts in series:
        for sz, v, cvp in pts:
            if cvp is not None and cvp > 1.0:
                noisy = True
                cv.marker(ax._x(sz), ax._y(v), "#B3261E", "circle", r=8.5, halo=None)
                cv.marker(ax._x(sz), ax._y(v), PAPER, "circle", r=6.0, halo=None)
    for i, (_nm, pts) in enumerate(series):
        colour, marker = series_style(i)
        draw_series(ax, [(sz, v) for sz, v, _ in pts], colour, marker)
    if noisy:
        cv.text(left + S, y + 1.6 * S,
                "a ringed point missed this project's 1% cycles/byte CV bar",
                size=FS_NOTE, fill="#B3261E")
    return cv.save(os.path.join(outdir, PLOT_OUT, "float-conversion-widths"))
# ------------------------------------------------------------------- PNG

# Rasterised at 2x, so a 980-point figure lands at 1960 px: sharp on a
# high-DPI screen and in a printed draft, and still well under a megabyte.
PNG_SCALE = 2

# Supersampling factor for the Pillow path, which draws with no antialiasing of
# its own. Rendering at 4x the final size and box-filtering down is what keeps a
# 1-point gridline from turning into a staircase.
_OVERSAMPLE = 2

# ImageMagick is deliberately absent from this list. Without librsvg it falls
# back to its own MSVG renderer, which ignores stroke colours on <line> (every
# gridline comes out black) and drops <path> and <polyline> entirely -- so it
# does not fail, it silently publishes a different figure. A converter that
# lies is worse than no converter.
_CONVERTERS = ("rsvg-convert", "inkscape", "cairosvg")


def _convert_cmd(tool, svg, png, width):
    return {
        "rsvg-convert": [tool, "-w", str(width), "-o", png, svg],
        "inkscape": [tool, f"--export-filename={png}", f"--export-width={width}", svg],
        "cairosvg": [tool, svg, "-o", png, "-W", str(width)],
    }[tool]


def rasterize(canvases):
    """SVG -> PNG for every figure. Returns (tool, count).

    PNG is what the write-ups display: GitHub renders it in the README, in the
    mobile app and inside a PR diff, and it needs no font installed on the
    reader's machine. It is a *rendering* of the checked SVG rather than a
    second source, which is why --check does not police it -- see tree().

    Two ways to get one. A real SVG renderer if the machine has one, else
    Pillow driving the same op list the SVG and PDF came from. Pillow is not in
    the standard library, so it stays optional and stays out of --check; it is
    listed second only because rsvg-convert is the reference implementation, not
    because it is better here.
    """
    if not canvases:
        return None, 0
    for tool in _CONVERTERS:
        if shutil.which(tool):
            return tool, _rasterize_external(tool, canvases)
    try:
        from PIL import Image  # noqa: F401
    except ImportError:
        return None, 0
    return "pillow", sum(_rasterize_pillow(cv) for cv in canvases)


def _rasterize_external(tool, canvases):
    done = 0
    for cv in canvases:
        cmd = _convert_cmd(tool, cv.stem + ".svg", cv.stem + ".png",
                           int(round(cv.w)) * PNG_SCALE)
        try:
            subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
            done += 1
        except (subprocess.CalledProcessError, OSError) as exc:
            print(f"warning: {tool} failed on {os.path.basename(cv.stem)}: {exc}",
                  file=sys.stderr)
    return done


# Metric-compatible with the Helvetica widths this file lays out against, in
# the order a Linux, macOS and Windows box respectively will find one. If none
# of them resolve, Pillow's bitmap default would place every label wrong, so
# the renderer declines rather than publishing a misaligned figure.
_FONT_CANDIDATES = (
    ("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
     "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"),
    ("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
     "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf"),
    ("/usr/share/fonts/opentype/urw-base35/NimbusSans-Regular.otf",
     "/usr/share/fonts/opentype/urw-base35/NimbusSans-Bold.otf"),
    ("/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/Helvetica.ttc"),
    ("C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/arialbd.ttf"),
)


def _font_pair():
    for regular, bold in _FONT_CANDIDATES:
        if os.path.exists(regular) and os.path.exists(bold):
            return regular, bold
    return None


def _rasterize_pillow(cv):
    from PIL import Image, ImageDraw, ImageFont

    pair = _font_pair()
    if pair is None:
        print("warning: no Helvetica-metric font found; skipping PNG for "
              f"{os.path.basename(cv.stem)}", file=sys.stderr)
        return 0
    regular, bold = pair

    k = PNG_SCALE * _OVERSAMPLE
    img = Image.new("RGB", (int(round(cv.w * k)), int(round(cv.h * k))), PAPER)
    d = ImageDraw.Draw(img)
    fonts = {}

    def font(size, is_bold):
        key = (round(size * k, 2), is_bold)
        if key not in fonts:
            fonts[key] = ImageFont.truetype(bold if is_bold else regular, key[0])
        return fonts[key]

    for op in cv.ops:
        kind = op[0]
        if kind == "rect":
            _, x, y, w, h, fill, opacity, rx = op
            box = [x * k, y * k, (x + w) * k, (y + h) * k]
            colour = _blend(fill, PAPER, opacity) if opacity != 1.0 else fill
            if rx:
                d.rounded_rectangle(box, radius=rx * k, fill=colour)
            else:
                d.rectangle(box, fill=colour)
        elif kind == "line":
            _, x1, y1, x2, y2, stroke, width, dash = op
            for a, b in _dashed(x1, y1, x2, y2, dash):
                d.line([a[0] * k, a[1] * k, b[0] * k, b[1] * k], fill=stroke,
                       width=max(1, round(width * k)))
        elif kind == "poly":
            _, pts, stroke, width, dash = op
            w = max(1, round(width * k))
            for (ax, ay), (bx, by) in zip(pts, pts[1:]):
                for a, b in _dashed(ax, ay, bx, by, dash):
                    d.line([a[0] * k, a[1] * k, b[0] * k, b[1] * k], fill=stroke, width=w)
            # Pillow butt-joins segments, so a polyline turning a corner shows a
            # notch; a dot at each vertex is the cheapest round join there is.
            for px, py in pts[1:-1]:
                r = w / 2.0
                d.ellipse([px * k - r, py * k - r, px * k + r, py * k + r], fill=stroke)
        elif kind == "marker":
            _, x, y, fill, shape, r, halo = op
            if halo:
                _pil_shape(d, x * k, y * k, (r + 1.6) * k, shape, halo)
            _pil_shape(d, x * k, y * k, r * k, shape, fill)
        elif kind == "text":
            _, x, y, s, size, fill, is_bold = op
            d.text((x * k, y * k), s, font=font(size, is_bold), fill=fill, anchor="ls")
        elif kind == "vtext":
            _, x, y, s, size, fill, is_bold = op
            f = font(size, is_bold)
            tw = int(round(text_width(s, size, is_bold) * k)) + 4
            th = int(round(size * k * 1.5))
            base = int(round(size * k * 1.15))
            strip = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
            ImageDraw.Draw(strip).text((0, base), s, font=f, fill=fill, anchor="ls")
            strip = strip.rotate(90, expand=True)
            img.paste(strip, (int(round(x * k)) - base, int(round(y * k)) - (tw - 1)), strip)

    img = img.resize((int(round(cv.w * PNG_SCALE)), int(round(cv.h * PNG_SCALE))),
                     Image.LANCZOS)
    img.save(cv.stem + ".png", optimize=True)
    return 1


def _dashed(x1, y1, x2, y2, dash):
    """Split a segment into the on-runs of an SVG stroke-dasharray."""
    if not dash:
        return [((x1, y1), (x2, y2))]
    pattern = [float(v) for v in dash.split()]
    length = math.hypot(x2 - x1, y2 - y1)
    if length <= 0:
        return []
    ux, uy = (x2 - x1) / length, (y2 - y1) / length
    out, pos, i = [], 0.0, 0
    while pos < length:
        run = pattern[i % len(pattern)]
        if i % 2 == 0:
            end = min(pos + run, length)
            out.append(((x1 + ux * pos, y1 + uy * pos), (x1 + ux * end, y1 + uy * end)))
        pos += run
        i += 1
    return out


def _pil_shape(d, x, y, r, shape, fill):
    if shape == "circle":
        d.ellipse([x - r, y - r, x + r, y + r], fill=fill)
    else:
        d.polygon(_shape_points(x, y, r, shape), fill=fill)


# ---------------------------------------------------------------- driver

FIGURES = ("plot_matrix_relative", "plot_generate_n_sweep", "plot_thread_scaling",
           "plot_scaling_placement", "plot_float_conversion")


def generate(outdir, png=True):
    runs = load_runs()
    write_csvs(runs, outdir)
    canvases = [cv for cv in (globals()[name](runs, outdir) for name in FIGURES) if cv]
    tool, done = rasterize(canvases) if png else (None, 0)
    return runs, canvases, tool, done


def tree(root):
    """Only the files this script owns, and only the reproducible ones.

    Hand-archived JSON shares raw/, so it must not be reported stale just
    because nothing here generates it. PNG is excluded for a different reason:
    it is a rasterisation of the SVG in the same directory, and librsvg,
    Inkscape and ImageMagick do not agree byte-for-byte with each other or
    across their own versions, so checking it would fail on whichever machine
    did not happen to publish last. The SVG and the PDF are both written by
    this file and are byte-reproducible, and no figure can change without one
    of them changing -- so policing those two polices the PNG's content.
    """
    found = {}
    for sub in (RAW_OUT, PLOT_OUT):
        base = os.path.join(root, sub)
        if not os.path.isdir(base):
            continue
        for name in sorted(os.listdir(base)):
            if not name.endswith((".csv", ".svg", ".pdf")):
                continue
            with open(os.path.join(base, name), "rb") as fh:
                found[os.path.join(sub, name)] = fh.read()
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="fail if the committed output is stale")
    ap.add_argument("--no-png", action="store_true",
                    help="skip rasterisation (SVG and PDF only)")
    args = ap.parse_args()

    if not args.check:
        _runs, canvases, tool, done = generate(REPO, png=not args.no_png)
        print(f"wrote {RAW_OUT}/ and {len(canvases)} figures as .svg + .pdf "
              f"in {PLOT_OUT}/")
        if args.no_png:
            pass
        elif tool:
            print(f"rasterised {done}/{len(canvases)} to .png at {PNG_SCALE}x via {tool}")
        else:
            print("no rasteriser found, so the .png files were not refreshed.\n"
                  "  Install librsvg2-bin (rsvg-convert), inkscape, or python3-pil.\n"
                  "  The .svg and .pdf figures are current either way.", file=sys.stderr)
        return 0

    tmp = tempfile.mkdtemp(prefix="vphilox-publish-")
    try:
        generate(tmp, png=False)
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
