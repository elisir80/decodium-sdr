// Experimental receive-only DSC bitstream nucleus. GPL-3.0-or-later.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace dsc_rx {
struct Distress {
    std::array<int,15> body{}; // first 15 symbols; distress-compatible view
    std::array<int,27> fullBody{}; // complete decoded fields for supported formats
    int bodyLength = 0;
    int sourceOffset = 1; // first of five MMSI symbols in fullBody
    int eos = 0;
    int repaired = 0;
    bool inverted = false;
};

class Decoder {
public:
    // Input is one hard decision per symbol bit (not IQ or audio).
    std::optional<Distress> push(bool bit);
    void reset();
private:
    std::vector<uint8_t> bits_;
};
}
