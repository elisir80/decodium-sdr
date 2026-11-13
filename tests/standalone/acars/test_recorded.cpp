// SPDX-License-Identifier: GPL-3.0-or-later
// Optional external recording: see docs/decoders/priority-ten.md for provenance.
// This test never plays audio. Compare two independent input chunk sizes and
// known message identifiers from the acarsdec reference output.
#include "dsp/acars/acars_chan.h"
#include "core/AcarsReceiver.h"
#include <sndfile.h>
#include <algorithm>
#include <cassert>
#include <complex>
#include <chrono>
#include <thread>
#include <cstdio>
#include <string>
#include <vector>
#ifdef NDEBUG
#error "Assertions are required"
#endif
using Complex = std::complex<float>;
static std::vector<std::string> decode(const std::vector<float>& audio,int channel,std::size_t chunk,bool silent) {
    dect2::AcarsChannelRx rx; rx.configure(1000000,0);
    std::vector<dect2::AcarsRawBlock> blocks; std::vector<Complex> iq; iq.reserve(chunk);
    const auto frames=audio.size()/4;
    auto push=[&](float value) { iq.emplace_back(value,0); if(iq.size()==chunk){rx.process(iq.data(),iq.size());rx.takeBlocks(blocks);iq.clear();} };
    for(std::size_t i=0;i<frames;++i) {
        float a=silent?0:audio[i*4+channel], b=silent?0:audio[std::min(i+1,frames-1)*4+channel];
        for(int k=0;k<80;++k) push(0.5f+0.45f*(a+(b-a)*k/80.f));
    }
    for(int i=0;i<200000;++i) push(0.5f);
    rx.process(iq.data(),iq.size());rx.takeBlocks(blocks);
    std::vector<std::string> out;
    for(const auto &b:blocks) if(b.ok) { std::string text; for(int i=0;i<b.len;++i) text.push_back(char(b.txt[i]&127)); out.push_back(text); }
    return out;
}
static std::vector<std::string> decodeWorker(const std::vector<float>& audio,int channel,std::size_t expected) {
    dsdr::core::AcarsReceiver rx(2048,4096,16);
    rx.setSource(true,1000000,0); rx.setEnabled(true); rx.start();
    std::vector<Complex> iq; iq.reserve(4096);
    auto submit=[&] { if(!iq.empty()) { assert(rx.submit(iq.data(),iq.size())==dsdr::core::AcarsReceiver::SubmitResult::Accepted); iq.clear(); } };
    auto push=[&](float value) { iq.emplace_back(value,0); if(iq.size()==4096) submit(); };
    const auto frames=audio.size()/4;
    for(std::size_t i=0;i<frames;++i) {
        float a=audio[i*4+channel], b=audio[std::min(i+1,frames-1)*4+channel];
        for(int k=0;k<80;++k) push(0.5f+0.45f*(a+(b-a)*k/80.f));
    }
    for(int i=0;i<200000;++i) push(0.5f);
    submit();
    std::vector<std::string> result;
    for(int i=0;i<5000 && result.size()<expected;++i) {
        dsdr::core::AcarsReceiver::Message m;
        while(rx.read(m)) { std::string text; for(std::size_t j=0;j<m.size;++j) text.push_back(char(m.bytes[j]&127)); result.push_back(text); }
        if(result.size()<expected) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(rx.workerErrors()==0 && rx.stats().droppedBlocks==0 && rx.droppedMessages()==0);
    rx.stop(); return result;
}
int main(int argc,char**argv) {
    if(argc!=2) return 2;
    SF_INFO info{}; auto*file=sf_open(argv[1],SFM_READ,&info);
    if(!file || info.channels!=4 || info.samplerate!=12500 || info.frames<=0) return 2;
    std::vector<float> input(std::size_t(info.frames)*4);
    const auto read=sf_readf_float(file,input.data(),info.frames);sf_close(file);
    if(read!=info.frames) return 2;
    const std::vector<std::vector<std::string>> expected={{"F-GTAE","LN-DYY"},{"PH-BXR","LN-DYY"},{"G-DBCK","G-DBCK"},{"LN-DYY"}};
    // Labels and flight IDs read independently from the acarsdec output.
    const std::vector<std::vector<std::vector<std::string>>> fields={
        {{"H1","AF7728","#DFB00000"},{"_"}},
        {{"5V","KL1681"},{"Q0","DY083J"}},
        {{"BA031T"},{"Q0","BA031T"}},
        {{"Q0","DY083J"}}
    };
    for(int channel=0;channel<4;++channel) {
        auto a=decode(input,channel,977,false), b=decode(input,channel,4096,false);
        assert(a==b && a.size()==expected[channel].size());
        assert(decodeWorker(input,channel,expected[channel].size())==a);
        for(std::size_t i=0;i<a.size();++i) {
            assert(a[i].find(expected[channel][i])!=std::string::npos);
            for(const auto &field:fields[channel][i]) assert(a[i].find(field)!=std::string::npos);
        }
        assert(decode(input,channel,2048,true).empty());
        std::printf("channel %d: %zu reference messages, silence rejected\n",channel+1,a.size());
    }
}
