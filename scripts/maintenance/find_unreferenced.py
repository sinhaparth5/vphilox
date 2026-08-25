#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
# Copyright (c) 2026 Parth Sinha
"""Report tracked files that nothing else in the repository refers to.

This exists because "delete what we do not need" is a question about the
reference graph, not about file dates. A benchmark write-up from three days ago
may be load-bearing because a table in the paper cites it, and a header added
this morning may be dead. Only the graph knows.

The output is a report, never a deletion. Every candidate still needs a human to
decide, because being unreferenced is evidence and not proof: a file can be
reached through a glob, a CMake variable, or a shell loop that no textual scan
resolves. Those cases are listed under KNOWN BLIND SPOTS below.

Usage:
    python3 scripts/maintenance/find_unreferenced.py
    python3 scripts/maintenance/find_unreferenced.py --all   # include referenced files
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

# Files that are reached without ever being named in tracked text.
# Deleting any of these breaks something, so they are never reported as
# candidates. Each entry says who reaches it.
KNOWN_BLIND_SPOTS = {
    "LICENSE-MIT": "referenced by SPDX headers, not by path",
    "LICENSE-APACHE": "referenced by SPDX headers, not by path",
    "VERSION": "read by cmake/VphiloxVersion.cmake at configure time",
    ".gitignore": "consumed by git",
    "paper/.gitignore": "consumed by git; ignores figures/ and the TeX build products",
    ".clang-format": "consumed by clang-format",
    "CITATION.cff": "consumed by GitHub and Zenodo",
    "CMakePresets.json": "consumed by cmake --preset",
    ".github/workflows/ci.yml": "consumed by GitHub Actions",
    "CLAUDE.md": "read by tooling, not linked",
}

# Whole subtrees reached by directory rather than by filename.
BLIND_SPOT_PREFIXES = {
    "results/": "read as a directory glob by scripts/benchmarks/publish_results.py",
    "docs/benchmarks/raw/": "written as a directory by publish_results.py; --check reads it back",
    "docs/benchmarks/plots/": "written as a directory by publish_results.py; the paper's \\graphicspath points at it",
    "include/": "compiled; reached by #include and by install(DIRECTORY)",
    "tests/": "listed in tests/CMakeLists.txt, some by glob",
    "benchmarks/third_party/": "vendored; provenance hashes in its own README",
}

TEXT_SUFFIXES = {
    ".md", ".tex", ".txt", ".py", ".sh", ".yml", ".yaml", ".cmake", ".json",
    ".hpp", ".cpp", ".in", ".cff", ".toml", ".cfg",
}


def tracked_files() -> list[str]:
    out = subprocess.run(
        ["git", "ls-files"], capture_output=True, text=True, check=True
    ).stdout
    return [line for line in out.splitlines() if line]


def readable_text(paths: list[str]) -> dict[str, str]:
    """Every tracked file we can scan for references, by path."""
    blobs: dict[str, str] = {}
    for path in paths:
        p = Path(path)
        if p.suffix.lower() not in TEXT_SUFFIXES and p.name not in {"CMakeLists.txt"}:
            continue
        try:
            blobs[path] = p.read_text(errors="ignore")
        except OSError:
            continue
    return blobs


def blind_spot(path: str) -> str | None:
    if path in KNOWN_BLIND_SPOTS:
        return KNOWN_BLIND_SPOTS[path]
    for prefix, reason in BLIND_SPOT_PREFIXES.items():
        if path.startswith(prefix):
            return reason
    return None


def find_references(paths: list[str], blobs: dict[str, str]) -> dict[str, list[str]]:
    """Map each file to the files that mention it.

    A file counts as referenced if its full path, or its bare name, appears in
    the text of some other tracked file. The bare name is included because
    markdown links are relative and CMake lists sources without directories;
    it costs some precision on common names, which is the right trade when the
    consequence of a false candidate is a deletion.
    """
    referrers: dict[str, list[str]] = defaultdict(list)
    names: dict[str, list[str]] = defaultdict(list)
    for path in paths:
        names[path].append(re.escape(path))
        bare = Path(path).name
        # A bare name is only a useful signal when it is distinctive. "README.md"
        # matches everywhere and means nothing.
        if bare not in {"README.md", "CMakeLists.txt", "__init__.py"}:
            names[path].append(re.escape(bare))
        stem = Path(path).stem
        if len(stem) > 8 and stem != bare:
            names[path].append(re.escape(stem))

    patterns = {p: re.compile("|".join(alts)) for p, alts in names.items()}
    for source, text in blobs.items():
        for target, pattern in patterns.items():
            if source == target:
                continue
            if pattern.search(text):
                referrers[target].append(source)
    return referrers


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--all", action="store_true",
                        help="list every file with its referrer count, not just candidates")
    args = parser.parse_args()

    paths = tracked_files()
    blobs = readable_text(paths)
    referrers = find_references(paths, blobs)

    candidates: list[str] = []
    protected: list[tuple[str, str]] = []
    for path in sorted(paths):
        reason = blind_spot(path)
        if reason:
            if not referrers[path]:
                protected.append((path, reason))
            continue
        if not referrers[path]:
            candidates.append(path)

    if args.all:
        print("REFERENCE COUNTS")
        for path in sorted(paths):
            print(f"{len(referrers[path]):4d}  {path}")
        print()

    print(f"Scanned {len(paths)} tracked files, {len(blobs)} of them searchable.")
    print()

    if protected:
        print(f"REACHED WITHOUT BEING NAMED ({len(protected)}) -- not candidates:")
        for path, reason in protected[:12]:
            print(f"  {path}\n      {reason}")
        if len(protected) > 12:
            print(f"  ... and {len(protected) - 12} more under the same rules")
        print()

    if not candidates:
        print("No unreferenced files. Nothing to review.")
        return 0

    print(f"UNREFERENCED ({len(candidates)}) -- review each, do not bulk delete:")
    for path in candidates:
        size = Path(path).stat().st_size if Path(path).exists() else 0
        print(f"  {size:8d}  {path}")
    print()
    print("Unreferenced means no tracked text names it. That is evidence, not a")
    print("verdict: check whether a glob, a build variable, or an external link")
    print("reaches it before removing anything.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
