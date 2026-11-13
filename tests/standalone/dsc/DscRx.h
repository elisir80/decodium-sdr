// Experimental receive-only DSC distress bitstream nucleus. GPL-3.0-or-later.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace dsc_rx {
struct Distress {
    std::array<int,15> body{}; // format, MMSI, nature, position, time, subsequent
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
