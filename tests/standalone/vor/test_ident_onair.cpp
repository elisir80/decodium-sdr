// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/VorIdentDecoder.h"
#include "dsp/VorReceiver.h"
#include "dect2/vor/vor_gen.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
int main(int argc,char **argv) {
    int failed=0;
    const bool stress=argc>1 && std::strcmp(argv[1],"--stress")==0;
    const bool names=argc>1 && std::strcmp(argv[1],"--names")==0;
    const bool longNegative=argc>1 && std::strcmp(argv[1],"--negative-long")==0;
    const std::vector<std::string> identifiers=names ? std::vector<std::string>{"AAA","SOS","MT","TST","N0X","ET","TTT","EET","A1B","Z9"} : std::vector<std::string>{"DXB"};
    for(const auto &id:identifiers)
    for(double wpm:{6.,7.,8.,10.,12.,14.}) for(bool present:{false,true})
    for(double snr:{99.,25.}) for(bool voice:{false,true}) for(unsigned seed:{1u,2u,3u}) {
        if(names && (!present || (wpm!=7. && wpm!=10. && wpm!=14.) || snr!=25. || seed!=1)) continue;
        if(longNegative && (present || wpm!=7. || snr!=25. || !voice)) continue;
        if (!names && !stress && !longNegative && present && (snr!=99. || voice)) continue;
        dect2::VorGenConfig cfg; cfg.rate=48000; cfg.snrDb=snr; cfg.amp=.5; cfg.seed=seed;
        cfg.voice=voice; cfg.ident=present ? id : ""; cfg.identWpm=wpm; cfg.identPeriod=30;
        dect2::VorGenerator generator(cfg);
        dsdr::dsp::VorIdentDecoder decoder;
        std::vector<dsdr::dsp::VorReceiver::Result> results;
        dsdr::dsp::VorReceiver receiver([](const auto &r,void *p) { static_cast<std::vector<dsdr::dsp::VorReceiver::Result> *>(p)->push_back(r); },&results);
        if(!receiver.configure(48000)) return 2;
        std::vector<dect2::cf32> iq(480); std::vector<float> audio(480);
        bool transientFalseId=false;
        for(int block=0;block<(longNegative ? 6000 : 900);++block) {
            generator.generate(iq.data(),iq.size());
            for(std::size_t i=0;i<iq.size();++i) audio[i]=std::abs(iq[i]);
            decoder.process(audio.data(),audio.size());
            if(longNegative && !decoder.identifier().empty()) transientFalseId=true;
            receiver.processAudio(audio.data(),audio.size());
        }
        std::printf("OnAir VOR ident expected=%s wpm=%.0f present=%d snr=%.0f voice=%d seed=%u decoded=%s\n",id.c_str(),wpm,present,snr,voice,seed,decoder.identifier().c_str());
        if(decoder.identifier()!=(present ? id : "")) ++failed;
        if(transientFalseId) { std::printf("Unexpected transient identification\n"); ++failed; }
        if(results.empty() || !results.back().valid || std::string(results.back().identifier.data())!=(present ? id : "")) ++failed;
    }
    return failed ? 1 : 0;
}
