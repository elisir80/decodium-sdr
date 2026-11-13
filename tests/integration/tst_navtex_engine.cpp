// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DspEngine.h"
#include "core/NavtexController.h"
#include "dsp/navtex/NavtexProtocol.h"
#include <QTest>
#include <algorithm>
#include <cmath>
#include <vector>
using namespace dsdr;
class TestNavtexEngine : public QObject {
    Q_OBJECT
private slots:
    void sidebandMutedAB_data() {
        QTest::addColumn<bool>("lower");
        QTest::newRow("USB") << false;
        QTest::newRow("LSB") << true;
    }
    void sidebandMutedAB() {
        QFETCH(bool,lower);
        constexpr double rate=192000,pi=3.14159265358979323846;
        std::vector<int> tokens(40,-1);
        for(auto c:navtex::navtexEncode("ZCZC EA01\nTEST MESSAGE 123\nNNNN\n")) tokens.push_back(c);
        tokens.insert(tokens.end(),40,-1);
        const auto bits=navtex::codesToBits(navtex::sitorBSlots(tokens));
        std::vector<float> iq; iq.reserve((bits.size()*1920+size_t(rate/2))*2); double phase=0;
        for(size_t i=0;i<bits.size()*1920+size_t(rate/2);++i) {
            const bool active=i<bits.size()*1920;
            if(active) phase+=(lower?-1:1)*2*pi*(1000+(bits[i/1920]?85:-85))/rate;
            iq.push_back(active?float(.5*std::cos(phase)):0.f);
            iq.push_back(active?float(.5*std::sin(phase)):0.f);
        }
        core::NavtexReceiver rx(2048,4096,8); rx.start(); rx.selectChannel(1);
        core::DspEngine engine; dsp::SpscRing<float> ring(32768);
        engine.attachNavtexReceiver(&rx); engine.setSource(&ring,rate,518000,false);
        dsp::ChannelSettings cfg; cfg.mode=lower ? DemodMode::Lsb : DemodMode::Usb;
        // Filter controls are positive audio frequencies for both sidebands;
        // ChannelProcessor mirrors them internally for LSB.
        cfg.filterLowHz=100; cfg.filterHighHz=2500;
        cfg.squelchEnabled=false; cfg.muted=true; engine.addChannel(1,cfg);
        const auto feed=[&](size_t begin,size_t end) {
            for(size_t i=begin;i<end;) {
                auto n=std::min<size_t>(8192,end-i); QCOMPARE(ring.write(iq.data()+i,n),n);
                hal::IqFrame f; f.frameCount=n/2; engine.onIqFrameReady(f); i+=n; QTest::qWait(1);
            }
        };
        core::NavtexReceiver::Message msg; feed(0,iq.size());
        QCOMPARE(rx.readOne(msg),core::NavtexReceiver::ReadResult::Empty);
        rx.setEnabled(true); feed(0,iq.size()); bool got=false;
        QTRY_VERIFY_WITH_TIMEOUT((got=got || rx.readOne(msg)==core::NavtexReceiver::ReadResult::Message),15000);
        QCOMPARE(QByteArray(msg.header.data()),QByteArray("EA01"));
        QVERIFY(QByteArray(msg.text.data()).contains("TEST MESSAGE 123"));
        {
            struct Gate { core::NavtexReceiver* rx; bool fired=false;
                static void run(void* p) noexcept { auto& g=*static_cast<Gate*>(p); if(!g.fired) {g.fired=true;g.rx->discontinuity();} }
            } gate{&rx};
            core::NavtexController controller(&rx,nullptr,Gate::run,&gate);
            feed(0,iq.size()); QTRY_VERIFY_WITH_TIMEOUT((controller.poll(),gate.fired),15000);
            QCOMPARE(controller.messageCount(),qulonglong(0)); QVERIFY(controller.header().isEmpty());
            feed(0,iq.size()); QTRY_COMPARE_WITH_TIMEOUT((controller.poll(),controller.messageCount()),qulonglong(1),15000);
            QCOMPARE(controller.header(),QStringLiteral("EA01"));
        }
        const auto split=iq.size()/2/2*2;
        feed(0,split); hal::IqFrame gap; gap.droppedFrames=128; engine.onIqFrameReady(gap);
        feed(split+256,iq.size()); QTest::qWait(250);
        QCOMPARE(rx.readOne(msg),core::NavtexReceiver::ReadResult::Empty);
        feed(0,iq.size()); got=false;
        QTRY_VERIFY_WITH_TIMEOUT((got=got || rx.readOne(msg)==core::NavtexReceiver::ReadResult::Message),15000);
        QCOMPARE(QByteArray(msg.header.data()),QByteArray("EA01"));
        std::vector<float> audio(engine.audioRing()->available()); QVERIFY(!audio.empty());
        QCOMPARE(engine.audioRing()->read(audio.data(),audio.size()),audio.size());
        QVERIFY(std::all_of(audio.begin(),audio.end(),[](float v){return v==0;}));
        engine.clearSource(); engine.attachNavtexReceiver(nullptr); rx.stop();
    }
};
QTEST_GUILESS_MAIN(TestNavtexEngine)
#include "tst_navtex_engine.moc"
