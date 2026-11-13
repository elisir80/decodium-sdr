// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_fixture.h"
#include "dsp/PocsagAudioDecoder.h"
#include <cmath>
#include <random>

static std::vector<float> audio(unsigned baud, double offset, double noise, double phase, double drift, unsigned seed, bool invert=false) {
    auto bits = stream(0, invert); constexpr double rate=48000;
    const double symbol = rate / (baud * (1.0 + drift));
    std::mt19937 rng(seed); std::normal_distribution<double> dist(0, noise);
    std::vector<float> result; double filtered = 0;
    for (std::size_t i=0;;++i) {
        auto index=static_cast<std::size_t>((i + phase) / symbol);
        if(index>=bits.size()) break;
        filtered += 0.3 * ((bits[index] ? 1.0 : -1.0) - filtered);
        result.push_back(static_cast<float>(filtered + offset + dist(rng)));
    }
    return result;
}
static std::vector<PocsagDecoder::Message> decode(const std::vector<float>& a, unsigned baud, std::size_t chunk) {
    std::vector<PocsagDecoder::Message> output;
    dsdr::dsp::PocsagAudioDecoder d(receive,&output); assert(d.configure(48000,baud));
    for(std::size_t i=0;i<a.size();) {auto n=std::min(chunk,a.size()-i); d.process(a.data()+i,n); i+=n;}
    return output;
}
int main() {
    for(unsigned baud:{512u,1200u,2400u}) for(double offset:{-0.35,0.0,0.35})
        for(double noise:{0.0,0.15}) for(bool invert:{false,true}) {
            auto a=audio(baud,offset,noise,3.7,0,42,invert);
            auto a1=decode(a,baud,1), a2=decode(a,baud,257), a3=decode(a,baud,a.size());
            if (a1.size()!=1 || a2.size()!=1 || a3.size()!=1) { std::cerr<<"FAIL "<<baud<<" "<<offset<<" "<<noise<<" "<<invert<<" results "<<a1.size()<<" "<<a2.size()<<" "<<a3.size()<<"\n"; return 1; }
            for (const auto *v:{&a1,&a2,&a3}) assert((*v)[0].address==1234563 && (*v)[0].function==3 && (*v)[0].alpha=="HELLO PAGER" && (*v)[0].correctedBits==0);
        }
    for(unsigned baud:{512u,1200u,2400u}) for(double drift:{-0.001,0.001}) {
        auto m=decode(audio(baud,0.25,0.10,11.2,drift,91),baud,73);
        if(m.size()!=1 || m[0].alpha!="HELLO PAGER") { std::cerr<<"drift failed "<<baud<<" "<<drift<<"\n"; return 1; }
    }
    std::mt19937 rng(18); std::normal_distribution<float> noise(0,0.15);
    std::vector<float> noiseOnly(48000*3); for(auto &v:noiseOnly)v=noise(rng);
    for(unsigned baud:{512u,1200u,2400u}) assert(decode(noiseOnly,baud,113).empty());
    std::vector<PocsagDecoder::Message> resetMessages;
    dsdr::dsp::PocsagAudioDecoder receiver(receive,&resetMessages);
    assert(!receiver.configure(48000, 9600));
    assert(!receiver.configure(10000, 2400));
    assert(receiver.configure(48000,1200));
    auto resetAudio=audio(1200,0.1,0.05,4,0,7);
    const std::size_t split = 850 * 40;
    receiver.process(resetAudio.data(),split);
    receiver.reset();
    receiver.process(resetAudio.data()+split,resetAudio.size()-split);
    assert(resetMessages.empty());
    receiver.reset(); receiver.process(resetAudio.data(),resetAudio.size());
    assert(resetMessages.size()==1 && resetMessages[0].alpha=="HELLO PAGER");
    auto source=audio(1200,0.1,0.05,4,0,7);
    assert(decode(source,2400,129).empty());
    std::cout<<"audio A/B passed: 3 baud x 3 DC offsets x 2 noise levels x 2 polarities x 3 chunk sizes\n";
}
