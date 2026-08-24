# Thread scaling, Raspberry Pi 5 — 2026-08-24

Phase 4 multi-core scaling (issues #50, #51) on the one machine available here
with a pinned clock, real cores, and no SMT. The Coffee Lake laptop has
hyperthreading and an unpinned turbo ceiling; the EPYC host is a 2-vCPU VM.
Neither can answer "does this scale" without confounds, so both were used only
to build the harness.

As with the throughput matrix, **NEON is a stub (#28), so this is the scalar
kernel.** What scales here is the counter-partitioning scheme, not a SIMD
kernel — and that is the part the design claim is actually about.

## Environment

| | |
|---|---|
| CPU | Raspberry Pi 5, 4-core Cortex-A76 at 2.4 GHz, no SMT |
| OS | 64-bit Raspberry Pi OS, Linux 6.12.96+rpt-rpi-2712 |
| Compiler | GCC 12.2.0 (Debian 12.2.0-14+deb12u1) |
| Governor | `performance` |
| Affinity | unpinned — the curve needs every core |
| Cycle source | `perf_event_open`, per worker thread |
| Resolved backend | `scalar` |

Seven randomly interleaved repetitions, one-second minimum, via
`scripts/benchmarks/run_matrix.sh --tag pi-arm --bench scaling`. Medians below.
Every one of the 24 configurations reported a cycles/byte CV of 0.08% or
better.

## Results

Aggregate cycles/byte — every worker's own cycles, over all the bytes produced.
Flat means linear scaling.

**`generate_n` (bulk), 256 KiB per worker**

| threads | 1 | 2 | 4 | 8 | 16 | 32 |
|---|---:|---:|---:|---:|---:|---:|
| cycles/byte | 3.1953 | 3.1948 | 3.1950 | 3.1954 | 3.1952 | 3.1954 |
| GiB/s | 0.682 | 1.363 | 2.697 | 2.625 | 2.668 | 2.585 |
| speedup | 1.00 | 2.00 | **3.95** | 3.85 | 3.91 | 3.79 |

**buffered `operator()`, 256 KiB per worker**

| threads | 1 | 2 | 4 | 8 | 16 | 32 |
|---|---:|---:|---:|---:|---:|---:|
| cycles/byte | 4.1161 | 4.1206 | 4.1171 | 4.1145 | 4.1158 | 4.1228 |
| GiB/s | 0.532 | 1.050 | 2.105 | 2.077 | 2.027 | 1.961 |
| speedup | 1.00 | 1.97 | **3.96** | 3.90 | 3.81 | 3.69 |

The 4 MiB-per-worker arms behave identically; the full set is in the JSON.

## What this says

**Scaling is linear to the core count, and the cost per byte does not move.**
Across all twelve bulk configurations — every thread count from 1 to 32, both
working sets — aggregate cycles/byte spans 3.1934 to 3.1954. That is a spread
of **0.063%**. The buffered arm spans 0.238%.

Four threads on four cores gives 3.95x throughput at 98.8% efficiency. Beyond
that, throughput plateaus at ~2.6 GiB/s and cycles/byte still does not move:
oversubscription costs wall-clock, as it must, but it does not make the work
itself more expensive. That is what "no shared state" is supposed to look like,
and it is the first direct evidence for it rather than an argument from the
design.

**There is no memory-bandwidth knee here, and that is a statement about the
scalar kernel rather than about the memory system.** At 3.19 cycles/byte and
2.4 GHz, four cores produce roughly 3 GB/s — far below what LPDDR4X on this
board will supply. The 4 MiB working set was included specifically to provoke a
knee and could not. The same benchmark on Coffee Lake with the AVX2 kernel
degrades 6.66x by 32 threads at 4 MiB, because there the generator is fast
enough to saturate something. Expect this axis to start discriminating on ARM
only once #28 lands.

**The bulk path is 28.9% cheaper per byte and that margin is constant.** It
does not widen or narrow with thread count. This is the same refill-buffer
overhead the throughput matrix measured at 25.2%, reproduced by an independent
benchmark on the same machine.

## Correction to the Coffee Lake reading

An earlier note recorded from the laptop that the buffered arm "degrades from
two threads, so the bulk path is the one that scales". That is wrong, and this
run is why.

On the Pi both arms scale within 0.24% out to 32 threads. The laptop's apparent
degradation was measurement, not behaviour: RDTSC counts reference rather than
core cycles, so the moving turbo ceiling under added threads lands directly in
the cycles/byte column, and 8 threads on a 4C/8T part is hyperthreading rather
than scaling. The correct statement is that the bulk path is uniformly cheaper,
not that it is the only one that scales.

That is the argument for having run this on a machine with a pinned clock and
no SMT, rather than treating the laptop numbers as indicative.

## Artifacts

- [`results/pi-arm-scaling.json`](../../results/pi-arm-scaling.json)
- [`results/pi-arm-scaling-environment.txt`](../../results/pi-arm-scaling-environment.txt)
