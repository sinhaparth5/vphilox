#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
# Copyright (c) 2026 Parth Sinha
#
# Run the Phase 4 throughput matrix and archive it with enough provenance to
# reproduce: git SHA, CPU, governor, thermal state, and the verbatim command.
# Writes results/<tag>-matrix.json and results/<tag>-environment.txt, matching
# the layout the earlier baseline runs already use.
#
# Usage:
#   scripts/benchmarks/run_matrix.sh --tag pi-arm --cpu 3
#   scripts/benchmarks/run_matrix.sh --tag pi-arm --bench scaling
#   scripts/benchmarks/run_matrix.sh --tag icelake --cpu 2 --repetitions 9
#
# --bench engines (default) runs the throughput matrix pinned to one CPU.
# --bench scaling runs the thread-scaling curve, which needs every core, so it
# is not pinned to a single CPU.
#
# The governor is forced to `performance` for the duration and restored on
# exit, including on Ctrl-C. Results below this project's sub-1% cycles/byte
# CV bar are reported as a warning rather than silently published: a noisy run
# is a smoke test, not a measurement.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUTDIR="${OUTDIR:-$REPO/results}"
INVOCATION="$0 $*"

tag=""
bench="engines"
cpu=3
repetitions=7
min_time="1s"
filter='.*'

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bench)       bench="$2"; shift 2 ;;
        --tag)         tag="$2"; shift 2 ;;
        --cpu)         cpu="$2"; shift 2 ;;
        --repetitions) repetitions="$2"; shift 2 ;;
        --min-time)    min_time="$2"; shift 2 ;;
        --filter)      filter="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

if [[ -z "$tag" ]]; then
    tag="$(uname -m)"
fi

# Which benchmark, and the rows that prove the binary is current. bench_engines
# gained xoshiro/PCG64 with the matrix; bench_scaling gained its bulk arm with
# the pool -- in both cases a build predating that change still runs and still
# produces a plausible-looking table, which is the failure this guards.
case "$bench" in
    engines)
        target="bench_engines"
        suffix="matrix"
        required_rows=(BM_xoshiro256pp BM_pcg64)
        ;;
    scaling)
        target="bench_scaling"
        suffix="scaling"
        required_rows=(BM_thread_scaling_bulk)
        ;;
    *) echo "unknown --bench: $bench (expected 'engines' or 'scaling')" >&2; exit 2 ;;
esac

BENCH="$REPO/build/bench/benchmarks/$target"
JSON="$OUTDIR/$tag-$suffix.json"
# Suffixed, not just "$tag-environment.txt": two suites under one tag would
# otherwise overwrite each other's provenance, leaving a write-up citing an
# environment file that describes a different run.
ENVFILE="$OUTDIR/$tag-$suffix-environment.txt"

# ---------------------------------------------------------------- build

# Always build, never "build only if missing". This script stamps a git SHA
# into the environment file next to the numbers, so running a binary that is
# older than the checkout turns that provenance into a false claim -- which is
# exactly what a leftover build/ directory from an earlier baseline causes. An
# incremental no-op build costs about a second; a mislabelled result costs a
# retracted write-up.
echo "==> building the bench preset"
cmake --preset bench >/dev/null
cmake --build --preset bench --target "$target" >/dev/null
[[ -x "$BENCH" ]] || { echo "no $target at $BENCH" >&2; exit 1; }

# Belt and braces: confirm the rows we intend to measure are present, so a
# checkout older than the matrix fails here rather than silently reporting a
# subset of the table.
listing="$("$BENCH" --benchmark_list_tests=true)"
for required in "${required_rows[@]}"; do
    if ! grep -q "^$required" <<<"$listing"; then
        echo "ERROR: $BENCH does not contain $required." >&2
        echo "  This build predates the current benchmark. From $REPO:" >&2
        echo "    git fetch origin && git checkout master && git pull" >&2
        exit 1
    fi
done

mkdir -p "$OUTDIR"

# ------------------------------------------------------------- governor

GOV_PATHS=(/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor)
saved_gov=""

# sysfs governor nodes are root-owned, so the plain write is expected to fail
# for a normal user. Test writability first rather than letting the redirection
# fail into a fallback: bash applies `> "$g"` before `2>/dev/null`, so that
# spelling prints a Permission denied line per CPU even when the sudo path
# then succeeds, which reads like a broken run when it is not.
write_governor() {
    local val="$1" g
    for g in "${GOV_PATHS[@]}"; do
        if [[ -w "$g" ]]; then
            echo "$val" > "$g" || true
        else
            echo "$val" | sudo tee "$g" >/dev/null 2>&1 || true
        fi
    done
}

restore_governor() {
    if [[ -n "$saved_gov" ]]; then
        echo "==> restoring governor to $saved_gov"
        write_governor "$saved_gov"
    fi
}

if [[ -r "${GOV_PATHS[0]}" ]]; then
    saved_gov="$(cat "${GOV_PATHS[0]}")"
    trap restore_governor EXIT INT TERM
    echo "==> governor was $saved_gov; setting performance"
    write_governor performance
    now_gov="$(cat "${GOV_PATHS[0]}")"
    if [[ "$now_gov" == "performance" ]]; then
        echo "==> governor is performance"
    else
        echo "WARNING: governor is '$now_gov', not performance -- the clock is not pinned." >&2
        echo "  Re-run after 'sudo -v', or the cycles/byte CV will not reach this" >&2
        echo "  project's sub-1% bar." >&2
    fi
else
    echo "WARNING: no cpufreq governor visible; clock is not pinned" >&2
fi

# ---------------------------------------------------------- environment

echo "==> capturing environment to $ENVFILE"
{
    date -Is
    uname -a
    echo "git-sha: $(git -C "$REPO" rev-parse HEAD)"
    echo "git-dirty: $(git -C "$REPO" status --porcelain | wc -l) modified file(s)"
    if [[ "$bench" == "scaling" ]]; then
        echo "affinity: unpinned (scaling needs every core)"
    else
        echo "affinity-cpu: $cpu"
    fi
    echo "nproc: $(nproc 2>/dev/null || echo unknown)"
    echo "governor: $(cat "${GOV_PATHS[0]}" 2>/dev/null || echo unavailable)"
    echo "perf_event_paranoid: $(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo n/a)"
    # The write-ups quote a compiler version, and a benchmark number without one
    # is not reproducible: the same source on the same part gives different
    # results across GCC releases.
    echo "compiler: $("${CXX:-c++}" --version 2>/dev/null | head -1 || echo unknown)"
    echo "cmake: $(cmake --version 2>/dev/null | head -1 || echo unknown)"
    echo "command: $INVOCATION"
    # Raspberry Pi thermal/throttle state; absent elsewhere.
    if command -v vcgencmd >/dev/null 2>&1; then
        echo "vcgencmd-temp: $(vcgencmd measure_temp)"
        echo "vcgencmd-throttled: $(vcgencmd get_throttled)"
    fi
    echo
    lscpu
} > "$ENVFILE"

if command -v vcgencmd >/dev/null 2>&1; then
    thr="$(vcgencmd get_throttled)"
    [[ "$thr" == "throttled=0x0" ]] || \
        echo "WARNING: $thr -- the Pi has throttled; let it cool before trusting this run" >&2
fi

# ---------------------------------------------------------------- run

if [[ "$bench" == "scaling" ]]; then
    echo "==> running $target unpinned across $(nproc) CPUs ($repetitions reps, $min_time each)"
else
    echo "==> running $target on CPU $cpu ($repetitions reps, $min_time each)"
fi
# The matrix is pinned to one CPU to keep a single-threaded measurement quiet.
# The scaling curve must NOT be -- pinning every worker to one core would
# measure a 32-way context switch storm and report it as a scaling failure.
if [[ "$bench" == "scaling" ]]; then
    run_prefix=()
else
    run_prefix=(taskset --cpu-list "$cpu")
fi

"${run_prefix[@]}" "$BENCH" \
    --benchmark_filter="$filter" \
    --benchmark_repetitions="$repetitions" \
    --benchmark_min_time="$min_time" \
    --benchmark_enable_random_interleaving=true \
    --benchmark_report_aggregates_only=true \
    --benchmark_out="$JSON" \
    --benchmark_out_format=json

# ------------------------------------------------------------- validate

# The cycles counter degrades silently: on ARM the benchmark omits
# cycles_per_byte entirely when perf_event_open is denied, leaving a run that
# looks fine but has lost the primary metric. Fail loudly instead.
if ! grep -q '"cycles_per_byte"' "$JSON"; then
    echo >&2
    echo "ERROR: no cycles_per_byte in $JSON -- the cycle counter did not run." >&2
    echo "  On ARM this is perf_event_open being denied. Try:" >&2
    echo "    sudo sysctl -w kernel.perf_event_paranoid=1" >&2
    echo "  GB/s in the JSON is still valid, but cycles/byte is the metric this" >&2
    echo "  project reports first, so the run is not publishable as it stands." >&2
    exit 1
fi

# The binary stamps the resolved kernel into the JSON context, which is the one
# fact the environment capture above cannot know: it runs before the benchmark,
# and dispatch resolves inside it. Mirror it into the environment file so the
# provenance is complete in one place.
backend="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["context"].get("vphilox_backend","unknown"))' "$JSON")"
echo "resolved-backend: $backend" >> "$ENVFILE"
echo
echo "==> resolved backend: $backend"

echo
echo "==> medians and cycles/byte CV"
python3 - "$JSON" <<'PY'
import json, sys

rows = json.load(open(sys.argv[1]))["benchmarks"]
med = {}
cv  = {}
for b in rows:
    name = b["name"]
    if name.endswith("_median"):
        med[name[:-7]] = b
    elif name.endswith("_cv"):
        cv[name[:-3]] = b

noisy = []
print(f"{'benchmark':<42}{'cycles/byte':>13}{'GiB/s':>10}{'cv':>9}")
for name, b in med.items():
    cpb = b.get("cycles_per_byte")
    gib = b.get("bytes_per_second", 0) / (1 << 30)
    c   = cv.get(name, {}).get("cycles_per_byte")
    cvs = f"{c*100:.2f}%" if c is not None else "n/a"
    cpbs = f"{cpb:.4f}" if cpb is not None else "n/a"
    print(f"{name.replace('/real_time', ''):<42}{cpbs:>13}{gib:>10.3f}{cvs:>9}")
    if c is not None and c > 0.01:
        noisy.append((name, c))

if noisy:
    print()
    print("WARNING: above this project's sub-1% cycles/byte CV bar:")
    for name, c in sorted(noisy, key=lambda x: -x[1]):
        print(f"  {name.replace('/real_time', '')}: {c*100:.2f}%")
    print("A run this noisy is a smoke test, not a result worth writing up.")
PY

echo
echo "==> wrote $JSON"
echo "==> wrote $ENVFILE"
