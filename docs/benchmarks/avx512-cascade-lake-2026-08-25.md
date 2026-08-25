# AVX-512 on Cascade Lake — 2026-08-25

Issue #27, the last untested part. It asks that dispatch prefer AVX-512 only
where it demonstrably beats AVX2 **on the same processor**, because Intel's
frequency licensing can make a 512-bit kernel a net loss.

[`avx512-sapphire-rapids-2026-08-24.md`](avx512-sapphire-rapids-2026-08-24.md)
and [`avx512-downclocking-2026-08-24.md`](avx512-downclocking-2026-08-24.md)
answered it for Sapphire Rapids and Skylake-SP. Cascade Lake sits between them,
and the expectation recorded at the time was that it would behave the same.

**It does not, quite.** AVX-512 still wins decisively, so the dispatch decision
is unchanged and #27 closes — but this is the first part measured here where
anything resembling the penalty the issue was written about shows up.

> **Superseded in part, 2026-08-25.** A second Cascade Lake — the whole
> two-socket host rather than four vCPUs of a shared one — converts **92%** of
> the doubled width, matching Skylake-SP, and a direct frequency measurement
> there shows no licence drop at all. The 78% below is a property of that
> 4-vCPU instance, not of the architecture. See
> [the correction](#the-correction-a-whole-socket-cascade-lake) at the foot of
> this page; the hypothesis section between here and there is left standing so
> the reasoning that turned out to be wrong is still on the record.

## Environment

| | |
|---|---|
| CPU | Intel Xeon @ 2.80 GHz, family 6 **model 85 stepping 7** |
| Instance | GCP `n2-standard-4`, `--min-cpu-platform="Intel Cascade Lake"`, `europe-west2-b` |
| OS | Ubuntu 24.04 LTS, Linux 6.17 |
| Compiler | GCC 13.3.0 — the same compiler as the Skylake-SP run |
| ISA | `avx512f`, `avx512dq`, `avx512cd`, `avx512bw`, `avx512vl`, `avx512_vnni` |
| Caches | L1d 64 KiB ×2, L2 2 MiB ×2, L3 33 MiB |
| Affinity | CPU 1 |
| Commit | `7e739fd` |

Stepping 7 and the presence of `avx512_vnni` are what confirm this is Cascade
Lake rather than Skylake-SP, which is stepping 4 and has no VNNI.
`--min-cpu-platform` is a floor, not a guarantee, so the part was checked rather
than assumed.

Cloud VM, so steal time is invisible; every CV below came in at 0.07–0.23%,
well inside this project's 1% bar. The instance was deleted after the run. All
113 tests passed here with the backend resolving to `avx512` — a fifth machine
for the cross-platform digest.

## The measurement

`BM_vphilox_bulk`, one binary, backend forced with `VPHILOX_BACKEND`, nine
randomly interleaved repetitions:

| backend | cycles/byte | vs AVX2 | vs scalar |
|---|---:|---:|---:|
| scalar | 2.4142 | | 1.00x |
| avx2 | 0.8537 | 1.00x | 2.83x |
| **avx512** | **0.5466** | **1.56x** | **4.42x** |

**AVX-512 is 1.56x AVX2, so preferring it is correct here too.** That is the
question #27 asks, and the answer is yes on all three AVX-512 generations
reachable.

## Where it differs from the other two parts

AVX-512 interleaves sixteen counters per iteration against AVX2's eight —
exactly twice the lanes. How much of that doubling each part actually converts
into speedup:

| part | scalar | avx2 | avx512 | avx512/avx2 | avx2/scalar | width converted |
|---|---:|---:|---:|---:|---:|---:|
| Skylake-SP | 2.1753 | 0.7698 | 0.4202 | 1.83x | 2.83x | **92%** |
| **Cascade Lake** | 2.4142 | 0.8537 | 0.5466 | **1.56x** | 2.83x | **78%** |
| Sapphire Rapids | 1.9311 | 0.8462 | 0.4712 | 1.80x | 2.28x | **90%** |

Width converted is a ratio of ratios measured inside each machine, so it
compares across machines even though cycles/byte does not.

Cascade Lake converts 78% of the doubled width where the other two convert
about 90%. The fourteen-point shortfall is confined to the 512-bit path: the
**AVX2 kernel on this part is 2.83x scalar, identical to Skylake-SP's 2.83x**.
Same family, same compiler, same kernel, same AVX2 behaviour — and only the
512-bit row is off.

## The hypothesis, and why it is not a finding

A light-tier AVX-512 licence transition fits. The two parts differ in exactly
the way that would matter: the Skylake-SP instance is nominally 2.00 GHz and
this one is 2.80 GHz. A part already at or below its AVX-512 licence ceiling
has no clock to give up; a 2.80 GHz part has roughly 15% of headroom to lose,
and a 15% core downclock inflates *reference*-cycles per byte by about the
amount seen. RDTSC counts reference cycles, so a moving core clock lands
directly in this column — the third measurement rule in `CLAUDE.md`, arriving
in a place it was not expected.

**This was not confirmed, and it is recorded as a hypothesis.** The direct test
is `cycles` against `ref-cycles`, whose ratio is the actual clock. GCP does not
expose a virtual PMU to the guest:

```
$ perf stat -e cycles,ref-cycles true
   <not supported>      cycles
   <not supported>      ref-cycles
```

Both counters are unavailable, so the one measurement that would settle it
could not be taken on this host. Confirming it needs a bare-metal Cascade Lake
part, which is not currently reachable. The competing explanation — that the
two SKUs differ in 512-bit execution resources — is not excluded either.

What can be said without the counter: the shortfall is real, reproducible at
0.07–0.15% CV, specific to the 512-bit path, and **not large enough to change
any decision**.

## What this does and does not change

**Dispatch is unchanged.** There is no part measured where preferring AVX2
would be right. Even at 78% width conversion, AVX-512 is 1.56x AVX2 and 4.42x
scalar. The `{avx512, avx2, neon}` preference order in `detail/dispatch.hpp`
stands on all three generations.

**The earlier phrasing was too strong.** `avx512-downclocking-2026-08-24.md`
says the penalty "does not appear", which was true of the two parts it
measured and is not true as a general claim about the architecture. That page
now carries a pointer here. The light-tier argument in it is still the right
explanation for why the effect is *small* — Philox's inner loop is
`vpmuludq`/`vpaddd`/`vpxord`/`vpsrlq` and shuffles with no floating-point
instruction anywhere — but "light tier" means a smaller licence drop, not none,
and Cascade Lake is where that distinction became visible.

## Throughput matrix

The full Phase 4 column, taken on the same instance
(`scripts/benchmarks/run_matrix.sh --tag cascadelake --cpu 1`), every row at
0.04–0.23% CV:

| Benchmark | Cycles/byte | Throughput | vs `std::mt19937` |
|---|---:|---:|---:|
| `BM_xoshiro256pp` | 0.3917 | 6.652 GiB/s | 0.17x |
| `BM_vphilox_bulk` | 0.5472 | 4.762 GiB/s | **0.24x** |
| `BM_vphilox_generate_n/65536` | 0.5464 | 4.769 GiB/s | 0.24x |
| `BM_vphilox_generate_n/1024` | 0.5597 | 4.656 GiB/s | 0.25x |
| `BM_vphilox_generate_n/256` | 0.5945 | 4.384 GiB/s | 0.26x |
| `BM_pcg64` | 0.7171 | 3.635 GiB/s | 0.32x |
| `BM_vphilox_generate_n/32` | 0.7677 | 3.396 GiB/s | 0.34x |
| `BM_vphilox_generate_n/8` | 1.0518 | 2.479 GiB/s | 0.47x |
| `BM_vphilox` | 1.3218 | 1.972 GiB/s | 0.58x |
| `BM_mt19937` | 2.2606 | 1.154 GiB/s | 1.00x |
| `BM_philox_scalar` | 2.4792 | 1.052 GiB/s | 1.10x |

Two things worth noting. Unspecialised Philox is **1.10x `std::mt19937`** here,
the closest any part measured has come to parity, and nowhere near the 10x
penalty the original objection asserted. And the refill buffer costs 2.42x the
bulk path — the widest gap of any machine measured, which is the same pattern
seen elsewhere: the faster the kernel, the more the drain pass dominates.

xoshiro256++ leads the bulk path by 1.40x on this part.

## Artifacts

- [`results/cascadelake-scalar.json`](../../results/cascadelake-scalar.json),
  [`-avx2.json`](../../results/cascadelake-avx2.json),
  [`-avx512.json`](../../results/cascadelake-avx512.json) — the pinned sweep
- [`results/cascadelake-matrix.json`](../../results/cascadelake-matrix.json) — the matrix
- [`results/cascadelake-environment.txt`](../../results/cascadelake-environment.txt),
  [`-matrix-environment.txt`](../../results/cascadelake-matrix-environment.txt)
- [`docs/benchmarks/raw/`](raw) and [`plots/`](plots) — regenerated with this column

These are the first runs whose JSON carries its own resolved backend in the
`context` block, so nothing here had to be recovered by hand.

## The correction: a whole-socket Cascade Lake

The write-up above closed on a hypothesis: that a light-tier AVX-512 licence
transition cost this part about fourteen points of width conversion, untestable
because GCP exposes no virtual PMU. Issue #51 needed a many-core x86 host, which
turned out to be the machine that could answer this as well.

| | |
|---|---|
| CPU | Intel Xeon @ 2.80 GHz, family 6 model 85 **stepping 7** — the same part |
| Instance | GCP `n2-standard-32`, `--min-cpu-platform="Intel Cascade Lake"`, `us-east1-b` |
| Topology | 2 sockets × 8 cores × 2 threads = 32 logical CPUs, 2 NUMA nodes |
| Caches | L2 16 MiB (16 instances), **L3 66 MiB (2 instances, 33 MiB per socket)** |
| Compiler | GCC 13.3.0, CMake 3.28.3 — identical to the 4-vCPU run |
| Affinity | CPU 2 |
| Commit | `35e08af` |

`BM_vphilox_bulk`, same binary, backend forced, seven interleaved repetitions,
every row at 0.05–0.46% CV:

| backend | cycles/byte | vs AVX2 | vs scalar |
|---|---:|---:|---:|
| scalar | 2.2883 | | 1.00x |
| avx2 | 0.8740 | 1.00x | 2.62x |
| **avx512** | **0.4747** | **1.84x** | **4.82x** |

**1.84x, not 1.56x.** Width conversion is 92%, which is Skylake-SP's number to
the point.

### The licence hypothesis is now contradicted, not merely unconfirmed

The 32-vCPU host has whole cores rather than a slice of a shared socket, so the
clock can be measured from inside the guest without a PMU. A dependent chain of
`addq` instructions retires exactly one per core cycle, so timing a chain of
known length against a wall clock gives the core frequency, and against RDTSC
gives the reference frequency. Their ratio is the turbo state. The probe thread
and every load thread get their own logical CPU — without that the probe
time-shares a core and reports 1/N of the clock, which looks exactly like a
catastrophic downclock and is nothing of the kind.

Core GHz, with N cores hammering the kernel:

| active cores | scalar load | AVX2 load | AVX-512 load |
|---:|---:|---:|---:|
| 1 | 3.371 | 3.371 | 3.371 |
| 4 | 3.368 | 3.367 | 3.366 |
| 8 | 3.370 | 3.369 | 3.368 |
| 16 | 3.369 | 3.368 | 3.366 |
| 32 | 3.151 | 3.358 | 3.158 |

Reference frequency read 2.800 GHz in every cell, as it must.

**AVX-512 load does not depress the core clock on this part**, at any core
count up to 16, by more than 0.15% — which is the probe's own noise. The
light-tier argument was right about the mechanism and wrong about the
conclusion: Philox's inner loop is `vpmuludq`/`vpaddd`/`vpxord`/`vpsrlq` and
shuffles with no floating-point instruction, and on Cascade Lake that costs
nothing at all rather than a little. The 32-core row drops for both scalar and
AVX-512 and not for AVX2, which is hyperthread contention on the probe's own
core rather than a licence effect — at 32 active the probe shares a physical
core with a load thread.

### What the 4-vCPU number was, then

Unknown, and it stays unknown. What can be said is what it was not: it was not
Cascade Lake declining to convert 512-bit width, because the same stepping
converts 92% of it here. The remaining candidate is the instance — four vCPUs
are two cores of a socket shared with other tenants, whose activity moves the
package turbo budget underneath a measurement that RDTSC reports in reference
cycles. That is consistent with the scalar kernel also being slower there
(2.4142 against 2.2883 for identical code on an identical part), which no
AVX-512 licence can explain.

That reading is *not* confirmed either, and the honest summary of #27 across
both hosts is:

- AVX-512 beats AVX2 on every part measured, by 1.56x at worst and 1.84x at
  best. **Dispatch is correct and unchanged.**
- On a part whose cores are not shared, Philox's AVX-512 kernel triggers no
  measurable frequency licence drop. That is now measured rather than argued.
- Small shared-socket cloud instances are not a sound basis for a claim about a
  microarchitecture, which is the third measurement rule in `CLAUDE.md` biting
  in a new way: it warns that cycles/byte does not transfer across machines, and
  the 4-vCPU and 32-vCPU hosts here are two machines despite being one part.

### Artifacts for the correction

- [`results/cascadelake-32v-scalar-matrix.json`](../../results/cascadelake-32v-scalar-matrix.json),
  [`-avx2-matrix.json`](../../results/cascadelake-32v-avx2-matrix.json),
  [`-avx512-matrix.json`](../../results/cascadelake-32v-avx512-matrix.json)
- [`results/cascadelake-32v-frequency-probe.txt`](../../results/cascadelake-32v-frequency-probe.txt)
  — the table above, verbatim
- [`scripts/benchmarks/freq_probe.cpp`](../../scripts/benchmarks/freq_probe.cpp)
  — the probe, with its build line
- [`scaling-cascade-lake-2026-08-25.md`](scaling-cascade-lake-2026-08-25.md) —
  the run this host was actually provisioned for
