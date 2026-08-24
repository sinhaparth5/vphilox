# AVX-512 kernel, Sapphire Rapids — 2026-08-24

First measurement of `detail/kernel_avx512.hpp` (issues #24, #25, #26, #27),
run on a rented GCP `c3-standard-4` because no AVX-512 part was otherwise
reachable. The instance was deleted afterwards.

## Environment

| | |
|---|---|
| CPU | Intel Xeon Platinum 8481C (Sapphire Rapids) @ 2.70 GHz, 4 vCPU |
| Instance | GCP `c3-standard-4`, `europe-west2-b` |
| OS | Ubuntu 24.04.4 LTS, Linux 6.8 |
| Compiler | GCC 13.3.0 |
| ISA | `avx512f`, `avx512dq`, `avx512bw`, `avx512vl` |
| Affinity | CPU 1 |
| Cycle source | RDTSC |

**This is a cloud VM, so steal time is not visible and these are not
bare-metal numbers.** They are reported because the CV came in at 0.02-0.24%
across every row, and because the effects below are far too large to be
tenancy noise. A pinned bare-metal part should still confirm #27 before the
downclocking question is called settled.

## Results

| Benchmark | Cycles/byte | Throughput | CV |
|---|---:|---:|---:|
| `BM_xoshiro256pp` | 0.3698 | 6.793 GiB/s | 0.24% |
| `BM_vphilox_bulk` | **0.4712** | **5.332 GiB/s** | 0.07% |
| `BM_vphilox_generate_n/65536` | 0.4715 | 5.328 GiB/s | 0.05% |
| `BM_vphilox_generate_n/1024` | 0.4799 | 5.236 GiB/s | 0.19% |
| `BM_vphilox_generate_n/256` | 0.5027 | 4.998 GiB/s | 0.10% |
| `BM_vphilox_generate_n/32` | 0.5944 | 4.228 GiB/s | 0.20% |
| `BM_pcg64` | 0.7347 | 3.421 GiB/s | 0.02% |
| `BM_vphilox_generate_n/8` | 0.8034 | 3.129 GiB/s | 0.17% |
| `BM_vphilox` (buffered) | 0.9829 | 2.557 GiB/s | 0.12% |
| `BM_mt19937` | 1.2450 | 2.019 GiB/s | 0.24% |
| `BM_philox_scalar` | 1.9347 | 1.299 GiB/s | 0.03% |

## AVX-512 against AVX2 — issue #27

Same machine, same binary, backend pinned with `VPHILOX_BACKEND`:

| backend | cycles/byte | vs scalar |
|---|---:|---:|
| scalar | 1.9311 | 1.00x |
| avx2 | 0.8462 | 2.28x |
| **avx512** | **0.4712** | **4.11x** |

**AVX-512 is 1.80x AVX2** — 90% of the 2x that doubling the lane count allows.
No downclocking penalty is visible. That is consistent with Sapphire Rapids,
where the frequency behaviour that made AVX-512 a bad trade on Skylake-SP was
largely addressed; it should **not** be assumed for older parts.

Issue #27 asks that dispatch not prefer AVX-512 until it demonstrably beats
AVX2 on the same part. On this part it does, by a wide margin. On a Skylake-SP
or Cascade Lake part the question is open and worth re-measuring before
trusting the default.

## Where this puts vphilox

With a real AVX-512 kernel, vphilox is **second in the matrix**, not last:

- **1.56x faster than PCG64**
- **2.64x faster than `std::mt19937`**
- **4.11x faster than its own scalar kernel**
- 1.27x *slower* than xoshiro256++

That last gap is the honest headline. xoshiro256++ remains the fastest
generator here, but the margin has gone from 5.84x on the Pi's scalar kernel to
1.27x — and it buys that speed by giving up everything counter-based: no O(1)
seek, no reproducible stream from an arbitrary (key, counter), no independent
substream per thread without `jump()`.

## The refill buffer is now the dominant cost

`BM_vphilox` (0.9829) against `BM_vphilox_bulk` (0.4712) puts the refill buffer
at **+108.6%** — it more than doubles the cost per byte.

This is the same effect tracked in
[`buffer-overhead-2026-08-23.md`](buffer-overhead-2026-08-23.md), sharpening as
the kernel gets faster: ~10% on x86 scalar, 25% on ARM scalar, 42% on AVX2, and
now 109% on AVX-512. The drain pass hands words back four at a time regardless
of how fast the kernel filled the buffer, so the faster the kernel, the more of
the total the drain becomes.

`generate_n` avoids it entirely (0.4715, 100.0% of raw kernel throughput), but
the crossover moved: 8 words per call is now worse than the buffer itself
(0.8034), and it takes ~1024 words to come within 2% of the kernel. A wider
kernel needs a wider minimum request, which is worth knowing for callers sizing
their batches.

## Artifacts

- [`results/sapphire-rapids-matrix.json`](../../results/sapphire-rapids-matrix.json)
- [`results/sapphire-rapids-matrix-environment.txt`](../../results/sapphire-rapids-matrix-environment.txt)
