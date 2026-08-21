// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox_stream - raw random bytes on stdout, for statistical test batteries.
//
// Phase 4 usage:
//   ./vphilox_stream | RNG_test stdin32 -tlmax 1TB -multithreaded -tf 1
//   ./vphilox_stream --bytes 4G > sample.bin
//
// PractRand reads 32-bit words from stdin in native byte order, which is what
// --format=raw32 (the default) writes. Note that a 1 TB run takes many hours;
// start with -tlmax 1GB to confirm the plumbing before committing a machine
// to the full sweep.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "vphilox/vphilox.hpp"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace {

void usage(const char* argv0) {
    std::fprintf(stderr,
                 "vphilox_stream (vphilox %s)\n"
                 "\n"
                 "Writes raw pseudorandom bytes to stdout.\n"
                 "\n"
                 "Usage: %s [options]\n"
                 "  --seed N        64-bit seed (default 0)\n"
                 "  --bytes N       stop after N bytes; accepts K/M/G/T suffixes\n"
                 "                  (default: unlimited -- let the consumer stop us)\n"
                 "  --format FMT    raw32 (default) or float32\n"
                 "  --backend B     force scalar|avx2|avx512|neon (same as VPHILOX_BACKEND)\n"
                 "  --info          print the resolved backend to stderr and exit\n"
                 "  --help\n",
                 vphilox::version_string, argv0);
}

/// Parse a byte count with an optional K/M/G/T suffix (powers of 1024).
bool parse_size(const char* s, std::uint64_t& out) {
    char* end                  = nullptr;
    const unsigned long long n = std::strtoull(s, &end, 10);
    if (end == s) return false;

    std::uint64_t mult = 1;
    switch (*end) {
        case '\0':
            break;
        case 'k':
        case 'K':
            mult = 1ull << 10;
            ++end;
            break;
        case 'm':
        case 'M':
            mult = 1ull << 20;
            ++end;
            break;
        case 'g':
        case 'G':
            mult = 1ull << 30;
            ++end;
            break;
        case 't':
        case 'T':
            mult = 1ull << 40;
            ++end;
            break;
        default:
            return false;
    }
    if (*end == 'B' || *end == 'b') ++end;
    if (*end != '\0') return false;

    out = static_cast<std::uint64_t>(n) * mult;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::uint64_t seed  = 0;
    std::uint64_t limit = 0;  // 0 == unlimited
    std::string format  = "raw32";
    bool info_only      = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next             = [&](const char* what) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "vphilox_stream: %s needs a value\n", what);
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        } else if (arg == "--info") {
            info_only = true;
        } else if (arg == "--seed") {
            seed = std::strtoull(next("--seed"), nullptr, 0);
        } else if (arg == "--format") {
            format = next("--format");
        } else if (arg == "--backend") {
            // Routed through the same env var the library reads, so there is
            // one override mechanism rather than two.
#if defined(_WIN32)
            _putenv_s("VPHILOX_BACKEND", next("--backend"));
#else
            setenv("VPHILOX_BACKEND", next("--backend"), 1);
#endif
        } else if (arg == "--bytes") {
            if (!parse_size(next("--bytes"), limit)) {
                std::fprintf(stderr, "vphilox_stream: bad --bytes value\n");
                return 2;
            }
        } else {
            std::fprintf(stderr, "vphilox_stream: unknown option '%s'\n", arg.c_str());
            usage(argv[0]);
            return 2;
        }
    }

    if (format != "raw32" && format != "float32") {
        std::fprintf(stderr, "vphilox_stream: --format must be raw32 or float32\n");
        return 2;
    }

    vphilox::engine g{seed};

    std::fprintf(stderr, "vphilox %s | backend=%s | seed=%llu | format=%s\n",
                 vphilox::version_string, vphilox::backend_name(vphilox::engine::which_backend()),
                 static_cast<unsigned long long>(seed), format.c_str());
    if (info_only) return 0;

#if defined(_WIN32)
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    // 1 MiB writes: large enough that write() overhead disappears, small
    // enough to stay in cache.
    constexpr std::size_t kChunkWords = (1u << 20) / sizeof(vphilox::u32);
    std::vector<vphilox::u32> chunk(kChunkWords);
    std::vector<float> fchunk(format == "float32" ? kChunkWords : 0);

    std::uint64_t written = 0;
    for (;;) {
        std::size_t words = kChunkWords;
        if (limit != 0) {
            const std::uint64_t remaining = limit - written;
            if (remaining == 0) break;
            const std::uint64_t remaining_words = remaining / sizeof(vphilox::u32);
            if (remaining_words == 0) break;
            if (remaining_words < words) words = static_cast<std::size_t>(remaining_words);
        }

        const void* data = nullptr;
        if (format == "raw32") {
            for (std::size_t i = 0; i < words; ++i) chunk[i] = g();
            data = chunk.data();
        } else {
            for (std::size_t i = 0; i < words; ++i) fchunk[i] = g.next_float();
            data = fchunk.data();
        }

        const std::size_t bytes = words * sizeof(vphilox::u32);
        if (std::fwrite(data, 1, bytes, stdout) != bytes) {
            // Normal termination: the consumer (PractRand) closed the pipe.
            return 0;
        }
        written += bytes;
    }

    std::fflush(stdout);
    return 0;
}
