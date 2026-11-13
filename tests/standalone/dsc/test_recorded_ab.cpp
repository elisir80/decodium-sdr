// SPDX-License-Identifier: GPL-3.0-or-later
// Independent OnAir comparison; input is mono 24 kHz float after translation.
#include "dsp/dsc/FskAudioRx.h"
#include "dect2/marine/marine_dsc.h"
#include "dect2/marine/marine_dsp.h"
#include <cstdio>
#include <set>
#include <string>
#include <vector>
int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) return 2;
    FILE* f=std::fopen(argv[1],"rb"); if(!f) return 2;
    std::multiset<std::string> ours, reference;
    int referenceBitsOurs=0;
    dsc_rx::Decoder bitDecoder;
    dsc_rx::FskAudioRx local([&](const auto& m) {
        char id[11]{};
        for(int i=0;i<5;++i) std::snprintf(id+2*i,3,"%02d",m.fullBody[m.sourceOffset+i]);
        id[9]=0; ours.insert(id);
    });
    dect2::marine::FskSlicer slicer; slicer.setCentreHz(1700);
    dect2::marine::DscDecoder ref(false,false), inverted(true,false);
    auto accept=[&](const auto& m){ if(m.eccOk) reference.insert(m.fromMmsi); };
    ref.setCallback(accept); inverted.setCallback(accept);
    float samples[4096]; size_t n;
    while((n=std::fread(samples,sizeof(float),4096,f))) {
        local.process(samples,n);
        std::vector<dect2::cf32> data(n);
        for(size_t i=0;i<n;++i) data[i]={samples[i],0};
        slicer.process(data.data(),n,[&](float bit){
            ref.pushBit(-bit); inverted.pushBit(-bit);
            if(bitDecoder.push(-bit>0)) ++referenceBitsOurs;
        });
    }
    const bool readOk=!std::ferror(f); std::fclose(f);
    const auto totalReference=reference.size(); unsigned shared=0;
    if(argc==3) {
        FILE* ids=std::fopen(argv[2],"wx"); if(!ids) return 2;
        for(const auto& id:reference) if(std::fprintf(ids,"%s\n",id.c_str())<0) {std::fclose(ids); return 2;}
        if(std::fclose(ids)!=0) return 2;
    }
    for(const auto& id:ours) { auto it=reference.find(id); if(it!=reference.end()){++shared;reference.erase(it);} }
    std::printf("local=%zu reference=%zu shared=%u local_on_reference_bits=%d\n",ours.size(),totalReference,shared,referenceBitsOurs);
    return readOk && ours.size()==5 && totalReference==5 && shared==5 && referenceBitsOurs==5 ? 0 : 1;
}
