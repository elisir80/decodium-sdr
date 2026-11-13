// SPDX-License-Identifier: GPL-3.0-or-later
#include "PocsagDecoder.h"

namespace dsdr::dsp {
namespace {
constexpr std::uint32_t sync = 0x7cd215d8, idle = 0x7a89c197;
bool valid(std::uint32_t w) {
    std::uint32_t v = w >> 1;
    for (int i = 30; i >= 10; --i)
        if (v & (std::uint32_t(1) << i)) v ^= std::uint32_t(0x769) << (i - 10);
    unsigned parity = 0;
    for (auto x = w; x; x >>= 1) parity ^= x & 1u;
    return v == 0 && parity == 0;
}
}
bool PocsagDecoder::correct(std::uint32_t &w, unsigned &changes) {
    changes = 0;
    if (valid(w)) return true;
    for (unsigned i = 0; i < 32; ++i) {
        auto x = w ^ (std::uint32_t(1) << i);
        if (valid(x)) { w = x; changes = 1; return true; }
    }
    for (unsigned i = 0; i < 32; ++i) for (unsigned j = i + 1; j < 32; ++j) {
        auto x = w ^ (std::uint32_t(1) << i) ^ (std::uint32_t(1) << j);
        if (valid(x)) { w = x; changes = 2; return true; }
    }
    return false;
}
void PocsagDecoder::reset() {
    shift = 0; seen = nbits = position = 0; synced = inverted = active = invalid = false;
    message = {}; character = characterBits = payloadBits = 0;
}
void PocsagDecoder::process(const std::uint8_t *bits, std::size_t count) {
    if (!bits) return;
    for (std::size_t i = 0; i < count; ++i) bit(bits[i] != 0);
}
void PocsagDecoder::bit(bool b) {
    if (!synced) {
        shift = (shift << 1) | unsigned(b);
        if (seen < 32) ++seen;
        if (seen == 32 && (shift == sync || shift == ~sync)) {
            inverted = shift == ~sync; synced = true; nbits = position = 0; shift = 0;
        }
        return;
    }
    shift = (shift << 1) | unsigned(b != inverted);
    if (++nbits != 32) return;
    auto w = shift; shift = 0; nbits = 0;
    if (position == 16) {
        if (w != sync) { active = false; synced = false; seen = 0; return; }
        position = 0; return;
    }
    word(w); ++position;
}
void PocsagDecoder::finish() {
    if (active && !invalid && payloadBits && callback) callback(context, message);
    active = invalid = false; character = characterBits = payloadBits = 0;
}
void PocsagDecoder::word(std::uint32_t w) {
    unsigned changes = 0;
    if (!correct(w, changes)) { if (active) invalid = true; return; }
    if (w == idle) { finish(); return; }
    if (!(w & 0x80000000u)) {
        finish(); active = true; message = {};
        message.address = ((w >> 13) & 0x3ffffu) * 8u + position / 2;
        message.function = (w >> 11) & 3u; message.correctedBits = changes;
        return;
    }
    if (!active || invalid) return;
    message.correctedBits += changes;
    // Refuse truncated/overlong output, rather than deliver a fabricated page.
    if (payloadBits + 20 > 7 * 1024) { invalid = true; return; }
    for (int i = 30; i >= 11; --i) {
        character |= ((w >> i) & 1u) << characterBits;
        ++characterBits; ++payloadBits;
        if (characterBits == 7) {
            message.alpha.push_back(static_cast<char>(character));
            character = characterBits = 0;
        }
    }
}
}
