// SPDX-License-Identifier: GPL-3.0-or-later
// Both receivers consume the same generated audio; no sound device is opened.
#include "dsp/SelcalAudioDecoder.h"
#include "dect2/selcal/selcal_rx.h"
#include "dect2/selcal/selcal_tel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

static void collect(void* p, const std::array<char, 5>& v) {
    static_cast<std::vector<std::string>*>(p)->emplace_back(v.data(), v.size());
}
int main(int argc,char **argv) {
    constexpr double pi = 3.14159265358979323846;
    const bool wide=argc>1 && std::strcmp(argv[1],"--wide")==0;
    const std::vector<double> offsets=wide ? std::vector<double>{-5.,-3.,0.,3.,5.} : std::vector<double>{-1.5,0.,1.5};
    const std::vector<double> levels=wide ? std::vector<double>{0.,.10,.25} : std::vector<double>{0.,.10};
    int failures=0;
    constexpr double hz[]={312.6,346.7,384.6,426.6,473.2,524.8,582.1,645.7,716.1,794.3,881.0,977.2,1083.9,1202.3,1333.5,1479.1};
    struct Call { int a,b,c,d; const char *code; };
    for (const Call call: {Call{0,11,2,12,"AM-CP"},Call{0,1,2,3,"AB-CD"},
                           Call{6,7,8,9,"GH-JK"},Call{12,13,14,15,"PQ-RS"}})
    for (double offset : offsets) for (double noise : levels) for (int mode : {0,1,2,3,4,5,6}) for (std::uint32_t seed : {0x651317u,0x952a31u,0x785abcu}) {
        std::vector<float> samples(8000 * 4, 0.f);
        std::uint32_t random=seed;
        for (size_t i=0; i<samples.size(); ++i) {
            double t = double(i)/8000;
            const double toneDuration=mode==5 ? .15 : 1.0;
            if (mode && t>=.3 && t<.3+toneDuration) samples[i] = float(.3*(std::sin(2*pi*(hz[call.a]+offset)*t)+(mode==4 ? 0 : std::sin(2*pi*(hz[call.b]+offset)*t))));
            if (mode>=2 && mode!=6 && t>=1.5 && t<1.5+toneDuration) samples[i] = float(.3*(std::sin(2*pi*(hz[call.c]+offset)*t)+(mode==4 ? 0 : std::sin(2*pi*(hz[call.d]+offset)*t))));
            if (mode==6 && t>=.3 && t<1.3)
                samples[i]+=float(.3*(std::sin(2*pi*(hz[call.c]+offset)*t)+std::sin(2*pi*(hz[call.d]+offset)*t)));
            if (mode==3 && t>=.3 && t<2.5) {
                int other=0; while(other==call.a || other==call.b || other==call.c || other==call.d) ++other;
                samples[i]+=float(.3*std::sin(2*pi*(hz[other]+offset)*t));
            }
            random ^= random<<13; random ^= random>>17; random ^= random<<5;
            samples[i]+=float(noise*(double(random&65535u)/32767.5-1));
        }
        std::vector<std::string> found;
        dsdr::dsp::SelcalAudioDecoder ours(collect, &found);
        dect2::SelcalReceiver theirs;
        theirs.setSilent(true); theirs.setMuted(true); theirs.setAudioInput(true); theirs.configure(8000);
        dect2::SelcalTelemetry tel; uint64_t seq=0;
        for (size_t i=0; i<samples.size(); i+=257) {
            size_t n=std::min(size_t(257),samples.size()-i);
            ours.process(samples.data()+i,n);
            std::vector<dect2::cf32> data(n);
            for (size_t j=0;j<n;++j) data[j]={samples[i+j],0};
            theirs.feed(data.data(),n);
            if (theirs.telemetry(tel,seq)) seq=tel.seq;
        }
        bool a=found.size()==1 && found[0]==call.code;
        bool b=tel.calls.size()==1 && tel.calls[0].code==call.code;
        bool same=found.size()==tel.calls.size();
        for(std::size_t i=0;same && i<found.size();++i) same=found[i]==tel.calls[i].code;
        const char *label=mode==2?"complete":mode==1?"incomplete":mode==3?"three-tone":mode==4?"single-tone":mode==5?"short-tones":mode==6?"overlap":"noise-only";
        std::printf("%s offset=%+.1f noise=%.2f seed=%u %s: Decodium=%d OnAir=%d same=%d\n",call.code,offset,noise,seed,label,a,b,same);
        if (a!=(mode==2) || (!wide && (b!=(mode==2) || !same)) || (mode!=2 && (!found.empty() || !tel.calls.empty()))) ++failures;
    }
    return failures ? 1 : 0;
}
