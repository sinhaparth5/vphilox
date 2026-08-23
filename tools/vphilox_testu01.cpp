// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox_testu01 - run the TestU01 batteries against vphilox.
//
// Unlike PractRand, TestU01 has no stdin mode: it pulls from a callback rather
// than reading a stream, so it needs a harness compiled against the library
// instead of a pipe. That is also why this is the honest way to test a
// counter-based generator here -- the battery drives the engine directly, so
// what gets tested is the engine a caller would actually use, refill buffer
// and dispatch included.
//
// BigCrush consumes roughly 2^38 bits and takes several hours. Start with
// --battery small to confirm the plumbing.
//
// Built only when TestU01 is found; see scripts/statistical/build_testu01.sh.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "vphilox/vphilox.hpp"

extern "C" {
#include "bbattery.h"
#include "unif01.h"
}

namespace {

/// TestU01's extern-generator hook is a plain function pointer with no user
/// data, so the engine has to be reachable without one.
vphilox::engine* g_engine = nullptr;

unsigned int next_bits() {
    return (*g_engine)();
}

void usage(const char* argv0) {
    std::fprintf(stderr,
                 "vphilox_testu01 (vphilox %s)\n"
                 "\n"
                 "Usage: %s [options]\n"
                 "  --battery B   small | crush | big   (default: small)\n"
                 "  --seed N      64-bit seed (default 0)\n"
                 "  --backend B   force scalar|avx2|avx512|neon\n"
                 "  --help\n",
                 vphilox::version_string, argv0);
}

}  // namespace

int main(int argc, char** argv) {
    std::string battery = "small";
    std::uint64_t seed  = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next             = [&](const char* what) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "vphilox_testu01: %s needs a value\n", what);
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        } else if (arg == "--battery") {
            battery = next("--battery");
        } else if (arg == "--seed") {
            seed = std::strtoull(next("--seed"), nullptr, 0);
        } else if (arg == "--backend") {
            // Same single override mechanism the library and vphilox_stream use.
            setenv("VPHILOX_BACKEND", next("--backend"), 1);
        } else {
            std::fprintf(stderr, "vphilox_testu01: unknown option '%s'\n", arg.c_str());
            usage(argv[0]);
            return 2;
        }
    }

    if (battery != "small" && battery != "crush" && battery != "big") {
        std::fprintf(stderr, "vphilox_testu01: --battery must be small, crush, or big\n");
        return 2;
    }

    vphilox::engine engine{seed};
    g_engine = &engine;

    char name[128];
    std::snprintf(name, sizeof(name), "vphilox %s philox4x32-%u backend=%s seed=%llu",
                  vphilox::version_string, vphilox::engine::rounds,
                  vphilox::backend_name(vphilox::engine::which_backend()),
                  static_cast<unsigned long long>(seed));

    std::fprintf(stderr, "%s | battery=%s\n", name, battery.c_str());
    std::fflush(stderr);

    unif01_Gen* gen = unif01_CreateExternGenBits(name, next_bits);

    if (battery == "small") {
        bbattery_SmallCrush(gen);
    } else if (battery == "crush") {
        bbattery_Crush(gen);
    } else {
        bbattery_BigCrush(gen);
    }

    unif01_DeleteExternGenBits(gen);
    return 0;
}
