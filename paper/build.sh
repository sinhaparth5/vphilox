#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
# Copyright (c) 2026 Parth Sinha
#
# Build vphilox.pdf reproducibly.
#
# vphilox.pdf is tracked, so that anyone can read the paper without a TeX
# install. That only works if a rebuild with unchanged sources produces an
# unchanged file, otherwise every build churns 300 KB of binary diff and the
# history stops meaning anything. Two things would break it: pdftex stamping
# the wall clock into /CreationDate, and \today on the title page. Pinning
# SOURCE_DATE_EPOCH fixes both, because FORCE_SOURCE_DATE makes \today read
# from it as well.
#
# Bump PAPER_DATE when the draft is revised. That is the one intentional way
# the title-page date changes.
#
#   ./build.sh            build, and warn about overfull boxes and undefined refs
#   ./build.sh --strict   the same, but exit non-zero on either
#
# CI runs --strict. The warnings are the paper's quality bar rather than
# cosmetics: an undefined reference means a \cref lost its target, and an
# overfull box means something ran past the column, which in a two-column
# format is usually a table that was written against a full-width measure.
set -euo pipefail

PAPER_DATE="${PAPER_DATE:-2026-08-25}"
strict=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --strict) strict=1; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

cd "$(dirname "${BASH_SOURCE[0]}")"

SOURCE_DATE_EPOCH="$(date -u -d "$PAPER_DATE" +%s)"
export SOURCE_DATE_EPOCH
export FORCE_SOURCE_DATE=1

# Two passes: the second resolves \cref and the hyperref outlines.
for _ in 1 2; do
    pdflatex -interaction=nonstopmode -halt-on-error vphilox.tex >/dev/null
done

# The log is discarded above, so re-check the things that matter by hand.
problems=0
for pattern in 'Overfull' 'undefined'; do
    n="$(grep -c "$pattern" vphilox.log || true)"
    if [[ "$n" != "0" ]]; then
        problems=$((problems + n))
        echo "WARNING: $n '$pattern' in vphilox.log" >&2
        # Which ones, so the message is actionable without opening the log.
        grep -n "$pattern" vphilox.log | head -20 >&2
    fi
done

pages="$(grep -o 'Output written on vphilox.pdf ([0-9]* page' vphilox.log | grep -o '[0-9]*' | head -1)"
echo "vphilox.pdf  $(sha256sum vphilox.pdf | cut -c1-16)  ${pages:-?} pages  dated $PAPER_DATE"

if (( strict == 1 && problems > 0 )); then
    echo "ERROR: --strict, and the log has $problems overfull boxes or undefined references." >&2
    exit 1
fi
