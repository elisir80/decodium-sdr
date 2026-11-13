// SPDX-License-Identifier: GPL-3.0-or-later
// Compare the receiver to independently generated conventional VOR signals.
#include "dsp/VorReceiver.h"
#include "dect2/vor/vor_gen.h"
#include <cmath>
#include <cstdio>
#include <vector>

int main() {
    int failures=0;
    for(double radial : {0.,45.,90.,135.,180.,225.,270.,315.,359.})
    for(double snr : {99.,25.}) for(bool voice : {false,true}) {
        dect2::VorGenConfig cfg;
        cfg.rate=48000; cfg.radialDeg=radial; cfg.snrDb=snr; cfg.voice=voice;
        cfg.ident="DXB"; cfg.amp=.5; cfg.seed=42;
        dect2::VorGenerator source(cfg);
        using Result=dsdr::dsp::VorReceiver::Result;
        std::vector<Result> received;
        dsdr::dsp::VorReceiver receiver([](const Result& r,void* p) {
            static_cast<std::vector<Result>*>(p)->push_back(r);
        },&received);
        if(!receiver.configure(48000)) return 2;
        std::vector<dect2::cf32> iq(480);
        std::vector<float> audio(480);
        for(int block=0;block<400;++block) {
            source.generate(iq.data(),iq.size());
            for(size_t i=0;i<iq.size();++i) audio[i]=std::abs(iq[i]);
            receiver.processAudio(audio.data(),audio.size());
        }
        const bool ok=!received.empty() && received.back().valid &&
            std::abs(std::remainder(received.back().radialDegrees-radial,360.))<3.;
        std::printf("CVOR expected=%.0f snr=%.0f voice=%d actual=%.2f %s\n",radial,snr,voice,
                    received.empty()?-1:received.back().radialDegrees,ok?"PASS":"FAIL");
        failures+=!ok;
    }
    return failures?1:0;
}
