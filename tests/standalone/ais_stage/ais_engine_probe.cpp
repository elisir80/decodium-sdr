// SPDX-License-Identifier: GPL-3.0-or-later
// TEST FIXTURE ONLY: adapt the existing filtered-IQ module ABI to AisReceiver.
// The module ABI does not report all source gaps and is not a production tap.
#include "core/IqModuleApi.h"
#include "core/AisReceiver.h"
#include "ais_engine_probe.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
namespace {
struct Probe {
    std::atomic<std::uint64_t> workerCalls{0};
    static void onWorker(void *context) noexcept { ++static_cast<Probe *>(context)->workerCalls; }
    dsdr::core::AisReceiver receiver;
    std::array<dsdr::dsp::Complex,4096> scratch{};
    double lastOffset=std::numeric_limits<double>::quiet_NaN();
    std::int64_t lastCenter=0;
    std::atomic<std::uint64_t> calls{0},frames{0};
    std::atomic<double> observedRate{0};
    dsdr_iq_module_v1 module{};
    Probe():receiver(64,4096,16,onWorker,this) { receiver.start(); receiver.setSource(true,48000,0); }
};
std::atomic<Probe *> active{nullptr};
void destroy(void *p) {
    auto *probe=static_cast<Probe *>(p); active.store(nullptr); delete probe;
}
void process(void *p,std::uint32_t channel,const float *iq,std::size_t count,double rate,
             std::int64_t center,double channelOffset) {
    auto &probe=*static_cast<Probe *>(p);
    ++probe.calls; probe.frames.fetch_add(count); probe.observedRate.store(rate);
    if(channel!=1 || rate!=48000 || !iq) return;
    if(probe.lastOffset!=channelOffset || probe.lastCenter!=center) {
        probe.receiver.discontinuity(); probe.lastOffset=channelOffset; probe.lastCenter=center;
    }
    auto generation=probe.receiver.generation();
    for(std::size_t at=0;at<count;) {
        const auto n=std::min(probe.scratch.size(),count-at);
        for(std::size_t i=0;i<n;++i) probe.scratch[i]={iq[2*(at+i)],iq[2*(at+i)+1]};
        if(probe.receiver.submitForGeneration(probe.scratch.data(),n,generation)!=dsdr::core::AsyncIqDecoder::SubmitResult::Accepted) break;
        at+=n;
    }
}
}
extern "C" dsdr_iq_module_v1 *dsdr_create_iq_module_v1() {
    auto *probe=new Probe;
    probe->module={DSDR_IQ_MODULE_ABI_VERSION,"ais-engine-test",probe,destroy,process};
    active.store(probe); return &probe->module;
}
extern "C" void dsdr_ais_probe_enable(bool enabled) {
    if(auto *p=active.load()) p->receiver.setEnabled(enabled);
}
extern "C" std::uint64_t dsdr_ais_probe_worker_calls() { auto *p=active.load(); return p?p->workerCalls.load():0; }
extern "C" std::uint64_t dsdr_ais_probe_accepted() { auto *p=active.load(); return p?p->receiver.stats().accepted:0; }
extern "C" bool dsdr_ais_probe_fence() {
    auto *p=active.load(); if(!p)return false;
    const dsdr::dsp::Complex sample{0,0};
    return p->receiver.submit(&sample,1)==dsdr::core::AsyncIqDecoder::SubmitResult::Accepted;
}
extern "C" std::uint64_t dsdr_ais_probe_calls() { auto *p=active.load(); return p?p->calls.load():0; }
extern "C" std::uint64_t dsdr_ais_probe_frames() { auto *p=active.load(); return p?p->frames.load():0; }
extern "C" double dsdr_ais_probe_rate() { auto *p=active.load(); return p?p->observedRate.load():0; }
extern "C" bool dsdr_ais_probe_read(AisProbeMessage *output) {
    auto *p=active.load(); if(!p || !output) return false;
    dsdr::core::AisReceiver::Message m;
    if(!p->receiver.read(m)) return false;
    *output={m.mmsi,m.type,m.hasPosition,m.latitude,m.longitude}; return true;
}
