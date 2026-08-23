#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
# Copyright (c) 2026 Parth Sinha
#
# Fetch and build TestU01 into $TESTU01_HOME (default ~/.local).
# Idempotent: a present libtestu01 short-circuits.
#
# Source is umontreal-simul/TestU01-2009 on GitHub -- L'Ecuyer's own group,
# i.e. the authors. The canonical simul.iro.umontreal.ca/testu01/TestU01.zip
# is currently broken: it 302s to an https URL whose path is doubled
# (/~simul/~simul/...) and returns a 404 HTML page with a 200-shaped filename,
# which a naive `curl -o TestU01.zip` will cheerfully save as the archive.
# Hence the content check below.
#
# TestU01 is fetched, never vendored: it ships under its own licence (free for
# academic/non-commercial use, not MIT/Apache), so it stays an external
# dependency the user installs rather than code in this tree.
#
# -fcommon is required: TestU01 predates GCC 10's default of -fno-common and
# has tentative definitions that otherwise collide at link time.

set -euo pipefail

TESTU01_HOME="${TESTU01_HOME:-$HOME/.local}"
TESTU01_SRC="${TESTU01_SRC:-$HOME/.local/src/TestU01}"
TESTU01_URL="${TESTU01_URL:-https://github.com/umontreal-simul/TestU01-2009/archive/refs/heads/master.tar.gz}"

if [[ -f "$TESTU01_HOME/lib/libtestu01.a" || -f "$TESTU01_HOME/lib/libtestu01.so" ]]; then
    echo "TestU01 already installed under $TESTU01_HOME"
    exit 0
fi

mkdir -p "$TESTU01_SRC"
cd "$TESTU01_SRC"

echo "==> fetching TestU01"
curl -sSL --max-time 300 -o tu01.tar.gz "$TESTU01_URL"

# Refuse to proceed on an error page masquerading as the tarball.
if ! file -b tu01.tar.gz | grep -qi gzip; then
    echo "downloaded file is not a gzip archive -- upstream may have moved:" >&2
    head -c 300 tu01.tar.gz >&2
    exit 1
fi

tar xzf tu01.tar.gz
cd "$(find . -maxdepth 1 -type d -name 'TestU01-*' | head -1)"

# The GitHub tarball does not preserve the executable bit on the autotools
# helper scripts, so `make install` dies with "./install-sh: Permission denied"
# after a perfectly good build.
chmod +x configure install-sh missing depcomp compile config.guess config.sub \
    ltmain.sh bootstrap 2>/dev/null || true

echo "==> configuring"
./configure --prefix="$TESTU01_HOME" \
    CFLAGS="-O2 -fcommon -std=gnu17 -Wno-implicit-function-declaration -Wno-return-type" > /dev/null

echo "==> building"
make -j"$(nproc)" > /dev/null
make install > /dev/null

echo "==> installed into $TESTU01_HOME"
ls "$TESTU01_HOME/lib/" | grep -i testu01 || true
