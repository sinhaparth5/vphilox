# Bulk generation and the buffer overhead — 2026-08-23

Issues #36 and #37. #36 asked to "profile the per-call bounds check and reduce
the measured approximately 22% gap between buffered generation and the bulk
kernel". The profile says the bounds check is not the cost, so this document
records what the cost actually is and what closed it.

## Environment

| | |
|---|---|
| CPU | Intel Core i5-11300H (Tiger Lake), 4 cores / 8 threads |
| ISA | AVX2 (the resolved backend for every row below) |
| OS | Linux 7.0.0-30-generic |
| Compiler | GCC 15.2.0, `bench` preset (`-O3`, Release) |
| Affinity | CPU 3 (`taskset -c 3`) |
| Frequency scaling | `powersave` governor, turbo enabled |

Fifteen repetitions at a two-second minimum sample time; the table reports
medians. Serialized `RDTSC` supplied cycles per byte. Every coefficient of
variation below came in under 1.3%.

Same caveat as the AVX2 run: the governor cannot be pinned on this host, so
`RDTSC` counts at the nominal TSC rate rather than the actual core clock.
Cycles per byte is a comparison between rows of one run, not an absolute.

One procedural note worth keeping. **The first run on a cold core is useless**:
every row came out ~27% slower than the two runs after it, uniformly, because
the core had not reached a steady clock. Runs 2 and 3 agreed within 3% and
reproduced the AVX2 document's ratios (buffered / `mt19937` = 1.40x here versus
1.41x there), so run 3 is what is recorded. Discard the first run.

## Where the buffer overhead actually goes

The starting hypothesis in #36 was the per-call bounds check in `operator()`.
Three separate measurements say otherwise.

**The generated code is already tight.** GCC's hot loop for
`for (i) out[i] = g();` is nine instructions with the cursor held in a register
across iterations and the refill branch correctly laid out cold:

```asm
.L57:
    lea  rdx, 1[rax]                  ; cursor + 1
    mov  eax, DWORD PTR 64[r12+rax*4] ; buffer_[cursor]
    mov  QWORD PTR [r12], rdx         ; cursor_ writeback
    mov  DWORD PTR [r14+rbp*4], eax   ; out[i]
    add  rbp, 1
    cmp  r13, rbp
    je   .L28
    mov  rax, rdx
.L31:
    cmp  rax, 31
    jbe  .L57
```

There is no redundant reload and the compare is a well-predicted branch taken
once every 32 words. There is nothing here to remove.

**Micro-variants of the check change nothing.** Measured against the unmodified
engine at 1.98 cycles/byte: a pointer-and-end cursor instead of an index came
out at 2.03 (worse), a 32-bit cursor at 2.16 (worse), and marking `refill()`
`noinline`+`cold` at 2.00 (unchanged). None of these is a real effect.

**Enlarging the refill does not help either.** Sweeping the refill from 8 blocks
to 128 moved cycles per byte from 2.73 to 2.81 — slightly the wrong direction.
If the refill call were the bottleneck, amortising it over 16x more data would
have shown up. It did not.

The measurement that explains the gap is this one: an `operator()` loop reading
from a pre-generated buffer that **never refills at all** still costs
1.09 cycles/byte, against a raw AVX2 kernel that produces the same data at
1.06. In other words, draining the buffer word by word costs about as much as
generating the data did in the first place.

**The buffer overhead is a second pass over every byte.** The engine generates
128 bytes with vector stores, then hands them back four bytes at a time through
a scalar load-and-store. Both passes touch every byte. This was invisible when
the kernel was scalar and the drain was noise next to it — that is exactly why
the gap grew from ~10% on the scalar kernel to ~42% on AVX2, and why it will
grow again when AVX-512 lands. No amount of tuning the bounds check removes a
pass over the data. Only not making the pass does.

## Results

| Benchmark | Throughput | Cycles/byte | CV |
|---|---:|---:|---:|
| `BM_vphilox` (buffered `operator()`) | 2.299 GiB/s | 1.261 | 0.91% |
| `BM_vphilox_generate_n/8` | 2.344 GiB/s | 1.237 | 0.28% |
| `BM_vphilox_generate_n/32` | 2.807 GiB/s | 1.033 | 0.31% |
| `BM_vphilox_generate_n/256` | 3.768 GiB/s | 0.769 | 0.23% |
| `BM_vphilox_generate_n/1024` | 3.897 GiB/s | 0.743 | 0.26% |
| `BM_vphilox_generate_n/65536` | 3.948 GiB/s | 0.734 | 1.25% |
| `BM_vphilox_bulk` (raw AVX2 kernel) | 3.949 GiB/s | 0.734 | 0.39% |
| `BM_mt19937` | 1.644 GiB/s | 1.764 | 0.32% |
| `BM_philox_scalar` | 1.269 GiB/s | 2.285 | 0.38% |

The argument on `generate_n` is the chunk size in words the caller asks for.

| Ratio | |
|---|---:|
| Buffered `operator()` vs raw kernel | 58.2% — a 41.8% overhead |
| `generate_n` (bulk) vs raw kernel | **100.0%** |
| `generate_n` vs buffered `operator()` | **1.72x** |
| `generate_n` vs `std::mt19937` | **2.40x** |
| Buffered `operator()` vs `std::mt19937` | 1.40x |

`generate_n` reaches the raw kernel exactly, which is the point: there is no
longer any cost to going through the engine rather than calling the kernel by
hand, so callers keep seeking, seeding, and stream ordering for free.

## The chunk size that matters

`generate_n` is at 97% of the ceiling by 256 words and at 100% by 1024. Below
that the fixed cost of a refill is spread over less data and the curve falls
off, converging on plain `operator()` at 8 words. It never loses to
`operator()`, which is the property that makes it safe to recommend
unconditionally.

That last part is not automatic. The obvious implementation — hand any block
count straight to the kernel — is **dramatically worse than the buffer** for
small requests:

| Chunk | naive `generate_n` | width-aware `generate_n` |
|---|---:|---:|
| 8 words | 5.86 cycles/byte | 2.01 |
| 16 words | 5.44 | 1.70 |
| 32 words | 1.50 | 1.47 |
| 64 words | 1.28 | 1.27 |
| 256 words | 1.10 | 1.09 |

An 8-word request is 2 blocks. The AVX2 kernel batches 8 blocks at a time, so a
2-block call does zero vector iterations and runs entirely in the scalar tail —
the caller asks to skip the buffer and gets scalar Philox for their trouble,
nearly 3x slower than just using the buffer. `generate_n` therefore goes direct
only when the request is at least `preferred_blocks` wide and otherwise serves
the caller from a normal refill, which is always a full-width kernel call.

This is why `preferred_blocks` is part of the dispatch entry rather than a
comment: it is the threshold that keeps the fast path from being a trap.

## End to end: `vphilox_stream`

`tools/vphilox_stream` filled its 1 MiB output chunk a word at a time, which
made it the first real consumer to convert. Writing 2 GB to `/dev/null`,
`taskset -c 3`, best of two runs:

| Backend | Before | After | |
|---|---:|---:|---:|
| AVX2 | 0.90 s | 0.50 s | **1.80x** |
| scalar | 1.92 s | 1.65 s | 1.19x |

The output is byte-identical before and after on both backends (SHA-256 of a
64 MB stream, `8fe505fa3d916adef44f72a6206b79fc6d0e1a73f17ed5197763e335b955b8af`),
which is the check that matters — this is the tool the Phase 4 statistical
batteries run through, so its stream is not allowed to move.

At the new rate a 1 TB PractRand run (#41) spends roughly 4.2 minutes of CPU
generating rather than 7.6. The `float32` path still goes word by word; it
converts when SIMD float conversion lands (#34).

## What this leaves open

The gap is closed for callers who can ask for data in bulk. It is not closed,
and cannot be, for `std::shuffle`, `std::uniform_real_distribution`, and every
other standard facility that consumes a generator one word at a time — those
still pay the drain pass and still run at 2.299 GiB/s. That is the price of
satisfying `std::uniform_random_bit_generator`, and it is a reasonable price
now that there is a documented way around it.

Raw results: [`results/bulk-generate-baseline.json`](../../results/bulk-generate-baseline.json),
environment in [`results/bulk-generate-environment.txt`](../../results/bulk-generate-environment.txt).
