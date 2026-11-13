// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace dsdr::dsp {
// Streaming, already sliced POCSAG bits in transmission order. Not thread safe.
// Callback runs synchronously on the caller's thread; call process from a worker.
class PocsagDecoder {
public:
    struct Message { std::uint32_t address; unsigned function; std::string alpha; unsigned correctedBits; };
    using Callback = void (*)(void *, const Message &);
    explicit PocsagDecoder(Callback cb, void *context = nullptr) : callback(cb), context(context) {}
    void reset();
    void process(const std::uint8_t *bits, std::size_t count);
    // BCH(31,21) plus even parity, correction across the complete 32-bit word.
    static bool correct(std::uint32_t &word, unsigned &changes);
private:
    void bit(bool b);
    void word(std::uint32_t w);
    void finish();
    Callback callback; void *context;
    std::uint32_t shift = 0; unsigned seen = 0, nbits = 0, position = 0;
    bool synced = false, inverted = false, active = false, invalid = false;
    Message message{}; unsigned character = 0, characterBits = 0, payloadBits = 0;
};
}
