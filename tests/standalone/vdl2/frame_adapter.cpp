// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/vdl2/Vdl2Frame.h"
#include "dsp/vdl2/OnAirVdl2Abi.h"
#include <array>
#include <cstdio>
int main() {
    if(!dsdr::vdl2::compatible()) return 1;
    std::array<uint8_t,11> frame{};
    OnairVdl2Addr dst{0x10a000,4,0},src{0x4ca2b1,1,0};
    onair_vdl2_put_addr(&dst,1,frame.data()); onair_vdl2_put_addr(&src,1,frame.data()+4);
    frame[8]=0x01; // supervisory frame, no payload
    const auto crc=onair_vdl2_fcs(frame.data(),9); frame[9]=uint8_t(crc); frame[10]=uint8_t(crc>>8);
    const auto good=dsdr::vdl2::parse(frame.data(),frame.size());
    if(!good || good->source!=src.addr || good->destination!=dst.addr || good->sourceType!=1 || good->destinationType!=4) return 2;
    frame[4]^=0x40;
    if(dsdr::vdl2::parse(frame.data(),frame.size()) || dsdr::vdl2::parse(nullptr,11) ||
       dsdr::vdl2::parse(frame.data(),10) || dsdr::vdl2::parse(frame.data(),4097)) return 3;
    std::printf("valid AVLC decoded; corrupted, truncated and invalid inputs rejected\n");
    return 0;
}
