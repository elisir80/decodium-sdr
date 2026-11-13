// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_fixture.h"
#include "dsp/PocsagIqReceiver.h"
#include <cmath>
#include <chrono>
#include <complex>
#include <limits>
#include <random>

// Independent test modulator: direct phase integration of rectangular 2-FSK;
// no receive filter or discriminator code is shared.
static std::vector<dsdr::dsp::Complex> generate(unsigned rate, unsigned baud,
        double carrierHz, double noiseStd, unsigned seed, bool invert = false,
        double adjacent = 0) {
    const auto bits = stream(0, invert);
    constexpr double pi = 3.14159265358979323846;
    std::mt19937 rng(seed); std::normal_distribution<float> noise(0, static_cast<float>(noiseStd));
    std::vector<dsdr::dsp::Complex> result;
    const std::size_t size = (bits.size() * static_cast<std::size_t>(rate)) / baud;
    result.reserve(size);
    double phase = 0, interferencePhase = 0;
    for (std::size_t n = 0; n < size; ++n) {
        const auto bit = bits[(n * static_cast<std::size_t>(baud)) / rate];
        const double f = carrierHz + (bit ? 4500.0 : -4500.0);
        phase = std::remainder(phase + 2*pi*f/rate, 2*pi);
        interferencePhase = std::remainder(interferencePhase + 2*pi*(carrierHz+20000)/rate, 2*pi);
        result.emplace_back(static_cast<float>(std::cos(phase) + adjacent*std::cos(interferencePhase)) + noise(rng),
                            static_cast<float>(std::sin(phase) + adjacent*std::sin(interferencePhase)) + noise(rng));
    }
    return result;
}
static std::vector<PocsagDecoder::Message> decode(const std::vector<dsdr::dsp::Complex>& iq,
        unsigned rate, unsigned baud, double tune, std::size_t chunk) {
    std::vector<PocsagDecoder::Message> out;
    dsdr::dsp::PocsagIqReceiver receiver(receive,&out); assert(receiver.configure(rate, baud, tune));
    for(std::size_t i=0;i<iq.size();) {auto n=std::min(chunk,iq.size()-i); receiver.process(iq.data()+i,n); i+=n;}
    return out;
}
static bool expected(const std::vector<PocsagDecoder::Message>& messages) {
    return messages.size()==1 && messages[0].address==1234563 && messages[0].function==3 &&
           messages[0].alpha=="HELLO PAGER" && messages[0].correctedBits==0;
}
int main() {
    for(unsigned rate:{48000u,96000u,192000u}) for(unsigned baud:{512u,1200u,2400u})
        for(double centre:{-8000.0,0.0,8000.0}) for(double residual:{-600.0,600.0}) for(double noise:{0.0,0.10}) {
            auto iq=generate(rate,baud,centre+residual,noise,42);
            auto a=decode(iq,rate,baud,centre,1), b=decode(iq,rate,baud,centre,257), c=decode(iq,rate,baud,centre,iq.size());
            if(!expected(a)||!expected(b)||!expected(c)) {
                std::cerr<<"FAIL "<<rate<<" "<<baud<<" "<<centre<<" "<<residual<<" "<<noise<<" results "<<a.size()<<" "<<b.size()<<" "<<c.size()<<"\n"; return 1;
            }
        }
    for(unsigned baud:{512u,1200u,2400u}) {
        auto iq=generate(96000,baud,-3000,0.1,9,true,0.35);
        assert(expected(decode(iq,96000,baud,-3000,137)));
    }
    // Common SDR source rates: same signal at irregular and whole blocks.
    for (unsigned rate : {200000u, 960000u, 1536000u, 2000000u}) {
        const double tune=rate==200000 ? 50000.0 : 100000.0;
        auto high=generate(rate,1200,tune+300.0,0.05,11);
        const auto started=std::chrono::steady_clock::now();
        const auto a=decode(high,rate,1200,tune,257);
        const auto b=decode(high,rate,1200,tune,high.size());
        const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        if (!expected(a) || !expected(b)) {
            std::cerr<<"FAIL high rate "<<rate<<" results "<<a.size()<<" "<<b.size()<<"\n"; return 1;
        }
        std::cout<<"rate "<<rate<<": "<<seconds<<" s for "<<(2.0*high.size()/rate)<<" s synthetic input\n";
    }
    auto iq=generate(96000,1200,2000,0.05,7);
    assert(decode(iq,96000,2400,2000,199).empty());
    std::vector<PocsagDecoder::Message> out;
    dsdr::dsp::PocsagIqReceiver receiver(receive,&out);
    assert(!receiver.configure(44100,1200,0));
    assert(!receiver.configure(2000001,1200,0));
    assert(!receiver.configure(48000,1200,13000));
    assert(!receiver.configure(48000,1000,0));
    assert(!receiver.configure(48000,1200,std::numeric_limits<double>::infinity()));
    assert(receiver.configure(96000,1200,2000));
    const std::size_t split = 850 * 80;
    receiver.process(iq.data(),split); receiver.reset();
    receiver.process(iq.data()+split,iq.size()-split); assert(out.empty());
    receiver.reset(); receiver.process(iq.data(),iq.size()); assert(expected(out));
    std::vector<dsdr::dsp::Complex> noise(96000*3);
    std::mt19937 rng(12); std::normal_distribution<float> dist(0,0.1);
    for(auto &x:noise) x={dist(rng),dist(rng)};
    assert(decode(noise,96000,1200,0,127).empty());
    std::cout<<"IQ A/B: 3 rates x 3 baud x 3 offsets x 2 residuals x 2 noise x 3 chunk sizes passed; controls passed\n";
}
