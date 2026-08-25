# Interoperating with cuRAND

vphilox produces the same stream as NVIDIA cuRAND's `Philox4_32_10`, and this
page records both the verification and the seeding mapping that makes it usable.

**Verified: 16/16 cases identical over 4096 words each, under all three x86
backends.** Log: [`results/curand/curand-parity-tigerlake.txt`](../results/curand/curand-parity-tigerlake.txt).

## Why this is a separate question from #54's parity test

`tests/test_cross_platform_parity.cpp` proves vphilox agrees with *itself*
across architectures — four kernels, two architectures, one FNV-1a digest. And
`tests/test_reference_vectors.cpp` pins the core function to the published
Salmon et al. vectors, so agreement on the *algorithm* was never really in
doubt.

Neither says anything about the layer where implementations actually diverge.
cuRAND seeds with `(seed, subsequence, offset)`; vphilox seeds with
`(key, counter)`. Philox implementations agree on the round function and
disagree on exactly this mapping, and for a library whose reason for existing is
portable, reproducible state, "the same stream on both sides" is a claim worth
establishing rather than assuming.

## The mapping

`curand_init(seed, subsequence, offset, &state)` zeroes the counter, adds
`subsequence` to its **high** 64 bits, adds `offset / 4` to its **low** 64 bits,
and keeps `offset % 4` as a word index into the resulting block. vphilox's
`counter4` is little-endian by word, so:

| cuRAND | vphilox |
|---|---|
| `seed` (low 32) | `key.v[0]` |
| `seed` (high 32) | `key.v[1]` |
| `offset / 4` (low 32) | `counter.v[0]` |
| `offset / 4` (high 32) | `counter.v[1]` |
| `subsequence` (low 32) | `counter.v[2]` |
| `subsequence` (high 32) | `counter.v[3]` |
| `offset % 4` | word index within the block |

So to resume a cuRAND stream in vphilox:

```cpp
const std::uint64_t lo = offset / 4;
vphilox::key2     key{{u32(seed),      u32(seed >> 32)}};
vphilox::counter4 ctr{{u32(lo), u32(lo >> 32), u32(subsequence), u32(subsequence >> 32)}};

vphilox::engine eng(key, ctr);
// then discard `offset % 4` words to land exactly where cuRAND would be
```

Note the asymmetry worth remembering: cuRAND's `offset` counts **words**, while
a vphilox counter counts **blocks of four**. An `offset` that is not a multiple
of four lands mid-block, which is what the word index is for.

The practical consequence is that cuRAND's subsequences are `2^66` values apart
— it reserves the counter's entire high 64 bits for stream selection and leaves
the low 64 for position. A caller partitioning work across GPU threads the
cuRAND way and across CPU threads the vphilox way gets identical, non-
overlapping streams as long as both use the same convention.

## How it is checked

`tools/vphilox_curand_parity` runs sixteen cases and makes two *independent*
assertions per case:

1. **Core.** cuRAND reports its own `(counter, key)` after seeding. Those exact
   values are fed to vphilox and the two word streams must match over 4096
   words. This tests the round function and counter increment order with no
   assumption about seeding at all.
2. **Mapping.** The `(counter, key)` cuRAND derived must equal what the table
   above predicts from `(seed, subsequence, offset)`.

Splitting them matters: check 1 would pass even if the mapping were completely
wrong, because it takes cuRAND's word for the state. Check 2 is the interop
claim, and it is the half that could plausibly have failed.

Cases cover block-aligned and mid-block offsets, offsets straddling `2^32`
(forcing a carry between the counter's low words), the maximum subsequence, and
edge keys including all-zero and all-ones.

### The checks are proven sensitive

Both assertions were mutation-tested rather than assumed to work:

| mutation | core | mapping |
|---|---|---|
| *(none)* | 16/16 | 16/16 |
| predicted counter halves swapped | 16/16 | **7/16** |
| one bit flipped in the counter given to vphilox | **0/16** | 16/16 |

Each mutation is caught by exactly one check and passes the other, which is what
demonstrates they are two independent tests rather than one written twice. The
seven survivors of the first mutation are the cases whose counter halves are
both zero, where swapping them is a no-op.

## No GPU, and why that is not a weaker result

The tool needs the CUDA **headers** and nothing else — no `nvcc`, no CUDA
runtime, no GPU. cuRAND guards its function decoration with
`#if !defined(QUALIFIERS)`, so defining `QUALIFIERS` before the include compiles
NVIDIA's own reference implementation as ordinary host C++.

The arithmetic is identical either way by construction. The only
target-dependent line in the entire generator is `mulhilo32`, which cuRAND
writes as `NV_IF_ELSE_TARGET(NV_IS_HOST, <64-bit multiply>, __umulhi)`. Both
branches compute the high 32 bits of a 32×32 product; the difference is
instruction selection, not algorithm.

So what is verified is NVIDIA's reference implementation, not NVIDIA's device
code generation. The gap between those is a hypothetical `__umulhi` bug rather
than anything about Philox — but it *is* a gap, and it is stated here rather
than glossed. Anyone wanting to close it can run the same cases through a real
device build.

This also makes the check far more portable than a GPU test would have been: it
runs on any machine with the CUDA toolkit unpacked, and needs no NVIDIA hardware
at all.

## Running it

The target only exists when the cuRAND headers are found, exactly as
`vphilox_testu01` only exists when TestU01 is installed. Configure logs which
case you are in.

```bash
cmake --preset default && cmake --build --preset default --target vphilox_curand_parity
build/tools/vphilox_curand_parity                       # resolved backend
VPHILOX_BACKEND=scalar build/tools/vphilox_curand_parity  # or pin one
```

Exit status is 0 for `IDENTICAL` and 1 for `MISMATCH`, so it can be wired into a
script. It is not a CTest case because it needs a third-party header that most
machines do not have.

## Recorded run

Tiger Lake i5-11300H, GCC 15.2.0, cuRAND headers 10401 (CUDA 13.1), all sixteen
cases identical under `scalar`, `avx2` and `avx512`.

An RTX 3060 is present in that machine and was deliberately not used: its CUDA
13.1 toolkit cannot compile even an empty `.cu` against the host's glibc 2.43
(`rsqrt` is declared with incompatible exception specifications by
`crt/math_functions.h` and `bits/mathcalls.h`). That blocked the device route
entirely — and prompted the host-compiled approach, which turned out to be the
better artifact.
