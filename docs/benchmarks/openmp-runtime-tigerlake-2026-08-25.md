# OpenMP against std::thread — is the scaling result the generator or the harness? 2026-08-25

Issue #51 found that aggregate cycles per byte is flat up to one thread per
physical core. That result is measured through `bench_scaling.cpp`'s own
persistent worker pool, and a benchmark cannot distinguish a property of the
generator from a property of its own scaffolding. Issue #52 adds a second
threading runtime so it can.

**The answer: the flat result is the generator, and OpenMP shows it more
cleanly than the pool does.** libgomp is flat to within 0.3% across one, two and
four workers — 1.000×, 1.000×, 1.003× — where the `std::thread` pool drifts to
1.220× at four. Both runtimes then show the same hyperthread knee at eight. The
flatness #51 reported is real; the deviation from it on this host belongs to the
harness, not to Philox.

## Environment

Same host, clock regime and commit as
[`icache-placement-tigerlake-2026-08-25.md`](icache-placement-tigerlake-2026-08-25.md):
Core i5-11300H, 4 cores × 2 threads, bare metal, `performance` governor, turbo
disabled so cycles/byte is true core cycles, GCC 15.2.0, backend `avx512`,
commit `0c0d513`. OpenMP is libgomp, found by CMake at configure time; without
it `bench_scaling` still builds and lists 24 rows instead of 48.

Both runtimes run the identical job over identical buffers and disjoint counter
ranges, with persistent workers on both sides — libgomp keeps its team alive
between parallel regions exactly as the pool does, so neither column carries
thread-spawn cost. The only variable is the construct that starts the workers.

### Why 256 KiB per worker and not 4 MiB

`bench_scaling` sweeps two footprints. Only the smaller one answers this
question. At 1048576 words each worker touches 4 MiB, so four workers need 16
MiB against this part's 8 MiB L3 and the rows go memory-bound — that is #51's
*second* limit, and it buries the port contention #52 is about. At 65536 words
each worker touches 256 KiB and four workers stay comfortably resident.

The 4 MiB rows were measured and are not archived; they showed the runtimes
agreeing to ~1% at one and two workers and diverging into memory-bandwidth
noise beyond, which is a fact about the L3, not about threading.

## The comparison

`generate_n`, 256 KiB per worker, aggregate cycles per byte, 15 repetitions.
The `×` columns are normalised to each runtime's own single-worker cost, so
flat means perfect scaling:

| workers | `std::thread` | CV | × | OpenMP | CV | × | diff |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0.5084 | 0.06% | 1.000 | 0.5086 | 0.14% | **1.000** | +0.0% |
| 2 | 0.5165 | 0.91% | 1.016 | 0.5086 | 0.07% | **1.000** | −1.5% |
| 4 | 0.6205 | 2.74% | 1.220 | 0.5102 | 0.41% | **1.003** | −17.8% |
| 8 | 0.8076 | 2.08% | 1.588 | 0.9126 | 0.29% | 1.794 | +13.0% |

Three things fall out.

**At one and two workers the runtimes are indistinguishable.** 0.5084 against
0.5086 with both CVs at or under 0.14% — a 0.04% difference on a measurement
resolving to better than a tenth of a percent. Whatever starts the threads makes
no difference at all when there are cores to spare.

**At four workers — one per physical core — OpenMP is flat and the pool is
not.** 1.003× against 1.220×. This is the row #52 exists to check, and the two
runtimes disagree by 17.8%, far outside the 2.74% CV. The flat answer is the one
that matches #51's sixteen-core result on Cascade Lake.

**At eight workers both show the knee.** 1.794× and 1.588×, against the 2×
that perfect scaling across eight logical CPUs would give. The hyperthread knee
appears in both runtimes, so it is not an artifact of either — which is the
other half of what this issue was asked to establish.

### Why the pool drifts at four workers

The leading explanation is thread count, not threading model. On the OpenMP path
the team master *is* a worker, so four workers means four runnable threads on
four cores. On the `std::thread` path the dispatcher is a separate thread that
blocks on a condition variable while the workers run — four workers plus a
dispatcher is five threads on four cores, and the dispatcher must be woken to
collect results once per iteration.

On #51's host that costs nothing: seventeen threads on thirty-two vCPUs never
run out of room, which is why the pool measured flat there and does so here at
one and two workers. It only bites when the workers have claimed every core.

**This is an inference, not a measurement.** It is consistent with everything
above — the effect appears exactly at full physical-core occupancy, and the
runtime whose master participates is the one that stays flat — but the
benchmark registers 1, 2, 4, 8, … workers only, so the direct test (three
workers plus a dispatcher, which should fit) cannot be run without changing the
row set. It is not needed for this issue's conclusion, which rests on the two
runtimes agreeing about the generator, and it is recorded here as the next thing
to check if the pool's overhead ever matters.

## Wait policy, and a second-order finding

libgomp busy-waits at the end of a parallel region before sleeping. Re-running
the whole comparison under `OMP_WAIT_POLICY=passive GOMP_SPINCOUNT=0` isolates
what that costs:

| workers | OpenMP default | OpenMP passive | change |
|---:|---:|---:|---:|
| 1 | 0.5086 | 0.5082 | −0.1% |
| 2 | 0.5086 | 0.5105 | +0.4% |
| 4 | 0.5102 | 0.5560 | **+9.0%** |
| 8 | 0.9126 | 0.8763 | **−4.0%** |

The tradeoff is the textbook one, measured: spinning wins while cores are free
and loses once they are not. At four workers, sleeping costs 9% in futex wakeups
per parallel region. At eight, where every spin steals execution ports from a
sibling that is still working, sleeping *saves* 4% and drops that row's CV from
0.29% to 0.12%.

It also explains a wrecked measurement worth recording. In the full 48-row
sweep, the two noisiest rows in the entire run were **single-threaded** OpenMP
at 11.15% and 11.09% CV — absurd for a benchmark with no barrier and no
contention of its own. With random interleaving turned on, a 1-worker row's
repetitions are scattered among the 32-worker rows, and libgomp's team threads
were still spinning from the previous benchmark. Under `passive` those same two
rows fall to 2.41% and 1.45%.

Anyone attaching per-thread counters to a mixed-runtime benchmark should know
that a neighbouring OpenMP region can contaminate an unrelated row.

## What is weak here, stated plainly

The `std::thread` rows at four and eight workers sit at **2.08–2.74% CV, above
this project's 1% bar**, as do OpenMP's four-worker rows under `passive`. This
host is a four-core laptop running a full desktop, and an unpinned benchmark
that wants every core has to share them with the compositor.

The conclusions above survive it because they rest on gaps much larger than the
error bars — 17.8% at four workers against a 2.74% CV, and a knee of 1.6–1.8×
at eight — and because the rows carrying the primary claim (OpenMP at one, two
and four workers: 0.14%, 0.07%, 0.41%) are the *clean* ones.

What this host cannot deliver is the full 1–32 curve under the documented
protocol. That run is archived as `tigerlake-scaling` for provenance and is
explicitly **not quotable row by row**: 30 of its 48 rows missed the CV bar, and
above eight workers a four-core machine is measuring oversubscription rather
than scaling. `machines.csv` labels it *harness validation*. The curve to quote
remains Cascade Lake's in
[`scaling-cascade-lake-2026-08-25.md`](scaling-cascade-lake-2026-08-25.md); this
host's contribution is the runtime contrast, which is a within-machine
comparison and needs no cross-machine transfer.

## Consequence

#51's advice is unchanged and now rests on two independent threading runtimes:
**size pools by physical cores.** The scaling result is a property of Philox and
the hardware, not of `bench_scaling.cpp`.

For callers using OpenMP, one practical note falls out of the wait-policy table:
if you run one worker per physical core, leave the default spin alone; if you
oversubscribe to every hyperthread, `OMP_WAIT_POLICY=passive` is worth measuring.
