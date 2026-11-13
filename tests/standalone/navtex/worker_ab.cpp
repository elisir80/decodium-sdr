// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/NavtexReceiver.h"
#include "dect2/marine/marine_navtex.h"
#include "dect2/marine/marine_dsp.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>
using C=dsdr::dsp::Complex;
using R=dsdr::core::NavtexReceiver;
struct Progress { std::atomic<size_t> blocks{0}; static void mark(void* p) noexcept { ++static_cast<Progress*>(p)->blocks; } };
struct Gate {
    std::atomic<bool> held{true}, entered{false}; std::atomic<size_t> blocks{0};
    static void wait(void* p) noexcept {
        auto& g=*static_cast<Gate*>(p); g.entered=true;
        while(g.held.load()) std::this_thread::yield();
        ++g.blocks;
    }
};
int main() {
    constexpr double pi=3.14159265358979323846;
    std::vector<int> tokens(40,-1);
    for(auto c:dect2::marine::navtexEncode("ZCZC EA01\nTEST MESSAGE 123\nNNNN\n")) tokens.push_back(c);
    tokens.insert(tokens.end(),40,-1);
    auto bits=dect2::marine::codesToBits(dect2::marine::sitorBSlots(tokens));
    std::vector<C> audio(1874); double phase=.73;
    for(size_t i=0;i<bits.size()*480;++i) {
        phase+=2*pi*(1000+(bits[i/480]?85:-85))/48000;
        audio.emplace_back(float(.65*std::sin(phase)),0);
    }
    audio.insert(audio.end(),4800,C{});
    std::vector<C> reference; for(size_t i=0;i<audio.size();i+=2) reference.push_back(audio[i]);
    dect2::marine::FskSlicer slicer; slicer.setCentreHz(1000);
    dect2::marine::NavtexDecoder ref(false),inv(true); std::vector<dect2::NavtexMessage> seen;
    auto cb=[&](const auto& m){if(m.complete) seen.push_back(m);}; ref.setCallback(cb); inv.setCallback(cb);
    slicer.process(reference.data(),reference.size(),[&](float v){ref.pushBit(v); inv.pushBit(v);});
    if(seen.size()!=1) return 1;
    Progress progress; R worker(2048,1024,8,Progress::mark,&progress); worker.start();
    const auto old=worker.generation();
    if(worker.submitForGeneration(audio.data(),100,old)!=dsdr::core::AsyncIqDecoder::SubmitResult::Stopped) return 1;
    worker.setEnabled(true); worker.selectChannel(1);
    const auto initial=worker.generation();
    // Queue only the first half, invalidate it, then queue a complete call.
    size_t firstBlocks=0;
    for(size_t i=0;i<audio.size()/2;i+=1024) {
        const auto n=std::min(size_t(1024),audio.size()/2-i);
        if(worker.submitForGeneration(audio.data()+i,n,initial)!=dsdr::core::AsyncIqDecoder::SubmitResult::Accepted) return 1;
        ++firstBlocks;
    }
    const auto firstDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(progress.blocks.load()<firstBlocks) {
        if(std::chrono::steady_clock::now()>firstDeadline) return 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    R::Message prior;
    if(worker.readOne(prior)!=R::ReadResult::Empty) return 1;
    worker.discontinuity(); const auto next=worker.generation();
    if(worker.submitForGeneration(audio.data(),100,initial)!=dsdr::core::AsyncIqDecoder::SubmitResult::Stopped) return 1;
    for(size_t i=0;i<audio.size();i+=1024) {
        const auto n=std::min(size_t(1024),audio.size()-i);
        if(worker.submitForGeneration(audio.data()+i,n,next)!=dsdr::core::AsyncIqDecoder::SubmitResult::Accepted) return 1;
    }
    R::Message m; bool got=false;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(std::chrono::steady_clock::now()<deadline) {
        if(worker.readOne(m)==R::ReadResult::Message) { got=true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const bool match=got && m.header.data()==seen[0].header && m.text.data()==seen[0].text && !m.truncated;
    worker.setEnabled(false);
    const bool disabled=worker.submitForGeneration(audio.data(),100,next)==dsdr::core::AsyncIqDecoder::SubmitResult::Stopped;
    worker.stop();
    std::printf("OnAir=%zu worker=%d match=%d stale-rejected=1 disabled=%d dropped=%llu\n",seen.size(),got,match,disabled,
                static_cast<unsigned long long>(worker.stats().droppedBlocks));
    if(!match || !disabled || worker.stats().droppedBlocks!=0) return 1;
    // A: an overloaded queue must invalidate the partial message. B: after
    // the worker catches up, the same complete call must match OnAir again.
    Gate gate; R saturated(4,1024,8,Gate::wait,&gate); saturated.start();
    saturated.setEnabled(true); saturated.selectChannel(1);
    const auto before=saturated.generation();
    if(saturated.submitForGeneration(audio.data(),1024,before)!=dsdr::core::AsyncIqDecoder::SubmitResult::Accepted) return 1;
    const auto timeout=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(!gate.entered.load() && std::chrono::steady_clock::now()<timeout) std::this_thread::yield();
    if(!gate.entered.load()) { gate.held=false; saturated.stop(); return 1; }
    bool full=false; size_t accepted=1;
    for(int i=0;i<16;++i) {
        const auto r=saturated.submitForGeneration(audio.data()+1024,1024,before);
        if(r==dsdr::core::AsyncIqDecoder::SubmitResult::Full) { full=true; break; }
        if(r!=dsdr::core::AsyncIqDecoder::SubmitResult::Accepted) break;
        ++accepted;
    }
    gate.held=false;
    while(gate.blocks.load()<accepted && std::chrono::steady_clock::now()<timeout) std::this_thread::yield();
    if(!full || saturated.generation()==before || saturated.stats().droppedBlocks==0) { saturated.stop(); return 1; }
    R::Message stale;
    if(saturated.readOne(stale)==R::ReadResult::Message) { saturated.stop(); return 1; }
    auto generation=saturated.generation(); size_t processed=gate.blocks.load();
    for(size_t i=0;i<audio.size();i+=1024) {
        const auto n=std::min(size_t(1024),audio.size()-i);
        if(saturated.submitForGeneration(audio.data()+i,n,generation)!=dsdr::core::AsyncIqDecoder::SubmitResult::Accepted) { saturated.stop(); return 1; }
        ++processed;
        while(gate.blocks.load()<processed && std::chrono::steady_clock::now()<timeout) std::this_thread::yield();
        if(gate.blocks.load()<processed) { saturated.stop(); return 1; }
    }
    R::Message recovered; bool delivered=false;
    while(std::chrono::steady_clock::now()<timeout) {
        if(saturated.readOne(recovered)==R::ReadResult::Message) { delivered=true; break; }
        std::this_thread::yield();
    }
    const bool recovery=delivered && recovered.header.data()==seen[0].header && recovered.text.data()==seen[0].text;
    saturated.stop();
    std::printf("NAVTEX queue full=%d stale-rejected=1 recovery-matches-OnAir=%d\n",full,recovery);
    return recovery ? 0 : 1;
}
