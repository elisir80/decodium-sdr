// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core/SelcalReceiver.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <thread>
#include <atomic>
#include <vector>
using namespace dsdr;
struct Progress { std::atomic<std::size_t> count{0}; static void mark(void *p) noexcept { ++static_cast<Progress *>(p)->count; } };
struct Gate {
    std::atomic<bool> held{false},entered{false}; std::atomic<std::size_t> count{0};
    static void mark(void *p) noexcept {
        auto &gate=*static_cast<Gate *>(p); gate.entered=true;
        while(gate.held.load()) std::this_thread::yield();
        ++gate.count;
    }
};
int main() {
    constexpr double pi=3.14159265358979323846;
    std::vector<dsp::Complex> samples(48000*3);
    for(std::size_t i=0;i<samples.size();++i) {
        const double t=double(i)/48000; double v=0;
        if(t>=.2 && t<1.2) v=.3*(std::sin(2*pi*312.6*t)+std::sin(2*pi*977.2*t));
        if(t>=1.4 && t<2.4) v=.3*(std::sin(2*pi*384.6*t)+std::sin(2*pi*1083.9*t));
        samples[i]={float(v),0};
    }
    core::SelcalReceiver receiver(1024,512,16); receiver.start();
    const auto send=[&] {
        const auto generation=receiver.generation();
        for(std::size_t at=0;at<samples.size();at+=512) {
            const auto n=std::min<std::size_t>(512,samples.size()-at);
            assert(receiver.submitForGeneration(samples.data()+at,n,generation)==core::AsyncIqDecoder::SubmitResult::Accepted);
        }
    };
    core::SelcalReceiver::Message msg;
    assert(receiver.submitForGeneration(samples.data(),512,receiver.generation())==core::AsyncIqDecoder::SubmitResult::Stopped);
    receiver.setEnabled(true); send();
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(receiver.readOne(msg)!=core::SelcalReceiver::ReadResult::Message) {
        assert(std::chrono::steady_clock::now()<end); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert((msg.code==std::array<char,5>{'A','M','-','C','P'}));
    const auto old=receiver.generation(); receiver.discontinuity();
    assert(receiver.submitForGeneration(samples.data(),512,old)==core::AsyncIqDecoder::SubmitResult::Stopped);
    receiver.setEnabled(false); assert(receiver.submitForGeneration(samples.data(),512,receiver.generation())==core::AsyncIqDecoder::SubmitResult::Stopped);
    receiver.stop();
    // Run the first tone pair to completion, then split the call with either
    // an IQ gap or a selected-channel change. The second half must not join
    // the old first half; a fresh complete call must work afterwards.
    for(bool channelChange:{false,true}) {
        Progress progress; core::SelcalReceiver r(1024,512,16,Progress::mark,&progress);
        r.start(); r.setEnabled(true); r.selectChannel(1);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        std::size_t submitted=0;
        auto sendRange=[&](std::size_t begin,std::size_t finish) {
            const auto g=r.generation();
            for(std::size_t at=begin;at<finish;at+=512) {
                const auto n=std::min<std::size_t>(512,finish-at);
                assert(r.submitForGeneration(samples.data()+at,n,g)==core::AsyncIqDecoder::SubmitResult::Accepted);
                ++submitted;
            }
            // The marker entering the worker means earlier blocks completed.
            const dsp::Complex marker{};
            assert(r.submitForGeneration(&marker,1,g)==core::AsyncIqDecoder::SubmitResult::Accepted); ++submitted;
            while(progress.count.load()<submitted) { assert(std::chrono::steady_clock::now()<deadline); std::this_thread::yield(); }
        };
        const std::size_t split=std::size_t(48000*1.3);
        sendRange(0,split);
        core::SelcalReceiver::Message m;
        assert(r.readOne(m)!=core::SelcalReceiver::ReadResult::Message);
        const auto previous=r.generation();
        if(channelChange) r.selectChannel(2); else r.discontinuity();
        assert(r.generation()!=previous);
        sendRange(split,samples.size());
        assert(r.readOne(m)!=core::SelcalReceiver::ReadResult::Message);
        sendRange(0,samples.size());
        while(r.readOne(m)!=core::SelcalReceiver::ReadResult::Message) {
            assert(std::chrono::steady_clock::now()<deadline); std::this_thread::yield();
        }
        assert((m.code==std::array<char,5>{'A','M','-','C','P'}));
        r.stop();
    }
    // Hold the consumer to force an actual queue overflow. Its old generation
    // must be invalidated; after draining, a complete fresh call must decode.
    Gate gate; core::SelcalReceiver overloaded(4,512,16,Gate::mark,&gate);
    overloaded.start(); overloaded.setEnabled(true); overloaded.selectChannel(1);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    const auto oldGeneration=overloaded.generation();
    const std::size_t split=std::size_t(48000*1.3); std::size_t processed=0;
    for(std::size_t at=0;at<split;at+=512) {
        const auto n=std::min<std::size_t>(512,split-at);
        assert(overloaded.submitForGeneration(samples.data()+at,n,oldGeneration)==core::AsyncIqDecoder::SubmitResult::Accepted);
        ++processed;
        while(gate.count.load()<processed) { assert(std::chrono::steady_clock::now()<deadline); std::this_thread::yield(); }
    }
    gate.entered=false; gate.held=true;
    assert(overloaded.submitForGeneration(samples.data()+split,512,oldGeneration)==core::AsyncIqDecoder::SubmitResult::Accepted);
    while(!gate.entered.load()) { assert(std::chrono::steady_clock::now()<deadline); std::this_thread::yield(); }
    std::size_t accepted=processed+1; bool full=false;
    for(int i=0;i<16;++i) {
        const auto result=overloaded.submitForGeneration(samples.data()+512,512,oldGeneration);
        if(result==core::AsyncIqDecoder::SubmitResult::Full) { full=true; break; }
        if(result!=core::AsyncIqDecoder::SubmitResult::Accepted) break;
        ++accepted;
    }
    gate.held=false;
    while(gate.count.load()<accepted) { assert(std::chrono::steady_clock::now()<deadline); std::this_thread::yield(); }
    assert(full && overloaded.generation()!=oldGeneration && overloaded.stats().droppedBlocks>0);
    core::SelcalReceiver::Message stale;
    assert(overloaded.readOne(stale)!=core::SelcalReceiver::ReadResult::Message);
    const auto next=overloaded.generation(); processed=gate.count.load();
    for(std::size_t at=split+512;at<samples.size();at+=512) {
        const auto n=std::min<std::size_t>(512,samples.size()-at);
        assert(overloaded.submitForGeneration(samples.data()+at,n,next)==core::AsyncIqDecoder::SubmitResult::Accepted);
        ++processed;
        while(gate.count.load()<processed) { assert(std::chrono::steady_clock::now()<deadline); std::this_thread::yield(); }
    }
    const dsp::Complex marker{};
    assert(overloaded.submitForGeneration(&marker,1,next)==core::AsyncIqDecoder::SubmitResult::Accepted); ++processed;
    while(gate.count.load()<processed) { assert(std::chrono::steady_clock::now()<deadline); std::this_thread::yield(); }
    assert(overloaded.readOne(stale)!=core::SelcalReceiver::ReadResult::Message);
    for(std::size_t at=0;at<samples.size();at+=512) {
        const auto n=std::min<std::size_t>(512,samples.size()-at);
        assert(overloaded.submitForGeneration(samples.data()+at,n,next)==core::AsyncIqDecoder::SubmitResult::Accepted);
        ++processed;
        while(gate.count.load()<processed) { assert(std::chrono::steady_clock::now()<deadline); std::this_thread::yield(); }
    }
    while(overloaded.readOne(stale)!=core::SelcalReceiver::ReadResult::Message) {
        assert(std::chrono::steady_clock::now()<deadline); std::this_thread::yield();
    }
    assert((stale.code==std::array<char,5>{'A','M','-','C','P'}));
    overloaded.stop();
}
