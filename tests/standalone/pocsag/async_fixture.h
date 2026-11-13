// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_fixture.h"
#include "dsp/PocsagIqReceiver.h"
#include <cmath>
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
[[maybe_unused]] static std::vector<PocsagDecoder::Message> decode(const std::vector<dsdr::dsp::Complex>& iq,
        unsigned rate, unsigned baud, double tune, std::size_t chunk) {
    std::vector<PocsagDecoder::Message> out;
    dsdr::dsp::PocsagIqReceiver receiver(receive,&out); assert(receiver.configure(rate, baud, tune));
    for(std::size_t i=0;i<iq.size();) {auto n=std::min(chunk,iq.size()-i); receiver.process(iq.data()+i,n); i+=n;}
    return out;
}
[[maybe_unused]] static bool expected(const std::vector<PocsagDecoder::Message>& messages) {
    return messages.size()==1 && messages[0].address==1234563 && messages[0].function==3 &&
           messages[0].alpha=="HELLO PAGER" && messages[0].correctedBits==0;
}
