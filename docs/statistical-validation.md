# Statistical validation

Phase 4 evidence that the vphilox bit stream is statistically sound. This
covers PractRand (issues #39–#43) and TestU01 (#44–#45).

Everything here is reproducible from the scripts in `scripts/statistical/`,
and every archived log carries its own provenance header: git SHA, CPU, the
resolved backend, and the verbatim command.

```bash
scripts/statistical/build_practrand.sh          # PractRand pre0.95 -> ~/.local/src
scripts/statistical/build_testu01.sh            # TestU01 -> ~/.local
scripts/statistical/run_practrand.sh --length 1TB --backend avx2
```

## Summary

| Issue | Test | Result |
|---|---|---|
| #40 | PractRand 1 GB smoke | **pass** — no anomalies in 194 results |
| #41 | PractRand 1 TB | **pass** — no anomalies in 304 results |
| #42 | Backend equivalence over 1 TB | **pass** — byte-identical |
| #43 | Float conversion | **pass** — see [The float32 stream](#the-float32-stream) |
| #45 | TestU01 SmallCrush | **pass** — 15/15 |
| #45 | TestU01 BigCrush | **pass** — all 160 statistics |

## PractRand to 1 TB (#41)

Run on a GCP `n2-standard-8` (Xeon @ 2.80 GHz, us-east1-b), AVX2 backend,
seed 0, PractRand 0.95:

```
vphilox_stream --seed 0 --format raw32 --backend avx2 \
  | RNG_test stdin32 -tlmax 1TB -tf 1 -multithreaded
```

```
length= 1 terabyte (2^40 bytes), time= 4173 seconds
  no anomalies in 304 test result(s)
exit_status: 0
```

70 minutes at ~263 MB/s, PractRand-bound rather than generator-bound —
vphilox produces 1 TB in about four minutes on this hardware, so the battery
is the slow half by a wide margin.

Twelve checkpoints, eleven of them reporting no anomalies at all. Full log:
`results/practrand/raw32-1TB-avx2.log`.

### The one flagged result

Across the entire terabyte, exactly one test was flagged, at the 4 GB
checkpoint:

```
[Low1/32]BCFN(2+1,13-3,T)   R=  -7.8  p =1-2.1e-4   unusual
...and 216 test result(s) without anomalies
```

`unusual` is PractRand's mildest severity, well below `suspicious` and `FAIL`.
The run produced 2,936 test results in total, so a single value landing in a
0.02% tail is around what one should expect to see rather than a surprise.

The stronger evidence is that it did not recur — not at 8, 16, 32, 64, 128,
256, 512 GB, nor at 1 TB. That matters more than the count, because PractRand's
checkpoints are not independent draws: each one re-runs the same tests over the
accumulated stream. Genuine structure therefore *strengthens* as more data
arrives and keeps flagging at every subsequent checkpoint. A defect that shows
up once at 4 GB and never again at 256x the data is a fluctuation, not a
finding.

## Backend equivalence (#42)

Issue #42 asked for PractRand to be repeated per backend. That would cost four
times as much and prove strictly *less* than what is recorded here.

The backends do not produce merely similar streams, they produce **identical**
ones — that is a design invariant, not a coincidence. Block *i* is
`philox4x32(base + i, key)` regardless of how many counters a kernel happens
to process at once, and `tests/test_kernel_parity.cpp` enforces it across
carrying counters, edge keys, and block counts straddling every plausible SIMD
width. Four identical byte streams give four identical verdicts by
construction, so running the battery four times measures the same bytes four
times.

What is recorded instead is a direct byte comparison over exactly the range
PractRand consumed:

```
cmp <(vphilox_stream --backend scalar --bytes 1T) \
    <(vphilox_stream --backend avx2   --bytes 1T)
```

```
scalar_sha256: cffff5685841693f25f174d079059746ac3d41a8206d4eb284f9c517568cef85
avx2_sha256:   cffff5685841693f25f174d079059746ac3d41a8206d4eb284f9c517568cef85
bytes:         1099511627776
VERDICT: IDENTICAL over all 1099511627776 bytes
```

`cmp` rather than comparing two digests: it is byte-exact and needs no
collision argument. The digests are tee'd off the same pass so third parties
can spot-check a reproduction. The 1 TB verdict above therefore transfers to
every backend that passes the parity matrix.

Full log: `results/practrand/backend-equivalence-1TB.txt`.

**The AVX-512 arm was blocked and is no longer.** When this was written the
stub set `implemented = false`, so dispatch correctly refused it and
`--backend avx512` resolved to avx2. #24 landed on 2026-08-24 and the kernel
is now selected on hardware that reports `avx512f`/`avx512dq`.

The terabyte `cmp` above has *not* been repeated with it, and this section is
not claiming it has. What covers AVX-512 instead is the same argument that
retired the other three arms, applied one layer down:
`tests/test_kernel_parity.cpp` sweeps it against the scalar kernel across
carrying counters, edge keys and every block count that straddles a plausible
SIMD width, and `test_cross_platform_parity.cpp` folds an 8191-block stream —
tails, refill buffer, chunked `generate_n`, carry chain and float conversion —
into a digest pinned in the file. A kernel that reproduces the scalar stream
bit for bit cannot fail a battery the scalar stream passes.

The same now holds for NEON on real hardware: the Raspberry Pi 5 runs the full
suite green at `cce395b` with the backend resolving to `neon`, so that digest
is confirmed identical across two architectures and four kernels rather than
argued from x86 alone.

Re-running the terabyte comparison on AVX-512 is cheap if a suitable host is
free and would be worth doing. #42 was closed on the byte-identity argument
rather than on four batteries, and that reasoning is unchanged by a fourth
kernel existing — this is a belt-and-braces run, not a gap in the evidence.

## The float32 stream

Issue #43 asked for the float32 stream to be run through PractRand. **That
test is not meaningful, and it was replaced rather than performed.**

`to_float01` injects 23 random bits into the mantissa of a value in [1, 2) and
subtracts 1.0. That subtraction renormalises, and the consequences are
structural:

- the sign bit is always clear,
- the exponent field is geometrically skewed — half of all outputs land in
  [0.5, 1) and share one exponent, a quarter in [0.25, 0.5), and so on,
- low mantissa bits are frequently zero, because renormalising a small result
  shifts zeros in from the right.

Feeding those bytes to a bit-level battery measures the IEEE-754 format, not
the generator. vphilox duly fails 114 tests, with `[Low1/32]` — the lowest bit
of each word — screaming loudest at p = 2e-1994, exactly as the mechanism
predicts. **Every correct float generator fails this test.** The bits of a
uniform float are not uniform bits.

That log is archived at
`results/practrand/float32-raw-bits-expected-failure.log` rather than deleted,
so the next person to try it finds the explanation instead of a panic.

What the conversion actually has to guarantee is tested in
`tests/test_float_cast.cpp`:

- **`InjectedBitsSurviveConversionExactly`** — exhaustive over all 2^23
  representable outputs. Both steps are exact (the subtract by Sterbenz's
  lemma; the add-back because `k * 2^-23` needs only 23 significant bits), so
  every injected bit must round-trip. The double form samples 500k values
  through `next_double()`, covering the word pairing too.
- **`IsUniformByChiSquared`** — 1024 bins over 2^23 draws, bounded five sigma
  in both directions. The lower bound is deliberate: a statistic far *below*
  the degrees of freedom means counts track expectation more closely than
  chance allows, which is what a silently coarsened grid looks like.
- **`MatchesUniformCdfByKolmogorovSmirnov`** — catches systematic CDF drift,
  which chi-square cannot see because it ignores bin ordering.

Mutation testing shows these are not redundant. A **stuck low mantissa bit** is
caught only by the round-trip test and the existing monotonicity check —
chi-square, KS, and the coarse bucket test all pass it, because halving the
grid resolution still leaves values uniform at any coarser scale. No
distributional test can detect that class of defect, which is precisely why an
exact round-trip is worth its runtime.

## TestU01

TestU01 has no stdin mode; it pulls from a callback rather than reading a
stream, so it needs `tools/vphilox_testu01`, compiled against the library. That
is the better test anyway: the battery drives `vphilox::engine` directly, so
what gets exercised is the engine a caller would really use — refill buffer and
runtime dispatch included — rather than a byte stream downstream of it.

SmallCrush passes 15/15 on both the laptop and the GCP Xeon.

### BigCrush (#45)

Run on the same GCP `n2-standard-8`, AVX2 backend, seed 0:

```
========= Summary results of BigCrush =========

 Version:          TestU01 1.2.3
 Generator:        vphilox 2026.08.0 philox4x32-10 backend=avx2 seed=0
 Number of statistics:  160
 Total CPU time:   02:31:25.82

 All tests were passed
```

Full log: `results/testu01/bigcrush-avx2.log`.

One value in the run carries TestU01's `*****` suspect marker, and it is worth
naming rather than hiding behind the summary line. `sknuth_MaxOft`'s chi-square
sub-statistic came in at p = 0.9993, just past the [0.001, 0.9990] band TestU01
marks. It is not among the 160 statistics the battery scores, which is why the
verdict is still a clean pass — but the more useful point is that one such
value is exactly what should happen. BigCrush computed 254 p-values here;
0.51 are expected to fall outside that band by chance, and the probability of
seeing at least one is 40%. A run with none would be the anomaly.

The two batteries agree, which is the result worth having: PractRand found
nothing across 1 TB and 304 test results, and BigCrush found nothing across 160
statistics driving the engine directly.

TestU01 is fetched, never vendored: it ships under its own academic-use
licence, not MIT/Apache. Three upstream problems are worked around in
`build_testu01.sh` — the canonical download URL 302s to a doubled
`/~simul/~simul/` path and serves a 404 page that `curl -o` will happily save
as the archive; GCC 14 defaults to C23, where `()` means "no parameters" and
TestU01's K&R declarations become hard errors; and the GitHub tarball drops the
executable bit on the autotools helpers.

## A note on running these

`vphilox_stream` ignores `SIGPIPE`. PractRand closes the pipe the instant it
reaches `-tlmax`, and under the default disposition the generator was killed
before `fwrite` could return — so a *completed* 1 TB run exited 141, which any
runner using `set -o pipefail` reads as a failure. The `exit_status: 0` in the
log above is that fix proving out.

For the batteries themselves, cycle accuracy is irrelevant — they need correct
bits and wall time, so a cloud VM is a perfectly good host and noisy neighbours
do not matter. This is the opposite of the benchmark runs in
`docs/benchmarks/`, which need a quiet, frequency-pinned machine. Note that a
laptop on battery will not provide one: sustained throughput on the i5-11300H
above dropped to 1500 MHz against a 4.2 GHz turbo ceiling, roughly halving
throughput, while short bursts still measured full speed.
