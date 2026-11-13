// SPDX-License-Identifier: GPL-3.0-or-later
#include "../standalone/pocsag/async_fixture.h"
#include "core/DspEngine.h"
#include <QTest>
#include <algorithm>
using namespace dsdr;
class TestPocsagEngine : public QObject {
    Q_OBJECT
private slots:
    void audioUnchangedAB() {
        core::PocsagReceiver receiver(1024,1024,16); QVERIFY(receiver.startManaged());
        QVERIFY(receiver.configureManaged(48000,2400,2000)); receiver.setManagedEnabled(true);
        core::DspEngine baseline, withDecoder;
        dsp::SpscRing<float> a(32768),b(32768);
        baseline.setSource(&a,48000,100000000,false);
        withDecoder.setSource(&b,48000,100000000,false); withDecoder.attachPocsagReceiver(&receiver);
        dsp::ChannelSettings cfg; cfg.mode=DemodMode::Nfm; cfg.filterLowHz=-6000; cfg.filterHighHz=6000;
        cfg.squelchEnabled=false; cfg.volume=0.7f;
        baseline.addChannel(1,cfg); withDecoder.addChannel(1,cfg);
        const auto iq=generate(48000,2400,2600,0.01,7);
        for(std::size_t pos=0;pos<iq.size();) {
            const auto n=std::min<std::size_t>(1024,iq.size()-pos); float data[2048];
            for(std::size_t i=0;i<n;++i) { data[2*i]=iq[pos+i].real(); data[2*i+1]=iq[pos+i].imag(); }
            QCOMPARE(a.write(data,n*2),n*2); QCOMPARE(b.write(data,n*2),n*2);
            hal::IqFrame f; f.frameCount=n; baseline.onIqFrameReady(f); withDecoder.onIqFrameReady(f);
            pos+=n; QTest::qWait(1);
        }
        QTRY_COMPARE_WITH_TIMEOUT(a.available(),std::size_t(0),5000);
        QTRY_COMPARE_WITH_TIMEOUT(b.available(),std::size_t(0),5000);
        const auto count=baseline.audioRing()->available(); QVERIFY(count>0);
        QCOMPARE(withDecoder.audioRing()->available(),count);
        std::vector<float> outA(count),outB(count);
        QCOMPARE(baseline.audioRing()->read(outA.data(),count),count);
        QCOMPARE(withDecoder.audioRing()->read(outB.data(),count),count);
        QVERIFY(std::equal(outA.begin(),outA.end(),outB.begin()));
        baseline.clearSource(); withDecoder.clearSource(); withDecoder.attachPocsagReceiver(nullptr); receiver.stop(false);
    }
    void liveIqAB() {
        core::PocsagReceiver receiver(1024,1024,16);
        QVERIFY(receiver.startManaged());
        core::DspEngine engine;
        dsp::SpscRing<float> ring(32768);
        dsp::SpscRing<float> secondRing(32768);
        auto *source=&ring;
        engine.attachPocsagReceiver(&receiver);
        engine.setSource(&ring,48000,100000000,false);
        QVERIFY(receiver.configureManaged(48000,2400,2000));
        const auto iq=generate(48000,2400,2600,0.01,7);
        const auto feedRange=[&](std::size_t begin, std::size_t end) {
            float block[2048];
            for(std::size_t pos=begin;pos<end;) {
                const auto n=std::min<std::size_t>(1024,end-pos);
                for(std::size_t i=0;i<n;++i) { block[2*i]=iq[pos+i].real(); block[2*i+1]=iq[pos+i].imag(); }
                QCOMPARE(source->write(block,2*n),2*n);
                hal::IqFrame frame; frame.frameCount=n; engine.onIqFrameReady(frame);
                pos+=n;
                QTest::qWait(1);
            }
        };
        const auto feed=[&] { feedRange(0,iq.size()); };
        core::PocsagReceiver::Message msg;
        feed(); QVERIFY(!receiver.read(msg));
        receiver.setManagedEnabled(true);
        feed();
        bool received=false;
        QTRY_VERIFY_WITH_TIMEOUT((received = received || receiver.read(msg)),5000);
        QCOMPARE(msg.address,std::uint32_t(1234563));
        QCOMPARE(QString::fromLatin1(msg.text.data(),qsizetype(msg.size)),QStringLiteral("HELLO PAGER"));
        const auto split=std::size_t(850)*48000/2400;
        feedRange(0,split);
        hal::IqFrame gap; gap.droppedFrames=80; engine.onIqFrameReady(gap);
        feedRange(split+80,iq.size());
        QTest::qWait(100); QVERIFY(!receiver.read(msg));
        feed(); received=false;
        QTRY_VERIFY_WITH_TIMEOUT((received = received || receiver.read(msg)),5000);
        // A replacement ring has its own absolute position epoch.
        engine.setSource(&secondRing,48000,100000000,false); source=&secondRing;
        feed(); received=false;
        QTRY_VERIFY_WITH_TIMEOUT((received = received || receiver.read(msg)),5000);
        receiver.setManagedEnabled(false);
        feed(); QVERIFY(!receiver.read(msg));
        engine.clearSource();
        engine.attachPocsagReceiver(nullptr);
        receiver.stop(false);
    }
};
QTEST_GUILESS_MAIN(TestPocsagEngine)
#include "tst_pocsag_engine.moc"
