// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core/AisReceiver.h"
#include "core/AisController.h"
#include "ais_synthetic.h"
#include <QCoreApplication>
#include <cassert>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
using Receiver=dsdr::core::AisReceiver;
using Result=dsdr::core::AsyncIqDecoder::SubmitResult;
using Decoder=dsdr::dsp::AisDecoder;
struct Gate {
    std::atomic<bool> block{false}, entered{false};
    std::atomic<unsigned> calls{0};
    static void hook(void *p) noexcept {
        auto &g=*static_cast<Gate *>(p); ++g.calls;
        if(!g.block.load()) return;
        g.entered.store(true); while(g.block.load()) std::this_thread::yield();
    }
    ~Gate(){ block.store(false); }
};
template<class F> void waitFor(F f) {
    const auto end=std::chrono::steady_clock::now()+5s;
    while(!f()) { assert(std::chrono::steady_clock::now()<end); std::this_thread::sleep_for(1ms); }
}
void directCapture(void *p,const Decoder::Message &m) { static_cast<std::vector<Decoder::Message> *>(p)->push_back(m); }
void submit(Receiver &r,const std::vector<std::complex<float>> &v) { assert(r.submit(v.data(),v.size())==Result::Accepted); }
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    auto payload=ais_test::position();
    auto good=ais_test::transmit(payload,48000,0,0.025,3,false);
    auto bad=ais_test::transmit(payload,48000,0,0.025,3,true);
    auto good96=ais_test::transmit(payload,96000,0,0.025,3,false);
    std::vector<Decoder::Message> direct;
    Decoder sync(directCapture,&direct); assert(sync.configure(48000));
    sync.process(bad.data(),bad.size()); assert(direct.empty());
    sync.process(good.data(),good.size()); assert(direct.size()==1);
    Gate gate; Receiver rx(64,8192,2,Gate::hook,&gate);
    struct PollGate {
        Receiver* rx; bool armed=false, called=false;
        static void run(void* p) noexcept { auto& g=*static_cast<PollGate*>(p); if(g.armed) { g.armed=false; g.called=true; g.rx->discontinuity(); } }
    } pollGate{&rx};
    dsdr::core::AisController ui(&rx,nullptr,PollGate::run,&pollGate);
    assert(ui.processedUtc().isEmpty());
    assert(rx.status()==Receiver::Status::Disabled);
    ui.setEnabled(true); assert(rx.status()==Receiver::Status::Stopped);
    rx.start(); assert(rx.status()==Receiver::Status::NoSource);
    rx.setSource(true,32000,0); assert(rx.status()==Receiver::Status::WrongRate);
    rx.setSource(true,48000,20000); assert(rx.status()==Receiver::Status::WrongOffset);
    rx.setSource(true,48000,0,false); assert(rx.status()==Receiver::Status::WrongOffset);
    assert(ui.status()==QStringLiteral("Canale AIS fuori dalla banda disponibile"));
    assert(rx.submit(good.data(),good.size())==Result::Stopped);
    rx.setSource(true,48000,0); assert(rx.status()==Receiver::Status::Active);
    assert(ui.status()==QStringLiteral("Ricezione AIS attiva"));
    submit(rx,bad);
    for(std::size_t at=0;at<good.size();) { auto n=std::min<std::size_t>(137,good.size()-at); assert(rx.submit(good.data()+at,n)==Result::Accepted); at+=n; }
    Receiver::Message m;
    waitFor([&]{return rx.read(m);});
    assert(std::vector<std::uint8_t>(m.payload.begin(),m.payload.begin()+m.size)==direct[0].payload);
    assert(m.type==direct[0].type && m.mmsi==direct[0].mmsi && m.hasPosition);
    assert(std::abs(m.latitude-35.9)<1e-6 && std::abs(m.longitude-14.5)<1e-6);
    assert(!rx.read(m));
    submit(rx,good); waitFor([&]{ui.poll();return ui.frameCount()==1;});
    assert(ui.messageType()==1 && ui.mmsi()=="123456789" && ui.hasPosition());
    assert(!ui.processedUtc().isEmpty());
    pollGate.armed=true; submit(rx,good);
    waitFor([&]{ui.poll();return pollGate.called;});
    assert(ui.frameCount()==1 && ui.mmsi().isEmpty() && ui.processedUtc().isEmpty());
    // Hold worker inside the first old block, queue another; control methods
    // must return and neither block may be attributed to the new generation.
    for(int scenario=0;scenario<4;++scenario) {
        gate.entered=false; gate.block=true;
        submit(rx,good); waitFor([&]{return gate.entered.load();}); submit(rx,good);
        if(scenario==0) { rx.setSource(false,48000,0); rx.setSource(true,48000,0); }
        if(scenario==1) rx.discontinuity();
        if(scenario==2) {
            const auto begin=std::chrono::steady_clock::now(); ui.setEnabled(false); ui.setEnabled(true);
            assert(std::chrono::steady_clock::now()-begin<500ms);
        }
        if(scenario==3) rx.setSource(true,96000,0);
        auto old=rx.generation()-1; assert(rx.submitForGeneration(good.data(),good.size(),old)==Result::Stopped);
        gate.block=false; submit(rx,scenario==3 ? good96 : good);
        waitFor([&]{return rx.read(m);}); assert(m.generation==rx.generation());
        std::this_thread::sleep_for(30ms); assert(!rx.read(m));
        if(scenario==3) rx.setSource(true,48000,0);
    }
    // Previously decoded but unread result becomes stale too.
    for(int scenario=0;scenario<3;++scenario) {
        const auto previous=gate.calls.load(); submit(rx,good);
        const dsdr::dsp::Complex barrier{1,0}; assert(rx.submit(&barrier,1)==Result::Accepted);
        waitFor([&]{return gate.calls.load()>=previous+2;});
        if(scenario==0) { rx.setSource(false,48000,0); rx.setSource(true,48000,0); }
        if(scenario==1) rx.discontinuity();
        if(scenario==2) { ui.setEnabled(false); ui.setEnabled(true); }
        if(scenario<2) {
            assert(rx.readOne(m)==Receiver::ReadResult::Stale);
            assert(rx.readOne(m)==Receiver::ReadResult::Empty);
        } else assert(!rx.read(m));
    }
    // Output queue bounded to two messages, excess counted.
    for(int i=0;i<5;++i) submit(rx,good);
    waitFor([&]{return rx.droppedMessages()>=3;});
    int count=0; while(rx.read(m)) ++count; assert(count==2);
    // Input drop invalidates old partial frame, with recovery on the next one.
    const auto half=good.size()/2;
    const auto before=gate.calls.load();
    assert(rx.submit(good.data(),half)==Result::Accepted);
    assert(rx.submit(nullptr,1)==Result::Invalid);
    assert(rx.submit(good.data()+half,good.size()-half)==Result::Accepted);
    const dsdr::dsp::Complex barrier{1,0};
    assert(rx.submit(&barrier,1)==Result::Accepted); waitFor([&]{return gate.calls.load()>=before+3;});
    assert(!rx.read(m)); submit(rx,good); waitFor([&]{return rx.read(m);});
    assert(rx.stats().droppedBlocks>=1 && rx.workerErrors()==0);
    std::vector<std::uint8_t> type5(53,0); ais_test::setBits(type5,0,6,5); ais_test::setBits(type5,8,30,123456789);
    auto textOnly=ais_test::transmit(type5,48000,0,0,2,false); submit(rx,textOnly);
    waitFor([&]{ui.poll();return ui.messageType()==5;}); assert(!ui.hasPosition());
    rx.setSource(false,48000,0); ui.poll(); assert(!ui.hasPosition() && ui.mmsi().isEmpty() && ui.processedUtc().isEmpty());
    rx.stop(); assert(rx.status()==Receiver::Status::Stopped);
    // A blocked worker fills the input ring. Producer drops immediately and
    // the first subsequently accepted block starts a fresh generation.
    Gate slow; slow.block=true; Receiver saturated(2,8192,4,Gate::hook,&slow);
    saturated.start(); saturated.setSource(true,48000,0); saturated.setEnabled(true);
    submit(saturated,good); waitFor([&]{return slow.entered.load();}); submit(saturated,good);
    const auto began=std::chrono::steady_clock::now();
    assert(saturated.submit(good.data(),good.size())==Result::Full);
    assert(std::chrono::steady_clock::now()-began<500ms);
    slow.block=false;
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(saturated.submit(good.data(),good.size())==Result::Full) {
        assert(std::chrono::steady_clock::now()<deadline); std::this_thread::yield();
    }
    waitFor([&]{return saturated.read(m);}); assert(m.generation==saturated.generation());
    std::this_thread::sleep_for(30ms); assert(!saturated.read(m));
    assert(saturated.stats().droppedBlocks>=1); saturated.stop();
    // The UI consumes at most one batch per poll, leaving the rest for the
    // next event-loop turn even if messages continue arriving.
    Gate burstGate; Receiver burst(128,8192,128,Gate::hook,&burstGate);
    burst.start(); burst.setSource(true,48000,0); burst.setEnabled(true);
    dsdr::core::AisController burstUi(&burst);
    for(int i=0;i<70;++i) submit(burst,good);
    const dsdr::dsp::Complex endMarker{1,0};
    assert(burst.submit(&endMarker,1)==Result::Accepted);
    waitFor([&]{return burstGate.calls.load()>=71;});
    burstUi.poll(); assert(burstUi.frameCount()==dsdr::core::AisController::PollBatch);
    burstUi.poll(); assert(burstUi.frameCount()==70);
    burst.stop();
    std::puts("AIS asynchronous A/B, backlog, generations, bounds and controller: OK");
}
