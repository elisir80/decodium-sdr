// SPDX-License-Identifier: GPL-3.0-or-later
// Compare both decoders against documented public sample sequences.
#include "dsp/SelcalAudioDecoder.h"
#include "dect2/selcal/selcal_rx.h"
#include "dect2/selcal/selcal_tel.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
int main(int argc,char**argv) {
    if(argc!=2) return 2;
    std::ifstream in(argv[1],std::ios::binary); if(!in) return 2;
    std::vector<float> samples; float sample;
    while(in.read(reinterpret_cast<char*>(&sample),sizeof(sample))) samples.push_back(sample);
    if(!in.eof() || in.gcount()!=0 || samples.empty()) return 2;
    std::vector<std::string> expected;
    for(const char* code:{"AB-CD","EF-GH","JK-LM","PQ-RS"}) for(int n=0;n<4;++n) expected.emplace_back(code);
    for(std::size_t chunk:{std::size_t(257),std::size_t(4096)}) {
        std::vector<std::string> ours;
        dsdr::dsp::SelcalAudioDecoder local([](void* p,const std::array<char,5>& v){static_cast<std::vector<std::string>*>(p)->emplace_back(v.data(),v.size());},&ours);
        dect2::SelcalReceiver reference; reference.setSilent(true); reference.setMuted(true); reference.setAudioInput(true); reference.configure(8000);
        dect2::SelcalTelemetry tel{}; uint64_t seq=0;
        for(std::size_t i=0;i<samples.size();i+=chunk) {
            const auto count=std::min(chunk,samples.size()-i); local.process(samples.data()+i,count);
            std::vector<dect2::cf32> data(count);
            for(std::size_t j=0;j<count;++j) data[j]={samples[i+j],0};
            reference.feed(data.data(),count); if(reference.telemetry(tel,seq)) seq=tel.seq;
        }
        std::printf("chunk=%zu Decodium=%zu OnAir=%zu expected=%zu\n",chunk,ours.size(),tel.calls.size(),expected.size());
        if(ours!=expected) return 1;
    }
    return 0;
}
