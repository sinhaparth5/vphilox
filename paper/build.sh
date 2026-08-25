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
set -euo pipefail

PAPER_DATE="${PAPER_DATE:-2026-08-25}"

cd "$(dirname "${BASH_SOURCE[0]}")"

SOURCE_DATE_EPOCH="$(date -u -d "$PAPER_DATE" +%s)"
export SOURCE_DATE_EPOCH
export FORCE_SOURCE_DATE=1

# Two passes: the second resolves \cref and the hyperref outlines.
for _ in 1 2; do
    pdflatex -interaction=nonstopmode -halt-on-error vphilox.tex >/dev/null
done

# The log is discarded above, so re-check the things that matter by hand.
for pattern in 'Overfull' 'undefined'; do
    n="$(grep -c "$pattern" vphilox.log || true)"
    [[ "$n" == "0" ]] || echo "WARNING: $n '$pattern' in vphilox.log" >&2
done

echo "vphilox.pdf  $(sha256sum vphilox.pdf | cut -c1-16)  dated $PAPER_DATE"
