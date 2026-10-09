#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
# Copyright (c) 2026 Parth Sinha
#
# Build "$job.pdf" reproducibly.
#
# "$job.pdf" is tracked, so that anyone can read the paper without a TeX
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
#   ./build.sh              build, and warn about overfull boxes and undefined refs
#   ./build.sh --strict     the same, but exit non-zero on either
#   ./build.sh --anonymous  build vphilox-anon.pdf instead: the same text with
#                           the class's anonymous option on, which is what TOMS
#                           wants as the main document for a new submission.
#                           "$job.pdf" is the tracked, named one and is what
#                           the repository and arXiv get.
#
# CI runs --strict. The warnings are the paper's quality bar rather than
# cosmetics: an undefined reference means a \cref lost its target, and an
# overfull box means something ran past the measure, which in acmsmall is
# usually a table that was written against a wider one.
#
# acmart.cls and ACM-Reference-Format.bst are vendored under acmart/ and found
# through TEXINPUTS below. The local TeX tree has an older acmart, and the
# tracked PDF has to be reproducible from the sources in this repository
# rather than from whatever the machine happens to have installed.
set -euo pipefail

PAPER_DATE="${PAPER_DATE:-2026-10-09}"
strict=0
anon=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --strict) strict=1; shift ;;
        --anonymous) anon=1; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

cd "$(dirname "${BASH_SOURCE[0]}")"

SOURCE_DATE_EPOCH="$(date -u -d "$PAPER_DATE" +%s)"
export SOURCE_DATE_EPOCH
export FORCE_SOURCE_DATE=1

export TEXINPUTS="./acmart:"
export BSTINPUTS="./acmart:"

# The anonymous build is the same source under a different jobname, so the two
# never overwrite each other's .aux or .pdf. \PassOptionsToClass has to run
# before \documentclass, which is why the file is \input rather than named.
if (( anon == 1 )); then
    job=vphilox-anon
    src='\PassOptionsToClass{anonymous}{acmart}\input{vphilox.tex}'
else
    job=vphilox
    src='\input{vphilox.tex}'
fi

# pdflatex, bibtex, pdflatex, pdflatex: the first pass writes the .aux the
# bibliography is built from, and two more resolve \cref, the citation numbers
# and the hyperref outlines.
pdflatex -interaction=nonstopmode -halt-on-error -jobname="$job" "$src" >/dev/null
bibtex "$job" >/dev/null
for _ in 1 2; do
    pdflatex -interaction=nonstopmode -halt-on-error -jobname="$job" "$src" >/dev/null
done

# The log is discarded above, so re-check the things that matter by hand.
problems=0
for pattern in 'Overfull' 'undefined'; do
    n="$(grep -c "$pattern" "$job.log" || true)"
    if [[ "$n" != "0" ]]; then
        problems=$((problems + n))
        echo "WARNING: $n '$pattern' in $job.log" >&2
        # Which ones, so the message is actionable without opening the log.
        grep -n "$pattern" "$job.log" | head -20 >&2
    fi
done

pages="$(grep -o "Output written on $job.pdf ([0-9]* page" "$job.log" | grep -o '[0-9]*' | head -1)"
echo "$job.pdf  $(sha256sum "$job.pdf" | cut -c1-16)  ${pages:-?} pages  dated $PAPER_DATE"

if (( strict == 1 && problems > 0 )); then
    echo "ERROR: --strict, and the log has $problems overfull boxes or undefined references." >&2
    exit 1
fi
