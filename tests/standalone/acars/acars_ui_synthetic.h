// SPDX-License-Identifier: GPL-3.0-or-later
// Independent ACARS waveform synthesizer, copied from the standalone RX test.
#pragma once
#include "core/AcarsReceiver.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
using dsdr::dsp::Complex;
constexpr double pi=3.14159265358979323846, rate=1000000.;
static unsigned char parity(unsigned char x) {
    x &= 127; unsigned v=x; v^=v>>4; v^=v>>2; v^=v>>1; return x | ((!(v&1))<<7);
}
static std::uint16_t crc(const std::vector<unsigned char>& x) {
    // Equivalent polynomial implemented as a table-driven update, independent of the receiver.
    std::uint16_t tab[256];
    for(unsigned i=0;i<256;++i) { unsigned v=i; for(int k=0;k<8;++k) v=(v>>1)^((v&1)?0x8408:0); tab[i]=v; }
    unsigned r=0; for(auto v:x) r=(r>>8)^tab[(r^v)&255]; return r;
}
static std::vector<unsigned char> payload(bool parityBad=false) {
    std::vector<unsigned char> x;
    for(char c:std::string("2.N824UA")) x.push_back(parity(c));
    x.push_back(parity(0x15)); x.push_back(parity('H')); x.push_back(parity('1')); x.push_back(parity('0'));
    x.push_back(parity(2)); for(char c:std::string("TEST123")) x.push_back(parity(c)); x.push_back(parity(3));
    if(parityBad) x[3]^=0x80;
    return x;
}
static std::vector<Complex> generate(bool badCrc=false,bool badParity=false, double offset=0, bool badLayout=false) {
    auto body=payload(badParity); if(badLayout) body[12]=parity(4); unsigned c=crc(body); if(badCrc) c^=1;
    std::vector<unsigned char> bytes(20,0xff);
    bytes.reserve(20 + body.size() + 8);
    bytes.push_back(parity('+')); bytes.push_back(parity('*')); bytes.push_back(0x16); bytes.push_back(0x16); bytes.push_back(1);
    bytes.insert(bytes.end(),body.begin(),body.end()); bytes.push_back(c&255); bytes.push_back(c>>8); bytes.push_back(0x7f);
    std::vector<int> bits; for(unsigned char b:bytes) for(int k=0;k<8;++k) bits.push_back((b>>k)&1);
    const std::size_t lead=rate*0.08, tail=rate*0.18, total=std::size_t(std::ceil(bits.size()*rate/2400));
    std::vector<Complex> out(lead+total+tail);
    double audio=0, carrier=0; int previous=1;
    for(std::size_t i=0;i<out.size();++i) {
        float a=0;
        if(i>=lead && i<lead+total) {
            std::size_t bit=std::min(bits.size()-1,std::size_t((i-lead)*2400/rate));
            if(bit>0) previous=bits[bit-1];
            double hz=bits[bit]==previous ? 2400 : 1200;
            audio+=2*pi*hz/rate;
            a=float(0.5*(1+0.6*std::sin(audio)));
        } else if(i<lead) a=0.5f;
        carrier+=2*pi*offset/rate;
        out[i]=Complex(a*std::cos(carrier),a*std::sin(carrier));
    }
    return out;
}
