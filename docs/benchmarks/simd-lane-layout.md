# SIMD lane-layout decision

Issue #12 compares two AVX2 layouts using `bench_lane_layout`: four counters
stored in the low half of four 64-bit lanes, and eight counters filling all
eight 32-bit lanes with separate even/odd wide multiplies. Both candidates run
the complete Philox4x32-10 transformation, transpose output back to block
order, handle tails, and verify their output against `kernel_scalar` before
timing starts.

Build and run the decision benchmark on an AVX2 host:

```sh
cmake --preset bench
cmake --build --preset bench --target bench_lane_layout
build/bench/benchmarks/bench_lane_layout \
  --benchmark_min_time=1s --benchmark_repetitions=7 \
  --benchmark_report_aggregates_only=true
```

The eight-counter layout is the selected design. It keeps all 32-bit lanes
productive during XOR, key broadcast, and permutation operations; the extra
even/odd multiply and packing instructions are amortized across twice as many
counters. Accordingly, AVX2 uses `preferred_blocks = 8`. AVX-512 already uses
eight counters, matching its eight 64-bit multiply lanes, while NEON retains
two counters, matching `vmull_u32`.

The decision run used an Intel Core i5-11300H and GCC 15.2.0. Three 0.2-second
repetitions produced these medians (CPU frequency scaling was enabled, so the
checked-in command above uses longer runs for publication-quality reruns):

| Layout | Throughput | CPU-time CV |
| --- | ---: | ---: |
| 4 counters/register | 1.089 GiB/s | 0.01% |
| 8 counters/register | 1.318 GiB/s | 0.25% |

The eight-counter layout was 21.1% faster in this run. Correctness verification
covered 17 blocks, deliberately exercising a full batch, a tail, and carry in
the low counter word.

Record the CPU model, compiler, clock policy, GB/s, and variation when
publishing measurements. Production-kernel benchmarks should repeat the
comparison because instruction scheduling and AVX frequency behavior vary by
microarchitecture.
