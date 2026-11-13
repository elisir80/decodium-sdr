// SPDX-License-Identifier: GPL-3.0-or-later
// Based on OnAir by Kristijonas Adomaitis - https://on-air.dev
#include "Vdl2Symbols.h"
#include <cmath>
namespace dsdr::vdl2 {
std::optional<std::vector<std::uint8_t>> decodeSymbols(const std::complex<float>* samples,std::size_t count) {
    constexpr double pi=3.14159265358979323846;
    constexpr std::uint8_t gray[8]={0,1,3,2,6,7,5,4};
    constexpr std::size_t maxSymbols=90000;
    if(!samples || count<2 || count>maxSymbols) return {};
    std::vector<std::uint8_t> bits;
    bits.reserve((count-1)*3);
    for(std::size_t i=0;i<count;++i)
        if(!std::isfinite(samples[i].real()) || !std::isfinite(samples[i].imag()) ||
           std::norm(samples[i])<1e-20f) return {};
    for(std::size_t i=1;i<count;++i) {
        const auto delta=samples[i]*std::conj(samples[i-1]);
        int step=static_cast<int>(std::lround(std::arg(delta)/(pi/4)));
        step=(step%8+8)%8;
        const auto code=gray[step];
        bits.push_back((code>>2)&1); bits.push_back((code>>1)&1); bits.push_back(code&1);
    }
    return bits;
}
}
