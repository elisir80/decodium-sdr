// SPDX-License-Identifier: GPL-3.0-or-later
// Experimental reference receiver through Decodium's bounded IQ queue.
#include "core/AsyncIqDecoder.h"
#include "dect2/vdl2/vdl2_gen.h"
#include "dect2/vdl2/vdl2_proto.h"
#include "dect2/vdl2/vdl2_rx.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace {
constexpr double rate=1000000, center=136975000;
using C=dsdr::dsp::Complex;
struct Worker {
    dect2::Vdl2Receiver rx;
    std::atomic<size_t> handled{0};
    dect2::Vdl2Telemetry telemetry;
    Worker() { rx.setCenterHz(center); rx.setChannels({center}); rx.configure(rate); }
    static void reset(void* p) noexcept { static_cast<Worker*>(p)->rx.reset(); }
    static void feed(void* p,const C* samples,size_t n) noexcept {
        auto& w=*static_cast<Worker*>(p);
        w.rx.feed(samples,n);
        w.rx.telemetry(w.telemetry,0);
        w.handled.fetch_add(n,std::memory_order_release);
    }
};
std::vector<C> frame(double& start, double& end, double carrier=0) {
    std::vector<C> iq(size_t(rate*1.5));
    auto f=dect2::vdl2Frame({0x10a000,4,false},{0x400001,1,false},
              dect2::vdl2ControlI(1,0,false),
              dect2::vdl2AcarsInfo("N123AB","H1",'1',"SYNTHETIC TEST MESSAGE"));
    auto wave=dect2::vdl2MakeWave({f});
    wave.start=rate*.3; wave.amp=.5; wave.carHz=carrier;
    start=wave.start; end=wave.endSample(rate);
    dect2::vdl2Render(wave,iq.data(),0,iq.size(),rate);
    return iq;
}
bool run(bool signal,double carrier=0,float noise=0) {
    std::vector<C> iq(size_t(rate*1.5));
    if(signal) {
        double start,end; iq=frame(start,end,carrier);
    }
    // Deterministic complex noise keeps both paths on exactly the same input.
    unsigned state=0x7ac3141u;
    auto random=[&]() { state=state*1664525u+1013904223u; return float((state>>8)*(1.0/16777216.0)-0.5); };
    if(noise>0) for(auto &s:iq) s+=C(noise*random(),noise*random());
    dect2::Vdl2Receiver direct; direct.setCenterHz(center); direct.setChannels({center}); direct.configure(rate);
    Worker worker;
    dsdr::core::AsyncIqDecoder queue(16,4096,{&worker,Worker::reset,Worker::feed});
    queue.start();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    for(size_t i=0;i<iq.size();i+=4096) {
        size_t n=std::min(size_t(4096),iq.size()-i);
        direct.feed(iq.data()+i,n);
        // Deterministic pacing: no overload is intended in this comparison.
        while(worker.handled.load(std::memory_order_acquire)<i) {
            if(std::chrono::steady_clock::now()>deadline) { queue.stop(false); return false; }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(queue.submit(iq.data()+i,n)!=dsdr::core::AsyncIqDecoder::SubmitResult::Accepted) {
            queue.stop(false); return false;
        }
    }
    queue.stop(true);
    dect2::Vdl2Telemetry reference; direct.telemetry(reference,0);
    const auto& a=reference.messages; const auto& b=worker.telemetry.messages;
    bool same=a.size()==b.size();
    for(size_t i=0;same && i<a.size();++i)
        same=a[i].src==b[i].src && a[i].dst==b[i].dst && a[i].freqHz==b[i].freqHz;
    std::printf("%s carrier=%.0f noise=%.3f: direct=%zu queued=%zu drops=%llu same=%d\n",signal?"signal":"silence",carrier,noise,a.size(),b.size(),
                static_cast<unsigned long long>(queue.stats().droppedBlocks),same);
    return same && (signal ? !a.empty() : a.empty()) && queue.stats().droppedBlocks==0;
}
bool gapAndRecovery() {
    double start,end; const auto iq=frame(start,end);
    const size_t split=size_t((start+end)*.5), missing=4097;
    if(split+missing>=iq.size()) return false;
    dect2::Vdl2Receiver direct; direct.setCenterHz(center); direct.setChannels({center}); direct.configure(rate);
    Worker worker;
    dsdr::core::AsyncIqDecoder queue(16,4096,{&worker,Worker::reset,Worker::feed});
    queue.start(); size_t sent=0;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    auto feed=[&](size_t first,size_t last) {
        for(size_t i=first;i<last;i+=4096) {
            const size_t n=std::min(size_t(4096),last-i);
            direct.feed(iq.data()+i,n);
            while(worker.handled.load(std::memory_order_acquire)<sent) {
                if(std::chrono::steady_clock::now()>deadline) return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if(queue.submit(iq.data()+i,n)!=dsdr::core::AsyncIqDecoder::SubmitResult::Accepted) return false;
            sent+=n;
        }
        return true;
    };
    bool ok=feed(0,split);
    direct.reset();
    // Rejection must automatically mark a gap before the next accepted block.
    const auto rejectedBlock=queue.submit(iq.data()+split,4097);
    ok=ok && rejectedBlock==dsdr::core::AsyncIqDecoder::SubmitResult::TooLarge;
    ok=ok && feed(split+missing,iq.size());
    while(ok && worker.handled.load(std::memory_order_acquire)<sent) {
        if(std::chrono::steady_clock::now()>deadline) { ok=false; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    dect2::Vdl2Telemetry partial; direct.telemetry(partial,0);
    const bool rejected=partial.messages.empty() && worker.telemetry.messages.empty();
    direct.reset(); queue.discontinuity();
    ok=ok && feed(0,iq.size());
    queue.stop(true);
    dect2::Vdl2Telemetry recovered; direct.telemetry(recovered,0);
    const bool decoded=recovered.messages.size()==1 && worker.telemetry.messages.size()==1 &&
        recovered.messages[0].src==worker.telemetry.messages[0].src &&
        recovered.messages[0].dst==worker.telemetry.messages[0].dst;
    std::printf("gap: partial rejected=%d recovery matched=%d drops=%llu\n",rejected,decoded,
                static_cast<unsigned long long>(queue.stats().droppedBlocks));
    return ok && rejected && decoded && queue.stats().droppedBlocks==1 && queue.stats().droppedSamples==4097;
}
}
int main() { return run(false) && run(true) && run(true,3200,0.03f) && run(true,-3200,0.03f) && gapAndRecovery() ? 0 : 1; }
