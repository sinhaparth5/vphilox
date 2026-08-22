// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox - detail/dispatch.hpp
//
// Picks the fastest kernel this binary was compiled with AND this CPU can
// actually run. Resolved once, at first use, into a function pointer.
//
// A kernel is eligible only if all three hold:
//   1. compiled in        -- VPHILOX_HAS_* (CMake option / target arch)
//   2. implemented        -- kernel::implemented (false while stubbed)
//   3. supported by CPU   -- detect_cpu()
//
// Rule 2 is what lets the Phase-2 stubs sit in the tree without ever being
// selected: a stub falls back to scalar internally anyway, but keeping it out
// of dispatch means `active_backend()` never lies about what ran.

#ifndef VPHILOX_DETAIL_DISPATCH_HPP
#define VPHILOX_DETAIL_DISPATCH_HPP

#include <cstdlib>
#include <cstring>

#include "vphilox/config.hpp"
#include "vphilox/detail/cpu_features.hpp"
#include "vphilox/detail/kernel_avx2.hpp"
#include "vphilox/detail/kernel_avx512.hpp"
#include "vphilox/detail/kernel_neon.hpp"
#include "vphilox/detail/kernel_scalar.hpp"

namespace vphilox {

enum class backend { scalar, avx2, avx512, neon };

[[nodiscard]] constexpr const char* backend_name(backend b) noexcept {
    switch (b) {
        case backend::scalar:
            return "scalar";
        case backend::avx2:
            return "avx2";
        case backend::avx512:
            return "avx512";
        case backend::neon:
            return "neon";
    }
    return "unknown";
}

namespace detail {

using kernel_fn = void (*)(const counter4&, const key2&, u32*, std::size_t) noexcept;

struct dispatch_entry {
    backend which                = backend::scalar;
    kernel_fn fn                 = nullptr;
    std::size_t preferred_blocks = 1;
};

/// Optional override, read once from the VPHILOX_BACKEND env var. Lets tests
/// and benchmarks pin a backend without rebuilding. An unset or unrecognised
/// value means "choose automatically"; a value naming an unavailable backend
/// is ignored rather than fatal.
inline backend backend_override(bool& has_override) noexcept {
    const char* env = std::getenv("VPHILOX_BACKEND");
    has_override    = false;
    if (env == nullptr) return backend::scalar;

    struct {
        const char* name;
        backend b;
    } table[] = {
        {"scalar", backend::scalar},
        {"avx2", backend::avx2},
        {"avx512", backend::avx512},
        {"neon", backend::neon},
    };
    for (const auto& e : table) {
        if (std::strcmp(env, e.name) == 0) {
            has_override = true;
            return e.b;
        }
    }
    return backend::scalar;
}

template <unsigned Rounds>
inline const dispatch_entry& resolve_dispatch() noexcept {
    static const dispatch_entry entry = [] {
        const cpu_features& cpu = detect_cpu();

        auto make = [](backend b) -> dispatch_entry {
            switch (b) {
#if VPHILOX_HAS_AVX512
                case backend::avx512:
                    return {b, &kernel_avx512::generate<Rounds>, kernel_avx512::preferred_blocks};
#endif
#if VPHILOX_HAS_AVX2
                case backend::avx2:
                    return {b, &kernel_avx2::generate<Rounds>, kernel_avx2::preferred_blocks};
#endif
#if VPHILOX_HAS_NEON
                case backend::neon:
                    return {b, &kernel_neon::generate<Rounds>, kernel_neon::preferred_blocks};
#endif
                default:
                    return {backend::scalar, &kernel_scalar::generate<Rounds>,
                            kernel_scalar::preferred_blocks};
            }
        };

        auto available = [&cpu](backend b) {
            switch (b) {
                case backend::avx512:
                    return VPHILOX_HAS_AVX512 && kernel_avx512::implemented && cpu.avx512;
                case backend::avx2:
                    return VPHILOX_HAS_AVX2 && kernel_avx2::implemented && cpu.avx2;
                case backend::neon:
                    return VPHILOX_HAS_NEON && kernel_neon::implemented && cpu.neon;
                case backend::scalar:
                    return true;
            }
            return false;
        };

        bool has_override    = false;
        const backend forced = backend_override(has_override);
        if (has_override && available(forced)) return make(forced);

        for (backend b : {backend::avx512, backend::avx2, backend::neon}) {
            if (available(b)) return make(b);
        }
        return make(backend::scalar);
    }();
    return entry;
}

}  // namespace detail

/// Which kernel this process resolved to. Stable for the process lifetime.
template <unsigned Rounds = default_rounds>
[[nodiscard]] inline backend active_backend() noexcept {
    return detail::resolve_dispatch<Rounds>().which;
}

}  // namespace vphilox

#endif  // VPHILOX_DETAIL_DISPATCH_HPP
