// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace dsdr::vdl2 {
// Number of scrambled bits needed after training, including the 25-bit header.
// A streaming demodulator can use this after collecting just the header.
struct BurstHeader {
    std::uint32_t dataBits = 0;
    std::size_t totalBits = 0;
    bool corrected = false;
};
std::optional<BurstHeader> inspectBurstHeader(const std::vector<std::uint8_t>& bits);
// Input: hard-decision bits after the training sequence, still scrambled.
// No IQ demodulation is performed here. Invalid headers/FEC are rejected.
struct Burst {
    bool headerCorrected = false;
    int correctedOctets = 0;
    std::vector<std::vector<std::uint8_t>> frames; // includes each frame's FCS
};
std::optional<Burst> decodeBurst(const std::vector<std::uint8_t>& bits);
}
