// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace dect2 {
constexpr std::uint8_t kAcarsSyn=0x16, kAcarsSoh=0x01, kAcarsDel=0x7f;
constexpr std::uint8_t kAcarsEtx=0x83, kAcarsEtb=0x97;
inline bool acarsParityOk(std::uint8_t c) {
    unsigned n=0; for (; c; c>>=1) n+=c&1u; return (n&1u)!=0;
}
// Strict validation only: this intentionally does not attempt error repair.
inline bool acarsRepairBlock(std::uint8_t *data, int len, const std::uint8_t crc[2],
                             int &fixed, int &parityErrors) {
    fixed=0; parityErrors=0;
    if (!data || len < 13 || len > 234) return false;
    std::uint16_t c=0;
    auto update=[&](std::uint8_t byte) {
        c ^= byte;
        for (int j=0;j<8;++j) c = (c&1u) ? (c>>1)^0x8408u : c>>1;
    };
    for (int i=0;i<len;++i) { if (!acarsParityOk(data[i])) ++parityErrors; update(data[i]); }
    update(crc[0]); update(crc[1]);
    if (parityErrors || c || (data[len-1]!=kAcarsEtx && data[len-1]!=kAcarsEtb)) return false;
    auto printable=[](std::uint8_t v) { v &= 0x7f; return v>=0x20 && v<=0x7e; };
    for (int i=0;i<8;++i) if (!printable(data[i])) return false; // mode and address
    if (!printable(data[9]) || (!printable(data[10]) && (data[10]&0x7f)!=0x7f) || !printable(data[11])) return false;
    if (len>13 && (data[12]&0x7f)!=2) return false; // optional STX and text
    return true;
}
}
