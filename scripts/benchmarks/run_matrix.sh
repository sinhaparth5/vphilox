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
#   scripts/benchmarks/run_matrix.sh --tag icelake --cpu 2 --repetitions 9
#
# The governor is forced to `performance` for the duration and restored on
# exit, including on Ctrl-C. Results below this project's sub-1% cycles/byte
# CV bar are reported as a warning rather than silently published: a noisy run
# is a smoke test, not a measurement.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUTDIR="${OUTDIR:-$REPO/results}"

tag=""
cpu=3
repetitions=7
min_time="1s"
filter='.*'

while [[ $# -gt 0 ]]; do
    case "$1" in
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

BENCH="$REPO/build/bench/benchmarks/bench_engines"
JSON="$OUTDIR/$tag-matrix.json"
ENVFILE="$OUTDIR/$tag-environment.txt"

# ---------------------------------------------------------------- build

if [[ ! -x "$BENCH" ]]; then
    echo "==> building the bench preset"
    cmake --preset bench >/dev/null
    cmake --build --preset bench --target bench_engines >/dev/null
fi
[[ -x "$BENCH" ]] || { echo "no bench_engines at $BENCH" >&2; exit 1; }

mkdir -p "$OUTDIR"

# ------------------------------------------------------------- governor

GOV_PATHS=(/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor)
saved_gov=""

restore_governor() {
    if [[ -n "$saved_gov" ]]; then
        echo "==> restoring governor to $saved_gov"
        for g in "${GOV_PATHS[@]}"; do
            [[ -w "$g" ]] && echo "$saved_gov" > "$g" 2>/dev/null || \
                echo "$saved_gov" | sudo tee "$g" >/dev/null 2>&1 || true
        done
    fi
}

if [[ -r "${GOV_PATHS[0]}" ]]; then
    saved_gov="$(cat "${GOV_PATHS[0]}")"
    trap restore_governor EXIT INT TERM
    echo "==> governor was $saved_gov; setting performance"
    for g in "${GOV_PATHS[@]}"; do
        echo performance > "$g" 2>/dev/null || \
            echo performance | sudo tee "$g" >/dev/null 2>&1 || true
    done
    now_gov="$(cat "${GOV_PATHS[0]}")"
    [[ "$now_gov" == "performance" ]] || \
        echo "WARNING: governor is '$now_gov', not performance -- run with sudo for a pinned clock" >&2
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
    echo "affinity-cpu: $cpu"
    echo "governor: $(cat "${GOV_PATHS[0]}" 2>/dev/null || echo unavailable)"
    echo "perf_event_paranoid: $(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo n/a)"
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

echo "==> running the matrix on CPU $cpu ($repetitions reps, $min_time each)"
taskset --cpu-list "$cpu" "$BENCH" \
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
print(f"{'benchmark':<34}{'cycles/byte':>13}{'GiB/s':>10}{'cv':>9}")
for name, b in med.items():
    cpb = b.get("cycles_per_byte")
    gib = b.get("bytes_per_second", 0) / (1 << 30)
    c   = cv.get(name, {}).get("cycles_per_byte")
    cvs = f"{c*100:.2f}%" if c is not None else "n/a"
    cpbs = f"{cpb:.4f}" if cpb is not None else "n/a"
    print(f"{name:<34}{cpbs:>13}{gib:>10.3f}{cvs:>9}")
    if c is not None and c > 0.01:
        noisy.append((name, c))

if noisy:
    print()
    print("WARNING: above this project's sub-1% cycles/byte CV bar:")
    for name, c in sorted(noisy, key=lambda x: -x[1]):
        print(f"  {name}: {c*100:.2f}%")
    print("A run this noisy is a smoke test, not a result worth writing up.")
PY

echo
echo "==> wrote $JSON"
echo "==> wrote $ENVFILE"
