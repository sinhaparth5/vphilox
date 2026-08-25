# Raspberry Pi 5 ARM baseline — 2026-08-22

Native AArch64 rerun of `bench_engines` for Phase 1. This run supplies the ARM
measurement requested by issue #9 and the native, frequency-pinned baseline
requested by issue #11.

## Environment

| | |
|---|---|
| CPU | Raspberry Pi 5, 4-core Cortex-A76 at 2.4 GHz |
| OS | 64-bit Raspberry Pi OS, Linux 6.12.96 |
| Compiler | GCC 12.2.0, optimized benchmark preset |
| Affinity | CPU 3 |
| Governor | `performance` |
| Temperature before run | 55.4 °C |
| Throttling before run | none (`0x0`) |

The benchmark used seven randomly interleaved repetitions with a one-second
minimum sample time. The table reports medians. Cycles/byte is derived from the
Google Benchmark `CYCLES` counter captured in the raw run; the benchmark now
reports this ratio directly on Linux ARM.

## Results

| Benchmark | Throughput | Cycles/byte | Cycles/byte CV |
|---|---:|---:|---:|
| `BM_vphilox` | 0.564 GiB/s | 3.958 | 0.080% |
| `BM_vphilox_bulk` | 0.700 GiB/s | 3.193 | 0.0018% |
| `BM_mt19937` | 1.142 GiB/s | 1.956 | 0.944% |
| `BM_philox_scalar` | 0.696 GiB/s | 3.209 | 0.0025% |

Scalar Philox reached 0.61x the throughput of `std::mt19937`, so the reported
approximately 10x slowdown did not reproduce on ARM. Together with the Coffee
Lake result, this supports treating that slowdown as external context rather
than a project result. An AVX-512 measurement is still needed to complete
issue #9.

The source artifact is
[`results/pi-arm-baseline.json`](../../results/pi-arm-baseline.json). This run
predates `run_matrix.sh`, so no environment file was captured beside it; the
environment table above is the whole provenance record. Every archived run from
`pi-arm-matrix` onwards has a `-environment.txt` next to its JSON.
