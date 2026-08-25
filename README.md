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
  Random numbers you can save, move between machines, and get back exactly.
</p>

vphilox is a small C++20 library for simulations, tests, games, data tools, and
other programs that need a lot of random numbers. Give it the same seed and it
produces the same results on another compiler, another operating system,
another CPU, and whether the work runs on one thread or sixty-four. You can
write a generator's position to a file and pick it up somewhere else, jump to
any point in a stream in constant time, and hand every worker its own stream
without locks. It is also quick: on a machine with AVX-512 it generates numbers
several times faster than `std::mt19937`.

> vphilox has not been tagged yet, so the API may still change. The bit stream is
> frozen regardless: for a given key and counter it produces the same bytes now
> and in every later release. All four backends are implemented and verified on
> hardware, and filling a buffer runs at 1.3x to 4.6x the throughput of
> `std::mt19937` across the five CPUs benchmarked. The low end of that range is a
> Raspberry Pi 5 under NEON; drawing one value at a time on that machine is
> 0.87x, the one place where vphilox comes out behind. Every backend produces the
> same bytes, checked against the published Random123 vectors and pinned by a
> digest test. PractRand runs clean to a terabyte and TestU01 BigCrush passes all
> 160 statistics. Open work is tracked in
> [issues](https://github.com/sinhaparth5/vphilox/issues).

## What it gives you

- A given seed produces repeatable results across runs and supported machines.
- Each thread can own a random stream without locks or shared state.
- `discard()` jumps to a later point in a stream immediately.
- The engine works with standard C++ algorithms and distributions.
- Filling a whole buffer at once runs at full speed, without the per-value cost.
- A generator's position can be saved as text and read back on another
  operating system, compiler or CPU.
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
`random()` a million times, and leaves the generator in the same place, so you
can mix the two styles on one engine. It runs 1.5 to 2.4 times faster than the
one-at-a-time loop, and the faster the machine's kernel, the wider that gap
gets. Ask for a few hundred values or more to get the full benefit.

## Where it stands against other generators

On four of the five CPUs benchmarked, xoshiro256++ costs less per byte than
vphilox does. The exception is Skylake-SP, where the bulk path comes in at
0.4210 cycles per byte against xoshiro's 0.4703. xoshiro is a latency-bound
scalar chain that a wider kernel does not catch, and it offers none of what this
library is for: a checkpoint that survives a change of standard library,
constant-time seek, and output that does not depend on thread count. Speed here
only has to be good enough that portability costs nothing. The full table,
including PCG64 and unspecialised Philox, is in
[`docs/benchmarks/README.md`](docs/benchmarks/README.md).

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

Size the pool by physical cores rather than by `hardware_concurrency()`. Cost per
byte stays flat out to sixteen cores when each worker gets its own core, but two
workers sharing one core's hyperthread siblings each lose about a third of their
throughput. The kernel is execution-port-bound, so the siblings are competing for
the same ports; measurements ruling out memory bandwidth, frequency, and
instruction supply as the cause are in
[`docs/benchmarks/`](docs/benchmarks/README.md).

## Saving and restoring a generator

A long job that checkpoints needs to write its generator down and pick it up
later, sometimes on a different machine. That is the one thing `std::mt19937`
cannot do: each standard library writes its 624 internal numbers differently, so
a checkpoint written on Linux fails to load on Windows, and on macOS it loads
silently wrong.

vphilox writes a position instead, meaning a key and how far along the stream
you are:

```cpp
#include <vphilox/serialize.hpp>

vphilox::engine random{42};
for (int i = 0; i < 1000; ++i) (void)random();

std::ostringstream out;
out << random;                  // "vphilox1 42 0 250 0 0 0 0"

vphilox::engine restored;
std::istringstream in{out.str()};
in >> restored;                 // continues exactly where `random` was
```

Seven plain integers. No floating point, no layout that depends on your
standard library, and no digits that a locale can regroup. If the text is not
recognised the read fails and the engine is left alone, rather than resuming
somewhere unintended.

## How it works

Most random-number engines keep changing an internal state. vphilox instead
treats a key and a counter like coordinates: the same pair always gives the
same output. That makes it easy to split a large job into independent pieces or
resume at a known position.

Philox calculations are also independent of one another, so vphilox processes
several at once with the vector instructions on modern CPUs: eight counters at a
time under AVX2, sixteen under AVX-512, and eight under NEON as two groups of
four. The plain scalar path is the reference that every faster path must match
bit for bit, and a test checks that it does on every CPU the library runs on.

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
vector register. `tests/test_cross_platform_parity.cpp` folds 8191 blocks into a
digest and compares it with a constant, which covers what the published vectors
cannot: SIMD tails, the refill buffer, chunked bulk calls, the counter carry
chain, and float conversion.

The stream has also been through the two standard batteries.
[PractRand](docs/statistical-validation.md) reported no anomalies over a
terabyte of output, and all four backends produced that terabyte byte for byte.
TestU01 BigCrush passed all 160 statistics. A third check compares the stream
against NVIDIA cuRAND and writes down the seeding mapping between the two
libraries, in [`docs/curand-parity.md`](docs/curand-parity.md).

## Repository map

```text
include/vphilox/          Public headers
include/vphilox/detail/   Scalar and SIMD backends, CPU checks, dispatch
tests/                    GoogleTest correctness tests
benchmarks/               Google Benchmark programs
tools/                    Raw stream, TestU01 driver, cuRAND cross-check
scripts/                  Benchmark harness and statistical battery runners
results/                  Archived benchmark JSON and battery logs
docs/                     Theory, measured results, and figures
paper/                    The LaTeX write-up and its figures
```

The technical background lives in the documentation:

- [How Philox and vphilox work](docs/vPhilox%20theory.md)
- [Measured results, figures, and the rules they follow](docs/benchmarks/README.md)
- [Statistical validation](docs/statistical-validation.md)

Benchmarks are run through `scripts/benchmarks/run_matrix.sh` rather than by
hand. It pins the governor, records provenance beside the numbers, and refuses a
run whose cycles-per-byte varies by more than 1%.

## Versioning

Versions use `YYYY.0M.MICRO`, with the value stored in [`VERSION`](VERSION).
The bit stream for an existing key and counter will not change. Read
[`VERSIONING.md`](VERSIONING.md) for the compatibility rules.

## Contributing

Start with [`CONTRIBUTING.md`](CONTRIBUTING.md). New behavior needs a focused
test. New SIMD code must pass the shared parity checks and should include
benchmark results in both cycles per byte and GB/s.

## Citing vphilox

[`CITATION.cff`](CITATION.cff) holds the machine-readable record; GitHub turns it
into BibTeX or APA from the "Cite this repository" button on the sidebar.

The generator itself is not ours. It comes from J. K. Salmon, M. A. Moraes,
R. O. Dror, and D. E. Shaw, *Parallel Random Numbers: As Easy as 1, 2, 3*, SC'11,
[doi:10.1145/2063384.2063405](https://doi.org/10.1145/2063384.2063405). Cite that
paper for Philox and cite vphilox for this implementation.

The write-up of the design and the measurements is in
[`paper/`](paper/README.md).

## License

Use vphilox under either the [MIT license](LICENSE-MIT) or the
[Apache License 2.0](LICENSE-APACHE), at your choice. Contributions are offered
under the same `MIT OR Apache-2.0` terms.
