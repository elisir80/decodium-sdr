// SPDX-License-Identifier: GPL-3.0-or-later
// Isolated ingress rehearsal; this does not instantiate DspEngine.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "async_fixture.h"
#include "core/PocsagEngineIngress.h"
#include "dsp/SpscRing.h"
#include <cassert>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace dsdr;
static void feed(core::PocsagEngineIngress &tap, dsp::SpscRing<float> &ring,
                 const std::vector<dsp::Complex> &iq, std::size_t begin, std::size_t end,
                 bool enabled = true) {
    std::vector<float> raw(2*257), out(2*257);
    while (begin < end) {
        const auto n = std::min<std::size_t>(257, end-begin);
        for (std::size_t i=0;i<n;++i) { raw[2*i]=iq[begin+i].real(); raw[2*i+1]=iq[begin+i].imag(); }
        assert(ring.write(raw.data(),2*n)==2*n);
        const auto generation=tap.snapshot();
        const auto first=ring.totalRead()/2;
        assert(ring.read(out.data(),2*n)==2*n);
        if (enabled) tap.process(out.data(),n,first,generation);
        begin+=n;
    }
}
static void check(core::PocsagReceiver &rx, unsigned count) {
    core::PocsagReceiver::Message message;
    for (unsigned i=0;i<count;++i) {
        assert(rx.read(message));
        assert(message.address==1234563 && message.function==3);
        assert(std::string(message.text.data(),message.size)=="HELLO PAGER");
    }
    assert(!rx.read(message));
}
int main() {
    for (const unsigned rate : {48000u, 96000u}) {
        const unsigned baud=rate==48000 ? 2400 : 1200;
        const auto iq=generate(rate,baud,2600,0.01,19);
        assert(expected(decode(iq,rate,baud,2000,257)));
        core::PocsagReceiver rx(2048,1024,16);
        assert(rx.configure(rate,baud,2000)); assert(rx.start());
        dsp::SpscRing<float> ring(4096); core::PocsagEngineIngress tap(rx);
        feed(tap,ring,iq,0,iq.size(),false); // A: no feed
        feed(tap,ring,iq,0,iq.size());       // B: live IQ
        rx.stop(true); check(rx,1);
        assert(rx.start());
        const auto split=std::size_t(850)*rate/baud;
        feed(tap,ring,iq,0,split);
        // A pre-loss snapshot used after notification must never be accepted as
        // current, even when its block is submitted after the boundary.
        const auto stale=tap.snapshot();
        tap.onLoss(ring.totalWritten()/2);
        float dummy[2]={0,0}; tap.process(dummy,1,ring.totalRead()/2,stale);
        feed(tap,ring,iq,split+4*rate/baud,iq.size());
        feed(tap,ring,iq,0,iq.size());
        rx.stop(true); check(rx,1);
        assert(rx.stats().droppedBlocks==0);
    }
    {
        struct Gate {
            std::atomic<bool> entered{false}, release{false};
            static void hold(void *p) noexcept {
                auto &gate=*static_cast<Gate *>(p);
                gate.entered.store(true);
                while (!gate.release.load()) std::this_thread::yield();
            }
        } gate;
        core::PocsagReceiver rx(2048,1024,16,Gate::hold,&gate);
        assert(rx.configure(48000,2400,2000)); assert(rx.start());
        dsp::SpscRing<float> ring(4096); core::PocsagEngineIngress tap(rx);
        const auto iq=generate(48000,2400,2600,0.01,19);
        feed(tap,ring,iq,0,iq.size());
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while (!gate.entered.load()) {
            if (std::chrono::steady_clock::now()>deadline) { gate.release=true; assert(false); }
            std::this_thread::yield();
        }
        // A buffered pre-loss block is completely discarded, including a
        // crossing suffix, and queued work cannot leak after invalidation.
        float buffered[200]{};
        assert(ring.write(buffered,200)==200);
        const auto stale=tap.snapshot(), first=ring.totalRead()/2;
        tap.onLoss(ring.totalWritten()/2);
        assert(ring.read(buffered,200)==200);
        tap.process(buffered,100,first,stale);
        assert(tap.discardedFrames()==100);
        gate.release=true;
        feed(tap,ring,iq,0,iq.size());
        rx.stop(true); check(rx,1);
    }
}
