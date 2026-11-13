// Experimental receive-only DSC distress bitstream nucleus. GPL-3.0-or-later.
#include "DscRx.h"
#include <algorithm>

namespace dsc_rx {
namespace {
constexpr int slots = 52;
int symbol(const std::vector<uint8_t>& b, int slot, bool invert) {
    int value = 0, ones = 0, check = 0;
    for (int i = 0; i < 7; ++i) {
        const int v = b[slot * 10 + i] ^ invert;
        value |= v << i;
        ones += v;
    }
    for (int i = 7; i < 10; ++i) check = (check << 1) | (b[slot * 10 + i] ^ invert);
    return check == 7 - ones ? value : -1;
}
std::optional<Distress> decode(const std::vector<uint8_t>& b, bool inv) {
    // Strict phasing is deliberately conservative; no partial phasing recovery.
    for (int i = 0; i < 12; ++i) {
        const int expected = i % 2 ? 111 - i / 2 : 125;
        if (symbol(b, i, inv) != expected) return {};
    }
    int values[18]; int repaired = 0;
    for (int i = 0; i < 18; ++i) {
        const int dx = symbol(b, 12 + 2*i, inv);
        const int rx = symbol(b, 17 + 2*i, inv);
        if (dx < 0 && rx < 0) return {};
        if (dx >= 0 && rx >= 0 && dx != rx) return {};
        if (dx < 0 || rx < 0) ++repaired;
        values[i] = dx >= 0 ? dx : rx;
    }
    if (values[0] != 112 || values[1] != 112 || values[16] != 127) return {};
    int parity = values[16] ^ values[0];
    for (int i = 2; i < 16; ++i) parity ^= values[i];
    if (parity != values[17]) return {};
    Distress out; out.body[0] = 112;
    for (int i = 1; i < 15; ++i) out.body[i] = values[i+1];
    for (int i = 1; i <= 5; ++i) if (out.body[i] > 99) return {};
    if (out.body[5] % 10 != 0) return {};
    out.eos = values[16]; out.repaired = repaired; out.inverted = inv;
    return out;
}
}
void Decoder::reset() { bits_.clear(); }
std::optional<Distress> Decoder::push(bool bit) {
    bits_.push_back(bit);
    if (bits_.size() < slots * 10) return {};
    auto result = decode(bits_, false);
    if (!result) result = decode(bits_, true);
    if (result) bits_.clear();
    else bits_.erase(bits_.begin());
    return result;
}
}
