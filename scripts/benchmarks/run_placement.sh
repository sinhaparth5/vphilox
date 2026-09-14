#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
# Copyright (c) 2026 Parth Sinha
#
# The instruction-supply half of the scaling study (#53): does the hyperthread
# knee found in #51 come with an instruction-cache cost, or is it purely
# execution-port contention?
#
# The measurement is a contrast, not a curve. The same number of workers is run
# twice with only their placement changed -- one per physical core, then packed
# onto half as many cores' sibling threads -- and the question is whether
# icache_mpki moves. Flat across that boundary excludes instruction supply as
# the mechanism; climbing does not.
#
#   scripts/benchmarks/run_placement.sh --tag <machine>
#   scripts/benchmarks/run_placement.sh --tag <machine> --workers 4
#
# THIS SCRIPT REFUSES TO RUN ON A VM, and that is its main reason for existing.
# A guest can publish a perfectly correct sibling map and accept every taskset
# mask while the hypervisor schedules its vCPUs onto host cores as it pleases,
# so the mask fixes virtual CPU IDs and nothing physical. The failure is silent:
# you get two placements, two sets of numbers, and no indication that they were
# the same placement. It was caught the first time only because co-location came
# out *faster* than separate cores, which is impossible for a port-bound kernel.
# So the gate below measures that penalty first and aborts if it is absent.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BENCH="$REPO/build/bench/benchmarks/bench_scaling"

tag=""
workers=4
words=65536
# #51 measured 35% per worker for sibling co-location. Requiring 15% leaves room
# for a part that shares less aggressively while still being far outside the
# range a VM's non-placement produces, which is zero or negative.
min_penalty_pct=15

while [[ $# -gt 0 ]]; do
    case "$1" in
        --tag)     tag="$2"; shift 2 ;;
        --workers) workers="$2"; shift 2 ;;
        --words)   words="$2"; shift 2 ;;
        --min-penalty) min_penalty_pct="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

[[ -n "$tag" ]] || { echo "--tag is required" >&2; exit 2; }
(( workers % 2 == 0 )) || { echo "--workers must be even (it is halved for the packed arm)" >&2; exit 2; }

# ------------------------------------------------------- topology

# Read the sibling map rather than assuming an enumeration. Linux numbers
# logical CPUs differently across vendors and firmware versions: siblings are
# adjacent on some parts and offset by the core count on others, so a hardcoded
# "0,1" mask silently measures the wrong thing on half the machines it runs on.
declare -a cores=()
seen=""
for d in /sys/devices/system/cpu/cpu[0-9]*/topology/thread_siblings_list; do
    [[ -r "$d" ]] || continue
    # Normalise "0-1" and "0,1" to "0 1".
    sibs="$(tr ',' ' ' < "$d" | sed 's/\([0-9]\+\)-\([0-9]\+\)/\1 \2/g')"
    key="${sibs// /,}"
    case " $seen " in *" $key "*) continue ;; esac
    seen="$seen $key"
    cores+=("$sibs")
done

(( ${#cores[@]} > 0 )) || { echo "could not read CPU topology from sysfs" >&2; exit 1; }

smt=0
for c in "${cores[@]}"; do
    set -- $c
    (( $# >= 2 )) && smt=1
    break
done

echo "==> ${#cores[@]} physical cores, SMT $([[ $smt == 1 ]] && echo present || echo absent)"
if (( smt == 0 )); then
    echo "ERROR: this machine has no SMT, so there is no co-location boundary to cross." >&2
    echo "  #53 is about two workers sharing one core; that cannot be staged here." >&2
    exit 1
fi

# One CPU from each of the first N cores.
mask_phys() {
    local n="$1" out=() i
    for (( i = 0; i < n; i++ )); do
        set -- ${cores[$i]}
        out+=("$1")
    done
    (IFS=,; echo "${out[*]}")
}

# Both siblings of the first N/2 cores.
mask_ht() {
    local n="$1" out=() i s
    for (( i = 0; i < n / 2; i++ )); do
        for s in ${cores[$i]}; do out+=("$s"); done
    done
    (IFS=,; echo "${out[*]}")
}

(( workers <= ${#cores[@]} )) || {
    echo "ERROR: --workers $workers needs $workers physical cores; this machine has ${#cores[@]}." >&2
    exit 1
}

# ---------------------------------------------------------- build

echo "==> building"
cmake --preset bench >/dev/null
cmake --build --preset bench --target bench_scaling >/dev/null
[[ -x "$BENCH" ]] || { echo "no bench_scaling at $BENCH" >&2; exit 1; }

# Median cycles/byte and its coefficient of variation for a placement, straight
# from the binary. The gate wants numbers fast, not an archived run. Both are
# printed on one line, "<median> <cv-percent>", because the gate needs the
# spread as well as the level -- see the three-sigma condition below.
measure_cpb() {
    local cpus="$1" n="$2" out median cv
    out="$(taskset -c "$cpus" "$BENCH" \
        --benchmark_filter="BM_thread_scaling_bulk/$n/$words/" \
        --benchmark_repetitions=5 \
        --benchmark_report_aggregates_only=true \
        --benchmark_min_time=0.3s 2>/dev/null)"
    median="$(grep '_median' <<<"$out" | grep -o 'cycles_per_byte=[0-9.]*' | cut -d= -f2)"
    # The _cv row already carries a percentage, printed with a trailing '%'
    # ("cycles_per_byte=5.61%"). Scaling it again turns a 1.4% spread into a
    # 143% one and the gate then refuses every host it is shown.
    cv="$(grep '_cv' <<<"$out" | grep -o 'cycles_per_byte=[0-9.]*' | cut -d= -f2)"
    echo "$median ${cv:-0}"
}

# ----------------------------------------------------------- gate

phys2="$(mask_phys 2)"
ht2="$(mask_ht 2)"

echo "==> placement gate: 2 workers on $phys2 (two cores) vs $ht2 (one core's siblings)"
read -r cpb_sep cv_sep <<<"$(measure_cpb "$phys2" 2)"
read -r cpb_col cv_col <<<"$(measure_cpb "$ht2" 2)"
[[ -n "$cpb_sep" && -n "$cpb_col" ]] || { echo "ERROR: no cycles_per_byte from the gate run." >&2; exit 1; }

# Single-quoted so bash leaves sys.argv alone: in double quotes $1/$2 are the
# script's own positional parameters, not Python's arguments.
penalty="$(python3 -c 'import sys; a, b = float(sys.argv[1]), float(sys.argv[2]); print(f"{(b / a - 1) * 100:.1f}")' "$cpb_sep" "$cpb_col")"
worst_cv="$(python3 -c 'import sys; print(f"{max(float(sys.argv[1]), float(sys.argv[2])):.2f}")' "$cv_sep" "$cv_col")"
echo "    separate cores: $cpb_sep cycles/byte (CV ${cv_sep}%)"
echo "    shared core:    $cpb_col cycles/byte (CV ${cv_col}%)"
echo "    co-location penalty: ${penalty}% (need >= ${min_penalty_pct}% and >= 3x the worse CV, ${worst_cv}%)"

# Two conditions, because either alone is passable by accident. The threshold
# alone is not enough: a WSL2 guest returned +23.7% on one invocation of this
# same comparison and -6.5% on the next, so a single noisy probe can clear 15%
# on scatter. Requiring the gap to exceed three times the worse arm's CV is
# roughly a three-sigma separation from zero, which scatter does not clear.
if ! python3 -c "import sys; sys.exit(0 if float(sys.argv[1]) >= float(sys.argv[2]) else 1)" \
        "$penalty" "$min_penalty_pct" \
   || ! python3 -c "import sys; sys.exit(0 if float(sys.argv[1]) >= 3 * float(sys.argv[2]) else 1)" \
        "$penalty" "$worst_cv"; then
    cat >&2 <<'MSG'

ERROR: sharing a core did not cost what it must, so placement is not controlled.

  Two workers on one core's sibling threads have to contend for that core's
  execution ports; #51 measured 35% per worker. A penalty near zero -- or
  negative, meaning co-location came out faster -- is not a machine with cheap
  hyperthreads. It means the mask never moved the workers, and the two arms of
  this study would have been the same placement twice.

  This is what a VM looks like from the inside. The guest publishes a correct
  sibling map and taskset accepts every mask, but the hypervisor schedules
  vCPUs onto host cores on its own. Run this on bare metal.

  If this IS bare metal, check that SMT is enabled in firmware and that nothing
  else is competing for the cores before lowering --min-penalty.

  A penalty that clears the threshold but not three times the worse arm's
  coefficient of variation is a different failure: the machine is too noisy for
  the comparison to mean anything yet, whatever the point estimate says.
MSG
    exit 1
fi

echo "==> placement is real; proceeding"

# ------------------------------------------------------ the study

# The counters are the point of #53, but the placement comparison itself does
# not need them: a cloud instance with dedicated vCPUs can control placement
# while exposing no PMU at all. Dropping the flag there costs the i-cache
# columns and keeps the cycles/byte comparison, where hard-failing would throw
# away a run the host can actually support.
perf_flag="--perf-counters"
if [[ ! -e /sys/bus/event_source/devices/cpu ]]; then
    perf_flag=""
    echo "==> no PMU exposed (/sys/bus/event_source/devices/cpu absent):"
    echo "    running without counters, so this arm carries cycles/byte only."
fi

# Archived through run_matrix.sh so each arm carries a governor, provenance and
# a CV check. The tags differ so the two placements never share a file.
for arm in phys ht; do
    if [[ "$arm" == phys ]]; then
        cpus="$(mask_phys "$workers")"
        note="one thread per physical core"
    else
        cpus="$(mask_ht "$workers")"
        note="packed onto $(( workers / 2 )) cores' sibling threads"
    fi
    echo
    echo "==> $workers workers, $note (cpus $cpus)"
    taskset -c "$cpus" "$REPO/scripts/benchmarks/run_matrix.sh" \
        --tag "$tag-icache-$arm" \
        --bench scaling \
        ${perf_flag:+$perf_flag} \
        --filter "BM_thread_scaling_bulk/$workers/$words/"
done

echo
if [[ -n "$perf_flag" ]]; then
    echo "==> both arms archived. The comparison is icache_mpki between"
    echo "    results/$tag-icache-phys-icache.json and results/$tag-icache-ht-icache.json"
    echo "    Flat across the two excludes instruction supply as the mechanism"
    echo "    behind the hyperthread knee; a rise does not."
else
    echo "==> both arms archived without counters, so this run carries the"
    echo "    placement comparison only:"
    echo "    results/$tag-icache-phys-scaling.json and results/$tag-icache-ht-scaling.json"
    echo "    The i-cache question needs a host with a PMU."
fi
