# Float conversion widths, Sapphire Rapids — 2026-08-25

Issue #34's last open sub-item was the AVX-512 and NEON variants of the bulk
`u32 -> float` conversion, blocked until those kernels landed. The code went in
at `ec69448`; this is the measurement that says whether it was worth having.

**Answer: yes, and the size that matters most is the one the library actually
uses.** At `float_tile_words` (256 words, the chunk `generate_n` converts) the
sixteen-lane variant runs the conversion at **1.89x the baseline** and 1.68x
AVX2. At a 32 MiB working set it is 3.19x baseline and 1.76x AVX2.

**This is a cloud host.** Per this project's measurement rules a cloud VM is
for correctness, not throughput — the exception being a dedicated-vCPU instance
whose rows come in under the CV bar, which must be labelled as cloud. Two of
the four sizes qualify and two do not; both are below, with the failing rows
marked.

## Environment

| | |
|---|---|
| Host | GCP `c3-standard-4`, `us-central1-a` — **cloud**, dedicated vCPU |
| CPU | Intel Xeon Platinum 8481C @ 2.70 GHz (Sapphire Rapids) |
| Topology | 2 physical cores, 2 threads each — cpu0/cpu2 and cpu1/cpu3 pair up |
| Caches | L1d 48 KiB ×2, L2 2 MiB ×2, L3 105 MiB shared |
| OS | Debian 12, Linux 6.1.0-52-cloud-amd64 |
| Compiler | GCC 12.2.0, `cmake --preset bench` (Release, `-O3`) |
| Affinity | `taskset -c 1` |
| Cycle source | RDTSC (reference cycles) |
| Steal time | 0 for the run's duration, per `/proc/stat` |
| Commit | `8b4556d` plus the tile row |

Nine randomly interleaved repetitions, one-second minimum sample time. Medians
reported. Raw JSON in `results/sapphire-rapids-float-convert.json`, full
provenance in `results/sapphire-rapids-float-convert-environment.txt`.

## Results

Cycles per byte of output; lower is better. Each row touches two buffers of
`words * 4` bytes, source and destination, so the footprint is eight bytes per
element.

| Words | Footprint | baseline | AVX2 | AVX-512 | AVX-512 vs baseline |
|---:|---:|---:|---:|---:|---:|
| **256** | 2 KiB | 0.0731 | 0.0651 ⚠ | **0.0387** | **1.89x** |
| 2048 | 16 KiB | 0.0710 ⚠ | 0.0469 ⚠ | 0.0368 ⚠ | 1.93x ⚠ |
| 65536 | 512 KiB | 0.0774 ⚠ | 0.0835 | 0.0781 | 0.99x |
| **4194304** | 32 MiB | 1.1202 | 0.6200 ⚠ | **0.3514** | **3.19x** |

⚠ marks a row whose cycles/byte CV exceeded this project's 1% bar:

| Row | CV |
|---|---:|
| `avx2/256` | 2.89% |
| `baseline/2048`, `avx2/2048`, `avx512/2048` | 2.24%, 15.38%, 9.10% |
| `baseline/65536` | 18.71% |
| `avx2/4194304` | 2.23% |

The two rows the conclusion rests on are clean: `avx512/256` at 0.05% against
`baseline/256` at 0.03%, and `avx512/4194304` at 0.29% against
`baseline/4194304` at 0.03%.

## Three regimes, and the middle one is flat

**256 and 2048 words — issue-bound.** Everything is in L1 and the loop is
limited by how many elements an instruction can retire. Width converts almost
directly into throughput: 1.89x for sixteen lanes over the scalar-ish baseline,
1.68x over eight.

**65536 words — bandwidth-bound, and width stops mattering.** All three land
between 0.077 and 0.084 cycles/byte, about 30-32 GiB/s. The AVX-512 variant is
0.99x the baseline: no gain, no loss. A 512 KiB footprint is L2-resident, and
L2-to-L1 bandwidth does not care how wide the register consuming it is.

**4194304 words — width matters again, more than anywhere else.** 3.19x. This
one deserves care rather than a confident mechanism. If it were simple
bandwidth saturation the three would converge as they do at 65536, not spread
out. The plausible reading is memory-level parallelism: a zmm load claims a
full 64-byte line per instruction where a scalar load claims a sixteenth of
one, so the narrow loop runs out of issue slots before it runs out of
bandwidth. That is a hypothesis — confirming it needs miss and
outstanding-request counters, which were not collected.

Note this row is **not** DRAM on this host. 32 MiB sits inside Sapphire Rapids'
105 MiB L3. On Coffee Lake (8 MiB L3) or a Pi 5 (2 MiB) the same row would be
past every cache, so it is named for its size, not for a tier.

## What this changes for callers

`generate_n(float*, n)` converts in 256-word tiles, which is the top row.
Callers on an AVX-512 machine get the 1.89x without doing anything: dispatch
resolves the converter alongside the kernel.

It does not change the conversion's share of the whole bulk float path, which
is dominated by generation. The conversion at 0.0387 cycles/byte sits against
an AVX-512 kernel at roughly 0.25 — this makes a small term smaller. The reason
to have it is that it is free: the same loop, a different attribute, and one
runtime branch already being taken.

## A compiler finding worth keeping

`float_bulk.hpp` exists because a header-only library compiles with the
*consumer's* flags, so the ISA leaks in from the call site and the target
attribute pins it. The vectoriser leaks in the same way, and the attribute does
**not** pin that:

| Flags (GCC 12.2) | AVX2 clone | AVX-512 clone |
|---|---|---|
| `-O2` | scalar `%xmm` | scalar `%xmm` |
| `-O3` | `%ymm` | `%zmm` |
| `-O2 -ftree-loop-vectorize` | `%ymm` | `%zmm` |

GCC 15 vectorises both at `-O2`; GCC 12's `-O2` cost model rejects the loop. So
a GCC 12 consumer building at `-O2` gets scalar conversion no matter which
variant dispatch selects — correct output, no speedup. The measurements above
are all `-O3` via the bench preset.

## Three instrument bugs, and what they cost

Recorded because each one produced a plausible-looking table first.

**One size is not a measurement.** The first run used only 65536 words and
reported the three variants as indistinguishable — which is true *at that
size*, and would have been written up as "the width does not help". It happens
to be the one size where that is the answer.

**The baseline was being inlined and the others were not.** Only the baseline
lacks a target attribute, and GCC will not inline an attributed function into
an unattributed caller. Passing the variant as a template parameter therefore
inlined the baseline into a `-O3` benchmark TU while AVX2 and AVX-512 stayed
real calls. It showed up as the baseline moving 0.077 → 0.103 cycles/byte
between two runs at the same size. All three now arrive as an opaque runtime
pointer, which is also how `generate_n` calls them.

**The instrument was louder than the signal.** The small rows ran in ~200 ns,
the same order as the RDTSC pair timing them, giving 10-14% CVs that more
repetitions made *worse*. An intervening hypothesis — that `taskset -c 2` had
landed on cpu0's SMT sibling — was tested by re-pinning to cpu 1 and was wrong;
the CVs did not move. The fix was to convert repeatedly inside one timed region
until it covers at least 64 Ki words. `baseline/256` went from unusable to
0.03% CV.

That last fix is also what surfaced the top row at all. Every size originally
measured was larger than `float_tile_words`, so the benchmark had not been
measuring the granularity the library actually runs at.
