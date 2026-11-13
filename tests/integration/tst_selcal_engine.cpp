// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DspEngine.h"
#include "core/SelcalController.h"
#include <QTest>
#include <cmath>
#include <vector>
using namespace dsdr;
class TestSelcalEngine : public QObject {
    Q_OBJECT
private slots:
    void usbAB() {
        constexpr double pi=3.14159265358979323846, rate=192000;
        core::SelcalReceiver receiver(1024,4096,32); receiver.start(); receiver.selectChannel(1);
        core::DspEngine engine; dsp::SpscRing<float> ring(32768);
        engine.attachSelcalReceiver(&receiver); engine.setSource(&ring,rate,118000000,false);
        dsp::ChannelSettings cfg; cfg.mode=DemodMode::Usb; cfg.filterLowHz=100; cfg.filterHighHz=3000;
        cfg.squelchEnabled=false; cfg.muted=true; engine.addChannel(1,cfg);
        std::vector<float> iq; iq.reserve(192000*3*2);
        for(int i=0;i<int(rate*3);++i) {
            const double t=i/rate; double re=0,im=0;
            const auto tone=[&](double hz) { re+=.2*std::cos(2*pi*hz*t); im+=.2*std::sin(2*pi*hz*t); };
            if(t>=.2 && t<1.2) { tone(312.6); tone(977.2); }
            if(t>=1.4 && t<2.4) { tone(384.6); tone(1083.9); }
            iq.push_back(float(re)); iq.push_back(float(im));
        }
        const auto feed=[&] {
            for(std::size_t at=0;at<iq.size();) {
                const auto n=std::min<std::size_t>(8192,iq.size()-at);
                QCOMPARE(ring.write(iq.data()+at,n),n); hal::IqFrame f; f.frameCount=n/2; engine.onIqFrameReady(f);
                at+=n; QTest::qWait(1);
            }
        };
        core::SelcalReceiver::Message msg; feed(); QCOMPARE(receiver.readOne(msg),core::SelcalReceiver::ReadResult::Empty);
        receiver.setEnabled(true); feed(); bool got=false;
        QTRY_VERIFY_WITH_TIMEOUT((got=got || receiver.readOne(msg)==core::SelcalReceiver::ReadResult::Message),5000);
        QVERIFY((msg.code==std::array<char,5>{'A','M','-','C','P'}));
        {
            struct Gate { core::SelcalReceiver* rx; bool fired=false;
                static void run(void* p) noexcept { auto& g=*static_cast<Gate*>(p); if(!g.fired) {g.fired=true;g.rx->discontinuity();} }
            } gate{&receiver};
            core::SelcalController controller(&receiver,nullptr,Gate::run,&gate);
            feed(); QTRY_VERIFY_WITH_TIMEOUT((controller.poll(),gate.fired),5000);
            QCOMPARE(controller.messageCount(),qulonglong(0)); QVERIFY(controller.lastCode().isEmpty());
            feed(); QTRY_COMPARE_WITH_TIMEOUT((controller.poll(),controller.messageCount()),qulonglong(1),5000);
            QCOMPARE(controller.lastCode(),QStringLiteral("AM-CP"));
        }
        // Mirror the analytic signal and passband to exercise LSB as well.
        cfg.mode=DemodMode::Lsb; cfg.filterLowHz=100; cfg.filterHighHz=3000; engine.updateChannel(1,cfg);
        for(std::size_t i=1;i<iq.size();i+=2) iq[i]=-iq[i];
        feed(); got=false;
        QTRY_VERIFY_WITH_TIMEOUT((got=got || receiver.readOne(msg)==core::SelcalReceiver::ReadResult::Message),5000);
        QVERIFY((msg.code==std::array<char,5>{'A','M','-','C','P'}));
        engine.clearSource(); engine.attachSelcalReceiver(nullptr); receiver.stop();
    }
    void amMuteAB() {
        constexpr double pi=3.14159265358979323846, rate=192000;
        core::SelcalReceiver receiver(1024,4096,32); receiver.start(); receiver.selectChannel(1);
        core::DspEngine engine; dsp::SpscRing<float> ring(32768);
        engine.attachSelcalReceiver(&receiver); engine.setSource(&ring,rate,118000000,false);
        dsp::ChannelSettings cfg; cfg.mode=DemodMode::Am; cfg.filterLowHz=-5000; cfg.filterHighHz=5000;
        cfg.squelchEnabled=false; cfg.volume=.7f; engine.addChannel(1,cfg);
        std::vector<float> iq; iq.reserve(192000*3*2);
        for(int i=0;i<int(rate*3);++i) {
            const double t=i/rate; double tone=0;
            if(t>=.2 && t<1.2) tone=.22*(std::sin(2*pi*312.6*t)+std::sin(2*pi*977.2*t));
            if(t>=1.4 && t<2.4) tone=.22*(std::sin(2*pi*384.6*t)+std::sin(2*pi*1083.9*t));
            iq.push_back(float(.55*(1+tone))); iq.push_back(0);
        }
        const auto feedRange=[&](std::size_t begin,std::size_t end) {
            for(std::size_t at=begin;at<end;) {
                const auto n=std::min<std::size_t>(8192,end-at);
                QCOMPARE(ring.write(iq.data()+at,n),n); hal::IqFrame f; f.frameCount=n/2; engine.onIqFrameReady(f);
                at+=n; QTest::qWait(1);
            }
        };
        const auto feed=[&] { feedRange(0,iq.size()); };
        core::SelcalReceiver::Message msg; feed();
        QCOMPARE(receiver.readOne(msg),core::SelcalReceiver::ReadResult::Empty);
        receiver.setEnabled(true); feed();
        bool got=false; QTRY_VERIFY_WITH_TIMEOUT((got=got || receiver.readOne(msg)==core::SelcalReceiver::ReadResult::Message),5000);
        QVERIFY((msg.code==std::array<char,5>{'A','M','-','C','P'}));
        const auto split=std::size_t(rate*1.3)*2;
        feedRange(0,split); hal::IqFrame gap; gap.droppedFrames=128; engine.onIqFrameReady(gap);
        feedRange(split+256,iq.size()); QTest::qWait(200);
        QCOMPARE(receiver.readOne(msg),core::SelcalReceiver::ReadResult::Empty);
        feed(); got=false; QTRY_VERIFY_WITH_TIMEOUT((got=got || receiver.readOne(msg)==core::SelcalReceiver::ReadResult::Message),5000);
        QVERIFY((msg.code==std::array<char,5>{'A','M','-','C','P'}));
        engine.audioRing()->clear(); cfg.muted=true; engine.updateChannel(1,cfg); feed(); got=false;
        QTRY_VERIFY_WITH_TIMEOUT((got=got || receiver.readOne(msg)==core::SelcalReceiver::ReadResult::Message),5000);
        QVERIFY((msg.code==std::array<char,5>{'A','M','-','C','P'}));
        std::vector<float> audio(engine.audioRing()->available()); QVERIFY(!audio.empty());
        QCOMPARE(engine.audioRing()->read(audio.data(),audio.size()),audio.size());
        QVERIFY(std::all_of(audio.begin(),audio.end(),[](float v){return v==0;}));
        engine.clearSource(); engine.attachSelcalReceiver(nullptr); receiver.stop();
    }
};
QTEST_GUILESS_MAIN(TestSelcalEngine)
#include "tst_selcal_engine.moc"
