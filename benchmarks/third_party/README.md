# Vendored benchmark generators

Issue #48. The throughput matrix compares vphilox against the generators
people actually reach for, and a comparison is only worth publishing if the
things being compared are provably the real algorithms. These are vendored
rather than fetched so that a benchmark run five years from now measures the
same code, and so the repository stays buildable offline.

Nothing here is part of the vphilox library. It is compiled only by the
benchmarks and by `tests/test_third_party_generators.cpp`, never installed,
and never included from `include/vphilox/`.

## Provenance

Fetched 2026-08-24.

| File | Upstream | SHA-256 |
|---|---|---|
| `xoshiro/xoshiro256plusplus.c` | https://prng.di.unimi.it/xoshiro256plusplus.c | `1346a5d2066ec20077b10b3064496b60a2dbb1f7850aa16bb6afe411213bd522` |
| `xoshiro/f2x.c` | https://prng.di.unimi.it/f2x.c | `62e9b22bd882c6dade29a57cabcd079abf83522218415e3e9a95a4e6086dac99` |
| `xoshiro/splitmix64.c` | https://prng.di.unimi.it/splitmix64.c | `071795a8e29978a5cbd7015ce8f7d772e7ab4631e574e9102b748fe99105ff3d` |
| `pcg-cpp/pcg_random.hpp` | https://github.com/imneme/pcg-cpp `include/pcg_random.hpp` | `ea71df69343b36d212b76abc6a3ba21d77651893e87558d633f4854c37804ca3` |
| `pcg-cpp/pcg_extras.hpp` | https://github.com/imneme/pcg-cpp `include/pcg_extras.hpp` | `b2100463f9dfa6db46ea112235cecf2e206900a96c33dbf44ed41d8f26e090a2` |
| `pcg-cpp/pcg_uint128.hpp` | https://github.com/imneme/pcg-cpp `include/pcg_uint128.hpp` | `b62ad9e955836699c90cd78a02187edfa6538d6e775d1b3a91102fd7f00e30a1` |

Every file above is **byte-for-byte upstream**. They are deliberately exempt
from `.clang-format` and from the SPDX header convention: reformatting
vendored code destroys the only thing that makes the hashes above meaningful.

## Licences

Both are compatible with vphilox's own MIT OR Apache-2.0, per the rule in
`CONTRIBUTING.md` that MIT / BSD / Apache-2.0 sources relocate with
attribution and copyleft does not.

**xoshiro256++, splitmix64 and f2x** — David Blackman and Sebastiano Vigna.
Public domain (CC0); the dedication is carried in the header comment of each
file, which is why there is no separate licence file for them. `f2x.c` is not
optional decoration: upstream's current `xoshiro256plusplus.c` includes it for
the arbitrary-jump polynomial, so the file does not compile without it. The
benchmarks never call `jump()`, but the vendored copy stays whole rather than
trimmed to the parts we use — a trimmed file could not be checked against the
hash above.

**PCG** — Melissa O'Neill, 2014-2022. `Apache-2.0 OR MIT`, the same dual
licence vphilox uses. Upstream's own licence texts are alongside the headers
as `LICENSE-APACHE.txt` and `LICENSE-MIT.txt`.

## xoshiro256plusplus.hpp

Upstream xoshiro is a pair of C files holding state in file-scope statics and
exposing `next()`. That is unusable for the matrix, which wants several
generators alive at once, and for the scaling benchmark (#50-#52), which wants
one per thread. So `xoshiro256plusplus.hpp` is ours: the state moves into a
class and the arithmetic is line-for-line upstream.

That transliteration is the one place a mistake could quietly bias the
comparison in vphilox's favour, so it is not taken on trust.
`tests/test_third_party_generators.cpp` includes the vendored `.c` files
directly, runs both, and requires bit-for-bit agreement. If someone edits the
class, the test fails against the real thing rather than against a copy of the
same mistake.

PCG needs no such wrapper: `pcg64` is already an instantiable C++ class, so the
benchmarks use it directly and the test pins it to fixed outputs.
