// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox - serialize.hpp
//
// Portable text form for an engine's position.
//
// This is the problem the library was built around. `std::mt19937`'s stream
// operator is not portable between standard libraries, for two independent
// reasons: the format flags (the standard has operator<< set ios_base::dec
// itself, so libstdc++ ignores a caller's std::hex while the MSVC STL does
// not, and each then reads the other's digits in the wrong base), and the
// layout (libstdc++ writes 624 state words in raw order plus a position;
// libc++ and the MSVC STL write 624 rotated into canonical order with no
// position). A checkpoint written on Linux does not reliably load on Windows,
// and on macOS it can load *wrongly* without any error at all.
//
// Three rules follow from that, and this file keeps all of them.
//
// 1. Serialize a position, not an implementation. The fields are the key, the
//    block holding the next output, and the word within it -- see engine_state
//    in philox.hpp. Nothing here depends on `refill_blocks`, so a state
//    written before that changed from 8 to 16 still restores correctly.
//
// 2. Never touch the locale. Integer extraction through std::istream consults
//    the global locale's num_get, which can be imbued with a facet that groups
//    digits. Digits are written and parsed by hand below; the only stream
//    operation is on characters.
//
// 3. Refuse anything unrecognised rather than interpret it. The format carries
//    a version tag, and parsing fails on a wrong tag, a wrong field count, a
//    value past 2^32-1, a word offset outside a block, or trailing rubbish. A
//    state that cannot be read is an error the caller can see, not a stream
//    that silently resumes somewhere else.

#ifndef VPHILOX_SERIALIZE_HPP
#define VPHILOX_SERIALIZE_HPP

#include <istream>
#include <ostream>
#include <string>
#include <string_view>

#include "vphilox/philox.hpp"

namespace vphilox {

/// Tag identifying the format below. Present so that a future format is a
/// clean parse failure rather than a misreading of these fields.
inline constexpr std::string_view state_format_tag = "vphilox1";

namespace detail {

/// Decimal digits of `v`, appended by hand. std::to_string would do, but it
/// routes through the locale on some implementations; this cannot.
inline void append_u32(std::string& out, u32 v) {
    char buf[10];
    std::size_t n = 0;
    do {
        buf[n++] = static_cast<char>('0' + (v % 10u));
        v /= 10u;
    } while (v != 0);
    while (n != 0) out.push_back(buf[--n]);
}

/// Parse one decimal field, rejecting anything that is not digits and
/// anything that would not fit in 32 bits. `pos` stops on the first non-digit
/// -- separators are the caller's business, so exactly one space between
/// fields can be required and a trailing one refused.
[[nodiscard]] inline bool parse_u32(std::string_view s, std::size_t& pos, u32& out) {
    if (pos >= s.size() || s[pos] < '0' || s[pos] > '9') return false;

    u64 acc = 0;
    while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
        acc = acc * 10 + static_cast<u64>(s[pos] - '0');
        if (acc > 0xFFFFFFFFull) return false;  // would not round-trip
        ++pos;
    }
    out = static_cast<u32>(acc);
    return true;
}

}  // namespace detail

/// The textual form of `st`: the tag, then seven decimal fields.
[[nodiscard]] inline std::string to_string(const engine_state& st) {
    std::string out;
    out.reserve(state_format_tag.size() + 7 * 11);
    out.append(state_format_tag);
    for (const u32 v : {st.key.v[0], st.key.v[1], st.counter.v[0], st.counter.v[1], st.counter.v[2],
                        st.counter.v[3], st.offset}) {
        out.push_back(' ');
        detail::append_u32(out, v);
    }
    return out;
}

/// Parse a state written by `to_string`. Returns false and leaves `out`
/// untouched if the text is not exactly one well-formed state.
[[nodiscard]] inline bool from_string(std::string_view text, engine_state& out) {
    if (text.size() < state_format_tag.size()) return false;
    if (text.substr(0, state_format_tag.size()) != state_format_tag) return false;

    std::size_t pos = state_format_tag.size();
    if (pos >= text.size() || text[pos] != ' ') return false;
    ++pos;

    // Exactly one space between fields, and nothing at all after the last.
    // Being strict is the point: a format that tolerates stray bytes is one
    // that will eventually accept some other format's output.
    u32 f[7]{};
    for (std::size_t i = 0; i < 7; ++i) {
        if (i != 0) {
            if (pos >= text.size() || text[pos] != ' ') return false;
            ++pos;
        }
        if (!detail::parse_u32(text, pos, f[i])) return false;
    }
    if (pos != text.size()) return false;                     // trailing rubbish
    if (f[6] >= static_cast<u32>(block_words)) return false;  // offset outside a block

    out = engine_state{key2{{f[0], f[1]}}, counter4{{f[2], f[3], f[4], f[5]}}, f[6]};
    return true;
}

/// Write an engine's position. Only characters reach the stream, so the
/// result does not depend on the stream's format flags or its locale.
template <unsigned Rounds>
std::ostream& operator<<(std::ostream& os, const basic_engine<Rounds>& g) {
    return os << to_string(g.state());
}

/// Read a position previously written by `operator<<`. On malformed input the
/// stream's failbit is set and the engine is left exactly as it was.
template <unsigned Rounds>
std::istream& operator>>(std::istream& is, basic_engine<Rounds>& g) {
    std::string token;
    std::string text;

    // Eight whitespace-delimited tokens, joined with single spaces. Reading
    // tokens rather than formatted integers is what keeps num_get, and with it
    // the locale, out of the path.
    for (int i = 0; i < 8; ++i) {
        if (!(is >> token)) return is;
        if (i != 0) text.push_back(' ');
        text.append(token);
    }

    engine_state st;
    if (!from_string(text, st)) {
        is.setstate(std::ios_base::failbit);
        return is;
    }
    g.set_state(st);
    return is;
}

}  // namespace vphilox

#endif  // VPHILOX_SERIALIZE_HPP
