#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
# Copyright (c) 2026 Parth Sinha
#
# Run the placement study (#53) on a GCP instance and bring the results home.
#
# The study needs a host that can actually place threads. Bare metal is the
# safe answer and CLAUDE.md says so, but a cloud instance qualifies when the
# provider allocates each vCPU as a dedicated hardware thread and exposes the
# host's sibling topology -- n2-standard-32 reports two sockets and sixteen
# cores, and the gate in run_placement.sh is what decides whether that is true
# on the day rather than in the marketing.
#
#   scripts/benchmarks/gcp-placement-run.sh --tag gcp-cascadelake
#
# Everything here bills by the second, so the instance is deleted on every exit
# path including Ctrl-C. Check `gcloud compute instances list` afterwards
# anyway; a trap is not a guarantee.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

tag="gcp-cascadelake"
machine="n2-standard-32"
min_cpu="Intel Cascade Lake"
workers=8
keep=0

# One zone is not enough. Every launch attempt on record refused with
# ZONE_RESOURCE_POOL_EXHAUSTED in the first zone tried, which is a capacity
# refusal rather than a quota one and clears by moving zone.
ZONES=(europe-west2-a europe-west2-b europe-west2-c
       europe-west1-b europe-west1-c europe-west1-d
       us-central1-a us-central1-b us-central1-c)

while [[ $# -gt 0 ]]; do
    case "$1" in
        --tag)     tag="$2";     shift 2 ;;
        --machine) machine="$2"; shift 2 ;;
        --workers) workers="$2"; shift 2 ;;
        --keep)    keep=1;       shift ;;   # debugging only; you pay for it
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

instance="vphilox-${tag}-$$"
zone=""

cleanup() {
    local rc=$?
    if [[ -n "$zone" && "$keep" == 0 ]]; then
        echo
        echo "==> deleting $instance (nothing should be left billing)"
        gcloud compute instances delete "$instance" --zone "$zone" --quiet 2>/dev/null || true
    elif [[ -n "$zone" ]]; then
        echo "==> --keep: $instance is STILL RUNNING in $zone and still billing."
    fi
    exit "$rc"
}
trap cleanup EXIT INT TERM

# ------------------------------------------------------------ the working tree
#
# Not a git clone of master. The whole reason for this run is usually a change
# that has not been pushed yet, and cloning from the remote silently measures
# something else -- that mistake cost a full billed run once. .git goes along
# because run_matrix.sh records the commit and the dirty count, and provenance
# without a SHA is not provenance.

tarball="$(mktemp /tmp/vphilox-tree-XXXXXX.tar.gz)"
trap 'rm -f "$tarball"' RETURN 2>/dev/null || true
echo "==> packing working tree (including .git, for the provenance stamp)"
tar czf "$tarball" -C "$REPO" \
    --exclude=build --exclude='*.tar.gz' \
    .git include tests benchmarks tools scripts cmake CMakeLists.txt CMakePresets.json VERSION 2>/dev/null
echo "    $(du -h "$tarball" | cut -f1)"

# ------------------------------------------------------------------ the launch

for z in "${ZONES[@]}"; do
    echo "==> creating $instance ($machine, $z)"
    if err="$(gcloud compute instances create "$instance" \
            --zone "$z" \
            --machine-type "$machine" \
            --min-cpu-platform "$min_cpu" \
            --image-family ubuntu-2404-lts-amd64 \
            --image-project ubuntu-os-cloud \
            --boot-disk-size 32GB \
            --boot-disk-type pd-balanced \
            --no-restart-on-failure \
            --maintenance-policy TERMINATE 2>&1 >/dev/null)"; then
        zone="$z"
        break
    fi
    # A capacity refusal is worth retrying elsewhere. Anything else -- quota,
    # permissions, a bad flag -- will refuse identically in every zone, so
    # stopping immediately is what keeps a typo from walking the whole list.
    if grep -q 'ZONE_RESOURCE_POOL_EXHAUSTED\|does not have enough resources' <<<"$err"; then
        echo "    no capacity in $z, trying the next zone"
        continue
    fi
    echo "$err" >&2
    echo "ERROR: instance creation failed for a reason that changing zone will not fix." >&2
    exit 1
done
[[ -n "$zone" ]] || { echo "ERROR: no zone had capacity for $machine." >&2; exit 1; }

echo "==> waiting for ssh"
for _ in $(seq 1 30); do
    gcloud compute ssh "$instance" --zone "$zone" --command true >/dev/null 2>&1 && break
    sleep 10
done

run() { gcloud compute ssh "$instance" --zone "$zone" --command "$1"; }

echo "==> uploading working tree"
gcloud compute scp "$tarball" "$instance:~/tree.tar.gz" --zone "$zone" >/dev/null

# ------------------------------------------------------------------- preflight
#
# Cheaper to find out now than after twenty minutes of toolchain install.

echo "==> preflight"
run 'set -e
echo "  virt:      $(systemd-detect-virt || true)"
echo "  sockets:   $(lscpu | grep -i "^socket" || true)"
echo "  cores:     $(lscpu | grep -i "core(s) per socket" || true)"
echo "  threads:   $(lscpu | grep -i "thread(s) per core" || true)"
echo "  model:     $(lscpu | grep -i "model name" || true)"
echo "  pmu:       $([ -e /sys/bus/event_source/devices/cpu ] && echo present || echo absent)"
echo "  governor:  $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unavailable)"
echo "  loadavg:   $(cut -d" " -f1-3 /proc/loadavg)"'

echo "==> installing toolchain"
run 'sudo DEBIAN_FRONTEND=noninteractive apt-get update -qq &&
     sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
       build-essential cmake ninja-build git python3 linux-tools-common >/dev/null &&
     echo "  toolchain ok"'

echo "==> unpacking and running the study (detached; this takes a while)"
# setsid alone does not detach when the ssh shell is already a process group
# leader -- it execs in place and blocks, and the exit code recorded is its own.
# nohup with an explicit background and a written return code is what makes the
# poll loop below able to tell finished from failed.
run "rm -rf ~/vphilox && mkdir -p ~/vphilox && tar xzf ~/tree.tar.gz -C ~/vphilox && rm -f ~/run.rc &&
     cd ~/vphilox &&
     nohup bash -c 'scripts/benchmarks/run_placement.sh --tag $tag --workers $workers > ~/run.log 2>&1;
                    echo \$? > ~/run.rc' >/dev/null 2>&1 &
     sleep 2; echo '  started'"

# ----------------------------------------------------------------- poll
#
# Three outcomes, not one. Waiting only for the results would spin forever on a
# failure while the instance kept billing.

echo "==> polling"
rc=""
for _ in $(seq 1 90); do
    rc="$(run 'cat ~/run.rc 2>/dev/null || true' 2>/dev/null | tr -d "[:space:]")"
    [[ -n "$rc" ]] && break
    run 'tail -1 ~/run.log 2>/dev/null || echo "  (no output yet)"' 2>/dev/null | sed 's/^/    /'
    sleep 60
done

echo "==> fetching log"
gcloud compute scp "$instance:~/run.log" "$REPO/gcp-run.log" --zone "$zone" >/dev/null 2>&1 || true
tail -40 "$REPO/gcp-run.log" 2>/dev/null | sed 's/^/    /' || true

if [[ "$rc" != "0" ]]; then
    echo
    echo "ERROR: the run exited ${rc:-<never finished>}. Results not archived." >&2
    echo "       Full log: $REPO/gcp-run.log" >&2
    exit 1
fi

echo "==> fetching results"
gcloud compute scp --recurse "$instance:~/vphilox/results/*" "$REPO/results/" --zone "$zone" >/dev/null
echo "    archived under $REPO/results/"
