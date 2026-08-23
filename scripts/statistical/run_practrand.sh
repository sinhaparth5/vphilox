#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
# Copyright (c) 2026 Parth Sinha
#
# Run one PractRand battery against vphilox_stream and archive the log with
# enough provenance to reproduce it: git SHA, resolved backend, CPU, PractRand
# version, and the verbatim command.
#
# Usage:
#   scripts/statistical/run_practrand.sh --length 1TB --backend avx2
#   scripts/statistical/run_practrand.sh --length 256GB --format float32
#
# Note the pipeline runs under `set -o pipefail`. That only became meaningful
# once vphilox_stream stopped dying of SIGPIPE when PractRand closes the pipe
# at -tlmax -- before that, every completed run reported 141.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PRACTRAND_HOME="${PRACTRAND_HOME:-$HOME/.local/src/PractRand}"
STREAM="${STREAM:-$REPO/build/bench/tools/vphilox_stream}"
OUTDIR="${OUTDIR:-$REPO/results/practrand}"

length="1GB"
backend=""
format="raw32"
seed=0
label=""
threads="-multithreaded"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --length)         length="$2"; shift 2 ;;
        --backend)        backend="$2"; shift 2 ;;
        --format)         format="$2"; shift 2 ;;
        --seed)           seed="$2"; shift 2 ;;
        --label)          label="$2"; shift 2 ;;
        --single-threaded) threads=""; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

[[ -x "$STREAM" ]] || { echo "no vphilox_stream at $STREAM; cmake --build --preset bench" >&2; exit 1; }
[[ -x "$PRACTRAND_HOME/RNG_test" ]] || { echo "no RNG_test; run build_practrand.sh" >&2; exit 1; }

stream_args=(--seed "$seed" --format "$format")
[[ -n "$backend" ]] && stream_args+=(--backend "$backend")

# Ask the tool what it actually resolved rather than trusting the flag --
# an unknown --backend value falls back silently, and a mislabelled archived
# log is worse than no log.
resolved="$("$STREAM" "${stream_args[@]}" --info 2>&1 | sed -n 's/.*backend=\([a-z0-9]*\).*/\1/p')"

[[ -n "$label" ]] || label="${format}-${length}-${resolved}"
mkdir -p "$OUTDIR"
log="$OUTDIR/${label}.log"

practrand_mode="stdin32"
[[ "$format" == "float32" ]] && practrand_mode="stdin32"   # raw IEEE-754 bits, read as words

{
    echo "# vphilox PractRand run"
    echo "date_utc:      $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "host:          $(hostname)"
    echo "kernel:        $(uname -srm)"
    echo "cpu:           $(LC_ALL=C lscpu | sed -n 's/^Model name:[[:space:]]*//p' | head -1)"
    echo "cpu_flags_simd: $(LC_ALL=C lscpu | sed -n 's/^Flags:.*/&/p' | tr ' ' '\n' | grep -E '^(avx2|avx512f|avx512dq|asimd|neon)$' | paste -sd, -)"
    echo "git_sha:       $(git -C "$REPO" rev-parse HEAD)"
    echo "git_dirty:     $(git -C "$REPO" status --porcelain | wc -l) file(s)"
    echo "vphilox:       $("$STREAM" --info 2>&1 | head -1)"
    echo "requested_backend: ${backend:-<auto>}"
    echo "resolved_backend:  $resolved"
    echo "format:        $format"
    echo "seed:          $seed"
    echo "length:        $length"
    echo "command:       $STREAM ${stream_args[*]} | RNG_test $practrand_mode -tlmax $length -tf 1 $threads"
    echo "#"
} > "$log"

start=$(date +%s)
set -o pipefail
"$STREAM" "${stream_args[@]}" 2>>"$log" \
    | "$PRACTRAND_HOME/RNG_test" "$practrand_mode" -tlmax "$length" -tf 1 $threads >>"$log" 2>&1
rc=$?
end=$(date +%s)

{
    echo "#"
    echo "exit_status:   $rc"
    echo "wall_seconds:  $((end - start))"
} >> "$log"

echo "wrote $log (exit $rc, $((end - start))s)"
grep -E "no anomalies|FAIL|suspicious|unusual" "$log" | tail -5 || true
exit $rc
