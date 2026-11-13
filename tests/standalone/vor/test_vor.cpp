// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../../src/dsp/VorReceiver.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>
using dsdr::dsp::VorReceiver;
namespace {
constexpr double pi=3.14159265358979323846, fs=48000;
struct Signal { std::vector<float> audio; std::vector<std::complex<float>> iq; };
// Closed-form transmitter: frequency is derivative of FM phase; no RX code used.
Signal generate(double radial, double noise, int mode, unsigned seed) {
    Signal s; std::mt19937 rng(seed); std::normal_distribution<double> random(0,noise);
    for (int n=0;n<int(3*fs);++n) {
        double t=n/fs, p=2*pi*30*t;
        double variable = mode==1 ? 0 : .3*std::cos(p-radial*pi/180);
        double sub = mode==2 ? 0 : .3*std::cos(2*pi*9960*t+16*std::sin(p));
        // 1020 Hz ident keyed as ... --- ... ; used only as an interferer.
        const char* ident="101010001110111011100010101";
        const auto slot=static_cast<unsigned>(t/0.1);
        double identTone = mode==3 && slot<27 && ident[slot]=='1' ? .07*std::cos(2*pi*1020*t) : 0;
        double a=1+variable+sub+identTone;
        s.audio.push_back(float(a+random(rng)));
        const double carrier=2*pi*1400*t+0.42;
        s.iq.emplace_back(float(a*std::cos(carrier)+random(rng)),float(a*std::sin(carrier)+random(rng)));
    }
    return s;
}
void callback(const VorReceiver::Result& r,void* p) { static_cast<std::vector<VorReceiver::Result>*>(p)->push_back(r); }
void check(bool ok,const char* label) { if (!ok) { std::cerr << "FAIL " << label << '\n'; std::exit(1); } }
double diff(double a,double b) { return std::abs(std::remainder(a-b,360.)); }
template<class T, class F> void feed(const std::vector<T>& data,std::size_t chunk,F f) {
    for(std::size_t i=0;i<data.size();) { auto n=std::min(chunk,data.size()-i); f(data.data()+i,n); i+=n; }
}
}
int main() {
    unsigned runs=0;
    for(double angle:{0.,45.,90.,180.,270.,359.}) for(double noise:{0.,0.05}) {
        auto s=generate(angle,noise,0,42);
        for(std::size_t chunk:{std::size_t(1),std::size_t(257),s.audio.size()}) for(bool iq:{false,true}) {
            std::vector<VorReceiver::Result> out; VorReceiver d(callback,&out); check(d.configure(48000),"config");
            if(iq) feed(s.iq,chunk,[&](auto p,auto n){d.processIq(p,n);});
            else feed(s.audio,chunk,[&](auto p,auto n){d.processAudio(p,n);});
            check(out.size()==2,"window count");
            for(const auto& r:out) { if(!r.valid || diff(r.radialDegrees,angle)>3) {
                std::cerr<<"angle="<<angle<<" got="<<r.radialDegrees<<" valid="<<r.valid<<" va="<<r.variableAmplitude<<" ref="<<r.referenceAmplitude<<" sc="<<r.subcarrierAmplitude<<'\n'; check(false,"radial"); } }
            ++runs;
        }
    }
    for (double angle:{45.,180.,359.}) {
        auto s=generate(angle,0.05,3,77);
        for (bool iq:{false,true}) {
            std::vector<VorReceiver::Result> result; VorReceiver receiver(callback,&result); receiver.configure(48000);
            if (iq) receiver.processIq(s.iq.data(),s.iq.size()); else receiver.processAudio(s.audio.data(),s.audio.size());
            check(result.size()==2,"ident window count");
            for (auto r:result) check(r.valid && diff(r.radialDegrees,angle)<3,"ident interference");
        }
    }
    for(int mode:{1,2}) { auto s=generate(45,0,mode,1); std::vector<VorReceiver::Result> out; VorReceiver d(callback,&out); d.configure(48000); d.processAudio(s.audio.data(),s.audio.size()); check(!out.empty(),"negative output"); for(auto r:out) check(!r.valid,"missing component"); }
    // The detector must be invariant to gain without admitting a single
    // branch, silence or weak random noise as a complete VOR signal.
    for(float gain:{0.001f,0.01f,0.1f,1.0f}) {
        for(int mode:{0,1,2}) {
            auto s=generate(135,0.03,mode,87);
            for(auto& sample:s.audio) sample*=gain;
            std::vector<VorReceiver::Result> out; VorReceiver d(callback,&out); d.configure(48000);
            d.processAudio(s.audio.data(),s.audio.size()); check(out.size()==2,"gain windows");
            for(const auto& r:out) {
                if(mode==0) check(r.valid && diff(r.radialDegrees,135)<3,"valid at different gains");
                else check(!r.valid,"missing branch at different gains");
            }
        }
    }
    std::vector<VorReceiver::Result> quiet; VorReceiver q(callback,&quiet); q.configure(48000);
    std::vector<float> silence(144000,0); q.processAudio(silence.data(),silence.size());
    check(!quiet.empty(),"silence windows"); for (auto r:quiet) check(!r.valid,"silence invalid");
    q.reset(); quiet.clear(); std::mt19937 rng(19); std::normal_distribution<float> noise(0,0.05);
    for (auto& x:silence) x=noise(rng); q.processAudio(silence.data(),silence.size());
    for (auto r:quiet) check(!r.valid,"noise invalid");
    for(unsigned seed=0;seed<12;++seed) {
        q.reset(); quiet.clear(); std::mt19937 noiseRng(seed);
        for(auto& x:silence) x=noise(noiseRng);
        q.processAudio(silence.data(),silence.size());
        for(const auto& r:quiet) check(!r.valid,"multiple noise seeds invalid");
    }
    for(double gain:{.005,.05,.5}) for(double voiceHz:{120.,300.,750.}) {
        q.reset(); quiet.clear();
        // Audio containing voice-like harmonics and a keyed ident, but no
        // VOR 9960-Hz FM subcarrier or 30-Hz navigation modulation.
        for(std::size_t i=0;i<silence.size();++i) {
            const double t=i/fs;
            silence[i]=float(gain*(.5*std::sin(2*pi*voiceHz*t)+.25*std::sin(4*pi*voiceHz*t)+
                                     .15*std::sin(6*pi*voiceHz*t)+.1*std::sin(2*pi*1020*t)));
        }
        q.processAudio(silence.data(),silence.size());
        for(const auto& r:quiet) check(!r.valid,"voice-like interference invalid");
    }
    std::vector<VorReceiver::Result> out; VorReceiver d(callback,&out); check(!d.configure(8000),"invalid sample rate");
    auto s=generate(90,0,0,1); d.processAudio(s.audio.data(),s.audio.size()); check(out.empty(),"disabled");
    d.configure(48000); d.processAudio(s.audio.data(),24000); d.reset(); d.processAudio(s.audio.data(),s.audio.size()); check(out.size()==2 && out.back().valid && diff(out.back().radialDegrees,90)<3,"reset");
    std::cout<<"VOR "<<runs<<" audio/IQ A/B runs passed, missing-component and reset controls passed\n";
}
