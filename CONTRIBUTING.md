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

This is Phase 2, and the scaffolding is already in place.

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

Benchmark on a quiet machine with frequency scaling pinned. A 10% swing between
runs on a laptop is thermal, not algorithmic.

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
