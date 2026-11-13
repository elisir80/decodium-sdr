// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "dsp/SelcalAudioDecoder.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using dsdr::dsp::SelcalAudioDecoder;
static void receive(void *p,const std::array<char,5> &v) { static_cast<std::vector<std::string> *>(p)->emplace_back(v.data(),v.size()); }
static std::vector<float> signal(bool second,bool extra=false) {
    constexpr double pi=3.14159265358979323846;
    std::vector<float> v(8000*3,0);
    for(std::size_t i=0;i<v.size();++i) {
        const double t=double(i)/8000;
        if(t>=0.2 && t<1.2) v[i]=float(0.3*(std::sin(2*pi*312.6*t)+std::sin(2*pi*977.2*t)));
        if(second && t>=1.4 && t<2.4) v[i]=float(0.3*(std::sin(2*pi*384.6*t)+std::sin(2*pi*1083.9*t)));
        if(extra && t>=0.2 && t<2.4) v[i]+=float(0.3*std::sin(2*pi*716.1*t));
    }
    return v;
}
int main(int argc, char **argv) {
    const auto good=signal(true), incomplete=signal(false), interference=signal(true,true);
    for(const std::size_t chunk : {std::size_t(1),std::size_t(257),good.size()}) {
        std::vector<std::string> events; SelcalAudioDecoder decoder(receive,&events);
        for(std::size_t i=0;i<good.size();i+=chunk) decoder.process(good.data()+i,std::min(chunk,good.size()-i));
        assert(events.size()==1 && events[0]=="AM-CP");
        decoder.reset(); events.clear(); decoder.process(incomplete.data(),incomplete.size()); assert(events.empty());
        decoder.reset(); decoder.process(interference.data(),interference.size()); assert(events.empty());
        decoder.reset(); decoder.process(good.data(),good.size()/2); decoder.reset(); decoder.process(good.data()+good.size()/2,good.size()-good.size()/2); assert(events.empty());
    }
    // A deterministic noise-only control and a low-noise version of exactly
    // the same call exercise rejection and recovery without relying on RF.
    auto noisy=good; std::vector<float> noise(good.size()); std::uint32_t rng=0x4137;
    for(std::size_t i=0;i<noise.size();++i) {
        rng=1664525u*rng+1013904223u;
        noise[i]=float((double(rng)/4294967295.0-0.5)*0.02);
        noisy[i]+=noise[i];
    }
    std::vector<std::string> events; SelcalAudioDecoder decoder(receive,&events);
    decoder.process(noise.data(),noise.size()); assert(events.empty());
    decoder.reset(); decoder.process(noisy.data(),noisy.size()); assert(events.size()==1 && events[0]=="AM-CP");
    // Synthetic voiced harmonics with moving pitch, syllabic envelope and
    // broadband noise: this is a negative control, not a speech recording.
    constexpr double pi=3.14159265358979323846;
    for(std::uint32_t seed=1;seed<=8;++seed) {
        std::vector<float> voice(8000*12); std::uint32_t random=seed; double phase=0;
        for(std::size_t i=0;i<voice.size();++i) {
            const double t=double(i)/8000, pitch=105+25*std::sin(2*pi*.73*t+seed);
            phase+=2*pi*pitch/8000;
            const double env=std::max(0.,std::sin(2*pi*2.4*t+seed));
            random ^= random<<13; random ^= random>>17; random ^= random<<5;
            const double hiss=(double(random&65535u)/32767.5-1)*.10;
            voice[i]=float(env*(.2*std::sin(phase)+.14*std::sin(2*phase)+.10*std::sin(3*phase)+.08*std::sin(5*phase))+hiss);
        }
        decoder.reset(); events.clear(); decoder.process(voice.data(),voice.size());
        assert(events.empty());
    }
    // Optional, externally supplied mono 8 kHz signed-16 little-endian
    // speech fixture. It is not redistributed or required by CI.
    for (int arg=1; arg<argc; ++arg) {
        std::ifstream file(argv[arg], std::ios::binary);
        assert(file.is_open());
        const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file),
                                               std::istreambuf_iterator<char>()};
        assert(bytes.size() >= 16000 && bytes.size()%2 == 0);
        std::vector<float> speech(bytes.size()/2);
        for (std::size_t i=0; i<speech.size(); ++i) {
            const unsigned sample=unsigned(bytes[2*i]) | (unsigned(bytes[2*i+1])<<8);
            const int signedSample=sample>=32768 ? int(sample)-65536 : int(sample);
            speech[i]=float(signedSample)/32768.0f;
        }
        decoder.reset(); events.clear();
        decoder.process(speech.data(),speech.size());
        assert(events.empty());
        // Same spoken sample plus a valid call after a silence: speech must
        // not prevent subsequent reception, and exactly one call is emitted.
        const std::vector<float> silence(8000,0.0f);
        decoder.process(silence.data(),silence.size());
        decoder.process(good.data(),good.size());
        assert(events.size()==1 && events[0]=="AM-CP");
    }
}
