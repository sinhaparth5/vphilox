# Contributing to vphilox

## Getting a build

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

Before opening a PR, also run the strict presets:

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug   # -Werror
cmake --preset asan  && cmake --build --preset asan  && ctest --preset asan    # ASan + UBSan
```

## The one rule

**The generated bit stream never changes.** For any (key, counter), vphilox
must produce exactly what Random123's Philox4x32-10 produces, forever. Users
depend on this to reproduce results across machines, architectures, and years.

A change that alters the output is not a bug fix and not a new version — it is
a different generator, and it does not belong here. `tests/test_reference_vectors.cpp`
enforces this against the published vectors; if it fails, the change is wrong.

## Adding a SIMD kernel

Scalar, AVX2, AVX-512 and NEON are all implemented. A fifth kernel (SVE, say)
follows the same five steps.

1. Implement `generate()` in the kernel header, replacing the scalar
   fallthrough.
2. Set `implemented = true` on the kernel struct. Dispatch picks it up
   automatically, and `tests/test_kernel_parity.cpp` stops skipping.
3. Make parity green. Every counter, every key, every block count in the test
   matrix — including the block counts that do not fill a vector register.
   Tails are where kernels break.
4. Confirm `preferred_blocks` matches your actual interleaving width.
5. Benchmark it. A kernel that is not faster than scalar has no reason to
   exist, and one that is slower than the kernel below it in the dispatch
   preference order should not be preferred.

Kernels honor a shared contract: `generate(base, key, out, blocks)` writes
`blocks * 4` words where block *i* is `philox4x32(base + i, key)`. Output must
not depend on how the caller chunks the request — that independence is what
makes parity testing meaningful and what lets the engine refill in any size.

Never add ISA flags to the whole build. Attach them to the kernel translation
unit (`VPHILOX_FLAGS_AVX2` / `VPHILOX_FLAGS_AVX512` are staged in the root
`CMakeLists.txt`) or use `[[gnu::target]]`. One binary has to start and run
correctly on a CPU that lacks the instructions, falling back at runtime.

## Style

`.clang-format` is authoritative; CI checks it.

**Use clang-format 21.1.8.** The version matters as much as the config file:
clang-format is not stable across major versions, and 18 and 21 disagree on a
short block following a `#pragma omp` in `benchmarks/bench_scaling.cpp`. There
is no source text that satisfies both, so a check run against whatever version
the distribution happens to ship is a coin flip rather than an enforcement of
`.clang-format`. CI installs the pin explicitly; `CLANG_FORMAT_VERSION` at the
top of `.github/workflows/ci.yml` is the single place it is written down.

If your distribution ships something else, get the pinned build from PyPI
rather than fighting the packaged one:

```bash
python3 -m venv ~/.local/clang-format
~/.local/clang-format/bin/pip install clang-format==21.1.8
~/.local/clang-format/bin/clang-format --version   # expect 21.1.8
```

Check and fix, the same commands CI runs:

```bash
find include tests benchmarks tools \
  \( -name '*.hpp' -o -name '*.cpp' \) -not -path '*/third_party/*/*' \
  | xargs clang-format --dry-run --Werror   # check
find include tests benchmarks tools \
  \( -name '*.hpp' -o -name '*.cpp' \) -not -path '*/third_party/*/*' \
  | xargs clang-format -i                   # fix
```

Vendored code one level down inside `third_party/` is excluded on purpose:
reformatting it would destroy the hashes in `benchmarks/third_party/README.md`,
which are the only thing making its provenance checkable. Files directly in
`third_party/` are ours and stay checked.

Bumping the pin is fine, but do it deliberately, in a commit that also applies
whatever the new version reformats. Do not bump it as a side effect of an
unrelated change.

Beyond formatting: comments explain *why*, not *what*. The intrinsic sequences
in the kernels are dense by necessity — a comment naming the lane layout or the
reason for a particular shuffle is worth more than a comment restating the
intrinsic's name.

## Tests

One file per concern, named for what it protects. New behavior needs a test;
new kernels need to pass the existing parity matrix rather than get their own
bespoke checks.

## Benchmarks

Report cycles per byte alongside GB/s. GB/s is not comparable across machines,
and the claims this project makes are cycle-level claims.

Run them through the harness rather than by hand:

```bash
scripts/benchmarks/run_matrix.sh --tag <machine> --cpu <isolated-cpu>
```

It rebuilds first, pins the governor and restores it on exit, writes a
provenance file next to the JSON, and exits non-zero if any row moves more than
1% between repetitions. A stale `build/` will otherwise run a months-old binary
and print a plausible table. A 10% swing between runs on a laptop is thermal,
not algorithmic, and the CV check is there to catch it.

`docs/benchmarks/README.md` explains why cross-machine figures plot ratios
measured inside each machine, and why a cloud VM is fine for correctness but not
for throughput.

## Before deleting anything

```bash
python3 scripts/maintenance/find_unreferenced.py
```

It reports tracked files that no other tracked file names, so "is this still
needed" is answered from the reference graph rather than from a file's age. Most
of `docs/benchmarks/` looks like history and is not: `paper/README.md` maps every
table and figure in the paper to a write-up or a CSV under it, and the claim that
each number traces to an archived run is only true while those files exist.

The report is a list to review, never a list to delete. A file can be reached by
a glob, a CMake variable or a shell loop that no textual scan resolves, so the
script keeps a list of those blind spots and prints them separately. Add to that
list rather than deleting something it flagged wrongly.

## Versioning

CalVer, `YYYY.0M.MICRO`, driven by the `VERSION` file. See
[VERSIONING.md](VERSIONING.md). Do not hardcode a version anywhere else.

## Licensing

vphilox is dual-licensed **MIT OR Apache-2.0**; a user picks either one. Every
source file starts with:

```
// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha
```

New files need that header — CI does not check it yet, but review will.

By submitting a contribution you agree it is dual licensed on the same terms,
as defined in Section 5 of the Apache-2.0 license. There is no CLA to sign.

Pasted code from another project needs its license checked first. MIT, BSD, and
Apache-2.0 sources can be relocated here with attribution; anything copyleft
cannot. The Random123 reference vectors in `tests/test_reference_vectors.cpp`
are published constants from the Salmon et al. paper, not copied code.
