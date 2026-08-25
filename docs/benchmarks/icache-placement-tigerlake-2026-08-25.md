# Instruction supply at the hyperthread knee, 2026-08-25

Issue #51 established that the first scaling limit is hyperthread co-location:
one worker per physical core is flat, two workers on one core's sibling threads
cost about a third each, and the kernel is execution-port-bound. Issue #53 asks
the obvious follow-up — is *instruction supply* part of that? Two sibling
threads share one L1 instruction cache, so a plausible story is that the knee is
partly an i-cache effect rather than pure port contention.

**The answer: it is not. Co-location costs 59% more cycles per byte while L1
i-cache misses per thousand instructions fall by 36%.** Instruction supply moves
in the *opposite* direction to the penalty, which excludes it as the mechanism
and leaves #51's port-contention explanation standing.

## Why this took a specific machine

The harness landed in `c5c13dc` and then sat unused, because no host on record
could run the measurement. It needs SMT, a working PMU, and *controllable thread
placement* at the same time. The Cascade Lake host from #51 has no vPMU (GCP
exposes none). The Pi 5 has no SMT. The WSL2 laptop reported all three and had
none of the third: `taskset` there fixes virtual CPU IDs while the hypervisor
places vCPUs itself, so both arms of the study silently became the same
placement.

That failure is why `run_placement.sh` measures the co-location penalty *before*
it will archive anything, and aborts below 15%. This host clears it by a wide
margin, and the gate reproduced across every invocation:

| run | separate cores | shared core | penalty |
|---|---:|---:|---:|
| turbo on | 0.3787 | 0.5136 | 35.6% |
| turbo off, run 1 | 0.5148 | 0.7241 | 40.6% |
| turbo off, run 2 | 0.5163 | 0.7173 | 38.9% |

#51 measured 35% on a completely different part. That two unrelated CPUs agree
on the size of the penalty is the strongest evidence that the effect is a
property of the kernel rather than of either machine.

## Environment

| | |
|---|---|
| CPU | Intel Core i5-11300H @ 3.10 GHz (Tiger Lake) |
| Topology | 1 socket × 4 cores × 2 threads = 8 logical CPUs |
| Sibling map | `0,4` `1,5` `2,6` `3,7` — offset by core count, not adjacent |
| Caches | L1d 48 KiB/core, **L1i 32 KiB/core**, L2 1280 KiB/core, L3 8 MiB shared |
| Backend | `avx512`, stamped into every JSON `context` |
| Compiler | GCC 15.2.0 |
| Commit | `0c0d513` |
| Governor | `performance` |
| Turbo | **disabled** (`intel_pstate/no_turbo=1`) |

Bare metal, not a VM — `systemd-detect-virt` reports `none`. This is the same
laptop already in `machines.csv` as `avx2-baseline`, whose earlier runs are
labelled *harness validation: unpinned governor*; those conditions are fixed
here.

Turbo is off deliberately. RDTSC counts reference cycles at the nominal rate,
and this part's base clock (3.1 GHz) *is* the TSC rate, so with boost disabled
reference cycles and core cycles coincide and cycles/byte is a true core-cycle
count. It also stops a moving turbo ceiling from landing in the column, which is
the failure mode `CLAUDE.md` warns about. The cost is that absolute numbers here
are ~1.4× the same machine's boosted numbers (4.4/3.1), so they are not
comparable with this host's older archived runs — only within this study.

## The measurement

`scripts/benchmarks/run_placement.sh --tag tigerlake` runs four workers twice,
changing only placement, with per-worker retired-instruction and L1
i-cache-read-miss counters attached:

| placement | CPUs | cycles/byte | CV | i$ MPKI | instr/byte |
|---|---|---:|---:|---:|---:|
| one thread per physical core | 0,1,2,3 | 0.5160 | 0.67% | **0.2108** | 0.958554 |
| packed onto 2 cores' siblings | 0,4,1,5 | 0.8204 | 2.22% | **0.1356** | 0.958554 |
| | | **+59.0%** | | **−35.7%** | **±0.000000** |

Instruction supply gets *better* under co-location, not worse, while the cost
per byte rises by nearly 60%. Whatever the sibling threads are contending for,
it is not the instruction cache.

The direction is worth a sentence, because at first glance it looks wrong.
Both siblings run the *identical* kernel over disjoint counter ranges. Sharing
one 32 KiB L1i between two threads executing the same loop is constructive —
the second thread finds the code already resident — so co-location slightly
improves instruction supply even as execution-port contention swamps the result.
A workload whose threads ran *different* code would not get this for free.

### The self-check

`instructions_per_byte` is a property of the kernel. It cannot move with thread
count or placement, so a run where it moves has counters attached to the wrong
threads — which is the specific way this measurement fails silently, since
Google Benchmark's own `--benchmark_perf_counters` would have counted the
dispatcher rather than the workers.

It reads **0.958554 in both arms, identical to six decimal places, at 0.00% CV
across fifteen repetitions**. The counters were on the right threads.

A second, independent check: MPKI is a per-instruction ratio and so is
clock-independent. The turbo-on run measured 0.211 / 0.134 against turbo-off's
0.2108 / 0.1356 — agreement to about 1% across a 42% change in clock, which is
what a correctly normalised counter should do.

## What is weak here, stated plainly

The co-located arm's cycles/byte CV is **2.22%, above this project's 1% bar**,
and `run_matrix.sh` flagged it. The cause is this host: a four-core laptop
running a full desktop, where the packed arm confines four workers to two
physical cores and leaves the desktop free to land on them. The phys arm, with
twice the cores, came in at 0.67%.

Two things keep the conclusion standing despite that:

- The ht arm's **median reproduces to 0.04% across two independent runs**
  (0.8201 and 0.8204). The CV is inflated by occasional outlier repetitions, not
  by an unstable median.
- The finding is a **direction, not a threshold**. MPKI fell under co-location in
  all four runs and both clock regimes; a 2% error bar on a 36% effect does not
  reach the conclusion.

The honest summary is that the cycles/byte column here is a weaker version of
what #51 already measured on better hardware, and the MPKI column — the column
this issue is about — is solid.

## Consequence

Instruction supply is excluded, so #51's advice is unchanged and now has a
mechanism behind it on two unrelated parts: **size thread pools by physical
cores, not `hardware_concurrency()`.** Absolute MPKI is 0.14–0.21 under every
placement tried, roughly one i-cache miss per 5,000–7,000 instructions, which is
low in absolute terms as well as flat in the direction that matters.

The earlier weak indication recorded in #53 from the rejected WSL2 host — MPKI
0.19–0.26, "unlikely to be the mechanism" — pointed the right way. It is now
measured on a host where placement is real.
