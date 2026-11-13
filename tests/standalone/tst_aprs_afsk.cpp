// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/AprsAfskDecoder.h"
#include <algorithm>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
using Decoder = dsdr::dsp::AprsAfskDecoder;
using Frame = Decoder::Frame;
constexpr double pi = 3.14159265358979323846;
void address(Frame &f, std::string call, unsigned ssid, bool last) {
    call.resize(6, ' ');
    for (char c : call) f.push_back(static_cast<std::uint8_t>(c << 1));
    f.push_back(static_cast<std::uint8_t>(0x60 | (ssid << 1) | last));
}
Frame frame() {
    Frame f; address(f,"APRS",0,false); address(f,"IU8LMC",7,false); address(f,"WIDE1",1,true);
    f.push_back(0x03); f.push_back(0xf0);
    for (char c : std::string("!3554.00N/01430.00E-Test ~")) f.push_back(c);
    auto crc = Decoder::fcs(f.data(), f.size()); f.push_back(crc & 255); f.push_back(crc >> 8); return f;
}
std::vector<float> waveform(const Frame &f, int rate, double phase, int offset) {
    std::vector<int> bits;
    auto flag = [&] { for (int i=0;i<8;++i) bits.push_back((0x7e >> i)&1); };
    for (int i=0;i<32;++i) flag();
    int ones=0;
    for (auto byte : f) for (int i=0;i<8;++i) {
        int b=(byte>>i)&1; bits.push_back(b);
        if (b && ++ones==5) { bits.push_back(0); ones=0; } else if (!b) ones=0;
    }
    for (int i=0;i<3;++i) flag();
    std::vector<float> out(offset, 0); bool tone=false; double p=phase;
    for (std::size_t i=0;i<bits.size();++i) {
        if (!bits[i]) tone=!tone;
        auto start=static_cast<std::size_t>(std::llround(i*rate/1200.0));
        auto end=static_cast<std::size_t>(std::llround((i+1)*rate/1200.0));
        for (auto j=start;j<end;++j) { out.push_back(static_cast<float>(0.7*std::sin(p))); p+=2*pi*(tone?1200:2200)/rate; }
    }
    out.insert(out.end(), rate/100, 0); return out;
}
void capture(void *p, const Frame &f) { static_cast<std::vector<Frame> *>(p)->push_back(f); }
int main() {
    const char *check="123456789";
    assert(Decoder::fcs(reinterpret_cast<const std::uint8_t *>(check),9)==0x906e);
    auto good=frame(); assert(Decoder::validFrame(good));
    auto bad=good; bad[bad.size()-3]^=1; assert(!Decoder::validFrame(bad));
    auto badFcs=good; badFcs.back()^=1; assert(!Decoder::validFrame(badFcs));
    auto unsupported=good; unsupported[22]=0x01; auto c=Decoder::fcs(unsupported.data(),unsupported.size()-2); unsupported[unsupported.size()-2]=c&255; unsupported.back()=c>>8; assert(!Decoder::validFrame(unsupported));
    auto expected=good; expected.resize(expected.size()-2);
    for (int rate : {8000, 11025, 22050, 44100, 48000}) for (int offset : {0, 3, 11}) {
        std::vector<Frame> got; Decoder d(capture,&got); assert(d.configure(rate));
        auto invalid=waveform(bad,rate,0.3,offset); d.process(invalid.data(),invalid.size()); assert(got.empty());
        d.reset(); auto invalidFcs=waveform(badFcs,rate,0.8,offset); d.process(invalidFcs.data(),invalidFcs.size()); assert(got.empty());
        d.reset(); auto samples=waveform(good,rate,1.1,offset);
        for (std::size_t at=0;at<samples.size();) { auto n=std::min<std::size_t>(137,samples.size()-at); d.process(samples.data()+at,n); at+=n; }
        if (got.size()!=1 || got[0]!=expected) { std::cerr<<"decode failure rate="<<rate<<" offset="<<offset<<" count="<<got.size()<<'\n'; return 1; }
        d.process(samples.data(),samples.size()); assert(got.size()==2);
        d.reset(); got.clear(); d.process(samples.data(),samples.size()/2); d.reset(); d.process(samples.data()+samples.size()/2,samples.size()-samples.size()/2); assert(got.empty());
    }
    std::vector<Frame> got; Decoder d(capture,&got); assert(!d.configure(0)); assert(!d.configure(7999)); assert(d.configure(48000));
    std::cout << "APRS A/B, CRC, streaming and reset tests passed\n";
    return 0;
}
