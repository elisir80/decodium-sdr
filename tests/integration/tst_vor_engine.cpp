// SPDX-License-Identifier: GPL-3.0-or-later
#define main vor_fixture_main
#include "../standalone/vor_engine/test.cpp"
#undef main
#include "core/DspEngine.h"
#include "core/VorEngineTap.h"
#include <QTest>
using namespace dsdr;
class TestVorEngine : public QObject {
    Q_OBJECT
private slots:
    void highRateConfigurationAB() {
        core::VorAsyncReceiver receiver;
        core::VorEngineTap tap(receiver);
        QVERIFY(tap.configure(960000,7000));
        QVERIFY(tap.configure(2000000,7000));
        QVERIFY(tap.configure(2048000,7000));
        QVERIFY(!tap.configure(2048001,7000));
        QVERIFY(tap.configure(960000,7000));
    }
    void inputAB_data() {
        QTest::addColumn<unsigned>("rate");
        QTest::newRow("192k") << 192000u;
        QTest::newRow("200k") << 200000u;
        QTest::newRow("960k") << 960000u;
        QTest::newRow("2M") << 2000000u;
        QTest::newRow("2048k") << 2048000u;
    }
    void inputAB() {
        QFETCH(unsigned,rate);
        core::VorAsyncReceiver receiver(1024,512);
        QVERIFY(receiver.configure(core::VorAsyncReceiver::Input::FilteredIq,48000)); QVERIFY(receiver.start());
        core::DspEngine engine; dsp::SpscRing<float> ring(32768);
        engine.attachVorReceiver(&receiver); engine.setSource(&ring,rate,110000000,false);
        const auto iq=signal(rate,90,7000,true);
        const auto feedRange=[&](std::size_t begin,std::size_t end) {
            float data[2048];
            for(std::size_t at=begin;at<end;) {
                const auto n=std::min<std::size_t>(1024,end-at);
                for(std::size_t i=0;i<n;++i) { data[2*i]=iq[at+i].real(); data[2*i+1]=iq[at+i].imag(); }
                QCOMPARE(ring.write(data,n*2),n*2); hal::IqFrame f; f.frameCount=n; engine.onIqFrameReady(f);
                at+=n; QTest::qWait(1);
            }
        };
        const auto feed=[&] { feedRange(0,iq.size()); };
        core::VorAsyncReceiver::Result r; feed(); QVERIFY(!receiver.read(r));
        engine.setVorConfiguration(true,110007000); feed();
        bool got=false; QTRY_VERIFY_WITH_TIMEOUT((got=got || receiver.read(r)),5000);
        QVERIFY(r.value.valid); QVERIFY(std::abs(std::remainder(r.value.radialDegrees-90.,360.))<3.);
        if(rate==200000) {
            feedRange(0,rate/2);
            const auto generation=receiver.generation();
            float broken[2048]{};
            broken[1000]=std::numeric_limits<float>::quiet_NaN();
            QCOMPARE(ring.write(broken,2048),std::size_t(2048));
            hal::IqFrame damaged; damaged.frameCount=1024; engine.onIqFrameReady(damaged);
            QVERIFY(receiver.generation()!=generation);
            QVERIFY(!receiver.read(r));
            feed(); got=false; QTRY_VERIFY_WITH_TIMEOUT((got=got || receiver.read(r)),5000);
            QVERIFY(r.value.valid); QVERIFY(std::abs(std::remainder(r.value.radialDegrees-90.,360.))<3.);
        }
        feedRange(0,iq.size()/2); hal::IqFrame gap; gap.droppedFrames=128; engine.onIqFrameReady(gap);
        feedRange(iq.size()/2+128,iq.size()/2+rate/2); QTest::qWait(150); QVERIFY(!receiver.read(r));
        feed(); got=false; QTRY_VERIFY_WITH_TIMEOUT((got=got || receiver.read(r)),5000);
        QVERIFY(r.value.valid); QVERIFY(std::abs(std::remainder(r.value.radialDegrees-90.,360.))<3.);
        engine.setVorConfiguration(false,110007000); feed(); QVERIFY(!receiver.read(r));
        engine.clearSource(); engine.detachVorTap(); engine.clearVorReceiver(); receiver.stop();
    }
};
QTEST_GUILESS_MAIN(TestVorEngine)
#include "tst_vor_engine.moc"
