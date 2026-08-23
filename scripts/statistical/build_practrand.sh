#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
# Copyright (c) 2026 Parth Sinha
#
# Fetch and build PractRand pre0.95 into $PRACTRAND_HOME (default
# ~/.local/src/PractRand). Idempotent: re-running with RNG_test already built
# is a no-op.
#
# PractRand is not packaged anywhere reliable and its build is three commands
# that are easy to get subtly wrong (the object files must be archived before
# RNG_test links, or the tool silently misses half the test batteries), so it
# is scripted rather than written down.

set -euo pipefail

PRACTRAND_HOME="${PRACTRAND_HOME:-$HOME/.local/src/PractRand}"
PRACTRAND_URL="${PRACTRAND_URL:-https://sourceforge.net/projects/pracrand/files/PractRand-pre0.95.zip/download}"

if [[ -x "$PRACTRAND_HOME/RNG_test" ]]; then
    echo "PractRand already built at $PRACTRAND_HOME/RNG_test"
    exit 0
fi

mkdir -p "$PRACTRAND_HOME"
cd "$PRACTRAND_HOME"

echo "==> fetching PractRand"
curl -sSL --max-time 300 -o practrand.zip "$PRACTRAND_URL"
unzip -oq practrand.zip

# -march=native is safe here: PractRand is the *consumer*, never the thing
# under measurement, and it is always built on the machine that runs it.
echo "==> compiling library objects"
g++ -std=c++17 -O3 -march=native -Iinclude -pthread \
    -c src/*.cpp src/RNGs/*.cpp src/RNGs/other/*.cpp

echo "==> archiving"
ar rcs libPractRand.a ./*.o
rm -f ./*.o

echo "==> linking RNG_test"
g++ -std=c++17 -O3 -march=native -Iinclude -pthread \
    -o RNG_test tools/RNG_test.cpp libPractRand.a

"$PRACTRAND_HOME/RNG_test" --help 2>&1 | head -2 || true
echo "==> built $PRACTRAND_HOME/RNG_test"
