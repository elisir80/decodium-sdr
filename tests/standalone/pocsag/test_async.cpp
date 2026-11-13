// SPDX-License-Identifier: GPL-3.0-or-later
#include "async_fixture.h"
#include "core/PocsagReceiver.h"
#include <atomic>
#include <chrono>
#include <thread>
using namespace std::chrono_literals;
using dsdr::core::PocsagReceiver;
using Result = dsdr::core::AsyncIqDecoder::SubmitResult;
struct Gate { std::atomic<bool> block{true}, entered{false}; std::atomic<int> calls{0}; };
static void hook(void *p) noexcept {
    auto *g=static_cast<Gate*>(p); ++g->calls; g->entered=true;
    while(g->block.load()) std::this_thread::yield();
}
template<class F> static void waitFor(F f) {
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while (!f()) { assert(std::chrono::steady_clock::now()<deadline); std::this_thread::sleep_for(1ms); }
}
static void submitAll(PocsagReceiver &r, const std::vector<dsdr::dsp::Complex> &iq, std::size_t from=0) {
    for (auto i=from;i<iq.size();) { auto n=std::min<std::size_t>(1024,iq.size()-i); assert(r.submit(iq.data()+i,n)==Result::Accepted); i+=n; }
}
static void verify(const PocsagReceiver::Message &m, const PocsagDecoder::Message &ref) {
    assert(m.address==ref.address && m.function==ref.function && m.correctedBits==ref.correctedBits);
    assert(std::string(m.text.data(),m.size)==ref.alpha);
}
int main() {
    auto iq=generate(96000,1200,2600,0.05,7);
    auto reference=decode(iq,96000,1200,2000,257); assert(expected(reference));
    {
        PocsagReceiver rx(512,1024,4); assert(rx.configure(96000,1200,2000)); assert(rx.start());
        assert(!rx.configure(96000,1200,3000));
        assert(rx.submit(nullptr,1)==Result::Invalid);
        assert(rx.submit(iq.data(),1025)==Result::TooLarge);
        submitAll(rx,iq); rx.stop(true);
        PocsagReceiver::Message m; assert(rx.read(m)); verify(m,reference[0]); assert(!rx.read(m));
        assert(rx.stats().droppedBlocks==2 && rx.workerErrors()==0);
        assert(rx.configure(96000,1200,-3000)); assert(rx.start());
        auto shifted=generate(96000,1200,-2400,0.05,8);
        submitAll(rx,shifted); rx.stop(true); assert(rx.read(m)); verify(m,reference[0]);
        assert(rx.start()); submitAll(rx,shifted); rx.stop(true);
        assert(rx.configure(48000,2400,0)); assert(!rx.read(m));
        assert(rx.start()); auto second=generate(48000,2400,600,0.05,13);
        submitAll(rx,second); rx.stop(true); assert(rx.read(m)); verify(m,reference[0]);
    }
    {
        Gate gate; PocsagReceiver rx(2,1024,4,hook,&gate);
        assert(rx.configure(96000,1200,2000)); assert(rx.start());
        assert(rx.submit(iq.data(),1024)==Result::Accepted); waitFor([&]{return gate.entered.load();});
        assert(rx.submit(iq.data()+1024,1024)==Result::Accepted);
        assert(rx.submit(iq.data()+2048,1024)==Result::Full);
        gate.block=false; waitFor([&]{return gate.calls.load()>=2;});
        std::this_thread::sleep_for(10ms);
        // A missing span forces reset on the next accepted block.
        for(std::size_t pos=3072;pos<iq.size();) {
            auto n=std::min<std::size_t>(1024,iq.size()-pos);
            auto res=rx.submit(iq.data()+pos,n);
            if(res==Result::Accepted)pos+=n; else {assert(res==Result::Full); std::this_thread::sleep_for(2ms);}
        }
        rx.stop(true); PocsagReceiver::Message m; assert(!rx.read(m)); assert(rx.stats().droppedBlocks>0); assert(rx.resetCount()>=2);
        assert(rx.start());
        for(std::size_t pos=0;pos<iq.size();) {
            auto n=std::min<std::size_t>(1024,iq.size()-pos);
            auto res=rx.submit(iq.data()+pos,n);
            if(res==Result::Accepted)pos+=n; else {assert(res==Result::Full); std::this_thread::sleep_for(2ms);}
        }
        rx.stop(true);
        // Retrying after a full queue itself denotes data loss; no guarantee of decode.
    }
    {
        PocsagReceiver rx(512,1024,4); assert(rx.configure(96000,1200,2000)); assert(rx.start());
        const std::size_t split=850*80;
        for(std::size_t pos=0;pos<split;) {auto n=std::min<std::size_t>(1024,split-pos); assert(rx.submit(iq.data()+pos,n)==Result::Accepted); pos+=n;}
        rx.discontinuity(); submitAll(rx,iq,split); submitAll(rx,iq); rx.stop(true);
        PocsagReceiver::Message m; assert(rx.read(m)); verify(m,reference[0]);
        assert(!rx.read(m) && rx.resetCount()>=2);
    }
    {
        PocsagReceiver rx(1024,1024,1); assert(rx.configure(96000,1200,2000)); assert(rx.start());
        submitAll(rx,iq); submitAll(rx,iq); rx.stop(true);
        PocsagReceiver::Message m; assert(rx.read(m)); verify(m,reference[0]);
        assert(!rx.read(m) && rx.droppedMessages()==1);
    }
    {
        Gate gate; PocsagReceiver rx(512,1024,4,hook,&gate);
        assert(rx.configure(96000,1200,2000)); assert(rx.start());
        assert(rx.submit(iq.data(),1024)==Result::Accepted); waitFor([&]{return gate.entered.load();});
        submitAll(rx,iq,1024); rx.discontinuity(); submitAll(rx,iq);
        gate.block=false; rx.stop(true);
        PocsagReceiver::Message m; assert(rx.read(m)); verify(m,reference[0]); assert(!rx.read(m));
    }
    {
        Gate gate; PocsagReceiver rx(2,1024,1,hook,&gate);
        assert(rx.configure(96000,1200,2000)); assert(rx.start());
        assert(rx.submit(iq.data(),1024)==Result::Accepted); waitFor([&]{return gate.entered.load();});
        std::atomic<bool> stopped{false}; std::thread stopper([&]{rx.stop(false); stopped=true;});
        std::this_thread::sleep_for(10ms); assert(!stopped.load()); gate.block=false; stopper.join(); assert(stopped.load());
        PocsagReceiver::Message m; assert(!rx.read(m));
    }
    return 0;
}
