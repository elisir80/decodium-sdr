// SPDX-License-Identifier: GPL-3.0-or-later
// Independent, direct waveform synthesizer: no OnAir generator or frame builder.
#ifdef NDEBUG
#error "ACARS standalone test requires active assertions"
#endif
#include "core/AcarsReceiver.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>
#include <limits>
#include <string>
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
static std::vector<dect2::AcarsRawBlock> direct(const std::vector<Complex>& x,double offset,std::size_t chunk) {
    dect2::AcarsChannelRx d; d.configure(rate,offset); std::vector<dect2::AcarsRawBlock> result;
    for(std::size_t i=0;i<x.size();i+=chunk) { d.process(x.data()+i,std::min(chunk,x.size()-i)); d.takeBlocks(result); }
    result.erase(std::remove_if(result.begin(),result.end(),[](auto &b){return !b.ok;}),result.end()); return result;
}
static std::vector<dsdr::core::AcarsReceiver::Message> asyncRun(const std::vector<Complex>& x,double offset,std::size_t chunk) {
    dsdr::core::AcarsReceiver rx(512,4096,32); rx.setSource(true,rate,offset); rx.setEnabled(true); rx.start();
    assert(rx.status()==dsdr::core::AcarsReceiver::Status::Active);
    auto g=rx.generation();
    for(std::size_t i=0;i<x.size();i+=chunk) assert(rx.submitForGeneration(x.data()+i,std::min(chunk,x.size()-i),g)==dsdr::core::AcarsReceiver::SubmitResult::Accepted);
    std::vector<dsdr::core::AcarsReceiver::Message> out; dsdr::core::AcarsReceiver::Message m;
    // Wait for output / queue consumption while still accepting results.
    for(int i=0;i<1000;++i) { while(rx.read(m)) out.push_back(m); if(rx.stats().accepted && !out.empty()) break; std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
    assert(rx.workerErrors()==0); rx.stop(); return out;
}
int main() {
    std::puts("Based on OnAir by Kristijonas Adomaitis - https://on-air.dev");
    for(double offset:{0.,23000.}) {
        auto x=generate(false,false,offset); auto a=direct(x,offset,977); auto b=asyncRun(x,offset,2048);
        std::printf("offset %.0f direct %zu worker %zu\n",offset,a.size(),b.size());
        assert(a.size()==1 && b.size()==1); auto expect=payload();
        assert(std::size_t(a[0].len)==expect.size() && b[0].size==expect.size());
        assert(std::equal(expect.begin(),expect.end(),a[0].txt)); assert(std::equal(expect.begin(),expect.end(),b[0].bytes.begin()));
        auto a2=direct(x,offset,4096); assert(a2.size()==1 && a2[0].len==a[0].len && std::equal(a[0].txt,a[0].txt+a[0].len,a2[0].txt));
    }
    for(auto x:{generate(true,false),generate(false,true),generate(false,false,0,true),std::vector<Complex>(300000)}) {
        assert(direct(x,0,1024).empty()); assert(asyncRun(x,0,2048).empty());
    }
    dsdr::core::AcarsReceiver r(8,1024,8); r.setSource(true,48000,0); r.setEnabled(true); r.start();
    assert(r.status()==dsdr::core::AcarsReceiver::Status::WrongRate); r.setSource(true,rate,0); auto old=r.generation(); r.discontinuity();
    Complex x{}; assert(r.submitForGeneration(&x,1,old)==dsdr::core::AcarsReceiver::SubmitResult::Stopped);
    assert(r.submit(nullptr,1)==dsdr::core::AcarsReceiver::SubmitResult::Invalid);
    assert(r.submit(&x,1025)==dsdr::core::AcarsReceiver::SubmitResult::TooLarge);
    r.stop(); assert(r.submit(&x,1)==dsdr::core::AcarsReceiver::SubmitResult::Stopped);

    // A discontinuity between the two halves must prevent a frame from being
    // assembled across the gap. A subsequent complete frame must recover.
    {
        auto v=generate(); dsdr::core::AcarsReceiver rx(512,4096,8);
        rx.setSource(true,rate,0); rx.setEnabled(true); rx.start();
        auto send=[&](std::size_t begin,std::size_t end) {
            auto g=rx.generation();
            for(auto i=begin;i<end;i+=2048) assert(rx.submitForGeneration(v.data()+i,std::min<std::size_t>(2048,end-i),g)==dsdr::core::AcarsReceiver::SubmitResult::Accepted);
        };
        const auto middle=v.size()/2; send(0,middle);
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); rx.discontinuity(); send(middle,v.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        dsdr::core::AcarsReceiver::Message msg; assert(!rx.read(msg));
        send(0,v.size());
        bool got=false; for(int i=0;i<1000 && !got;++i) { got=rx.read(msg); if(!got) std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        assert(got && msg.size==payload().size());
        // Unread messages from the previous source must not escape a retune.
        send(0,v.size()); std::this_thread::sleep_for(std::chrono::milliseconds(250));
        rx.discontinuity(); assert(!rx.read(msg));
        auto before=rx.workerErrors(); Complex invalid(std::numeric_limits<float>::quiet_NaN(),0);
        assert(rx.submit(&invalid,1)==dsdr::core::AcarsReceiver::SubmitResult::Accepted);
        for(int i=0;i<500 && rx.workerErrors()==before;++i) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        assert(rx.workerErrors()==before+1); rx.stop();
    }
    std::puts("ACARS A/B and negative cases passed");
}
