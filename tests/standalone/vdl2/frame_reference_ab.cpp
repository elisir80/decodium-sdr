// SPDX-License-Identifier: GPL-3.0-or-later
// Compare the adapter with OnAir's public frame builder and parser. This
// exercises the original C++ mapping but both paths share the Rust protocol
// implementation; it is not an independent protocol or RF validation.
#include "dsp/vdl2/Vdl2Frame.h"
#include "dsp/vdl2/Vdl2Burst.h"
#include "dsp/vdl2/Vdl2Symbols.h"
#include "dsp/vdl2/Vdl2Acquisition.h"
#include "dsp/vdl2/Vdl2Offline.h"
#include "dsp/vdl2/Vdl2WindowStream.h"
#include "dsp/vdl2/Vdl2Frontend.h"
#include "core/Vdl2AsyncStream.h"
#include "core/Vdl2IqPipeline.h"
#include "core/Vdl2ResultQueue.h"
#include "core/Vdl2Session.h"
#include "dect2/vdl2/vdl2_proto.h"
#include "dect2/vdl2/vdl2_gen.h"
#include "dect2/vdl2/vdl2_rx.h"
#include <cmath>
#include <atomic>
#include <chrono>
#include <complex>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

int main(int argc, char **argv) {
    using namespace dect2;
    const Vdl2Addr aircraft{0x4ca2b1, 1, false};
    const Vdl2Addr ground{0x10a000, 4, false};
    const std::vector<std::vector<uint8_t>> frames{
        vdl2Frame(ground, aircraft, 0x01, {}),
        vdl2Frame(aircraft, ground, 0x01, {}),
        vdl2Frame(ground, aircraft, vdl2ControlI(2, 3, false),
                  vdl2X25CallRequest(1, "", "")),
        vdl2Frame(ground, aircraft, kVdl2CtrlXID, vdl2XidAircraftLe(50.0, 3.0, 10000, "TEST"))
    };
    if(argc==2 && std::strcmp(argv[1],"--emit-fixture")==0) {
        const auto coded=vdl2BurstBits(vdl2HdlcBits({frames[0]}));
        for(auto bit:coded) std::putchar('0'+bit);
        std::putchar('\n');
        for(auto byte:frames[0]) std::printf("%02x",byte);
        std::putchar('\n');
        return 0;
    }
    if(argc==2 && (std::strcmp(argv[1],"--emit-iq")==0 || std::strcmp(argv[1],"--emit-iq-192")==0)) {
        const bool demoRate=std::strcmp(argv[1],"--emit-iq-192")==0;
        auto generated=vdl2MakeWave(frames);
        generated.start=demoRate ? 25600 : 140000; generated.amp=0.5; generated.carHz=demoRate ? 1200 : 201200;
        const unsigned rate=demoRate ? 192000 : 1050000;
        std::vector<std::complex<float>> iq(demoRate ? 82300 : 450000);
        vdl2Render(generated,iq.data(),0,iq.size(),rate);
        for(const auto& z:iq) {
            const float pair[2]={z.real(),z.imag()};
            if(std::fwrite(pair,sizeof(float),2,stdout)!=2) return 1;
        }
        return 0;
    }
    for (auto frame : frames) {
        Vdl2Message expected;
        if (!vdl2FcsOk(frame.data(), frame.size()) ||
            !vdl2ParseFrame(frame.data(), frame.size() - 2, expected)) return 1;
        const auto actual = dsdr::vdl2::parse(frame.data(), frame.size());
        if (!actual || actual->source != expected.src || actual->destination != expected.dst ||
            actual->sourceType != expected.srcType || actual->destinationType != expected.dstType ||
            actual->fromAir != expected.fromAir || actual->fromGround != expected.fromGround ||
            actual->kind != expected.frameName || actual->protocol != expected.proto ||
            actual->summary != expected.summary) return 2;
        frame[4] ^= 0x40;
        if (vdl2FcsOk(frame.data(), frame.size()) || dsdr::vdl2::parse(frame.data(), frame.size())) return 3;
    }
    {
        dsdr::core::Vdl2ResultQueue queue(8);
        dsdr::vdl2::OfflineBurst old; old.generation=7; old.burst.frames=frames;
        auto fresh=old; fresh.generation=8;
        queue.publish(old); queue.publish(fresh);
        dsdr::core::Vdl2ResultQueue::Result result;
        for(const auto& frame:frames) {
            if(!queue.read(result,8) || result.generation!=8 || result.size!=frame.size() ||
               !std::equal(frame.begin(),frame.end(),result.bytes.begin())) return 37;
        }
        if(queue.read(result,8) || queue.dropped()) return 38;
        dsdr::core::Vdl2ResultQueue limited(1);
        limited.publish(fresh);
        if(limited.dropped()!=frames.size()-1 || !limited.read(result,8)) return 39;
    }
    // OnAir performs framing and burst encoding; compare our full reverse path
    // to the known originals, including the correctable header-bit case.
    const auto payload=vdl2HdlcBits(frames);
    auto bits=vdl2BurstBits(payload);
    const auto wave=vdl2MakeWave(frames);
    {
        auto mixed=frames; mixed[1][4]^=0x40; // invalid FCS in one frame
        auto expected=mixed; expected.erase(expected.begin()+1);
        auto generated=vdl2MakeWave(mixed);
        generated.start=1500; generated.amp=0.5;
        std::vector<std::complex<float>> iq(6000);
        vdl2Render(generated,iq.data(),0,iq.size(),105000);
        dect2::Vdl2Receiver reference;
        reference.setCenterHz(136975000); reference.setChannels({136975000}); reference.configure(105000);
        reference.feed(iq.data(),iq.size());
        Vdl2Telemetry telemetry; reference.telemetry(telemetry,0);
        const auto local=dsdr::vdl2::decodeOffline(iq.data(),iq.size(),105000,0.001);
        if(!local || local->burst.frames!=expected || telemetry.messages.size()!=expected.size() || telemetry.fcsBad<1) {
            std::fprintf(stderr,"mixed-FCS burst: local=%zu reference=%zu bad=%llu\n",local?local->burst.frames.size():0,telemetry.messages.size(),static_cast<unsigned long long>(telemetry.fcsBad));
            return 43;
        }
    }
    for(double carrier:{-1200.0,1200.0}) {
        auto generated=wave;
        generated.start=1500.0; generated.amp=0.5; generated.carHz=carrier;
        constexpr double sampleRate=105000;
        std::vector<std::complex<float>> iq(6000);
        vdl2Render(generated,iq.data(),0,iq.size(),sampleRate);
        // Search only the burst's neighbourhood; full streaming acquisition and
        // false-alarm characterisation remain outside this focused test.
        const auto found=dsdr::vdl2::findPreamble(iq.data()+1450,iq.size()-1450,sampleRate,0.001);
        if(!found || std::abs(double(found->sample)+1450-generated.start)>10 ||
           std::abs(found->carrierHz-carrier)>200) return 11;
        const auto decoded=dsdr::vdl2::decodeOffline(iq.data()+1450,iq.size()-1450,sampleRate,0.001);
        if(!decoded || decoded->burst.frames!=frames) return 13;
        if(dsdr::vdl2::decodeOffline(iq.data()+1450,250,sampleRate,0.001)) return 14;
    }
    // End-to-end A/B with both receivers on identical, finite IQ. Compare
    // individual frames; reference and experimental paths share protocol code.
    for(double carrier:{-1200.0,0.0,1200.0}) {
        constexpr double sampleRate=105000, center=136975000;
        auto generated=wave;
        generated.start=1500.0; generated.amp=0.5; generated.carHz=carrier;
        std::vector<std::complex<float>> iq(6000);
        vdl2Render(generated,iq.data(),0,iq.size(),sampleRate);
        unsigned randomState=0x315eca7u;
        auto random=[&](){randomState=randomState*1664525u+1013904223u; return float((randomState>>8)*(1.0/16777216.0)-0.5);};
        for(auto& s:iq) s+=std::complex<float>(random()*0.01f,random()*0.01f);
        dect2::Vdl2Receiver reference;
        reference.setCenterHz(center); reference.setChannels({center}); reference.configure(sampleRate);
        for(std::size_t p=0;p<iq.size();p+=1024) reference.feed(iq.data()+p,std::min(std::size_t(1024),iq.size()-p));
        Vdl2Telemetry telemetry; reference.telemetry(telemetry,0);
        auto local=dsdr::vdl2::decodeOffline(iq.data(),iq.size(),sampleRate,0.001);
        if(!local || local->burst.frames!=frames || telemetry.messages.size()!=frames.size()) {
            std::fprintf(stderr,"IQ A/B local=%zu reference=%zu expected=%zu\n",local?local->burst.frames.size():0,telemetry.messages.size(),frames.size());
            return 16;
        }
        for(const auto& frame:frames) {
            Vdl2Message expected;
            if(!vdl2ParseFrame(frame.data(),frame.size()-2,expected)) return 17;
            auto it=std::find_if(telemetry.messages.begin(),telemetry.messages.end(),[&](const Vdl2Message& m){
                return m.src==expected.src && m.dst==expected.dst && m.frameName==expected.frameName && m.proto==expected.proto;
            });
            if(it==telemetry.messages.end()) return 18;
        }
    }
    std::vector<std::complex<float>> silence(1000);
    if(dsdr::vdl2::findPreamble(silence.data(),silence.size(),105000,0.001)) return 12;
    if(dsdr::vdl2::decodeOffline(silence.data(),silence.size(),105000,0.001)) return 15;
    {
        constexpr double rate=105000,center=136975000;
        auto generated=wave; generated.start=20000; generated.amp=0.5;
        std::vector<std::complex<float>> iq(65536);
        vdl2Render(generated,iq.data(),0,iq.size(),rate);
        dect2::Vdl2Receiver reference;
        reference.setCenterHz(center); reference.setChannels({center}); reference.configure(rate);
        for(std::size_t p=0;p<iq.size();p+=1024) reference.feed(iq.data()+p,std::min(std::size_t(1024),iq.size()-p));
        Vdl2Telemetry telemetry; reference.telemetry(telemetry,0);
        std::vector<dsdr::vdl2::OfflineBurst> observed;
        dsdr::vdl2::WindowStream stream(rate,0.001,[&](const auto& b){observed.push_back(b);});
        for(std::size_t p=0;p<iq.size();p+=1024) stream.feed(iq.data()+p,std::min(std::size_t(1024),iq.size()-p));
        if(observed.size()!=1 || observed[0].burst.frames!=frames || telemetry.messages.size()!=frames.size()) {
            std::fprintf(stderr,"stream A/B callbacks=%zu reference=%zu expected=%zu\n",observed.size(),telemetry.messages.size(),frames.size());
            return 19;
        }
        observed.clear(); stream.reset();
        // A gap before burst completion must not be bridged into a false frame.
        const std::size_t split=20500;
        stream.feed(iq.data(),split); stream.reset();
        stream.feed(iq.data()+split,iq.size()-split);
        if(!observed.empty()) return 20;
        stream.reset();
        stream.feed(iq.data(),iq.size());
        if(observed.size()!=1 || observed[0].burst.frames!=frames) return 21;
    }
    {
        constexpr double rate=105000;
        auto generated=wave; generated.start=20000; generated.amp=0.5;
        std::vector<std::complex<float>> iq(65536);
        vdl2Render(generated,iq.data(),0,iq.size(),rate);
        std::vector<dsdr::vdl2::OfflineBurst> observed;
        dsdr::core::Vdl2AsyncStream async(rate,0.001,[&](const auto& b){observed.push_back(b);},128,4096);
        async.start();
        for(std::size_t p=0;p<iq.size();p+=1024)
            if(async.submit(iq.data()+p,std::min(std::size_t(1024),iq.size()-p))!=dsdr::core::AsyncIqDecoder::SubmitResult::Accepted) return 24;
        async.stop(true);
        if(observed.size()!=1 || observed[0].burst.frames!=frames || async.errors()!=0 || async.stats().droppedBlocks) return 25;
        observed.clear(); async.start();
        for(std::size_t p=0;p<20480;p+=1024)
            if(async.submit(iq.data()+p,1024)!=dsdr::core::AsyncIqDecoder::SubmitResult::Accepted) return 26;
        if(async.submit(iq.data()+20480,4097)!=dsdr::core::AsyncIqDecoder::SubmitResult::TooLarge) return 27;
        for(std::size_t p=24577;p<iq.size();p+=1024)
            if(async.submit(iq.data()+p,std::min(std::size_t(1024),iq.size()-p))!=dsdr::core::AsyncIqDecoder::SubmitResult::Accepted) return 28;
        async.stop(true);
        if(!observed.empty() || async.errors()!=0 || async.stats().droppedBlocks!=1) return 29;
    }
    {
        constexpr double rate=105000;
        auto generated=wave; generated.start=20000; generated.amp=0.5;
        std::vector<std::complex<float>> iq(65536);
        vdl2Render(generated,iq.data(),0,iq.size(),rate);
        struct Gate { std::atomic<bool> entered{false},release{false}; } gate;
        std::vector<dsdr::vdl2::OfflineBurst> observed;
        dsdr::core::Vdl2AsyncStream async(rate,0.001,[&](const auto& b){observed.push_back(b);},256,1024,
            [](void* p) noexcept { auto& g=*static_cast<Gate*>(p); g.entered=true; while(!g.release.load()) std::this_thread::yield(); },&gate);
        async.start();
        if(async.submit(iq.data(),1024)!=dsdr::core::AsyncIqDecoder::SubmitResult::Accepted) { gate.release=true; async.stop(false); return 34; }
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!gate.entered.load() && std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
        if(!gate.entered.load()) { gate.release=true; async.stop(false); return 35; }
        bool accepted=true;
        for(std::size_t p=1024;p<iq.size();p+=1024)
            accepted &= async.submit(iq.data()+p,1024)==dsdr::core::AsyncIqDecoder::SubmitResult::Accepted;
        async.discontinuity();
        for(std::size_t p=0;p<iq.size();p+=1024)
            accepted &= async.submit(iq.data()+p,1024)==dsdr::core::AsyncIqDecoder::SubmitResult::Accepted;
        gate.release=true; async.stop(true);
        if(!accepted || observed.size()!=1 || observed[0].burst.frames!=frames || async.errors()) return 36;
    }
    for(bool adjacent:{false,true}) {
        constexpr unsigned rate=1050000;
        constexpr double center=136775000, offset=200000;
        auto generated=wave; generated.start=140000; generated.amp=0.5; generated.carHz=offset+1200;
        std::vector<std::complex<float>> iq(450000);
        vdl2Render(generated,iq.data(),0,iq.size(),rate);
        if(adjacent) {
            const auto adjacentFrame=vdl2Frame({0x10a111,4,false},{0x400002,1,false},0x01,{});
            auto interferer=vdl2MakeWave({adjacentFrame});
            interferer.start=generated.start; interferer.amp=0.5; interferer.carHz=offset+25000;
            vdl2Render(interferer,iq.data(),0,iq.size(),rate);
        }
        dect2::Vdl2Receiver reference;
        reference.setCenterHz(center); reference.setChannels({center+offset}); reference.configure(rate);
        std::vector<dsdr::vdl2::OfflineBurst> observed;
        dsdr::core::Vdl2IqPipeline pipeline(rate,offset,0.001,[&](const auto& b){observed.push_back(b);},128);
        pipeline.start();
        for(std::size_t p=0;p<iq.size();p+=4096) {
            const auto n=std::min(std::size_t(4096),iq.size()-p);
            reference.feed(iq.data()+p,n); pipeline.process(iq.data()+p,n);
        }
        pipeline.stop(true);
        Vdl2Telemetry telemetry; reference.telemetry(telemetry,0);
        if(pipeline.stats().droppedBlocks || pipeline.errors()!=0 || observed.size()!=1 || observed[0].burst.frames!=frames ||
           telemetry.messages.size()!=frames.size()) {
            std::fprintf(stderr,"wideband IQ A/B adjacent=%d: local=%zu reference=%zu drops=%llu\n",int(adjacent),observed.size(),telemetry.messages.size(),static_cast<unsigned long long>(pipeline.stats().droppedBlocks));
            return 31;
        }
        if(!adjacent) {
            observed.clear(); pipeline.start(); pipeline.start();
            const std::size_t split=145000;
            pipeline.process(iq.data(),split);
            pipeline.discontinuity();
            pipeline.process(iq.data()+split,iq.size()-split);
            pipeline.stop(true);
            if(!observed.empty() || pipeline.stats().droppedBlocks || pipeline.errors()) return 32;
            pipeline.start(); pipeline.process(iq.data(),iq.size()); pipeline.stop(true);
            if(observed.size()!=1 || observed[0].burst.frames!=frames || pipeline.stats().droppedBlocks) return 33;
        }
    }
    {
        constexpr unsigned rate=1050000;
        constexpr double offset=200000;
        auto generated=wave; generated.start=140000; generated.amp=0.5; generated.carHz=offset+1200;
        std::vector<std::complex<float>> iq(450000);
        vdl2Render(generated,iq.data(),0,iq.size(),rate);
        dsdr::core::Vdl2Session session(rate,offset,0.001,16,128);
        session.start(); session.process(iq.data(),iq.size()); session.stop(true);
        const auto oldGeneration=session.generation();
        session.start();
        if(session.generation()==oldGeneration) return 40;
        session.process(iq.data(),iq.size()); session.stop(true);
        dsdr::core::Vdl2ResultQueue::Result result;
        for(const auto& frame:frames) {
            if(!session.read(result) || result.generation!=session.generation() || result.size!=frame.size() ||
               !std::equal(frame.begin(),frame.end(),result.bytes.begin())) return 41;
        }
        if(session.read(result) || session.droppedResults() || session.stats().droppedBlocks || session.errors()) return 42;
    }
    {
        constexpr double rate=105000,center=136975000;
        auto first=wave,second=wave;
        first.start=5000; first.amp=0.5; second.start=20000; second.amp=0.45;
        std::vector<std::complex<float>> iq(65536);
        vdl2Render(first,iq.data(),0,iq.size(),rate);
        vdl2Render(second,iq.data(),0,iq.size(),rate);
        dect2::Vdl2Receiver reference;
        reference.setCenterHz(center); reference.setChannels({center}); reference.configure(rate);
        std::vector<dsdr::vdl2::OfflineBurst> observed;
        dsdr::vdl2::WindowStream stream(rate,0.001,[&](const auto& b){observed.push_back(b);});
        for(std::size_t p=0;p<iq.size();p+=1024) {
            const auto n=std::min(std::size_t(1024),iq.size()-p);
            reference.feed(iq.data()+p,n); stream.feed(iq.data()+p,n);
        }
        Vdl2Telemetry telemetry; reference.telemetry(telemetry,0);
        if(observed.size()!=2 || observed[0].burst.frames!=frames || observed[1].burst.frames!=frames ||
           telemetry.messages.size()!=2*frames.size()) {
            std::fprintf(stderr,"two bursts: stream=%zu reference=%zu\n",observed.size(),telemetry.messages.size());
            return 22;
        }
    }
    {
        constexpr double rate=105000,center=136975000;
        auto damaged=wave,valid=wave;
        damaged.start=5000; damaged.amp=0.8;
        for(std::size_t i=19;i<damaged.ph.size();++i) damaged.ph[i]=0;
        valid.start=20000; valid.amp=0.4;
        std::vector<std::complex<float>> iq(65536);
        vdl2Render(damaged,iq.data(),0,iq.size(),rate);
        vdl2Render(valid,iq.data(),0,iq.size(),rate);
        dect2::Vdl2Receiver reference;
        reference.setCenterHz(center); reference.setChannels({center}); reference.configure(rate);
        std::vector<dsdr::vdl2::OfflineBurst> observed;
        dsdr::vdl2::WindowStream stream(rate,0.001,[&](const auto& b){observed.push_back(b);});
        for(std::size_t p=0;p<iq.size();p+=1024) {
            const auto n=std::min(std::size_t(1024),iq.size()-p);
            reference.feed(iq.data()+p,n); stream.feed(iq.data()+p,n);
        }
        Vdl2Telemetry telemetry; reference.telemetry(telemetry,0);
        if(observed.size()!=1 || observed[0].burst.frames!=frames || telemetry.messages.size()!=frames.size()) {
            std::fprintf(stderr,"damaged then valid: stream=%zu reference=%zu\n",observed.size(),telemetry.messages.size());
            return 23;
        }
    }
    const std::size_t symbols=(bits.size()+2)/3;
    // OnAir has 3 ramp symbols and 16 training symbols. Use the final
    // training symbol as the differential reference, omit the tail.
    std::vector<std::complex<float>> aligned;
    for(std::size_t i=18;i<19+symbols;++i)
        aligned.push_back(std::polar(0.5f,float(wave.ph[i])*0.7853981633974483f+0.37f));
    auto demod=dsdr::vdl2::decodeSymbols(aligned.data(),aligned.size());
    if(!demod || demod->size()<bits.size() || !std::equal(bits.begin(),bits.end(),demod->begin())) return 8;
    auto fromSymbols=dsdr::vdl2::decodeBurst(*demod);
    if(!fromSymbols || fromSymbols->frames!=frames) return 9;
    aligned[3]={0,0};
    if(dsdr::vdl2::decodeSymbols(aligned.data(),aligned.size())) return 10;
    auto burst=dsdr::vdl2::decodeBurst(bits);
    if(!burst || burst->frames!=frames || burst->headerCorrected) return 4;
    bits[4]^=1;
    burst=dsdr::vdl2::decodeBurst(bits);
    if(!burst || burst->frames!=frames || !burst->headerCorrected) return 5;
    bits.resize(24);
    if(dsdr::vdl2::decodeBurst(bits)) return 6;
    bits.assign(25,2);
    if(dsdr::vdl2::decodeBurst(bits)) return 7;
    std::printf("OnAir vs adapter: %zu valid and %zu corrupted frames agree\n", frames.size(), frames.size());
    std::printf("OnAir encoded burst: %zu frames recovered; header correction and invalid inputs checked\n",frames.size());
    std::printf("OnAir aligned D8PSK symbols: bits and frames matched; zero sample rejected\n");
    std::printf("OnAir IQ preamble: positive and negative carrier offsets acquired; silence rejected\n");
    std::printf("OnAir IQ -> offline acquisition, symbols and frames matched\n");
    std::printf("full noisy IQ window at three carrier offsets: OnAir and offline frames matched\n");
    std::printf("overlapping stream: matched reference, no duplicate, gap rejected, recovery matched\n");
    std::printf("two bursts within one window: stream and OnAir receiver matched\n");
    std::printf("damaged stronger burst then valid burst: stream and OnAir matched\n");
    std::printf("async IQ queue: valid burst matched, oversized-block gap rejected\n");
    std::printf("wideband shifted IQ with/without adjacent channel: FIR/mixer/async receiver matched OnAir\n");
    std::printf("wideband pipeline: interrupted burst rejected and restart recovered full burst\n");
    std::printf("blocked worker: queued pre-gap burst suppressed, post-gap burst recovered\n");
    std::printf("result queue: old generation discarded, fresh frames preserved, overflow counted\n");
    std::printf("IQ session to result queue: restart invalidated old frames; new frames matched\n");
    std::printf("mixed-FCS burst: valid frames preserved, damaged frame rejected by both receivers\n");
}
