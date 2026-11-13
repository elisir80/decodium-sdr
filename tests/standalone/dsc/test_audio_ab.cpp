// Reuse the independent bit fixture, but modulate it with an independent audio oscillator.
#define main bit_fixture_main
#include "test_ab.cpp"
#undef main
#ifdef DSC_PRODUCT
#include "dsp/dsc/FskAudioRx.h"
#else
#include "FskAudioRx.h"
#endif
#include "dect2/marine/marine_dsp.h"
#include <cmath>
#include <complex>
#include <string>

static std::vector<float> modulate(const Bits& bits, double baud, double offset, double noise, double initialPhase, unsigned seed = 171) {
    constexpr double pi = 3.14159265358979323846;
    std::mt19937 gen(seed); std::normal_distribution<double> gaussian(0, noise);
    std::vector<float> audio(937, 0.f);
    double phase = initialPhase;
    const size_t n = static_cast<size_t>(bits.size() * 24000.0 / baud);
    for (size_t i = 0; i < n; ++i) {
        const size_t bit = std::min(bits.size()-1, size_t(i*baud/24000));
        const double hz = 1700 + offset + (bits[bit] ? -85 : 85);
        phase += 2*pi*hz/24000;
        audio.push_back(float(.65*std::sin(phase) + gaussian(gen)));
    }
    audio.insert(audio.end(), 720, 0.f);
    return audio;
}
struct AudioResult { std::vector<dsc_rx::Distress> ours; std::vector<dect2::DscCall> theirs; };
static AudioResult evaluate(const std::vector<float>& audio, size_t chunk, size_t gap = SIZE_MAX) {
    AudioResult r;
    dsc_rx::FskAudioRx local([&](const auto& c){r.ours.push_back(c);});
    FskSlicer slicer; slicer.setCentreHz(1700);
    DscDecoder ref(false, false), inv(true, false);
    auto cb = [&](const auto& c) { if (c.eccOk) r.theirs.push_back(c); };
    ref.setCallback(cb); inv.setCallback(cb);
    for (size_t at=0; at<audio.size();) {
        if (at == gap) { local.reset(); slicer.reset(); slicer.setCentreHz(1700); ref.reset(); inv.reset(); }
        size_t len = std::min(chunk, audio.size()-at);
        if (at < gap && at + len > gap) len = gap-at;
        local.process(audio.data()+at, len);
        std::vector<dect2::cf32> complex;
        for(size_t i=0;i<len;++i) complex.emplace_back(audio[at+i],0.f);
        slicer.process(complex.data(), len, [&](float s){ ref.pushBit(-s); inv.pushBit(-s); });
        at += len;
    }
    return r;
}
#ifndef DSC_AUDIO_NO_MAIN
int main() {
    std::cout << "Based on OnAir by Kristijonas Adomaitis - https://on-air.dev\n";
    const std::array<int,15> body = {112,23,21,23,45,60,103,15,5,10,1,18,12,34,109};
    auto bits = fixture(body);
    for (double baud : {99.9, 100., 100.1}) {
        for (double offset : {-10., 0., 10.}) {
            auto a = modulate(bits, baud, offset, .02, .73);
            auto r = evaluate(a, 137);
            std::cout << "baud " << baud << " offset " << offset << ": local " << r.ours.size() << " OnAir " << r.theirs.size() << '\n';
            require(r.ours.size()==1 && r.ours[0].body==body, "local audio decode");
            require(r.theirs.size()==1 && r.theirs[0].fromMmsi=="232123456", "OnAir audio decode");
        }
    }
    // Different messages, starting phases and independent noise seeds keep the
    // timing regression from depending on one very regular synthetic frame.
    for (int variant=0; variant<12; ++variant) {
        auto varied=body;
        varied[1]=(17+variant*7)%100;
        varied[2]=(31+variant*11)%100;
        varied[3]=(47+variant*13)%100;
        varied[4]=(59+variant*17)%100;
        varied[5]=((variant+2)%10)*10;
        const auto audio=modulate(fixture(varied),variant%2?99.9:100.1,
                                  variant%3==0?-10.:variant%3==1?0.:10.,
                                  .05,.25+variant*.47,800+variant);
        const auto r=evaluate(audio,variant%2?137:4096);
        require(r.ours.size()==1 && r.ours[0].body==varied, "varied local audio frame");
        char mmsi[11]{};
        for(int i=0;i<5;++i) std::snprintf(mmsi+2*i,3,"%02d",varied[i+1]);
        mmsi[9]=0;
        require(r.theirs.size()==1 && r.theirs[0].eccOk && r.theirs[0].fromMmsi==mmsi,
                "varied reference audio frame and identity");
    }
    auto invertedBits = bits; for (auto& bit : invertedBits) bit ^= 1;
    auto swapped = evaluate(modulate(invertedBits, 100, 0, .02, 2.3), 357);
    require(swapped.ours.size()==1 && swapped.ours[0].inverted && swapped.theirs.size()==1, "swapped tones");
    auto a = modulate(bits, 100, 0, 0, 1.9);
    auto single = evaluate(a,1), blocks = evaluate(a,4096);
    require(single.ours.size()==1 && blocks.ours.size()==1 && single.theirs.size()==1 && blocks.theirs.size()==1, "chunk invariance");
    auto broken = evaluate(a, 137, a.size()/2);
    require(broken.ours.empty() && broken.theirs.empty(), "reset rejects interrupted call");
    auto twice = a; twice.insert(twice.end(), a.begin(), a.end());
    auto recovered = evaluate(twice, 509, a.size()/2);
    require(recovered.ours.size()==1 && recovered.ours[0].body==body && recovered.theirs.size()==1, "new call after discontinuity");
    std::mt19937 rng(544); std::normal_distribution<float> normal(0.f, .3f);
    std::vector<float> noiseOnly(24000*8); for (auto& sample : noiseOnly) sample = normal(rng);
    auto n = evaluate(noiseOnly, 1337);
    require(n.ours.empty() && n.theirs.empty(), "noise only");
    std::vector<float> silence(24000*3,0.f);
    auto quiet = evaluate(silence, 777);
    require(quiet.ours.empty() && quiet.theirs.empty(), "silence");
    std::cout << "PASS audio FSK A/B, clock drift, offsets, noise, chunking, discontinuity, silence\n";
    return 0;
}

#endif
