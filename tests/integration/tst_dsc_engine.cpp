// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DspEngine.h"
#include "core/DscController.h"
#include <QTest>
#include <array>
#include <cmath>
#include <vector>
using namespace dsdr;
class TestDscEngine : public QObject {
    Q_OBJECT
private slots:
    void addressedFramesRejectDamageAndRecover() {
        const std::vector<int> body={120,12,34,56,78,90,112,23,21,23,45,60,110,
                                     12,34,56,78,90,103,99,99,99,99,99,88,88,109};
        const auto makeBits=[&](bool corrupt) {
            std::vector<int> symbols(2*body.size()+22,126);
            for(int j=0;j<12;++j) symbols[j]=j%2?111-j/2:125;
            for(int j=13;j<16;j+=2) symbols[j]=111-j/2;
            std::vector<int> msg={body[0],body[0]};
            msg.insert(msg.end(),body.begin()+1,body.end());
            int parity=122; for(int value:body) parity^=value;
            msg.push_back(122); msg.push_back(parity);
            if(corrupt) msg[3]=(msg[3]+1)%100; // Valid symbols, wrong frame ECC.
            for(size_t i=0;i<msg.size();++i) { symbols[12+2*i]=msg[i]; symbols[17+2*i]=msg[i]; }
            symbols[symbols.size()-4]=122; symbols[symbols.size()-2]=122;
            std::vector<bool> bits;
            for(int i=0;i<200;++i) bits.push_back((i+1)%2);
            for(int symbol:symbols) {
                int zeros=7;
                for(int k=0;k<7;++k) { const int bit=(symbol>>k)&1; bits.push_back(bit); zeros-=bit; }
                for(int k=2;k>=0;--k) bits.push_back((zeros>>k)&1);
            }
            return bits;
        };
        const auto good=makeBits(false), bad=makeBits(true);
        dsc_rx::Decoder decoder; int count=0;
        for(bool bit:bad) if(decoder.push(bit)) ++count;
        QCOMPARE(count,0);
        decoder.reset();
        for(size_t i=0;i<good.size()/2;++i) if(decoder.push(good[i])) ++count;
        QCOMPARE(count,0);
        decoder.reset();
        for(bool bit:good) if(auto frame=decoder.push(bit)) {
            ++count; QCOMPARE(frame->bodyLength,int(body.size())); QCOMPARE(frame->eos,122);
            QVERIFY(std::equal(body.begin(),body.end(),frame->fullBody.begin()));
        }
        QCOMPARE(count,1);
    }
    void sidebandMuteAB_data() {
        QTest::addColumn<bool>("lower");
        QTest::newRow("USB") << false;
        QTest::newRow("LSB") << true;
    }
    void sidebandMuteAB() {
        QFETCH(bool,lower);
        constexpr double pi=3.14159265358979323846, rate=192000;
        const std::array<int,15> body={112,23,21,23,45,60,103,15,5,10,1,18,12,34,109};
        std::vector<int> symbols(52,126);
        for(int j=0;j<12;++j) symbols[j]=j%2?111-j/2:125;
        for(int j=13;j<16;j+=2) symbols[j]=111-j/2;
        std::vector<int> msg={body[0],body[0]};
        msg.insert(msg.end(),body.begin()+1,body.end());
        int parity=127; for(int v:body) parity^=v;
        msg.push_back(127); msg.push_back(parity);
        for(int j=0;j<18;++j) { symbols[12+2*j]=msg[j]; symbols[17+2*j]=msg[j]; }
        symbols[48]=127; symbols[50]=127;
        std::vector<int> bits; for(int i=0;i<200;++i) bits.push_back((i+1)%2);
        for(int s:symbols) {
            int zeros=7; for(int k=0;k<7;++k) { int b=(s>>k)&1; bits.push_back(b); zeros-=b; }
            for(int k=2;k>=0;--k) bits.push_back((zeros>>k)&1);
        }
        core::DscReceiver rx(1024,4096,32); rx.start(); rx.selectChannel(1);
        core::DspEngine engine; dsp::SpscRing<float> ring(32768);
        engine.attachDscReceiver(&rx); engine.setSource(&ring,rate,8414500,false);
        dsp::ChannelSettings cfg; cfg.mode=lower ? DemodMode::Lsb : DemodMode::Usb; cfg.filterLowHz=100; cfg.filterHighHz=3000;
        cfg.squelchEnabled=false; cfg.muted=true; engine.addChannel(1,cfg);
        std::vector<float> iq; iq.reserve((bits.size()*1920+rate)*2);
        double phase=0;
        for(size_t i=0;i<bits.size()*1920+size_t(rate/2);++i) {
            const bool active=i<bits.size()*1920;
            if(active) phase+=(lower?-1:1)*2*pi*(1700+(bits[i/1920]?-85:85))/rate;
            iq.push_back(active?float(.5*std::cos(phase)):0);
            iq.push_back(active?float(.5*std::sin(phase)):0);
        }
        const auto feedRange=[&](size_t first,size_t last) {
            for(size_t i=first;i<last;) {
                const auto n=std::min<size_t>(8192,last-i);
                QCOMPARE(ring.write(iq.data()+i,n),n);
                hal::IqFrame f; f.frameCount=n/2; engine.onIqFrameReady(f); i+=n; QTest::qWait(1);
            }
        };
        const auto feed=[&] { feedRange(0,iq.size()); };
        core::DscReceiver::Message out; feed();
        QCOMPARE(rx.readOne(out),core::DscReceiver::ReadResult::Empty);
        rx.setEnabled(true); feed(); bool got=false;
        QTRY_VERIFY_WITH_TIMEOUT((got=got || rx.readOne(out)==core::DscReceiver::ReadResult::Message),10000);
        QVERIFY(out.call.body==body);
        {
        struct PollGate {
            core::DscReceiver* receiver; bool armed=false,called=false;
            static void run(void* p) noexcept { auto& g=*static_cast<PollGate*>(p); if(g.armed) { g.armed=false; g.called=true; g.receiver->discontinuity(); } }
        } gate{&rx};
        core::DscController controller(&rx,nullptr,PollGate::run,&gate);
        gate.armed=true; feed();
        QTRY_VERIFY_WITH_TIMEOUT((controller.poll(),gate.called),10000);
        QVERIFY(controller.lastMmsi().isEmpty()); QVERIFY(controller.lastCategory().isEmpty());
        QCOMPARE(controller.messageCount(),qulonglong(0));
        feed(); QTRY_COMPARE_WITH_TIMEOUT((controller.poll(),controller.messageCount()),qulonglong(1),10000);
        QCOMPARE(controller.lastMmsi(),QStringLiteral("232123456"));
        }
        // Invalidate a call in progress, and verify the next complete one.
        const size_t split=(bits.size()*1920/2)*2;
        feedRange(0,split);
        hal::IqFrame gap; gap.droppedFrames=128; engine.onIqFrameReady(gap);
        feedRange(split+256,iq.size()); QTest::qWait(250);
        QCOMPARE(rx.readOne(out),core::DscReceiver::ReadResult::Empty);
        feed(); got=false;
        QTRY_VERIFY_WITH_TIMEOUT((got=got || rx.readOne(out)==core::DscReceiver::ReadResult::Message),10000);
        QVERIFY(out.call.body==body);
        std::vector<float> audio(engine.audioRing()->available()); QVERIFY(!audio.empty());
        QCOMPARE(engine.audioRing()->read(audio.data(),audio.size()),audio.size());
        QVERIFY(std::all_of(audio.begin(),audio.end(),[](float v){return v==0;}));
        engine.clearSource(); engine.attachDscReceiver(nullptr); rx.stop();
    }
};
QTEST_GUILESS_MAIN(TestDscEngine)
#include "tst_dsc_engine.moc"
