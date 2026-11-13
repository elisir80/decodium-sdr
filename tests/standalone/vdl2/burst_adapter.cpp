// SPDX-License-Identifier: GPL-3.0-or-later
// Deterministic synthetic fixture produced by OnAir's vdl2BurstBits and
// vdl2HdlcBits; it contains no real station information.
#include "dsp/vdl2/Vdl2Burst.h"
#include <cstdint>
#include <vector>

int main() {
    const char encoded[]="0001000111011011110000100001101000011111101011010011010111101111110010000111001111001110010000011010111000110111111111101100010000100001100001100";
    std::vector<std::uint8_t> bits;
    for(const char* p=encoded;*p;++p) bits.push_back(std::uint8_t(*p-'0'));
    const std::vector<std::uint8_t> expected{0x04,0x42,0x02,0x00,0x50,0x4c,0xa2,0x8d,0x01,0x94,0xb4};
    std::vector<std::uint8_t> prefix(bits.begin(),bits.begin()+25);
    auto header=dsdr::vdl2::inspectBurstHeader(prefix);
    if(!header || header->totalBits!=bits.size() || header->corrected) return 6;
    auto burst=dsdr::vdl2::decodeBurst(bits);
    if(!burst || burst->frames.size()!=1 || burst->frames[0]!=expected || burst->headerCorrected) return 1;
    bits[4]^=1;
    prefix[4]^=1;
    header=dsdr::vdl2::inspectBurstHeader(prefix);
    if(!header || !header->corrected || header->totalBits!=bits.size()) return 7;
    burst=dsdr::vdl2::decodeBurst(bits);
    if(!burst || !burst->headerCorrected || burst->frames.size()!=1 || burst->frames[0]!=expected) return 2;
    bits.resize(24);
    if(dsdr::vdl2::decodeBurst(bits) || dsdr::vdl2::inspectBurstHeader(bits)) return 3;
    bits.assign(25,2);
    if(dsdr::vdl2::decodeBurst(bits)) return 4;
    bits.assign(262145,0);
    if(dsdr::vdl2::decodeBurst(bits)) return 5;
    return 0;
}
