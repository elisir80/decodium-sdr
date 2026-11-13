// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <random>
#include <vector>
namespace ais_test {
// Independent MSB-first CRC-CCITT formulation with reflected inputs/outputs.
inline std::uint8_t reverse8(std::uint8_t v) {
    std::uint8_t r=0; for(int i=0;i<8;++i) { r=std::uint8_t((r<<1)|(v&1)); v>>=1; } return r;
}
inline std::uint16_t fcs(const std::vector<std::uint8_t> &v) {
    std::uint16_t c=0xffff;
    for(auto b:v) { c^=std::uint16_t(reverse8(b))<<8; for(int i=0;i<8;++i) c=std::uint16_t((c<<1)^((c&0x8000)?0x1021:0)); }
    std::uint16_t r=0; for(int i=0;i<16;++i) { r=std::uint16_t((r<<1)|(c&1)); c>>=1; } return std::uint16_t(r^0xffff);
}
inline void setBits(std::vector<std::uint8_t> &p,unsigned start,unsigned count,std::uint32_t value) {
    for(unsigned i=0;i<count;++i) {
        const auto mask=std::uint8_t(0x80>>((start+i)%8));
        auto &byte=p[(start+i)/8];
        if((value>>(count-1-i))&1) byte|=mask;
        else byte&=std::uint8_t(~mask);
    }
}
inline std::vector<std::uint8_t> position(std::uint32_t mmsi=123456789) {
    std::vector<std::uint8_t> p(21,0); setBits(p,0,6,1); setBits(p,8,30,mmsi);
    setBits(p,61,28,8700000); setBits(p,89,27,21540000); return p;
}
// Independent HDLC/NRZI + Gaussian-smoothed CPFSK waveform, not a decoder.
inline std::vector<std::complex<float>> transmit(const std::vector<std::uint8_t> &payload,
      int rate, double offsetHz, double noise, int delay, bool corrupt, bool conjugate=false) {
    constexpr double pi=3.14159265358979323846;
    std::vector<int> data;
    auto byte=[&](int v){ for(int b=0;b<8;++b) data.push_back((v>>b)&1); };
    for(int i=0;i<24;++i)data.push_back(i&1);
    byte(0x7e);
    auto bytes=payload; auto crc=fcs(payload); if(corrupt)crc^=1;
    bytes.push_back(crc&255); bytes.push_back(crc>>8);
    int ones=0;
    for(auto v:bytes) for(int bit=0;bit<8;++bit) {
        int b=(v>>bit)&1; data.push_back(b);
        if(b && ++ones==5) { data.push_back(0); ones=0; } else if(!b) ones=0;
    }
    byte(0x7e); for(int i=0;i<20;++i)data.push_back(1);
    std::vector<double> levels; bool nrzi=false;
    for(int b:data) { if(!b) nrzi=!nrzi; levels.push_back(nrzi?1.:-1.); }
    const auto sps=rate/9600; const double sigma=0.331*sps; int radius=int(std::ceil(3*sigma));
    std::vector<double> weights; double sum=0;
    for(int k=-radius;k<=radius;++k) { double w=std::exp(-0.5*k*k/(sigma*sigma)); weights.push_back(w); sum+=w; }
    for(auto &w:weights)w/=sum;
    std::mt19937 rng(12345); std::normal_distribution<double> dist(0,noise);
    std::vector<std::complex<float>> output(delay,{0,0}); double phase=0;
    const auto count=levels.size()*std::size_t(sps);
    for(std::size_t n=0;n<count;++n) {
        double shaped=0;
        for(int k=-radius;k<=radius;++k) {
            const long pos=std::clamp<long>(long(n)+k,0,long(count)-1);
            shaped+=weights[k+radius]*levels[std::size_t(pos)/sps];
        }
        phase+=2*pi*(offsetHz+2400*shaped)/rate;
        std::complex<float> v(float(0.8*std::cos(phase)+dist(rng)),float(0.8*std::sin(phase)+dist(rng)));
        output.push_back(conjugate?std::conj(v):v);
    }
    output.insert(output.end(),std::size_t(sps)*4,{0,0}); return output;
}
}
