#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
# Copyright (c) 2026 Parth Sinha
#
# Copy the published figure PDFs into paper/figures/ so the .tex compiles from
# a flat directory, which is what arXiv wants. Everything here is generated:
# regenerate the source with scripts/benchmarks/publish_results.py, never edit
# a file this script copies.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$repo/docs/benchmarks/plots"
dst="$repo/paper/figures"

python3 "$repo/scripts/benchmarks/publish_results.py" --check

mkdir -p "$dst"
for name in matrix-relative generate-n-sweep thread-scaling scaling-placement \
            float-conversion-widths; do
    cp "$src/$name.pdf" "$dst/$name.pdf"
    echo "staged $name.pdf"
done
