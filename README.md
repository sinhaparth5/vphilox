# vphilox

SIMD-accelerated counter-based pseudorandom number generation for parallel CPU
systems. Header-only, zero-dependency, C++20.

> **Early development.** The scalar reference engine, the C++20 wrapper, and
> the dispatch layer work and are tested against the published Random123
> vectors. The SIMD kernels — the entire point of the library — are stubbed and
> currently fall back to scalar. See [ROADMAP.md](ROADMAP.md).

## The problem

Counter-based PRNGs compute `R = f(key, counter)` instead of stepping a state
machine. That buys three things a stateful generator cannot offer: O(1) seeking
to any point in the stream, lock-free parallelism across threads, and
bit-identical output on any hardware.

The catch is CPU throughput. Philox leans on 32×32→64 wide multiplies, and each
round depends on the previous one's product. On x86 that dependency chain
stalls the pipeline — NVIDIA engineers measured scalar Philox running roughly
**10× slower than `std::mt19937`** while investigating XGBoost's RNG, and
compilers cannot auto-vectorize their way out of it because the dependency is
real.

vphilox fixes this by vectorizing *across* independent counter streams rather
than within one. Several Philox evaluations occupy the lanes of a single SIMD
register, so the multiply latency amortizes across all of them and the pipeline
stays fed.

## Usage

```cpp
#include <vphilox/vphilox.hpp>

vphilox::engine g{seed};

std::uint32_t x = g();            // raw 32 bits
float f = g.next_float();         // uniform [0, 1), no division
double d = g.next_double();

g.discard(1ull << 40);            // O(1) — jump a trillion draws ahead

// Drops into anything taking a UniformRandomBitGenerator:
std::shuffle(v.begin(), v.end(), g);
std::normal_distribution<double> norm(0.0, 1.0);
double z = norm(g);
```

Per-thread streams need no synchronization — give each worker its own key, or
its own counter range:

```cpp
#pragma omp parallel
{
    vphilox::engine g{static_cast<std::uint64_t>(omp_get_thread_num())};
    // ... no locks, no atomics, no false sharing
}
```

## Building

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

Or with plain CMake:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### Consuming it

```cmake
find_package(vphilox 2026.08 REQUIRED)
target_link_libraries(your_app PRIVATE vphilox::vphilox)
```

Or vendor it — `add_subdirectory(vphilox)` gives you the same target, with
tests and benchmarks off by default when it is not the top-level project.

### Options

| Option | Default | Effect |
|---|---|---|
| `VPHILOX_BUILD_TESTS` | ON when top-level | GoogleTest suite |
| `VPHILOX_BUILD_BENCHMARKS` | ON when top-level | Google Benchmark suite |
| `VPHILOX_BUILD_TOOLS` | ON when top-level | `vphilox_stream` for PractRand |
| `VPHILOX_INSTALL` | ON when top-level | Install and export rules |
| `VPHILOX_FETCH_DEPS` | ON | Fetch GTest/Benchmark if not found locally |
| `VPHILOX_ENABLE_AVX2` | ON | Compile the AVX2 kernel in |
| `VPHILOX_ENABLE_AVX512` | ON | Compile the AVX-512 kernel in |
| `VPHILOX_ENABLE_NEON` | ON | Compile the NEON kernel in |
| `VPHILOX_FORCE_SCALAR` | OFF | Ignore every SIMD kernel |

Enabling a kernel does not force it onto a CPU that cannot run it — runtime
dispatch decides, so one binary runs everywhere. Set `VPHILOX_BACKEND` in the
environment (`scalar`, `avx2`, `avx512`, `neon`) to pin one for testing.

## Layout

```
include/vphilox/
  vphilox.hpp            umbrella header
  config.hpp             feature macros, architecture detection
  constants.hpp          Philox constants, counter4 / key2
  counter.hpp            128-bit counter arithmetic (this is what makes seeking O(1))
  float_cast.hpp         IEEE-754 mantissa injection
  philox.hpp             the engine: C++20 concept + aligned refill buffer
  version.hpp.in         generated from the VERSION file
  detail/
    kernel_scalar.hpp    reference implementation and ground truth
    kernel_avx2.hpp      Phase 2 — stubbed
    kernel_avx512.hpp    Phase 2 — stubbed
    kernel_neon.hpp      Phase 2 — stubbed
    cpu_features.hpp     runtime CPU probe
    dispatch.hpp         kernel selection
tests/                   GoogleTest, one file per concern
benchmarks/              Google Benchmark
tools/vphilox_stream.cpp raw bytes on stdout for PractRand / TestU01
docs/                    theory, prior art, development strategy and phases
```

Every kernel implements the same contract: `generate(base_counter, key, out,
blocks)` writes `blocks * 4` words, where block *i* is
`philox4x32(base + i, key)`. Output does not depend on how the caller chunks
the request, which is what makes the parity tests meaningful.

## Correctness

`tests/test_reference_vectors.cpp` checks the scalar core against the
`kat_vectors` published with Random123 (all-zeros, all-ones, digits of pi). Those
values are the contract — every SIMD kernel added later has to reproduce them
bit for bit, and `tests/test_kernel_parity.cpp` enforces that across a range of
counters, keys, and block counts, including the tails that do not fill a vector
register.

Statistical validation (PractRand to 1 TB, TestU01 BigCrush) is Phase 4.

## Versioning

CalVer, `YYYY.0M.MICRO`. The generated bit stream for a given (key, counter) is
frozen permanently — reproducibility is the product. See
[VERSIONING.md](VERSIONING.md).

## Documentation

- [`docs/vPhilox theory.md`](docs/vPhilox%20theory.md) — CBRNG theory, Philox mechanics, SIMD interleaving, float conversion math
- [`docs/Research on vPhilox.md`](docs/Research%20on%20vPhilox.md) — prior art and what vphilox does differently
- [`docs/Vector Philox Development Strategy.md`](docs/Vector%20Philox%20Development%20Strategy.md) — the full technical write-up
- [`docs/VPhilox Development Phases.md`](docs/VPhilox%20Development%20Phases.md) — the five-phase plan

## References

J. K. Salmon, M. A. Moraes, R. O. Dror, D. E. Shaw. *Parallel Random Numbers:
As Easy as 1, 2, 3.* SC'11.

## License

Dual-licensed under either of

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE) or
  <https://www.apache.org/licenses/LICENSE-2.0>)
- MIT License ([LICENSE-MIT](LICENSE-MIT) or <https://opensource.org/licenses/MIT>)

at your option. Source files carry `SPDX-License-Identifier: MIT OR Apache-2.0`.

The dual form is the C++/Rust ecosystem convention: Apache-2.0 supplies an
explicit patent grant, and MIT stays compatible with GPLv2 projects, which
Apache-2.0 alone is not. Taking either one is enough — you do not have to
comply with both.

### Contribution

Unless you explicitly state otherwise, any contribution intentionally submitted
for inclusion in this work by you, as defined in the Apache-2.0 license, shall
be dual licensed as above, without any additional terms or conditions.
