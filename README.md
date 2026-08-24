<p align="center">
  <img src="docs/assets/vphilox-logo.svg" width="200" height="200"
       alt="vphilox logo: a V traced across a lattice of counter cells">
</p>

<h1 align="center">vphilox</h1>

<p align="center">
  <a href="https://github.com/sinhaparth5/vphilox/actions/workflows/ci.yml"><img alt="Build and test status" src="https://github.com/sinhaparth5/vphilox/actions/workflows/ci.yml/badge.svg?branch=master"></a>
  <a href="VERSIONING.md"><img alt="Development version 2026.08.0" src="https://img.shields.io/badge/version-2026.08.0-22c55e"></a>
  <a href="CMakeLists.txt"><img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599c?logo=cplusplus"></a>
  <a href="CMakeLists.txt"><img alt="Header-only library" src="https://img.shields.io/badge/header--only-yes-334155"></a>
  <a href="#license"><img alt="MIT or Apache 2.0 license" src="https://img.shields.io/badge/license-MIT%20OR%20Apache--2.0-22c55e"></a>
</p>

<p align="center">
  Fast, repeatable random numbers for C++ programs that do work in parallel.
</p>

vphilox is a small C++20 library for simulations, tests, games, data tools, and
other programs that need a lot of random numbers. Give it the same seed and it
produces the same results, even when the work is split across threads.

> vphilox is still in early development. The scalar engine works and passes the
> published Random123 test vectors, and the AVX2 backend is now implemented --
> 3.3x the scalar kernel, and 1.41x `std::mt19937` through the buffered engine
> on a Tiger Lake laptop. The AVX-512 and NEON paths still fall back to scalar.
> Follow the work in the [roadmap](ROADMAP.md).

## What it gives you

- A given seed produces repeatable results across runs and supported machines.
- Each thread can own a random stream without locks or shared state.
- `discard()` jumps to a later point in a stream immediately.
- The engine works with standard C++ algorithms and distributions.
- Filling a whole buffer at once runs at full speed, without the per-value cost.
- The library is header-only and has no runtime dependencies.

## A small example

```cpp
#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

#include <vphilox/vphilox.hpp>

vphilox::engine random{42};

std::uint32_t bits = random();
float chance = random.next_float();   // between 0 and 1
double precise = random.next_double();

std::vector<int> values{1, 2, 3, 4, 5};
std::shuffle(values.begin(), values.end(), random);

std::normal_distribution<double> normal{0.0, 1.0};
double sample = normal(random);
```

Jump ahead without generating everything in between:

```cpp
random.discard(1ull << 40);
```

That jump takes constant time.

## Filling a buffer

Asking for one value at a time has a cost per value. When you need many values
at once, ask for them together:

```cpp
std::vector<std::uint32_t> block(1'000'000);
random.generate(block);                     // or: random.generate_n(ptr, count)
```

This produces exactly the same numbers, in the same order, as calling
`random()` a million times, and leaves the generator in the same place — you
can mix the two styles on one engine. On a machine with AVX2 it runs about
1.7 times faster than the one-at-a-time loop. Ask for a few hundred values or
more to get the full benefit.

## Using it from several threads

Give each worker its own seed. The workers do not need to coordinate access to
one shared generator.

```cpp
#pragma omp parallel
{
    auto worker = static_cast<std::uint64_t>(omp_get_thread_num());
    vphilox::engine random{worker};

    // Use random() inside this worker.
}
```

For reproducible jobs, keep the worker-to-seed mapping stable between runs.

## How it works

Most random-number engines keep changing an internal state. vphilox instead
treats a key and a counter like coordinates: the same pair always gives the
same output. That makes it easy to split a large job into independent pieces or
resume at a known position.

Philox calculations are also independent of one another. vphilox is being
built to process several of them together with the vector instructions already
available on modern CPUs. The plain scalar path is the reference that every
faster path must match bit for bit.

## Add vphilox to a project

With an installed copy:

```cmake
find_package(vphilox 2026.08 REQUIRED)
target_link_libraries(your_app PRIVATE vphilox::vphilox)
```

Or keep the repository inside your project:

```cmake
add_subdirectory(vphilox)
target_link_libraries(your_app PRIVATE vphilox::vphilox)
```

Tests, benchmarks, and command-line tools stay off when vphilox is included as
a subdirectory of another project.

## Build and test

You need CMake 3.24 or newer, Ninja, and a C++20 compiler.

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

Before sending a pull request, run the stricter checks too:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

GoogleTest and Google Benchmark are downloaded during configuration when they
are not already installed. Set `VPHILOX_FETCH_DEPS=OFF` for an offline build
that uses only installed packages.

<details>
<summary>CMake options</summary>

| Option | Default | Purpose |
|---|---|---|
| `VPHILOX_BUILD_TESTS` | On at the top level | Build the GoogleTest suite |
| `VPHILOX_BUILD_BENCHMARKS` | On at the top level | Build performance checks |
| `VPHILOX_BUILD_TOOLS` | On at the top level | Build the PractRand stream tool |
| `VPHILOX_INSTALL` | On at the top level | Create install and package files |
| `VPHILOX_FETCH_DEPS` | On | Download missing test dependencies |
| `VPHILOX_ENABLE_AVX2` | On | Include the AVX2 backend |
| `VPHILOX_ENABLE_AVX512` | On | Include the AVX-512 backend |
| `VPHILOX_ENABLE_NEON` | On | Include the ARM NEON backend |
| `VPHILOX_FORCE_SCALAR` | Off | Use only the scalar backend |

</details>

At runtime, vphilox chooses a backend supported by the current CPU. Set
`VPHILOX_BACKEND` to `scalar`, `avx2`, `avx512`, or `neon` when you need to pin
one for testing.

## Correctness comes first

The same key and counter must always produce the same bits. That rule applies
across compiler versions, CPU types, and future vphilox releases.

`tests/test_reference_vectors.cpp` checks the scalar engine against the
published Random123 values. `tests/test_kernel_parity.cpp` checks that each SIMD
backend gives the same answer, including requests that do not fill a complete
vector register.

Long statistical runs with PractRand and TestU01 are planned for Phase 4. They
have not been completed yet.

## Repository map

```text
include/vphilox/          Public headers
include/vphilox/detail/   Scalar and SIMD backends, CPU checks, dispatch
tests/                    GoogleTest correctness tests
benchmarks/               Google Benchmark programs
tools/                    Raw output tool for statistical testing
docs/                     Theory, research, plans, and recorded results
```

The technical background lives in the documentation:

- [How Philox and vphilox work](docs/vPhilox%20theory.md)
- [Research and prior work](docs/Research%20on%20vPhilox.md)
- [Development strategy](docs/Vector%20Philox%20Development%20Strategy.md)
- [Current roadmap](ROADMAP.md)

## Versioning

Versions use `YYYY.0M.MICRO`, with the value stored in [`VERSION`](VERSION).
The bit stream for an existing key and counter will not change. Read
[`VERSIONING.md`](VERSIONING.md) for the compatibility rules.

## Contributing

Start with [`CONTRIBUTING.md`](CONTRIBUTING.md). New behavior needs a focused
test. New SIMD code must pass the shared parity checks and should include
benchmark results in both cycles per byte and GB/s.

## Reference

J. K. Salmon, M. A. Moraes, R. O. Dror, and D. E. Shaw. *Parallel Random
Numbers: As Easy as 1, 2, 3.* SC'11.

## License

Use vphilox under either the [MIT license](LICENSE-MIT) or the
[Apache License 2.0](LICENSE-APACHE), at your choice. Contributions are offered
under the same `MIT OR Apache-2.0` terms.
